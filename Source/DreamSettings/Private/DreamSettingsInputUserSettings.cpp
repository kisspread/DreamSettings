#include "DreamSettingsInputUserSettings.h"
#include "EnhancedActionKeyMapping.h"
#include "GameFramework/InputSettings.h"
#include "Misc/Paths.h"

FString UDreamSettingsInputUserSettings::GetNativeSaveFilename() const
{
    if (!GetLocalPlayer()) return {};
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("SaveGames")/(GetSaveFilename(GetLocalPlayer())+TEXT(".sav")));
}
FHardwareDeviceIdentifier UDreamSettingsInputUserSettings::DetermineHardwareDeviceForActionMapping(
    const FEnhancedActionKeyMapping& ActionMapping,const UInputMappingContext* IMC) const
{
    return ActionMapping.Key.IsGamepadKey() ? FHardwareDeviceIdentifier::DefaultGamepad : FHardwareDeviceIdentifier::DefaultKeyboardAndMouse;
}
