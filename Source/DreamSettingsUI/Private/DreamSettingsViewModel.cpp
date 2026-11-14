#include "DreamSettingsViewModel.h"
#include "DreamSettingsSubsystem.h"
#include "DreamSettingsConfiguration.h"
#include "DreamSettingsNavigationMemory.h"
#include "DreamSettingsSubtitleViewModel.h"
#include "DreamSettingsDetails.h"
#include "DreamSettingsPresentation.h"
#include "DreamSettingsKeySelector.h"
#include "Event/DreamUIKeyRouting.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/AudioComponent.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundWaveProcedural.h"
#include "Kismet/GameplayStatics.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "DreamSettingsUI"
namespace
{
    FText DefaultLabel(const FDreamSettingDefinition& D,bool Percent)
    {
        const auto& V=D.Default;
        if (D.Kind==EDreamSettingKind::Toggle) return V.Number!=0 ? LOCTEXT("On","On") : LOCTEXT("Off","Off");
        if (D.Kind==EDreamSettingKind::Key)
        {
            const FKey Key(FName(*V.Token));
            return Key.IsValid() ? Key.GetDisplayName() : LOCTEXT("DefaultUnbound","Unbound");
        }
        if (V.bToken)
        {
            const auto Options=D.GetOptions();
            if (const auto* O=Options.FindByPredicate([&](const auto& Option){ return Option.Id==V.Token; })) return O->Label;
            return FText::FromString(V.Token);
        }
        if (const auto* Special=D.SpecialValues.Find(V.Number)) return *Special;
        FNumberFormattingOptions Format; Format.SetMaximumFractionalDigits(6);
        return Percent ? FText::AsPercent(V.Number,&Format) : FText::AsNumber(V.Number,&Format);
    }
    /**
     * The registry owns tab metadata; these names only translate the categories this plugin ships so their
     * existing msgctxt history keeps resolving. A host category arrives already labeled and never reaches here.
     */
    FText BuiltInCategoryLabel(FName Id)
    {
        if (Id == TEXT("Display")) return LOCTEXT("DisplayCategory", "Display");
        if (Id == TEXT("Graphics")) return LOCTEXT("GraphicsCategory", "Graphics");
        if (Id == TEXT("Audio")) return LOCTEXT("AudioCategory", "Audio");
        if (Id == TEXT("Input")) return LOCTEXT("InputCategory", "Controls");
        if (Id == TEXT("Gameplay")) return LOCTEXT("GameplayCategory", "Gameplay");
        if (Id == TEXT("KeyboardKeys")) return LOCTEXT("KeyboardKeysCategory", "Keyboard / mouse");
        if (Id == TEXT("GamepadKeys")) return LOCTEXT("GamepadKeysCategory", "Gamepad buttons");
        if (Id == TEXT("Accessibility")) return LOCTEXT("AccessibilityCategory", "Accessibility");
        if (Id == TEXT("Localization")) return LOCTEXT("LanguageCategory", "Language");
        return FText::FromName(Id);
    }
    FText ResolveCategoryLabel(const FDreamSettingCategory& Category)
    {
        return Category.Label.IsEmpty() ? BuiltInCategoryLabel(Category.Id) : Category.Label;
    }
}
void UDreamSettingRowViewModel::Initialize(TSharedPtr<FDreamSettingsSession> InSession, FName InId, UDreamSettingsViewModel* InOwner)
{
    Session = InSession; Id = InId; NavigationId=InId; Owner=InOwner; Refresh();
}
void UDreamSettingRowViewModel::Refresh()
{
    auto S = Session.Pin();
    if (!S) return;
    const auto* D = S->GetRegistry().Find(Id);
    const auto& Value = S->GetPending().FindChecked(Id);
    const auto Availability = S->Availability(Id);
    if (!FMath::IsNearlyEqual(static_cast<double>(Number),Value.Number,1.e-6)) NumericError=FText::GetEmpty();
    DREAM_VM_SET(Label, D->Label);
    DREAM_VM_SET(Description, Availability.IsOk() ? (!NumericError.IsEmpty() ? NumericError : (D->DynamicDescription ? D->DynamicDescription(S->GetPending()) : D->Description)) : Availability.Message);
    DREAM_VM_SET(Enabled, S->IsOpen() && !S->IsAwaitingConfirmation() && !S->IsBusy() && Availability.IsOk() && (!Owner.IsValid() || Owner->CanEditRow(Id)));
    DREAM_VM_SET(IsScalar, D->Kind == EDreamSettingKind::Scalar);
    DREAM_VM_SET(IsKey, D->Kind == EDreamSettingKind::Key);
    DREAM_VM_SET(IsOrdinary,!IsKey);
    DREAM_VM_SET(IsDiscrete,!IsKey && !IsScalar);
    const auto* Family=S->GetPending().Find(TEXT("Input.GamepadIcons"));
    DREAM_VM_SET(IconFamily,Family && Family->bToken ? Family->Token : FString(TEXT("xbox")));
    if (IsKey) DREAM_VM_SET(SelectedKey,FKey(FName(*Value.Token)));
    // Declared presentation wins; the ID checks are the fallback for definitions authored before the flags existed.
    const bool bAudioSample=EnumHasAnyFlags(D->Presentation,EDreamSettingPresentation::AudioSample)
        || (D->Presentation==EDreamSettingPresentation::None && D->Category==TEXT("Audio") && D->Kind==EDreamSettingKind::Scalar);
    const bool bSubtitleSample=EnumHasAnyFlags(D->Presentation,EDreamSettingPresentation::SubtitleSample)
        || (D->Presentation==EDreamSettingPresentation::None && (Id==TEXT("Accessibility.Subtitles") || Id==TEXT("Accessibility.SubtitleSize")));
    DREAM_VM_SET(HasAudioSample,bAudioSample && !bSubtitleSample);
    DREAM_VM_SET(HasSubtitleSample,bSubtitleSample);
    DREAM_VM_SET(HasSample,HasAudioSample || HasSubtitleSample);
    const bool bHDRCalibration=EnumHasAnyFlags(D->Presentation,EDreamSettingPresentation::HDRCalibration)
        || (D->Presentation==EDreamSettingPresentation::None && (Id==TEXT("Display.HDRPeakNits") || Id==TEXT("Display.HDRPaperWhite")));
    DREAM_VM_SET(IsHDRPeak,EnumHasAnyFlags(D->Presentation,EDreamSettingPresentation::HDRPeak) || Id==TEXT("Display.HDRPeakNits"));
    DREAM_VM_SET(HasHDRPreview,Enabled && bHDRCalibration);
    DREAM_VM_SET(HDRInstructions,IsHDRPeak
        ? LOCTEXT("HDRPeakInstructions","Increase peak brightness until the cross just blends into the bright patch. Display tone mapping can affect this comparison; use the display's rated peak if the cross never disappears. Apply saves the result; Revert restores your previous settings.")
        : LOCTEXT("HDRWhiteInstructions","Adjust until the white patch feels comfortable for ordinary surfaces and text. The surrounding grey stays proportional to white. Peak highlights are adjusted separately. Apply saves the result; Revert restores your previous settings."));
    DREAM_VM_SET(SampleLabel,HasSubtitleSample ? LOCTEXT("SubtitlePreviewAction","Preview") : LOCTEXT("AudioListenAction","Listen"));
    DREAM_VM_SET(Minimum, static_cast<float>(D->Min));
    DREAM_VM_SET(Maximum, static_cast<float>(D->Max));
    DREAM_VM_SET(Step, static_cast<float>(D->Step));
    DREAM_VM_SET(Number, static_cast<float>(Value.Number));
    FNumberFormattingOptions InputFormat;
    InputFormat.SetUseGrouping(false).SetMaximumFractionalDigits(6);
    const double DisplayScale=NumberDisplayScale();
    DREAM_VM_SET(NumericText,FText::AsNumber(Value.Number*DisplayScale,&InputFormat,FInternationalization::Get().GetInvariantCulture()).ToString()+(DisplayScale==100 ? TEXT("%") : TEXT("")));
    FText Text;
    if (D->Kind == EDreamSettingKind::Toggle) Text = Value.Number != 0 ? LOCTEXT("On", "On") : LOCTEXT("Off", "Off");
    else if (Value.bToken)
    {
        const auto Options=D->GetOptions();
        const auto* Option = Options.FindByPredicate([&](const auto& O) { return O.Id == Value.Token; });
        Text = Option ? Option->Label : FText::FromString(Value.Token);
    }
    else if (const auto* Special = D->SpecialValues.Find(Value.Number)) Text = *Special;
    else
    {
        FNumberFormattingOptions Format; Format.SetMaximumFractionalDigits(2);
        Text = NumberDisplayScale()==100 ? FText::AsPercent(Value.Number) : FText::AsNumber(Value.Number, &Format);
    }
    DREAM_VM_SET(ValueText, Text);
    DREAM_VM_SET(DefaultValueText,DefaultLabel(*D,NumberDisplayScale()==100));
    DREAM_VM_SET(ResetHint,FText::Format(LOCTEXT("ResetSettingHint","Reset this setting to {0}."),DefaultValueText));
}
void UDreamSettingRowViewModel::SetNumber(float Value)
{
    if (Enabled) if (auto S = Session.Pin())
    {
        NumericError=FText::GetEmpty();
        if (!S->Stage(Id, Value).IsOk()) BroadcastFieldValueChanged(ThisClass::FFieldNotificationClassDescriptor::Number);
    }
}
double UDreamSettingRowViewModel::NumberDisplayScale() const
{
    if (auto S=Session.Pin()) if (const auto* D=S->GetRegistry().Find(Id))
        return EnumHasAnyFlags(D->Presentation,EDreamSettingPresentation::Percent)
            || (D->Presentation==EDreamSettingPresentation::None && (HasAudioSample || Id==TEXT("Accessibility.UIScale"))) ? 100.0 : 1.0;
    return HasAudioSample || Id==TEXT("Accessibility.UIScale") ? 100.0 : 1.0;
}

