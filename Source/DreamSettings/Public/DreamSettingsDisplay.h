#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamSettingsDisplay.generated.h"

/** Read-only facts about the monitor containing the game window; no desktop/display settings are changed. */
USTRUCT(BlueprintType)
struct DREAMSETTINGS_API FDreamSettingsDisplayCapabilities
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly,Category="Display") bool HasWindow=false;
    UPROPERTY(BlueprintReadOnly,Category="Display") bool GameWorld=false;
    UPROPERTY(BlueprintReadOnly,Category="Display") FString MonitorName;
    UPROPERTY(BlueprintReadOnly,Category="Display") FString MonitorId;
    UPROPERTY(BlueprintReadOnly,Category="Display") int32 MonitorCount=0;
    UPROPERTY(BlueprintReadOnly,Category="Display") FIntPoint DesktopResolution=FIntPoint::ZeroValue;
    /** The desktop's nominal refresh rate reported by Windows; not the game's frame rate. */
    UPROPERTY(BlueprintReadOnly,Category="Display") int32 DesktopRefreshHz=0;
    UPROPERTY(BlueprintReadOnly,Category="Display") bool HDRAllowed=false;
    UPROPERTY(BlueprintReadOnly,Category="Display") bool PlatformSupportsHDR=false;
    UPROPERTY(BlueprintReadOnly,Category="Display") bool CurrentMonitorSupportsHDR=false;
    UPROPERTY(BlueprintReadOnly,Category="Display") bool MonitorCapabilitiesKnown=false;
    UPROPERTY(BlueprintReadOnly,Category="Display") float MonitorMinimumNits=0;
    UPROPERTY(BlueprintReadOnly,Category="Display") float MonitorMaximumNits=0;
    UPROPERTY(BlueprintReadOnly,Category="Display") float MonitorFullFrameNits=0;
    /** Output metadata may be SDR even when the monitor can present HDR. */
    UPROPERTY(BlueprintReadOnly,Category="Display") bool OutputMetadataSupportsHDR=false;
    UPROPERTY(BlueprintReadOnly,Category="Display") bool HDROutputEnabled=false;
    UPROPERTY(BlueprintReadOnly,Category="Display") bool WindowIsHDR=false;
    UPROPERTY(BlueprintReadOnly,Category="Display") int32 OutputFormat=0;
    /** Effective output metadata; HDRHelper returns SDR defaults while engine HDR is off. */
    UPROPERTY(BlueprintReadOnly,Category="Display") float MinimumNits=0;
    UPROPERTY(BlueprintReadOnly,Category="Display") float MaximumNits=0;
    UPROPERTY(BlueprintReadOnly,Category="Display") float FullFrameNits=0;
    UPROPERTY(BlueprintReadOnly,Category="Display") float PaperWhiteNits=0;
};

UCLASS()
class DREAMSETTINGS_API UDreamSettingsDisplayLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Settings|Display",meta=(WorldContext="WorldContextObject"))
    static FDreamSettingsDisplayCapabilities QueryCapabilities(UObject* WorldContextObject);
    /** Engine/window metadata agree on a display HDR format; not a panel calibration measurement. */
    UFUNCTION(BlueprintPure,Category="Settings|Display")
    static bool IsHDRPresentationActive(const FDreamSettingsDisplayCapabilities& Capabilities);
    static FText Describe(const FDreamSettingsDisplayCapabilities& Capabilities);
};
