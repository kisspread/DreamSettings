#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamButton.h"
#include "DreamSettingsStepButton.generated.h"

/** A setting arrow: one step on a click, accelerated steps while held, no extra step on release. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsStepButton : public UDreamButton
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintSetter=SetRepeatEnabled,Category="Settings") bool RepeatEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings",meta=(ClampMin="0.1")) float InitialDelay=.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings",meta=(ClampMin="0.02")) float RepeatInterval=.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings",meta=(ClampMin="0.1")) float AccelerationDelay=1.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings",meta=(ClampMin="0.02")) float AcceleratedInterval=.05f;
    UPROPERTY(BlueprintAssignable,Category="Settings") FDreamButtonClickedEvent OnStep;
    TMulticastDelegate<void()>& GetOnStepEvent() { return OnStepNative; }
    UFUNCTION(BlueprintCallable,Category="Settings") void SetRepeatEnabled(bool Value);
    virtual void NativeOnConstruct() override;
    virtual void NativeOnDestruct() override;
    virtual void NativeOnDisable() override;
    virtual void NativeOnTick(float DeltaTime) override;
private:
    void BeginPress();
    void EndPress();
    void Click();
    void CancelPress();
    void ActivationChanged(bool Active);
    void EnabledChanged(bool Enabled);
    void EmitStep();
    bool Held=false,SuppressClick=false;
    double PressTime=0,NextRepeat=0;
    FDelegateHandle ActivationHandle,EnabledHandle;
    TMulticastDelegate<void()> OnStepNative;
};
