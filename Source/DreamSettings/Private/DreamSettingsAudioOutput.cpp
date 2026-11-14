#include "DreamSettingsAudioOutput.h"
#include "DreamSettings.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "AudioMixerDevice.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
namespace
{
    FDreamSettingsResult AudioPending() { return FDreamSettingsResult::Fail(EDreamSettingsError::Pending,LOCTEXT("AudioSwitchPending", "Waiting for the audio output device…")); }
    class FAudioOutputProvider final : public IDreamSettingProvider
    {
        TWeakObjectPtr<UDreamSettingsAudioOutput> Service;
    public:
        explicit FAudioOutputProvider(UDreamSettingsAudioOutput* In) : Service(In) {}
        FDreamSettingsResult Availability() const override { return Service.IsValid() ? Service->Availability() : Missing(); }
        FDreamSettingsResult Read(FDreamSettingValue& Out) const override { return Service.IsValid() ? Service->Read(Out) : Missing(); }
        FDreamSettingsResult Write(const FDreamSettingValue& In) override { return Service.IsValid() ? Service->Write(In) : Missing(); }
        FDreamSettingsResult PollWrite() override { return Service.IsValid() ? Service->PollWrite() : Missing(); }
        static FDreamSettingsResult Missing() { return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("AudioOutputUnavailable", "Audio output is unavailable.")); }
    };
}
UWorld* UDreamSettingsAudioOutput::GetWorld() const { return Instance.IsValid() ? Instance->GetWorld() : nullptr; }
void UDreamSettingsAudioOutput::Initialize(UGameInstance* InInstance,FString SavedDevice)
{
    Instance=InInstance; InitialDevice=SavedDevice.IsEmpty() ? TEXT("system") : MoveTemp(SavedDevice);
    QueryDevices();
}
FString UDreamSettingsAudioOutput::ActualDeviceId() const
{
    if (!GetWorld()) return {};
    const auto Device=GetWorld()->GetAudioDevice();
    if (!Device.IsValid() || !Device->bAudioMixerModuleLoaded) return {};
    auto* Mixer=static_cast<Audio::FMixerDevice*>(Device.GetAudioDevice());
    if (!Mixer->GetAudioMixerPlatform()) return {};
    const auto Info=Mixer->GetAudioMixerPlatform()->GetPlatformDeviceInfo();
    return Info.NumChannels>0 ? Info.DeviceId : FString();
}
FString UDreamSettingsAudioOutput::DefaultDeviceId() const
{
    for (const auto& Device : Devices) if (Device.bIsSystemDefault) return Device.DeviceId;
    return {};
}
void UDreamSettingsAudioOutput::QueryDevices()
{
    if (bQueryInFlight || !GetWorld() || ActualDeviceId().IsEmpty()) return;
    bQueryInFlight=true;
    FOnAudioOutputDevicesObtained Callback; Callback.BindDynamic(this,&ThisClass::OnDevices);
    UAudioMixerBlueprintLibrary::GetAvailableAudioOutputDevices(this,Callback);
}
void UDreamSettingsAudioOutput::OnDevices(const TArray<FAudioOutputDeviceInfo>& AvailableDevices)
{
    check(IsInGameThread());
    bool bChanged=Devices.Num()!=AvailableDevices.Num();
    if (!bChanged) for (int I=0;I<Devices.Num();++I)
        bChanged |= Devices[I].DeviceId!=AvailableDevices[I].DeviceId || Devices[I].Name!=AvailableDevices[I].Name || Devices[I].bIsSystemDefault!=AvailableDevices[I].bIsSystemDefault;
    Devices=AvailableDevices; bQueryInFlight=false;
    const bool bFirst=!bHaveDevices; bHaveDevices=true;
    if (bFirst)
    {
        const FString Actual=ActualDeviceId();
        SelectedDevice=Actual==DefaultDeviceId() ? TEXT("system") : Actual;
        FString Target=InitialDevice;
        if (Target!=TEXT("system") && !Devices.ContainsByPredicate([&](const auto& D) { return D.DeviceId==Target; }))
        {
            Target=TEXT("system"); Notice=LOCTEXT("SavedDeviceMissing", "The saved output device is disconnected. Using the system default.");
        }
        if (GetWorld()->WorldType==EWorldType::Game)
        {
            const auto Result=BeginSwap(Target); bBackgroundSwap=Result.IsPending();
            if (!Result.IsOk() && !Result.IsPending()) Notice=Result.Message;
        }
        bInitializing=bBackgroundSwap;
    }
    if (bChanged || bFirst) OnDevicesChanged.Broadcast();
}
void UDreamSettingsAudioOutput::Tick()
{
    if (FPlatformTime::Seconds()>=NextQuery && !bSwapPending) { NextQuery=FPlatformTime::Seconds()+1; QueryDevices(); }
    if (bBackgroundSwap)
    {
        const auto Result=PollWrite();
        if (!Result.IsPending())
        {
            bBackgroundSwap=false; bInitializing=false;
            if (!Result.IsOk()) { Notice=Result.Message; UE_LOG(LogDreamSettings,Warning,TEXT("Audio output: %s"),*Notice.ToString()); }
        }
    }
    if (!bInitializing && !bSwapPending && bHaveDevices)
    {
        if (SelectedDevice!=TEXT("system") && !Devices.ContainsByPredicate([&](const auto& D) { return D.DeviceId==SelectedDevice; }))
        {
            SelectedDevice=TEXT("system"); Notice=LOCTEXT("DeviceDisconnected", "The output device was disconnected. Using the system default.");
        }
        const FString Default=DefaultDeviceId();
        if (GetWorld() && GetWorld()->WorldType==EWorldType::Game && SelectedDevice==TEXT("system") && !Default.IsEmpty() && ActualDeviceId()!=Default)
        {
            const auto Result=BeginSwap(TEXT("system")); bBackgroundSwap=Result.IsPending();
            if (!Result.IsOk() && !Result.IsPending()) Notice=Result.Message;
        }
    }
}
FDreamSettingsResult UDreamSettingsAudioOutput::Availability() const
{
    if (!GetWorld() || GetWorld()->WorldType!=EWorldType::Game)
        return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("AudioDeviceStandalone", "Use Standalone Game to change the output device."));
    if (bInitializing || bBackgroundSwap) return AudioPending();
    if (ActualDeviceId().IsEmpty() || Devices.IsEmpty()) return FAudioOutputProvider::Missing();
    return {};
}
FDreamSettingsResult UDreamSettingsAudioOutput::OptionAvailability(const FString& Id) const
{
    const auto Result=Availability(); if (!Result.IsOk()) return Result;
    if ((Id==TEXT("system") && !DefaultDeviceId().IsEmpty()) || Devices.ContainsByPredicate([&](const auto& D) { return D.DeviceId==Id; })) return {};
    return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("DeviceMissing", "This output device is no longer connected."));
}
FDreamSettingsResult UDreamSettingsAudioOutput::Read(FDreamSettingValue& Out) const
{
    if (bInitializing || bBackgroundSwap) return AudioPending();
    const FString Actual=ActualDeviceId();
    if (Actual.IsEmpty()) return FAudioOutputProvider::Missing();
    const FString Effective=(Actual==SelectedDevice || (SelectedDevice==TEXT("system") && Actual==DefaultDeviceId())) ? SelectedDevice
        : Actual==DefaultDeviceId() ? TEXT("system") : Actual;
    Out=FDreamSettingValue::Id(Effective); return {};
}
FDreamSettingsResult UDreamSettingsAudioOutput::Write(const FDreamSettingValue& Value)
{
    auto Result=OptionAvailability(Value.Token); if (!Result.IsOk()) return Result;
    Notice=FText::GetEmpty(); return BeginSwap(Value.Token);
}
FDreamSettingsResult UDreamSettingsAudioOutput::BeginSwap(const FString& LogicalId)
{
    if (bSwapPending) return FDreamSettingsResult::Fail(EDreamSettingsError::Conflict,LOCTEXT("AudioSwapBusy", "An audio output change is already in progress."));
    const FString Physical=LogicalId==TEXT("system") ? DefaultDeviceId() : LogicalId;
    if (Physical.IsEmpty()) return FAudioOutputProvider::Missing();
    if (ActualDeviceId()==Physical) { SelectedDevice=LogicalId; return {}; }
    RequestedLogical=LogicalId; RequestedPhysical=Physical; bSwapPending=true; bSwapCallback=false;
    SwapStarted=FPlatformTime::Seconds(); SwapResult=ESwapAudioOutputDeviceResultState::None;
    FOnCompletedDeviceSwap Callback; Callback.BindDynamic(this,&ThisClass::OnSwapped);
    UAudioMixerBlueprintLibrary::SwapAudioOutputDevice(this,Physical,Callback);
    return AudioPending();
}
void UDreamSettingsAudioOutput::OnSwapped(const FSwapAudioOutputResult& Result)
{
    check(IsInGameThread());
    if (!bSwapPending || Result.RequestedDeviceId!=RequestedPhysical) return;
    bSwapCallback=true; SwapResult=Result.Result;
}
FDreamSettingsResult UDreamSettingsAudioOutput::PollWrite()
{
    if (!bSwapPending) return FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("NoAudioSwap", "No output-device operation is pending."));
    // UE exposes no cancellation. Do not release ownership while a late callback can still change the device.
    if (!bSwapCallback) return AudioPending();
    if (SwapResult==ESwapAudioOutputDeviceResultState::Success && ActualDeviceId()!=RequestedPhysical && FPlatformTime::Seconds()-SwapStarted<10) return AudioPending();
    bSwapPending=false;
    if (SwapResult!=ESwapAudioOutputDeviceResultState::Success || ActualDeviceId()!=RequestedPhysical)
        return FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("AudioSwapFailed", "The audio output device could not be changed. Previous settings will be restored."));
    SelectedDevice=RequestedLogical;
    return {};
}
TArray<FDreamSettingOption> UDreamSettingsAudioOutput::Options() const
{
    TArray<FDreamSettingOption> Result{{TEXT("system"),LOCTEXT("SystemAudioDevice", "System default")}};
    for (const auto& Device : Devices) Result.Add({Device.DeviceId,FText::FromString(Device.Name)});
    return Result;
}
FText UDreamSettingsAudioOutput::Description() const
{
    const FString Actual=ActualDeviceId();
    const auto* Device=Devices.FindByPredicate([&](const auto& D) { return D.DeviceId==Actual; });
    const FText Active=Device ? FText::Format(LOCTEXT("AudioCurrentOutput", "Current output: {0}. Changes take effect when you apply."),FText::FromString(Device->Name))
        : LOCTEXT("AudioNoCurrentOutput", "No output device is currently available.");
    return Notice.IsEmpty() ? Active : FText::Format(LOCTEXT("AudioOutputNotice", "{0} {1}"),Notice,Active);
}
FDreamSettingsResult UDreamSettingsAudioOutput::RegisterSetting(FDreamSettingsRegistry& Registry)
{
    TWeakObjectPtr<UDreamSettingsAudioOutput> Weak(this);
    FDreamSettingDefinition D; D.Id=TEXT("Audio.OutputDevice"); D.Category=TEXT("Audio"); D.Order=310;
    D.Label=LOCTEXT("OutputDevice", "Output device"); D.Description=LOCTEXT("OutputDeviceDescription", "Select a connected output device, or follow the system default.");
    D.Kind=EDreamSettingKind::Choice; D.Default=FDreamSettingValue::Id(TEXT("system"));
    D.bFollowExternalChanges=true; D.bRetryUnavailable=true; D.bAsyncWrite=true;
    D.DynamicOptions=[Weak] { return Weak.IsValid() ? Weak->Options() : TArray<FDreamSettingOption>{}; };
    D.DynamicDescription=[Weak](const FDreamSettingsValues&) { return Weak.IsValid() ? Weak->Description() : FText::GetEmpty(); };
    D.OptionAvailability=[Weak](const FString& Id,const FDreamSettingsValues&) { return Weak.IsValid() ? Weak->OptionAvailability(Id) : FAudioOutputProvider::Missing(); };
    return Registry.Register(D,MakeShared<FAudioOutputProvider>(this));
}
#undef LOCTEXT_NAMESPACE
