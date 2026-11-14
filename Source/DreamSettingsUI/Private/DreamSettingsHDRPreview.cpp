#include "DreamSettingsHDRPreview.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamImage.h"
#include "DreamUIRender/DreamUIHDR.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealClient.h"

UDreamSettingsHDRPreview::UDreamSettingsHDRPreview()
{ bStartWithTickEnabled=true; bTickEvenWhenPaused=true; }
void UDreamSettingsHDRPreview::OnEnable()
{ Super::OnEnable(); SetCanExecuteTick(true); }
void UDreamSettingsHDRPreview::OnDisable()
{ Release(); Super::OnDisable(); }
void UDreamSettingsHDRPreview::OnUnregister()
{ Release(); Super::OnUnregister(); }
void UDreamSettingsHDRPreview::Release()
{
    SetCanExecuteTick(false);
    if (HaveOriginal) if (auto* Image=GetWidget() ? Cast<UDreamImage>(GetWidget()->GetVisual()) : nullptr)
        if (Image->GetBrush().GetResourceObject()==Instance) Image->SetBrush(OriginalBrush);
    Instance=nullptr; OriginalBrush=FDreamUIImageBrush(); HaveOriginal=false;
    LastCandidate=LastWhite=LastEnabled=LastPeak=-1;
}
void UDreamSettingsHDRPreview::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    for (auto* Node=GetWidget();Node;Node=Node->GetParent()) if (!Node->GetShown()) return;
    auto* Image=GetWidget() ? Cast<UDreamImage>(GetWidget()->GetVisual()) : nullptr;
    if (!Image) return;
    // Dynamic DUI rows may be duplicated from a live template. Never share that template's MID.
    if (Instance && Instance->GetOuter()!=this)
    {
        Instance=nullptr; HaveOriginal=false; OriginalBrush=FDreamUIImageBrush();
        LastCandidate=LastWhite=LastEnabled=LastPeak=-1;
    }
    if (!Instance)
    {
        auto* Material=Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
        if (!Material) return; // DUI properties can arrive after OnEnable.
        auto* Seed=Cast<UMaterialInstanceDynamic>(Material);
        while (auto* Dynamic=Cast<UMaterialInstanceDynamic>(Material)) Material=Dynamic->Parent;
        if (!Material) return;
        OriginalBrush=Image->GetBrush(); HaveOriginal=true;
        Instance=UMaterialInstanceDynamic::Create(Material,this);
        if (Seed) Instance->CopyInterpParameters(Seed);
        Image->SetBrushFromMaterial(Instance);
    }
    auto* Viewport=GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    const auto Output=Viewport && Viewport->Viewport ? DreamUIHDR::FromTarget(*Viewport->Viewport) : DreamUIHDR::FOutput{};
    const float Enabled=Output.IsActive() && GetWorld()->WorldType==EWorldType::Game ? 1.f : 0.f;
    const float White=Output.PaperWhiteNits;
    const float Candidate=FMath::IsFinite(CandidateNits) ? FMath::Clamp(CandidateNits,PeakPattern ? 100.f : 80.f,PeakPattern ? 4000.f : 400.f) : 200.f;
    const float Peak=PeakPattern ? 1.f : 0.f;
    if (Candidate!=LastCandidate) { Instance->SetScalarParameterValue(TEXT("CandidateNits"),Candidate); LastCandidate=Candidate; }
    if (White!=LastWhite) { Instance->SetScalarParameterValue(TEXT("PaperWhiteNits"),White); LastWhite=White; }
    if (Peak!=LastPeak) { Instance->SetScalarParameterValue(TEXT("PeakPattern"),Peak); LastPeak=Peak; }
    if (Enabled!=LastEnabled) { Instance->SetScalarParameterValue(TEXT("OutputEnabled"),Enabled); LastEnabled=Enabled; }
}
