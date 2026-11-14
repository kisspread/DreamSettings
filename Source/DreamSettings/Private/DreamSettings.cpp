#include "DreamSettings.h"
#include "DreamSettingsFileTransaction.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogDreamSettings);

class FDreamSettingsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDreamSettings, Log, TEXT("DreamSettings runtime loaded"));
		// PostConfigInit precedes native resolution preloading and GameUserSettings initialization.
		// Recovering only when GameInstance is created restores disk too late: the engine has
		// already loaded/applied a partially committed INI while input loads the old SaveGame.
		FDreamSettingsFileTransaction Transaction(FPaths::ProjectSavedDir());
		if (!IFileManager::Get().FileExists(*Transaction.GetJournalFilename())) return;
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
			UE_LOG(LogDreamSettings,Error,TEXT("Could not reload game settings after early transaction recovery."));
		}
		else
		{
			UE_LOG(LogDreamSettings,Log,TEXT("Recovered settings transaction before native startup settings loaded."));
		}
	}
};

IMPLEMENT_MODULE(FDreamSettingsModule, DreamSettings);
