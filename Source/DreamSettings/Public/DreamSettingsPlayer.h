#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UObject/Interface.h"
#include "DreamSettingsModel.h"
#include "DreamSettingsPlayer.generated.h"

/** Native host contract. Values must be read from and applied to the game's actual camera/input consumer. */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UDreamSettingsGameplayAdapter : public UInterface { GENERATED_BODY() };
class DREAMSETTINGS_API IDreamSettingsGameplayAdapter
{
    GENERATED_BODY()
public:
    virtual FDreamSettingsResult ReadGameplaySetting(FName Id, FDreamSettingValue& Out) const = 0;
    virtual FDreamSettingsResult WriteGameplaySetting(FName Id, const FDreamSettingValue& Value) = 0;
};

/** Maps player preferences into the same atomic machine file, preserving every other player's values. */
class DREAMSETTINGS_API FDreamPlayerSettingsStore final : public IDreamSettingsStore
{
public:
    FDreamPlayerSettingsStore(TSharedRef<IDreamSettingsStore> InStore, int32 UserId, TSet<FName> InPlayerIds);
    FDreamSettingsResult Load(FDreamSettingsValues& Out) override;
    FDreamSettingsResult Save(const FDreamSettingsValues& Values) override;
    FDreamSettingsResult PrepareSave(const FDreamSettingsValues& Values,FDreamSettingsPreparedWrite& Out) override;
    void NotifyCommitted() override { Store->NotifyCommitted(); }
    FDreamSettingsResult BeginRiskyChange(const FDreamSettingsValues& Values) override;
    FDreamSettingsResult LoadRecovery(FDreamSettingsValues& Out) override;
private:
    void Decode(const FDreamSettingsValues& Values, FDreamSettingsValues& Out) const;
    FDreamSettingsResult Merge(const FDreamSettingsValues& Values, FDreamSettingsValues& Out) const;
    TSharedRef<IDreamSettingsStore> Store;
    FString Prefix;
    TSet<FName> PlayerIds;
};

/** Reusable, frame-rate independent look math used by the native reference host. */
struct DREAMSETTINGS_API FDreamLookPreferences
{
    double MouseSensitivity=1, GamepadSensitivity=1, GamepadDeadZone=0.15;
    bool bInvertX=false, bInvertY=false;
    FVector2D Transform(FVector2D Input, bool bGamepad, double DeltaSeconds) const;
};

UCLASS()
class DREAMSETTINGS_API UDreamSettingsPlayerSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    FDreamSettingsResult AttachGameplayAdapter(UObject* Adapter, TSharedRef<IDreamSettingsStore> MachineStore);
    FDreamSettingsResult RegisterSettings(FDreamSettingsRegistry& Registry);
    FDreamSettingsResult LoadPresentationSettings(TSharedRef<IDreamSettingsStore> MachineStore);
    /** Stable family token for game prompts; hosts may render their own assets. */
    const FString& GetGamepadIconFamily() const { return GamepadIconFamily; }
    DECLARE_MULTICAST_DELEGATE_OneParam(FGamepadIconFamilyChanged,const FString&);
    FGamepadIconFamilyChanged OnGamepadIconFamilyChanged;
    bool GetReducedMotion() const { return bReducedMotion; }
    DECLARE_MULTICAST_DELEGATE_OneParam(FReducedMotionChanged,bool);
    FReducedMotionChanged OnReducedMotionChanged;
    double GetUIScale() const { return UIScale; }
    double GetSubtitleSize() const { return SubtitleSize; }
    DECLARE_MULTICAST_DELEGATE_OneParam(FPresentationNumberChanged,double);
    FPresentationNumberChanged OnUIScaleChanged;
    FPresentationNumberChanged OnSubtitleSizeChanged;
    TSharedRef<IDreamSettingsStore> ScopeStore(TSharedRef<IDreamSettingsStore> MachineStore) const;
    static TArray<FDreamSettingDefinition> Definitions();
    static TArray<FDreamSettingDefinition> GameplayDefinitions();
    static TArray<FDreamSettingDefinition> PresentationDefinitions();
    static TSet<FName> SettingIds();
private:
    FDreamSettingsResult Read(FName Id, FDreamSettingValue& Out) const;
    FDreamSettingsResult Write(FName Id, const FDreamSettingValue& Value);
    TWeakObjectPtr<UObject> GameplayAdapter;
    FDreamSettingsValues Cached;
    bool bLoaded=false;
    bool bPresentationLoaded=false;
    FString GamepadIconFamily=TEXT("xbox");
    bool bReducedMotion=false;
    double UIScale=1;
    double SubtitleSize=24;
};
