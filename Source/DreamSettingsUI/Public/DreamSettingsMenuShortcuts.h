#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "DreamSettingsMenuShortcuts.generated.h"

/** Presentation commands; these never become player gameplay bindings. Empty arrays disable an action. */
USTRUCT(BlueprintType)
struct DREAMSETTINGSUI_API FDreamSettingsMenuShortcuts
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Settings") TArray<FKey> PreviousCategory={EKeys::Q,EKeys::Gamepad_LeftShoulder};
    UPROPERTY(EditAnywhere,Category="Settings") TArray<FKey> NextCategory={EKeys::E,EKeys::Gamepad_RightShoulder};
    UPROPERTY(EditAnywhere,Category="Settings") TArray<FKey> Apply={EKeys::F5,EKeys::Gamepad_Special_Left};
    UPROPERTY(EditAnywhere,Category="Settings") TArray<FKey> Reset={EKeys::F6,EKeys::Gamepad_FaceButton_Left};
};
