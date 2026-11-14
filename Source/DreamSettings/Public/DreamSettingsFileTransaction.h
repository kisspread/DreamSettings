#pragma once

#include "DreamSettingsModel.h"

/** Coordinates native input SaveGame and INI replacement. Recover before either is loaded. */
class DREAMSETTINGS_API FDreamSettingsFileTransaction
{
public:
    explicit FDreamSettingsFileTransaction(FString InRoot);
    FDreamSettingsResult Commit(const TArray<FDreamSettingsPreparedWrite>& Writes);
    FDreamSettingsResult Recover();
    FString GetJournalFilename() const;
#if WITH_DEV_AUTOMATION_TESTS
    /** Failure injection occurs before the named write; never used by runtime callers. */
    TFunction<bool(const FString&)> AllowWrite;
    /** Dedicated child-process crash tests only. After=true means the atomic replace succeeded.
     * Never bound by runtime callers; absent from builds without development automation. */
    static TFunction<void(const FString& Phase,const FString& Filename,bool After)> ObserveWriteForAutomation;
#endif
private:
    struct FPreviousFile { FString Filename; bool bExisted=false; TArray<uint8> Bytes; };
    bool Resolve(const FString& Filename,FString& Out) const;
    bool Replace(const FString& Filename,const TArray<uint8>& Bytes,const FString& Phase);
    bool WriteJournal(const TArray<FPreviousFile>& Previous,bool bCommitted);
    FDreamSettingsResult Restore(const TArray<FPreviousFile>& Previous);
    FString Root;
};
