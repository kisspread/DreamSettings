#include "DreamSettingsMotionPolicy.h"
#include "DreamSettingsPlayer.h"
#include "DreamSettingsViewModel.h"
#include "DreamSettingsNavigationMemory.h"
#include "DreamSettingsNavigationScope.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamThrobber.h"
#include "Interaction/UINavigationInputSelectionHandler.h"

UDreamSettingsMotionPolicy::UDreamSettingsMotionPolicy()
{ bStartWithTickEnabled=true; bTickEvenWhenPaused=true; }
void UDreamSettingsMotionPolicy::BindPlayer(ULocalPlayer* Player)
{
    Release();
    Preferences=Player ? Player->GetSubsystem<UDreamSettingsPlayerSubsystem>() : nullptr;
    if (Preferences.IsValid())
    {
        Preferences->OnReducedMotionChanged.AddUObject(this,&ThisClass::Changed);
        Changed(Preferences->GetReducedMotion());
    }
}
void UDreamSettingsMotionPolicy::TryBind()
{
    if (!GetWorld() || !GetWorld()->IsGameWorld()) { SetCanExecuteTick(false); return; }
    for (auto* Node=GetWidget();Node;Node=Node->GetParent())
        if (auto* Screen=Cast<UDreamUserWidget>(Node))
            if (auto* VM=Cast<UDreamSettingsViewModel>(Screen->GetViewModel(TEXT("Settings"))))
                if (auto* Memory=VM->GetNavigationMemory()) { BindPlayer(Memory->GetLocalPlayer()); return; }
}
void UDreamSettingsMotionPolicy::OnEnable()
{
    Super::OnEnable();
    SetCanExecuteTick(true); // A view model may be assigned after this tree is enabled.
    if (Preferences.IsValid()) BindPlayer(Preferences->GetLocalPlayer()); else TryBind();
}
void UDreamSettingsMotionPolicy::Changed(bool Reduced)
{
    bReduced=Reduced;
    ApplyTree(GetWidget(),Reduced); ApplyRing(Reduced);
    if (!Reduced) { Scrolls.Reset(); Throbbers.Reset(); Rings.Reset(); }
    SetCanExecuteTick(Reduced);
}
void UDreamSettingsMotionPolicy::ApplyTree(UDreamWidget* Node,bool Reduced)
{
    if (!Node) return;
    if (auto* Scroll=Cast<UDreamScrollBox>(Node))
    {
        if (Reduced)
        {
            const bool First=!Scrolls.Contains(Scroll);
            if (First) Scrolls.Add(Scroll,{Scroll->GetScrollWhenFocusChanges(),Scroll->GetAnimateWheelScrolling()});
            auto& Original=Scrolls.FindChecked(Scroll);
            if (Scroll->GetScrollWhenFocusChanges()!=EDreamUIScrollWhenFocusChanges::InstantScroll)
            { Original.Focus=Scroll->GetScrollWhenFocusChanges(); Scroll->SetScrollWhenFocusChanges(EDreamUIScrollWhenFocusChanges::InstantScroll); }
            if (Scroll->GetAnimateWheelScrolling()) { Original.Wheel=true; Scroll->SetAnimateWheelScrolling(false); }
            if (First)
            {
                Scroll->EndInertialScrolling();
                const auto* Scope=GetWidget()->GetComponent<UDreamSettingsNavigationScope>();
                if (Scope) if (auto* Input=UDreamUIInputServices::Get(this))
                    if (auto* Focus=Input->GetFocusedWidget(Scope->GetUserIndex())) Scroll->ScrollWidgetIntoView(Focus,false);
            }
        }
        else if (const auto* Original=Scrolls.Find(Scroll))
        { Scroll->SetScrollWhenFocusChanges(Original->Focus); Scroll->SetAnimateWheelScrolling(Original->Wheel); }
    }
    if (auto* Throbber=Cast<UDreamThrobber>(Node))
    {
        if (Reduced)
        {
            if (!Throbbers.Contains(Throbber)) Throbbers.Add(Throbber,Throbber->GetAnimate());
            if (Throbber->GetAnimate()) { Throbbers.FindChecked(Throbber)=true; Throbber->SetAnimate(false); }
        }
        else if (const auto* Original=Throbbers.Find(Throbber)) Throbber->SetAnimate(*Original);
    }
    const auto Children=Node->GetChildren();
    for (auto* Child:Children) ApplyTree(Child,Reduced);
}
void UDreamSettingsMotionPolicy::ApplyRing(bool Reduced)
{
    auto* Ring=GetWidget() ? UUINavigationInputSelectionHandler::FindFor(GetWidget()) : nullptr;
    if (!Ring) return;
    if (Reduced)
    {
        if (!Rings.Contains(Ring)) Rings.Add(Ring,Ring->GetAnimDuration());
        if (Ring->GetAnimDuration()>0) { Rings.FindChecked(Ring)=Ring->GetAnimDuration(); Ring->SetAnimDuration(0); }
    }
    else if (const auto* Original=Rings.Find(Ring)) Ring->SetAnimDuration(*Original);
}
void UDreamSettingsMotionPolicy::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!Preferences.IsValid()) { TryBind(); return; }
    // Dynamic rows and freshly created focus indicators must inherit the policy as well.
    if (bReduced) { ApplyTree(GetWidget(),true); ApplyRing(true); }
}
void UDreamSettingsMotionPolicy::Release()
{
    if (Preferences.IsValid()) Preferences->OnReducedMotionChanged.RemoveAll(this);
    ApplyTree(GetWidget(),false); ApplyRing(false);
    Scrolls.Reset(); Throbbers.Reset(); Rings.Reset(); Preferences.Reset(); bReduced=false;
    SetCanExecuteTick(false);
}
void UDreamSettingsMotionPolicy::OnDisable() { Release(); Super::OnDisable(); }
void UDreamSettingsMotionPolicy::OnUnregister() { Release(); Super::OnUnregister(); }
