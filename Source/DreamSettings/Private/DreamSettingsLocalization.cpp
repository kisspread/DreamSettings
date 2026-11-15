#include "DreamSettingsLocalization.h"
#include "DreamSettingsConfiguration.h"
#include "DreamSettingsProvider.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/TextLocalizationManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
namespace
{
#if WITH_EDITOR
    // Game text is process-wide even with multiple PIE GameInstances. Share the preview
    // and restore it only when the final provider is released (in either teardown order).
    struct FPIELanguagePreview
    {
        bool bWasEnabled = FTextLocalizationManager::Get().IsGameLocalizationPreviewEnabled();
        FString Previous = FTextLocalizationManager::Get().GetConfiguredGameLocalizationPreviewLanguage();
        FString Current = bWasEnabled ? Previous : FTextLocalizationManager::Get().GetNativeCultureName(ELocalizedTextSourceCategory::Game);
        ~FPIELanguagePreview()
        {
            auto& Manager = FTextLocalizationManager::Get();
            if (bWasEnabled) Manager.EnableGameLocalizationPreview(Previous);
            else Manager.DisableGameLocalizationPreview();
            Manager.WaitForAsyncTasks();
        }
    };
    TWeakPtr<FPIELanguagePreview> SharedPIEPreview;
#endif
    struct FLanguageState
    {
        TWeakObjectPtr<UGameInstance> Instance;
        TArray<FString> Cultures;
        FString ResourceRoot;
#if WITH_EDITOR
        TSharedPtr<FPIELanguagePreview> Preview;
#endif
        FString CurrentLanguage() const
        {
#if WITH_EDITOR
            if (Preview) return Preview->Current;
#endif
            return FInternationalization::Get().GetCurrentLanguage()->GetName();
        }
        FDreamSettingsResult Availability() const
        {
            const auto* World=Instance.IsValid() ? Instance->GetWorld() : nullptr;
            if (!World || (World->WorldType!=EWorldType::Game && World->WorldType!=EWorldType::PIE))
                return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("LanguageGameRequired","Start the game to change its language."));
            bool bMissingNativePreviewResources=false;
#if WITH_EDITOR
            bMissingNativePreviewResources=Preview && FTextLocalizationManager::Get().GetNativeCultureName(ELocalizedTextSourceCategory::Game).IsEmpty();
#endif
            // An incompletely localized host can still open every other setting.
            // Report this on the language row instead of aborting catalogue registration.
            if (bMissingNativePreviewResources)
                return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,
                    LOCTEXT("PreviewNativeLanguageMissing", "Install the game's native localization resources to preview languages in PIE."));
            return {};
        }
        FString Match(const FString& Name) const
        {
            for (const auto& Candidate:FInternationalization::Get().GetPrioritizedCultureNames(Name))
                if (Cultures.Contains(Candidate)) return Candidate;
            return {};
        }
        FDreamSettingsResult CanSelect(const FString& Name) const
        {
            if (!Cultures.Contains(Name) || !FInternationalization::Get().GetCulture(Name).IsValid())
                return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("LanguageUnsupported","This language is not supported by this game."));
            for (const auto& Candidate:FInternationalization::Get().GetPrioritizedCultureNames(Name))
            {
                if (Candidate==TEXT("en")) return {}; // The native source text is always available.
                if (!ResourceRoot.IsEmpty() && IFileManager::Get().FileExists(*(ResourceRoot/Candidate/TEXT("DreamSettings.locres")))) return {};
            }
            return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("LanguageFilesMissing","The language files for this selection are not installed."));
        }
        FDreamSettingsResult Read(FDreamSettingValue& Out) const
        {
            auto Result=Availability(); if (!Result.IsOk()) return Result;
            const FString Name=Match(CurrentLanguage());
            if (Name.IsEmpty()) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("ExternalLanguage","Another system selected a language outside this game's supported list."));
            Out=FDreamSettingValue::Id(Name); return {};
        }
        FDreamSettingsResult Write(const FDreamSettingValue& Value)
        {
            auto Result=Availability(); if (!Result.IsOk()) return Result;
            if (!Value.bToken) return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue,LOCTEXT("LanguageIdentifier","A language identifier is required."));
            Result=CanSelect(Value.Token); if (!Result.IsOk()) return Result;