void UDreamSettingRowViewModel::CommitNumberText(const FString& Input)
{
    auto S=Session.Pin();
    if (!S || !Enabled || !IsScalar) return;
    FString Text=Input.TrimStartAndEnd();
    // Common full-width IME digits/punctuation have an unambiguous ASCII equivalent.
    for (TCHAR& C:Text)
        if ((C>=0xff10 && C<=0xff19) || C==0xff0e || C==0xff0b || C==0xff0d || C==0xff05) C-=0xfee0;
    if (NumberDisplayScale()==100 && Text.EndsWith(TEXT("%")))
    { Text.LeftChopInline(1); Text.TrimEndInline(); }
    bool Valid=!Text.IsEmpty(),HasDigit=false,HasPoint=false;
    for (int32 I=0;I<Text.Len();++I)
    {
        const TCHAR C=Text[I];
        if (C>='0' && C<='9') HasDigit=true;
        else if (C=='.' && !HasPoint) HasPoint=true;
        else if ((C=='+' || C=='-') && I==0) {}
        else Valid=false;
    }
    double Parsed=0;
    Valid=Valid && HasDigit && LexTryParseString(Parsed,*Text) && FMath::IsFinite(Parsed);
    NumericError=FText::GetEmpty();
    if (!Valid)
        NumericError=LOCTEXT("InvalidNumericEntry","Enter a valid number using '.' for decimals. The current value was kept.");
    else
    {
        const auto Result=S->Stage(Id,Parsed/NumberDisplayScale());
        if (!Result.IsOk()) NumericError=FText::Format(LOCTEXT("RejectedNumericEntry","{0} The current value was kept."),Result.Message);
    }
    Refresh();
    // A rejected or unchanged value still has to replace the field's local edit.
    BroadcastFieldValueChanged(ThisClass::FFieldNotificationClassDescriptor::NumericText);
}
void UDreamSettingRowViewModel::Advance(int32 Direction)
{
    auto S = Session.Pin();
    if (!S || !Enabled) return;
    NumericError=FText::GetEmpty();
    const auto* D = S->GetRegistry().Find(Id);
    const auto& Current = S->GetPending().FindChecked(Id);
    if (D->Kind == EDreamSettingKind::Choice)
    {
        const auto Options=D->GetOptions();
        int32 Index = Options.IndexOfByPredicate([&](const auto& O) { return O.Id == Current.Token; });
        if (Index==INDEX_NONE && Direction<0) Index=0;
        for (int32 Offset = 1; Offset <= Options.Num(); ++Offset)
        {
            const auto& Option = Options[(Index + Direction * Offset + 2 * Options.Num()) % Options.Num()];
            if (Option.bSelectable && (!D->OptionAvailability || D->OptionAvailability(Option.Id,S->GetPending()).IsOk()))
            { S->Stage(Id, FDreamSettingValue::Id(Option.Id)); break; }
        }
    }
    else if (D->Kind == EDreamSettingKind::Toggle) S->Stage(Id, Current.Number != 0 ? 0 : 1);
    else if (D->Kind == EDreamSettingKind::Scalar)
    {
        double Value = FMath::Clamp(Current.Number + Direction * D->Step, D->Min, D->Max);
        if (Direction < 0 && Current.Number == D->Min)
            for (const auto& Pair : D->SpecialValues) if (Pair.Key < D->Min) Value = Pair.Key;
        S->Stage(Id,Value);
    }
}
void UDreamSettingRowViewModel::Previous() { Advance(-1); }
void UDreamSettingRowViewModel::Next() { Advance(1); }
void UDreamSettingRowViewModel::Reset() { if (Enabled) if (auto S = Session.Pin()) { NumericError=FText::GetEmpty(); S->ResetOne(Id); } }
void UDreamSettingRowViewModel::PlaySample()
{ if (Enabled && Owner.IsValid()) { if (HasSubtitleSample) Owner->PreviewSubtitles(); else Owner->PlayTestSoundForSetting(Id); } }
void UDreamSettingRowViewModel::SetKey(FKey Value)
{
    const auto S=Session.Pin();
    if (!S || !IsKey || S->GetPending().FindChecked(Id).Token==Value.GetFName().ToString()) return;
    if (Enabled && Owner.IsValid()) Owner->ChooseKey(Id,Value);
    // A rejected capture still changed the selector's local value. Force the binding back even
    // when the view-model field itself did not change. Programmatic selection echoes are ignored above.
    Refresh(); BroadcastFieldValueChanged(ThisClass::FFieldNotificationClassDescriptor::SelectedKey);
}
void UDreamSettingRowViewModel::SetListening(bool Value) { if (Owner.IsValid()) Owner->CaptureChanged(Id,Value); }
void UDreamSettingRowViewModel::ClearKey() { if (Enabled && Owner.IsValid() && Owner->ActionsEnabled) SetKey(EKeys::Invalid); }
void UDreamSettingsCategoryViewModel::Select() { if (Owner.IsValid()) Owner->SelectCategory(Id); }

