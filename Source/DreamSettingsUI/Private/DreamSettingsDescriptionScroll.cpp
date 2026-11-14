#include "DreamSettingsDescriptionScroll.h"
#include "DreamSettingsViewModel.h"
#include "DreamSettingsDetails.h"
#include "DreamSettingsNavigationMemory.h"
#include "DreamSettingsPlayer.h"
#include "Controls/DreamScrollBox.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

UDreamSettingsDescriptionScroll::UDreamSettingsDescriptionScroll()
{ bStartWithTickEnabled=true; bTickEvenWhenPaused=true; }

bool UDreamSettingsDescriptionScroll::Bind()
{
    auto* Box=Cast<UDreamScrollBox>(GetWidget());
    if (!Box) return false;
    for (auto* Node=GetWidget();Node;Node=Node->GetParent())
        if (auto* Screen=Cast<UDreamUserWidget>(Node))
            if (auto* VM=Cast<UDreamSettingsViewModel>(Screen->GetViewModel(TEXT("Settings"))))
                if (auto* Memory=VM->GetNavigationMemory())
                {
                    Release(); Scroll=Box; Presenter=VM;
                    Preferences=Memory->GetLocalPlayer()->GetSubsystem<UDreamSettingsPlayerSubsystem>();
                    VM->OnDetailsOpened.AddUObject(this,&ThisClass::BeginVisit);
                    ScrollHandle=Box->GetOnScrolledEvent().AddUObject(this,&ThisClass::Scrolled);
                    LastOffset=Box->GetScrollOffset(); LastTime=FPlatformTime::Seconds();
                    return Preferences.IsValid();
                }
    return false;
}
void UDreamSettingsDescriptionScroll::OnEnable()
{
    Super::OnEnable(); Release();
    SetCanExecuteTick(GetWorld() && GetWorld()->IsGameWorld());
}
void UDreamSettingsDescriptionScroll::BeginVisit()
{ bVisit=false; bManual=false; bEligible=false; bResetPending=true; }
void UDreamSettingsDescriptionScroll::Scrolled(float)
{
    if (!Scroll.IsValid()) return;
    const float Offset=Scroll->GetScrollOffset();
    if (bVisit && !bWriting && !FMath::IsNearlyEqual(Offset,LastOffset,.25f)) bManual=true;
    LastOffset=Offset;
}
void UDreamSettingsDescriptionScroll::WriteOffset(float Offset)
{
    TGuardValue<bool> Guard(bWriting,true);
    Scroll->SetScrollOffset(Offset); LastOffset=Scroll->GetScrollOffset();
}
void UDreamSettingsDescriptionScroll::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if ((!Scroll.IsValid() || !Presenter.IsValid() || !Preferences.IsValid()) && !Bind()) return;
    const double Now=FPlatformTime::Seconds();
    const double Elapsed=FMath::Clamp(Now-LastTime,0.0,.1); LastTime=Now;
    auto* VM=Presenter.Get();
    if (!VM->ShowDetails || !GetWidget()->GetWidgetActiveInHierarchy())
    { bVisit=false; bEligible=false; return; }
    const FName Current=VM->Details ? VM->Details->SettingId : NAME_None;
    if (!bVisit || Setting!=Current)
    {
        bVisit=true; Setting=Current; bManual=false; bResetPending=true; bForward=true; bEligible=false; LastExtent=-1;
        LastOffset=Scroll->GetScrollOffset();
    }
    const float Extent=Scroll->GetScrollOffsetOfEnd();
    const bool Allowed=VM->Ready && !VM->PreparingText && VM->AllowsDescriptionAutoScroll() &&
        !Preferences->GetReducedMotion() && Extent>1 && FMath::IsFinite(UnitsPerSecond) && UnitsPerSecond>0;
    if (!Allowed) { bEligible=false; return; }
    if (!bEligible || !FMath::IsNearlyEqual(Extent,LastExtent,1.f)) ResumeAt=Now+FMath::Max(0.f,InitialDelay);
    bEligible=true; LastExtent=Extent;
    if (bManual) return;
    if (bResetPending) { WriteOffset(0); bResetPending=false; ResumeAt=Now+FMath::Max(0.f,InitialDelay); }
    if (Scroll->GetIsScrolling()) { ResumeAt=Now+FMath::Max(0.f,InitialDelay); return; }
    if (Now<ResumeAt) return;
    const float Next=FMath::Clamp(Scroll->GetScrollOffset()+(bForward ? 1.f : -1.f)*UnitsPerSecond*Elapsed,0.f,Extent);
    WriteOffset(Next);
    if ((bForward && Next>=Extent) || (!bForward && Next<=0))
    { bForward=!bForward; ResumeAt=Now+FMath::Max(0.f,EndPause); }
}
void UDreamSettingsDescriptionScroll::Release()
{
    if (Presenter.IsValid()) Presenter->OnDetailsOpened.RemoveAll(this);
    if (Scroll.IsValid()) Scroll->GetOnScrolledEvent().Remove(ScrollHandle);
    ScrollHandle.Reset(); Scroll.Reset(); Presenter.Reset(); Preferences.Reset();
    bVisit=bManual=bWriting=bEligible=false; bResetPending=bForward=true; Setting=NAME_None; LastExtent=-1;
}
void UDreamSettingsDescriptionScroll::OnDisable() { Release(); SetCanExecuteTick(false); Super::OnDisable(); }
void UDreamSettingsDescriptionScroll::OnUnregister() { Release(); SetCanExecuteTick(false); Super::OnUnregister(); }
