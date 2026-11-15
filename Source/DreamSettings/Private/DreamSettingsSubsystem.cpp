#include "DreamSettingsSubsystem.h"
#include "DreamSettingsGameBenchmark.h"
#include "DreamSettings.h"
#include "DreamSettingsIniStore.h"
#include "DreamSettingsEngineCatalog.h"
#include "DreamSettingsUpscaling.h"
#include "DreamSettingsAudio.h"
#include "DreamSettingsAudioOutput.h"
#include "DreamSettingsPlayer.h"
#include "DreamSettingsKeyBindings.h"
#include "DreamSettingsFileTransaction.h"
#include "DreamSettingsAccessibility.h"
#include "DreamSettingsLocalization.h"
#include "DreamSettingsHDR.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Scalability.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
namespace
{
	TWeakPtr<FDreamSettingsSession> ProcessLease;
	void SynchronizeNativeDisplay(UGameInstance* Instance)
	{
		if (!Instance || !Instance->GetWorld() || Instance->GetWorld()->WorldType != EWorldType::Game || !GEngine->GameViewport) return;
		const auto* Viewport = GEngine->GameViewport->Viewport;
		if (!Viewport || Viewport->GetSizeXY().GetMin() <= 0) return;
		// Command-line window overrides and manual window resizing may bypass GameUserSettings at startup.
		auto* Settings = GEngine->GetGameUserSettings();
		Settings->SetFullscreenMode(Viewport->GetWindowMode());
		Settings->SetScreenResolution(Viewport->GetSizeXY());
	}

	class FVSyncProvider final : public IDreamSettingProvider
	{
	public:
		virtual FDreamSettingsResult Availability() const override
		{
			const auto* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
			if (!Variable || (Variable->GetFlags() & ECVF_SetByMask) > ECVF_SetByGameSetting)
				return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable, LOCTEXT("VSyncPolicy", "VSync is controlled by a higher-priority engine or command-line setting."));
			return {};
		}
		virtual FDreamSettingsResult Read(FDreamSettingValue& Out) const override
		{
			const auto* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
			if (!Variable || !GEngine || !GEngine->GetGameUserSettings()) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable, LOCTEXT("NoVSync", "VSync is unavailable."));
			Out = Variable->GetInt() != 0 ? 1 : 0;
			return {};
		}
		virtual FDreamSettingsResult Write(const FDreamSettingValue& Value) override
		{
			FDreamSettingValue Previous;
			auto Result = Read(Previous);
			if (!Result.IsOk()) return Result;
			auto* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
			if ((Variable->GetFlags() & ECVF_SetByMask) > ECVF_SetByGameSetting)
				return FDreamSettingsResult::Fail(EDreamSettingsError::Backend, LOCTEXT("VSyncPolicy", "VSync is controlled by a higher-priority engine or command-line setting."));
			Variable->Set(Value.Number != 0 ? 1 : 0, ECVF_SetByGameSetting);
			if ((Variable->GetInt() != 0) != (Value.Number != 0)) return FDreamSettingsResult::Fail(EDreamSettingsError::Backend, LOCTEXT("VSyncRejected", "The engine did not accept the VSync change."));
			GEngine->GetGameUserSettings()->SetVSyncEnabled(Value.Number != 0);
			return {};
		}
	};

}

void UDreamSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// GameInstance initialization precedes LocalPlayer creation and native input SaveGame loading.
	InitializationResult=FDreamSettingsFileTransaction(FPaths::ProjectSavedDir()).Recover();
	// Standalone GameInstance subsystems exist before PostEngineInit vendor modules.
	// Initialize on first game tick (or first menu open), after their feature registration.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		InitializeSettings();
		if (AudioOutput) AudioOutput->Tick();
		if (const auto Session = ActiveSession)
		{
            if (FPlatformTime::Seconds()>=NextDisplayQuery) RefreshDisplayCapabilities();
			if (!Session->IsDirty()) SynchronizeNativeDisplay(GetGameInstance());
			Session->Tick(FPlatformTime::Seconds());
			if (!Session->IsOpen() && !Session->IsBusy()) ActiveSession.Reset();
		}
		return true;
	}), 0.25f);
}

