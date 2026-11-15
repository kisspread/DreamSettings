#pragma once
#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIImageBrush.h"
#include "DreamSettingsAtmosphere.generated.h"

class UDreamSettingsViewModel;
class UDreamSettingsPlayerSubsystem;
class UDreamWidget;
class UDreamImage;
class UDreamNiagaraSystem;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/** Optional default-theme presentation. No setting writes and no dependency on a host game. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsAtmosphere : public UDreamUIBehaviour
{
    GENERATED_BODY()
public:
    UDreamSettingsAtmosphere();
    UPROPERTY(EditAnywhere,Category="Settings|Appearance") bool bAmbientMotion=true;
    UPROPERTY(EditAnywhere,Category="Settings|Appearance") bool bParticles=true;
    UPROPERTY(EditAnywhere,Category="Settings|Appearance") bool bTransitions=true;
    UPROPERTY(EditAnywhere,Category="Settings|Appearance",meta=(ClampMin="0",ClampMax="2")) float MotionRate=1;
    UPROPERTY(EditAnywhere,Category="Settings|Appearance") FName BackgroundName=TEXT("Background");
    UPROPERTY(EditAnywhere,Category="Settings|Appearance") FName ParticleName=TEXT("AmbientMotes");
    UPROPERTY(EditAnywhere,Category="Settings|Appearance") FName ContentName=TEXT("CategoryContent");
    UPROPERTY(EditAnywhere,Category="Settings|Appearance") TSoftObjectPtr<UMaterialInterface> ParticleMaterial;
    float GetMotionTime() const { return MotionTime; }
    int32 GetTransitionCount() const { return Reveals.Num(); }
    bool IsMotionSuppressed() const { return bSuppressed; }
protected:
    virtual void OnEnable() override;
    virtual void OnDisable() override;
    virtual void OnUnregister() override;
    virtual void Tick(float DeltaTime) override;
private:
    bool TryBind();
    void ReducedChanged(bool Reduced);
    void Release();
    void Settle();
    void RefreshRows();
    void RowsPresented();
    void Reveal(UDreamWidget* Node,FVector2D Offset,float Delay=0);
    UDreamWidget* Find(FName Name) const;
    struct FReveal
    {
        TWeakObjectPtr<UDreamWidget> Widget;
        FVector2D OriginalOffset,Offset;
        float OriginalOpacity=1;
        double Started=0;
    };
    struct FSurface
    {
        TWeakObjectPtr<UDreamWidget> Widget;
        TWeakObjectPtr<UDreamImage> Image;
        TWeakObjectPtr<UMaterialInstanceDynamic> Material;
        FDreamUIImageBrush Original;
        float Focus=0;
    };
    TArray<FReveal> Reveals;
    TArray<FSurface> Surfaces;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> SurfaceMaterials;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BackgroundMaterial;
    UPROPERTY(Transient) FDreamUIImageBrush OriginalBackground;
    TWeakObjectPtr<UDreamSettingsViewModel> Presenter;
    TWeakObjectPtr<UDreamSettingsPlayerSubsystem> Preferences;
    TWeakObjectPtr<UDreamImage> Background;
    TWeakObjectPtr<UDreamNiagaraSystem> Particles;
    FName Category;
    float MotionTime=0,Pulse=0;
    uint64 RefreshRowsAfter=0;
    bool bOpened=false,bSuppressed=true,bHaveBackground=false;
};
