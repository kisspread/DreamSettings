#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DreamSettingsPresentation.h"
#include "DreamSettingsDetails.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsHelpDataTest,"DreamSettings.UI.HelpPresentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDreamSettingsHelpDataTest::RunTest(const FString&)
{
    TStrongObjectPtr<UDreamSettingsPresentation> Data(NewObject<UDreamSettingsPresentation>());
    auto& Entry=Data->Settings.FindOrAdd(TEXT("Test.Help"));
    Entry.Body=FText::FromString(TEXT("Host body")); Entry.Additional=FText::FromString(TEXT("Base detail"));
    for (double Threshold:{10.0,0.0,5.0})
    { FDreamSettingsHelpVariant V; V.Minimum=Threshold; V.Additional=FText::AsNumber(Threshold); Entry.Variants.Add(V); }
    FDreamSettingsHelpVariant Choice; Choice.ForToken=true; Choice.Token=TEXT("quality"); Choice.Additional=FText::FromString(TEXT("Quality detail"));
    Entry.Variants.Add(Choice);
    TestEqual(TEXT("Threshold selection is independent of array order"),Data->Resolve(TEXT("Test.Help"),7).Additional.ToString(),FString(TEXT("5")));
    TestEqual(TEXT("Exact upper threshold is inclusive"),Data->Resolve(TEXT("Test.Help"),10).Additional.ToString(),FString(TEXT("10")));
    TestEqual(TEXT("Values below all thresholds retain the base detail"),Data->Resolve(TEXT("Test.Help"),-1).Additional.ToString(),FString(TEXT("Base detail")));
    TestEqual(TEXT("Token values select only their exact variant"),Data->Resolve(TEXT("Test.Help"),FDreamSettingValue::Id(TEXT("quality"))).Additional.ToString(),FString(TEXT("Quality detail")));
    TestEqual(TEXT("Unknown token retains base detail"),Data->Resolve(TEXT("Test.Help"),FDreamSettingValue::Id(TEXT("other"))).Additional.ToString(),FString(TEXT("Base detail")));

    TStrongObjectPtr<UDreamSettingsDetails> Details(NewObject<UDreamSettingsDetails>());
    FDreamSettingDefinition D; D.Id=TEXT("Test.Help"); D.Label=FText::FromString(TEXT("Test setting"));
    const FText Base=FText::FromString(TEXT("Backend body")),State=FText::FromString(TEXT("Device unavailable"));
    Entry.Image=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/NotLoadedForHiddenHelp.Image")));
    Details->Update(D,7,Base,FText::AsNumber(7),FText::AsNumber(1),State,Data.Get(),false);
    TestEqual(TEXT("Host data overrides the backend help copy"),Details->Body.ToString(),FString(TEXT("Host body")));
    TestEqual(TEXT("Unavailable reason remains separate from the explanation"),Details->StateText.ToString(),State.ToString());
    TestFalse(TEXT("Hidden details never load an illustration"),Details->HasImage || Details->HasImageStatus);
    Entry.Body=FText::GetEmpty(); Entry.Variants.Reset();
    TStrongObjectPtr<UTexture2D> Texture(UTexture2D::CreateTransient(4,2));
    Entry.Image=Texture.Get();
    Details->Update(D,7,Base,FText::AsNumber(7),FText::AsNumber(1),FText::GetEmpty(),Data.Get(),true);
    TestEqual(TEXT("Empty host body preserves backend description"),Details->Body.ToString(),Base.ToString());
    TestTrue(TEXT("A resident illustration is exposed immediately"),Details->HasImage && Details->ImageBrush.GetResourceObject()==Texture.Get());
    TestEqual(TEXT("Image brush carries its real dimensions"),Details->ImageBrush.ImageSize,FVector2f(4,2));
    Details->Reset(); TestFalse(TEXT("Reset releases the visible image"),Details->HasImage);
    TestNull(TEXT("Reset clears the brush reference"),Details->ImageBrush.GetResourceObject());
    return true;
}
#endif
