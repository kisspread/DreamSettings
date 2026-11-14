#if WITH_DEV_AUTOMATION_TESTS
#include "DreamSettingsModel.h"
#include "DreamSettingsProvider.h"
#include "DreamSettingsIniStore.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"

namespace
{
    struct FJournalStore : IDreamSettingsStore
    {
        FDreamSettingsValues Saved, Recovery;
        bool bFailJournal = false;
        int Saves = 0;
        FDreamSettingsResult Load(FDreamSettingsValues& Out) override { Out = Saved; return {}; }
        FDreamSettingsResult Save(const FDreamSettingsValues& In) override { Saved = In; Recovery.Reset(); ++Saves; return {}; }
        FDreamSettingsResult BeginRiskyChange(const FDreamSettingsValues& In) override
        {
            if (bFailJournal) return FDreamSettingsResult::Fail(EDreamSettingsError::Storage, FText::FromString(TEXT("Journal unavailable")));
            Recovery = In; return {};
        }
    };
    TSharedRef<FDreamCallbackSettingsProvider> NumberProvider(double& Number)
    {
        return MakeShared<FDreamCallbackSettingsProvider>(
            [&Number](FDreamSettingValue& Out) { Out = Number; return FDreamSettingsResult{}; },
            [&Number](const FDreamSettingValue& In) { Number = In.Number; return FDreamSettingsResult{}; });
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsConfirmationTest, "DreamSettings.Core.DisplayConfirmation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsConfirmationTest::RunTest(const FString&)
{
    auto Registry = MakeShared<FDreamSettingsRegistry>();
    auto Store = MakeShared<FJournalStore>();
    double Mode = 0;
    FDreamSettingDefinition D; D.Id = TEXT("Mode"); D.Step = 1; D.bRequiresConfirmation = true;
    Registry->Register(D, NumberProvider(Mode));
    FDreamSettingsSession Session(Registry, Store);
    TestTrue(TEXT("Open"), Session.Open().IsOk());
    Session.Stage(D.Id, 1);
    Store->bFailJournal = true;
    TestFalse(TEXT("Cannot change display without durable recovery"), Session.Apply().IsOk());
    TestEqual(TEXT("Display unchanged if journal fails"), Mode, 0.0);
    Store->bFailJournal = false;
    TestTrue(TEXT("Apply enters confirmation"), Session.Apply().IsOk());
    TestTrue(TEXT("Confirmation pending"), Session.IsAwaitingConfirmation());
    TestEqual(TEXT("Previewed display"), Mode, 1.0);
    TestEqual(TEXT("Safe state recorded"), Store->Recovery.FindChecked(D.Id).Number, 0.0);
    TestEqual(TEXT("Not committed before confirmation"), Store->Saves, 0);
    TestFalse(TEXT("Cannot edit during countdown"), Session.Stage(D.Id, 0).IsOk());
    Session.Tick(FPlatformTime::Seconds() + 16);
    TestEqual(TEXT("Real-time expiry restores previous mode"), Mode, 0.0);
    TestFalse(TEXT("Expiry ends confirmation"), Session.IsAwaitingConfirmation());
    TestFalse(TEXT("Expiry leaves clean session"), Session.IsDirty());
    TestTrue(TEXT("Successful recovery removes journal"), Store->Recovery.IsEmpty());
    Session.Stage(D.Id, 1);
    Session.Apply();
    TestTrue(TEXT("Confirm commits"), Session.Confirm().IsOk());
    TestEqual(TEXT("Confirmed mode persisted"), Store->Saved.FindChecked(D.Id).Number, 1.0);
    TestFalse(TEXT("Confirmed clean"), Session.IsDirty());
    Session.Stage(D.Id, 0);
    Session.Apply();
    TestTrue(TEXT("Closing rejects pending display"), Session.Close().IsOk());
    TestEqual(TEXT("Closing returns to confirmed mode"), Mode, 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsGroupTest, "DreamSettings.Core.GroupSideEffects", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsGroupTest::RunTest(const FString&)
{
    auto Registry = MakeShared<FDreamSettingsRegistry>();
    auto Store = MakeShared<FJournalStore>();
    double Group = 0, Detail = 0.2;
    FDreamSettingDefinition D; D.Id = TEXT("Group"); D.Step = 1; D.bRequiresConfirmation = true;
    Registry->Register(D, MakeShared<FDreamCallbackSettingsProvider>(
        [&](FDreamSettingValue& Out) { Out = Group; return FDreamSettingsResult{}; },
        [&](const FDreamSettingValue& In) { Group = In.Number; Detail = Group == 1 ? 0.8 : 0.4; return FDreamSettingsResult{}; }));
    D.Id = TEXT("Detail"); D.Step = 0.1; D.Order = 10; D.bRequiresConfirmation = false;
    Registry->Register(D, NumberProvider(Detail));
    FDreamSettingsSession S(Registry, Store); S.Open();
    S.Stage(TEXT("Group"), 1);
    TestTrue(TEXT("Group applies with side effect"), S.Apply().IsOk());
    TestEqual(TEXT("Unedited detail follows engine"), S.GetPending().FindChecked(TEXT("Detail")).Number, 0.8);
    TestTrue(TEXT("Reject restores group and overrides in order"), S.Reject().IsOk());
    TestEqual(TEXT("Original group restored"), Group, 0.0);
    TestEqual(TEXT("Original custom detail restored"), Detail, 0.2);
    S.Stage(TEXT("Group"),1); S.Stage(TEXT("Detail"),0.2);
    TestTrue(TEXT("Explicit unchanged override survives group side effect"),S.Apply().IsOk());
    TestTrue(TEXT("Original detail is still the requested value"),FMath::IsNearlyEqual(Detail,0.2));
    TestTrue(TEXT("Unchanged explicit override rejects safely"),S.Reject().IsOk());
    S.Stage(TEXT("Group"), 1); S.Stage(TEXT("Detail"), 0.6);
    TestTrue(TEXT("Explicit override applies after group"), S.Apply().IsOk());
    TestTrue(TEXT("Explicit detail retained"), FMath::IsNearlyEqual(Detail, 0.6));
    TestTrue(TEXT("Confirm handles captured side effects"), S.Confirm().IsOk());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsTypedStoreTest, "DreamSettings.Core.TypedRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsTypedStoreTest::RunTest(const FString&)
{
    FDreamSettingValue V;
    TestFalse(TEXT("Trailing garbage is corrupt"), FDreamSettingValue::Decode(TEXT("0.5garbage"), V));
    TestFalse(TEXT("NaN rejected"), FDreamSettingValue::Decode(TEXT("nan"), V));
    const auto Token = FDreamSettingValue::Id(TEXT("device=中文;[one]\n"));
    TestTrue(TEXT("Token decodes"), FDreamSettingValue::Decode(Token.Encode(), V));
    TestTrue(TEXT("Stable ID roundtrip, including INI punctuation"), V.Equals(Token));
    FDreamSettingDefinition Scalar;
    Scalar.SpecialValues.Add(-1,FText::FromString(TEXT("Inherit")));
    TestTrue(TEXT("Engine inheritance sentinel remains valid"),Scalar.Normalize(-1,V).IsOk());
    TestEqual(TEXT("Sentinel is not clamped into a fake numeric value"),V.Number,-1.0);
    TestFalse(TEXT("Other negative values remain invalid"),Scalar.Normalize(-0.5,V).IsOk());
    FDreamSettingDefinition D; D.Id = TEXT("Choice"); D.Kind = EDreamSettingKind::Choice;
    D.Options = {{TEXT("b"), FText::FromString(TEXT("B"))}, {TEXT("a"), FText::FromString(TEXT("A"))}};
    TestTrue(TEXT("Choice is ID, not index"), D.Normalize(FDreamSettingValue::Id(TEXT("a")), V).IsOk());
    TestFalse(TEXT("Unknown choice rejected"), D.Normalize(FDreamSettingValue::Id(TEXT("c")), V).IsOk());
    const FString Filename = FPaths::ProjectSavedDir() / TEXT("Automation/DreamSettings") / (FGuid::NewGuid().ToString() + TEXT(".ini"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FFileHelper::SaveStringToFile(TEXT("[DreamSettings.Preferences]\nSchemaVersion=1\nAudio.MasterVolume=0.25\n[Other]\nKeep=1\n"), *Filename);
    FDreamSettingsIniStore Store(Filename, TEXT("/Script/Engine.GameUserSettings"));
    FDreamSettingsValues Values;
    TestTrue(TEXT("Schema 1 readable"), Store.Load(Values).IsOk());
    TestEqual(TEXT("Schema 1 numeric value"), Values.FindChecked(DreamSettingIds::MasterVolume).Number, 0.25);
    Values.Add(TEXT("Display.WindowMode"), FDreamSettingValue::Id(TEXT("windowed")));
    Values.Add(TEXT("Display.Resolution"), FDreamSettingValue::Id(TEXT("1280x720")));
    TestTrue(TEXT("Schema 2 save"), Store.Save(Values).IsOk());
    TestTrue(TEXT("Durable journal save"), Store.BeginRiskyChange(Values).IsOk());
    FDreamSettingsIniStore Cold(Filename, TEXT("/Script/Engine.GameUserSettings"));
    FDreamSettingsValues Recovery;
    TestTrue(TEXT("Cold startup reads journal"), Cold.LoadRecovery(Recovery).IsOk());
    TestEqual(TEXT("Recovered stable mode"), Recovery.FindChecked(TEXT("Display.WindowMode")).Token, FString(TEXT("windowed")));
    TestTrue(TEXT("Recovery commit"), Cold.Save(Recovery).IsOk());
    Cold.LoadRecovery(Recovery);
    TestTrue(TEXT("Journal atomically removed with commit"), Recovery.IsEmpty());
    FString Text; FFileHelper::LoadFileToString(Text, *Filename);
    TestTrue(TEXT("Native resolution persisted"), Text.Contains(TEXT("ResolutionSizeX=1280")));
    TestTrue(TEXT("Other sections survive migration"), Text.Contains(TEXT("Keep=1")));
    IFileManager::Get().Delete(*Filename);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsCompositeTest, "DreamSettings.Core.CompositeDraft", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsCompositeTest::RunTest(const FString&)
{
    auto Registry = MakeShared<FDreamSettingsRegistry>();
    auto Store = MakeShared<FJournalStore>();
    double A = 0.2, B = 0.3;
    FDreamSettingDefinition D; D.Id = TEXT("A"); D.bHidden = true;
    Registry->Register(D, NumberProvider(A)); D.Id = TEXT("B"); Registry->Register(D, NumberProvider(B));
    D.Id = TEXT("Preset"); D.Kind = EDreamSettingKind::Choice; D.bHidden = false;
    D.Default = FDreamSettingValue::Id(TEXT("high"));
    D.Options = {{TEXT("high"),FText::FromString(TEXT("High"))},{TEXT("custom"),FText::FromString(TEXT("Custom")),false}};
    D.Dependencies = {TEXT("A"),TEXT("B")};
    D.Derive = [](const FDreamSettingsValues& V) { return FDreamSettingValue::Id(V.FindRef(TEXT("A")).Equals(1) && V.FindRef(TEXT("B")).Equals(1) ? TEXT("high") : TEXT("custom")); };
    D.Expand = [](const FDreamSettingValue&, const FDreamSettingsValues&, FDreamSettingsValues& V) { V.Add(TEXT("A"),1); V.Add(TEXT("B"),1); return FDreamSettingsResult{}; };
    TestTrue(TEXT("Composite needs no artificial backend"), Registry->Register(D).IsOk());
    FDreamSettingsSession S(Registry, Store); TestTrue(TEXT("Open composite"), S.Open().IsOk());
    TestEqual(TEXT("Derived initial custom state"), S.GetPending().FindChecked(TEXT("Preset")).Token, FString(TEXT("custom")));
    TestFalse(TEXT("Read-only custom choice is not selectable"), S.Stage(TEXT("Preset"),FDreamSettingValue::Id(TEXT("custom"))).IsOk());
    TestTrue(TEXT("Preset stages child values"), S.Stage(TEXT("Preset"),FDreamSettingValue::Id(TEXT("high"))).IsOk());
    TestEqual(TEXT("No engine write until apply"), A,0.2);
    TestEqual(TEXT("Derived draft updates immediately"), S.GetPending().FindChecked(TEXT("Preset")).Token,FString(TEXT("high")));
    S.Stage(TEXT("A"),0.5);
    TestEqual(TEXT("Individual edit becomes custom"),S.GetPending().FindChecked(TEXT("Preset")).Token,FString(TEXT("custom")));
    TestTrue(TEXT("Composite draft applies"),S.Apply().IsOk());
    TestEqual(TEXT("Real child persisted"),Store->Saved.FindChecked(TEXT("A")).Number,0.5);
    TestFalse(TEXT("No duplicate preset state persisted"),Store->Saved.Contains(TEXT("Preset")));
    S.Stage(TEXT("Preset"),FDreamSettingValue::Id(TEXT("high"))); S.Revert();
    TestEqual(TEXT("Revert restores custom detail"),S.GetPending().FindChecked(TEXT("A")).Number,0.5);
    TestFalse(TEXT("An invalid batch is rejected atomically"),S.StageBatch({{TEXT("A"),0.7},{TEXT("B"),2}}).IsOk());
    TestEqual(TEXT("Valid prefix of rejected batch is not staged"),S.GetPending().FindChecked(TEXT("A")).Number,0.5);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsAvailabilityTest, "DreamSettings.Core.AvailabilityAndSynchronization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsAvailabilityTest::RunTest(const FString&)
{
    auto Registry = MakeShared<FDreamSettingsRegistry>();
    auto Store = MakeShared<FJournalStore>();
    double Available = 0.2;
    FDreamSettingDefinition D; D.Id = TEXT("Available"); Registry->Register(D,NumberProvider(Available));
    D.Id = TEXT("MissingPlugin");
    Registry->Register(D,MakeShared<FDreamCallbackSettingsProvider>(
        [](FDreamSettingValue&) { return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,FText::FromString(TEXT("Plugin missing"))); },
        [](const FDreamSettingValue&) { return FDreamSettingsResult::Fail(EDreamSettingsError::Backend,FText::FromString(TEXT("Must not write"))); }));
    Store->Saved.Add(D.Id,0.9);
    FDreamSettingsSession S(Registry,Store);
    TestTrue(TEXT("An absent plugin does not block other settings"),S.Open().IsOk());
    TestFalse(TEXT("Unavailable row reports reason"),S.Availability(D.Id).IsOk());
    TestFalse(TEXT("Cannot stage absent provider"),S.Stage(D.Id,0.5).IsOk());
    Available = 0.4; S.Tick(FPlatformTime::Seconds());
    TestEqual(TEXT("Clean page follows engine startup changes"),S.GetPending().FindChecked(TEXT("Available")).Number,0.4);
    TestFalse(TEXT("Synchronization is not a user edit"),S.IsDirty());
    S.Stage(TEXT("Available"),0.7); Available = 0.5; S.Tick(FPlatformTime::Seconds());
    TestEqual(TEXT("External change never replaces a dirty draft"),S.GetPending().FindChecked(TEXT("Available")).Number,0.7);
    TestTrue(TEXT("Dirty draft retains conflict detection"),S.Apply().Code==EDreamSettingsError::Conflict);
    S.Revert(); S.Tick(FPlatformTime::Seconds());
    S.Stage(TEXT("Available"),0.6);
    TestTrue(TEXT("Available setting can still be saved"),S.Apply().IsOk());
    TestEqual(TEXT("Missing-plugin preference is preserved, not replaced with a fake default"),Store->Saved.FindChecked(D.Id).Number,0.9);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsRelatedTest,"DreamSettings.Core.RelatedDraftChoices",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsRelatedTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FJournalStore>();
    FString Method=TEXT("native"); double Scale=1;
    FDreamSettingDefinition D; D.Id=TEXT("Scale"); D.Step=0.1; D.Default=1;
    Registry->Register(D,NumberProvider(Scale));
    D.Id=TEXT("Method"); D.Kind=EDreamSettingKind::Choice; D.Default=FDreamSettingValue::Id(TEXT("native"));
    D.Options={{TEXT("native"),FText::FromString(TEXT("Native"))},{TEXT("sdk"),FText::FromString(TEXT("SDK"))},{TEXT("missing"),FText::FromString(TEXT("Missing"))}};
    D.OptionAvailability=[](const FString& Id,const FDreamSettingsValues&)
    { return Id!=TEXT("missing") ? FDreamSettingsResult{} : FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,FText::FromString(TEXT("SDK unavailable"))); };
    D.RelatedChanges=[](const FDreamSettingValue& V,const FDreamSettingsValues&,FDreamSettingsValues& Out)
    { Out.Add(TEXT("Scale"),V.Token==TEXT("native") ? 1.0 : 0.5); return FDreamSettingsResult{}; };
    Registry->Register(D,MakeShared<FDreamCallbackSettingsProvider>(
        [&](FDreamSettingValue& Out) { Out=FDreamSettingValue::Id(Method); return FDreamSettingsResult{}; },
        [&](const FDreamSettingValue& In) { Method=In.Token; return FDreamSettingsResult{}; }));
    FDreamSettingsSession S(Registry,Store); S.Open();
    TestFalse(TEXT("Unavailable option rejected"),S.Stage(TEXT("Method"),FDreamSettingValue::Id(TEXT("missing"))).IsOk());
    TestFalse(TEXT("Rejected choice leaves clean draft"),S.IsDirty());
    TestTrue(TEXT("Available choice stages related value"),S.Stage(TEXT("Method"),FDreamSettingValue::Id(TEXT("sdk"))).IsOk());
    TestEqual(TEXT("Related scale staged"),S.GetPending().FindChecked(TEXT("Scale")).Number,0.5);
    TestEqual(TEXT("No early engine writes"),Scale,1.0);
    TestTrue(TEXT("Reset one honors related values"),S.ResetOne(TEXT("Method")).IsOk());
    TestEqual(TEXT("Related scale reset"),S.GetPending().FindChecked(TEXT("Scale")).Number,1.0);
    TestFalse(TEXT("Batch cannot bypass dynamic capability"),S.StageBatch({{TEXT("Method"),FDreamSettingValue::Id(TEXT("missing"))},{TEXT("Scale"),0.3}}).IsOk());
    TestEqual(TEXT("Rejected batch preserves related draft"),S.GetPending().FindChecked(TEXT("Scale")).Number,1.0);
    return true;
}
#endif
