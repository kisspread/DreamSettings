#pragma once
#include "DreamSettingsModel.h"
#include "UserSettings/EnhancedInputUserSettings.h"

class ULocalPlayer;
class UDreamSettingsInputUserSettings;

struct DREAMSETTINGS_API FDreamSettingsKeyBinding
{
    FName Id;
    FMapPlayerKeyArgs Mapping;
    FText Label;
    FString CategoryId;
    FText CategoryLabel;
    TArray<FDreamSettingsInputContext> Contexts;
    FKey DefaultKey;
    bool bGamepad=false;
    /** A secondary slot with no authored default. It may not exist in the native profile yet. */
    bool bOptionalSlot=false;
};

/** One local player's active profile. Providers never save; the outer store coordinates persistence. */
class DREAMSETTINGS_API FDreamSettingsKeyBindings : public TSharedFromThis<FDreamSettingsKeyBindings>
{
public:
    FDreamSettingsResult Initialize(ULocalPlayer* Player,FDreamSettingsRegistry& Registry);
    const TArray<FDreamSettingsKeyBinding>& GetBindings() const { return Bindings; }
    FDreamSettingsResult Read(FName Id,FDreamSettingValue& Out) const;
    FDreamSettingsResult Write(FName Id,const FDreamSettingValue& Value);
    FDreamSettingsResult Validate(FName Id,const FString& KeyName,const FDreamSettingsValues& Candidate) const;
    TArray<FName> FindConflicts(FName Id,const FString& KeyName,const FDreamSettingsValues& Candidate) const;
    FDreamSettingsResult PrepareSave(FDreamSettingsPreparedWrite& Out) const;
    TSharedRef<IDreamSettingsStore> WrapStore(TSharedRef<IDreamSettingsStore> Inner);
private:
    const FDreamSettingsKeyBinding* Find(FName Id) const;
    TWeakObjectPtr<UDreamSettingsInputUserSettings> Settings;
    TArray<FDreamSettingsKeyBinding> Bindings;
};
