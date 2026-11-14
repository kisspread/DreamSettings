#include "DreamSettingsNavigationScope.h"
#include "DreamSettingsViewModel.h"
#include "DreamSettingsNavigationMemory.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/UISelectable.h"
#include "Controls/DreamScrollBox.h"
#include "Interaction/UIScrollView.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/UITextInput.h"

namespace
{
    UDreamSettingsViewModel* FindPresenter(UDreamWidget* Widget)
    {
        for (auto* Node=Widget;Node;Node=Node->GetParent())
            if (auto* Screen=Cast<UDreamUserWidget>(Node))
                if (auto* VM=Cast<UDreamSettingsViewModel>(Screen->GetViewModel(TEXT("Settings")))) return VM;
        return nullptr;
    }
    UDreamWidget* FindAnchor(UDreamWidget* Root,FName Setting,FName Category)
    {
        if (!Root || !Root->GetWidgetActiveInHierarchy()) return nullptr;
        if (auto* Anchor=Root->GetComponent<UDreamSettingsFocusAnchor>())
            if ((!Setting.IsNone() && Anchor->SettingId==Setting) || (!Category.IsNone() && Anchor->CategoryId==Category)) return Root;
        for (auto* Child:Root->GetChildren()) if (auto* Found=FindAnchor(Child,Setting,Category)) return Found;
        return nullptr;
    }
    UUISelectable* Navigable(UDreamWidget* Root)
    {
        if (!Root || !Root->GetWidgetActiveInHierarchy()) return nullptr;
        auto* Selectable=UUISelectable::FindDefaultSelectableIn(Root,Root);
        if (Selectable && Selectable->CanBeNavigatedTo()) return Selectable;
        for (auto* Child:Root->GetChildren()) if (auto* Found=Navigable(Child)) return Found;
        return nullptr;
    }
    void Reveal(UDreamWidget* Target,bool PreserveNativeScroll=false)
    {
        for (auto* Parent=Target ? Target->GetParent() : nullptr;Parent;Parent=Parent->GetParent())
            if (auto* Scroll=Cast<UDreamScrollBox>(Parent))
                if (!PreserveNativeScroll || !Scroll->GetIsScrolling())
                {
                    const auto* View=Scroll->GetScrollView() ? Scroll->GetScrollView()->GetContentViewport() : nullptr;
                    const FVector2D Spare=View ? View->GetSize()-Target->GetSize() : FVector2D::ZeroVector;
                    // At large text/UI sizes the visible strip can be only slightly taller than a
                    // control. Decorative padding must not push part of that control outside it.
                    const float Padding=FMath::Clamp(FMath::Min(Spare.X,Spare.Y)*.5,0.0,8.0);
                    Scroll->ScrollWidgetIntoView(Target,false,EDreamUIScrollDestination::IntoView,Padding);
                }
    }
}

