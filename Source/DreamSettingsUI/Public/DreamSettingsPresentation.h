#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DreamSettingsModel.h"
#include "DreamSettingsPresentation.generated.h"

class UTexture2D;

/** Exact choice-token match, or the highest scalar threshold not greater than the current value. */
USTRUCT(BlueprintType)
struct DREAMSETTINGSUI_API FDreamSettingsHelpVariant
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Help") bool ForToken=false;
    UPROPERTY(EditAnywhere,Category="Help",meta=(EditCondition="ForToken")) FString Token;
    UPROPERTY(EditAnywhere,Category="Help",meta=(EditCondition="!ForToken")) double Minimum=0;
    UPROPERTY(EditAnywhere,Category="Help",meta=(MultiLine=true)) FText Additional;
    UPROPERTY(EditAnywhere,Category="Help") bool OverrideImage=false;
    UPROPERTY(EditAnywhere,Category="Help",meta=(EditCondition="OverrideImage")) TSoftObjectPtr<UTexture2D> Image;
    UPROPERTY(EditAnywhere,Category="Help",meta=(EditCondition="OverrideImage")) FText ImageCaption;
};

USTRUCT(BlueprintType)
struct DREAMSETTINGSUI_API FDreamSettingsHelpContent
{
    GENERATED_BODY()
    /** Empty uses the setting definition's description. */
    UPROPERTY(EditAnywhere,Category="Help",meta=(MultiLine=true)) FText Body;
    UPROPERTY(EditAnywhere,Category="Help",meta=(MultiLine=true)) FText Additional;
    UPROPERTY(EditAnywhere,Category="Help") TSoftObjectPtr<UTexture2D> Image;
    UPROPERTY(EditAnywhere,Category="Help") FText ImageCaption;
    UPROPERTY(EditAnywhere,Category="Help") TArray<FDreamSettingsHelpVariant> Variants;
};

/** Optional presentation data, separate from backend definitions and saved preferences. */
UCLASS(BlueprintType)
class DREAMSETTINGSUI_API UDreamSettingsPresentation : public UDataAsset
{
    GENERATED_BODY()
public:
    UDreamSettingsPresentation();
    UPROPERTY(EditAnywhere,Category="Settings") TMap<FName,FDreamSettingsHelpContent> Settings;
    FDreamSettingsHelpContent Resolve(FName Id,const FDreamSettingValue& Value) const;
};
