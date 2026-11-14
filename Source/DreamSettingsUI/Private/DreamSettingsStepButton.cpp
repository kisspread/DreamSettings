#include "DreamSettingsStepButton.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"

void UDreamSettingsStepButton::NativeOnConstruct()
{
    Super::NativeOnConstruct();
    GetOnPressedEvent().RemoveAll(this); GetOnPressedEvent().AddUObject(this,&ThisClass::BeginPress);
    GetOnReleasedEvent().RemoveAll(this); GetOnReleasedEvent().AddUObject(this,&ThisClass::EndPress);
    GetOnClickedEvent().RemoveAll(this); GetOnClickedEvent().AddUObject(this,&ThisClass::Click);
    GetOnUnhoveredEvent().RemoveAll(this); GetOnUnhoveredEvent().AddUObject(this,&ThisClass::CancelPress);
    EnabledHandle=GetEnabledChangedEvent().AddUObject(this,&ThisClass::EnabledChanged);
    if (FSlateApplication::IsInitialized())
        ActivationHandle=FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this,&ThisClass::ActivationChanged);
}
void UDreamSettingsStepButton::NativeOnDestruct()
{
    CancelPress();
    GetEnabledChangedEvent().Remove(EnabledHandle); EnabledHandle.Reset();
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
    ActivationHandle.Reset();
    Super::NativeOnDestruct();
}
void UDreamSettingsStepButton::NativeOnDisable() { CancelPress(); Super::NativeOnDisable(); }
void UDreamSettingsStepButton::SetRepeatEnabled(bool Value)
{
    RepeatEnabled=Value;
    if (!Value) CancelPress();
}
void UDreamSettingsStepButton::BeginPress()
{
    SuppressClick=false;
    Held=RepeatEnabled && GetIsEnabledInHierarchy() && GetWidgetActiveInHierarchy();
    PressTime=FPlatformTime::Seconds();
    NextRepeat=PressTime+FMath::Max(.1f,InitialDelay);
    SetWantsTick(Held);
}
void UDreamSettingsStepButton::EndPress() { Held=false; SetWantsTick(false); }
void UDreamSettingsStepButton::CancelPress()
{
    if (Held) SuppressClick=true;
    EndPress();
}
void UDreamSettingsStepButton::ActivationChanged(bool Active) { if (!Active) CancelPress(); }
void UDreamSettingsStepButton::EnabledChanged(bool Enabled) { if (!Enabled) CancelPress(); }
void UDreamSettingsStepButton::EmitStep()
{
    if (!GetIsEnabledInHierarchy() || !GetWidgetActiveInHierarchy()) return;
    OnStepNative.Broadcast();
    OnStep.Broadcast();
}
void UDreamSettingsStepButton::Click() { if (!SuppressClick) EmitStep(); }
void UDreamSettingsStepButton::NativeOnTick(float DeltaTime)
{
    Super::NativeOnTick(DeltaTime);
    if (!Held) return;
    if (!RepeatEnabled || !GetIsEnabledInHierarchy() || !GetWidgetActiveInHierarchy() || !IsPressed())
    { CancelPress(); return; }
    // Real time keeps UI input independent of gameplay pause/time dilation. Never catch up a stalled
    // frame with a burst of edits; a held arrow emits at most one step per rendered UI tick.
    const double Now=FPlatformTime::Seconds();
    if (Now<NextRepeat) return;
    SuppressClick=true;
    const float Interval=Now-PressTime>=FMath::Max(.1f,AccelerationDelay) ? AcceleratedInterval : RepeatInterval;
    NextRepeat=Now+FMath::Max(.02f,Interval);
    EmitStep();
}