bool UDreamSettingsViewModel::Initialize(ULocalPlayer* Player)
{
	Shutdown();
	if (!Player || !Player->GetGameInstance()) return false;
	NavigationMemory=Player->GetSubsystem<UDreamSettingsNavigationMemory>();
	if (!Presentation) Presentation=NewObject<UDreamSettingsPresentation>(this);
	DREAM_VM_SET(Details,NewObject<UDreamSettingsDetails>(this));
	DREAM_VM_SET(Subtitles,NewObject<UDreamSettingsSubtitleViewModel>(this)); Subtitles->Initialize(Player);
	FDreamSettingsResult Result;
	Session = Player->GetGameInstance()->GetSubsystem<UDreamSettingsSubsystem>()->OpenSession(Player, Result);
	if (!Session)
	{
		DREAM_VM_SET(Status, Result.Message);
		return false;
	}
	Session->OnChanged.AddUObject(this, &UDreamSettingsViewModel::Refresh);
	TArray<FName> Order;
	Session->GetRegistry().Validate(Order);
	Order.Sort([&](FName A, FName B) { return Session->GetRegistry().Find(A)->Order < Session->GetRegistry().Find(B)->Order; });
	TArray<TObjectPtr<UDreamSettingsCategoryViewModel>> NewCategories;
	for (FName Id : Order)
	{
		if (Session->GetRegistry().Find(Id)->bHidden) continue;
		auto* Row = NewObject<UDreamSettingRowViewModel>(this);
		Row->Initialize(Session, Id,this); AllRows.Add(Row);
		const FName Category = Session->GetRegistry().Find(Id)->Category;
		if (!NewCategories.ContainsByPredicate([&](const auto& C) { return C->Id == Category; }))
		{
			auto* C = NewObject<UDreamSettingsCategoryViewModel>(this); C->Id = Category; C->Owner = this; C->Label = CategoryPresentation(Category);
			NewCategories.Add(C);
		}
	}
	// A host that restricts the page keeps only its categories, in the host's order, after dropping empty ones.
	if (!VisibleCategories.IsEmpty())
	{
		TArray<TObjectPtr<UDreamSettingsCategoryViewModel>> Restricted;
		for (FName Allowed : VisibleCategories)
			if (auto* Found = NewCategories.FindByPredicate([&](const auto& C) { return C->Id == Allowed; }))
				Restricted.Add(*Found);
		NewCategories = MoveTemp(Restricted);
	}
	DREAM_VM_SET(Categories, NewCategories);
	// A restricted page opens at the host's chosen tab; a full page may resume the player's last visit.
	const FName Home = NewCategories.ContainsByPredicate([&](const auto& C){ return C->Id==InitialCategory; })
		? InitialCategory : (NewCategories.IsEmpty() ? NAME_None : NewCategories[0]->Id);
	const FName Previous = NavigationMemory.IsValid() && VisibleCategories.IsEmpty() ? NavigationMemory->Category : Home;
	SelectCategory(Categories.ContainsByPredicate([&](const auto& C){ return C->Id==Previous; }) ? Previous : Home);
	Refresh();
	return true;
}

void UDreamSettingsViewModel::Shutdown()
{
	OnNavigationChanging.Broadcast();
	if (Details) Details->Reset();
	DREAM_VM_SET(Details,TObjectPtr<UDreamSettingsDetails>());
	DREAM_VM_SET(ShowDetails,false); DetailsSetting=NAME_None;
	if (Subtitles) Subtitles->Shutdown();
	DREAM_VM_SET(Subtitles,TObjectPtr<UDreamSettingsSubtitleViewModel>());
	CapturingRow=ConflictRow=NAME_None; ConflictSnapshot.Reset(); ConflictKey.Reset();
	DREAM_VM_SET(ConfirmKeyConflict,false); DREAM_VM_SET(ActionsEnabled,false);
	DREAM_VM_SET(ConfirmReset,false); ResetSetting=NAME_None;
	DREAM_VM_SET(ConfirmGameBenchmark,false); DREAM_VM_SET(ShowGameBenchmark,false); DREAM_VM_SET(CanStartGameBenchmark,false);
	if (IsValid(PreviewAudio)) PreviewAudio->Stop();
	PreviewAudio = nullptr;
	PreviewTone = nullptr;
	if (Session)
	{
		Session->OnChanged.RemoveAll(this);
		Session->Close();
	}
	Session.Reset(); // Session destructor rolls back uncommitted previews.
	AllRows.Reset();
	DREAM_VM_SET(Rows, TArray<TObjectPtr<UDreamSettingRowViewModel>>());
	DREAM_VM_SET(Categories, TArray<TObjectPtr<UDreamSettingsCategoryViewModel>>());
	bCloseAfterDisplay = bCloseAfterOperation = false;
	DREAM_VM_SET(Ready, false);
	DREAM_VM_SET(PageEnabled, false);
	ScenePreviewOverride.Reset(); RefreshScenePreview();
}

