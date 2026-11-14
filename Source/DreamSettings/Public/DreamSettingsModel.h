#pragma once

#include "CoreMinimal.h"

enum class EDreamSettingKind : uint8 { Toggle, Scalar, Choice, Key };
enum class EDreamSettingsError : uint8 { None, InvalidDefinition, InvalidValue, Unavailable, Conflict, Backend, Storage, Rollback, Pending };

struct DREAMSETTINGS_API FDreamSettingsResult
{
	EDreamSettingsError Code = EDreamSettingsError::None;
	FText Message;
	bool IsOk() const { return Code == EDreamSettingsError::None; }
	bool IsPending() const { return Code == EDreamSettingsError::Pending; }
	static FDreamSettingsResult Fail(EDreamSettingsError Code, const FText& Message) { return { Code, Message }; }
};

/** Stable token values represent choices, device IDs and keys; labels never enter storage. */
struct DREAMSETTINGS_API FDreamSettingValue
{
	double Number = 0;
	FString Token;
	bool bToken = false;
	FDreamSettingValue() = default;
	FDreamSettingValue(double In) : Number(In) {}
	static FDreamSettingValue Id(FString In) { FDreamSettingValue V; V.bToken = true; V.Token = MoveTemp(In); return V; }
	bool Equals(const FDreamSettingValue& Other) const;
	FString Encode() const;
	static bool Decode(const FString& Text, FDreamSettingValue& Out);
};

struct DREAMSETTINGS_API FDreamSettingOption
{
	FString Id;
	FText Label;
	bool bSelectable = true;
};

using FDreamSettingsValues = TMap<FName, FDreamSettingValue>;

/** Presentation metadata only; never part of a setting's persistent identity. */
struct DREAMSETTINGS_API FDreamSettingsInputContext
{
	FSoftObjectPath Path;
	FText Label;
};

struct DREAMSETTINGS_API FDreamSettingDefinition
{
	FName Id;
	FName Category;
	FText Label;
	FText Description;
	FString InputCategoryId;
	FText InputCategoryLabel;
	TArray<FDreamSettingsInputContext> InputContexts;
	TFunction<FText(const FDreamSettingsValues&)> DynamicDescription;
	EDreamSettingKind Kind = EDreamSettingKind::Scalar;
	FDreamSettingValue Default;
	double Min = 0;
	double Max = 1;
	double Step = 0.01;
	bool bPreview = false;
	bool bRequiresConfirmation = false;
	bool bRequiresRestart = false;
	bool bHidden = false;
	bool bFollowExternalChanges = false;
	bool bRetryUnavailable = false;
	/** Independent non-preview backend, completed before any risky display write. */
	bool bAsyncWrite = false;
	int32 Order = 0;
	TArray<FDreamSettingOption> Options;
	TFunction<TArray<FDreamSettingOption>()> DynamicOptions;
	TArray<FDreamSettingOption> GetOptions() const { return DynamicOptions ? DynamicOptions() : Options; }
	/** Engine sentinel values such as -1 = inherit, outside the ordinary slider range. */
	TMap<double, FText> SpecialValues;
	TArray<FName> Dependencies;
	TFunction<FDreamSettingsResult(const FDreamSettingsValues&)> Condition;
	/** Direct row editing can be disabled while a composite control owns this value. */
	TFunction<FDreamSettingsResult(const FDreamSettingsValues&)> EditCondition;
	/** Composite controls derive their value from ordinary settings and expand edits into a draft. */
	TFunction<FDreamSettingValue(const FDreamSettingsValues&)> Derive;
	TFunction<FDreamSettingsResult(const FDreamSettingValue&, const FDreamSettingsValues&, FDreamSettingsValues&)> Expand;
	/** Related ordinary settings to stage atomically, e.g. upscaler selection and compatible AA. */
	TFunction<FDreamSettingsResult(const FDreamSettingValue&, const FDreamSettingsValues&, FDreamSettingsValues&)> RelatedChanges;
	TFunction<FDreamSettingsResult(const FString&, const FDreamSettingsValues&)> OptionAvailability;
	FDreamSettingsResult Normalize(const FDreamSettingValue& Input, FDreamSettingValue& Output) const;
};

/** Write must read back the effective value and report rejection. No persistence here. */
class DREAMSETTINGS_API IDreamSettingProvider
{
public:
	virtual ~IDreamSettingProvider() = default;
	virtual FDreamSettingsResult Availability() const { return {}; }
	virtual FDreamSettingsResult Read(FDreamSettingValue& Out) const = 0;
	virtual FDreamSettingsResult Write(const FDreamSettingValue& Value) = 0;
	/** Pending writes are polled on the game thread; async providers may only mutate their own value. */
	virtual FDreamSettingsResult PollWrite() { return FDreamSettingsResult::Fail(EDreamSettingsError::Backend, NSLOCTEXT("DreamSettings", "NoPendingWrite", "No asynchronous operation is pending.")); }
	/** Validate asynchronous engine presentation before a dangerous change can become permanent. */
	virtual FDreamSettingsResult ValidateApplied(const FDreamSettingValue& Value) const { return {}; }
};

/** Save is all-or-nothing for this store; failed writes must preserve the previous file. */
struct DREAMSETTINGS_API FDreamSettingsPreparedWrite
{
	FString Filename;
	TArray<uint8> Bytes;
};

