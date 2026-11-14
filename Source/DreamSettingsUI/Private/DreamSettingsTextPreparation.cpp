#include "DreamSettingsTextPreparation.h"
#include "DreamSettingsViewModel.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamText.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Internationalization/Internationalization.h"
#include "Engine/World.h"

namespace
{
    void CollectFont(UDreamUIFontData_FreeTypeRender* Font,TSet<UDreamUIFontData_FreeTypeRender*>& Fonts)
    {
        if (!Font || Fonts.Contains(Font)) return;
        Fonts.Add(Font);
        for (const auto& Fallback:Font->GetFallbacks()) CollectFont(Fallback.Font,Fonts);
        // Style faces are rasterized into this owning font's atlas and counted by its queue.
    }
    void PrepareTexts(UDreamWidget* Node,TSet<UDreamUIFontData_FreeTypeRender*>& Fonts)
    {
        if (!Node || !Node->GetWidgetActiveInHierarchy() || !Node->GetRenderVisibleInHierarchy()) return;
        if (auto* Text=Cast<UDreamText>(Node->GetVisual()))
        {
            // Request normal shaped geometry, including fallback/style faces and coverage glyphs.
            // Do not rasterize synchronously or guess Unicode code points from the source string.
            Text->GetCacheTextGeometryData();
            CollectFont(Cast<UDreamUIFontData_FreeTypeRender>(Text->GetFont()),Fonts);
        }
        for (auto* Child:Node->GetChildren()) PrepareTexts(Child,Fonts);
    }
}
UDreamSettingsTextPreparation::UDreamSettingsTextPreparation()
{ bStartWithTickEnabled=false; bTickEvenWhenPaused=true; }
UDreamSettingsViewModel* UDreamSettingsTextPreparation::FindPresenter() const
{
    for (auto* Node=GetWidget();Node;Node=Node->GetParent())
        if (auto* Screen=Cast<UDreamUserWidget>(Node))
            if (auto* VM=Cast<UDreamSettingsViewModel>(Screen->GetViewModel(TEXT("Settings")))) return VM;
    return nullptr;
}
void UDreamSettingsTextPreparation::OnEnable()
{
    Super::OnEnable();
    if (!GetWorld() || !GetWorld()->IsGameWorld()) return;
    CultureHandle=FInternationalization::Get().OnCultureChanged().AddUObject(this,&ThisClass::BeginPreparation);
    Presenter=FindPresenter();
    if (Presenter.IsValid())
    {
        Presenter->OnDetailsOpened.AddUObject(this,&ThisClass::BeginPreparation);
        Presenter->OnRowsPresented.AddUObject(this,&ThisClass::BeginPreparation);
        Presenter->OnBenchmarkConfirmationOpened.AddUObject(this,&ThisClass::BeginPreparation);
        Presenter->OnResetConfirmationOpened.AddUObject(this,&ThisClass::BeginPreparation);
    }
    BeginPreparation();
}
void UDreamSettingsTextPreparation::BeginPreparation()
{
    StartedFrame=GFrameCounter; QuietFrame=0;
    if (auto* VM=FindPresenter()) VM->SetPreparingText(true);
    // Set the cover immediately, before the next render; its FieldNotify binding also keeps it in sync.
    if (GetWidget()) if (auto* Cover=GetWidget()->FindChildByDisplayName(CoverName,true)) Cover->SetWidgetActive(true);
    SetCanExecuteTick(true);
}
void UDreamSettingsTextPreparation::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    auto* VM=FindPresenter();
    if (!VM || !VM->Ready) { Release(); return; }
    if (GFrameCounter<StartedFrame+2) return; // Dynamic row bindings must have their new text first.
    TSet<UDreamUIFontData_FreeTypeRender*> Fonts;
    PrepareTexts(GetWidget(),Fonts);
    for (const auto* Font:Fonts) if (Font->GetPendingAsyncGlyphCount()>0) { QuietFrame=0; return; }
    if (!QuietFrame) QuietFrame=GFrameCounter;
    // Glyph-ready callbacks rebuild geometry, and the next font flush queues its GPU uploads.
    if (GFrameCounter<QuietFrame+2) return;
    Release();
}
void UDreamSettingsTextPreparation::Release()
{
    if (auto* VM=FindPresenter())
    {
        VM->SetPreparingText(false);
        // Refresh can publish a deferred regroup and synchronously begin a new pass.
        // That newer pass owns the cover and tick; do not retire it here.
        if (VM->PreparingText) return;
    }
    if (GetWidget()) if (auto* Cover=GetWidget()->FindChildByDisplayName(CoverName,true)) Cover->SetWidgetActive(false);
    SetCanExecuteTick(false);
}
void UDreamSettingsTextPreparation::OnDisable()
{
    if (Presenter.IsValid()) { Presenter->OnDetailsOpened.RemoveAll(this); Presenter->OnRowsPresented.RemoveAll(this); Presenter->OnBenchmarkConfirmationOpened.RemoveAll(this); Presenter->OnResetConfirmationOpened.RemoveAll(this); }
    Presenter.Reset();
    FInternationalization::Get().OnCultureChanged().Remove(CultureHandle); CultureHandle.Reset();
    Release(); Super::OnDisable();
}
void UDreamSettingsTextPreparation::OnUnregister()
{
    if (Presenter.IsValid()) { Presenter->OnDetailsOpened.RemoveAll(this); Presenter->OnRowsPresented.RemoveAll(this); Presenter->OnBenchmarkConfirmationOpened.RemoveAll(this); Presenter->OnResetConfirmationOpened.RemoveAll(this); }
    Presenter.Reset();
    FInternationalization::Get().OnCultureChanged().Remove(CultureHandle); CultureHandle.Reset();
    Release(); Super::OnUnregister();
}
