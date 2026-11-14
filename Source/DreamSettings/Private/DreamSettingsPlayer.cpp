#include "DreamSettingsPlayer.h"
#include "DreamSettingsProvider.h"
#include "Engine/LocalPlayer.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
FDreamPlayerSettingsStore::FDreamPlayerSettingsStore(TSharedRef<IDreamSettingsStore> InStore,int32 UserId,TSet<FName> InPlayerIds)
    : Store(InStore),Prefix(FString::Printf(TEXT("Player.%d."),UserId)),PlayerIds(MoveTemp(InPlayerIds)) {}
void FDreamPlayerSettingsStore::Decode(const FDreamSettingsValues& Values,FDreamSettingsValues& Out) const
{
    Out.Reset();
    for (const auto& Pair : Values)
    {
        const FString Id=Pair.Key.ToString();
        if (Id.StartsWith(Prefix))
        {
            const FName Local(*Id.RightChop(Prefix.Len()));
            if (PlayerIds.Contains(Local)) Out.Add(Local,Pair.Value);
        }
        else if (!Id.StartsWith(TEXT("Player.")) && !PlayerIds.Contains(Pair.Key)) Out.Add(Pair.Key,Pair.Value);
    }
}
FDreamSettingsResult FDreamPlayerSettingsStore::Merge(const FDreamSettingsValues& Values,FDreamSettingsValues& Out) const
{
    auto Result=Store->Load(Out); if (!Result.IsOk()) return Result;
    for (const auto& Pair : Values)
        if (!Pair.Key.ToString().StartsWith(TEXT("Player.")))
            Out.Add(PlayerIds.Contains(Pair.Key) ? FName(*(Prefix+Pair.Key.ToString())) : Pair.Key,Pair.Value);
    return {};
}
FDreamSettingsResult FDreamPlayerSettingsStore::Load(FDreamSettingsValues& Out)
{
    FDreamSettingsValues All; auto Result=Store->Load(All); if (Result.IsOk()) Decode(All,Out); return Result;
}
FDreamSettingsResult FDreamPlayerSettingsStore::Save(const FDreamSettingsValues& Values)
{
    FDreamSettingsValues All; auto Result=Merge(Values,All); return Result.IsOk() ? Store->Save(All) : Result;
}
FDreamSettingsResult FDreamPlayerSettingsStore::PrepareSave(const FDreamSettingsValues& Values,FDreamSettingsPreparedWrite& Out)
{
    FDreamSettingsValues All; auto Result=Merge(Values,All); return Result.IsOk() ? Store->PrepareSave(All,Out) : Result;
}
FDreamSettingsResult FDreamPlayerSettingsStore::BeginRiskyChange(const FDreamSettingsValues& Values)
{
    FDreamSettingsValues All; auto Result=Merge(Values,All); return Result.IsOk() ? Store->BeginRiskyChange(All) : Result;
}
FDreamSettingsResult FDreamPlayerSettingsStore::LoadRecovery(FDreamSettingsValues& Out)
{
    FDreamSettingsValues All; auto Result=Store->LoadRecovery(All); if (Result.IsOk()) Decode(All,Out); return Result;
}
FVector2D FDreamLookPreferences::Transform(FVector2D Input,bool bGamepad,double DeltaSeconds) const
{
    if (bGamepad)
    {
        const double Length=Input.Size();
        const double Dead=FMath::Clamp(GamepadDeadZone,0.0,0.95);
        Input=Length<=Dead ? FVector2D::ZeroVector : Input/Length*((FMath::Min(Length,1.0)-Dead)/(1.0-Dead));
        Input*=120.0*GamepadSensitivity*FMath::Max(0.0,DeltaSeconds);
    }
    else Input*=0.1*MouseSensitivity; // Mouse input is a displacement, not a per-second rate.
    return Input*FVector2D(bInvertX ? -1 : 1,bInvertY ? -1 : 1);
}
TArray<FDreamSettingDefinition> UDreamSettingsPlayerSubsystem::GameplayDefinitions()
{
    TArray<FDreamSettingDefinition> Out;
    auto Scalar=[&](const TCHAR* Id,const TCHAR* Category,FText Label,FText Description,double Min,double Max,double Step,double Default)
    {
        FDreamSettingDefinition D; D.Id=Id; D.Category=Category; D.Label=Label; D.Description=Description;
        D.Min=Min; D.Max=Max; D.Step=Step; D.Default=Default; D.bPreview=true; D.Order=400+Out.Num(); Out.Add(D);
    };
    Scalar(TEXT("Gameplay.CameraFOV"),TEXT("Gameplay"),LOCTEXT("CameraFOV","Horizontal field of view"),LOCTEXT("CameraFOVHelp","Preview the gameplay camera's horizontal field of view, in degrees."),60,120,1,90);
    Scalar(TEXT("Input.MouseSensitivity"),TEXT("Input"),LOCTEXT("MouseSensitivity","Mouse sensitivity"),LOCTEXT("MouseSensitivityHelp","Scale mouse look. Mouse movement is independent of frame time."),0.1,5,0.05,1);
    Scalar(TEXT("Input.GamepadSensitivity"),TEXT("Input"),LOCTEXT("GamepadSensitivity","Controller sensitivity"),LOCTEXT("GamepadSensitivityHelp","Scale the controller's look speed."),0.1,5,0.05,1);
    Scalar(TEXT("Input.InvertX"),TEXT("Input"),LOCTEXT("InvertX","Invert horizontal look"),LOCTEXT("InvertXHelp","Reverse horizontal camera input for mouse and controller."),0,1,1,0); Out.Last().Kind=EDreamSettingKind::Toggle;
    Scalar(TEXT("Input.InvertY"),TEXT("Input"),LOCTEXT("InvertY","Invert vertical look"),LOCTEXT("InvertYHelp","Reverse vertical camera input for mouse and controller."),0,1,1,0); Out.Last().Kind=EDreamSettingKind::Toggle;
    Scalar(TEXT("Input.GamepadDeadZone"),TEXT("Input"),LOCTEXT("GamepadDeadZone","Controller look dead zone"),LOCTEXT("GamepadDeadZoneHelp","Ignore small stick movements, then rescale the remaining range smoothly."),0,0.5,0.01,0.15);
    Scalar(TEXT("Input.GamepadVibration"),TEXT("Input"),LOCTEXT("GamepadVibration","Controller vibration"),LOCTEXT("GamepadVibrationHelp","Allow gameplay force feedback on supported controllers."),0,1,1,1); Out.Last().Kind=EDreamSettingKind::Toggle;
    return Out;
}
TArray<FDreamSettingDefinition> UDreamSettingsPlayerSubsystem::Definitions()
{
    auto Out=GameplayDefinitions(); Out.Append(PresentationDefinitions()); return Out;
}
TArray<FDreamSettingDefinition> UDreamSettingsPlayerSubsystem::PresentationDefinitions()
{
    TArray<FDreamSettingDefinition> Out;
    FDreamSettingDefinition D; D.Id=TEXT("Input.GamepadIcons"); D.Category=TEXT("Input");
    D.Label=LOCTEXT("GamepadIcons","Controller button style");
    D.Description=LOCTEXT("GamepadIconsHelp","Choose the controller button labels shown in menus. This changes prompts, not physical button assignments.");
    D.Kind=EDreamSettingKind::Choice; D.bPreview=true; D.Default=FDreamSettingValue::Id(TEXT("xbox")); D.Order=407;
    D.Options={{TEXT("xbox"),LOCTEXT("XboxButtons","Xbox")},{TEXT("playstation"),LOCTEXT("PlayStationButtons","PlayStation")},{TEXT("nintendo"),LOCTEXT("NintendoButtons","Nintendo Switch")}};
    Out.Add(MoveTemp(D));
    FDreamSettingDefinition Motion; Motion.Id=TEXT("Accessibility.ReducedMotion"); Motion.Category=TEXT("Accessibility"); Motion.Order=503;
    Motion.Kind=EDreamSettingKind::Toggle; Motion.Default=0; Motion.Step=1; Motion.bPreview=true;
    Motion.Label=LOCTEXT("ReducedMotion","Reduce menu motion");
    Motion.Description=LOCTEXT("ReducedMotionHelp","Use instant wheel and navigation scrolling, instant focus movement, and static loading indicators.");
    Out.Add(MoveTemp(Motion));
    FDreamSettingDefinition Scale; Scale.Id=TEXT("Accessibility.UIScale"); Scale.Category=TEXT("Accessibility"); Scale.Order=504;
    Scale.Label=LOCTEXT("UIScale","Interface scale");
    Scale.Description=LOCTEXT("UIScaleHelp","Resize the interface. 1.00 is the standard size.");
    Scale.Min=.8; Scale.Max=1.25; Scale.Step=.05; Scale.Default=1; Scale.bPreview=true; Out.Add(MoveTemp(Scale));
    FDreamSettingDefinition Size; Size.Id=TEXT("Accessibility.SubtitleSize"); Size.Category=TEXT("Accessibility"); Size.Order=505;
    Size.Label=LOCTEXT("SubtitleSize","Subtitle text size");
    Size.Description=LOCTEXT("SubtitleSizeHelp","Change dialogue text size. Use Preview to check readability.");
    Size.Min=18; Size.Max=40; Size.Step=2; Size.Default=24; Size.bPreview=true; Out.Add(MoveTemp(Size));
    return Out;
}
FDreamSettingsResult UDreamSettingsPlayerSubsystem::LoadPresentationSettings(TSharedRef<IDreamSettingsStore> MachineStore)
{
    if (bPresentationLoaded) return {};
    FDreamSettingsValues Saved; auto Result=ScopeStore(MachineStore)->Load(Saved); if (!Result.IsOk()) return Result;
    for (const auto& Definition:PresentationDefinitions()) if (const auto* Value=Saved.Find(Definition.Id))
    {
        FDreamSettingValue Normalized; Result=Definition.Normalize(*Value,Normalized);
        if (!Result.IsOk()) return Result;
        Result=Write(Definition.Id,Normalized); if (!Result.IsOk()) return Result;
    }
    bPresentationLoaded=true; return {};
}
TSet<FName> UDreamSettingsPlayerSubsystem::SettingIds()
{ TSet<FName> Ids; for (const auto& D : Definitions()) Ids.Add(D.Id); return Ids; }
TSharedRef<IDreamSettingsStore> UDreamSettingsPlayerSubsystem::ScopeStore(TSharedRef<IDreamSettingsStore> MachineStore) const
{
    const auto* Player=GetLocalPlayer();
    const int32 User=Player->GetPlatformUserId().IsValid() ? Player->GetPlatformUserId().GetInternalId() : Player->GetControllerId();
    return MakeShared<FDreamPlayerSettingsStore>(MachineStore,User,SettingIds());
}
FDreamSettingsResult UDreamSettingsPlayerSubsystem::Read(FName Id,FDreamSettingValue& Out) const
{
    if (Id==TEXT("Input.GamepadIcons")) { Out=FDreamSettingValue::Id(GamepadIconFamily); return {}; }
    if (Id==TEXT("Accessibility.ReducedMotion")) { Out=bReducedMotion ? 1 : 0; return {}; }
    if (Id==TEXT("Accessibility.UIScale")) { Out=UIScale; return {}; }
    if (Id==TEXT("Accessibility.SubtitleSize")) { Out=SubtitleSize; return {}; }
    const auto* Adapter=GameplayAdapter.IsValid() ? Cast<IDreamSettingsGameplayAdapter>(GameplayAdapter.Get()) : nullptr;
    return Adapter ? Adapter->ReadGameplaySetting(Id,Out) : FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("NoGameplayAdapter","This game has not connected its camera and input settings."));
}
FDreamSettingsResult UDreamSettingsPlayerSubsystem::Write(FName Id,const FDreamSettingValue& Value)
{
    if (Id==TEXT("Accessibility.UIScale") || Id==TEXT("Accessibility.SubtitleSize"))
    {
        const auto Presentation=PresentationDefinitions();
        const auto* Definition=Presentation.FindByPredicate([Id](const auto& D){ return D.Id==Id; });
        FDreamSettingValue Normalized; auto Result=Definition->Normalize(Value,Normalized);
        if (!Result.IsOk()) return Result;
        if (Id==TEXT("Accessibility.UIScale"))
        { if (UIScale!=Normalized.Number) { UIScale=Normalized.Number; OnUIScaleChanged.Broadcast(UIScale); } }
        else if (SubtitleSize!=Normalized.Number) { SubtitleSize=Normalized.Number; OnSubtitleSizeChanged.Broadcast(SubtitleSize); }
        return {};
    }
    if (Id==TEXT("Accessibility.ReducedMotion"))
    {
        if (Value.bToken || (Value.Number!=0 && Value.Number!=1))
            return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue,LOCTEXT("ReducedMotionBoolean","Reduced motion must be on or off."));
        const bool Reduced=Value.Number!=0;
        if (bReducedMotion!=Reduced) { bReducedMotion=Reduced; OnReducedMotionChanged.Broadcast(Reduced); }
        return {};
    }
    if (Id==TEXT("Input.GamepadIcons"))
    {
        const auto Presentation=PresentationDefinitions();
        const auto* Definition=Presentation.FindByPredicate([Id](const auto& D){ return D.Id==Id; });
        FDreamSettingValue Normalized; auto Result=Definition->Normalize(Value,Normalized); if (!Result.IsOk()) return Result;
        if (GamepadIconFamily!=Normalized.Token) { GamepadIconFamily=Normalized.Token; OnGamepadIconFamilyChanged.Broadcast(GamepadIconFamily); }
        return {};
    }
    auto* Adapter=GameplayAdapter.IsValid() ? Cast<IDreamSettingsGameplayAdapter>(GameplayAdapter.Get()) : nullptr;
    if (!Adapter) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("NoGameplayAdapter","This game has not connected its camera and input settings."));
    auto Result=Adapter->WriteGameplaySetting(Id,Value);
    if (Result.IsOk())
    {
        FDreamSettingValue Actual; Result=Adapter->ReadGameplaySetting(Id,Actual);
        if (Result.IsOk() && !Actual.Equals(Value)) Result=FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("GameplayReadback","The game did not accept this camera or input setting."));
        if (Result.IsOk()) Cached.Add(Id,Actual);
    }
    return Result;
}
FDreamSettingsResult UDreamSettingsPlayerSubsystem::AttachGameplayAdapter(UObject* Adapter,TSharedRef<IDreamSettingsStore> MachineStore)
{
    if (!Adapter || !Cast<IDreamSettingsGameplayAdapter>(Adapter))
        return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidDefinition,LOCTEXT("InvalidGameplayAdapter","A native gameplay settings adapter is required."));
    const auto PresentationResult=LoadPresentationSettings(MachineStore); if (!PresentationResult.IsOk()) return PresentationResult;
    if (GameplayAdapter.IsValid() && GameplayAdapter.Get()!=Adapter) for (const auto& D : GameplayDefinitions())
    { FDreamSettingValue Value; if (Read(D.Id,Value).IsOk()) Cached.Add(D.Id,Value); }
    GameplayAdapter=Adapter;
    FDreamSettingsValues Saved;
    if (!bLoaded)
    {
        const auto Result=ScopeStore(MachineStore)->Load(Saved); if (!Result.IsOk()) return Result;
    }
    FDreamSettingsValues Targets=Cached;
    for (const auto& Pair : Saved) if (SettingIds().Contains(Pair.Key)) Targets.Add(Pair.Key,Pair.Value);
    FDreamSettingsValues Before;
    for (const auto& D : GameplayDefinitions())
    {
        FDreamSettingValue Value;
        if (Read(D.Id,Value).IsOk()) Before.Add(D.Id,Value);
        if (const auto* Target=Targets.Find(D.Id))
        { FDreamSettingValue Normalized; const auto Result=D.Normalize(*Target,Normalized); if (!Result.IsOk()) return Result; Targets.Add(D.Id,Normalized); }
    }
    for (const auto& D : GameplayDefinitions()) if (const auto* Target=Targets.Find(D.Id))
    {
        const auto Result=Write(D.Id,*Target);
        if (!Result.IsOk())
        {
            for (const auto& Pair : Before) Write(Pair.Key,Pair.Value);
            return Result;
        }
    }
    bLoaded=true;
    return {};
}
FDreamSettingsResult UDreamSettingsPlayerSubsystem::RegisterSettings(FDreamSettingsRegistry& Registry)
{
    TWeakObjectPtr<UDreamSettingsPlayerSubsystem> Weak(this);
    for (auto D : Definitions())
    {
        const FName Id=D.Id;
        const auto Result=Registry.Register(D,MakeShared<FDreamCallbackSettingsProvider>(
            [Weak,Id](FDreamSettingValue& Out) { return Weak.IsValid() ? Weak->Read(Id,Out) : FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,FText::GetEmpty()); },
            [Weak,Id](const FDreamSettingValue& Value) { return Weak.IsValid() ? Weak->Write(Id,Value) : FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,FText::GetEmpty()); },
            [Weak,Id] { FDreamSettingValue Out; return Weak.IsValid() ? Weak->Read(Id,Out) : FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,FText::GetEmpty()); }));
        if (!Result.IsOk()) return Result;
    }
    return {};
}
#undef LOCTEXT_NAMESPACE
