#include "DreamSettingsIniStore.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "UObject/UnrealType.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

#define LOCTEXT_NAMESPACE "DreamSettings"
namespace
{
    const TCHAR* PreferencesSection = TEXT("DreamSettings.Preferences");
    const TCHAR* RecoverySection = TEXT("DreamSettings.DisplayRecovery");
    const TCHAR* NativeKeys[] = {TEXT("Version"),TEXT("bUseVSync"), TEXT("ResolutionSizeX"), TEXT("ResolutionSizeY"),
        TEXT("LastUserConfirmedResolutionSizeX"), TEXT("LastUserConfirmedResolutionSizeY"),
        TEXT("FullscreenMode"), TEXT("LastConfirmedFullscreenMode"), TEXT("FrameRateLimit"),
        TEXT("bUseHDRDisplayOutput"),TEXT("bUseHDRDisplayCalibration"),TEXT("HDRDisplayOutputNits"),TEXT("HDRPaperWhiteNits")};

    void PreserveNativeVersion(FConfigFile& File,const FString& Section)
    {
        const auto* Native=GEngine ? GEngine->GetGameUserSettings() : nullptr;
        if (!Native || Native->GetClass()->GetPathName()!=Section) return;
        // ValidateSettings has already established the native object's version by menu startup.
        // Preserve that actual value (including host overrides), not a hard-coded engine version.
        // Without it a first-ever atomic save is deleted by UE on the next launch.
        if (const auto* Property=FindFProperty<FUInt32Property>(Native->GetClass(),TEXT("Version")))
            File.SetString(*Section,TEXT("Version"),*FString::Printf(TEXT("%u"),Property->GetPropertyValue_InContainer(Native)));
    }

    FDreamSettingsResult DecodeSection(const FConfigFile& File, const TCHAR* SectionName, FDreamSettingsValues& Out)
    {
        Out.Reset();
        if (const auto* Section = File.FindSection(SectionName))
        {
            for (const auto& Pair : *Section)
            {
                if (Pair.Key == TEXT("SchemaVersion")) continue;
                FDreamSettingValue Value;
                if (!FDreamSettingValue::Decode(Pair.Value.GetValue(), Value))
                    return FDreamSettingsResult::Fail(EDreamSettingsError::Storage, LOCTEXT("Corrupt", "A saved setting is invalid. The existing file has been preserved."));
                Out.Add(Pair.Key, Value);
            }
        }
        return {};
    }
}
FDreamSettingsIniStore::FDreamSettingsIniStore(FString InFilename, FString GameUserSettingsSection)
    : Filename(InFilename), CacheKey(InFilename), NativeSection(MoveTemp(GameUserSettingsSection))
{
    // UE 5.8 globals are cache keys ("GameUserSettings"), not filesystem paths.
    if (GConfig)
        if (const auto* Branch=GConfig->FindBranch(NAME_None,CacheKey))
            Filename=Branch->IniPath;
    if (CacheKey==GGameUserSettingsIni && FPaths::GetExtension(Filename).IsEmpty()) Filename.Reset();
}

FDreamSettingsResult FDreamSettingsIniStore::Load(FDreamSettingsValues& Out)
{
    Out.Reset();
    if (Filename.IsEmpty()) return FDreamSettingsResult::Fail(EDreamSettingsError::Storage,LOCTEXT("NativeIniUnavailable","The engine's settings file path is not available yet."));
    if (!IFileManager::Get().FileExists(*Filename)) return {};
    FConfigFile File;
    FString Contents;
    if (!FFileHelper::LoadFileToString(Contents, *Filename))
        return FDreamSettingsResult::Fail(EDreamSettingsError::Storage, LOCTEXT("ReadFailed", "Unable to read the settings file."));
    File.ProcessInputFileContents(Contents, Filename);
    FString VersionText;
    if (File.GetString(PreferencesSection, TEXT("SchemaVersion"), VersionText))
    {
        FDreamSettingValue Version;
        if (!FDreamSettingValue::Decode(VersionText, Version) || Version.bToken || Version.Number < 0
            || Version.Number > 2 || Version.Number != FMath::FloorToDouble(Version.Number))
            return FDreamSettingsResult::Fail(EDreamSettingsError::Storage, LOCTEXT("Schema", "This settings file uses an unsupported version."));
    }
    auto Result = DecodeSection(File, PreferencesSection, Out);
    if (!Result.IsOk()) return Result;
    bool bVSync;
    if (File.GetBool(*NativeSection, TEXT("bUseVSync"), bVSync)) Out.Add(DreamSettingIds::VSync, bVSync ? 1 : 0);
    bool HDRValue;
    if (File.GetBool(*NativeSection,TEXT("bUseHDRDisplayOutput"),HDRValue)) Out.Add(TEXT("Display.HDR"),HDRValue ? 1 : 0);
    if (File.GetBool(*NativeSection,TEXT("bUseHDRDisplayCalibration"),HDRValue)) Out.Add(TEXT("Display.HDRCalibration"),HDRValue ? 1 : 0);
    double HDRNumber;
    if (File.GetDouble(*NativeSection,TEXT("HDRDisplayOutputNits"),HDRNumber)) Out.Add(TEXT("Display.HDRPeakNits"),HDRNumber);
    if (File.GetDouble(*NativeSection,TEXT("HDRPaperWhiteNits"),HDRNumber)) Out.Add(TEXT("Display.HDRPaperWhite"),HDRNumber);
    return {};
}

