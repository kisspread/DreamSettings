#pragma once
#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIImageBrush.h"
#include "DreamSettingsHDRPreview.generated.h"
class UMaterialInstanceDynamic;

/** Drives a DreamShader calibration material from this game's actual display target. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsHDRPreview : public UDreamUIBehaviour
{
    GENERATED_BODY()
public:
    UDreamSettingsHDRPreview();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings") float CandidateNits=200;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings") bool PeakPattern=false;
    UFUNCTION(BlueprintCallable,Category="Settings") void SetCandidateNits(float Value) { CandidateNits=Value; }
    UFUNCTION(BlueprintCallable,Category="Settings") void SetPeakPattern(bool Value) { PeakPattern=Value; }
protected:
    virtual void OnEnable() override;
    virtual void OnDisable() override;
    virtual void OnUnregister() override;
    virtual void Tick(float DeltaTime) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FDreamSettingsHDRPreviewTest;
#endif
    void Release();
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Instance;
    UPROPERTY(Transient) FDreamUIImageBrush OriginalBrush;
    bool HaveOriginal=false;
    float LastCandidate=-1,LastWhite=-1,LastEnabled=-1,LastPeak=-1;
};
