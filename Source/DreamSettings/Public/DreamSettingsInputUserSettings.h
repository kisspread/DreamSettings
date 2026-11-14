#pragma once

#include "UserSettings/EnhancedInputUserSettings.h"
#include "DreamSettingsInputUserSettings.generated.h"

/** Standard Enhanced Input serialization with explicit keyboard/gamepad identity. */
UCLASS()
class DREAMSETTINGS_API UDreamSettingsInputUserSettings : public UEnhancedInputUserSettings
{
    GENERATED_BODY()
public:
    FString GetNativeSaveFilename() const;
protected:
    virtual FHardwareDeviceIdentifier DetermineHardwareDeviceForActionMapping(
        const FEnhancedActionKeyMapping& ActionMapping,const UInputMappingContext* IMC=nullptr) const override;
};
