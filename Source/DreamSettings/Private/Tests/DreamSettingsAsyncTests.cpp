#if WITH_DEV_AUTOMATION_TESTS
#include "DreamSettingsModel.h"
#include "DreamSettingsProvider.h"
#include "Misc/AutomationTest.h"

namespace
{
    struct FAsyncStore : IDreamSettingsStore
    {
        FDreamSettingsValues Saved,Recovery; bool bFail=false;
        FDreamSettingsResult Load(FDreamSettingsValues& Out) override { Out=Saved; return {}; }
        FDreamSettingsResult Save(const FDreamSettingsValues& In) override
        { if (bFail) return FDreamSettingsResult::Fail(EDreamSettingsError::Storage,FText::FromString(TEXT("Disk failure"))); Saved=In; return {}; }
        FDreamSettingsResult BeginRiskyChange(const FDreamSettingsValues& In) override { Recovery=In; return {}; }
    };
    struct FAsyncDevice : IDreamSettingProvider
    {
        FString Actual=TEXT("a"),Target; bool bCompleted=false,bSuccess=true,bReadable=true;
        FDreamSettingsResult Read(FDreamSettingValue& Out) const override
        { if (!bReadable) return Pending(); Out=FDreamSettingValue::Id(Actual); return {}; }
        FDreamSettingsResult Write(const FDreamSettingValue& In) override { Target=In.Token; bCompleted=false; return Pending(); }
        FDreamSettingsResult PollWrite() override
        { return !bCompleted ? Pending() : bSuccess ? FDreamSettingsResult{} : FDreamSettingsResult::Fail(EDreamSettingsError::Backend,FText::FromString(TEXT("Device failure"))); }
        void Complete(bool bOk=true,bool bPartial=false) { bCompleted=true; bSuccess=bOk; if (bOk || bPartial) Actual=Target; }
        static FDreamSettingsResult Pending() { return FDreamSettingsResult::Fail(EDreamSettingsError::Pending,FText::FromString(TEXT("Waiting"))); }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsAsyncTest,"DreamSettings.Core.AsyncLifecycle",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsAsyncTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FAsyncStore>(); auto Device=MakeShared<FAsyncDevice>();
    double Volume=0.6;
    FDreamSettingDefinition V; V.Id=TEXT("Volume"); V.bPreview=true;
    Registry->Register(V,MakeShared<FDreamCallbackSettingsProvider>(
        [&](FDreamSettingValue& Out) { Out=Volume; return FDreamSettingsResult{}; },
        [&](const FDreamSettingValue& In) { Volume=In.Number; return FDreamSettingsResult{}; }));
    FDreamSettingDefinition D; D.Id=TEXT("Device"); D.Kind=EDreamSettingKind::Choice; D.Default=FDreamSettingValue::Id(TEXT("a")); D.Order=1;
    D.Options={{TEXT("a"),FText::FromString(TEXT("A"))},{TEXT("b"),FText::FromString(TEXT("B"))}};
    D.bAsyncWrite=true; Registry->Register(D,Device);
    FDreamSettingsSession S(Registry,Store); S.Open();
    S.Stage(D.Id,FDreamSettingValue::Id(TEXT("b"))); S.Stage(V.Id,0.42);
    TestTrue(TEXT("Apply waits for device"),S.Apply().IsPending());
    TestTrue(TEXT("Busy and unsaved"),S.IsBusy() && S.IsDirty() && Store->Saved.IsEmpty());
    TestFalse(TEXT("Editing locked during device operation"),S.Stage(V.Id,0.1).IsOk());
    S.Tick(0); TestTrue(TEXT("Polling is non-blocking"),S.IsBusy());
    Device->Complete(); S.Tick(0);
    TestTrue(TEXT("Only acknowledged output is committed"),!S.IsBusy() && !S.IsDirty() && S.GetLastResult().IsOk());
    TestEqual(TEXT("Saved real output"),Store->Saved.FindChecked(D.Id).Token,FString(TEXT("b")));

    S.Stage(D.Id,FDreamSettingValue::Id(TEXT("a"))); S.Stage(V.Id,0.2); Store->bFail=true;
    S.Apply(); Device->Complete(); S.Tick(0);
    TestTrue(TEXT("Storage failure starts asynchronous compensation"),S.IsBusy());
    TestTrue(TEXT("Earlier volume already compensated"),FMath::IsNearlyEqual(Volume,0.42));
    Device->Complete(); S.Tick(0);
    TestTrue(TEXT("Storage error reported after compensation"),!S.IsBusy() && S.GetLastResult().Code==EDreamSettingsError::Storage);
    TestEqual(TEXT("Output physically restored"),Device->Actual,FString(TEXT("b")));
    Store->bFail=false; S.Revert();

    S.Stage(D.Id,FDreamSettingValue::Id(TEXT("a"))); S.Apply();
    Device->Complete(false,true); S.Tick(0);
    TestTrue(TEXT("Partial async failure compensates"),S.IsBusy());
    Device->Complete(); S.Tick(0);
    TestTrue(TEXT("Original backend error retained"),S.GetLastResult().Code==EDreamSettingsError::Backend);
    TestEqual(TEXT("Partial async write restored"),Device->Actual,FString(TEXT("b"))); S.Revert();

