#include "DreamSettingsNumberInput.h"

UDreamSettingsNumberInput::UDreamSettingsNumberInput()
{
    bRevertTextOnEscape=true;
    bClearKeyboardFocusOnCommit=true;
    bSelectAllWhenActivateInput=true;
    bSubmitWhenDeactivate=true;
}
void UDreamSettingsNumberInput::SetBoundText(const FString& Value)
{
    BoundText=Value;
    if (Committing || !InputBehaviour || !InputBehaviour->IsInputActive()) SetText(BoundText);
}
void UDreamSettingsNumberInput::NativeOnConstruct()
{
    Super::NativeOnConstruct();
    GetOnTextCommittedEvent().RemoveAll(this);
    GetOnTextCommittedEvent().AddUObject(this,&ThisClass::Submitted);
    if (InputBehaviour) ActivationHandle=InputBehaviour->GetOnInputActivateEvent().AddUObject(this,&ThisClass::ActivationChanged);
    EnabledHandle=GetEnabledChangedEvent().AddUObject(this,&ThisClass::EnabledChanged);
    SetText(BoundText);
}
void UDreamSettingsNumberInput::ActivationChanged(bool Active)
{
    if (Active) TextAtStart=GetText();
    else SetText(BoundText);
}
void UDreamSettingsNumberInput::Submitted(const FString& Value)
{
    if (Discarding) return;
    // Escape reports the activation's original text. An unchanged edit must not write an old
    // value back over a newer external/session value that arrived while the field was active.
    if (!Value.Equals(TextAtStart,ESearchCase::CaseSensitive))
    {
        TGuardValue<bool> Guard(Committing,true);
        OnNumberSubmitted.Broadcast(Value);
    }
    SetText(BoundText);
}
void UDreamSettingsNumberInput::DiscardEdit()
{
    TGuardValue<bool> Guard(Discarding,true);
    if (InputBehaviour && InputBehaviour->IsInputActive()) InputBehaviour->DeactivateInput(false);
    SetText(BoundText);
}
void UDreamSettingsNumberInput::EnabledChanged(bool Enabled) { if (!Enabled) DiscardEdit(); }
void UDreamSettingsNumberInput::NativeOnDisable() { DiscardEdit(); Super::NativeOnDisable(); }
void UDreamSettingsNumberInput::NativeOnDestruct()
{
    DiscardEdit();
    if (InputBehaviour) InputBehaviour->GetOnInputActivateEvent().Remove(ActivationHandle);
    GetEnabledChangedEvent().Remove(EnabledHandle);
    ActivationHandle.Reset(); EnabledHandle.Reset();
    Super::NativeOnDestruct();
}