#if WITH_EDITOR
            if (Preview)
            {
                auto& Manager = FTextLocalizationManager::Get();
                Manager.EnableGameLocalizationPreview(Value.Token);
                // Loading is asynchronous in UE 5.8. Complete it before session readback
                // and UI refresh, so Apply/Cancel cannot report success with stale text.
                Manager.WaitForAsyncTasks();
                Preview->Current = Value.Token;
                return {};
            }
#endif
            auto& I18n=FInternationalization::Get();
            if (I18n.GetCurrentLanguage()->GetName()!=Value.Token && !I18n.SetCurrentLanguage(Value.Token))
                return FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("LanguageRejected","The game could not switch to this language."));
            return Match(I18n.GetCurrentLanguage()->GetName())==Value.Token ? FDreamSettingsResult{} :
                FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("LanguageReadback","The selected language was not activated."));
        }
    };
}
FDreamSettingsResult RegisterDreamLocalizationSettings(FDreamSettingsRegistry& Registry,UGameInstance* Instance)
{
    auto State=MakeShared<FLanguageState>(); State->Instance=Instance;
#if WITH_EDITOR
    if (Instance && Instance->GetWorld() && Instance->GetWorld()->WorldType == EWorldType::PIE)
    {
        State->Preview = SharedPIEPreview.Pin();
        if (!State->Preview) { State->Preview = MakeShared<FPIELanguagePreview>(); SharedPIEPreview = State->Preview; }
    }
#endif
    for (const auto& Name:GetDefault<UDreamSettingsConfiguration>()->SupportedCultures)
        if (const auto Culture=FInternationalization::Get().GetCulture(Name)) State->Cultures.AddUnique(Culture->GetName());
    if (State->Cultures.IsEmpty()) return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidDefinition,LOCTEXT("NoSupportedLanguages","Configure at least one valid supported language."));
    if (const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("DreamSettings")))
        State->ResourceRoot=Plugin->GetContentDir()/TEXT("Localization/DreamSettings");
    FString Initial=State->Match(State->CurrentLanguage());
    if (Initial.IsEmpty() || !State->CanSelect(Initial).IsOk())
        for (const auto& Name:State->Cultures) if (State->CanSelect(Name).IsOk()) { Initial=Name; break; }
    if (!Initial.IsEmpty() && State->Availability().IsOk())
    { const auto Result=State->Write(FDreamSettingValue::Id(Initial)); if (!Result.IsOk()) return Result; }
    FDreamSettingDefinition D; D.Id=TEXT("Localization.Culture"); D.Category=TEXT("Localization"); D.Order=600;
    D.Label=LOCTEXT("InterfaceLanguage","Language");
    D.Description=LOCTEXT("InterfaceLanguageHelp","Change the language used for game text. Preview the change before applying it.");
    D.Kind=EDreamSettingKind::Choice; D.bPreview=true; D.bRetryUnavailable=true;
    D.Default=FDreamSettingValue::Id(Initial.IsEmpty() ? State->Cultures[0] : Initial);
    for (const auto& Name:State->Cultures)
    { const auto Culture=FInternationalization::Get().GetCulture(Name); D.Options.Add({Name,FText::FromString(Culture->GetNativeName())}); }
    D.OptionAvailability=[State](const FString& Name,const FDreamSettingsValues&) { return State->CanSelect(Name); };
    return Registry.Register(D,MakeShared<FDreamCallbackSettingsProvider>(
        [State](FDreamSettingValue& Out) { return State->Read(Out); },
        [State](const FDreamSettingValue& Value) { return State->Write(Value); },
        [State] { return State->Availability(); }));
}
#undef LOCTEXT_NAMESPACE
