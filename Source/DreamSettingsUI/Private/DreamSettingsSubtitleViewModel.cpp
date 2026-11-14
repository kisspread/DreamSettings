#include "DreamSettingsSubtitleViewModel.h"
#include "DreamSettingsPlayer.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "SubtitleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Internationalization/Internationalization.h"
#include "HAL/PlatformTime.h"

#define LOCTEXT_NAMESPACE "DreamSettingsUI"
bool UDreamSettingsSubtitleViewModel::Initialize(ULocalPlayer* Player)
{
    Shutdown(); World=Player ? Player->GetWorld() : nullptr;
    Preferences=Player ? Player->GetSubsystem<UDreamSettingsPlayerSubsystem>() : nullptr;
    if (Preferences.IsValid())
    {
        Preferences->OnSubtitleSizeChanged.AddUObject(this,&ThisClass::SizeChanged);
        SizeChanged(Preferences->GetSubtitleSize());
    }
    if (!World.IsValid() || World->WorldType!=EWorldType::Game) { World.Reset(); return false; }
    SubtitleHandle=FSubtitleManager::GetSubtitleManager()->OnSetSubtitleText().AddUObject(this,&ThisClass::Receive);
    CultureHandle=FInternationalization::Get().OnCultureChanged().AddUObject(this,&ThisClass::RefreshPreviewLanguage);
    TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float)
    {
        if (!World.IsValid()) { Receive(FText::GetEmpty()); return true; }
        if (PreviewUntil>0 && FPlatformTime::Seconds()>=PreviewUntil) StopPreview();
        // The native manager may not emit an empty cue when subtitles are disabled. Clear existing
        // text independently, including changes made by other code through the native API.
        if (!GEngine || GEngine->bSubtitlesForcedOff || !UGameplayStatics::AreSubtitlesEnabled()) Receive(FText::GetEmpty());
        return true;
    }));
    return true;
}
void UDreamSettingsSubtitleViewModel::SizeChanged(double Value)
{
    DREAM_VM_SET(FontSize,static_cast<float>(Value));
    DREAM_VM_SET(BandSize,FVector2D(0,Value*4.5));
    DREAM_VM_SET(PreviewSize,FVector2D(0,Value*2.5));
}
void UDreamSettingsSubtitleViewModel::Receive(const FText& Text)
{
    const FText Visible=World.IsValid() && GEngine && !GEngine->bSubtitlesForcedOff && UGameplayStatics::AreSubtitlesEnabled() ? Text : FText::GetEmpty();
    DREAM_VM_SET(Caption,Visible); DREAM_VM_SET(HasText,!Visible.IsEmpty());
}
bool UDreamSettingsSubtitleViewModel::Preview()
{
    if (!World.IsValid() || !GEngine || GEngine->bSubtitlesForcedOff || !UGameplayStatics::AreSubtitlesEnabled()) return false;
    PreviewUntil=FPlatformTime::Seconds()+4.0;
    RefreshPreviewLanguage(); return true;
}
void UDreamSettingsSubtitleViewModel::RefreshPreviewLanguage()
{
    if (!World.IsValid() || PreviewUntil<=0) return;
    FSubtitleManager::GetSubtitleManager()->SetMovieSubtitle(this,
        {LOCTEXT("SubtitlePreviewLine","This is how dialogue subtitles will appear.").ToString()});
}
void UDreamSettingsSubtitleViewModel::StopPreview()
{
    if (PreviewUntil<=0) return;
    PreviewUntil=0;
    // Remove only our sample. Never kill a host game's dialogue or another display's subtitles.
    FSubtitleManager::GetSubtitleManager()->SetMovieSubtitle(this,{});
    Receive(FText::GetEmpty());
}
void UDreamSettingsSubtitleViewModel::Shutdown()
{
    if (Preferences.IsValid()) Preferences->OnSubtitleSizeChanged.RemoveAll(this);
    Preferences.Reset();
    StopPreview();
    if (SubtitleHandle.IsValid()) FSubtitleManager::GetSubtitleManager()->OnSetSubtitleText().Remove(SubtitleHandle);
    SubtitleHandle.Reset();
    if (CultureHandle.IsValid()) FInternationalization::Get().OnCultureChanged().Remove(CultureHandle);
    CultureHandle.Reset();
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset();
    World.Reset(); DREAM_VM_SET(Caption,FText::GetEmpty()); DREAM_VM_SET(HasText,false);
}
void UDreamSettingsSubtitleViewModel::BeginDestroy() { Shutdown(); Super::BeginDestroy(); }
#undef LOCTEXT_NAMESPACE
