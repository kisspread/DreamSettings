#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DreamSettingsViewModel.h"
#include "DreamSettingsProvider.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    struct FNumericEntryStore : IDreamSettingsStore
    {
        int Saves=0;
        FDreamSettingsResult Load(FDreamSettingsValues& Out) override { Out.Reset(); return {}; }
        FDreamSettingsResult Save(const FDreamSettingsValues&) override { ++Saves; return {}; }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsNumericEntryTest,"DreamSettings.UI.NumericEntry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDreamSettingsNumericEntryTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FNumericEntryStore>();
    FDreamSettingsValues Native;
    auto Add=[&](FName Id,FName Category,double Min,double Max,double Step,double Default)
    {
        FDreamSettingDefinition D; D.Id=Id; D.Category=Category; D.Min=Min; D.Max=Max; D.Step=Step; D.Default=Default; D.bPreview=true;
        Native.Add(Id,Default);
        TestTrue(TEXT("Numeric fixture registers"),Registry->Register(D,MakeShared<FDreamCallbackSettingsProvider>(
            [&,Id](FDreamSettingValue& Out){ Out=Native.FindChecked(Id); return FDreamSettingsResult{}; },
            [&,Id](const FDreamSettingValue& V){ Native.Add(Id,V); return FDreamSettingsResult{}; })).IsOk());
    };
    Add(TEXT("Test.Number"),TEXT("Display"),.4,3.5,.05,2.2);
    Add(TEXT("Audio.MasterVolume"),TEXT("Audio"),0,1,.01,1);
    Add(TEXT("Accessibility.UIScale"),TEXT("Accessibility"),.8,1.25,.05,1);
    auto S=MakeShared<FDreamSettingsSession>(Registry,Store); TestTrue(TEXT("Numeric session opens"),S->Open().IsOk());
    TStrongObjectPtr<UDreamSettingRowViewModel> Row(NewObject<UDreamSettingRowViewModel>());
    Row->Initialize(S,TEXT("Test.Number"));
    Row->CommitNumberText(TEXT(" 2.23 "));
    TestTrue(TEXT("Typed value uses the definition step grid"),FMath::IsNearlyEqual(Native.FindChecked(TEXT("Test.Number")).Number,2.25,1.e-6));
    TestEqual(TEXT("Canonical input reflects normalization"),Row->NumericText,FString(TEXT("2.25")));
    for (const TCHAR* Bad:{TEXT(""),TEXT("-"),TEXT("1.2.3"),TEXT("1abc"),TEXT("NaN"),TEXT("Inf"),TEXT("1e2"),TEXT("1,5"),TEXT("4"),TEXT("0.3"),TEXT("25%")})
    {
        Row->CommitNumberText(Bad);
        TestTrue(FString::Printf(TEXT("Rejected %s keeps the current native value"),Bad),FMath::IsNearlyEqual(Native.FindChecked(TEXT("Test.Number")).Number,2.25,1.e-6));
        TestFalse(TEXT("Rejected entry explains why"),Row->Description.IsEmpty());
        TestEqual(TEXT("Rejected input restores canonical text"),Row->NumericText,FString(TEXT("2.25")));
    }
    Row->CommitNumberText(TEXT("３．５")); TestEqual(TEXT("Full-width IME decimal accepted"),Native.FindChecked(TEXT("Test.Number")).Number,3.5);
    Row->Initialize(S,TEXT("Audio.MasterVolume"));
    TestEqual(TEXT("Audio input uses percent units"),Row->NumericText,FString(TEXT("100%")));
    Row->CommitNumberText(TEXT("７５％")); TestEqual(TEXT("Percent input maps to the native volume ratio"),Native.FindChecked(TEXT("Audio.MasterVolume")).Number,.75);
    Row->CommitNumberText(TEXT("50")); TestEqual(TEXT("Percent sign is optional for percent settings"),Native.FindChecked(TEXT("Audio.MasterVolume")).Number,.5);
    Row->Initialize(S,TEXT("Accessibility.UIScale")); Row->CommitNumberText(TEXT("125%"));
    TestEqual(TEXT("UI scale uses the same percent convention"),Native.FindChecked(TEXT("Accessibility.UIScale")).Number,1.25);
    TestEqual(TEXT("Typing never saves preferences"),Store->Saves,0);
    TestTrue(TEXT("Numeric preview reverts"),S->Revert().IsOk());
    TestEqual(TEXT("Revert restores audio"),Native.FindChecked(TEXT("Audio.MasterVolume")).Number,1.0);
    TestEqual(TEXT("Revert restores scale"),Native.FindChecked(TEXT("Accessibility.UIScale")).Number,1.0);
    return true;
}
#endif
