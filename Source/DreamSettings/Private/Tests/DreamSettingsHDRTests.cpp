#if WITH_DEV_AUTOMATION_TESTS
#include "DreamSettingsHDR.h"
#include "DreamSettingsProvider.h"
#include "DreamSettingsIniStore.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"

namespace
{
    struct FHDRStore : IDreamSettingsStore
    {
        FDreamSettingsValues Saved,Recovery; bool FailNext=false;
        FDreamSettingsResult Load(FDreamSettingsValues& Out) override { Out=Saved; return {}; }
        FDreamSettingsResult Save(const FDreamSettingsValues& In) override
        {
            if (FailNext) { FailNext=false; return FDreamSettingsResult::Fail(EDreamSettingsError::Storage,FText::FromString(TEXT("Test storage failure"))); }
            Saved=In; Recovery.Reset(); return {};
        }
        FDreamSettingsResult BeginRiskyChange(const FDreamSettingsValues& In) override { Recovery=In; return {}; }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamHDRSessionTest,"DreamSettings.Core.HDRTransactions",EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDreamHDRSessionTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FHDRStore>();
    FDreamSettingsValues Native; bool Presented=false;
    for (const auto& D:DreamHDRDefinitions())
    {
        Native.Add(D.Id,D.Default); const FName Id=D.Id;
        TestTrue(TEXT("Native HDR definition registers"),Registry->Register(D,MakeShared<FDreamCallbackSettingsProvider>(
            [&,Id](FDreamSettingValue& Out){ Out=Native.FindChecked(Id); return FDreamSettingsResult{}; },
            [&,Id](const FDreamSettingValue& V){ Native.Add(Id,V); return FDreamSettingsResult{}; },
            [&,Id](){ return Id==TEXT("Display.HDR") || (Presented && Native.FindChecked(TEXT("Display.HDR")).Number!=0) ? FDreamSettingsResult{} : FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,FText::GetEmpty()); },
            [&,Id](const FDreamSettingValue& V){ return Id!=TEXT("Display.HDR") || V.Number==0 || Presented ? FDreamSettingsResult{} : FDreamSettingsResult::Fail(EDreamSettingsError::Backend,FText::FromString(TEXT("No HDR presentation"))); })).IsOk());
    }
    FDreamSettingsSession S(Registry,Store); TestTrue(TEXT("HDR session opens"),S.Open().IsOk());
    TestFalse(TEXT("Brightness unavailable before real HDR presentation"),S.Availability(TEXT("Display.HDRPaperWhite")).IsOk());
    TestTrue(TEXT("HDR request stages without preview"),S.Stage(TEXT("Display.HDR"),1).IsOk());
    TestEqual(TEXT("Staging does not switch output"),Native.FindChecked(TEXT("Display.HDR")).Number,0.0);
    TestTrue(TEXT("Apply starts safe confirmation"),S.Apply().IsOk());
    TestTrue(TEXT("HDR request is held for confirmation"),S.IsAwaitingConfirmation());
    TestEqual(TEXT("Journal stores SDR baseline"),Store->Recovery.FindChecked(TEXT("Display.HDR")).Number,0.0);
    TestFalse(TEXT("A requested flag without actual presentation cannot commit"),S.Confirm().IsOk());
    TestEqual(TEXT("Failed presentation returns to SDR"),Native.FindChecked(TEXT("Display.HDR")).Number,0.0);
    S.Revert(); Presented=true;
    S.Stage(TEXT("Display.HDR"),1); S.Apply(); S.Tick(FPlatformTime::Seconds()+16);
    TestEqual(TEXT("HDR countdown expiry restores SDR"),Native.FindChecked(TEXT("Display.HDR")).Number,0.0);
    S.Stage(TEXT("Display.HDR"),1); S.Apply(); Store->FailNext=true;
    TestFalse(TEXT("Failed durable save is reported"),S.Confirm().IsOk());
    TestEqual(TEXT("Save failure compensates output"),Native.FindChecked(TEXT("Display.HDR")).Number,0.0);
    TestTrue(TEXT("Compensation clears the recovery record"),Store->Recovery.IsEmpty());
    S.Revert(); S.Stage(TEXT("Display.HDR"),1); S.Apply();
    TestTrue(TEXT("Actual HDR presentation can be kept"),S.Confirm().IsOk());
    TestTrue(TEXT("Custom calibration previews"),S.Stage(TEXT("Display.HDRCalibration"),1).IsOk());
    TestTrue(TEXT("Peak brightness previews"),S.Stage(TEXT("Display.HDRPeakNits"),1200).IsOk());
    TestTrue(TEXT("Paper white previews"),S.Stage(TEXT("Display.HDRPaperWhite"),260).IsOk());
    TestTrue(TEXT("Switching custom calibration off retains the edited value"),S.Stage(TEXT("Display.HDRCalibration"),0).IsOk());
    TestFalse(TEXT("Inactive peak row cannot be edited"),S.Availability(TEXT("Display.HDRPeakNits")).IsOk());
    TestTrue(TEXT("Inactive stored peak does not prevent applying other changes"),S.Apply().IsOk());
    TestEqual(TEXT("Peak preference persists for later reuse"),Store->Saved.FindChecked(TEXT("Display.HDRPeakNits")).Number,1200.0);
    S.Stage(TEXT("Display.HDRPaperWhite"),300); S.Stage(TEXT("Display.HDR"),0); S.Apply();
    TestTrue(TEXT("Disabling HDR also requires confirmation"),S.IsAwaitingConfirmation());
    TestTrue(TEXT("Reject restores output and all previews"),S.Reject().IsOk());
    TestEqual(TEXT("Paper white reverts with rejected output"),Native.FindChecked(TEXT("Display.HDRPaperWhite")).Number,260.0);

