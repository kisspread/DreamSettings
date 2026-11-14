#pragma once
#include "Interaction/DreamUINavigationScope.h"
#include "Core/DreamUIInputServices.h"
#include "Event/Interface/DreamKeyInterface.h"
#include "DreamSettingsNavigationScope.generated.h"

class UDreamSettingsViewModel;

/** Reusable settings navigation policy; the native DreamGUI stack owns confinement and return focus. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsNavigationScope : public UDreamUINavigationScope, public IDreamKeyInterface
{
    GENERATED_BODY()
public:
    UDreamSettingsNavigationScope();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings") FString InitialFocusName=TEXT("CategoryButton");
    /** Enable only on the page scope; dialogs retain the native stack's live return focus. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings") bool bRememberPageFocus=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintGetter="GetRequestedActive",BlueprintSetter="SetRequestedActive",Category="Settings") bool bRequestedActive=false;
    UFUNCTION(BlueprintGetter) bool GetRequestedActive() const { return bRequestedActive; }
    UFUNCTION(BlueprintSetter) void SetRequestedActive(bool Value);
    virtual bool HandleBackAction_Implementation() override;
    virtual void OnKeyDown_Implementation(UDreamKeyEventData* EventData) override;
protected:
    virtual void OnEnable() override;
    virtual void OnDisable() override;
    virtual void OnUnregister() override;
    virtual void Tick(float DeltaTime) override;
private:
    UFUNCTION() void HandleActivated(UDreamUINavigationScope* Scope);
    bool bScopeEnabled=false;
    void CapturePageFocus();
    void NavigationChanging();
    void DetachPresenter();
    TWeakObjectPtr<UDreamSettingsViewModel> Presenter;
    TWeakObjectPtr<UDreamWidget> LastObservedFocus;
    TMap<TWeakObjectPtr<UDreamWidget>,FVector2D> FocusScrollSizes;
    bool bRestoreFocus=true,bPreferLiveFocus=false,bHasFocused=false;
    uint64 FocusAfterFrame=0;
    FDreamUIFocusRevision HelpFocusRevision;
    FVector HelpPointerPosition=FVector::ZeroVector;
    bool HaveHelpPointer=false,HelpUsesPointer=true;
};