void UDreamSettingsSubsystem::RefreshDisplayCapabilities()
{
    NextDisplayQuery=FPlatformTime::Seconds()+1.0;
    const auto Current=UDreamSettingsDisplayLibrary::QueryCapabilities(GetGameInstance());
    if (!FDreamSettingsDisplayCapabilities::StaticStruct()->CompareScriptStruct(&Current,&DisplayCapabilities,PPF_None))
    {
        DisplayCapabilities=Current;
        if (ActiveSession) ActiveSession->OnChanged.Broadcast();
    }
}

void UDreamSettingsSubsystem::InitializeSettings()
{
	if (bSettingsInitialized) return;
	bSettingsInitialized = true;
	Registry = MakeShared<FDreamSettingsRegistry>();
	if (!InitializationResult.IsOk()) return;
	// Register the built-in catalogue's metadata before its rows. The generic registry's
	// name fallback remains available to unknown host categories. Keep existing localization keys.
	const FDreamSettingCategory Categories[]={
		{TEXT("Display"),NSLOCTEXT("DreamSettingsUI","DisplayCategory","Display"),{},10,EDreamSettingCategoryContent::DisplayInformation|EDreamSettingCategoryContent::ScenePreview},
		{TEXT("Graphics"),NSLOCTEXT("DreamSettingsUI","GraphicsCategory","Graphics"),{},20,EDreamSettingCategoryContent::ScenePreview|EDreamSettingCategoryContent::GameBenchmark},
		{TEXT("Audio"),NSLOCTEXT("DreamSettingsUI","AudioCategory","Audio"),{},30},
		{TEXT("Input"),NSLOCTEXT("DreamSettingsUI","InputCategory","Controls"),{},40},
		{TEXT("Gameplay"),NSLOCTEXT("DreamSettingsUI","GameplayCategory","Gameplay"),{},50},
		{TEXT("KeyboardKeys"),NSLOCTEXT("DreamSettingsUI","KeyboardKeysCategory","Keyboard / mouse"),{},60},
		{TEXT("GamepadKeys"),NSLOCTEXT("DreamSettingsUI","GamepadKeysCategory","Gamepad buttons"),{},70},
		{TEXT("Accessibility"),NSLOCTEXT("DreamSettingsUI","AccessibilityCategory","Accessibility"),{},80,EDreamSettingCategoryContent::ColourSamples},
		{TEXT("Localization"),NSLOCTEXT("DreamSettingsUI","LanguageCategory","Language"),{},90}
	};
	for (const auto& Category:Categories)
	{
		InitializationResult=Registry->RegisterCategory(Category);
		if (!InitializationResult.IsOk()) return;
	}
	FDreamSettingDefinition VSync;
	VSync.Id = DreamSettingIds::VSync;
	VSync.Category = TEXT("Display");
	VSync.Label = LOCTEXT("VSyncLabel", "Vertical sync");
	VSync.Description = LOCTEXT("VSyncDescription", "Synchronize presentation to the display. Applies when you save.");
	VSync.Kind = EDreamSettingKind::Toggle;
	VSync.Step = 1;
	VSync.Order = 30;
	Registry->Register(VSync, MakeShared<FVSyncProvider>());

	InitializationResult = RegisterDreamAudioSettings(*Registry);
	if (!InitializationResult.IsOk()) return;
	UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!UserSettings)
	{
		InitializationResult = FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable, LOCTEXT("NoUserSettings", "Game user settings are unavailable."));
		return;
	}
	Store = MakeShared<FDreamSettingsIniStore>(GGameUserSettingsIni, UserSettings->GetClass()->GetPathName());
    RefreshDisplayCapabilities();
	InitializationResult = RegisterDreamEngineSettings(*Registry, GetGameInstance());
	if (!InitializationResult.IsOk()) return;
    InitializationResult=RegisterDreamHDRSettings(*Registry,GetGameInstance());
    if (!InitializationResult.IsOk()) return;
	InitializationResult = RegisterDreamUpscalingSettings(*Registry, GetGameInstance());
	if (!InitializationResult.IsOk()) return;
	InitializationResult = RegisterDreamAccessibilitySettings(*Registry,GetGameInstance());
	if (!InitializationResult.IsOk()) return;
	InitializationResult = RegisterDreamLocalizationSettings(*Registry,GetGameInstance());
	if (!InitializationResult.IsOk()) return;
	FDreamSettingsValues Saved;
	InitializationResult = Store->Load(Saved);
	if (!InitializationResult.IsOk()) return;
	FDreamSettingsValues Recovery;
	InitializationResult = Store->LoadRecovery(Recovery);
	if (!InitializationResult.IsOk()) return;
	AudioOutput=NewObject<UDreamSettingsAudioOutput>(this);
	AudioOutput->OnDevicesChanged.AddWeakLambda(this,[this]
	{
		if (const auto Session=ActiveSession) Session->OnChanged.Broadcast();
	});
	const auto* Device=(Recovery.IsEmpty() ? Saved : Recovery).Find(TEXT("Audio.OutputDevice"));
	AudioOutput->Initialize(GetGameInstance(),Device && Device->bToken ? Device->Token : FString());
	InitializationResult=AudioOutput->RegisterSetting(*Registry);
	if (!InitializationResult.IsOk()) return;
	TArray<FName> Ordered;
	InitializationResult = Registry->Validate(Ordered);
	if (!InitializationResult.IsOk()) return;
	// Snapshot the whole editor state before restoring any group that can alter other providers.
	for (FName Id : Ordered)
	{
		if (Registry->Find(Id)->Derive) continue;
		FDreamSettingValue Current;
		if (Registry->Provider(Id)->Read(Current).IsOk()) StartupValues.Add(Id, Current);
	}
	for (FName Id : Ordered)
	{
		if (Registry->Find(Id)->Derive) continue;
		FDreamSettingValue Current;
		if (!Registry->Provider(Id)->Read(Current).IsOk()) continue;
		if (Id==TEXT("Audio.OutputDevice")) continue; // Restored asynchronously by the output service.
		const FDreamSettingValue* Value = Recovery.IsEmpty() ? Saved.Find(Id) : Recovery.Find(Id);
		if (Value)
		{
			FDreamSettingValue Normalized;
			auto Result = Registry->Find(Id)->Normalize(*Value, Normalized);
			const bool bNativeDisplay = Id == DreamSettingIds::VSync || Id == TEXT("Display.Resolution") || Id == TEXT("Display.WindowMode") || Id==TEXT("Display.HDR");
			if (Result.IsOk() && (!bNativeDisplay || !Recovery.IsEmpty()) && !Current.Equals(Normalized))
				Result = Registry->Provider(Id)->Write(Normalized);
			if (!Result.IsOk())
			{
				UE_LOG(LogDreamSettings, Warning, TEXT("Restore %s: %s"), *Id.ToString(), *Result.Message.ToString());
				if (!Recovery.IsEmpty()) { InitializationResult = Result; return; }
			}
		}
	}
	if (!Recovery.IsEmpty()) InitializationResult = Store->Save(Recovery);
}

