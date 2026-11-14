#pragma once
#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamSettingsSubtitleSizing.generated.h"

/** DUI bindings target behaviours, not layout containers. Forward the subtitle region's size to SizeBox. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsSubtitleSizing : public UDreamUIBehaviour
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintGetter="GetMinimumSize",BlueprintSetter="SetMinimumSize",Category="Subtitles")
    FVector2D MinimumSize=FVector2D::ZeroVector;
    UFUNCTION(BlueprintGetter) FVector2D GetMinimumSize() const { return MinimumSize; }
    UFUNCTION(BlueprintSetter) void SetMinimumSize(FVector2D Value);
protected:
    virtual void OnEnable() override;
};
