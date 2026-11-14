#include "DreamSettingsKeySelector.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamText.h"

namespace
{
    class FSettingsInputCapture final : public IInputProcessor
    {
    public:
        explicit FSettingsInputCapture(UDreamSettingsKeySelector* InOwner):Owner(InOwner) {}
        void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
        bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& Event) override
        {
            if (ConsumedKeys.Contains(Event.GetKey())) return true;
            if (!Armed()) return false;
            ConsumedKeys.Add(Event.GetKey());
            Owner->NotifyKeyPressed(Event.GetKey()); return true;
        }
        bool HandleKeyUpEvent(FSlateApplication&,const FKeyEvent& Event) override
        { return ConsumedKeys.Remove(Event.GetKey())>0; }
        bool HandleMouseButtonDownEvent(FSlateApplication&,const FPointerEvent& Event) override
        {
            if (!Armed()) return false;
            const FKey Key=Event.GetEffectingButton();
            // Keep consuming its release after NotifyKeyPressed disarms, so this click cannot
            // also activate a newly opened conflict dialog or another control under the cursor.
            ConsumedButtons.Add(Key);
            Owner->NotifyKeyPressed(Key); return true;
        }
        bool HandleMouseButtonUpEvent(FSlateApplication&,const FPointerEvent& Event) override
        { return ConsumedButtons.Remove(Event.GetEffectingButton())>0; }
        bool HandleMouseButtonDoubleClickEvent(FSlateApplication& App,const FPointerEvent& Event) override
        { return HandleMouseButtonDownEvent(App,Event); }
        bool HandleMouseWheelOrGestureEvent(FSlateApplication&,const FPointerEvent& Event,const FPointerEvent*) override
        {
            if (!Armed() || FMath::IsNearlyZero(Event.GetWheelDelta())) return false;
            Owner->NotifyKeyPressed(Event.GetWheelDelta()>0 ? EKeys::MouseScrollUp : EKeys::MouseScrollDown); return true;
        }
        const TCHAR* GetDebugName() const override { return TEXT("DreamSettingsInputCapture"); }
    private:
        bool Armed() const { return Owner.IsValid() && Owner->GetIsListening() && Owner->GetWidgetActiveInHierarchy(); }
        TWeakObjectPtr<UDreamSettingsKeySelector> Owner;
        TSet<FKey> ConsumedButtons;
        TSet<FKey> ConsumedKeys;
    };
}
UDreamSettingsKeySelector::UDreamSettingsKeySelector()
{
    bAllowModifierKeys=false;
    // Legacy InputComponent capture runs after Enhanced Input has already evaluated menu keys.
    // Intercept Slate input first so Escape cancels capture without also opening a close prompt.
    bCaptureKeysWhileListening=false;
    EscapeKeys={EKeys::Escape,EKeys::Gamepad_Special_Right};
}
void UDreamSettingsKeySelector::NativeOnConstruct()
{
    Super::NativeOnConstruct();
    GetOnKeySelectedEvent().RemoveAll(this); GetOnKeySelectedEvent().AddUObject(this,&ThisClass::KeyChanged);
    GetOnIsListeningChangedEvent().RemoveAll(this); GetOnIsListeningChangedEvent().AddUObject(this,&ThisClass::ListeningChanged);
    RefreshButtonLabel();
    if (!InputCapture && FSlateApplication::IsInitialized())
    { InputCapture=MakeShared<FSettingsInputCapture>(this); FSlateApplication::Get().RegisterInputPreProcessor(InputCapture,0); }
}
void UDreamSettingsKeySelector::ReleaseInputCapture()
{
    if (InputCapture && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(InputCapture);
    InputCapture.Reset();
}
void UDreamSettingsKeySelector::NativeOnDestruct()
{ ReleaseInputCapture(); GetOnKeySelectedEvent().RemoveAll(this); GetOnIsListeningChangedEvent().RemoveAll(this); Super::NativeOnDestruct(); }
void UDreamSettingsKeySelector::BeginDestroy()
{ ReleaseInputCapture(); Super::BeginDestroy(); }
void UDreamSettingsKeySelector::ApplyStyle() { Super::ApplyStyle(); RefreshButtonLabel(); }
void UDreamSettingsKeySelector::SetDisplayKey(FKey Value) { DisplayKey=Value; SetSelectedKey(Value); RefreshButtonLabel(); }
void UDreamSettingsKeySelector::SetIconFamily(const FString& Value) { IconFamily=Value; RefreshButtonLabel(); }
void UDreamSettingsKeySelector::KeyChanged(FKey Value) { DisplayKey=GetSelectedKey(); RefreshButtonLabel(); }
void UDreamSettingsKeySelector::ListeningChanged(bool Value) { RefreshButtonLabel(); }
void UDreamSettingsKeySelector::RefreshButtonLabel()
{
    // Let the base control retain its localized listening and unbound prompts.
    if (GetIsListening() || !GetSelectedKey().IsValid()) return;
    if (auto* Label=LabelNode ? Cast<UDreamText>(LabelNode->GetVisual()) : nullptr) Label->SetText(FormatButtonLabel(GetSelectedKey()));
}
FText UDreamSettingsKeySelector::FormatButtonLabel(FKey Key) const
{ return ButtonLabelForFamily(Key,IconFamily); }
FText UDreamSettingsKeySelector::ButtonLabelForFamily(FKey Key,const FString& Family)
{
    if (!Key.IsGamepadKey()) return Key.GetDisplayName();
    const bool PS=Family==TEXT("playstation"),Nintendo=Family==TEXT("nintendo");
    if (Key==EKeys::Gamepad_FaceButton_Bottom) return PS ? NSLOCTEXT("DreamSettingsUI","PadCross","×  Cross") : FText::FromString(Nintendo ? TEXT("B") : TEXT("A"));
    if (Key==EKeys::Gamepad_FaceButton_Right) return PS ? NSLOCTEXT("DreamSettingsUI","PadCircle","○  Circle") : FText::FromString(Nintendo ? TEXT("A") : TEXT("B"));
    if (Key==EKeys::Gamepad_FaceButton_Left) return PS ? NSLOCTEXT("DreamSettingsUI","PadSquare","□  Square") : FText::FromString(Nintendo ? TEXT("Y") : TEXT("X"));
    if (Key==EKeys::Gamepad_FaceButton_Top) return PS ? NSLOCTEXT("DreamSettingsUI","PadTriangle","△  Triangle") : FText::FromString(Nintendo ? TEXT("X") : TEXT("Y"));
    if (Key==EKeys::Gamepad_LeftShoulder) return FText::FromString(PS ? TEXT("L1") : Nintendo ? TEXT("L") : TEXT("LB"));
    if (Key==EKeys::Gamepad_RightShoulder) return FText::FromString(PS ? TEXT("R1") : Nintendo ? TEXT("R") : TEXT("RB"));
    if (Key==EKeys::Gamepad_LeftTrigger || Key==EKeys::Gamepad_LeftTriggerAxis) return FText::FromString(PS ? TEXT("L2") : Nintendo ? TEXT("ZL") : TEXT("LT"));
    if (Key==EKeys::Gamepad_RightTrigger || Key==EKeys::Gamepad_RightTriggerAxis) return FText::FromString(PS ? TEXT("R2") : Nintendo ? TEXT("ZR") : TEXT("RT"));
    if (Key==EKeys::Gamepad_LeftThumbstick) return PS || Nintendo ? FText::FromString(TEXT("L3")) : NSLOCTEXT("DreamSettingsUI","LeftStickClick","LS  Click");
    if (Key==EKeys::Gamepad_RightThumbstick) return PS || Nintendo ? FText::FromString(TEXT("R3")) : NSLOCTEXT("DreamSettingsUI","RightStickClick","RS  Click");
    if (Key==EKeys::Gamepad_Special_Left) return Nintendo ? FText::FromString(TEXT("−")) : PS ? NSLOCTEXT("DreamSettingsUI","PadCreate","Create / Share") : NSLOCTEXT("DreamSettingsUI","PadView","View");
    if (Key==EKeys::Gamepad_Special_Right) return Nintendo ? FText::FromString(TEXT("+")) : PS ? NSLOCTEXT("DreamSettingsUI","PadOptions","Options") : NSLOCTEXT("DreamSettingsUI","PadMenu","Menu");
    if (Key==EKeys::Gamepad_DPad_Up) return NSLOCTEXT("DreamSettingsUI","PadUp","↑  D-pad up");
    if (Key==EKeys::Gamepad_DPad_Down) return NSLOCTEXT("DreamSettingsUI","PadDown","↓  D-pad down");
    if (Key==EKeys::Gamepad_DPad_Left) return NSLOCTEXT("DreamSettingsUI","PadLeft","←  D-pad left");
    if (Key==EKeys::Gamepad_DPad_Right) return NSLOCTEXT("DreamSettingsUI","PadRight","→  D-pad right");
    return Key.GetDisplayName();
}
