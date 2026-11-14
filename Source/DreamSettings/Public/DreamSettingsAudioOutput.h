#pragma once
#include "CoreMinimal.h"
#include "AudioMixerBlueprintLibrary.h"
#include "DreamSettingsModel.h"
#include "DreamSettingsAudioOutput.generated.h"

class UGameInstance;

/** Enumerates endpoints and owns one asynchronous device swap at a time. */
UCLASS()
class DREAMSETTINGS_API UDreamSettingsAudioOutput : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(UGameInstance* InInstance,FString SavedDevice);
    void Tick();
    virtual UWorld* GetWorld() const override;
    FDreamSettingsResult RegisterSetting(FDreamSettingsRegistry& Registry);
    FDreamSettingsResult Read(FDreamSettingValue& Out) const;
    FDreamSettingsResult Write(const FDreamSettingValue& Value);
    FDreamSettingsResult PollWrite();
    FDreamSettingsResult Availability() const;
    FDreamSettingsResult OptionAvailability(const FString& Id) const;
    TArray<FDreamSettingOption> Options() const;
    FString ActualDeviceId() const;
    FText Description() const;
    bool IsReady() const { return !bInitializing; }
    DECLARE_MULTICAST_DELEGATE(FDevicesChanged);
    FDevicesChanged OnDevicesChanged;
private:
    void QueryDevices();
    FDreamSettingsResult BeginSwap(const FString& LogicalId);
    FString DefaultDeviceId() const;
    UFUNCTION() void OnDevices(const TArray<FAudioOutputDeviceInfo>& AvailableDevices);
    UFUNCTION() void OnSwapped(const FSwapAudioOutputResult& Result);
    TWeakObjectPtr<UGameInstance> Instance;
    TArray<FAudioOutputDeviceInfo> Devices;
    FString InitialDevice,SelectedDevice=TEXT("system"),RequestedLogical,RequestedPhysical;
    FText Notice;
    bool bInitializing=true,bQueryInFlight=false,bHaveDevices=false,bSwapPending=false,bSwapCallback=false,bBackgroundSwap=false;
    ESwapAudioOutputDeviceResultState SwapResult=ESwapAudioOutputDeviceResultState::None;
    double NextQuery=0,SwapStarted=0;
};
