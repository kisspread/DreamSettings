#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DreamSettingsLaunchConfig.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsLaunchConfigTest,"DreamSettings.Core.EditorLaunchConfig",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDreamSettingsLaunchConfigTest::RunTest(const FString&)
{
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Config/Windows");
    for (const FString& Value:{FString(TEXT("PIEGameUserSettings0")),FString(TEXT("\"PIEGameUserSettings12\""))})
    {
        FString Command=TEXT("/Game/Maps/Example -game -PIEVIACONSOLE GameUserSettingsINI=")+Value+TEXT(" -windowed -ResX=1280");
        TestTrue(TEXT("Editor generated basename is normalized"),NormalizeDreamEditorSettingsOverride(Command,Root));
        FString Filename; FParse::Value(*Command,TEXT("GameUserSettingsINI="),Filename);
        FString FullRoot=FPaths::ConvertRelativePathToFull(Root); FPaths::NormalizeDirectoryName(FullRoot);
        TestTrue(TEXT("Generated path stays inside the project config folder"),Filename.StartsWith(FullRoot+TEXT("/")));
        TestTrue(TEXT("Generated filename has .ini extension"),Filename.EndsWith(TEXT(".ini")));
        TestTrue(TEXT("Following launch arguments preserved"),Command.EndsWith(TEXT(" -windowed -ResX=1280")));
        TestFalse(TEXT("Normalization is idempotent"),NormalizeDreamEditorSettingsOverride(Command,Root));
    }
    const TArray<FString> Unchanged={TEXT("-game GameUserSettingsINI=PIEGameUserSettings0"),
        TEXT("-game -PIEVIACONSOLE GameUserSettingsINI=\"E:/Settings/My game.ini\""),
        TEXT("-game -PIEVIACONSOLE GameUserSettingsINI=PIEGameUserSettings../Other"),
        TEXT("-game -PIEVIACONSOLE GameUserSettingsINI=Custom.ini")};
    for (const auto& Original:Unchanged)
    {
        FString Command=Original;
        TestFalse(TEXT("Other overrides remain untouched"),NormalizeDreamEditorSettingsOverride(Command,Root));
        TestEqual(TEXT("Original command line preserved"),Command,Original);
    }
    return true;
}
#endif
