#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DreamSettingsHDRPreview.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamImage.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsHDRPreviewTest,"DreamSettings.UI.HDRPreviewLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDreamSettingsHDRPreviewTest::RunTest(const FString&)
{
    auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/DreamSettings/UI/M_HDRCalibration.M_HDRCalibration"));
    if (!TestNotNull(TEXT("Calibration material ships inside the portable plugin"),Material)) return false;
    auto* World=UWorld::CreateWorld(EWorldType::Editor,false);
    TStrongObjectPtr<UDreamWidget> Widget(NewObject<UDreamWidget>(World));
    auto* Image=Widget->CreateNewVisual<UDreamImage>(); Image->SetBrushFromMaterial(Material);
    auto* Preview=Widget->AddComponent<UDreamSettingsHDRPreview>();
    Preview->CandidateNits=300; Preview->Tick(0);
    if (TestNotNull(TEXT("Calibration owns a dynamic material"),Preview->Instance.Get()))
    {
        float Value=-1;
        Preview->Instance->GetScalarParameterValue(TEXT("OutputEnabled"),Value);
        TestEqual(TEXT("A non-game non-HDR target cannot show a false calibration"),Value,0.f);
        Preview->Instance->GetScalarParameterValue(TEXT("CandidateNits"),Value);
        TestEqual(TEXT("Paper white follows the unsaved candidate"),Value,300.f);
        Preview->PeakPattern=true; Preview->CandidateNits=1200; Preview->Tick(0);
        Preview->Instance->GetScalarParameterValue(TEXT("CandidateNits"),Value);
        TestEqual(TEXT("Peak changes reach the dynamic material"),Value,1200.f);
        Preview->Instance->GetScalarParameterValue(TEXT("PeakPattern"),Value);
        TestEqual(TEXT("Peak selects its distinct clipping pattern"),Value,1.f);
        TStrongObjectPtr<UDreamWidget> Copy(NewObject<UDreamWidget>(World));
        auto* CopyImage=Copy->CreateNewVisual<UDreamImage>(); CopyImage->SetBrush(Image->GetBrush());
        auto* CopyPreview=Copy->AddComponent<UDreamSettingsHDRPreview>();
        CopyPreview->CandidateNits=250; CopyPreview->Tick(0);
        if (TestNotNull(TEXT("A dynamic row can inherit a live material brush"),CopyPreview->Instance.Get()))
        {
            TestTrue(TEXT("A copied row owns a separate MID with a legal parent"),CopyPreview->Instance!=Preview->Instance && CopyPreview->Instance->Parent && !Cast<UMaterialInstanceDynamic>(CopyPreview->Instance->Parent));
            Preview->Instance->GetScalarParameterValue(TEXT("CandidateNits"),Value);
            TestEqual(TEXT("Editing the copy leaves the source row unchanged"),Value,1200.f);
        }
        CopyPreview->Release(); Copy->OnUnregister(); Copy.Reset();
        Preview->OnDisable();
        TestNull(TEXT("Closing releases the dynamic instance"),Preview->Instance.Get());
        TestEqual(TEXT("Closing restores the authored brush"),Image->GetBrush().GetResourceObject(),static_cast<UObject*>(Material));
        Preview->OnEnable(); Preview->Tick(0);
        TestNotNull(TEXT("Reopening recreates the instance"),Preview->Instance.Get());
    }
    Preview->Release(); Widget->OnUnregister(); Widget.Reset(); World->DestroyWorld(false);
    return true;
}
#endif
