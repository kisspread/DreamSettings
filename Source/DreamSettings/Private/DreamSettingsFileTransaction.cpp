#include "DreamSettingsFileTransaction.h"
#include "DreamSettings.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

#define LOCTEXT_NAMESPACE "DreamSettings"
#if WITH_DEV_AUTOMATION_TESTS
TFunction<void(const FString&,const FString&,bool)> FDreamSettingsFileTransaction::ObserveWriteForAutomation;
#endif
namespace
{
    constexpr int64 MaxFileBytes=64*1024*1024;
    FDreamSettingsResult StorageError()
    { return FDreamSettingsResult::Fail(EDreamSettingsError::Storage,LOCTEXT("TransactionWrite","Unable to commit input and game settings. Previous files have been preserved.")); }
    FDreamSettingsResult RecoveryError()
    { return FDreamSettingsResult::Fail(EDreamSettingsError::Rollback,LOCTEXT("TransactionRecovery","Settings recovery could not finish. The recovery journal has been preserved; resolve the file access problem before saving again.")); }
}
FDreamSettingsFileTransaction::FDreamSettingsFileTransaction(FString InRoot)
{
    Root=FPaths::ConvertRelativePathToFull(InRoot); FPaths::NormalizeDirectoryName(Root);
    FPaths::CollapseRelativeDirectories(Root); Root+=TEXT("/");
}
FString FDreamSettingsFileTransaction::GetJournalFilename() const
{ return Root+TEXT("DreamSettings/SettingsCommit.json"); }
bool FDreamSettingsFileTransaction::Resolve(const FString& Filename,FString& Out) const
{
    Out=FPaths::ConvertRelativePathToFull(Filename); FPaths::NormalizeFilename(Out);
    return FPaths::CollapseRelativeDirectories(Out) && Out.StartsWith(Root,ESearchCase::IgnoreCase)
        && Out.Len()>Root.Len() && !Out.Equals(GetJournalFilename(),ESearchCase::IgnoreCase);
}
bool FDreamSettingsFileTransaction::Replace(const FString& Filename,const TArray<uint8>& Bytes,const FString& Phase)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (AllowWrite && !AllowWrite(Phase)) return false;
    if (ObserveWriteForAutomation) ObserveWriteForAutomation(Phase,Filename,false);
