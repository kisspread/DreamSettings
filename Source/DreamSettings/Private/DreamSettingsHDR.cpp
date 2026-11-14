#include "DreamSettingsHDR.h"
#include "DreamSettingsDisplay.h"
#include "DreamSettingsSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "HDRHelper.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
namespace
{
    const FName EnableId(TEXT("Display.HDR")),CalibrationId(TEXT("Display.HDRCalibration")),PeakId(TEXT("Display.HDRPeakNits")),WhiteId(TEXT("Display.HDRPaperWhite"));
    FDreamSettingsResult HDRUnavailable(const FText& Text) { return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,Text); }
    const TCHAR* VariableName(FName Id)
    {
        if (Id==EnableId) return TEXT("r.HDR.EnableHDROutput");
        if (Id==CalibrationId) return TEXT("r.HDR.Display.OverrideOSMaxLuminance");
        if (Id==PeakId) return TEXT("r.HDR.Display.MaxLuminance");
        return TEXT("r.HDR.PaperWhite");
    }
    class FDreamHDRProvider final : public IDreamSettingProvider
    {
    public:
        FDreamHDRProvider(UGameInstance* InInstance,const FDreamSettingDefinition& InDefinition):Instance(InInstance),Definition(InDefinition) {}
        FDreamSettingsResult Context() const
        {
            if (!GEngine || !GEngine->GetGameUserSettings()) return HDRUnavailable(LOCTEXT("HDRNoSettings","HDR settings are unavailable."));
            const auto* World=Instance.IsValid() ? Instance->GetWorld() : nullptr;
            if (!World || World->WorldType!=EWorldType::Game) return HDRUnavailable(LOCTEXT("HDRStandalone","Use Standalone Game to change HDR settings."));
            return {};
        }
        FDreamSettingsResult Policy() const
        {
            auto* Variable=IConsoleManager::Get().FindConsoleVariable(VariableName(Definition.Id));
            const auto Priority=Definition.Id==EnableId ? ECVF_SetByGameSetting : ECVF_SetByCode;
            if (!Variable) return HDRUnavailable(LOCTEXT("HDRMissingVariable","This engine does not provide the required HDR control."));
            if (Variable->TestFlags(ECVF_ReadOnly) || (Variable->GetFlags() & ECVF_SetByMask)>Priority)
                return HDRUnavailable(LOCTEXT("HDRPolicy","A higher-priority engine policy controls this HDR setting."));
            return {};
        }
        FDreamSettingsResult Availability() const override
        {
            auto Result=Context(); if (!Result.IsOk()) return Result;
            Result=Policy(); if (!Result.IsOk()) return Result;
            auto* Settings=GEngine->GetGameUserSettings();
            // A missing display must not prevent the user from turning an existing request off.
            if (Definition.Id==EnableId && Settings->IsHDREnabled()) return {};
            const auto& C=Instance->GetSubsystem<UDreamSettingsSubsystem>()->GetDisplayCapabilities();
            if (!C.HDRAllowed) return HDRUnavailable(LOCTEXT("HDRNotAllowed","HDR output is not enabled in this game."));
            if (!C.HasWindow || !C.MonitorCapabilitiesKnown || !C.PlatformSupportsHDR || !C.CurrentMonitorSupportsHDR)
                return HDRUnavailable(LOCTEXT("HDRMonitorUnavailable","HDR is unavailable for this window. Check Windows HDR and display settings."));
            if (Definition.Id!=EnableId && !UDreamSettingsDisplayLibrary::IsHDRPresentationActive(C))
                return HDRUnavailable(LOCTEXT("HDRConfirmFirst","Enable HDR and keep the display change before adjusting HDR brightness."));
            return {};
        }
        FDreamSettingsResult Read(FDreamSettingValue& Out) const override
        {
            // A launch/project policy can force the native request off. Mark it unreadable for
            // persistence so saving unrelated settings does not erase the user's stored HDR choice.
            if (Definition.Id==EnableId && !IsHDRAllowed()) return HDRUnavailable(LOCTEXT("HDRNotAllowed","HDR output is not enabled in this game."));
            return ReadNative(Out);
        }
        FDreamSettingsResult ReadNative(FDreamSettingValue& Out) const
        {
            if (!GEngine || !GEngine->GetGameUserSettings()) return HDRUnavailable(LOCTEXT("HDRNoSettings","HDR settings are unavailable."));
            const auto* S=GEngine->GetGameUserSettings();
            if (Definition.Id==EnableId) Out=S->IsHDREnabled() ? 1 : 0;
            else if (Definition.Id==CalibrationId) Out=S->IsHDRCalibrationUsed() ? 1 : 0;
            else if (Definition.Id==PeakId) Out=S->GetMaximumHDRDisplayNits();
            else Out=S->GetHDRPaperWhiteNits();
            return {};
        }
        FDreamSettingsResult Write(const FDreamSettingValue& Value) override
        {
            FDreamSettingValue Normalized; auto Result=Definition.Normalize(Value,Normalized); if (!Result.IsOk()) return Result;
            Result=Context(); if (!Result.IsOk()) return Result;
            Result=Policy(); if (!Result.IsOk()) return Result;
            auto* S=GEngine->GetGameUserSettings();
            auto* Variable=IConsoleManager::Get().FindConsoleVariable(VariableName(Definition.Id));
            if (Definition.Id==EnableId)
            {
                if (Normalized.Number!=0 && (!S->SupportsHDRDisplayOutput() || !IsHDRAllowed()))
                    return HDRUnavailable(LOCTEXT("HDRNotAllowed","HDR output is not enabled in this game."));
                // SetHDREnabled uses GameSetting priority and does not save or change window mode.
                // Restoring a saved request on an unavailable monitor is safe: UE renders SDR there.
                // UI availability gates new requests; confirmation validates actual presentation.
                S->SetHDREnabled(Normalized.Number!=0);
            }
            else
            {
                if (Definition.Id==CalibrationId) S->SetHDRCalibrationUsed(Normalized.Number!=0);
                else if (Definition.Id==PeakId) S->SetMaximumHDRDisplayNits(FMath::RoundToInt(Normalized.Number));
                else S->SetHDRPaperWhiteNits(Normalized.Number);
                // These native setters only modify members. Change the actual renderer input as well
                // so the engine sink becomes dirty; setting an unchanged enable flag can be skipped by history.
                const double Effective=Definition.Id==PeakId && !::IsHDREnabled() ? 0 : Normalized.Number;
                Variable->Set(static_cast<float>(Effective),ECVF_SetByCode);
            }
            IConsoleManager::Get().CallAllConsoleVariableSinks();
            FDreamSettingValue Actual; Result=ReadNative(Actual); if (!Result.IsOk()) return Result;
            const double Effective=Definition.Id==PeakId && !::IsHDREnabled() ? 0 : Normalized.Number;
            if (!Actual.Equals(Normalized) || !FMath::IsNearlyEqual(static_cast<double>(Variable->GetFloat()),Effective,1.e-4))
                return FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("HDRReadback","The engine did not accept the requested HDR setting."));
            return {};
        }
        FDreamSettingsResult ValidateApplied(const FDreamSettingValue& Value) const override
        {
            if (Definition.Id!=EnableId) return {};
            const auto C=UDreamSettingsDisplayLibrary::QueryCapabilities(Instance.Get());
            const bool Actual=UDreamSettingsDisplayLibrary::IsHDRPresentationActive(C);
            if ((Value.Number!=0 && !Actual) || (Value.Number==0 && C.HDROutputEnabled))
                return FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("HDRPresentationRejected","The window has not accepted the requested HDR output. Previous settings will be restored."));
            return {};
        }
    private:
        TWeakObjectPtr<UGameInstance> Instance;
        FDreamSettingDefinition Definition;
    };
}
TArray<FDreamSettingDefinition> DreamHDRDefinitions()
{
    TArray<FDreamSettingDefinition> Out;
    FDreamSettingDefinition Enable; Enable.Id=EnableId; Enable.Category=TEXT("Display"); Enable.Order=60;
    Enable.Kind=EDreamSettingKind::Toggle; Enable.Step=1; Enable.Default=0; Enable.bRequiresConfirmation=true; Enable.bRetryUnavailable=true;
    Enable.Label=LOCTEXT("HDREnable","HDR output");
    Enable.Description=LOCTEXT("HDREnableHelp","Use HDR on a supported display with Windows HDR enabled. Keep the change within 15 seconds or the previous output is restored.");
    Out.Add(Enable);
    FDreamSettingDefinition Calibration; Calibration.Id=CalibrationId; Calibration.Category=Enable.Category; Calibration.Order=61;
    Calibration.Kind=EDreamSettingKind::Toggle; Calibration.Step=1; Calibration.Default=0; Calibration.bPreview=true;
    Calibration.Label=LOCTEXT("HDRCalibration","Custom HDR peak brightness");
    Calibration.Description=LOCTEXT("HDRCalibrationHelp","Override the display's reported peak brightness with your calibrated value. Off uses the system's display information.");
    Calibration.Dependencies={EnableId};
    Calibration.EditCondition=[](const FDreamSettingsValues& V) { return V.FindRef(EnableId).Number!=0 ? FDreamSettingsResult{} : HDRUnavailable(LOCTEXT("HDRConfirmFirst","Enable HDR and keep the display change before adjusting HDR brightness.")); };
    Out.Add(Calibration);
    FDreamSettingDefinition Peak; Peak.Id=PeakId; Peak.Category=Enable.Category; Peak.Order=62; Peak.Min=100; Peak.Max=4000; Peak.Step=10; Peak.Default=1000; Peak.bPreview=true;
Peak.Presentation=EDreamSettingPresentation::HDRCalibration|EDreamSettingPresentation::HDRPeak;
    Peak.Label=LOCTEXT("HDRPeak","HDR peak brightness (nits)");
    Peak.Description=LOCTEXT("HDRPeakHelp","Set the maximum highlight brightness for your display when custom calibration is enabled.");
    Peak.Dependencies={EnableId,CalibrationId};
    Peak.EditCondition=[](const FDreamSettingsValues& V) { return V.FindRef(EnableId).Number!=0 && V.FindRef(CalibrationId).Number!=0 ? FDreamSettingsResult{} : HDRUnavailable(LOCTEXT("HDRCalibrationFirst","Enable HDR and custom peak brightness first.")); };
    Out.Add(Peak);
    FDreamSettingDefinition White; White.Id=WhiteId; White.Category=Enable.Category; White.Order=63; White.Min=80; White.Max=400; White.Step=1; White.Default=203; White.bPreview=true;
White.Presentation=EDreamSettingPresentation::HDRCalibration;
    White.Label=LOCTEXT("HDRPaperWhite","HDR paper white (nits)");
    White.Description=LOCTEXT("HDRPaperWhiteHelp","Adjust the brightness of ordinary white surfaces. Peak brightness controls highlights separately.");
    White.Dependencies={EnableId}; White.EditCondition=Calibration.EditCondition; Out.Add(White);
    return Out;
}
FDreamSettingsResult RegisterDreamHDRSettings(FDreamSettingsRegistry& Registry,UGameInstance* Instance)
{
    for (const auto& Definition:DreamHDRDefinitions())
    {
        auto Result=Registry.Register(Definition,MakeShared<FDreamHDRProvider>(Instance,Definition));
        if (!Result.IsOk()) return Result;
    }
    return {};
}
#undef LOCTEXT_NAMESPACE
