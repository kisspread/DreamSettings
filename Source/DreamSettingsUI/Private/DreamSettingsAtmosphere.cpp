#include "DreamSettingsAtmosphere.h"
#include "DreamSettingsViewModel.h"
#include "DreamSettingsPlayer.h"
#include "DreamSettingsNavigationMemory.h"
#include "DreamSettingsNavigationScope.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamImage.h"
#include "DreamNiagaraSystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

UDreamSettingsAtmosphere::UDreamSettingsAtmosphere()
{
    bStartWithTickEnabled=true; bTickEvenWhenPaused=true;
    ParticleMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/DreamSettings/UI/M_SettingsMote.M_SettingsMote")));
}

UDreamWidget* UDreamSettingsAtmosphere::Find(FName Name) const
{ return GetWidget() ? GetWidget()->FindChildByDisplayName(Name.ToString(),true) : nullptr; }

bool UDreamSettingsAtmosphere::TryBind()
{
    if (!GetWorld() || !GetWorld()->IsGameWorld()) return false;
    for (auto* Node=GetWidget();Node;Node=Node->GetParent())
        if (auto* Screen=Cast<UDreamUserWidget>(Node))
            if (auto* VM=Cast<UDreamSettingsViewModel>(Screen->GetViewModel(TEXT("Settings"))))
                if (auto* Memory=VM->GetNavigationMemory())
                {
                    Presenter=VM;
                    Preferences=Memory->GetLocalPlayer()->GetSubsystem<UDreamSettingsPlayerSubsystem>();
                    if (!Preferences.IsValid()) return false;
                    Preferences->OnReducedMotionChanged.AddUObject(this,&ThisClass::ReducedChanged);
                    VM->OnRowsPresented.AddUObject(this,&ThisClass::RowsPresented);
                    Category=VM->GetCurrentCategory();
                    if (auto* Back=Find(BackgroundName))
                        if (auto* Image=Cast<UDreamImage>(Back->GetVisual()))
                            if (auto* Material=Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject()))
                            {
                                OriginalBackground=Image->GetBrush(); bHaveBackground=true; Background=Image;
                                while (auto* Dynamic=Cast<UMaterialInstanceDynamic>(Material)) Material=Dynamic->Parent;
                                BackgroundMaterial=UMaterialInstanceDynamic::Create(Material,this);
                                Image->SetBrushFromMaterial(BackgroundMaterial);
                            }
                    if (auto* Motes=Find(ParticleName))
                        if (auto* Visual=Cast<UDreamNiagaraSystem>(Motes->GetVisual()))
                        {
                            Particles=Visual;
                            // Renderer material shading is optional and uses an ordinary saved Graph material.
                            auto* MoteMaterial=ParticleMaterial.LoadSynchronous();
                            if (MoteMaterial)
                            {
                                FDreamNiagaraRendererOverride Style;
                                Style.Emitter=TEXT("Motes"); Style.Renderer=0;
                                Style.Shading=EDreamNiagaraShading::Material; Style.Material=MoteMaterial;
                                Visual->SetRendererOverrides({Style});
                            }
                            if (bParticles && !Preferences->GetReducedMotion()) Visual->Activate(true);
                        }
                    RefreshRows();
                    ReducedChanged(Preferences->GetReducedMotion());
                    return true;
                }
    return false;
}

void UDreamSettingsAtmosphere::OnEnable()
{ Super::OnEnable(); Release(); bOpened=false; MotionTime=0; SetCanExecuteTick(true); TryBind(); }

void UDreamSettingsAtmosphere::Reveal(UDreamWidget* Node,FVector2D Offset,float Delay)
{
    if (!Node || !bTransitions || bSuppressed) return;
    FReveal Entry;
    Entry.Widget=Node; Entry.OriginalOffset=Node->GetRenderOffset(); Entry.Offset=Offset;
    Entry.OriginalOpacity=Node->GetRenderOpacity(); Entry.Started=FPlatformTime::Seconds()+Delay;
    Node->SetRenderOffset(Entry.OriginalOffset+Offset);
    Node->SetRenderOpacity(Entry.OriginalOpacity*0.72f);
    Reveals.Add(Entry);
}

