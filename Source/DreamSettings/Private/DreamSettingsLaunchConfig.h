#pragma once
#include "CoreMinimal.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

// UE 5.8's editor launcher substitutes a config cache key as though it were a
// filename (PlayLevelNewProcess.cpp). Restrict this compatibility fix to that
// exact generated shorthand; a host's explicit override must remain untouched.
inline bool NormalizeDreamEditorSettingsOverride(FString& CommandLine, const FString& GeneratedPlatformDir)
{
    FString Override;
    if (!FParse::Param(*CommandLine,TEXT("PIEVIACONSOLE")) ||
        !FParse::Value(*CommandLine,TEXT("GameUserSettingsINI="),Override)) return false;
    const FString Prefix=TEXT("PIEGameUserSettings");
    if (!Override.StartsWith(Prefix)) return false;
    const FString Index=Override.Mid(Prefix.Len());
    if (Index.IsEmpty()) return false;
    for (TCHAR Character:Index) if (Character<TEXT('0') || Character>TEXT('9')) return false;
    const FString Filename=FPaths::ConvertRelativePathToFull(GeneratedPlatformDir/(Override+TEXT(".ini")));
    const int32 Start=CommandLine.Find(TEXT("GameUserSettingsINI="),ESearchCase::IgnoreCase);
    const int32 ValueStart=Start+FCString::Strlen(TEXT("GameUserSettingsINI="));
    const int32 OldLength=Override.Len()+(CommandLine[ValueStart]==TEXT('"') ? 2 : 0);
    CommandLine=CommandLine.Left(ValueStart)+TEXT("\"")+Filename+TEXT("\"")+CommandLine.Mid(ValueStart+OldLength);
    return true;
}
