#pragma once
#include "DreamSettingsModel.h"
#include "Features/IModularFeature.h"

struct DREAMSETTINGS_API FDreamUpscalerQuality
{
    FString Id;
    FText Label;
    float ScreenPercentage = 100;
};

/** Implemented by optional companion plugins. The core has no vendor module dependency. */
class DREAMSETTINGS_API IDreamSettingsUpscaler : public IModularFeature
{
public:
    static FName FeatureName() { return TEXT("DreamSettings.Upscaler"); }
    virtual FName Id() const = 0;
    virtual FText Label() const = 0;
    virtual FDreamSettingsResult Availability() const = 0;
    virtual bool IsEnabled() const = 0;
    virtual FDreamSettingsResult SetEnabled(bool bEnabled) = 0;
    virtual TArray<FDreamUpscalerQuality> QualityModes(FIntPoint OutputResolution) const = 0;
    /** Register hidden, actual vendor quality parameters; ordinary global render scale is already registered. */
    virtual FDreamSettingsResult RegisterSettings(FDreamSettingsRegistry& Registry) { return {}; }
    virtual TArray<FName> QualityDependencies() const { return { TEXT("Graphics.ResolutionScale") }; }
    virtual FString QualityFromValues(const FDreamSettingsValues& Values, FIntPoint OutputResolution) const;
    virtual FDreamSettingsResult QualityChanges(const FString& Quality, FIntPoint OutputResolution, FDreamSettingsValues& Out) const;
};

/** Shared labels/IDs across adapters. Values and supported modes are queried from the installed SDK. */
namespace DreamUpscalerQuality
{
    DREAMSETTINGS_API FText Label(const FString& Id);
}
