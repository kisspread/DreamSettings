#pragma once
#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamSettingsScalePolicy.generated.h"
class ULocalPlayer;
class UDreamCanvas;
class UDreamSettingsPlayerSubsystem;

/** Player preference multiplier for a screen canvas authored with ScaleWithScreenSize.
 * Attach to the persistent screen root, so closing a settings page keeps the chosen HUD scale. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsScalePolicy : public UDreamUIBehaviour
{
    GENERATED_BODY()
public:
    bool BindPlayer(ULocalPlayer* Player);
protected:
    virtual void OnEnable() override;
    virtual void OnDisable() override;
    virtual void OnUnregister() override;
private:
    void Changed(double Value);
    void Release();
    TWeakObjectPtr<UDreamSettingsPlayerSubsystem> Preferences;
    TWeakObjectPtr<ULocalPlayer> BoundPlayer;
    TWeakObjectPtr<UDreamCanvas> Canvas;
    FVector2D AuthoredResolution=FVector2D::ZeroVector;
};