FDreamSettingsResult FDreamSettingsIniStore::Save(const FDreamSettingsValues& Values)
{
    FConfigFile File;
    auto Result=BuildSave(Values,File);
    if (Result.IsOk()) Result=WriteFile(File);
    if (Result.IsOk()) NotifyCommitted();
    return Result;
}
FDreamSettingsResult FDreamSettingsIniStore::PrepareSave(const FDreamSettingsValues& Values,FDreamSettingsPreparedWrite& Out)
{
    FConfigFile File; auto Result=BuildSave(Values,File); if (!Result.IsOk()) return Result;
    FString Text; File.WriteToString(Text); FTCHARToUTF8 Utf8(*Text);
    Out.Filename=Filename; Out.Bytes.Reset(); Out.Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    return {};
}
void FDreamSettingsIniStore::NotifyCommitted()
{
    FConfigFile File; File.Read(Filename); SynchronizeCache(File);
    if (CacheKey==GGameUserSettingsIni && GEngine && GEngine->GetGameUserSettings()) GEngine->GetGameUserSettings()->ConfirmVideoMode();
}
FDreamSettingsResult FDreamSettingsIniStore::BuildSave(const FDreamSettingsValues& Values,FConfigFile& File)
{
    FDreamSettingsValues Existing;
    auto Result = Load(Existing);
    if (!Result.IsOk()) return Result;
    if (IFileManager::Get().FileExists(*Filename)) File.Read(Filename);
    // This is a complete atomic snapshot, not an engine hierarchy/diff save. UE may load
    // an INI with SectionsToSave filtering enabled; dropping plugin or unrelated sections
    // would otherwise report success while silently losing persisted preferences.
    File.bCanSaveAllSections=true;
    PreserveNativeVersion(File,NativeSection);
    File.SetString(PreferencesSection, TEXT("SchemaVersion"), TEXT("2"));
    for (const auto& Pair : Values)
    {
        if (!Pair.Value.bToken && !FMath::IsFinite(Pair.Value.Number))
            return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue, LOCTEXT("Finite", "Settings must contain finite values."));
        File.SetString(PreferencesSection, *Pair.Key.ToString(), *Pair.Value.Encode());
        if (Pair.Key == DreamSettingIds::VSync)
            File.SetBool(*NativeSection, TEXT("bUseVSync"), Pair.Value.Number != 0);
        else if (Pair.Key == TEXT("Display.Resolution"))
        {
            FString X, Y;
            if (!Pair.Value.bToken || !Pair.Value.Token.Split(TEXT("x"), &X, &Y))
                return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue, LOCTEXT("ResolutionFormat", "Invalid resolution."));
            File.SetString(*NativeSection, TEXT("ResolutionSizeX"), *X);
            File.SetString(*NativeSection, TEXT("ResolutionSizeY"), *Y);
            File.SetString(*NativeSection, TEXT("LastUserConfirmedResolutionSizeX"), *X);
            File.SetString(*NativeSection, TEXT("LastUserConfirmedResolutionSizeY"), *Y);
        }
        else if (Pair.Key == TEXT("Display.WindowMode"))
        {
            const FString Mode = Pair.Value.Token == TEXT("fullscreen") ? TEXT("0") : Pair.Value.Token == TEXT("borderless") ? TEXT("1") : TEXT("2");
            File.SetString(*NativeSection, TEXT("FullscreenMode"), *Mode);
            File.SetString(*NativeSection, TEXT("LastConfirmedFullscreenMode"), *Mode);
        }
        else if (Pair.Key == TEXT("Display.FrameRateLimit"))
            File.SetString(*NativeSection, TEXT("FrameRateLimit"), *Pair.Value.Encode());
        else if (Pair.Key==TEXT("Display.HDR")) File.SetBool(*NativeSection,TEXT("bUseHDRDisplayOutput"),Pair.Value.Number!=0);
        else if (Pair.Key==TEXT("Display.HDRCalibration")) File.SetBool(*NativeSection,TEXT("bUseHDRDisplayCalibration"),Pair.Value.Number!=0);
        else if (Pair.Key==TEXT("Display.HDRPeakNits")) File.SetString(*NativeSection,TEXT("HDRDisplayOutputNits"),*Pair.Value.Encode());
        else if (Pair.Key==TEXT("Display.HDRPaperWhite")) File.SetString(*NativeSection,TEXT("HDRPaperWhiteNits"),*Pair.Value.Encode());
    }
    File.Remove(RecoverySection);
    return {};
}

