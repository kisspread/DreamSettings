#include "DreamSettingsAccessibility.h"
#include "DreamSettingsProvider.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Rendering/RenderingCommon.h"
#include "Misc/App.h"
#include "RHI.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
namespace
{
    const FName TypeId(TEXT("Accessibility.ColorCorrectionType"));
    const FName StrengthId(TEXT("Accessibility.ColorCorrectionIntensity"));
    struct FColorVisionState
    {
        TWeakObjectPtr<UGameInstance> Instance;
        FString Type=TEXT("none");
        int32 Strength=2;
        bool bOwnsRenderer=false;

        FDreamSettingsResult Availability() const
        {
            const auto* World=Instance.IsValid() ? Instance->GetWorld() : nullptr;
            // Slate's filter affects every window in the process, including editor windows.
            if (!World || World->WorldType!=EWorldType::Game)
                return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("ColorVisionStandalone","Use Standalone Game to preview colour correction."));
            if (!FApp::CanEverRender() || GUsingNullRHI || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
                return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("ColorVisionRenderer","Colour correction needs a rendered game window."));
            return {};
        }
        void Apply()
        {
            const auto Mode=Type==TEXT("deuteranope") ? EColorVisionDeficiency::Deuteranope :
                Type==TEXT("protanope") ? EColorVisionDeficiency::Protanope :
                Type==TEXT("tritanope") ? EColorVisionDeficiency::Tritanope : EColorVisionDeficiency::NormalVision;
            FSlateApplication::Get().GetRenderer()->SetColorVisionDeficiencyType(Mode,Strength,true,false);
        }
        FDreamSettingsResult Read(FName Id,FDreamSettingValue& Out) const
        {
            auto Result=Availability(); if (!Result.IsOk()) return Result;
            if (!bOwnsRenderer) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("ColorVisionNotInitialized","Colour correction is not initialized for this game window."));
            // The renderer exposes a setter but no public getter. This service is its sole owner;
            // host code must route changes through the registry, not call the renderer separately.
            Out=Id==TypeId ? FDreamSettingValue::Id(Type) : FDreamSettingValue(Strength); return {};
        }
        FDreamSettingsResult Write(FName Id,const FDreamSettingValue& Value)
        {
            auto Result=Availability(); if (!Result.IsOk()) return Result;
            if (!bOwnsRenderer) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("ColorVisionNotInitialized","Colour correction is not initialized for this game window."));
            if (Id==TypeId)
            {
                if (!Value.bToken || (Value.Token!=TEXT("none") && Value.Token!=TEXT("deuteranope") && Value.Token!=TEXT("protanope") && Value.Token!=TEXT("tritanope")))
                    return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue,LOCTEXT("ColorVisionTypeInvalid","Choose a supported colour correction type."));
                Type=Value.Token;
            }
            else
            {
                if (Value.bToken || !FMath::IsFinite(Value.Number) || Value.Number<0 || Value.Number>10 || !FMath::IsNearlyEqual(Value.Number,FMath::RoundToDouble(Value.Number)))
                    return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue,LOCTEXT("ColorVisionStrengthInvalid","Colour correction strength must be a whole number from 0 to 10."));
                Strength=static_cast<int32>(Value.Number);
            }
            Apply(); return {};
        }
        ~FColorVisionState()
        {
            if (bOwnsRenderer && FSlateApplication::IsInitialized())
                if (auto Renderer=FSlateApplication::Get().GetRenderer())
                    Renderer->SetColorVisionDeficiencyType(EColorVisionDeficiency::NormalVision,0,true,false);
        }
    };
}
FDreamSettingsResult RegisterDreamAccessibilitySettings(FDreamSettingsRegistry& Registry,UGameInstance* Instance)
{
    auto State=MakeShared<FColorVisionState>(); State->Instance=Instance;
    if (State->Availability().IsOk()) { State->Apply(); State->bOwnsRenderer=true; }
    auto Provider=[State](FName Id)
    {
        return MakeShared<FDreamCallbackSettingsProvider>(
            [State,Id](FDreamSettingValue& Out) { return State->Read(Id,Out); },
            [State,Id](const FDreamSettingValue& Value) { return State->Write(Id,Value); },
            [State] { return State->Availability(); });
    };
    FDreamSettingDefinition Type; Type.Id=TypeId; Type.Category=TEXT("Accessibility"); Type.Order=500;
    Type.Kind=EDreamSettingKind::Choice; Type.bPreview=true; Type.Default=FDreamSettingValue::Id(TEXT("none"));
    Type.Label=LOCTEXT("ColorVisionType","Colour correction type");
    Type.Description=LOCTEXT("ColorVisionTypeHelp","Preview correction across the game window, including its menus. Compare the labelled colour samples.");
    Type.Options={{TEXT("none"),LOCTEXT("ColorVisionNone","None")},{TEXT("deuteranope"),LOCTEXT("ColorVisionGreen","Deuteranope — green")},
        {TEXT("protanope"),LOCTEXT("ColorVisionRed","Protanope — red")},{TEXT("tritanope"),LOCTEXT("ColorVisionBlue","Tritanope — blue")}};
    auto Result=Registry.Register(Type,Provider(TypeId)); if (!Result.IsOk()) return Result;
    FDreamSettingDefinition Strength; Strength.Id=StrengthId; Strength.Category=Type.Category; Strength.Order=501;
    Strength.bPreview=true; Strength.Min=0; Strength.Max=10; Strength.Step=1; Strength.Default=2;
    Strength.Label=LOCTEXT("ColorVisionStrength","Colour correction strength");
    Strength.Description=LOCTEXT("ColorVisionStrengthHelp","Adjust correction from 0 to 10. Zero leaves colours unchanged; the engine applies whole-number levels.");
    Strength.Dependencies={TypeId};
    Strength.EditCondition=[](const FDreamSettingsValues& Values)
    { return Values.FindRef(TypeId).Token==TEXT("none") ? FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("ColorVisionChooseType","Choose a colour correction type first.")) : FDreamSettingsResult{}; };
    Result=Registry.Register(Strength,Provider(StrengthId)); if (!Result.IsOk()) return Result;
    const TWeakObjectPtr<UGameInstance> Owner=Instance;
    auto SubtitleAvailability=[Owner]() -> FDreamSettingsResult
    {
        const auto* World=Owner.IsValid() ? Owner->GetWorld() : nullptr;
        if (!World || World->WorldType!=EWorldType::Game || !GEngine)
            return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("SubtitlesStandalone","Use Standalone Game to change subtitles."));
        if (GEngine->bSubtitlesForcedOff)
            return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("SubtitlesForcedOff","This game has disabled the native subtitle system."));
        return {};
    };
    FDreamSettingDefinition Subtitles; Subtitles.Id=TEXT("Accessibility.Subtitles"); Subtitles.Category=TEXT("Accessibility"); Subtitles.Order=502;
