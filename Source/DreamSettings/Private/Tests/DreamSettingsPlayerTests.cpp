#if WITH_DEV_AUTOMATION_TESTS
#include "DreamSettingsPlayer.h"
#include "Misc/AutomationTest.h"

namespace
{
    struct FPlayerMemoryStore : IDreamSettingsStore
    {
        FDreamSettingsValues Saved,Recovery; bool bFail=false;
        FDreamSettingsResult Load(FDreamSettingsValues& Out) override { Out=Saved; return {}; }
        FDreamSettingsResult Save(const FDreamSettingsValues& In) override
        { if (bFail) return FDreamSettingsResult::Fail(EDreamSettingsError::Storage,FText::FromString(TEXT("Cannot write"))); Saved=In; return {}; }
        FDreamSettingsResult BeginRiskyChange(const FDreamSettingsValues& In) override { Recovery=In; return {}; }
        FDreamSettingsResult LoadRecovery(FDreamSettingsValues& Out) override { Out=Recovery; return {}; }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamPlayerScopeTest,"DreamSettings.Core.PlayerScope",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamPlayerScopeTest::RunTest(const FString&)
{
    auto Store=MakeShared<FPlayerMemoryStore>();
    FDreamPlayerSettingsStore P0(Store,0,UDreamSettingsPlayerSubsystem::SettingIds()),P1(Store,1,UDreamSettingsPlayerSubsystem::SettingIds());
    FDreamSettingsValues V; V.Add(TEXT("Input.MouseSensitivity"),1.7); V.Add(TEXT("Input.GamepadIcons"),FDreamSettingValue::Id(TEXT("playstation"))); V.Add(TEXT("Display.VSync"),1); V.Add(TEXT("Accessibility.ReducedMotion"),1); V.Add(TEXT("Accessibility.UIScale"),1.25); V.Add(TEXT("Accessibility.SubtitleSize"),40); P0.Save(V);
    V.Add(TEXT("Input.MouseSensitivity"),0.4); V.Add(TEXT("Input.GamepadIcons"),FDreamSettingValue::Id(TEXT("nintendo"))); V.Add(TEXT("Display.VSync"),0); V.Add(TEXT("Accessibility.ReducedMotion"),0); V.Add(TEXT("Accessibility.UIScale"),.8); V.Add(TEXT("Accessibility.SubtitleSize"),18); P1.Save(V);
    FDreamSettingsValues Loaded; P0.Load(Loaded);
    TestEqual(TEXT("Player zero sensitivity isolated"),Loaded.FindChecked(TEXT("Input.MouseSensitivity")).Number,1.7);
    TestEqual(TEXT("Player zero icon family isolated"),Loaded.FindChecked(TEXT("Input.GamepadIcons")).Token,FString(TEXT("playstation")));
    FDreamSettingsValues Other; P1.Load(Other);
    TestEqual(TEXT("Player one icon family isolated"),Other.FindChecked(TEXT("Input.GamepadIcons")).Token,FString(TEXT("nintendo")));
    TestEqual(TEXT("Player zero motion preference isolated"),Loaded.FindChecked(TEXT("Accessibility.ReducedMotion")).Number,1.0);
    TestEqual(TEXT("Player one motion preference isolated"),Other.FindChecked(TEXT("Accessibility.ReducedMotion")).Number,0.0);
    TestEqual(TEXT("Player zero interface scale isolated"),Loaded.FindChecked(TEXT("Accessibility.UIScale")).Number,1.25);
    TestEqual(TEXT("Player one interface scale isolated"),Other.FindChecked(TEXT("Accessibility.UIScale")).Number,.8);
    TestEqual(TEXT("Player zero subtitle size isolated"),Loaded.FindChecked(TEXT("Accessibility.SubtitleSize")).Number,40.0);
    TestEqual(TEXT("Player one subtitle size isolated"),Other.FindChecked(TEXT("Accessibility.SubtitleSize")).Number,18.0);
    TestEqual(TEXT("Machine preference remains shared"),Loaded.FindChecked(TEXT("Display.VSync")).Number,0.0);
    TestFalse(TEXT("Other player IDs are not exposed to a session"),Loaded.Contains(TEXT("Player.1.Input.MouseSensitivity")));
    P0.BeginRiskyChange(Loaded);
    TestEqual(TEXT("Recovery preserves other player"),Store->Recovery.FindChecked(TEXT("Player.1.Input.MouseSensitivity")).Number,0.4);
    FDreamSettingsValues Recovery; P0.LoadRecovery(Recovery);
    TestEqual(TEXT("Recovery decodes current player"),Recovery.FindChecked(TEXT("Input.MouseSensitivity")).Number,1.7);
    Store->bFail=true; Loaded.Add(TEXT("Input.MouseSensitivity"),2.0);
    TestFalse(TEXT("Atomic failure is propagated"),P0.Save(Loaded).IsOk()); P0.Load(Loaded);
    TestEqual(TEXT("Failed save preserves committed player setting"),Loaded.FindChecked(TEXT("Input.MouseSensitivity")).Number,1.7);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLookInputTest,"DreamSettings.Core.LookResponse",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamLookInputTest::RunTest(const FString&)
{
    FDreamLookPreferences P; P.MouseSensitivity=2; P.GamepadSensitivity=1.5; P.GamepadDeadZone=0.2;
    TestTrue(TEXT("Mouse displacement ignores frame duration"),P.Transform(FVector2D(10,5),false,1.0/30).Equals(P.Transform(FVector2D(10,5),false,1.0/120)));
    TestTrue(TEXT("Mouse sensitivity controls actual angle"),P.Transform(FVector2D(10,5),false,0).Equals(FVector2D(2,1)));
    TestTrue(TEXT("Radial dead zone eliminates drift"),P.Transform(FVector2D(0.1,0.1),true,1).IsNearlyZero());
    TestTrue(TEXT("Full deflection remains full speed after dead zone"),P.Transform(FVector2D(1,0),true,1).Equals(FVector2D(180,0)));
    const auto Slow=P.Transform(FVector2D(0.6,0),true,1.0/30)*30;
    const auto Fast=P.Transform(FVector2D(0.6,0),true,1.0/120)*120;
    TestTrue(TEXT("Controller rotation is frame-rate independent"),Slow.Equals(Fast) && Slow.Equals(FVector2D(90,0)));
    P.bInvertX=P.bInvertY=true;
    TestTrue(TEXT("Both inversion axes are consumed"),P.Transform(FVector2D(10,5),false,0).Equals(FVector2D(-2,-1)));
    return true;
}
#endif