FText UDreamSettingsViewModel::CategoryPresentation(FName Category) const
{
    if (Session)
        for (const auto& Known : Session->GetRegistry().Categories())
            if (Known.Id == Category) return ResolveCategoryLabel(Known);
    return BuiltInCategoryLabel(Category);
}

void UDreamSettingsViewModel::ConfigureVisibleCategories(const TArray<FName>& InCategories, FName InInitialCategory)
{
    VisibleCategories = InCategories;
    InitialCategory = InInitialCategory;
}

void UDreamSettingsViewModel::SelectCategory(FName Category)
{
    if (!Session || ShowDetails || ConfirmDisplay || ConfirmClose || ConfirmKeyConflict || ConfirmGameBenchmark || ConfirmReset || !CapturingRow.IsNone()) return;
    if (!Categories.ContainsByPredicate([&](const auto& C){ return C->Id==Category; })) return;
    if (CurrentCategory!=Category) { OnNavigationChanging.Broadcast(); ScenePreviewOverride.Reset(); }
    CurrentCategory=Category;
    for (const auto& Item:Categories) Item->SetSelected(Item->Id==Category);
    if (NavigationMemory.IsValid()) NavigationMemory->Category=Category;
    TArray<TObjectPtr<UDreamSettingRowViewModel>> Selected;
    for (auto Row : AllRows) if (Session->GetRegistry().Find(Row->GetSettingId())->Category == Category) Selected.Add(Row);
    GroupInputRows(Selected);
    const bool NewText=Rows!=Selected || bInputGroupingDirty; bInputGroupingDirty=false;
    DREAM_VM_SET(Rows, Selected);
    DREAM_VM_SET(CategoryTitle, CategoryPresentation(Category));
    DREAM_VM_SET(ShowColourSamples,Category==TEXT("Accessibility"));
    DREAM_VM_SET(ShowDisplayInformation,Category==TEXT("Display"));
    RefreshGameBenchmark();
    RefreshScenePreview();
    DetailsSetting=Selected.IsEmpty() ? NAME_None : Selected[0]->GetSettingId();
    RefreshDetails(); if (NewText) OnRowsPresented.Broadcast();
}

void UDreamSettingsViewModel::BeginDestroy()
{
	Shutdown();
	Super::BeginDestroy();
}

void UDreamSettingsViewModel::Refresh()
{
	if (!Session) return;
    if (auto* Player=NavigationMemory.IsValid() ? NavigationMemory->GetLocalPlayer() : nullptr)
        if (auto* System=Player->GetGameInstance()->GetSubsystem<UDreamSettingsSubsystem>())
            DREAM_VM_SET(DisplayInformation,UDreamSettingsDisplayLibrary::Describe(System->GetDisplayCapabilities()));
    if (bCloseAfterOperation && !Session->IsBusy())
    {
        bCloseAfterOperation=false;
        if (Session->GetLastResult().IsOk())
        {
            if (Session->IsAwaitingConfirmation()) bCloseAfterDisplay=true;
            else { Close(); return; }
        }
    }
	const auto& Values = Session->GetPending();
	DREAM_VM_SET(VSync, Values.FindRef(DreamSettingIds::VSync).Number != 0);
	DREAM_VM_SET(MasterVolume, static_cast<float>(Values.FindRef(DreamSettingIds::MasterVolume).Number));
	DREAM_VM_SET(VolumeText, FText::AsPercent(MasterVolume));
	DREAM_VM_SET(Dirty, Session->IsDirty());
	DREAM_VM_SET(Ready, Session->IsOpen());
	DREAM_VM_SET(ConfirmDisplay, Session->IsAwaitingConfirmation());
	DREAM_VM_SET(PageEnabled, Ready && !PreparingText && !Session->IsBusy() && !ShowDetails && !ConfirmDisplay && !ConfirmClose && !ConfirmKeyConflict && !ConfirmGameBenchmark && !ConfirmReset);
	DREAM_VM_SET(ActionsEnabled,PageEnabled && CapturingRow.IsNone());
    if (bInputGroupingDirty && ActionsEnabled) { OnNavigationChanging.Broadcast(); SelectCategory(CurrentCategory); }
	RefreshGameBenchmark();
    RefreshMenuShortcutHint();
	RefreshScenePreview();
	DREAM_VM_SET(DisplayPrompt, FText::Format(LOCTEXT("KeepDisplayPrompt", "Keep these display settings? Reverting in {0} seconds."), FText::AsNumber(FMath::CeilToInt(Session->GetConfirmationSecondsLeft()))));
	for (auto Row : AllRows) Row->Refresh();
	RefreshDetails();
	const auto& Result = Session->GetLastResult();
	DREAM_VM_SET(Status, Result.IsOk() ? (Dirty ? LOCTEXT("Pending", "Unsaved changes") : LOCTEXT("Current", "Settings are up to date")) : Result.Message);
}

