#if WITH_DEV_AUTOMATION_TESTS
#include "DreamSettingsModel.h"
#include "DreamSettingsProvider.h"
#include "Misc/AutomationTest.h"

namespace
{
    struct FCatalogueStore : IDreamSettingsStore
    {
        FDreamSettingsValues Saved;
        FDreamSettingsResult Load(FDreamSettingsValues& Out) override { Out = Saved; return {}; }
        FDreamSettingsResult Save(const FDreamSettingsValues& In) override { Saved = In; return {}; }
    };
    FDreamSettingDefinition CatalogueEntry(FName Id, FName Category, int32 Order)
    {
        FDreamSettingDefinition D;
        D.Id = Id; D.Category = Category; D.Order = Order;
        D.Label = FText::FromName(Id); D.Default = 0; D.Step = 1;
        return D;
    }
    TSharedRef<FDreamCallbackSettingsProvider> InertProvider()
    {
        return MakeShared<FDreamCallbackSettingsProvider>(
            [](FDreamSettingValue& Out) { Out = 0; return FDreamSettingsResult{}; },
            [](const FDreamSettingValue&) { return FDreamSettingsResult{}; });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsCatalogueOrderTest,
    "DreamSettings.Core.CatalogueOrder",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDreamSettingsCatalogueOrderTest::RunTest(const FString&)
{
    auto Registry = MakeShared<FDreamSettingsRegistry>();
    Registry->Register(CatalogueEntry(TEXT("B.One"), TEXT("Beta"), 300), InertProvider());
    Registry->Register(CatalogueEntry(TEXT("A.One"), TEXT("Alpha"), 100), InertProvider());
    Registry->Register(CatalogueEntry(TEXT("B.Two"), TEXT("Beta"), 500), InertProvider());

    // Unregistered categories infer their order from the first row and sort by that value.
    const auto Inferred = Registry->Categories();
    TestEqual(TEXT("Two categories inferred"), Inferred.Num(), 2);
    TestEqual(TEXT("Alpha sorts by its row order"), Inferred[0].Id, FName(TEXT("Alpha")));
    TestEqual(TEXT("Beta keeps the earliest row order"), Inferred[1].Order, 300);

    // An explicit order overrides inference without touching definitions.
    TestTrue(TEXT("Reorder Beta"), Registry->OverrideCategory(TEXT("Beta"), {}, {}, 50).IsOk());
    const auto Reordered = Registry->Categories();
    TestEqual(TEXT("Explicit order wins"), Reordered[0].Id, FName(TEXT("Beta")));
    TestEqual(TEXT("Alpha keeps its inferred order"), Reordered[1].Order, 100);

    // Explicit registration is rejected once rows already own the name, so hosts use the override path.
    FDreamSettingCategory Duplicate;
    Duplicate.Id = TEXT("Alpha"); Duplicate.Label = FText::FromString(TEXT("Alpha"));
    TestFalse(TEXT("A used category cannot be registered twice"), Registry->RegisterCategory(Duplicate).IsOk());
    FDreamSettingCategory Fresh;
    Fresh.Id = TEXT("Network"); Fresh.Label = FText::FromString(TEXT("Network")); Fresh.Order = 900;
    TestTrue(TEXT("An unused category registers"), Registry->RegisterCategory(Fresh).IsOk());
    TestEqual(TEXT("New category is listed last"), Registry->Categories().Last().Id, FName(TEXT("Network")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsCatalogueOverrideTest,
    "DreamSettings.Core.CatalogueOverride",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDreamSettingsCatalogueOverrideTest::RunTest(const FString&)
{
    auto Registry = MakeShared<FDreamSettingsRegistry>();
    Registry->Register(CatalogueEntry(TEXT("Display.HDR"), TEXT("Display"), 10), InertProvider());
    Registry->Register(CatalogueEntry(TEXT("Display.VSync"), TEXT("Display"), 20), InertProvider());

    FDreamSettingsCatalogOverride Request;
    Request.HiddenSettings.Add(TEXT("Display.HDR"));
    Request.SettingLabels.Add(TEXT("Display.VSync"), FText::FromString(TEXT("Sync")));
    Request.CategoryLabels.Add(TEXT("Display"), FText::FromString(TEXT("Video")));
    TestTrue(TEXT("Apply override"), ApplyDreamSettingsCatalogOverride(*Registry, Request).IsOk());
    TestTrue(TEXT("Hidden survives Find"), Registry->Find(TEXT("Display.HDR"))->bHidden);
    TestEqual(TEXT("Label replaced"), Registry->Find(TEXT("Display.VSync"))->Label.ToString(), FString(TEXT("Sync")));
    TestEqual(TEXT("Category renamed"), Registry->Categories()[0].Label.ToString(), FString(TEXT("Video")));

    // A rejected request must not apply the entries before the failing one.
    FDreamSettingsCatalogOverride Bad;
    Bad.HiddenSettings.Add(TEXT("Display.VSync"));
    Bad.CategoryOrder.Add(TEXT("Nowhere"), 1);
    TestFalse(TEXT("Unknown category rejects the request"), ApplyDreamSettingsCatalogOverride(*Registry, Bad).IsOk());
    TestFalse(TEXT("Nothing was hidden"), Registry->Find(TEXT("Display.VSync"))->bHidden);

    // Overrides are presentation only: the provider contract is untouched.
    TestNotNull(TEXT("Provider still present"), Registry->Provider(TEXT("Display.HDR")).Get());
    TArray<FName> Ordered;
    TestTrue(TEXT("Overridden catalogue still validates"), Registry->Validate(Ordered).IsOk());
    TestTrue(TEXT("Both definitions remain"), Ordered.Contains(FName(TEXT("Display.HDR"))));

    // Page capabilities travel with the tab, so a host tab can offer the scene preview without being Graphics.
    Registry->Register(CatalogueEntry(TEXT("Network.Mode"), TEXT("Network"), 900), InertProvider());
    FDreamSettingsCatalogOverride Content;
    Content.CategoryContent.Add(TEXT("Network"), EDreamSettingCategoryContent::ScenePreview);
    TestTrue(TEXT("Set tab content"), ApplyDreamSettingsCatalogOverride(*Registry, Content).IsOk());
    const auto Tabs = Registry->Categories();
    const auto* Network = Tabs.FindByPredicate([&](const auto& C) { return C.Id == FName(TEXT("Network")); });
    TestNotNull(TEXT("Host tab listed"), Network);
    TestTrue(TEXT("Host tab opts into the scene preview"),
        EnumHasAnyFlags(Network->Content, EDreamSettingCategoryContent::ScenePreview));
    const auto* Display = Tabs.FindByPredicate([&](const auto& C) { return C.Id == FName(TEXT("Display")); });
    TestTrue(TEXT("Display keeps its information panel"),
        EnumHasAnyFlags(Display->Content, EDreamSettingCategoryContent::DisplayInformation));
    TestFalse(TEXT("Rename did not add capabilities"), EnumHasAnyFlags(Display->Content, EDreamSettingCategoryContent::ColourSamples));

    // A session opened after the edit sees the same catalogue, and a hidden row never reaches the page.
    FDreamSettingsValues Values;
    Values.Add(TEXT("Display.VSync"), 0); Values.Add(TEXT("Display.HDR"), 0);
    auto Live = MakeShared<FDreamSettingsRegistry>();
    auto AddLive = [&](FName Id, FName Category, int32 Order, TSharedPtr<IDreamSettingProvider> Provider)
    {
        FDreamSettingDefinition D = CatalogueEntry(Id, Category, Order);
        Live->Register(D, Provider);
    };
    AddLive(TEXT("Display.VSync"), TEXT("Display"), 10, MakeShared<FDreamCallbackSettingsProvider>(
        [&Values](FDreamSettingValue& Out) { Out = Values.FindChecked(TEXT("Display.VSync")); return FDreamSettingsResult{}; },
        [&Values](const FDreamSettingValue& In) { Values.Add(TEXT("Display.VSync"), In); return FDreamSettingsResult{}; }));
    AddLive(TEXT("Display.HDR"), TEXT("Display"), 20, MakeShared<FDreamCallbackSettingsProvider>(
        [&Values](FDreamSettingValue& Out) { Out = Values.FindChecked(TEXT("Display.HDR")); return FDreamSettingsResult{}; },
        [&Values](const FDreamSettingValue& In) { Values.Add(TEXT("Display.HDR"), In); return FDreamSettingsResult{}; }));
    FDreamSettingsCatalogOverride SessionOverride;
    SessionOverride.HiddenSettings.Add(TEXT("Display.VSync"));
    TestTrue(TEXT("Override live catalogue"), ApplyDreamSettingsCatalogOverride(*Live, SessionOverride).IsOk());
    FDreamSettingsSession Session(Live, MakeShared<FCatalogueStore>());
    TestTrue(TEXT("Session opens on the edited catalogue"), Session.Open().IsOk());
    TestTrue(TEXT("Hidden setting still has a value"), Session.GetPending().Contains(FName(TEXT("Display.VSync"))));
    TestTrue(TEXT("Hidden setting still stages"), Session.Stage(TEXT("Display.VSync"), 1).IsOk());
    TestTrue(TEXT("Hidden setting still applies"), Session.Apply().IsOk());
    Session.Close();
    return true;
}
#endif
