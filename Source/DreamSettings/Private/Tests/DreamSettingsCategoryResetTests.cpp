#if WITH_DEV_AUTOMATION_TESTS
#include "DreamSettingsModel.h"
#include "DreamSettingsProvider.h"
#include "Misc/AutomationTest.h"

namespace
{
    struct FCategoryStore : IDreamSettingsStore
    {
        FDreamSettingsValues Saved;
        int Saves=0;
        FDreamSettingsResult Load(FDreamSettingsValues& Out) override { Out=Saved; return {}; }
        FDreamSettingsResult Save(const FDreamSettingsValues& In) override { Saved=In; ++Saves; return {}; }
    };
    TSharedRef<FDreamCallbackSettingsProvider> CategoryProvider(FDreamSettingsValues& Values,FName Id,bool Available=true)
    {
        return MakeShared<FDreamCallbackSettingsProvider>(
            [&Values,Id](FDreamSettingValue& Out) { Out=Values.FindChecked(Id); return FDreamSettingsResult{}; },
            [&Values,Id](const FDreamSettingValue& In) { Values.Add(Id,In); return FDreamSettingsResult{}; },
            [Available]() { return Available ? FDreamSettingsResult{} : FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,FText::FromString(TEXT("Unsupported"))); });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsCategoryResetTest,"DreamSettings.Core.CategoryReset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDreamSettingsCategoryResetTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FCategoryStore>();
    FDreamSettingsValues Values;
    auto Add=[&](const TCHAR* Id,const TCHAR* Category,double Initial,double Default,bool Available=true)
    {
        Values.Add(Id,Initial);
        FDreamSettingDefinition D; D.Id=Id; D.Category=Category; D.Default=Default;
        TestTrue(TEXT("Register category fixture"),Registry->Register(D,CategoryProvider(Values,D.Id,Available)).IsOk());
    };
    Add(TEXT("Master"),TEXT("Audio"),.2,1);
    Add(TEXT("Voice"),TEXT("Audio"),.3,1);
    Add(TEXT("Locked"),TEXT("Audio"),.4,1,false);
    Add(TEXT("Other"),TEXT("Graphics"),.6,.1);
    Values.Add(TEXT("Total"),.5);
    FDreamSettingDefinition Total; Total.Id=TEXT("Total"); Total.Category=TEXT("Audio"); Total.Max=2;
    Total.Dependencies={TEXT("Master"),TEXT("Voice")};
    Total.Derive=[](const FDreamSettingsValues& V) { return FDreamSettingValue(V.FindChecked(TEXT("Master")).Number+V.FindChecked(TEXT("Voice")).Number); };
    Total.Expand=[](const FDreamSettingValue&,const FDreamSettingsValues&,FDreamSettingsValues&) { return FDreamSettingsResult{}; };
    TestTrue(TEXT("Register derived aggregate"),Registry->Register(Total,CategoryProvider(Values,Total.Id)).IsOk());
    FDreamSettingsSession Session(Registry,Store);
    TestTrue(TEXT("Open category fixture"),Session.Open().IsOk());
    Session.Stage(TEXT("Master"),.55); Session.Stage(TEXT("Other"),.9);
    TestTrue(TEXT("Reset audio category"),Session.ResetCategory(TEXT("Audio")).IsOk());
    TestEqual(TEXT("Master default staged"),Session.GetPending().FindChecked(TEXT("Master")).Number,1.0);
    TestEqual(TEXT("Voice default staged"),Session.GetPending().FindChecked(TEXT("Voice")).Number,1.0);
    TestEqual(TEXT("Unavailable value preserved"),Session.GetPending().FindChecked(TEXT("Locked")).Number,.4);
    TestEqual(TEXT("Unrelated draft preserved"),Session.GetPending().FindChecked(TEXT("Other")).Number,.9);
    TestEqual(TEXT("Composite reflects its children"),Session.GetPending().FindChecked(TEXT("Total")).Number,2.0);
    TestEqual(TEXT("Reset has not applied native values"),Values.FindChecked(TEXT("Master")).Number,.2);
    TestEqual(TEXT("Reset has not saved"),Store->Saves,0);
    TestFalse(TEXT("Empty category cannot silently reset everything"),Session.ResetCategory(NAME_None).IsOk());
    TestFalse(TEXT("Unknown category rejected"),Session.ResetCategory(TEXT("Missing")).IsOk());
    TestEqual(TEXT("Rejected request preserves unrelated draft"),Session.GetPending().FindChecked(TEXT("Other")).Number,.9);
    TestTrue(TEXT("Apply commits the combined draft"),Session.Apply().IsOk());
    TestEqual(TEXT("Native audio updated on apply"),Values.FindChecked(TEXT("Master")).Number,1.0);
    TestEqual(TEXT("Other draft persists on explicit apply"),Store->Saved.FindChecked(TEXT("Other")).Number,.9);
    TestTrue(TEXT("Global reset remains available"),Session.Reset().IsOk());
    TestEqual(TEXT("Global reset reaches other category"),Session.GetPending().FindChecked(TEXT("Other")).Number,.1);
    TestTrue(TEXT("Revert cancels the global draft"),Session.Revert().IsOk());
    TestEqual(TEXT("Revert restores committed other value"),Session.GetPending().FindChecked(TEXT("Other")).Number,.9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsCategoryResetConflictTest,"DreamSettings.Core.CategoryResetConflict",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDreamSettingsCategoryResetConflictTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FCategoryStore>();
    FDreamSettingsValues Values; Values.Add(TEXT("Key"),FDreamSettingValue::Id(TEXT("A"))); Values.Add(TEXT("OtherKey"),FDreamSettingValue::Id(TEXT("B"))); Values.Add(TEXT("Scalar"),.4);
    FDreamSettingDefinition D; D.Id=TEXT("Scalar"); D.Category=TEXT("Controls"); D.Default=1;
    Registry->Register(D,CategoryProvider(Values,D.Id));
    D.Id=TEXT("Key"); D.Kind=EDreamSettingKind::Key; D.Default=FDreamSettingValue::Id(TEXT("B"));
    D.OptionAvailability=[](const FString& Key,const FDreamSettingsValues& Candidate)
    { return Key==Candidate.FindChecked(TEXT("OtherKey")).Token ? FDreamSettingsResult::Fail(EDreamSettingsError::Conflict,FText::FromString(TEXT("Key in use"))) : FDreamSettingsResult{}; };
    Registry->Register(D,CategoryProvider(Values,D.Id));
    D.Id=TEXT("OtherKey"); D.Category=TEXT("OtherControls"); D.Default=FDreamSettingValue::Id(TEXT("C")); D.OptionAvailability={};
    Registry->Register(D,CategoryProvider(Values,D.Id));
    FDreamSettingsSession Session(Registry,Store); TestTrue(TEXT("Open key fixture"),Session.Open().IsOk());
    Session.Stage(TEXT("Scalar"),.7);
    TestFalse(TEXT("Default key conflict rejects the whole category"),Session.ResetCategory(TEXT("Controls")).IsOk());
    TestEqual(TEXT("No partial scalar reset"),Session.GetPending().FindChecked(TEXT("Scalar")).Number,.7);
    TestEqual(TEXT("No partial key reset"),Session.GetPending().FindChecked(TEXT("Key")).Token,FString(TEXT("A")));
    TestEqual(TEXT("Outside category key preserved"),Session.GetPending().FindChecked(TEXT("OtherKey")).Token,FString(TEXT("B")));
    TestEqual(TEXT("Failed reset never saves"),Store->Saves,0);
    TestTrue(TEXT("Move conflicting outside key in draft"),Session.Stage(TEXT("OtherKey"),FDreamSettingValue::Id(TEXT("C"))).IsOk());
    TestTrue(TEXT("Retry succeeds with a valid combined draft"),Session.ResetCategory(TEXT("Controls")).IsOk());
    TestEqual(TEXT("Retry stages key default"),Session.GetPending().FindChecked(TEXT("Key")).Token,FString(TEXT("B")));
    TestEqual(TEXT("Retry preserves outside draft"),Session.GetPending().FindChecked(TEXT("OtherKey")).Token,FString(TEXT("C")));
    Session.Close();
    TestFalse(TEXT("Closed session cannot reset category"),Session.ResetCategory(TEXT("Controls")).IsOk());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsCategoryResetAvailabilityTest,"DreamSettings.Core.CategoryResetAvailability",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDreamSettingsCategoryResetAvailabilityTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FCategoryStore>();
    FDreamSettingsValues Values; Values.Add(TEXT("Volume"),.4); Values.Add(TEXT("Output"),FDreamSettingValue::Id(TEXT("current")));
    FDreamSettingDefinition D; D.Id=TEXT("Volume"); D.Category=TEXT("Audio"); D.Default=1;
    Registry->Register(D,CategoryProvider(Values,D.Id));
    bool DefaultAvailable=false;
    D.Id=TEXT("Output"); D.Kind=EDreamSettingKind::Choice; D.Default=FDreamSettingValue::Id(TEXT("default"));
    D.Options={{TEXT("current"),FText::FromString(TEXT("Current"))},{TEXT("default"),FText::FromString(TEXT("Default"))}};
    D.OptionAvailability=[&DefaultAvailable](const FString& Token,const FDreamSettingsValues&)
    { return Token==TEXT("default") && !DefaultAvailable ? FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,FText::FromString(TEXT("Default output unavailable"))) : FDreamSettingsResult{}; };
    Registry->Register(D,CategoryProvider(Values,D.Id));
    FDreamSettingsSession Session(Registry,Store); TestTrue(TEXT("Open availability fixture"),Session.Open().IsOk());
    TestFalse(TEXT("Unavailable default choice rejects category reset"),Session.ResetCategory(TEXT("Audio")).IsOk());
    TestEqual(TEXT("Rejected choice leaves sibling unchanged"),Session.GetPending().FindChecked(TEXT("Volume")).Number,.4);
    TestEqual(TEXT("Rejected choice keeps current output"),Session.GetPending().FindChecked(TEXT("Output")).Token,FString(TEXT("current")));
    TestFalse(TEXT("Global reset observes the same choice availability"),Session.Reset().IsOk());
    TestFalse(TEXT("Failed reset leaves a clean draft"),Session.IsDirty());
    DefaultAvailable=true;
    TestTrue(TEXT("Reset retries after capability becomes available"),Session.ResetCategory(TEXT("Audio")).IsOk());
    TestEqual(TEXT("Retry stages the available default"),Session.GetPending().FindChecked(TEXT("Output")).Token,FString(TEXT("default")));
    TestEqual(TEXT("Retry stages sibling default atomically"),Session.GetPending().FindChecked(TEXT("Volume")).Number,1.0);
    TestEqual(TEXT("Reset and retry never save"),Store->Saves,0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsAuthoredResetTest,"DreamSettings.Core.AuthoredPageReset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDreamSettingsAuthoredResetTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FCategoryStore>();
    FDreamSettingsValues Values;
    for (FName Id : {FName(TEXT("Preview")),FName(TEXT("Deferred")),FName(TEXT("Other"))})
    {
        Values.Add(Id,.3);
        FDreamSettingDefinition D; D.Id=Id; D.Default=.8; D.bPreview=Id==TEXT("Preview");
        Registry->Register(D,CategoryProvider(Values,Id));
    }
    FDreamSettingsSession Session(Registry,Store);
    TestTrue(TEXT("Authored reset fixture opens"),Session.Open().IsOk());
    TestTrue(TEXT("Outside draft stages"),Session.Stage(TEXT("Other"),.6).IsOk());
    TestTrue(TEXT("Mixed preview and deferred subset resets"),Session.ResetSettings({TEXT("Preview"),TEXT("Deferred")}).IsOk());
    TestEqual(TEXT("Preview default stays deferred until Apply"),Values.FindChecked(TEXT("Preview")).Number,.3);
    TestEqual(TEXT("Preview default in draft"),Session.GetPending().FindChecked(TEXT("Preview")).Number,.8);
    TestEqual(TEXT("Deferred default in draft"),Session.GetPending().FindChecked(TEXT("Deferred")).Number,.8);
    TestEqual(TEXT("Outside draft preserved"),Session.GetPending().FindChecked(TEXT("Other")).Number,.6);
    const auto Snapshot=Session.GetPending();
    TestFalse(TEXT("Unknown ID rejects subset"),Session.ResetSettings({TEXT("Other"),TEXT("Missing")}).IsOk());
    TestTrue(TEXT("Rejection leaves outside draft intact"),Session.GetPending().FindChecked(TEXT("Other")).Equals(Snapshot.FindChecked(TEXT("Other"))));
    TestTrue(TEXT("Authored reset can revert"),Session.Revert().IsOk());
    TestEqual(TEXT("Reset never persists"),Store->Saves,0);
    return true;
}
#endif