void UDreamSettingsViewModel::SetVSync(bool Value) { if (Session && ActionsEnabled) Session->Stage(DreamSettingIds::VSync, Value ? 1 : 0); }
void UDreamSettingsViewModel::ConfigureScenePreview(bool Allowed,bool AutomaticForGraphics)
{
    bAllowScenePreview=Allowed; bAutomaticGraphicsPreview=AutomaticForGraphics;
    ScenePreviewOverride.Reset(); RefreshScenePreview();
}
void UDreamSettingsViewModel::RefreshScenePreview()
{
    const bool Graphics=CurrentCategory==TEXT("Graphics");
    DREAM_VM_SET(ScenePreviewAvailable,Ready && bAllowScenePreview && (Graphics || CurrentCategory==TEXT("Display")));
    const bool Requested=ScenePreviewOverride.Get(bAutomaticGraphicsPreview && Graphics);
    // Modal and loading surfaces always regain an opaque backdrop. The user's
    // current-page choice resumes after the modal without touching settings.
    DREAM_VM_SET(ScenePreviewActive,ScenePreviewAvailable && Requested && ActionsEnabled);
    DREAM_VM_SET(BackgroundOpacity,ScenePreviewActive ? 0.f : 1.f);
    // Keep the intended action stable while loading/modals temporarily suspend
    // compositing, so the preparation pass warms the text that will actually show.
    DREAM_VM_SET(ScenePreviewLabel,ScenePreviewAvailable && Requested ? LOCTEXT("HideScenePreview","Hide scene") : LOCTEXT("ShowScenePreview","Show scene"));
}
void UDreamSettingsViewModel::ToggleScenePreview()
{
    if (!ActionsEnabled || !ScenePreviewAvailable) return;
    ScenePreviewOverride=!ScenePreviewActive; RefreshScenePreview();
}
bool UDreamSettingsViewModel::AllowsDescriptionAutoScroll() const
{
    return bAutoScrollDescriptions && CurrentCategory!=TEXT("Input") && CurrentCategory!=TEXT("KeyboardKeys") && CurrentCategory!=TEXT("GamepadKeys");
}
void UDreamSettingsViewModel::ConfigureInputGrouping(EDreamSettingsInputGrouping Mode,const TMap<FSoftObjectPath,FText>& ContextLabels)
{
    InputGrouping=Mode; InputContextLabels=ContextLabels; bInputGroupingDirty=true;
    if (Session && !Session->IsBusy() && !ShowDetails && !ConfirmClose && !ConfirmDisplay && !ConfirmKeyConflict && !ConfirmGameBenchmark && !ConfirmReset && CapturingRow.IsNone())
    { OnNavigationChanging.Broadcast(); SelectCategory(CurrentCategory); }
}
void UDreamSettingsViewModel::GroupInputRows(TArray<TObjectPtr<UDreamSettingRowViewModel>>& Selected)
{
    for (auto Row:Selected) Row->SetGroupHeading(FText::GetEmpty(),false);
    if (InputGrouping==EDreamSettingsInputGrouping::None || Selected.IsEmpty() ||
        Selected.ContainsByPredicate([](const auto& Row) { return !Row->IsKey; })) return;
    struct FGroup { FString Id; FText Title; };
    TMap<FName,FGroup> Groups;
    const FText Other=LOCTEXT("OtherInputBindings","Other bindings");
    for (auto Row:Selected)
    {
        const auto* D=Session->GetRegistry().Find(Row->GetSettingId()); FGroup Group;
        if (InputGrouping==EDreamSettingsInputGrouping::DisplayCategory)
        { Group.Id=D->InputCategoryId; Group.Title=D->InputCategoryLabel; }
        else if (InputGrouping==EDreamSettingsInputGrouping::MappingContext)
        {
            TArray<FString> Ids; TArray<FText> Labels;
            for (const auto& Context:D->InputContexts)
            {
                Ids.Add(Context.Path.ToString());
                const auto* Override=InputContextLabels.Find(Context.Path);
                FText Label=Override && !Override->IsEmpty() ? *Override : Context.Label;
                Labels.Add(Label.IsEmpty() ? Other : Label);
            }
            Group.Id=FString::Join(Ids,TEXT("|"));
            Group.Title=FText::Join(FText::AsCultureInvariant(TEXT(" / ")),Labels);
        }
        if (Group.Id.IsEmpty()) { Group.Id=TEXT("~other"); Group.Title=Other; }
        if (Group.Title.IsEmpty()) Group.Title=Other;
        Groups.Add(Row->GetSettingId(),MoveTemp(Group));
    }
    Selected.StableSort([&](const auto& A,const auto& B) { return Groups.FindChecked(A.GetSettingId()).Id<Groups.FindChecked(B.GetSettingId()).Id; });
    FString Previous;
    for (auto Row:Selected)
    {
        const auto& Group=Groups.FindChecked(Row->GetSettingId());
        Row->SetGroupHeading(Group.Title,Previous!=Group.Id); Previous=Group.Id;
    }
}
void UDreamSettingsViewModel::SetPreparingText(bool Value)
{ if (PreparingText!=Value) { DREAM_VM_SET(PreparingText,Value); Refresh(); } }
void UDreamSettingsViewModel::ConfigureMenuShortcuts(const FDreamSettingsMenuShortcuts& Value)
{
    MenuShortcuts=Value;
    TSet<FKey> Used;
    // Follow the host/platform's effective navigation contract, including custom Back keys.
    for (FKey Key:DreamUIKeyRouting::GetConfirmKeys()) Used.Add(Key);
    for (FKey Key:DreamUIKeyRouting::GetBackKeys()) Used.Add(Key);
    for (const auto& Key:DreamUIKeyRouting::GetDirectionKeys()) Used.Add(Key.Key);
    for (const auto& Key:DreamUIKeyRouting::GetPageKeys()) Used.Add(Key.Key);
    for (const auto& Key:DreamUIKeyRouting::GetExtentKeys()) Used.Add(Key.Key);
    for (auto* Keys:{&MenuShortcuts.PreviousCategory,&MenuShortcuts.NextCategory,&MenuShortcuts.Apply,&MenuShortcuts.Reset})
        Keys->RemoveAll([&](FKey Key)
        {
            if (!Key.IsValid() || Key.IsMouseButton() || Key.IsTouch() || Key.IsAnalog() || Key.IsModifierKey() || Used.Contains(Key)) return true;
            Used.Add(Key); return false;
        });
    RefreshMenuShortcutHint();
}
void UDreamSettingsViewModel::RefreshMenuShortcutHint()
{
    const FString Family=Session ? Session->GetPending().FindRef(TEXT("Input.GamepadIcons")).Token : TEXT("xbox");
    TArray<FText> Parts;
    auto Add=[&](const TArray<FKey>& Keys,const FText& Action)
    {
        TArray<FText> Labels;
        for (FKey Key:Keys) Labels.Add(UDreamSettingsKeySelector::ButtonLabelForFamily(Key,Family));
        if (!Labels.IsEmpty()) Parts.Add(FText::Format(LOCTEXT("MenuShortcutPart","{0}: {1}"),Action,FText::Join(FText::AsCultureInvariant(TEXT(" / ")),Labels)));
    };
    Add(MenuShortcuts.PreviousCategory,LOCTEXT("PreviousCategoryShortcut","Previous category"));
    Add(MenuShortcuts.NextCategory,LOCTEXT("NextCategoryShortcut","Next category"));
    Add(MenuShortcuts.Apply,LOCTEXT("ApplyShortcut","Apply"));
    Add(MenuShortcuts.Reset,LOCTEXT("ResetShortcut","Reset"));
    DREAM_VM_SET(MenuShortcutHint,FText::Join(FText::AsCultureInvariant(TEXT("    ")),Parts));
}
bool UDreamSettingsViewModel::HandleMenuShortcut(FKey Key,bool Repeat)
{
    if (!ActionsEnabled || !Session) return false;
    const bool Previous=MenuShortcuts.PreviousCategory.Contains(Key),Next=MenuShortcuts.NextCategory.Contains(Key);
    const bool ApplyKey=MenuShortcuts.Apply.Contains(Key),ResetKey=MenuShortcuts.Reset.Contains(Key);
    if (!Previous && !Next && !ApplyKey && !ResetKey) return false;
    if (Repeat) return true;
    if (Previous || Next)
    {
        const int32 Index=Categories.IndexOfByPredicate([&](const auto& C){ return C->Id==CurrentCategory; });
        if (Index!=INDEX_NONE && Categories.Num()>1) SelectCategory(Categories[(Index+(Previous ? Categories.Num()-1 : 1))%Categories.Num()]->Id);
    }
    else if (ApplyKey) { if (Dirty) Apply(); }
    else RequestResetOptions();
    return true;
}
void UDreamSettingsViewModel::RequestResetOptions()
{
    if (!ActionsEnabled || !Session) return;
    const auto* Definition=Session->GetRegistry().Find(DetailsSetting);
    ResetSetting=Definition && Definition->Category==CurrentCategory ? DetailsSetting : NAME_None;
    DREAM_VM_SET(CanResetSelected,!ResetSetting.IsNone() && Session->Availability(ResetSetting).IsOk());
    DREAM_VM_SET(ResetSelectedLabel,Definition ? FText::Format(LOCTEXT("ResetSelectedNamed","Reset {0}"),Definition->Label) : LOCTEXT("ResetSelected","Reset selected setting"));
    DREAM_VM_SET(ResetCategoryLabel,FText::Format(LOCTEXT("ResetCategoryNamed","Reset category: {0}"),CategoryPresentation(CurrentCategory)));
    DREAM_VM_SET(ConfirmReset,true); Refresh(); OnResetConfirmationOpened.Broadcast();
}
void UDreamSettingsViewModel::CancelResetOptions()
{ if (ConfirmReset) { DREAM_VM_SET(ConfirmReset,false); ResetSetting=NAME_None; Refresh(); } }
void UDreamSettingsViewModel::ConfirmResetSelected()
{
    if (!ConfirmReset || !CanResetSelected || PreparingText || !Session || Session->IsBusy()) return;
    const FName Id=ResetSetting; CancelResetOptions(); Session->ResetOne(Id);
}
void UDreamSettingsViewModel::ConfirmResetCategory()
{
    if (!ConfirmReset || PreparingText || !Session || Session->IsBusy()) return;
    const FName Category=CurrentCategory; CancelResetOptions(); Session->ResetCategory(Category);
}
void UDreamSettingsViewModel::SetMasterVolume(float Value) { if (Session && ActionsEnabled) Session->Stage(DreamSettingIds::MasterVolume, Value); }
void UDreamSettingsViewModel::Apply() { if (Session && ActionsEnabled) Session->Apply(); }
void UDreamSettingsViewModel::Revert() { if (Session && ActionsEnabled) Session->Revert(); }
void UDreamSettingsViewModel::ResetDefaults() { if (Session && ActionsEnabled) Session->Reset(); }
void UDreamSettingsViewModel::ResetCurrentCategory() { if (Session && ActionsEnabled) Session->ResetCategory(CurrentCategory); }
void UDreamSettingsViewModel::RequestClose()
{
	if (ConfirmReset) { CancelResetOptions(); return; }
	if (ConfirmGameBenchmark) { CancelGameBenchmark(); return; }
	if (ShowDetails) { CloseDetails(); return; }
	if (ConfirmKeyConflict) { CancelKeyConflict(); return; }
	if (ConfirmClose) { KeepEditing(); return; }
	if (ConfirmDisplay || !CapturingRow.IsNone() || (Session && Session->IsBusy())) return;
	if (Session && Session->IsDirty()) DREAM_VM_SET(ConfirmClose, true);
	else Close();
	Refresh();
}
void UDreamSettingsViewModel::KeepEditing() { DREAM_VM_SET(ConfirmClose, false); Refresh(); }
void UDreamSettingsViewModel::DiscardAndClose()
{
    if (!Session) return;
    const auto Result=Session->Revert();
    if (Result.IsOk()) Close();
    else if (Session->IsBusy()) { bCloseAfterOperation=true; DREAM_VM_SET(ConfirmClose,false); Refresh(); }
}
void UDreamSettingsViewModel::ApplyAndClose()
{
    if (!Session) return;
    const auto Result=Session->Apply();
    if (Session->IsBusy()) { bCloseAfterOperation=true; DREAM_VM_SET(ConfirmClose,false); Refresh(); return; }
    if (!Result.IsOk()) return;
    if (Session->IsAwaitingConfirmation()) { bCloseAfterDisplay = true; DREAM_VM_SET(ConfirmClose, false); Refresh(); }
    else Close();
}
void UDreamSettingsViewModel::KeepDisplay()
{
    if (Session && Session->Confirm().IsOk() && bCloseAfterDisplay) Close();
}
void UDreamSettingsViewModel::RevertDisplay() { bCloseAfterDisplay = false; if (Session) Session->Reject(); }
void UDreamSettingsViewModel::AutoDetectGraphics()
{
    if (!ActionsEnabled || !Session || !GetWorld()) return;
    auto* Subsystem = GetWorld()->GetGameInstance()->GetSubsystem<UDreamSettingsSubsystem>();
    const auto Result = Subsystem->RecommendGraphics(*Session);
    if (Result.IsOk()) DREAM_VM_SET(Status, LOCTEXT("RecommendationDraft", "Recommended graphics settings are ready. Apply to use them, or Revert to discard."));
    else DREAM_VM_SET(Status, Result.Message);
}
void UDreamSettingsViewModel::ConfigureGameBenchmark(bool Enabled,bool EnabledInShipping,const TArray<FKey>& Keys)
{
    bEnableGameBenchmark=Enabled && (!UE_BUILD_SHIPPING || EnabledInShipping);
    GameBenchmarkKeys.Reset();
    for (FKey Key:Keys)
        if (Key.IsValid() && !Key.IsMouseButton() && !Key.IsTouch() && !Key.IsAnalog() && !Key.IsModifierKey()) GameBenchmarkKeys.AddUnique(Key);
    if (!bEnableGameBenchmark) DREAM_VM_SET(ConfirmGameBenchmark,false);
    if (Session) Refresh(); else RefreshGameBenchmark();
}
void UDreamSettingsViewModel::RefreshGameBenchmark()
{
    auto* Player=NavigationMemory.IsValid() ? NavigationMemory->GetLocalPlayer() : nullptr;
    auto* System=Player && Player->GetGameInstance() ? Player->GetGameInstance()->GetSubsystem<UDreamSettingsSubsystem>() : nullptr;
    // A configured dedicated input is available throughout the menu, like the
    // legacy benchmark footer. Without shortcuts, the action belongs to Graphics.
    const bool Dedicated=System && System->GetGameBenchmark(Player) && !GameBenchmarkKeys.IsEmpty();
    DREAM_VM_SET(ShowGameBenchmark,Ready && bEnableGameBenchmark && (CurrentCategory==TEXT("Graphics") || Dedicated));
    auto Result=System ? System->GameBenchmarkAvailability(Player) : FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("NoGameBenchmarkUI","This game has not provided a gameplay benchmark."));
    if (Result.IsOk() && Session && Session->IsDirty())
        Result=FDreamSettingsResult::Fail(EDreamSettingsError::Conflict,LOCTEXT("CleanBenchmarkUI","Apply or revert pending changes before running the game benchmark."));
    DREAM_VM_SET(CanStartGameBenchmark,ShowGameBenchmark && !PreparingText && Session && Session->IsOpen() && !Session->IsBusy() && !Session->IsAwaitingConfirmation() && Result.IsOk());
    FText Hint=Result.Message;
    if (Result.IsOk())
    {
        TArray<FString> Labels;
        const FString Family=Session ? Session->GetPending().FindRef(TEXT("Input.GamepadIcons")).Token : TEXT("xbox");
        for (FKey Key:GameBenchmarkKeys)
        {
            FText Label=UDreamSettingsKeySelector::ButtonLabelForFamily(Key,Family);
            if (Key.IsGamepadKey()) Label=FText::Format(LOCTEXT("BenchmarkControllerKey","Controller {0}"),Label);
            Labels.Add(Label.ToString());
        }
        Hint=Labels.IsEmpty() ? LOCTEXT("BenchmarkHint","Run a gameplay benchmark using applied graphics settings.")
            : FText::Format(LOCTEXT("BenchmarkShortcutHint","Run a gameplay benchmark using applied settings. Shortcuts: {0}."),FText::FromString(FString::Join(Labels,TEXT(" / "))));
    }
    DREAM_VM_SET(GameBenchmarkHint,Hint);
}
bool UDreamSettingsViewModel::IsGameBenchmarkShortcut(FKey Key) const
{ return ActionsEnabled && ShowGameBenchmark && CanStartGameBenchmark && GameBenchmarkKeys.Contains(Key); }
void UDreamSettingsViewModel::RequestGameBenchmark()
{
    if (!ActionsEnabled || !ShowGameBenchmark) return;
    RefreshGameBenchmark();
    if (!CanStartGameBenchmark) { DREAM_VM_SET(Status,GameBenchmarkHint); return; }
    DREAM_VM_SET(ConfirmGameBenchmark,true); Refresh(); OnBenchmarkConfirmationOpened.Broadcast();
}
void UDreamSettingsViewModel::CancelGameBenchmark()
{ if (ConfirmGameBenchmark) { DREAM_VM_SET(ConfirmGameBenchmark,false); Refresh(); } }
void UDreamSettingsViewModel::ConfirmGameBenchmarkLaunch()
{
    if (!ConfirmGameBenchmark || !CanStartGameBenchmark || !Session || PreparingText) return;
    auto* Player=NavigationMemory.IsValid() ? NavigationMemory->GetLocalPlayer() : nullptr;
    auto* System=Player && Player->GetGameInstance() ? Player->GetGameInstance()->GetSubsystem<UDreamSettingsSubsystem>() : nullptr;
    if (!System) { CancelGameBenchmark(); return; }
    const auto Result=System->LaunchGameBenchmark(Player,*Session);
    if (Result.IsOk()) { Close(); return; }
    DREAM_VM_SET(ConfirmGameBenchmark,false); Refresh(); DREAM_VM_SET(Status,Result.Message);
}
void UDreamSettingsViewModel::Close()
{
	DREAM_VM_SET(ConfirmClose, false);
	Shutdown();
	OnClosed.Broadcast();
}

