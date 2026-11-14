#include "DreamSettingsTextPreparation.h"
#include "DreamSettingsViewModel.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamText.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Core/DreamUIGeometry.h"
#include "Internationalization/TextTransformer.h"
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
    CultureHandle=FInternationalization::Get().OnCultureChanged().AddUObject(this,&ThisClass::BeginCataloguePreparation);
    Presenter=FindPresenter();
    if (Presenter.IsValid())
    {
        Presenter->OnDetailsOpened.AddUObject(this,&ThisClass::BeginPreparation);
        Presenter->OnRowsPresented.AddUObject(this,&ThisClass::RowsPresented);
        Presenter->OnBenchmarkConfirmationOpened.AddUObject(this,&ThisClass::BeginPreparation);
        Presenter->OnResetConfirmationOpened.AddUObject(this,&ThisClass::BeginPreparation);
    }
    BeginCataloguePreparation();
}
void UDreamSettingsTextPreparation::BeginCataloguePreparation()
{
    bCatalogueReady=false; bWarmCatalogue=bPrewarmCategories; CatalogueFonts.Reset();
    BeginPreparation();
}
void UDreamSettingsTextPreparation::RowsPresented()
{
    if (!bPrewarmCategories || !bCatalogueReady) BeginPreparation();
}
void UDreamSettingsTextPreparation::WarmCatalogue()
{
    auto* VM=FindPresenter();
    if (!VM || !GetWidget()) return;
    FString Content=VM->GetCataloguePreparationText();
    TArray<FString> StaticTexts;
    TFunction<void(UDreamWidget*)> CollectStaticTexts=[&](UDreamWidget* Node)
    {
        if (!Node) return;
        if (const auto* Text=Cast<UDreamText>(Node->GetVisual())) StaticTexts.AddUnique(Text->GetText().ToString());
        for (auto* Child:Node->GetChildren()) CollectStaticTexts(Child);
    };
    CollectStaticTexts(GetWidget());
    Content+=TEXT("\n")+FString::Join(StaticTexts,TEXT("\n"));
    TArray<FDreamTextLayoutInput> PreparedStyles;
    TSet<UDreamUIFontData_FreeTypeRender*> Fonts;
    TFunction<void(UDreamWidget*)> Visit=[&](UDreamWidget* Node)
    {
        if (!Node) return;
        if (auto* Text=Cast<UDreamText>(Node->GetVisual()); Text && Text->GetFont())
        {
            auto Input=UDreamText::MakeLayoutInput(Text,Text->GetFontSize());
            Input.Content=Content;
            if (Input.TextTransform==EDreamUITextTransformPolicy::ToUpper) Input.Content=FTextTransformer::ToUpper(Content);
            else if (Input.TextTransform==EDreamUITextTransformPolicy::ToLower) Input.Content=FTextTransformer::ToLower(Content);
            // Layout shape/raster settings come from the real text. The temporary box must not
            // truncate catalogue paragraphs just because its template widget is currently hidden.
            Input.Width=10000; Input.Height=10000; Input.Pivot=FVector2f::ZeroVector;
            Input.bAutoWrapText=false; Input.WrapTextAt=0;
            Input.OverflowType=EDreamUITextOverflowType::HorizontalOverflow;
            if (!PreparedStyles.Contains(Input))
            {
                PreparedStyles.Add(Input);
                FDreamTextDisplayList Display;
                FDreamTextLayoutEngine::Layout(Input,Display);
                auto Paint=UDreamText::MakePaintParams(Text);
                Paint.Coverage.Report=nullptr; // Do not write the visible component's diagnostic counters.
                FDreamUIGeometry Geometry;
                TArray<FDreamUITextCharProperty> Characters;
                FDreamTextPainter::Paint(Display,Paint,Geometry,Characters);
                CollectFont(Cast<UDreamUIFontData_FreeTypeRender>(Text->GetFont()),Fonts);
            }
        }
        // Hidden scalar/key/sample controls still provide their actual typography templates.
        for (auto* Child:Node->GetChildren()) Visit(Child);
    };
    Visit(GetWidget());
    CatalogueFonts.Reset();
    for (auto* Font:Fonts) CatalogueFonts.Add(Font);
    bWarmCatalogue=false;
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
    if (bWarmCatalogue) WarmCatalogue();
    TSet<UDreamUIFontData_FreeTypeRender*> Fonts;
    for (const auto& Font:CatalogueFonts) if (Font) Fonts.Add(Font.Get());
    PrepareTexts(GetWidget(),Fonts);
    for (const auto* Font:Fonts) if (Font->GetPendingAsyncGlyphCount()>0) { QuietFrame=0; return; }
    if (!QuietFrame) QuietFrame=GFrameCounter;
    // Glyph-ready callbacks rebuild geometry, and the next font flush queues its GPU uploads.
    if (GFrameCounter<QuietFrame+2) return;
    bCatalogueReady=bPrewarmCategories;
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