class DREAMSETTINGS_API IDreamSettingsStore
{
public:
	virtual ~IDreamSettingsStore() = default;
	virtual FDreamSettingsResult Load(FDreamSettingsValues& Out) = 0;
	virtual FDreamSettingsResult Save(const FDreamSettingsValues& Values) = 0;
	/** Optional staging contract for an atomic commit coordinated with a native input SaveGame. */
	virtual FDreamSettingsResult PrepareSave(const FDreamSettingsValues& Values,FDreamSettingsPreparedWrite& Out)
	{ return FDreamSettingsResult::Fail(EDreamSettingsError::Storage,NSLOCTEXT("DreamSettings","NoPreparedStore","This storage adapter cannot coordinate input and machine settings.")); }
	virtual void NotifyCommitted() {}
	virtual FDreamSettingsResult BeginRiskyChange(const FDreamSettingsValues& SafeValues)
	{ return FDreamSettingsResult::Fail(EDreamSettingsError::Storage, NSLOCTEXT("DreamSettings", "NoRecoveryStore", "The storage adapter does not support safe display changes.")); }
	virtual FDreamSettingsResult LoadRecovery(FDreamSettingsValues& Out) { Out.Reset(); return {}; }
};

class DREAMSETTINGS_API FDreamSettingsRegistry
{
public:
	FDreamSettingsResult Register(FDreamSettingDefinition Definition, TSharedPtr<IDreamSettingProvider> Provider = nullptr);
	FDreamSettingsResult Validate(TArray<FName>& OrderedIds) const;
	const FDreamSettingDefinition* Find(FName Id) const { return Definitions.Find(Id); }
	TSharedPtr<IDreamSettingProvider> Provider(FName Id) const { return Providers.FindRef(Id); }
	const TMap<FName, FDreamSettingDefinition>& All() const { return Definitions; }
private:
	TMap<FName, FDreamSettingDefinition> Definitions;
	TMap<FName, TSharedPtr<IDreamSettingProvider>> Providers;
};

/** One editing transaction. Owner grants an exclusive process-settings lease for its lifetime. */
class DREAMSETTINGS_API FDreamSettingsSession
{
public:
	FDreamSettingsSession(TSharedRef<FDreamSettingsRegistry> Registry, TSharedRef<IDreamSettingsStore> Store);
	~FDreamSettingsSession();
	FDreamSettingsResult Open();
	FDreamSettingsResult Stage(FName Id, FDreamSettingValue Value);
	FDreamSettingsResult StageBatch(const FDreamSettingsValues& Changes);
	FDreamSettingsResult Apply();
	FDreamSettingsResult Confirm();
	FDreamSettingsResult Reject();
	void Tick(double NowSeconds);
	bool IsAwaitingConfirmation() const { return bAwaitingConfirmation; }
	double GetConfirmationSecondsLeft() const;
	FDreamSettingsResult Availability(FName Id, bool bDirectEdit = true) const;
	FDreamSettingsResult ResetOne(FName Id);
	FDreamSettingsResult Revert();
	FDreamSettingsResult Reset();
	/** Stage defaults for one category atomically; unrelated drafts and persisted values are preserved. */
	FDreamSettingsResult ResetCategory(FName Category);
	FDreamSettingsResult Close();
	bool IsDirty() const;
	bool IsOpen() const { return bOpen; }
	bool IsClosing() const { return bCloseRequested; }
	bool IsBusy() const { return !WaitingSetting.IsNone(); }
	const FDreamSettingsValues& GetPending() const { return Pending; }
	const FDreamSettingsValues& GetApplied() const { return Applied; }
	const FDreamSettingsValues& GetPersisted() const { return Persisted; }
	const FDreamSettingsRegistry& GetRegistry() const { return *Registry; }
	const FDreamSettingsResult& GetLastResult() const { return LastResult; }
	DECLARE_MULTICAST_DELEGATE(FChanged);
	FChanged OnChanged;
private:
	FDreamSettingsResult ResetDefaultsInCategory(FName Category);
	FDreamSettingsResult Publish(FDreamSettingsResult Result);
	FDreamSettingsResult Restore(const FDreamSettingsValues& Values);
	FDreamSettingsResult StartRestore(bool bDiscardDraft, FDreamSettingsResult ResultAfter = {});
	FDreamSettingsResult FinishRestore(FDreamSettingsResult Result);
	FDreamSettingsResult SuspendWrite(FName Id, FDreamSettingValue Target, FDreamSettingsResult Result);
	void PollOperation();
	FDreamSettingsResult CheckExternalChanges() const;
	FDreamSettingsResult Commit();
	FDreamSettingsResult CaptureApplied();
	void UpdateDerived(FDreamSettingsValues& Values) const;
	FDreamSettingsValues PersistentValues(const FDreamSettingsValues& Values) const;
	TSharedRef<FDreamSettingsRegistry> Registry;
	TSharedRef<IDreamSettingsStore> Store;
	TArray<FName> Order;
	FDreamSettingsValues Pending, Applied, Persisted, Baseline;
	/** Explicit draft values remain authoritative after another provider changes them as a side effect. */
	TSet<FName> StagedOverrides;
	TSet<FName> UncertainBackends;
	TMap<FName, FDreamSettingsResult> ReadFailures;
	FDreamSettingsResult LastResult;
	bool bOpen = false;
	bool bAwaitingConfirmation = false;
	bool bRecoveryArmed = false;
	bool bApplyInProgress = false;
	bool bRestoring = false;
	bool bDiscardAfterRestore = false;
	bool bCancelRequested = false;
	bool bCloseRequested = false;
	FName WaitingSetting;
	FDreamSettingValue WaitingTarget;
	FDreamSettingsResult AfterRestoreResult, RestoreFailure;
	TSet<FName> RestoreFailedIds;
	double ConfirmationDeadline = 0;
};

namespace DreamSettingIds
{
	DREAMSETTINGS_API extern const FName VSync;
	DREAMSETTINGS_API extern const FName MasterVolume;
}
