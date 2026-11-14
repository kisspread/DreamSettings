#pragma once
#include "DreamSettingsModel.h"
class UGameInstance;

/** Register the process-wide native colour-vision correction owned by the settings service. */
DREAMSETTINGS_API FDreamSettingsResult RegisterDreamAccessibilitySettings(FDreamSettingsRegistry& Registry,UGameInstance* Instance);