FDreamSettingsResult UDreamSettingsSubsystem::ApplyCatalogOverride(const FDreamSettingsCatalogOverride& Override)
{
	InitializeSettings();
	if (!InitializationResult.IsOk()) return InitializationResult;
	if (ActiveSession) return FDreamSettingsResult::Fail(EDreamSettingsError::Conflict,
		LOCTEXT("OverrideWhileOpen", "Close the open settings page before changing the catalogue."));
	const auto Result=ApplyDreamSettingsCatalogOverride(*Registry,Override);
	if (!Result.IsOk()) return Result;
	TArray<FName> Ordered;
	return Registry->Validate(Ordered);
}

FDreamSettingsResult UDreamSettingsSubsystem::RegisterCategory(FDreamSettingCategory Category)
{
	InitializeSettings();
	if (!InitializationResult.IsOk()) return InitializationResult;
	if (ActiveSession) return FDreamSettingsResult::Fail(EDreamSettingsError::Conflict,
		LOCTEXT("CategoryWhileOpen", "Close the open settings page before adding a category."));
	const auto Result=Registry->RegisterCategory(MoveTemp(Category));
	if (!Result.IsOk()) return Result;
	TArray<FName> Ordered;
	return Registry->Validate(Ordered);
}

TSharedPtr<FDreamSettingsSession> UDreamSettingsSubsystem::OpenSession(ULocalPlayer* Player, FDreamSettingsResult& OutResult)
{
	check(IsInGameThread());
	InitializeSettings();
	OutResult = InitializationResult;
	if (!OutResult.IsOk()) return nullptr;
	const auto Lease = ProcessLease.Pin();
	if (!Player || Player->GetGameInstance() != GetGameInstance() || (Lease && Lease->IsOpen()))
	{
		OutResult = FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, LOCTEXT("Busy", "Another settings session is open, or the local player is unavailable."));
		return nullptr;
	}
	SynchronizeNativeDisplay(GetGameInstance());
	auto* PlayerSettings=Player->GetSubsystem<UDreamSettingsPlayerSubsystem>();
	OutResult=PlayerSettings->LoadPresentationSettings(Store.ToSharedRef());
	if (!OutResult.IsOk()) return nullptr;
	auto PlayerRegistry=MakeShared<FDreamSettingsRegistry>(*Registry);
	OutResult=PlayerSettings->RegisterSettings(*PlayerRegistry);
	if (!OutResult.IsOk()) return nullptr;
	auto Keys=MakeShared<FDreamSettingsKeyBindings>();
	OutResult=Keys->Initialize(Player,*PlayerRegistry);
	if (!OutResult.IsOk()) return nullptr;
	auto Session = MakeShared<FDreamSettingsSession>(PlayerRegistry, Keys->WrapStore(PlayerSettings->ScopeStore(Store.ToSharedRef())));
	OutResult = Session->Open();
	if (!OutResult.IsOk()) return nullptr;
	ActiveSession = Session;
	ActiveSessionPlayer = Player;
	ProcessLease = Session;
	return Session;
}

