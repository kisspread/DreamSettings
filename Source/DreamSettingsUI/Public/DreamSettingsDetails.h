#pragma once
#include "CoreMinimal.h"
#include "ViewModel/DreamViewModel.h"
#include "Core/DreamUIImageBrush.h"
#include "Containers/Ticker.h"
#include "DreamSettingsPresentation.h"
#include "DreamSettingsDetails.generated.h"

struct FStreamableHandle;

/** Theme-independent detail state. Images are loaded only while the details surface is open. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsDetails : public UDreamViewModel
{
    GENERATED_BODY()
public:
    void Update(const FDreamSettingDefinition& Definition,const FDreamSettingValue& Value,const FText& FallbackBody,
        const FText& CurrentValue,const FText& DefaultValue,const FText& State,const UDreamSettingsPresentation* Presentation,bool LoadImage);
    void Reset();
    virtual void BeginDestroy() override;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FName SettingId;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText Title;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText Body;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText Additional;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText ValueText;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText DefaultText;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText StateText;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText ImageCaption;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText ImageStatus;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FDreamUIImageBrush ImageBrush;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool HasImage=false;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool HasImageStatus=false;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool HasImageCaption=false;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool HasAdditional=false;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool HasState=false;
private:
    void RequestImage(TSoftObjectPtr<UTexture2D> Image);
    void SetImage(UTexture2D* Image);
    void FinishImage(UTexture2D* Image,uint64 Revision);
    void CancelImageWait();
    UPROPERTY(Transient) TObjectPtr<UTexture2D> PendingImage;
    FTSTicker::FDelegateHandle ImageReadyHandle;
    FSoftObjectPath RequestedImage;
    TSharedPtr<FStreamableHandle> ImageHandle;
    uint64 ImageRevision=0;
};