FDreamSettingsResult FDreamSettingsIniStore::BeginRiskyChange(const FDreamSettingsValues& SafeValues)
{
    FDreamSettingsValues Existing;
    auto Result = Load(Existing);
    if (!Result.IsOk()) return Result;
    FConfigFile File;
    if (IFileManager::Get().FileExists(*Filename)) File.Read(Filename);
    File.bCanSaveAllSections=true;
    PreserveNativeVersion(File,NativeSection);
    File.Remove(RecoverySection);
    for (const auto& Pair : SafeValues) File.SetString(RecoverySection, *Pair.Key.ToString(), *Pair.Value.Encode());
    return WriteFile(File);
}

FDreamSettingsResult FDreamSettingsIniStore::LoadRecovery(FDreamSettingsValues& Out)
{
    FDreamSettingsValues Existing;
    auto Result = Load(Existing);
    if (!Result.IsOk()) return Result;
    FConfigFile File;
    if (IFileManager::Get().FileExists(*Filename)) File.Read(Filename);
    return DecodeSection(File, RecoverySection, Out);
}

FDreamSettingsResult FDreamSettingsIniStore::WriteFile(FConfigFile& File)
{
    FString Text;
    File.WriteToString(Text);
    const FString Temporary = Filename + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    bool bReplaced = false;
    if (FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
#if PLATFORM_WINDOWS
        bReplaced = ::MoveFileExW(*FPaths::ConvertRelativePathToFull(Temporary), *FPaths::ConvertRelativePathToFull(Filename),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#endif
    }
    if (!bReplaced)
    {
        IFileManager::Get().Delete(*Temporary, false, false, true);
        return FDreamSettingsResult::Fail(EDreamSettingsError::Storage, LOCTEXT("WriteFailed", "Unable to save settings. Changes have not been committed."));
    }
    SynchronizeCache(File);
    return {};
}
void FDreamSettingsIniStore::SynchronizeCache(const FConfigFile& File)
{
    if (GConfig && CacheKey == GGameUserSettingsIni)
    {
        for (const TCHAR* SectionName : {PreferencesSection, RecoverySection})
        {
            // EmptySection flushes the branch while the section is empty, overwriting the
            // just-committed file outside our transaction. Update the in-memory command stream
            // by key instead, and let a later native flush see the complete committed snapshot.
            TArray<FName> ExistingKeys;
            if (const auto* Existing=GConfig->GetSection(SectionName,false,CacheKey))
                for (const auto& Pair:*Existing) ExistingKeys.AddUnique(Pair.Key);
            for (FName Key:ExistingKeys) GConfig->RemoveKeyFromSection(SectionName,Key,CacheKey);
            if (const auto* Section = File.FindSection(SectionName))
                for (const auto& Pair : *Section) GConfig->SetString(SectionName, *Pair.Key.ToString(), *Pair.Value.GetValue(), CacheKey);
        }
        for (const TCHAR* Key : NativeKeys)
        {
            FString Value;
            if (File.GetString(*NativeSection, Key, Value)) GConfig->SetString(*NativeSection, Key, *Value, CacheKey);
        }
    }
}
#undef LOCTEXT_NAMESPACE
