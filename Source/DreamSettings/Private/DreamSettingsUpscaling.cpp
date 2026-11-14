#include "DreamSettingsUpscaling.h"
#include "DreamSettingsUpscaler.h"
#include "DreamSettingsProvider.h"
#include "Features/IModularFeatures.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
FText DreamUpscalerQuality::Label(const FString& Id)
{
    if (Id == TEXT("native")) return LOCTEXT("UpscaleNative", "Native AA");
    if (Id == TEXT("ultraqualityplus")) return LOCTEXT("UpscaleUltraQualityPlus", "Ultra quality+");
    if (Id == TEXT("ultraquality")) return LOCTEXT("UpscaleUltraQuality", "Ultra quality");
    if (Id == TEXT("quality")) return LOCTEXT("UpscaleQuality", "Quality");
    if (Id == TEXT("balanced")) return LOCTEXT("UpscaleBalanced", "Balanced");
    if (Id == TEXT("performance")) return LOCTEXT("UpscalePerformance", "Performance");
    if (Id == TEXT("ultraperformance")) return LOCTEXT("UpscaleUltraPerformance", "Ultra performance");
    return LOCTEXT("Custom", "Custom");
}
FString IDreamSettingsUpscaler::QualityFromValues(const FDreamSettingsValues& Values, FIntPoint Resolution) const
{
    const double Percent = Values.FindRef(TEXT("Graphics.ResolutionScale")).Number;
    for (const auto& Mode : QualityModes(Resolution)) if (FMath::Abs(Mode.ScreenPercentage - Percent) < 0.05) return Mode.Id;
    return TEXT("custom");
}
FDreamSettingsResult IDreamSettingsUpscaler::QualityChanges(const FString& Quality, FIntPoint Resolution, FDreamSettingsValues& Out) const
{
    for (const auto& Mode : QualityModes(Resolution)) if (Mode.Id == Quality)
    { Out.Add(TEXT("Graphics.ResolutionScale"),Mode.ScreenPercentage); return {}; }
    return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("UnsupportedUpscaleQuality", "This quality mode is not supported by the selected upscaler."));
}
namespace
{
    constexpr const TCHAR* MethodId = TEXT("Graphics.UpscalingMethod");
    constexpr const TCHAR* QualityId = TEXT("Graphics.UpscalingQuality");
    constexpr const TCHAR* TemporalId = TEXT("Graphics.TemporalUpscaler");
    constexpr const TCHAR* SamplingId = TEXT("Graphics.TemporalUpsampling");
    FDreamSettingsResult UpscaleUnavailable(FText Text) { return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,Text); }
    FIntPoint Resolution(const FDreamSettingsValues& V)
    {
        FString X,Y; V.FindRef(TEXT("Display.Resolution")).Token.Split(TEXT("x"),&X,&Y);
        return FIntPoint(FCString::Atoi(*X),FCString::Atoi(*Y));
    }
    TArray<FDreamUpscalerQuality> EngineModes()
    {
        return {{TEXT("native"),DreamUpscalerQuality::Label(TEXT("native")),100},
            {TEXT("quality"),DreamUpscalerQuality::Label(TEXT("quality")),66.67f},
            {TEXT("balanced"),DreamUpscalerQuality::Label(TEXT("balanced")),58.82f},
            {TEXT("performance"),DreamUpscalerQuality::Label(TEXT("performance")),50},
            {TEXT("ultraperformance"),DreamUpscalerQuality::Label(TEXT("ultraperformance")),33.33f}};
    }
    FDreamSettingsResult SetCVar(const TCHAR* Name, float Value)
    {
        auto* C = IConsoleManager::Get().FindConsoleVariable(Name);
        if (!C || (C->GetFlags() & ECVF_SetByMask) > ECVF_SetByGameOverride)
            return UpscaleUnavailable(LOCTEXT("UpscalePolicy", "A higher-priority engine policy controls temporal rendering."));
        C->Set(Value,ECVF_SetByGameOverride);
        return FMath::IsNearlyEqual(C->GetFloat(),Value) ? FDreamSettingsResult{} : UpscaleUnavailable(LOCTEXT("UpscaleRejected", "The engine rejected the upscaling change."));
    }
    struct FCoordinator
    {
        const IConsoleVariable* AntiAliasing=IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
        const IConsoleVariable* TemporalSampling=IConsoleManager::Get().FindConsoleVariable(TEXT("r.TemporalAA.Upsampling"));
        TArray<IDreamSettingsUpscaler*> Adapters;
        TWeakObjectPtr<UGameInstance> Instance;
        IDreamSettingsUpscaler* Find(const FString& Id) const
        { for (auto* A : Adapters) if (A->Id() == FName(Id)) return A; return nullptr; }
        FDreamSettingsResult Availability(const FString& Id) const
        {
            if (Id==TEXT("none") || Id==TEXT("taau") || Id==TEXT("tsr")) return {};
            const auto* Adapter = Find(Id);
            if (!Adapter) return UpscaleUnavailable(FText::Format(LOCTEXT("AdapterMissing", "The {0} settings adapter is not installed."),FText::FromString(Id)));
            if (Instance.IsValid() && Instance->GetWorld() && Instance->GetWorld()->WorldType == EWorldType::PIE)
                return UpscaleUnavailable(LOCTEXT("UpscaleStandalone", "Use Standalone Game to change vendor upscaling."));
            return Adapter->Availability();
        }
        FDreamSettingsResult Read(FDreamSettingValue& V) const
        {
            FString Id;
            for (auto* A : Adapters) if (A->IsEnabled())
            {
                if (!Id.IsEmpty()) return UpscaleUnavailable(LOCTEXT("MultipleUpscalers", "More than one vendor upscaler is enabled. Check the project's rendering configuration."));
                Id=A->Id().ToString();
            }
            if (Id.IsEmpty())
            {
                if (!AntiAliasing || !TemporalSampling) return UpscaleUnavailable(LOCTEXT("TemporalUnavailable","The engine's temporal rendering settings are unavailable."));
                const int AA=AntiAliasing->GetInt();
                const int Temporal=TemporalSampling->GetInt();
                Id=AA==4 ? TEXT("tsr") : AA==2 && Temporal ? TEXT("taau") : TEXT("none");
            }
            V=FDreamSettingValue::Id(Id); return {};
        }
        FDreamSettingsResult Write(const FDreamSettingValue& V)
        {
            auto Result=Availability(V.Token); if (!Result.IsOk()) return Result;
            // Never activate a second temporal upscaler before the previous implementation has been disabled.
            for (auto* A : Adapters) if (A->IsEnabled() && A->Id()!=FName(V.Token))
            { Result=A->SetEnabled(false); if (!Result.IsOk()) return Result; }
            Result=SetCVar(TEXT("r.AntiAliasingMethod"),V.Token==TEXT("none") || V.Token==TEXT("taau") ? 2 : 4);
            if (Result.IsOk()) Result=SetCVar(TEXT("r.TemporalAA.Upscaler"),1);
            if (Result.IsOk()) Result=SetCVar(TEXT("r.TemporalAA.Upsampling"),V.Token==TEXT("none") ? 0 : 1);
            if (!Result.IsOk()) return Result;
            if (auto* A=Find(V.Token)) { Result=A->SetEnabled(true); if (!Result.IsOk()) return Result; }
            FDreamSettingValue Actual; Result=Read(Actual);
            return Result.IsOk() && Actual.Equals(V) ? FDreamSettingsResult{} : UpscaleUnavailable(LOCTEXT("UpscalerNotActive", "The selected upscaler did not become active."));
        }
        TArray<FDreamUpscalerQuality> Modes(const FString& Method,FIntPoint Size) const
        { if (const auto* A=Find(Method)) return A->QualityModes(Size); return EngineModes(); }
        FDreamSettingsResult QualityChanges(const FString& Method,const FString& Quality,FIntPoint Size,FDreamSettingsValues& Out) const
        {
            if (const auto* A=Find(Method)) return A->QualityChanges(Quality,Size,Out);
            for (const auto& M : EngineModes()) if (M.Id==Quality) { Out.Add(TEXT("Graphics.ResolutionScale"),M.ScreenPercentage); return {}; }
            return UpscaleUnavailable(LOCTEXT("UnsupportedUpscaleQuality", "This quality mode is not supported by the selected upscaler."));
        }
    };
}
FDreamSettingsResult RegisterDreamUpscalingSettings(FDreamSettingsRegistry& Registry,UGameInstance* Instance)
{
    auto Coordinator=MakeShared<FCoordinator>(); Coordinator->Instance=Instance;
    Coordinator->Adapters=IModularFeatures::Get().GetModularFeatureImplementations<IDreamSettingsUpscaler>(IDreamSettingsUpscaler::FeatureName());
    FDreamSettingsResult Result;
    for (auto* A : Coordinator->Adapters) { Result=A->RegisterSettings(Registry); if (!Result.IsOk()) return Result; }
    for (const auto& Pair : TArray<TPair<FName,FString>>{{TemporalId,TEXT("r.TemporalAA.Upscaler")},{SamplingId,TEXT("r.TemporalAA.Upsampling")}})
    {
        FDreamSettingDefinition D; D.Id=Pair.Key; D.Category=TEXT("Graphics"); D.bHidden=true; D.Kind=EDreamSettingKind::Toggle; D.Step=1; D.Default=1; D.Order=190;
        const FString CVar=Pair.Value;
        const auto* Variable=IConsoleManager::Get().FindConsoleVariable(*CVar);
        Result=Registry.Register(D,MakeShared<FDreamCallbackSettingsProvider>(
            [Variable](FDreamSettingValue& V) { if (!Variable) return UpscaleUnavailable(LOCTEXT("TemporalUnavailable","The engine's temporal rendering settings are unavailable.")); V=Variable->GetInt(); return FDreamSettingsResult{}; },
            [CVar](const FDreamSettingValue& V) { return SetCVar(*CVar,V.Number); }));
        if (!Result.IsOk()) return Result;
    }
    FDreamSettingDefinition Method; Method.Id=MethodId; Method.Category=TEXT("Graphics"); Method.Order=140;
    Method.Kind=EDreamSettingKind::Choice; Method.Label=LOCTEXT("UpscalingMethod", "Upscaling method"); Method.Default=FDreamSettingValue::Id(TEXT("tsr"));
    Method.Options={{TEXT("none"),LOCTEXT("SpatialUpscale", "Spatial / native")},{TEXT("taau"),FText::FromString(TEXT("TAAU"))},{TEXT("tsr"),FText::FromString(TEXT("TSR"))},
        {TEXT("dlss"),FText::FromString(TEXT("NVIDIA DLSS"))},{TEXT("fsr"),FText::FromString(TEXT("AMD FSR"))},{TEXT("xess"),FText::FromString(TEXT("Intel XeSS"))}};
    FText Description=LOCTEXT("UpscaleDescription", "Only supported methods are selectable. Changing anti-aliasing to a native method disables vendor upscaling.");
    for (const auto& O : Method.Options)
    { const auto Capability=Coordinator->Availability(O.Id); if (!Capability.IsOk()) Description=FText::Format(LOCTEXT("UpscaleCapabilityDescription", "{0} {1}: {2}"),Description,O.Label,Capability.Message); }
    Method.Description=Description;
    Method.OptionAvailability=[Coordinator](const FString& Id,const FDreamSettingsValues&) { return Coordinator->Availability(Id); };
    Method.RelatedChanges=[Coordinator](const FDreamSettingValue& V,const FDreamSettingsValues& Pending,FDreamSettingsValues& Out)
    {
        Out.Add(TEXT("Graphics.AAMethod"),FDreamSettingValue::Id(V.Token==TEXT("none") || V.Token==TEXT("taau") ? TEXT("2") : TEXT("4")));
        Out.Add(TemporalId,1); Out.Add(SamplingId,V.Token==TEXT("none") ? 0 : 1);
        const auto Modes=Coordinator->Modes(V.Token,Resolution(Pending));
        FString Quality=Pending.FindRef(QualityId).Token;
        if (!Modes.ContainsByPredicate([&](const auto& M) { return M.Id==Quality; })) Quality=TEXT("quality");
        return Coordinator->QualityChanges(V.Token,Quality,Resolution(Pending),Out);
    };
    Result=Registry.Register(Method,MakeShared<FDreamCallbackSettingsProvider>(
        [Coordinator](FDreamSettingValue& V) { return Coordinator->Read(V); },
        [Coordinator](const FDreamSettingValue& V) { return Coordinator->Write(V); }));
    if (!Result.IsOk()) return Result;
    FDreamSettingDefinition Quality; Quality.Id=QualityId; Quality.Category=TEXT("Graphics"); Quality.Order=150; Quality.Kind=EDreamSettingKind::Choice;
    Quality.Label=LOCTEXT("UpscalingQuality", "Upscaling quality"); Quality.Default=FDreamSettingValue::Id(TEXT("quality"));
    Quality.Description=LOCTEXT("UpscalingQualityDescription", "Quality modes are queried from the active SDK. Custom render resolution remains available for engine upscaling.");
    for (const TCHAR* Id : {TEXT("native"),TEXT("ultraqualityplus"),TEXT("ultraquality"),TEXT("quality"),TEXT("balanced"),TEXT("performance"),TEXT("ultraperformance")})
        Quality.Options.Add({Id,DreamUpscalerQuality::Label(Id)});
    Quality.Options.Add({TEXT("custom"),LOCTEXT("Custom", "Custom"),false});
    Quality.Dependencies={MethodId,TEXT("Display.Resolution")};
    for (auto* A : Coordinator->Adapters) for (FName Id : A->QualityDependencies())
        if (Id!=TEXT("Graphics.ResolutionScale")) Quality.Dependencies.AddUnique(Id);
    Quality.Dependencies.Add(TemporalId); Quality.Dependencies.Add(SamplingId);
    Quality.Dependencies.Add(TEXT("Graphics.ResolutionScale"));
    Quality.Derive=[Coordinator](const FDreamSettingsValues& Values)
    {
        const FString Method=Values.FindRef(MethodId).Token;
        if (const auto* A=Coordinator->Find(Method)) return FDreamSettingValue::Id(A->QualityFromValues(Values,Resolution(Values)));
        const double Percent=Values.FindRef(TEXT("Graphics.ResolutionScale")).Number;
        for (const auto& M : EngineModes()) if (FMath::Abs(M.ScreenPercentage-Percent)<0.05) return FDreamSettingValue::Id(M.Id);
        return FDreamSettingValue::Id(TEXT("custom"));
    };
    Quality.Expand=[Coordinator](const FDreamSettingValue& V,const FDreamSettingsValues& Pending,FDreamSettingsValues& Out)
    { return Coordinator->QualityChanges(Pending.FindRef(MethodId).Token,V.Token,Resolution(Pending),Out); };
    Quality.OptionAvailability=[Coordinator](const FString& Id,const FDreamSettingsValues& Pending)
    {
        const auto Modes=Coordinator->Modes(Pending.FindRef(MethodId).Token,Resolution(Pending));
        return Modes.ContainsByPredicate([&](const auto& M) { return M.Id==Id; }) ? FDreamSettingsResult{} : UpscaleUnavailable(LOCTEXT("UnsupportedUpscaleQuality", "This quality mode is not supported by the selected upscaler."));
    };
    return Registry.Register(Quality);
}
#undef LOCTEXT_NAMESPACE
