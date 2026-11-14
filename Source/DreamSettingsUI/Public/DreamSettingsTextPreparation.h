#pragma once
#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamSettingsTextPreparation.generated.h"

class UDreamSettingsViewModel;

/** Keeps an opaque, font-free loading layer up while the page's translated glyphs are generated. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsTextPreparation : public UDreamUIBehaviour
{
    GENERATED_BODY()
public:
    UDreamSettingsTextPreparation();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings") FString CoverName=TEXT("TextPreparationCover");
protected:
    virtual void OnEnable() override;
    virtual void OnDisable() override;
    virtual void OnUnregister() override;
    virtual void Tick(float DeltaTime) override;
private:
    void BeginPreparation();
    void Release();
    UDreamSettingsViewModel* FindPresenter() const;
    FDelegateHandle CultureHandle;
    TWeakObjectPtr<UDreamSettingsViewModel> Presenter;
    uint64 StartedFrame=0,QuietFrame=0;
};
