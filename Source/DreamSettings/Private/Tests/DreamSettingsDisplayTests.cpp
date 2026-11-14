#if WITH_DEV_AUTOMATION_TESTS
#include "DreamSettingsDisplay.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDisplayStatusTest,"DreamSettings.Core.DisplayStatus",EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDreamDisplayStatusTest::RunTest(const FString&)
{
    FDreamSettingsDisplayCapabilities C;
    C.HasWindow=C.HDRAllowed=C.PlatformSupportsHDR=C.CurrentMonitorSupportsHDR=C.OutputMetadataSupportsHDR=C.HDROutputEnabled=C.WindowIsHDR=true;
    for (int Format=0;Format<10;++Format)
    {
        C.OutputFormat=Format;
        TestEqual(FString::Printf(TEXT("Only actual display HDR formats count (%d)"),Format),UDreamSettingsDisplayLibrary::IsHDRPresentationActive(C),Format>=3 && Format<=6);
    }
    C.OutputFormat=5;
    for (bool* Flag:{&C.HasWindow,&C.HDRAllowed,&C.PlatformSupportsHDR,&C.CurrentMonitorSupportsHDR,&C.OutputMetadataSupportsHDR,&C.HDROutputEnabled,&C.WindowIsHDR})
    {
        *Flag=false;
        TestFalse(TEXT("Every capability/presentation condition is required"),UDreamSettingsDisplayLibrary::IsHDRPresentationActive(C));
        *Flag=true;
    }
    TestTrue(TEXT("Matching ScRGB presentation metadata reports active"),UDreamSettingsDisplayLibrary::IsHDRPresentationActive(C));
    return true;
}
#endif