FDreamSettingsResult UDreamSettingsSubsystem::AttachGameplayAdapter(ULocalPlayer* Player,UObject* Adapter)
{
	InitializeSettings();
	if (!InitializationResult.IsOk()) return InitializationResult;
	if (!Player || Player->GetGameInstance()!=GetGameInstance())
		return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("WrongPlayer","The local player does not belong to this game instance."));
	auto Result=Player->GetSubsystem<UDreamSettingsPlayerSubsystem>()->AttachGameplayAdapter(Adapter,Store.ToSharedRef());
	// A level transition can temporarily remove the camera before the old UI finishes closing.
	// Retry its restoration after the new native consumer is attached, before granting a new lease.
	if (Result.IsOk() && ActiveSession && ActiveSession->IsClosing() && !ActiveSession->IsBusy())
		Result=ActiveSession->Close();
	return Result;
}

void UDreamSettingsSubsystem::SetGameBenchmark(ULocalPlayer* Player,TSharedPtr<IDreamSettingsGameBenchmark> Launcher)
{
    check(IsInGameThread());
    if (!Player || Player->GetGameInstance()!=GetGameInstance()) return;
    for (auto It=GameBenchmarks.CreateIterator();It;++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    if (Launcher) GameBenchmarks.Add(Player,MoveTemp(Launcher)); else GameBenchmarks.Remove(Player);
    if (ActiveSession && ActiveSessionPlayer.Get()==Player) ActiveSession->OnChanged.Broadcast();
}
TSharedPtr<IDreamSettingsGameBenchmark> UDreamSettingsSubsystem::GetGameBenchmark(ULocalPlayer* Player) const
{
    return Player && Player->GetGameInstance()==GetGameInstance() ? GameBenchmarks.FindRef(Player) : nullptr;
}
FDreamSettingsResult UDreamSettingsSubsystem::GameBenchmarkAvailability(ULocalPlayer* Player) const
{
    const auto Launcher=GetGameBenchmark(Player);
    if (!Launcher) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("NoGameBenchmark","This game has not provided a gameplay benchmark."));
    if (bDispatchingGameBenchmark) return FDreamSettingsResult::Fail(EDreamSettingsError::Conflict,LOCTEXT("GameBenchmarkStarting","The game benchmark is already starting."));
    return Launcher->Availability(Player);
}
FDreamSettingsResult UDreamSettingsSubsystem::LaunchGameBenchmark(ULocalPlayer* Player,FDreamSettingsSession& Session)
{
    check(IsInGameThread());
    if (ActiveSession.Get()!=&Session || ActiveSessionPlayer.Get()!=Player || !Session.IsOpen() || Session.IsClosing() || Session.IsBusy() || Session.IsAwaitingConfirmation() || Session.IsDirty())
        return FDreamSettingsResult::Fail(EDreamSettingsError::Conflict,LOCTEXT("CleanGameBenchmark","Apply or revert pending changes before running the game benchmark."));
    const auto Available=GameBenchmarkAvailability(Player); if (!Available.IsOk()) return Available;
    const auto Launcher=GetGameBenchmark(Player); // Keep it alive even if Start unregisters itself.
    TGuardValue<bool> Guard(bDispatchingGameBenchmark,true);
    return Launcher->Start(Player);
}

