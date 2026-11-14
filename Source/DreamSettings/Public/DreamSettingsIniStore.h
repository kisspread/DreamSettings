#pragma once
#include "DreamSettingsModel.h"
class FConfigFile;

/** One atomic INI replacement: native VSync plus versioned plugin preferences. Preserves unrelated sections. */
class DREAMSETTINGS_API FDreamSettingsIniStore : public IDreamSettingsStore
{
public:
	FDreamSettingsIniStore(FString Filename, FString GameUserSettingsSection);
	virtual FDreamSettingsResult Load(FDreamSettingsValues& Out) override;
	virtual FDreamSettingsResult Save(const FDreamSettingsValues& Values) override;
	virtual FDreamSettingsResult PrepareSave(const FDreamSettingsValues& Values,FDreamSettingsPreparedWrite& Out) override;
	virtual void NotifyCommitted() override;
	virtual FDreamSettingsResult BeginRiskyChange(const FDreamSettingsValues& SafeValues) override;
	virtual FDreamSettingsResult LoadRecovery(FDreamSettingsValues& Out) override;
private:
	FDreamSettingsResult BuildSave(const FDreamSettingsValues& Values,FConfigFile& File);
	void SynchronizeCache(const FConfigFile& File);
	FDreamSettingsResult WriteFile(FConfigFile& File);
	FString Filename;
	FString CacheKey;
	FString NativeSection;
};
