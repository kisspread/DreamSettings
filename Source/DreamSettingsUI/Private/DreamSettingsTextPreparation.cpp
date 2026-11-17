#include "DreamSettingsTextPreparation.h"
#include "DreamSettingsViewModel.h"
#include "DreamSettingsConfiguration.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamText.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Core/DreamUIGeometry.h"
#include "Internationalization/TextTransformer.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
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
    TextRevisionHandle=FTextLocalizationManager::Get().OnTextRevisionChangedEvent.AddUObject(this,&ThisClass::BeginCataloguePreparation);
    Presenter=FindPresenter();
    if (Presenter.IsValid())
    {
        Presenter->OnDetailsOpened.AddUObject(this,&ThisClass::BeginPreparation);
        Presenter->OnRowsPresented.AddUObject(this,&ThisClass::RowsPresented);
        Presenter->OnBenchmarkConfirmationOpened.AddUObject(this,&ThisClass::BeginPreparation);
        Presenter->OnResetConfirmationOpened.AddUObject(this,&ThisClass::BeginPreparation);
    }
    if (bHasPresented && PreparedRevision==FTextLocalizationManager::Get().GetTextRevision()
        && PreparedLanguage==FInternationalization::Get().GetCurrentLanguage()->GetName())
    {
        StartedFrame=GFrameCounter; QuietFrame=0; bWarmCatalogue=false;
        SetCanExecuteTick(true);
    }
    else BeginCataloguePreparation();
}
void UDreamSettingsTextPreparation::BeginCataloguePreparation()
{
    bCatalogueReady=false; bWarmCatalogue=bPrewarmCategories; CatalogueFonts.Reset();
    if (!bHasPresented || !bPrewarmCategories) { BeginPreparation(); return; }
    // An already visible page stays visible and interactive. Native FText switches
    // its display strings in place; never blank the page for a cached language.
    StartedFrame=GFrameCounter; QuietFrame=0;
    SetCanExecuteTick(true);
}
void UDreamSettingsTextPreparation::RowsPresented()
{
    if (!bPrewarmCategories || (!bHasPresented && !bCatalogueReady)) BeginPreparation();
}
void UDreamSettingsTextPreparation::SetAdditionalTextRoots(const TArray<UDreamWidget*>& Roots)
{
    AdditionalTextRoots.Reset();
    for (auto* Root : Roots) if (Root) AdditionalTextRoots.Add(Root);
}
void UDreamSettingsTextPreparation::WarmCatalogue()
{
    auto* VM=FindPresenter();
    if (!VM || !GetWidget()) return;
    TArray<FText> SourceTexts=VM->GetCataloguePreparationTexts();
    SourceTexts.Append(AdditionalTexts);
    TFunction<void(UDreamWidget*)> CollectStaticTexts=[&](UDreamWidget* Node)
    {
        if (!Node) return;
        if (const auto* Text=Cast<UDreamText>(Node->GetVisual())) SourceTexts.Add(Text->GetText());
        for (auto* Child:Node->GetChildren()) CollectStaticTexts(Child);
    };
    CollectStaticTexts(GetWidget());
    for (const auto& Root : AdditionalTextRoots) CollectStaticTexts(Root.Get());
    TArray<FString> CurrentTexts;
    TSet<FTextId> TextIds;
    for (const auto& Text : SourceTexts)
    {
        CurrentTexts.AddUnique(Text.ToString());
        if (const auto* Source=FTextInspector::GetSourceString(Text)) CurrentTexts.AddUnique(*Source);
        const auto Namespace=FTextInspector::GetNamespace(Text),Key=FTextInspector::GetKey(Text);
        if (Namespace.IsSet() && Key.IsSet()) TextIds.Add(FTextId(FTextKey(*Namespace.GetValue()),FTextKey(*Key.GetValue())));
    }
    TMap<FString,FString> LanguageTexts;
    LanguageTexts.Add(FString(),FString::Join(CurrentTexts,TEXT("\n")));
    if (!bHasPresented)
    {
        // Read resources without switching the process culture or publishing text
        // revisions. Prepare the installed built-in languages before the first
        // page is revealed, including strings not on the current category.
        TArray<FString> ResourceRoots=FPaths::GetGameLocalizationPaths();
        if (const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("DreamSettings"))) ResourceRoots.AddUnique(Plugin->GetContentDir()/TEXT("Localization/DreamSettings"));
        for (const auto& Language:GetDefault<UDreamSettingsConfiguration>()->SupportedCultures)
        {
            TArray<FString> Strings=CurrentTexts;
            for (const auto& ResourceRoot : ResourceRoots)
            {
                TArray<FString> Files;
                const FString Folder=ResourceRoot/Language;
                IFileManager::Get().FindFiles(Files,*(Folder/TEXT("*.locres")),true,false);
                for (const auto& File : Files)
                {
                    FTextLocalizationResource Resource;
                    if (!Resource.LoadFromFile(Folder/File,0)) continue;
                    for (const auto& Id : TextIds)
                        if (const auto* Entry=Resource.Entries.Find(Id); Entry && Entry->LocalizedString) Strings.AddUnique(*Entry->LocalizedString);
                }
            }
            LanguageTexts.Add(Language,FString::Join(Strings,TEXT("\n")));
        }
    }
    TArray<TPair<FString,FString>> TextBatches;
    for (const auto& Language:LanguageTexts)
    {
        TArray<FString> Lines; Language.Value.ParseIntoArrayLines(Lines,false);
        FString Batch;
        for (const FString& Line:Lines)
        {
            if (!Batch.IsEmpty() && Batch.Len()+Line.Len()>1024)
            { TextBatches.Emplace(Language.Key,MoveTemp(Batch)); Batch.Reset(); }
            // Keep whole translation lines together so rich-text spans stay intact.
            Batch+=Line+TEXT("\n");
        }
        if (!Batch.IsEmpty()) TextBatches.Emplace(Language.Key,MoveTemp(Batch));
    }
    TArray<FDreamTextLayoutInput> PreparedStyles;
    TSet<UDreamUIFontData_FreeTypeRender*> Fonts;
    TFunction<void(UDreamWidget*)> Visit=[&](UDreamWidget* Node)
    {
        if (!Node) return;
        if (auto* Text=Cast<UDreamText>(Node->GetVisual()); Text && Text->GetFont())
        {
            for (const auto& Language:TextBatches)
            {
                auto Input=UDreamText::MakeLayoutInput(Text,Text->GetFontSize());
                if (!Language.Key.IsEmpty()) Input.Language=Language.Key;
                Input.Content=Language.Value;
                if (Input.TextTransform==EDreamUITextTransformPolicy::ToUpper) Input.Content=FTextTransformer::ToUpper(Input.Content);
                else if (Input.TextTransform==EDreamUITextTransformPolicy::ToLower) Input.Content=FTextTransformer::ToLower(Input.Content);
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
        }
        // Hidden scalar/key/sample controls still provide their actual typography templates.
        for (auto* Child:Node->GetChildren()) Visit(Child);
    };
    Visit(GetWidget());
    for (const auto& Root : AdditionalTextRoots) Visit(Root.Get());
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
    bHasPresented=true;
    PreparedRevision=FTextLocalizationManager::Get().GetTextRevision();
    PreparedLanguage=FInternationalization::Get().GetCurrentLanguage()->GetName();
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
    FTextLocalizationManager::Get().OnTextRevisionChangedEvent.Remove(TextRevisionHandle); TextRevisionHandle.Reset();
    Release(); Super::OnDisable();
}
void UDreamSettingsTextPreparation::OnUnregister()
{
    if (Presenter.IsValid()) { Presenter->OnDetailsOpened.RemoveAll(this); Presenter->OnRowsPresented.RemoveAll(this); Presenter->OnBenchmarkConfirmationOpened.RemoveAll(this); Presenter->OnResetConfirmationOpened.RemoveAll(this); }
    Presenter.Reset();
    FInternationalization::Get().OnCultureChanged().Remove(CultureHandle); CultureHandle.Reset();
    FTextLocalizationManager::Get().OnTextRevisionChangedEvent.Remove(TextRevisionHandle); TextRevisionHandle.Reset();
    Release(); Super::OnUnregister();
}
