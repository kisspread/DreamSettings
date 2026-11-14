#pragma once
#include "DreamSettingsModel.h"
class UGameInstance;
TArray<FDreamSettingDefinition> DreamHDRDefinitions();
FDreamSettingsResult RegisterDreamHDRSettings(FDreamSettingsRegistry& Registry,UGameInstance* Instance);