void UDreamSettingsViewModel::PlayTestSound()
{ PlayTestSoundForSetting(DreamSettingIds::MasterVolume); }
void UDreamSettingsViewModel::PreviewSubtitles()
{
    if (!ActionsEnabled || !Subtitles) return;
    if (!Subtitles->Preview()) DREAM_VM_SET(Status,LOCTEXT("EnableSubtitlePreview","Enable subtitles to preview them."));
}

void UDreamSettingsViewModel::PlayTestSoundForSetting(FName SettingId)
{
	if (!ActionsEnabled || !GetWorld()) return;
	USoundClass* Route=GetDefault<UDreamSettingsConfiguration>()->ResolveSoundClass(SettingId);
	if (!Route || !Session || !Session->Availability(SettingId).IsOk()) return;
	if (IsValid(PreviewAudio)) PreviewAudio->Stop();
	PreviewTone = NewObject<USoundWaveProcedural>(this);
	PreviewTone->SetSampleRate(48000);
	PreviewTone->NumChannels = 1;
	PreviewTone->SoundClassObject = Route;
	TArray<int16> Samples;
	Samples.SetNumUninitialized(24000);
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const double Time = Index / 48000.0;
		const double Envelope = FMath::Min(Time / 0.015, FMath::Max(0.0, (0.5 - Time) / 0.12));
		Samples[Index] = static_cast<int16>(5000 * Envelope * FMath::Sin(2 * PI * 440 * Time));
	}
	PreviewTone->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
	PreviewAudio = UGameplayStatics::SpawnSound2D(this, PreviewTone);
	if (PreviewAudio) PreviewAudio->FadeOut(0.6f, 0);
	else DREAM_VM_SET(Status, LOCTEXT("NoAudioDevice", "Audio preview is unavailable on this device."));
}
bool UDreamSettingsViewModel::CanEditRow(FName Id) const
{ return !ShowDetails && !PreparingText && !ConfirmClose && !ConfirmDisplay && !ConfirmKeyConflict && !ConfirmGameBenchmark && !ConfirmReset && (CapturingRow.IsNone() || CapturingRow==Id); }

