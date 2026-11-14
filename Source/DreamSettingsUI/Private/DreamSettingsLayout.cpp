#include "DreamSettingsLayout.h"
#include "DreamSettingsPresentation.h"
#include "DreamSettingsViewModel.h"
#include "Core/DreamUserWidget.h"

UDreamSettingsLayout::UDreamSettingsLayout()
{
    ScreenClass=TSoftClassPtr<UDreamUserWidget>(FSoftObjectPath(TEXT("/DreamSettings/UI/WBP_DreamSettings.WBP_DreamSettings_C")));
}

void UDreamSettingsLayout::ConfigureViewModel(UDreamSettingsViewModel* Presenter) const
{
    if (Presenter)
    {
        Presenter->ConfigureScenePreview(bAllowScenePreview,bHideBackgroundForGraphicsOptions);
        Presenter->ConfigureGameBenchmark(bEnableGameBenchmark,bEnableGameBenchmarkInShipping,GameBenchmarkKeys);
        Presenter->ConfigureDescriptionAutoScroll(bAutoScrollDescriptions);
        Presenter->ConfigureInputGrouping(InputGrouping,InputContextLabels);
        Presenter->ConfigureMenuShortcuts(MenuShortcuts);
    }
}

FDreamSettingsResult UDreamSettingsLayout::Resolve(TSubclassOf<UDreamUserWidget>& OutClass,UDreamSettingsPresentation*& OutPresentation) const
{
    OutClass=nullptr; OutPresentation=nullptr;
    UClass* LoadedClass=ScreenClass.LoadSynchronous();
    if (!LoadedClass || !LoadedClass->IsChildOf(UDreamUserWidget::StaticClass()) || LoadedClass->HasAnyClassFlags(CLASS_Abstract))
        return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,NSLOCTEXT("DreamSettingsLayout","MissingScreen","The selected settings layout could not be loaded."));
    auto* Help=Presentation.IsNull() ? nullptr : Presentation.LoadSynchronous();
    if (!Presentation.IsNull() && !Help)
        return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,NSLOCTEXT("DreamSettingsLayout","MissingHelp","The selected settings help content could not be loaded."));
    OutClass=LoadedClass; OutPresentation=Help;
    return {};
}