FDreamSettingsResult UDreamSettingsSubsystem::RecommendGraphics(FDreamSettingsSession& Session)
{
	if (Session.IsAwaitingConfirmation() || ActiveSession.Get() != &Session)
		return FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, LOCTEXT("BenchmarkUnavailable", "Finish the display confirmation before running automatic detection."));
	const auto Levels = Scalability::BenchmarkQualityLevels(10);
	const TPair<FName, int32> Qualities[] = {
		{TEXT("Graphics.ViewDistanceQuality"),Levels.ViewDistanceQuality},{TEXT("Graphics.ShadowQuality"),Levels.ShadowQuality},
		{TEXT("Graphics.GlobalIlluminationQuality"),Levels.GlobalIlluminationQuality},{TEXT("Graphics.ReflectionsQuality"),Levels.ReflectionQuality},
		{TEXT("Graphics.AAQuality"),Levels.AntiAliasingQuality},{TEXT("Graphics.TextureQuality"),Levels.TextureQuality},
		{TEXT("Graphics.EffectsQuality"),Levels.EffectsQuality},{TEXT("Graphics.PostProcessQuality"),Levels.PostProcessQuality},
		{TEXT("Graphics.FoliageQuality"),Levels.FoliageQuality},{TEXT("Graphics.ShadingQuality"),Levels.ShadingQuality}};
	FDreamSettingsValues Changes;
	for (const auto& Pair : Qualities)
	{
		const auto& Options = Registry->Find(Pair.Key)->Options;
		Changes.Add(Pair.Key,FDreamSettingValue::Id(Options[FMath::Clamp(Pair.Value,0,Options.Num()-1)].Id));
	}
	const FString Method=Session.GetPending().FindRef(TEXT("Graphics.UpscalingMethod")).Token;
	if (Method!=TEXT("dlss") && Method!=TEXT("fsr") && Method!=TEXT("xess"))
		Changes.Add(TEXT("Graphics.ResolutionScale"), Levels.ResolutionQuality <= 0 ? 100 : FMath::Clamp(Levels.ResolutionQuality,33.f,100.f));
	return Session.StageBatch(Changes);
}

void UDreamSettingsSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (auto Session = ActiveSession) Session->Close();
	// SoundClasses are shared with the editor. Never leave PIE preview values on those objects.
	if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE && Registry)
	{
		TArray<FName> Ordered;
		Registry->Validate(Ordered);
		for (FName Id : Ordered)
		{
			const auto* Value = StartupValues.Find(Id);
			if (Registry->Find(Id)->Derive) continue;
		FDreamSettingValue Current;
			if (!Value || !Registry->Provider(Id)->Read(Current).IsOk() || Current.Equals(*Value)) continue;
			const auto Result = Registry->Provider(Id)->Write(*Value);
			if (!Result.IsOk()) UE_LOG(LogDreamSettings, Warning, TEXT("PIE restoration: %s"), *Result.Message.ToString());
		}
	}
	ActiveSession.Reset();
	ActiveSessionPlayer.Reset(); GameBenchmarks.Reset();
	Store.Reset();
	Registry.Reset();
	Super::Deinitialize();
}
#undef LOCTEXT_NAMESPACE
