#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DreamSettingsModel.h"
#include "DreamSettingsDisplay.h"
#include "Containers/Ticker.h"
#include "DreamSettingsSubsystem.generated.h"

class ULocalPlayer;
class UDreamSettingsAudioOutput;
class IDreamSettingsGameBenchmark;

/** Runtime owner. Sessions require a LocalPlayer, while shared machine writes are leased process-wide. */
UCLASS()
class DREAMSETTINGS_API UDreamSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	TSharedPtr<FDreamSettingsSession> OpenSession(ULocalPlayer* Player, FDreamSettingsResult& OutResult);
	TSharedPtr<FDreamSettingsRegistry> GetRegistry() const { return Registry; }
	const FDreamSettingsResult& GetInitializationResult() const { return InitializationResult; }
	FDreamSettingsResult RecommendGraphics(FDreamSettingsSession& Session);
	void SetGameBenchmark(ULocalPlayer* Player,TSharedPtr<IDreamSettingsGameBenchmark> Launcher);
	TSharedPtr<IDreamSettingsGameBenchmark> GetGameBenchmark(ULocalPlayer* Player) const;
	FDreamSettingsResult GameBenchmarkAvailability(ULocalPlayer* Player) const;
	FDreamSettingsResult LaunchGameBenchmark(ULocalPlayer* Player,FDreamSettingsSession& Session);
	FDreamSettingsResult AttachGameplayAdapter(ULocalPlayer* Player, UObject* Adapter);
	UDreamSettingsAudioOutput* GetAudioOutput() const { return AudioOutput; }
    const FDreamSettingsDisplayCapabilities& GetDisplayCapabilities() const { return DisplayCapabilities; }
private:
    FDreamSettingsDisplayCapabilities DisplayCapabilities;
    double NextDisplayQuery=0;
    void RefreshDisplayCapabilities();
	void InitializeSettings();
	bool bSettingsInitialized = false;
	UPROPERTY(Transient) TObjectPtr<UDreamSettingsAudioOutput> AudioOutput;
	TSharedPtr<FDreamSettingsRegistry> Registry;
	TSharedPtr<IDreamSettingsStore> Store;
	// Keep a closing session alive until an asynchronous device write and compensation finish.
	TSharedPtr<FDreamSettingsSession> ActiveSession;
	TWeakObjectPtr<ULocalPlayer> ActiveSessionPlayer;
	TMap<TWeakObjectPtr<ULocalPlayer>,TSharedPtr<IDreamSettingsGameBenchmark>> GameBenchmarks;
	bool bDispatchingGameBenchmark=false;
	FDreamSettingsValues StartupValues;
	FDreamSettingsResult InitializationResult;
	FTSTicker::FDelegateHandle TickHandle;
};