void UDreamSettingsAtmosphere::Settle()
{
    for (const auto& Entry:Reveals) if (Entry.Widget.IsValid())
    { Entry.Widget->SetRenderOffset(Entry.OriginalOffset); Entry.Widget->SetRenderOpacity(Entry.OriginalOpacity); }
    Reveals.Reset(); Pulse=0;
    if (BackgroundMaterial) BackgroundMaterial->SetScalarParameterValue(TEXT("CategoryPulse"),0);
}

void UDreamSettingsAtmosphere::ReducedChanged(bool Reduced)
{
    if (Reduced) { bSuppressed=true; Settle(); }
    if (Particles.IsValid())
    {
        const bool Allowed=!Reduced && bParticles && Presenter.IsValid() && Presenter->ActionsEnabled && !Presenter->ScenePreviewActive;
        Particles->SetPaused(!Allowed);
        if (auto* Node=Particles->GetWidget()) Node->SetRenderOpacity(Allowed ? 0.65f : 0);
    }
}

void UDreamSettingsAtmosphere::RowsPresented()
{ RefreshRowsAfter=GFrameCounter+1; }

void UDreamSettingsAtmosphere::RefreshRows()
{
    for (const auto& Surface:Surfaces) if (Surface.Image.IsValid() && Surface.Material.IsValid()
        && Surface.Image->GetBrush().GetResourceObject()==Surface.Material.Get()) Surface.Image->SetBrush(Surface.Original);
    Surfaces.Reset(); SurfaceMaterials.Reset();
    TFunction<void(UDreamWidget*)> Visit=[&](UDreamWidget* Node)
    {
        if (!Node || !Node->GetWidgetActiveInHierarchy()) return;
        if (auto* Anchor=Node->GetComponent<UDreamSettingsFocusAnchor>(); Anchor && !Anchor->GetSettingId().IsNone())
        {
            if (auto* Plate=Node->FindChildByDisplayName(TEXT("RowPlate"),true))
                if (auto* Image=Cast<UDreamImage>(Plate->GetVisual()))
                    if (auto* Material=Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject()))
                    {
                        FSurface Surface; Surface.Widget=Node; Surface.Image=Image; Surface.Original=Image->GetBrush();
                        while (auto* Dynamic=Cast<UMaterialInstanceDynamic>(Material)) Material=Dynamic->Parent;
                        auto* Instance=UMaterialInstanceDynamic::Create(Material,this);
                        SurfaceMaterials.Add(Instance); Surface.Material=Instance;
                        Image->SetBrushFromMaterial(Instance); Surfaces.Add(Surface);
                    }
            return;
        }
        for (auto* Child:Node->GetChildren()) Visit(Child);
    };
    Visit(Find(ContentName));
}

