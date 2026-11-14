#include "DreamSettingsAudio.h"
#include "DreamSettingsConfiguration.h"
#include "DreamSettingsProvider.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
USoundClass* UDreamSettingsConfiguration::ResolveSoundClass(FName Id) const
{
    if (Id==TEXT("Audio.MasterVolume")) return MasterSoundClass.IsNull()
        ? Cast<USoundClass>(GetDefault<UAudioSettings>()->DefaultSoundClassName.TryLoad()) : MasterSoundClass.LoadSynchronous();
    if (Id==TEXT("Audio.MusicVolume")) return MusicSoundClass.LoadSynchronous();
    if (Id==TEXT("Audio.EffectsVolume")) return EffectsSoundClass.LoadSynchronous();
    if (Id==TEXT("Audio.AmbientVolume")) return AmbientSoundClass.LoadSynchronous();
    if (Id==TEXT("Audio.UIVolume")) return UISoundClass.LoadSynchronous();
    if (Id==TEXT("Audio.VoiceVolume")) return VoiceSoundClass.LoadSynchronous();
    return nullptr;
}
FDreamSettingsResult RegisterDreamAudioSettings(FDreamSettingsRegistry& Registry)
{
    const auto* Config=GetDefault<UDreamSettingsConfiguration>();
    USoundClass* Master=Config->ResolveSoundClass(TEXT("Audio.MasterVolume"));
    TSet<USoundClass*> Used;
    const TPair<FName,FText> Channels[]={
        {TEXT("Audio.MasterVolume"),LOCTEXT("MasterVolume", "Master volume")},
        {TEXT("Audio.MusicVolume"),LOCTEXT("MusicVolume", "Music volume")},
        {TEXT("Audio.EffectsVolume"),LOCTEXT("EffectsVolume", "Effects volume")},
        {TEXT("Audio.AmbientVolume"),LOCTEXT("AmbientVolume", "Ambient volume")},
        {TEXT("Audio.UIVolume"),LOCTEXT("UIVolume", "Interface volume")},
        {TEXT("Audio.VoiceVolume"),LOCTEXT("VoiceVolume", "Voice volume")}};
    int32 Order=300;
    for (const auto& Channel : Channels)
    {
        USoundClass* SoundClass=Config->ResolveSoundClass(Channel.Key);
        FDreamSettingsResult RouteResult;
        if (!SoundClass || !Master)
            RouteResult=FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("AudioRouteMissing", "This game's audio channel has not been configured."));
        else if (Used.Contains(SoundClass))
            RouteResult=FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("AudioRouteDuplicate", "Audio channels must use distinct SoundClasses."));
        else if (SoundClass!=Master)
        {
            USoundClass* Parent=SoundClass->ParentClass.Get(); TSet<USoundClass*> Visited;
            while (Parent && Parent!=Master && !Visited.Contains(Parent)) { Visited.Add(Parent); Parent=Parent->ParentClass; }
            if (Parent!=Master) RouteResult=FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("AudioRouteParent", "This audio channel must be routed through Master."));
        }
        if (RouteResult.IsOk()) Used.Add(SoundClass);
        auto KeepAlive=MakeShared<TStrongObjectPtr<USoundClass>>(SoundClass);
        FDreamSettingDefinition D; D.Id=Channel.Key; D.Category=TEXT("Audio"); D.Label=Channel.Value;
        D.Description=LOCTEXT("ChannelVolumeDescription", "Preview this channel's volume. Apply to save, or Revert to restore the previous level.");
        D.Default=1; D.bPreview=true; D.Order=Order++;
        const auto Result=Registry.Register(D,MakeShared<FDreamCallbackSettingsProvider>(
            [KeepAlive,RouteResult](FDreamSettingValue& V)
            {
                if (!RouteResult.IsOk()) return RouteResult;
                V=KeepAlive->Get()->Properties.Volume; return FDreamSettingsResult{};
            },
            [KeepAlive,RouteResult](const FDreamSettingValue& V)
            {
                if (!RouteResult.IsOk()) return RouteResult;
                KeepAlive->Get()->Properties.Volume=static_cast<float>(V.Number);
                return FDreamSettingValue(KeepAlive->Get()->Properties.Volume).Equals(V) ? FDreamSettingsResult{}
                    : FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("AudioVolumeRejected", "The audio engine rejected the volume change."));
            },[RouteResult] { return RouteResult; }));
        if (!Result.IsOk()) return Result;
    }
    return {};
}
#undef LOCTEXT_NAMESPACE
