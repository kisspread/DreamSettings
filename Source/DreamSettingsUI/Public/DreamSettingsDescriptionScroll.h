#pragma once
#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamSettingsDescriptionScroll.generated.h"

class UDreamScrollBox;
class UDreamSettingsViewModel;
class UDreamSettingsPlayerSubsystem;

/** Optional, bounded reading motion. Any external scroll gives the player ownership until reopening. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsDescriptionScroll : public UDreamUIBehaviour
{
    GENERATED_BODY()
public:
    UDreamSettingsDescriptionScroll();
    UPROPERTY(EditAnywhere,Category="Description",meta=(ClampMin="0")) float UnitsPerSecond=18;
    UPROPERTY(EditAnywhere,Category="Description",meta=(ClampMin="0")) float InitialDelay=2;
    UPROPERTY(EditAnywhere,Category="Description",meta=(ClampMin="0")) float EndPause=2;
protected:
    virtual void OnEnable() override;
    virtual void OnDisable() override;
    virtual void OnUnregister() override;
    virtual void Tick(float DeltaTime) override;
private:
    bool Bind();
    void BeginVisit();
    void Scrolled(float Progress);
    void WriteOffset(float Offset);
    void Release();
    TWeakObjectPtr<UDreamScrollBox> Scroll;
    TWeakObjectPtr<UDreamSettingsViewModel> Presenter;
    TWeakObjectPtr<UDreamSettingsPlayerSubsystem> Preferences;
    FDelegateHandle ScrollHandle;
    FName Setting;
    bool bVisit=false,bManual=false,bWriting=false,bEligible=false,bResetPending=true,bForward=true;
    float LastOffset=0,LastExtent=-1;
    double LastTime=0,ResumeAt=0;
};
