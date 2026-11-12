#include "DreamSettings.h"

DEFINE_LOG_CATEGORY(LogDreamSettings);

class FDreamSettingsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDreamSettings, Log, TEXT("DreamSettings scaffold - no settings implemented yet"));
	}
};

IMPLEMENT_MODULE(FDreamSettingsModule, DreamSettings);
