#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DreamSettingsModel.h"
#include "InputCoreTypes.h"
#include "DreamSettingsInputGrouping.h"
#include "DreamSettingsMenuShortcuts.h"
#include "DreamSettingsLayout.generated.h"

class UDreamUserWidget;
class UDreamSettingsPresentation;
class UDreamSettingsViewModel;

/** Host-selected layout and help content. It has no setting values or backend dependencies. */
UCLASS(BlueprintType)
class DREAMSETTINGSUI_API UDreamSettingsLayout : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UDreamSettingsLayout();
    UPROPERTY(EditAnywhere,Category="Settings",meta=(AssetBundles="UI"))
    TSoftClassPtr<UDreamUserWidget> ScreenClass;
    /** Optional; an empty reference uses the presenter's default help. */
    UPROPERTY(EditAnywhere,Category="Settings",meta=(AssetBundles="UI"))
    TSoftObjectPtr<UDreamSettingsPresentation> Presentation;

    /** Display/graphics pages may reveal the live scene behind their controls. */
    UPROPERTY(EditAnywhere,Category="Settings|Scene preview") bool bAllowScenePreview=true;
    /** Equivalent to the legacy graphics-page background-hiding option. */
    UPROPERTY(EditAnywhere,Category="Settings|Scene preview") bool bHideBackgroundForGraphicsOptions=false;
    UPROPERTY(EditAnywhere,Category="Settings|Game benchmark") bool bEnableGameBenchmark=true;
    UPROPERTY(EditAnywhere,Category="Settings|Help") bool bAutoScrollDescriptions=true;
    UPROPERTY(EditAnywhere,Category="Settings|Input groups") EDreamSettingsInputGrouping InputGrouping=EDreamSettingsInputGrouping::DisplayCategory;
    UPROPERTY(EditAnywhere,Category="Settings|Navigation") FDreamSettingsMenuShortcuts MenuShortcuts;
    /** Optional localized titles, matching the host-authored mapping-context names in the legacy UI. */
    UPROPERTY(EditAnywhere,Category="Settings|Input groups",meta=(AllowedClasses="/Script/EnhancedInput.InputMappingContext")) TMap<FSoftObjectPath,FText> InputContextLabels;
    UPROPERTY(EditAnywhere,Category="Settings|Game benchmark") bool bEnableGameBenchmarkInShipping=false;
    /** Empty disables dedicated input. Only unmodified, non-repeat presses are accepted. */
    UPROPERTY(EditAnywhere,Category="Settings|Game benchmark") TArray<FKey> GameBenchmarkKeys={EKeys::F8,EKeys::Gamepad_FaceButton_Top};
    /** Empty shows every registry category. Otherwise the page embeds only these, in this order. */
    UPROPERTY(EditAnywhere,Category="Settings|Categories") TArray<FName> VisibleCategories;
    /** Tab selected on open when it is among the visible ones; empty selects the first. */
    UPROPERTY(EditAnywhere,Category="Settings|Categories") FName InitialCategory;
    void ConfigureViewModel(UDreamSettingsViewModel* Presenter) const;

    /** Resolve before creating the screen, or preload the UI asset bundle through AssetManager.
     * A configured broken reference is an error, never a silent fallback to another theme. */
    FDreamSettingsResult Resolve(TSubclassOf<UDreamUserWidget>& OutClass,UDreamSettingsPresentation*& OutPresentation) const;
};
