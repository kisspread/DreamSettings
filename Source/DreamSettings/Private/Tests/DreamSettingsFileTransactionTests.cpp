#if WITH_DEV_AUTOMATION_TESTS
#include "DreamSettingsFileTransaction.h"
#include "DreamSettingsIniStore.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFileTransactionTest,"DreamSettings.Core.FileTransaction",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamFileTransactionTest::RunTest(const FString&)
{
    FDreamSettingsIniStore NativeStore(GGameUserSettingsIni,TEXT("/Script/Engine.GameUserSettings"));
    FDreamSettingsPreparedWrite NativePrepared;
    TestTrue(TEXT("Native cache key resolves before preparing"),NativeStore.PrepareSave({},NativePrepared).IsOk());
    TestEqual(TEXT("Native target is a disk INI, not the cache key"),FPaths::GetCleanFilename(NativePrepared.Filename),FString(TEXT("GameUserSettings.ini")));
    TestTrue(TEXT("Native target stays in project saved directory"),FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(NativePrepared.Filename),FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())));
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString Ini=Root/TEXT("Settings.ini"), Sav=Root/TEXT("SaveGames/Input.sav");
    FDreamSettingsFileTransaction Transaction(Root);
    const TArray<uint8> OldIni={1,2,3},NewIni={4,5},OldSav={8,9},NewSav={10,11,12};
    auto Read=[](const FString& Path) { TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes,*Path); return Bytes; };
    TArray<FDreamSettingsPreparedWrite> Writes={{Ini,OldIni},{Sav,OldSav}};
    TestTrue(TEXT("First coordinated save"),Transaction.Commit(Writes).IsOk());
    Writes={{Ini,NewIni},{Sav,NewSav}};
    int32 DataWrites=0;
    Transaction.AllowWrite=[&](const FString& Phase) { return Phase!=TEXT("data") || ++DataWrites!=2; };
    TestEqual(TEXT("Second file failure is reported"),Transaction.Commit(Writes).Code,EDreamSettingsError::Storage);
    TestTrue(TEXT("First file rolled back"),Read(Ini)==OldIni);
    TestTrue(TEXT("Second file preserved"),Read(Sav)==OldSav);
    TestFalse(TEXT("Successful rollback clears journal"),IFileManager::Get().FileExists(*Transaction.GetJournalFilename()));
    Transaction.AllowWrite=[](const FString& Phase) { return Phase!=TEXT("commit"); };
    TestFalse(TEXT("Commit marker failure rolls both files back"),Transaction.Commit(Writes).IsOk());
    TestTrue(TEXT("Marker failure preserves both original files"),Read(Ini)==OldIni && Read(Sav)==OldSav);
    DataWrites=0;
    Transaction.AllowWrite=[&](const FString& Phase) { return Phase!=TEXT("restore") && (Phase!=TEXT("data") || ++DataWrites!=2); };
    TestEqual(TEXT("Rollback failure is distinguished"),Transaction.Commit(Writes).Code,EDreamSettingsError::Rollback);
    TestTrue(TEXT("Partial write and recovery journal remain"),Read(Ini)==NewIni && IFileManager::Get().FileExists(*Transaction.GetJournalFilename()));
    FDreamSettingsFileTransaction Restart(Root);
    TestTrue(TEXT("New transaction instance recovers interrupted write"),Restart.Recover().IsOk());
    TestTrue(TEXT("Recovery restores exact native and INI bytes"),Read(Ini)==OldIni && Read(Sav)==OldSav);
    // Simulate a crash after the durable commit marker but before journal cleanup.
    const FString Committed=TEXT("{\"version\":1,\"phase\":\"committed\",\"files\":[{\"path\":\"Settings.ini\",\"existed\":true,\"before\":\"AA==\"}]}");
    FFileHelper::SaveStringToFile(Committed,*Restart.GetJournalFilename());
    TestTrue(TEXT("Committed journal is cleaned without undo"),Restart.Recover().IsOk() && Read(Ini)==OldIni);
    const FString Corrupt=TEXT("{\"version\":1,\"phase\":\"prepared\",\"files\":[{\"path\":\"../outside.ini\",\"existed\":true,\"before\":\"AA==\"}]}");
    FFileHelper::SaveStringToFile(Corrupt,*Restart.GetJournalFilename());
    TestFalse(TEXT("Traversal journal cannot restore outside saved root"),Restart.Recover().IsOk());
    TestTrue(TEXT("Invalid journal preserved for diagnosis"),IFileManager::Get().FileExists(*Restart.GetJournalFilename()));
    IFileManager::Get().Delete(*Restart.GetJournalFilename());
    TestFalse(TEXT("Out of root commit rejected"),Restart.Commit({{Root/TEXT("../outside.ini"),NewIni}}).IsOk());
    TestFalse(TEXT("Duplicate targets rejected"),Restart.Commit({{Ini,NewIni},{Ini.ToUpper(),NewSav}}).IsOk());
    TestTrue(TEXT("Valid final commit"),Restart.Commit(Writes).IsOk());
    TestTrue(TEXT("Final pair agrees"),Read(Ini)==NewIni && Read(Sav)==NewSav);
    IFileManager::Get().Delete(*Ini); IFileManager::Get().Delete(*Sav);
    // Restore must remove a file that did not exist before the interrupted transaction.
    DataWrites=0;
    Transaction.AllowWrite=[&](const FString& Phase) { return Phase!=TEXT("data") || ++DataWrites!=2; };
    TestFalse(TEXT("First-time save fails on second file"),Transaction.Commit(Writes).IsOk());
    TestFalse(TEXT("First-time rollback removes newly created file"),IFileManager::Get().FileExists(*Ini));
    TestFalse(TEXT("First-time rollback does not create input file"),IFileManager::Get().FileExists(*Sav));
    // The adapter prepares valid UTF-8 without changing disk; coordinated commit then synchronizes it.
    FDreamSettingsIniStore Store(Ini,TEXT("/Script/Engine.GameUserSettings"));
    FDreamSettingsValues Values; Values.Add(TEXT("Input.Test"),FDreamSettingValue::Id(TEXT("中文")));
    FDreamSettingsPreparedWrite Prepared;
    TestTrue(TEXT("Prepare INI succeeds"),Store.PrepareSave(Values,Prepared).IsOk());
    TestFalse(TEXT("Prepare has no disk side effects"),IFileManager::Get().FileExists(*Ini));
    TestTrue(TEXT("Prepared INI participates in coordinated save"),Restart.Commit({Prepared,{Sav,NewSav}}).IsOk());
    Store.NotifyCommitted(); FDreamSettingsValues Loaded; Store.Load(Loaded);
    const auto* LoadedValue=Loaded.Find(TEXT("Input.Test"));
    TestTrue(TEXT("Unicode preferences round trip"),LoadedValue && LoadedValue->Token==TEXT("中文"));
    // Delete only explicitly created test files and empty directories.
    IFileManager::Get().Delete(*Ini); IFileManager::Get().Delete(*Sav);
    IFileManager::Get().DeleteDirectory(*(Root/TEXT("SaveGames")),false,false);
    IFileManager::Get().DeleteDirectory(*(Root/TEXT("DreamSettings")),false,false);
    IFileManager::Get().DeleteDirectory(*Root,false,false);
    return true;
}
#endif