void UDreamSettingsViewModel::SetPresentation(UDreamSettingsPresentation* Value)
{ Presentation=Value; RefreshDetails(); }
void UDreamSettingsViewModel::SetDetailsSetting(FName Id)
{
    if (ShowDetails || DetailsSetting==Id || !Rows.ContainsByPredicate([&](const auto& Row){ return Row->GetSettingId()==Id; })) return;
    DetailsSetting=Id; RefreshDetails();
}
void UDreamSettingsViewModel::OpenDetails()
{
    if (!ActionsEnabled || DetailsSetting.IsNone()) return;
    DREAM_VM_SET(ShowDetails,true); Refresh(); OnDetailsOpened.Broadcast();
}
void UDreamSettingsViewModel::CloseDetails()
{ if (ShowDetails) { DREAM_VM_SET(ShowDetails,false); Refresh(); } }
void UDreamSettingsViewModel::RefreshDetails()
{
    if (!Session || !Details) return;
    const auto* D=Session->GetRegistry().Find(DetailsSetting);
    const auto* Row=AllRows.FindByPredicate([&](const auto& Item){ return Item->GetSettingId()==DetailsSetting; });
    if (!D || !Row) { Details->Reset(); return; }
    const auto Availability=Session->Availability(DetailsSetting);
    const FText Body=D->DynamicDescription ? D->DynamicDescription(Session->GetPending()) : D->Description;
    Details->Update(*D,Session->GetPending().FindChecked(DetailsSetting),Body,
        (*Row)->IsScalar ? FText::FromString((*Row)->NumericText) : (*Row)->ValueText,(*Row)->DefaultValueText,
        Availability.IsOk() ? (*Row)->GetValidationMessage() : Availability.Message,Presentation,ShowDetails);
}