    S.Stage(D.Id,FDreamSettingValue::Id(TEXT("a"))); S.Apply();
    TestTrue(TEXT("Close queues cancellation without releasing ownership"),S.Close().IsPending() && S.IsOpen());
    Device->Complete(); S.Tick(0);
    TestTrue(TEXT("Close waits for compensation"),S.IsBusy() && S.IsOpen());
    Device->Complete(); S.Tick(0);
    TestTrue(TEXT("Close completes after real restoration"),!S.IsBusy() && !S.IsOpen());
    TestEqual(TEXT("Closing never commits discarded device"),Store->Saved.FindChecked(D.Id).Token,FString(TEXT("b")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsVolatileTest,"DreamSettings.Core.DynamicDevices",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsVolatileTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FAsyncStore>(); auto Device=MakeShared<FAsyncDevice>();
    TArray<FDreamSettingOption> Options{{TEXT("a"),FText::FromString(TEXT("A"))}};
    FDreamSettingDefinition D; D.Id=TEXT("Device"); D.Kind=EDreamSettingKind::Choice; D.Default=FDreamSettingValue::Id(TEXT("a"));
    D.DynamicOptions=[&] { return Options; }; D.bFollowExternalChanges=true; D.bRetryUnavailable=true;
    Registry->Register(D,Device);
    double Other=0; FDreamSettingDefinition V; V.Id=TEXT("Other");
    Registry->Register(V,MakeShared<FDreamCallbackSettingsProvider>(
        [&](FDreamSettingValue& Out) { Out=Other; return FDreamSettingsResult{}; },
        [&](const FDreamSettingValue& In) { Other=In.Number; return FDreamSettingsResult{}; }));
    FDreamSettingsSession S(Registry,Store); S.Open();
    Options.Add({TEXT("b"),FText::FromString(TEXT("B"))});
    TestTrue(TEXT("Hotplug choices available in an existing session"),S.Stage(D.Id,FDreamSettingValue::Id(TEXT("b"))).IsOk()); S.Revert();
    S.Stage(V.Id,0.7); Device->Actual=TEXT("b"); S.Tick(0);
    TestEqual(TEXT("External device fallback becomes safe baseline"),S.GetPending().FindChecked(D.Id).Token,FString(TEXT("b")));
    TestEqual(TEXT("Other unsaved edits preserved"),S.GetPending().FindChecked(V.Id).Number,0.7);
    TestTrue(TEXT("Revert restores other edits without undoing hardware fallback"),S.Revert().IsOk());
    TestEqual(TEXT("New actual device remains selected"),Device->Actual,FString(TEXT("b")));
    S.Stage(D.Id,FDreamSettingValue::Id(TEXT("a"))); Device->bReadable=false; S.Tick(0);
    Device->bReadable=true; S.Tick(0);
    TestEqual(TEXT("Temporary enumeration failure preserves the user's device draft"),S.GetPending().FindChecked(D.Id).Token,FString(TEXT("a")));
    S.Revert();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsAsyncDisplayTest,"DreamSettings.Core.AsyncBeforeDisplay",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsAsyncDisplayTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FAsyncStore>(); auto Device=MakeShared<FAsyncDevice>();
    double Display=0;
    FDreamSettingDefinition V; V.Id=TEXT("Display"); V.bRequiresConfirmation=true;
    Registry->Register(V,MakeShared<FDreamCallbackSettingsProvider>(
        [&](FDreamSettingValue& Out) { Out=Display; return FDreamSettingsResult{}; },
        [&](const FDreamSettingValue& In) { Display=In.Number; return FDreamSettingsResult{}; }));
    FDreamSettingDefinition D; D.Id=TEXT("Device"); D.Kind=EDreamSettingKind::Choice; D.Default=FDreamSettingValue::Id(TEXT("a")); D.Order=100;
    D.Options={{TEXT("a"),FText::FromString(TEXT("A"))},{TEXT("b"),FText::FromString(TEXT("B"))}}; D.bAsyncWrite=true;
    Registry->Register(D,Device);
    FDreamSettingsSession S(Registry,Store); S.Open(); S.Stage(V.Id,1); S.Stage(D.Id,FDreamSettingValue::Id(TEXT("b")));
    TestTrue(TEXT("Combined operation starts with asynchronous device"),S.Apply().IsPending());
    TestTrue(TEXT("Display remains safe while driver is pending"),Display==0 && !S.IsAwaitingConfirmation());
    TestFalse(TEXT("Recovery journal precedes any runtime changes"),Store->Recovery.IsEmpty());
    Device->Complete(); S.Tick(0);
    TestTrue(TEXT("Display confirmation begins only after acknowledgement"),Display==1 && S.IsAwaitingConfirmation() && S.GetConfirmationSecondsLeft()>14);
    TestTrue(TEXT("No preferences saved before display confirmation"),Store->Saved.IsEmpty());
    S.Reject();
    TestTrue(TEXT("Display restores before waiting for audio compensation"),Display==0 && S.IsBusy());
    Device->Complete(); S.Tick(0);
    TestTrue(TEXT("Rejected combined operation fully restored"),!S.IsDirty() && !S.IsAwaitingConfirmation() && S.GetLastResult().IsOk());
    TestEqual(TEXT("Original endpoint restored after reject"),Device->Actual,FString(TEXT("a")));
    return true;
}
#endif