void UDreamSettingsAtmosphere::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!GetWorld() || !GetWorld()->IsGameWorld()) { SetCanExecuteTick(false); return; }
    if ((!Presenter.IsValid() || !Preferences.IsValid()) && !TryBind()) return;
    auto* VM=Presenter.Get();
    const bool Reduced=Preferences->GetReducedMotion();
    const bool Hidden=!GetWidget()->GetRenderVisibleInHierarchy();
    bSuppressed=Reduced || Hidden || VM->PreparingText || !VM->ActionsEnabled || VM->ScenePreviewActive;
    if (bSuppressed) Settle();
    if (RefreshRowsAfter && GFrameCounter>=RefreshRowsAfter)
    { RefreshRowsAfter=0; RefreshRows(); }
    const float Dt=FMath::Clamp(DeltaTime,0.f,0.1f);
    if (BackgroundMaterial && !bSuppressed && bAmbientMotion)
    {
        MotionTime+=Dt*FMath::Max(MotionRate,0.f);
        BackgroundMaterial->SetScalarParameterValue(TEXT("MotionTime"),MotionTime);
        Pulse=FMath::Max(0.f,Pulse-Dt*2.2f);
        BackgroundMaterial->SetScalarParameterValue(TEXT("CategoryPulse"),Pulse);
    }
    if (Particles.IsValid())
    {
        const bool Allowed=!bSuppressed && bParticles;
        if (Allowed && !Particles->IsActive()) Particles->Activate(false);
        Particles->SetPaused(!Allowed);
        if (auto* Node=Particles->GetWidget())
        {
            Node->SetRenderOpacity(Allowed ? 0.65f : 0.f);
            Particles->SetPixelsPerUnit(FMath::Max(Node->GetWidth()/1280.f,0.1f));
        }
    }
    if (!bOpened && !VM->PreparingText)
    {
        bOpened=true; Category=VM->GetCurrentCategory(); RefreshRows();
        Reveal(Find(TEXT("Header")),FVector2D(0,10));
        Reveal(Find(TEXT("NavigationSurface")),FVector2D(-10,0),0.035f);
        Reveal(Find(ContentName),FVector2D(0,-10),0.065f);
        Reveal(Find(TEXT("Actions")),FVector2D(0,-6),0.09f);
    }
    if (Category!=VM->GetCurrentCategory())
    {
        Settle(); Category=VM->GetCurrentCategory(); RefreshRows();
        if (!bSuppressed)
        {
            Pulse=1;
            Reveal(Find(TEXT("CategoryHeading")),FVector2D(8,0));
            for (int32 Index=0;Index<Surfaces.Num();++Index)
                Reveal(Surfaces[Index].Widget.Get(),FVector2D(0,-7),FMath::Min(Index,5)*0.018f);
        }
    }
    const double Now=FPlatformTime::Seconds();
    for (int32 Index=Reveals.Num()-1;Index>=0;--Index)
    {
        auto& Entry=Reveals[Index];
        if (!Entry.Widget.IsValid()) { Reveals.RemoveAtSwap(Index); continue; }
        const float T=FMath::Clamp(float((Now-Entry.Started)/0.30),0.f,1.f);
        const float Remaining=FMath::Pow(1.f-T,3.f);
        Entry.Widget->SetRenderOffset(Entry.OriginalOffset+Entry.Offset*Remaining);
        Entry.Widget->SetRenderOpacity(Entry.OriginalOpacity*(1.f-0.28f*Remaining));
        if (T>=1) Reveals.RemoveAtSwap(Index);
    }
    auto* Input=UDreamUIInputServices::Get(this);
    auto* Scope=GetWidget()->GetComponent<UDreamSettingsNavigationScope>();
    const int32 User=Scope ? Scope->GetUserIndex() : 0;
    for (auto& Surface:Surfaces) if (Surface.Widget.IsValid() && Surface.Material.IsValid())
    {
        const bool Lit=VM->ActionsEnabled && Input && (Input->IsHovered(Surface.Widget.Get(),User)
            || (Input->IsFocusVisible(User) && Input->HasFocusedDescendant(Surface.Widget.Get(),User)));
        const float Target=Lit ? 1.f : 0.f;
        const float Next=Reduced ? Target : FMath::FInterpTo(Surface.Focus,Target,Dt,14.f);
        if (!FMath::IsNearlyEqual(Next,Surface.Focus,0.001f))
        { Surface.Focus=Next; Surface.Material->SetScalarParameterValue(TEXT("FocusAmount"),Next); }
    }
}

void UDreamSettingsAtmosphere::Release()
{
    if (Presenter.IsValid()) Presenter->OnRowsPresented.RemoveAll(this);
    if (Preferences.IsValid()) Preferences->OnReducedMotionChanged.RemoveAll(this);
    Settle();
    if (Particles.IsValid()) { Particles->SetPaused(true); Particles->Deactivate(); }
    if (Background.IsValid() && bHaveBackground && Background->GetBrush().GetResourceObject()==BackgroundMaterial)
        Background->SetBrush(OriginalBackground);
    for (const auto& Surface:Surfaces) if (Surface.Image.IsValid() && Surface.Material.IsValid()
        && Surface.Image->GetBrush().GetResourceObject()==Surface.Material.Get()) Surface.Image->SetBrush(Surface.Original);
    Reveals.Reset(); Surfaces.Reset(); SurfaceMaterials.Reset(); BackgroundMaterial=nullptr;
    Background.Reset(); Particles.Reset(); Presenter.Reset(); Preferences.Reset();
    OriginalBackground=FDreamUIImageBrush(); bHaveBackground=false; bOpened=false; bSuppressed=true; RefreshRowsAfter=0;
    SetCanExecuteTick(false);
}
void UDreamSettingsAtmosphere::OnDisable() { Release(); Super::OnDisable(); }
void UDreamSettingsAtmosphere::OnUnregister() { Release(); Super::OnUnregister(); }
