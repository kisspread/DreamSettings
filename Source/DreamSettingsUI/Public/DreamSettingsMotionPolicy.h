#pragma once
#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamScrollTypes.h"
#include "DreamSettingsMotionPolicy.generated.h"
class ULocalPlayer;
class UDreamSettingsPlayerSubsystem;
class UDreamScrollBox;
class UDreamThrobber;
class UUINavigationInputSelectionHandler;

/** Applies the player's menu motion preference to this DreamGUI tree without changing shared styles. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsMotionPolicy : public UDreamUIBehaviour
{
    GENERATED_BODY()
public:
    UDreamSettingsMotionPolicy();
    void BindPlayer(ULocalPlayer* Player);
protected:
    virtual void OnEnable() override;
    virtual void OnDisable() override;
    virtual void OnUnregister() override;
    virtual void Tick(float DeltaTime) override;
private:
    void TryBind();
    void Changed(bool Reduced);
    void ApplyTree(UDreamWidget* Node,bool Reduced);
    void ApplyRing(bool Reduced);
    void Release();
    struct FScrollState { EDreamUIScrollWhenFocusChanges Focus; bool Wheel; };
    TMap<TWeakObjectPtr<UDreamScrollBox>,FScrollState> Scrolls;
    TMap<TWeakObjectPtr<UDreamThrobber>,bool> Throbbers;
    TMap<TWeakObjectPtr<UUINavigationInputSelectionHandler>,float> Rings;
    TWeakObjectPtr<UDreamSettingsPlayerSubsystem> Preferences;
    bool bReduced=false;
};
