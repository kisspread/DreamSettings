#pragma once
#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamSettingsTextPreparation.generated.h"

class UDreamSettingsViewModel;
class UDreamUIFontData_FreeTypeRender;

/** Keeps an opaque, font-free loading layer up while the page's translated glyphs are generated. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsTextPreparation : public UDreamUIBehaviour
{
    GENERATED_BODY()
public:
    UDreamSettingsTextPreparation();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings") FString CoverName=TEXT("TextPreparationCover");
    /** Prepares catalogue glyphs once so normal tab changes never mask the page.
     * Disable for host pages that instantiate different fonts only after a tab is selected. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Settings") bool bPrewarmCategories=true;
protected:
    virtual void OnEnable() override;
    virtual void OnDisable() override;
    virtual void OnUnregister() override;
    virtual void Tick(float DeltaTime) override;
private:
    void BeginPreparation();
    void BeginCataloguePreparation();
    void RowsPresented();
    void WarmCatalogue();
    void Release();
    UDreamSettingsViewModel* FindPresenter() const;
    FDelegateHandle CultureHandle,TextRevisionHandle;
    TWeakObjectPtr<UDreamSettingsViewModel> Presenter;
    uint64 StartedFrame=0,QuietFrame=0;
    bool bCatalogueReady=false,bWarmCatalogue=false,bHasPresented=false;
    UPROPERTY(Transient) TArray<TObjectPtr<UDreamUIFontData_FreeTypeRender>> CatalogueFonts;
};
