#include "DreamSettings.h"
#include "DreamSettingsFileTransaction.h"
#include "DreamSettingsLaunchConfig.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformProperties.h"

DEFINE_LOG_CATEGORY(LogDreamSettings);

class FDreamSettingsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDreamSettings, Log, TEXT("DreamSettings runtime loaded"));
		FString CommandLine=FCommandLine::Get();
		const bool bNormalized=NormalizeDreamEditorSettingsOverride(CommandLine,
			FPaths::GeneratedConfigDir()/FPlatformProperties::PlatformName());
		if (bNormalized)
		{
			FCommandLine::Set(*CommandLine);
			// Keep the known branch identity while changing its destination before reload.
			// Otherwise FindBranch rejects the new .ini path as a different known file.
			if (auto* Branch=GConfig->FindBranch(NAME_None,GGameUserSettingsIni))
				FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),Branch->IniPath);
			UE_LOG(LogDreamSettings,Log,TEXT("Resolved editor Standalone settings override into the project's generated config directory."));
		}
		// PostConfigInit precedes native resolution preloading and GameUserSettings initialization.
		// Recovering only when GameInstance is created restores disk too late: the engine has
		// already loaded/applied a partially committed INI while input loads the old SaveGame.
		FDreamSettingsFileTransaction Transaction(FPaths::ProjectSavedDir());
		const bool bRecovery=IFileManager::Get().FileExists(*Transaction.GetJournalFilename());
		if (!bRecovery && !bNormalized) return;
		const auto Result=Transaction.Recover();
		if (!Result.IsOk())
		{
			UE_LOG(LogDreamSettings,Error,TEXT("Early settings recovery: %s"),*Result.Message.ToString());
			return; // Leave the journal intact; the subsystem also retries and reports the failure.
		}
		// Config may already have cached the new file. Use the same reload path as the native
		// settings class; this does not call SaveSettings or apply a display mode.
		FConfigContext Context=FConfigContext::ReadIntoGConfig();
		Context.bForceReload=true;
		if (!Context.Load(TEXT("GameUserSettings"),GGameUserSettingsIni))
		{
			UE_LOG(LogDreamSettings,Error,TEXT("Could not reload game settings after startup configuration preparation."));
		}
		else
		{
			UE_LOG(LogDreamSettings,Log,TEXT("Prepared settings configuration before native startup settings loaded."));
		}
	}
};

IMPLEMENT_MODULE(FDreamSettingsModule, DreamSettings);
