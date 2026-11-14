#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamSettingsConfiguration.generated.h"

class USoundClass;

/** Host-owned routes. Each channel must be a distinct descendant of Master. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Dream Settings"))
class DREAMSETTINGS_API UDreamSettingsConfiguration : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    /** Cultures shipped by this host; the plugin's native English source is the fallback. */
    UPROPERTY(Config, EditAnywhere, Category="Language") TArray<FString> SupportedCultures={TEXT("en"),TEXT("zh-Hans")};
    UPROPERTY(Config, EditAnywhere, Category="Audio") TSoftObjectPtr<USoundClass> MasterSoundClass;
    UPROPERTY(Config, EditAnywhere, Category="Audio") TSoftObjectPtr<USoundClass> MusicSoundClass;
    UPROPERTY(Config, EditAnywhere, Category="Audio") TSoftObjectPtr<USoundClass> EffectsSoundClass;
    UPROPERTY(Config, EditAnywhere, Category="Audio") TSoftObjectPtr<USoundClass> AmbientSoundClass;
    UPROPERTY(Config, EditAnywhere, Category="Audio") TSoftObjectPtr<USoundClass> UISoundClass;
    UPROPERTY(Config, EditAnywhere, Category="Audio") TSoftObjectPtr<USoundClass> VoiceSoundClass;
    USoundClass* ResolveSoundClass(FName SettingId) const;
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