void UDreamSettingsViewModel::CaptureChanged(FName Id,bool bListening)
{
    if (!Session) return;
    if (bListening)
    {
        if (!CanEditRow(Id)) return;
        CapturingRow=Id;
    }
    else if (CapturingRow==Id) CapturingRow=NAME_None;
    Refresh();
    if (bListening) DREAM_VM_SET(Status,LOCTEXT("CapturingKey","Press a key, mouse button or controller button. Escape / controller Menu cancels."));
}
TArray<FName> UDreamSettingsViewModel::FindKeyConflicts(FName Id,const FString& Key) const
{
    TArray<FName> Conflicts; if (!Session || Key==TEXT("None")) return Conflicts;
    const auto* Source=Session->GetRegistry().Find(Id); if (!Source) return Conflicts;
    for (const auto& Pair:Session->GetPending())
    {
        const auto* Target=Session->GetRegistry().Find(Pair.Key);
        if (Pair.Key!=Id && Target && Target->Kind==EDreamSettingKind::Key && Target->Category==Source->Category
            && Pair.Value.bToken && Pair.Value.Token==Key) Conflicts.Add(Pair.Key);
    }
    return Conflicts;
}
void UDreamSettingsViewModel::ChooseKey(FName Id,FKey Key)
{
    if (!Session || !CanEditRow(Id)) return;
    const FString Token=Key.GetFName().ToString();
    const auto Result=Session->Stage(Id,FDreamSettingValue::Id(Token));
    if (Result.Code!=EDreamSettingsError::Conflict) return;
    const auto Conflicts=FindKeyConflicts(Id,Token); if (Conflicts.IsEmpty()) return;
    ConflictRow=Id; ConflictKey=Token; ConflictSnapshot=Session->GetPending();
    TArray<FString> Labels;
    for (FName Other:Conflicts) Labels.Add(Session->GetRegistry().Find(Other)->Label.ToString());
    DREAM_VM_SET(KeyConflictPrompt,FText::Format(LOCTEXT("KeyConflictPrompt","{0} is assigned to {1}. Replace clears those bindings. Swap gives the other binding your previous key."),
        Key.GetDisplayName(),FText::FromString(FString::Join(Labels,TEXT(", ")))));
    DREAM_VM_SET(CanSwapKey,Conflicts.Num()==1);
    DREAM_VM_SET(ConfirmKeyConflict,true); Refresh();
}
void UDreamSettingsViewModel::ResolveKeyConflict(bool bSwap)
{
    if (!Session || !ConfirmKeyConflict || (bSwap && !CanSwapKey)) return;
    bool bCurrent=Session->GetPending().Num()==ConflictSnapshot.Num();
    for (const auto& Pair:ConflictSnapshot)
    { const auto* Current=Session->GetPending().Find(Pair.Key); bCurrent &= Current && Current->Equals(Pair.Value); }
    if (!bCurrent)
    { CancelKeyConflict(); DREAM_VM_SET(Status,LOCTEXT("StaleKeyConflict","Settings changed while this prompt was open. Choose the key again.")); return; }
    FDreamSettingsValues Changes;
    Changes.Add(ConflictRow,FDreamSettingValue::Id(ConflictKey));
    for (FName Other:FindKeyConflicts(ConflictRow,ConflictKey))
        Changes.Add(Other,bSwap ? ConflictSnapshot.FindChecked(ConflictRow) : FDreamSettingValue::Id(TEXT("None")));
    const auto Result=Session->StageBatch(Changes);
    if (Result.IsOk()) CancelKeyConflict();
}
void UDreamSettingsViewModel::SwapKey() { ResolveKeyConflict(true); }
void UDreamSettingsViewModel::ReplaceKey() { ResolveKeyConflict(false); }
void UDreamSettingsViewModel::CancelKeyConflict()
{
    ConflictRow=NAME_None; ConflictKey.Reset(); ConflictSnapshot.Reset();
    DREAM_VM_SET(ConfirmKeyConflict,false); DREAM_VM_SET(CanSwapKey,false); Refresh();
}
#undef LOCTEXT_NAMESPACE