    const FString Path=FPaths::ProjectSavedDir()/TEXT("Automation")/(TEXT("HDRPrepared-")+FGuid::NewGuid().ToString()+TEXT(".ini"));
    FDreamSettingsIniStore Disk(Path,TEXT("/Script/Engine.GameUserSettings")); FDreamSettingsPreparedWrite Prepared;
    TestTrue(TEXT("HDR atomic snapshot prepares"),Disk.PrepareSave(Store->Saved,Prepared).IsOk());
    const FString Text=FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Prepared.Bytes.GetData()),Prepared.Bytes.Num()).Get();
    FConfigFile Parsed; Parsed.ProcessInputFileContents(Text,Path);
    bool Enabled=false,Calibration=true; int32 Peak=0; float White=0;
    TestTrue(TEXT("Native HDR enable key is present"),Parsed.GetBool(TEXT("/Script/Engine.GameUserSettings"),TEXT("bUseHDRDisplayOutput"),Enabled));
    TestTrue(TEXT("Native calibration key is present"),Parsed.GetBool(TEXT("/Script/Engine.GameUserSettings"),TEXT("bUseHDRDisplayCalibration"),Calibration));
    TestTrue(TEXT("Native peak key is present"),Parsed.GetInt(TEXT("/Script/Engine.GameUserSettings"),TEXT("HDRDisplayOutputNits"),Peak));
    TestTrue(TEXT("Native paper-white key is present"),Parsed.GetFloat(TEXT("/Script/Engine.GameUserSettings"),TEXT("HDRPaperWhiteNits"),White));
    TestTrue(TEXT("Native mirrors contain the confirmed settings"),Enabled && !Calibration && Peak==1200 && White==260);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamHDRUnavailablePreferenceTest,"DreamSettings.Core.HDRUnavailablePreference",EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDreamHDRUnavailablePreferenceTest::RunTest(const FString&)
{
    auto Registry=MakeShared<FDreamSettingsRegistry>(); auto Store=MakeShared<FHDRStore>();
    Store->Saved.Add(TEXT("Display.HDR"),1);
    for (const auto& D:DreamHDRDefinitions())
    {
        const FName Id=D.Id; const auto Default=D.Default;
        Registry->Register(D,MakeShared<FDreamCallbackSettingsProvider>(
            [Id,Default](FDreamSettingValue& Out)
            {
                if (Id==TEXT("Display.HDR")) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,FText::FromString(TEXT("Forced SDR policy")));
                Out=Default; return FDreamSettingsResult{};
            },[](const FDreamSettingValue&){ return FDreamSettingsResult{}; }));
    }
    double Other=0;
    FDreamSettingDefinition D; D.Id=TEXT("Other"); D.Default=0;
    Registry->Register(D,MakeShared<FDreamCallbackSettingsProvider>([&](FDreamSettingValue& Out){ Out=Other; return FDreamSettingsResult{}; },[&](const FDreamSettingValue& V){ Other=V.Number; return FDreamSettingsResult{}; }));
    FDreamSettingsSession S(Registry,Store);
    TestTrue(TEXT("Menu remains usable under forced SDR"),S.Open().IsOk());
    TestFalse(TEXT("Forced-off HDR is unavailable for editing"),S.Availability(TEXT("Display.HDR")).IsOk());
    TestTrue(TEXT("Another setting can be edited"),S.Stage(D.Id,.5).IsOk());
    TestTrue(TEXT("Another setting can be saved"),S.Apply().IsOk());
    TestEqual(TEXT("Saving an unrelated setting preserves the stored HDR request"),Store->Saved.FindChecked(TEXT("Display.HDR")).Number,1.0);
    return true;
}
#endif