Subtitles.Presentation=EDreamSettingPresentation::SubtitleSample;
    Subtitles.Kind=EDreamSettingKind::Toggle; Subtitles.Default=1; Subtitles.Step=1; Subtitles.bPreview=true;
    Subtitles.Label=LOCTEXT("Subtitles","Subtitles");
    Subtitles.Description=LOCTEXT("SubtitlesHelp","Show dialogue and cinematic subtitles. Preview the change before applying it.");
    return Registry.Register(Subtitles,MakeShared<FDreamCallbackSettingsProvider>(
        [SubtitleAvailability](FDreamSettingValue& Out)
        { auto R=SubtitleAvailability(); if (R.IsOk()) Out=UGameplayStatics::AreSubtitlesEnabled() ? 1 : 0; return R; },
        [SubtitleAvailability](const FDreamSettingValue& Value)
        {
            auto R=SubtitleAvailability(); if (!R.IsOk()) return R;
            if (Value.bToken || (Value.Number!=0 && Value.Number!=1))
                return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue,LOCTEXT("SubtitleBoolean","Subtitles must be on or off."));
            UGameplayStatics::SetSubtitlesEnabled(Value.Number!=0);
            return UGameplayStatics::AreSubtitlesEnabled()==(Value.Number!=0) ? FDreamSettingsResult{} :
                FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("SubtitleReadback","The game did not accept the subtitle change."));
        },SubtitleAvailability));
}
#undef LOCTEXT_NAMESPACE