#endif
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
    const FString Temp=Filename+TEXT(".")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".tmp");
    bool bReplaced=false;
    if (FFileHelper::SaveArrayToFile(Bytes,*Temp))
    {
#if PLATFORM_WINDOWS
        bReplaced=::MoveFileExW(*Temp,*Filename,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#endif
    }
    if (!bReplaced) IFileManager::Get().Delete(*Temp,false,false,true);
#if WITH_DEV_AUTOMATION_TESTS
    if (bReplaced && ObserveWriteForAutomation) ObserveWriteForAutomation(Phase,Filename,true);
#endif
    return bReplaced;
}
bool FDreamSettingsFileTransaction::WriteJournal(const TArray<FPreviousFile>& Previous,bool bCommitted)
{
    auto Object=MakeShared<FJsonObject>(); Object->SetNumberField(TEXT("version"),1);
    Object->SetStringField(TEXT("phase"),bCommitted ? TEXT("committed") : TEXT("prepared"));
    TArray<TSharedPtr<FJsonValue>> Files;
    for (const auto& File:Previous)
    {
        auto Entry=MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("path"),File.Filename.RightChop(Root.Len()));
        Entry->SetBoolField(TEXT("existed"),File.bExisted); Entry->SetStringField(TEXT("before"),FBase64::Encode(File.Bytes));
        Files.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Object->SetArrayField(TEXT("files"),Files); FString Json;
    if (!FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Json))) return false;
    FTCHARToUTF8 Utf8(*Json); TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    return Replace(GetJournalFilename(),Bytes,bCommitted ? TEXT("commit") : TEXT("prepare"));
}
FDreamSettingsResult FDreamSettingsFileTransaction::Restore(const TArray<FPreviousFile>& Previous)
{
    bool bOk=true;
    for (const auto& File:Previous)
    {
        if (File.bExisted) bOk=Replace(File.Filename,File.Bytes,TEXT("restore")) && bOk;
        else if (IFileManager::Get().FileExists(*File.Filename))
            bOk=IFileManager::Get().Delete(*File.Filename,true,false,true) && bOk;
    }
    if (!bOk || !IFileManager::Get().Delete(*GetJournalFilename(),false,false,true)) return RecoveryError();
    return {};
}
FDreamSettingsResult FDreamSettingsFileTransaction::Recover()
{
    const FString Journal=GetJournalFilename();
    if (!IFileManager::Get().FileExists(*Journal)) return {};
    if (IFileManager::Get().FileSize(*Journal)>MaxFileBytes*3) return RecoveryError();
    FString Json; TSharedPtr<FJsonObject> Object;
    if (!FFileHelper::LoadFileToString(Json,*Journal) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object) || !Object) return RecoveryError();
    double Version=0; FString Phase; const TArray<TSharedPtr<FJsonValue>>* Files=nullptr;
    if (!Object->TryGetNumberField(TEXT("version"),Version) || Version!=1 || !Object->TryGetStringField(TEXT("phase"),Phase)
        || (Phase!=TEXT("prepared") && Phase!=TEXT("committed")) || !Object->TryGetArrayField(TEXT("files"),Files)
        || Files->IsEmpty() || Files->Num()>8) return RecoveryError();
    TArray<FPreviousFile> Previous; TSet<FString> Seen; int64 Total=0;
    for (const auto& Value:*Files)
    {
        const TSharedPtr<FJsonObject>* Entry=nullptr; FString Relative,Encoded; FPreviousFile File;
        if (!Value || !Value->TryGetObject(Entry) || !Entry || !*Entry
            || !(*Entry)->TryGetStringField(TEXT("path"),Relative) || !FPaths::IsRelative(Relative)
            || !Resolve(Root+Relative,File.Filename) || Seen.Contains(File.Filename.ToLower())
            || !(*Entry)->TryGetBoolField(TEXT("existed"),File.bExisted) || !(*Entry)->TryGetStringField(TEXT("before"),Encoded)
            || !FBase64::Decode(Encoded,File.Bytes) || (!File.bExisted && !File.Bytes.IsEmpty())) return RecoveryError();
        Total+=File.Bytes.Num(); if (Total>MaxFileBytes) return RecoveryError();
        Seen.Add(File.Filename.ToLower()); Previous.Add(MoveTemp(File));
    }
    if (Phase==TEXT("prepared")) return Restore(Previous);
    // The commit marker is durable. Failure to clean it up must not undo a successful commit.
    IFileManager::Get().Delete(*Journal,false,false,true); return {};
}
FDreamSettingsResult FDreamSettingsFileTransaction::Commit(const TArray<FDreamSettingsPreparedWrite>& Writes)
{
    auto Result=Recover(); if (!Result.IsOk()) return Result;
    // Never replace an uncleared journal, including a committed journal held open by another process.
    if (IFileManager::Get().FileExists(*GetJournalFilename()) || Writes.IsEmpty() || Writes.Num()>8) return StorageError();
    TArray<FPreviousFile> Previous; TSet<FString> Seen; int64 Total=0;
    for (const auto& Write:Writes)
    {
        FPreviousFile File;
        if (!Resolve(Write.Filename,File.Filename) || Seen.Contains(File.Filename.ToLower()))
        { UE_LOG(LogDreamSettings,Log,TEXT("Settings commit rejected target '%s' under root '%s' (resolved '%s')."),*Write.Filename,*Root,*File.Filename); return StorageError(); }
        Total+=Write.Bytes.Num(); if (Total>MaxFileBytes) return StorageError();
        Seen.Add(File.Filename.ToLower()); File.bExisted=IFileManager::Get().FileExists(*File.Filename);
        if (File.bExisted)
        {
            const int64 Size=IFileManager::Get().FileSize(*File.Filename);
            if (Size<0 || Size>MaxFileBytes || !FFileHelper::LoadFileToArray(File.Bytes,*File.Filename)) return StorageError();
            Total+=File.Bytes.Num(); if (Total>MaxFileBytes) return StorageError();
        }
        Previous.Add(MoveTemp(File));
    }
    if (!WriteJournal(Previous,false)) return StorageError();
    bool bWritten=true;
    for (int32 Index=0;Index<Writes.Num();++Index)
        if (!Replace(Previous[Index].Filename,Writes[Index].Bytes,TEXT("data"))) { bWritten=false; break; }
    if (bWritten && WriteJournal(Previous,true))
    { IFileManager::Get().Delete(*GetJournalFilename(),false,false,true); return {}; }
    Result=Restore(Previous); return Result.IsOk() ? StorageError() : Result;
}
#undef LOCTEXT_NAMESPACE
