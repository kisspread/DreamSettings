#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamSettingsNavigationMemory.generated.h"

/** Stable identities on a custom DreamGUI row/category, independent of localized text or UObject instances. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsFocusAnchor : public UDreamUIBehaviour
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintGetter="GetSettingId",BlueprintSetter="SetSettingId",Category="Settings") FName SettingId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintGetter="GetCategoryId",BlueprintSetter="SetCategoryId",Category="Settings") FName CategoryId;
    UFUNCTION(BlueprintGetter) FName GetSettingId() const { return SettingId; }
    UFUNCTION(BlueprintSetter) void SetSettingId(FName Value) { SettingId=Value; }
    UFUNCTION(BlueprintGetter) FName GetCategoryId() const { return CategoryId; }
    UFUNCTION(BlueprintSetter) void SetCategoryId(FName Value) { CategoryId=Value; }
};

struct FDreamSettingsFocusBookmark
{
    FName SettingId;
    TArray<FString> ControlPath;
};

/** Presentation-only state for one local player. Never stored in gameplay preferences or asset files. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsNavigationMemory : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    FName Category=TEXT("Display");
    TMap<FName,FDreamSettingsFocusBookmark> Rows;
};
