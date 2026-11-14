#include "DreamSettingsEngineCatalog.h"
#include "DreamSettingsProvider.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HAL/IConsoleManager.h"
#include "Scalability.h"
#include "RenderUtils.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
namespace
{
    using FProvider = FDreamCallbackSettingsProvider;
    FDreamSettingsResult Unavailable(const FText& Reason) { return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable, Reason); }
    TArray<FDreamSettingOption> QualityOptions()
    {
        return {{TEXT("low"), LOCTEXT("Low", "Low")}, {TEXT("medium"), LOCTEXT("Medium", "Medium")},
            {TEXT("high"), LOCTEXT("High", "High")}, {TEXT("epic"), LOCTEXT("Epic", "Epic")},
            {TEXT("cinematic"), LOCTEXT("Cinematic", "Cinematic")}};
    }
    FDreamSettingsResult CVarAvailability(const FString& Name, EConsoleVariableFlags Priority,const IConsoleVariable* CVar)
    {
        if (!CVar) return Unavailable(FText::Format(LOCTEXT("MissingCVar", "This engine does not provide {0}."), FText::FromString(Name)));
        if (CVar->TestFlags(ECVF_ReadOnly)) return Unavailable(LOCTEXT("ReadOnlyCVar", "This feature requires project configuration and cannot be changed at runtime."));
        if ((CVar->GetFlags() & ECVF_SetByMask) > Priority)
            return Unavailable(LOCTEXT("CVarPolicy", "This setting is controlled by a higher-priority engine policy."));
        return {};
    }
    FString ResolutionId(FIntPoint Size) { return FString::Printf(TEXT("%dx%d"), Size.X, Size.Y); }
}
FDreamSettingsResult RegisterDreamEngineSettings(FDreamSettingsRegistry& Registry, UGameInstance* GameInstance)
{
    FDreamSettingsResult Failure;
    auto Add = [&](FDreamSettingDefinition D, TSharedRef<IDreamSettingProvider> P)
    {
        auto Result = Registry.Register(MoveTemp(D), P);
        if (!Result.IsOk()) Failure = Result;
    };
    auto Scalar = [](FName Id, FName Category, FText Label, double Min, double Max, double Step, double Default)
    {
        FDreamSettingDefinition D; D.Id = Id; D.Category = Category; D.Label = Label;
        D.Min = Min; D.Max = Max; D.Step = Step; D.Default = Default; D.Order = 200; return D;
    };
    auto CVar = [&](FDreamSettingDefinition D, FString Name, EConsoleVariableFlags Priority = ECVF_SetByGameOverride)
    {
        const auto Options = D.Options;
        // These are built-in engine CVars with engine lifetime. Read values/priority dynamically,
        // but resolve the console registry entry only once per catalog rather than every UI tick.
        auto* Variable=IConsoleManager::Get().FindConsoleVariable(*Name);
        auto Read = [Variable, Options](FDreamSettingValue& Out) -> FDreamSettingsResult
        {
            if (!Variable) return Unavailable(LOCTEXT("MissingFeature", "This feature is not available in this engine."));
            const double V = Variable->GetFloat();
            Out = Options.IsEmpty() ? FDreamSettingValue(V) : FDreamSettingValue::Id(FString::Printf(TEXT("%d"), FMath::RoundToInt(V)));
            return {};
        };
        auto Write = [Variable, Priority](const FDreamSettingValue& V) -> FDreamSettingsResult
        {
            if (!Variable) return Unavailable(LOCTEXT("MissingFeature", "This feature is not available in this engine."));
            const double Number = V.bToken ? FCString::Atod(*V.Token) : V.Number;
            Variable->Set(static_cast<float>(Number), Priority);
            return FMath::IsNearlyEqual(static_cast<double>(Variable->GetFloat()), Number, 1.e-4) ? FDreamSettingsResult{}
                : FDreamSettingsResult::Fail(EDreamSettingsError::Backend, LOCTEXT("CVarRejected", "The engine rejected this setting."));
        };
        D.Description = FText::Format(LOCTEXT("RenderDescription", "Adjust {0}. Unsupported features are unavailable on this device."), D.Label);
        Add(D, MakeShared<FProvider>(Read, Write, [Name, Priority, Variable]() -> FDreamSettingsResult
        {
            if ((Name == TEXT("r.Lumen.HardwareRayTracing") || Name == TEXT("r.MegaLights.Allow")) && !IsRayTracingEnabled())
                return Unavailable(LOCTEXT("RayTracingUnavailable", "Ray tracing is not enabled for this project and graphics device."));
            return CVarAvailability(Name, Priority, Variable);
        }));
    };
    TWeakObjectPtr<UGameInstance> WeakInstance(GameInstance);
    auto DisplayAvailability = [WeakInstance]() -> FDreamSettingsResult
    {
        const auto* World = WeakInstance.IsValid() ? WeakInstance->GetWorld() : nullptr;
        if (World && World->WorldType == EWorldType::PIE) return Unavailable(LOCTEXT("StandaloneDisplay", "Use Standalone Game to change resolution or window mode."));
        return {};
    };
    FDreamSettingDefinition Mode;
    Mode.Id = TEXT("Display.WindowMode"); Mode.Category = TEXT("Display"); Mode.Label = LOCTEXT("WindowMode", "Window mode");
    Mode.Kind = EDreamSettingKind::Choice; Mode.Default = FDreamSettingValue::Id(TEXT("borderless")); Mode.Order = 10; Mode.bRequiresConfirmation = true;
    Mode.Options = {{TEXT("fullscreen"), LOCTEXT("Fullscreen", "Fullscreen")}, {TEXT("borderless"), LOCTEXT("Borderless", "Borderless")},
        {TEXT("windowed"), LOCTEXT("Windowed", "Windowed")}};
    Mode.Description = LOCTEXT("DisplayConfirmation", "Display changes must be confirmed within 15 seconds. Otherwise the previous mode is restored.");
    Add(Mode, MakeShared<FProvider>(
        [](FDreamSettingValue& V) { const auto Mode = GEngine->GetGameUserSettings()->GetFullscreenMode();
            V = FDreamSettingValue::Id(Mode == EWindowMode::Fullscreen ? TEXT("fullscreen") : Mode == EWindowMode::WindowedFullscreen ? TEXT("borderless") : TEXT("windowed")); return FDreamSettingsResult{}; },
        [](const FDreamSettingValue& V) { auto* Settings = GEngine->GetGameUserSettings();
            Settings->SetFullscreenMode(V.Token == TEXT("fullscreen") ? EWindowMode::Fullscreen : V.Token == TEXT("borderless") ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
            Settings->ApplyResolutionSettings(false); return FDreamSettingsResult{}; }, DisplayAvailability,
        [](const FDreamSettingValue& V)
        {
            const auto* Viewport = GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
            const auto Requested = V.Token == TEXT("fullscreen") ? EWindowMode::Fullscreen : V.Token == TEXT("borderless") ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed;
            return Viewport && Viewport->GetWindowMode() == Requested ? FDreamSettingsResult{} : Unavailable(LOCTEXT("WindowNotApplied", "The window has not accepted the requested display mode."));
        }));

    FDreamSettingDefinition Resolution = Mode;
    Resolution.Id = TEXT("Display.Resolution"); Resolution.Label = LOCTEXT("Resolution", "Resolution"); Resolution.Order = 20; Resolution.Options.Reset();
    TArray<FIntPoint> Resolutions, Windowed;
    UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
    UKismetSystemLibrary::GetConvenientWindowedResolutions(Windowed);
    for (FIntPoint Size : Windowed) Resolutions.AddUnique(Size);
    auto* UserSettings = GEngine->GetGameUserSettings();
    FIntPoint Current = UserSettings->GetScreenResolution();
    if (Current.X <= 0 || Current.Y <= 0) Current = FIntPoint(1280, 720);
    Resolutions.AddUnique(Current);
    Resolutions.Sort([](FIntPoint A, FIntPoint B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
    for (FIntPoint Size : Resolutions)
        if (Size.X >= 640 && Size.Y >= 480) Resolution.Options.Add({ResolutionId(Size), FText::FromString(ResolutionId(Size))});
    Resolution.Default = FDreamSettingValue::Id(ResolutionId(Current));
    Resolution.Dependencies = { Mode.Id };
    Add(Resolution, MakeShared<FProvider>(
        [](FDreamSettingValue& V) { V = FDreamSettingValue::Id(ResolutionId(GEngine->GetGameUserSettings()->GetScreenResolution())); return FDreamSettingsResult{}; },
        [](const FDreamSettingValue& V) { FString X, Y; if (!V.Token.Split(TEXT("x"), &X, &Y)) return Unavailable(LOCTEXT("BadResolution", "Invalid resolution."));
            auto* Settings = GEngine->GetGameUserSettings(); Settings->SetScreenResolution(FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y)));
            Settings->ApplyResolutionSettings(false); return FDreamSettingsResult{}; }, DisplayAvailability,
        [](const FDreamSettingValue& V)
        {
            const auto* Viewport = GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
            return Viewport && ResolutionId(Viewport->GetSizeXY()) == V.Token ? FDreamSettingsResult{} : Unavailable(LOCTEXT("ResolutionNotApplied", "The display has not accepted the requested resolution."));
        }));

    auto FrameLimit = Scalar(TEXT("Display.FrameRateLimit"), TEXT("Display"), LOCTEXT("FrameLimit", "Frame rate limit (0 = unlimited)"), 0, 1000, 1, 0);
    FrameLimit.Order = 40;
    Add(FrameLimit, MakeShared<FProvider>(
        [](FDreamSettingValue& V) { V = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetFloat(); return FDreamSettingsResult{}; },
        [](const FDreamSettingValue& V) { GEngine->GetGameUserSettings()->SetFrameRateLimit(V.Number);
            auto* C = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")); C->Set(static_cast<float>(V.Number), ECVF_SetByGameSetting);
            return FMath::IsNearlyEqual(C->GetFloat(), static_cast<float>(V.Number)) ? FDreamSettingsResult{} : Unavailable(LOCTEXT("FrameLimitPolicy", "A higher-priority policy controls the frame rate.")); },
        []() { static const auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")); return CVarAvailability(TEXT("t.MaxFPS"), ECVF_SetByGameSetting,Variable); }));

    auto Interval = Scalar(TEXT("Display.SyncInterval"), TEXT("Display"), LOCTEXT("SyncInterval", "VSync interval"), 1, 4, 1, 1);
    Interval.Order = 35; Interval.Dependencies = { DreamSettingIds::VSync };
    Interval.Condition = [](const FDreamSettingsValues& Values) { return Values.FindRef(DreamSettingIds::VSync).Number != 0 ? FDreamSettingsResult{} : Unavailable(LOCTEXT("EnableVSync", "Enable vertical sync first.")); };
    CVar(Interval, TEXT("rhi.SyncInterval"), ECVF_SetByGameSetting);

    auto Gamma = Scalar(TEXT("Display.Gamma"), TEXT("Display"), LOCTEXT("Gamma", "Gamma"), 0.4, 3.5, 0.05, 2.2);
    Gamma.Order = 50; Gamma.bPreview = true;
    Add(Gamma, MakeShared<FProvider>(
        [](FDreamSettingValue& V) { V = GEngine->GetDisplayGamma(); return FDreamSettingsResult{}; },
        [](const FDreamSettingValue& V) { GEngine->DisplayGamma = V.Number; return FDreamSettingsResult{}; }));

    struct FQuality { const TCHAR* Id; FText Label; int32 Scalability::FQualityLevels::* Member; };
    const FQuality Qualities[] = {
        {TEXT("Graphics.ViewDistanceQuality"), LOCTEXT("ViewDistance", "View distance"), &Scalability::FQualityLevels::ViewDistanceQuality},
        {TEXT("Graphics.ShadowQuality"), LOCTEXT("Shadows", "Shadows"), &Scalability::FQualityLevels::ShadowQuality},
        {TEXT("Graphics.GlobalIlluminationQuality"), LOCTEXT("GI", "Global illumination"), &Scalability::FQualityLevels::GlobalIlluminationQuality},
        {TEXT("Graphics.ReflectionsQuality"), LOCTEXT("Reflections", "Reflections"), &Scalability::FQualityLevels::ReflectionQuality},
        {TEXT("Graphics.AAQuality"), LOCTEXT("AAQuality", "Anti-aliasing quality"), &Scalability::FQualityLevels::AntiAliasingQuality},
        {TEXT("Graphics.TextureQuality"), LOCTEXT("Textures", "Textures"), &Scalability::FQualityLevels::TextureQuality},
        {TEXT("Graphics.EffectsQuality"), LOCTEXT("Effects", "Effects"), &Scalability::FQualityLevels::EffectsQuality},
        {TEXT("Graphics.PostProcessQuality"), LOCTEXT("PostProcess", "Post processing"), &Scalability::FQualityLevels::PostProcessQuality},
        {TEXT("Graphics.FoliageQuality"), LOCTEXT("Foliage", "Foliage"), &Scalability::FQualityLevels::FoliageQuality},
        {TEXT("Graphics.ShadingQuality"), LOCTEXT("Shading", "Shading"), &Scalability::FQualityLevels::ShadingQuality}
    };
    int32 Order = 100;
    TArray<FName> QualityIds;
    for (const auto& Quality : Qualities)
    {
        FDreamSettingDefinition D; D.Id = Quality.Id; D.Category = TEXT("Graphics"); D.Label = Quality.Label;
        D.Kind = EDreamSettingKind::Choice; D.Options = QualityOptions(); D.Default = FDreamSettingValue::Id(TEXT("high")); D.Order = Order++;
        const auto Member = Quality.Member;
        D.Options.SetNum(FMath::Clamp(Scalability::GetQualityLevelCounts().*Member, 1, D.Options.Num()));
        D.Default = FDreamSettingValue::Id(D.Options[FMath::Min(2, D.Options.Num() - 1)].Id);
        QualityIds.Add(D.Id);
        const auto Options = D.Options;
        Add(D, MakeShared<FProvider>(
            [Member, Options](FDreamSettingValue& V) { const int32 Index = Scalability::GetQualityLevels().*Member;
                if (!Options.IsValidIndex(Index)) return Unavailable(LOCTEXT("QualityRange", "The current engine quality level is outside the supported range."));
                V = FDreamSettingValue::Id(Options[Index].Id); return FDreamSettingsResult{}; },
            [Member, Options](const FDreamSettingValue& V) { const int32 Index = Options.IndexOfByPredicate([&](const auto& O) { return O.Id == V.Token; });
                auto Levels = Scalability::GetQualityLevels(); Levels.*Member = Index; Scalability::SetQualityLevels(Levels);
                GEngine->GetGameUserSettings()->ScalabilityQuality = Scalability::GetQualityLevels();
                return (Scalability::GetQualityLevels().*Member == Index) ? FDreamSettingsResult{} : Unavailable(LOCTEXT("QualityRejected", "The engine rejected the quality level.")); }));
    }

    FDreamSettingDefinition Preset;
    Preset.Id = TEXT("Graphics.Preset"); Preset.Category = TEXT("Graphics"); Preset.Label = LOCTEXT("Preset", "Graphics preset");
    Preset.Description = LOCTEXT("PresetDescription", "Set all ten quality groups together. Individual adjustments produce a custom preset.");
    Preset.Kind = EDreamSettingKind::Choice; Preset.Options = QualityOptions(); Preset.Options.Add({TEXT("custom"), LOCTEXT("Custom", "Custom"), false});
    Preset.Default = FDreamSettingValue::Id(TEXT("high")); Preset.Order = 90; Preset.Dependencies = QualityIds;
    Preset.Derive = [QualityIds](const FDreamSettingsValues& Values)
    {
        const FString First = Values.FindRef(QualityIds[0]).Token;
        for (FName Id : QualityIds) if (Values.FindRef(Id).Token != First) return FDreamSettingValue::Id(TEXT("custom"));
        return FDreamSettingValue::Id(First);
    };
    Preset.Expand = [QualityIds](const FDreamSettingValue& V, const FDreamSettingsValues&, FDreamSettingsValues& Changes)
    { for (FName Id : QualityIds) Changes.Add(Id, V); return FDreamSettingsResult{}; };
    if (const auto Result = Registry.Register(Preset); !Result.IsOk()) Failure = Result;

    // EasyOptions' original fog grid presets. Separate backing values preserve arbitrary pre-existing engine values on undo.
    const TCHAR* FogIds[] = {TEXT("Graphics.FogEnabled"),TEXT("Graphics.FogPixelSize"),TEXT("Graphics.FogDepth"),TEXT("Graphics.FogHistorySamples")};
    const TCHAR* FogCVars[] = {TEXT("r.VolumetricFog"),TEXT("r.VolumetricFog.GridPixelSize"),TEXT("r.VolumetricFog.GridSizeZ"),TEXT("r.VolumetricFog.HistoryMissSupersampleCount")};
    const int32 FogPresets[4][4] = {{1,32,32,2},{1,24,48,2},{1,16,64,4},{1,8,128,4}};
    TArray<FName> FogNames;
    TArray<TArray<int32>> FogValues;
    for (int32 I = 0; I < 4; ++I)
    {
        auto D = Scalar(FogIds[I], TEXT("Graphics"), LOCTEXT("FogBacking", "Volumetric fog parameter"), 0, I == 0 ? 1 : 2048, 1, FogPresets[2][I]);
        D.bHidden = true; D.Order = 170 + I; FogNames.Add(D.Id); CVar(D, FogCVars[I]);
        FogValues.Add({FogPresets[I][0],FogPresets[I][1],FogPresets[I][2],FogPresets[I][3]});
    }
    FDreamSettingDefinition Fog;
    Fog.Id = TEXT("Graphics.VolumetricsQuality"); Fog.Category = TEXT("Graphics"); Fog.Label = LOCTEXT("Volumetrics", "Volumetric effects");
    Fog.Description = LOCTEXT("VolumetricsDescription", "Adjust volumetric fog grid detail and temporal sampling.");
    Fog.Kind = EDreamSettingKind::Choice; Fog.Options = QualityOptions(); Fog.Options.SetNum(4);
    const auto FogOptions = Fog.Options;
    Fog.Options.Add({TEXT("custom"),LOCTEXT("Custom", "Custom"),false});
    Fog.Default = FDreamSettingValue::Id(TEXT("high")); Fog.Dependencies = FogNames; Fog.Order = 175;
    Fog.Derive = [FogNames, FogValues, FogOptions](const FDreamSettingsValues& Values)
    {
        for (int32 Index = 0; Index < 4; ++Index)
        {
            bool bMatch = true;
            for (int32 I = 0; I < 4; ++I) bMatch &= Values.FindRef(FogNames[I]).Equals(FogValues[Index][I]);
            if (bMatch) return FDreamSettingValue::Id(FogOptions[Index].Id);
        }
        return FDreamSettingValue::Id(TEXT("custom"));
    };
    Fog.Expand = [FogNames, FogValues, FogOptions](const FDreamSettingValue& V, const FDreamSettingsValues&, FDreamSettingsValues& Changes)
    {
        const int32 Index = FogOptions.IndexOfByPredicate([&](const auto& O) { return O.Id == V.Token; });
        if (Index == INDEX_NONE) return Unavailable(LOCTEXT("FogPresetInvalid", "Invalid volumetric preset."));
        for (int32 I = 0; I < 4; ++I) Changes.Add(FogNames[I], FogValues[Index][I]);
        return FDreamSettingsResult{};
    };
    if (const auto Result = Registry.Register(Fog); !Result.IsOk()) Failure = Result;

    auto Scale = Scalar(TEXT("Graphics.ResolutionScale"), TEXT("Graphics"), LOCTEXT("ResolutionScale", "Render resolution (%)"), 25, 200, 0.01, 100);
    Scale.Order = 195; Scale.SpecialValues.Add(0,LOCTEXT("Automatic", "Automatic"));
    Scale.EditCondition=[](const FDreamSettingsValues& V)
    {
        const FString Method=V.FindRef(TEXT("Graphics.UpscalingMethod")).Token;
        return Method==TEXT("dlss") || Method==TEXT("fsr") || Method==TEXT("xess")
            ? Unavailable(LOCTEXT("VendorScale", "Use Upscaling quality to choose a render resolution supported by this SDK.")) : FDreamSettingsResult{};
    };
    CVar(Scale, TEXT("r.ScreenPercentage"));
    auto Sharpness = Scalar(TEXT("Graphics.Sharpness"), TEXT("Graphics"), LOCTEXT("Sharpness", "Image sharpening"), 0, 2, 0.05, -1);
    Sharpness.SpecialValues.Add(-1,LOCTEXT("SceneDefault", "Scene default")); CVar(Sharpness, TEXT("r.Tonemapper.Sharpen"));
    auto AA = Scalar(TEXT("Graphics.AAMethod"), TEXT("Graphics"), LOCTEXT("AA", "Anti-aliasing method"), 0, 4, 1, 4);
    AA.Kind = EDreamSettingKind::Choice; AA.Default = FDreamSettingValue::Id(TEXT("4"));
    AA.Options = {{TEXT("0"), LOCTEXT("Off", "Off")}, {TEXT("1"), FText::FromString(TEXT("FXAA"))},
        {TEXT("2"), FText::FromString(TEXT("TAA"))}, {TEXT("4"), FText::FromString(TEXT("TSR"))}};
    AA.RelatedChanges = [](const FDreamSettingValue& V, const FDreamSettingsValues&, FDreamSettingsValues& Out)
    {
        Out.Add(TEXT("Graphics.UpscalingMethod"),FDreamSettingValue::Id(V.Token==TEXT("4") ? TEXT("tsr") : V.Token==TEXT("2") ? TEXT("taau") : TEXT("none")));
        Out.Add(TEXT("Graphics.TemporalUpscaler"),1);
        Out.Add(TEXT("Graphics.TemporalUpsampling"),V.Token==TEXT("0") || V.Token==TEXT("1") ? 0 : 1);
        return FDreamSettingsResult{};
    };
    CVar(AA, TEXT("r.AntiAliasingMethod"));
    auto Motion = Scalar(TEXT("Graphics.MotionBlur"), TEXT("Graphics"), LOCTEXT("MotionBlur", "Motion blur quality"), 0, 4, 1, 0);
    CVar(Motion, TEXT("r.MotionBlurQuality"));
    auto ToggleCVar = [&](const TCHAR* Id, FText Label, const TCHAR* Name)
    {
        auto D = Scalar(Id, TEXT("Graphics"), Label, 0, 1, 1, 1); D.Kind = EDreamSettingKind::Toggle;
        CVar(D, Name);
    };
    ToggleCVar(TEXT("Graphics.VirtualShadowMaps"), LOCTEXT("VSM", "Virtual shadow maps"), TEXT("r.Shadow.Virtual.Enable"));
    ToggleCVar(TEXT("Graphics.ContactShadows"), LOCTEXT("ContactShadows", "Contact shadows"), TEXT("r.ContactShadows"));
    ToggleCVar(TEXT("Graphics.HardwareLumen"), LOCTEXT("HardwareLumen", "Hardware ray-traced Lumen"), TEXT("r.Lumen.HardwareRayTracing"));
    ToggleCVar(TEXT("Graphics.MegaLights"), LOCTEXT("MegaLights", "MegaLights"), TEXT("r.MegaLights.Allow"));
    auto Anisotropy = Scalar(TEXT("Graphics.Anisotropy"), TEXT("Graphics"), LOCTEXT("Anisotropy", "Anisotropic filtering"), 2, 16, 1, 8);
    Anisotropy.Kind = EDreamSettingKind::Choice; Anisotropy.Default = FDreamSettingValue::Id(TEXT("8"));
    for (const TCHAR* X : {TEXT("2"),TEXT("4"),TEXT("8"),TEXT("16")}) Anisotropy.Options.Add({X,FText::FromString(FString(X)+TEXT("x"))});
    CVar(Anisotropy, TEXT("r.MaxAnisotropy"));
    return Failure;
}
#undef LOCTEXT_NAMESPACE