UDreamSettingsNavigationScope::UDreamSettingsNavigationScope()
{
    bCloseOnBack=false; // Only the session can approve closing a page with previews or an active transaction.
    bActivateWhenEnabled=false; // DUI `if` changes visibility, not the behaviour's enabled lifecycle.
    InputMode=EDreamUIScopeInputMode::Menu;
    bStartWithTickEnabled=true;
    bTickEvenWhenPaused=true;
}
void UDreamSettingsNavigationScope::OnEnable()
{
    OnScopeActivated.AddUniqueDynamic(this,&ThisClass::HandleActivated);
    Super::OnEnable();
    bScopeEnabled=true;
    SetRequestedActive(bRequestedActive);
}
void UDreamSettingsNavigationScope::OnDisable()
{ CapturePageFocus(); bScopeEnabled=false; SetCanExecuteTick(false); Super::OnDisable(); }
void UDreamSettingsNavigationScope::OnUnregister()
{ CapturePageFocus(); DetachPresenter(); bScopeEnabled=false; OnScopeActivated.RemoveDynamic(this,&ThisClass::HandleActivated); Super::OnUnregister(); }
void UDreamSettingsNavigationScope::HandleActivated(UDreamUINavigationScope* Scope)
{ bRestoreFocus=true; bPreferLiveFocus=bHasFocused; FocusAfterFrame=GFrameCounter+2; SetCanExecuteTick(true); }
void UDreamSettingsNavigationScope::SetRequestedActive(bool Value)
{
    bRequestedActive=Value;
    if (bScopeEnabled && Value) ActivateScope();
    else { SetCanExecuteTick(false); DeactivateScope(); }
}
void UDreamSettingsNavigationScope::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    const auto* Stack=UDreamUINavigationStack::Get(this);
    if (!Stack || Stack->GetActiveScope(GetUserIndex())!=this || !GetWidget()) return;
    if (bRememberPageFocus)
    {
        auto* VM=FindPresenter(GetWidget());
        if (Presenter.Get()!=VM)
        {
            DetachPresenter(); Presenter=VM;
            if (VM) VM->OnNavigationChanging.AddUObject(this,&ThisClass::NavigationChanging);
            bRestoreFocus=true; bPreferLiveFocus=false; FocusAfterFrame=GFrameCounter+2;
        }
        if (!VM || !VM->Ready) return;
        if (!VM->PageEnabled)
        {
            if (!bRestoreFocus) { bRestoreFocus=true; bPreferLiveFocus=true; }
            FocusAfterFrame=GFrameCounter+1; return;
        }
        auto* Input=UDreamUIInputServices::Get(this);
        if (!Input) return;
        const auto* Pointer=Input->FindPointer(GetUserIndex(),0);
        if (Pointer && Pointer->InputType==EDreamUIPointerInputType::Pointer)
        {
            if (HaveHelpPointer && !Pointer->PointerPosition.Equals(HelpPointerPosition,.1)) HelpUsesPointer=true;
            HelpPointerPosition=Pointer->PointerPosition; HaveHelpPointer=true;
        }
        const auto Revision=Input->GetFocusRevision(GetUserIndex());
        if (Revision!=HelpFocusRevision && Input->IsFocusVisible(GetUserIndex())) HelpUsesPointer=false;
        HelpFocusRevision=Revision;
        auto* HelpTarget=HelpUsesPointer && Pointer ? Pointer->HitWidgetIgnoringEnabled.Get() : Input->GetFocusedWidget(GetUserIndex());
        if (IsValid(HelpTarget) && HelpTarget->IsChildOf(GetWidget()))
            for (auto* Node=HelpTarget;Node && Node!=GetWidget();Node=Node->GetParent())
                if (const auto* Anchor=Node->GetComponent<UDreamSettingsFocusAnchor>(); Anchor && !Anchor->SettingId.IsNone())
                { VM->SetDetailsSetting(Anchor->SettingId); break; }
        if (bRestoreFocus)
        {
            if (GFrameCounter<FocusAfterFrame) return;
            UUISelectable* Target=bPreferLiveFocus && GetRememberedFocus() ? ResolveFocusTarget() : nullptr;
            if (Target && !Target->CanBeNavigatedTo()) Target=nullptr;
            if (!Target) if (auto* Memory=VM->GetNavigationMemory())
                if (const auto* Bookmark=Memory->Rows.Find(VM->GetCurrentCategory()))
                    if (auto* Row=FindAnchor(GetWidget(),Bookmark->SettingId,NAME_None))
                    {
                        auto* Control=Row;
                        for (const auto& Part:Bookmark->ControlPath)
                        {
                            UDreamWidget* Next=nullptr;
                            for (auto* Child:Control->GetChildren()) if (Child->GetDisplayName()==Part) { Next=Child; break; }
                            Control=Next; if (!Control) break;
                        }
                        Target=Navigable(Control);
                        if (!Target) Target=Navigable(Row);
                    }
            if (!Target) Target=Navigable(FindAnchor(GetWidget(),NAME_None,VM->GetCurrentCategory()));
            if (!Target) Target=Navigable(GetWidget()->FindChildByDisplayName(InitialFocusName,true));
            if (!Target) return;
            SetDesiredFocusTarget(Target);
            if (!Input->FocusForNavigation(Target->GetWidget(),GetUserIndex())) return;
            Reveal(Target->GetWidget()); bRestoreFocus=false; bHasFocused=true;
        }
        if (Input->GetFocusedWidget(GetUserIndex())!=LastObservedFocus.Get())
        {
            FocusScrollSizes.Reset();
            CapturePageFocus();
            // Confirmation can change the focus cause before this tick observes it. Ensure any
            // newly focused child is visible, while leaving native scroll animations intact.
            auto* Focus=Input->GetFocusedWidget(GetUserIndex());
            if (Focus && Focus->IsChildOf(GetWidget())) Reveal(Focus,true);
        }
        // A preview appearing or a scale change can shrink a viewport without changing focus.
        // Reveal only on a size change; ordinary manual wheel scrolling must remain free.
        if (auto* Focus=Input->GetFocusedWidget(GetUserIndex()); Focus && Focus->IsChildOf(GetWidget()))
            for (auto* Parent=Focus->GetParent();Parent;Parent=Parent->GetParent())
                if (auto* Scroll=Cast<UDreamScrollBox>(Parent))
                {
                    const FVector2D Size=Scroll->GetSize();
                    if (const auto* Before=FocusScrollSizes.Find(Scroll); Before && !Before->Equals(Size,.1))
                    {
                        if (Scroll->GetIsScrolling()) continue;
                        Reveal(Focus,true);
                    }
                    FocusScrollSizes.Add(Scroll,Size);
                }
        return;
    }
    // Dynamic DUI loops finish realizing after their containing scope is enabled.
    // Resolve once they exist; leave subsequent focus restoration to DreamGUI's scope stack.
    auto* Target=GetWidget()->FindChildByDisplayName(InitialFocusName,true);
    // On a modal pop the page's enabled bindings settle after the scope stack callback.
    // Resolve remembered focus now, instead of dropping it while that page is still disabled.
    auto* Selectable=GetRememberedFocus() ? ResolveFocusTarget() : Target ? UUISelectable::FindDefaultSelectableIn(this,Target) : nullptr;
    if (!Selectable) return;
    SetDesiredFocusTarget(Selectable);
    if (auto* Input=UDreamUIInputServices::Get(this))
        if (Input->FocusForNavigation(Selectable->GetWidget(),GetUserIndex()))
            SetCanExecuteTick(false);
}
void UDreamSettingsNavigationScope::DetachPresenter()
{
    if (Presenter.IsValid()) Presenter->OnNavigationChanging.RemoveAll(this);
    Presenter.Reset(); LastObservedFocus.Reset(); FocusScrollSizes.Reset();
}
void UDreamSettingsNavigationScope::CapturePageFocus()
{
    if (!bRememberPageFocus || !Presenter.IsValid()) return;
    auto* Memory=Presenter->GetNavigationMemory();
    auto* Input=UDreamUIInputServices::Get(this);
    auto* Focus=Input ? Input->GetFocusedWidget(GetUserIndex()) : nullptr;
    LastObservedFocus=Focus;
    if (!Memory || !Focus) return;
    bool WithinPage=false;
    for (auto* Node=Focus;Node;Node=Node->GetParent()) if (Node==GetWidget()) { WithinPage=true; break; }
    if (!WithinPage) return;
    FDreamSettingsFocusBookmark Bookmark;
    for (auto* Node=Focus;Node && Node!=GetWidget();Node=Node->GetParent())
    {
        if (auto* Anchor=Node->GetComponent<UDreamSettingsFocusAnchor>())
            if (!Anchor->SettingId.IsNone())
            {
                Bookmark.SettingId=Anchor->SettingId;
                Memory->Rows.Add(Presenter->GetCurrentCategory(),MoveTemp(Bookmark)); return;
            }
        Bookmark.ControlPath.Insert(Node->GetDisplayName(),0);
    }
}
void UDreamSettingsNavigationScope::NavigationChanging()
{
    CapturePageFocus(); bRestoreFocus=true; bPreferLiveFocus=false;
    FocusAfterFrame=GFrameCounter+2; LastObservedFocus.Reset();
}
bool UDreamSettingsNavigationScope::HandleBackAction_Implementation()
{
    for (auto* Node=GetWidget();Node;Node=Node->GetParent())
        if (auto* Screen=Cast<UDreamUserWidget>(Node))
            if (auto* VM=Cast<UDreamSettingsViewModel>(Screen->GetViewModel(TEXT("Settings"))))
            {
                if (VM->ConfirmDisplay) VM->RevertDisplay();
                else VM->RequestClose();
                return true;
            }
    return true; // A temporarily detached presenter must not let Back escape to the game underneath.
}
void UDreamSettingsNavigationScope::OnKeyDown_Implementation(UDreamKeyEventData* Event)
{
    if (!Event || Event->bHandled || Event->UserIndex!=GetUserIndex() || !bRememberPageFocus ||
        Event->bAltDown || Event->bCtrlDown || Event->bCmdDown || Event->bShiftDown) return;
    const auto* Stack=UDreamUINavigationStack::Get(this);
    auto* VM=FindPresenter(GetWidget());
    if (!Stack || Stack->GetActiveScope(GetUserIndex())!=this || !VM) return;
    for (auto* Node=Event->FocusedWidget.Get();Node;Node=Node->GetParent())
        if (auto* Input=Node->GetComponent<UUITextInput>(); Input && Input->IsInputActive()) return;
    if (VM->IsGameBenchmarkShortcut(Event->Key))
    { Event->SetHandled(); if (!Event->bIsRepeat) VM->RequestGameBenchmark(); return; }
    for (auto* Node=Event->FocusedWidget.Get();Node;Node=Node->GetParent())
        if (auto* Anchor=Node->GetComponent<UDreamSettingsFocusAnchor>(); Anchor && !Anchor->SettingId.IsNone())
        { VM->SetDetailsSetting(Anchor->SettingId); break; }
    if (VM->HandleMenuShortcut(Event->Key,Event->bIsRepeat)) Event->SetHandled();
}
