#pragma once

#include "CoreMinimal.h"

enum class EDreamSettingKind : uint8 { Toggle, Scalar, Choice, Key };
enum class EDreamSettingsError : uint8 { None, InvalidDefinition, InvalidValue, Unavailable, Conflict, Backend, Storage, Rollback, Pending };

/** Row presentation the definition asks for; providers and saved values never depend on it. */
enum class EDreamSettingPresentation : uint8
{
	None = 0,
	/** Scalar shown as a percentage and parsed as one. Audio volumes and interface scale use it. */
	Percent = 1 << 0,
	/** Scalar offers the short audition tone through its own audio route. */
	AudioSample = 1 << 1,
	/** Offers the subtitle style preview instead of an audio tone. */
	SubtitleSample = 1 << 2,
	/** Brightness calibration point that can drive the host's HDR pattern. */
	HDRCalibration = 1 << 3,
	/** Peak point of the calibration pair; it owns the bright patch rather than the paper white. */
	HDRPeak = 1 << 4,
};
ENUM_CLASS_FLAGS(EDreamSettingPresentation)

/** Page capabilities a tab opts into; a host category named Video can preview the scene without being called Graphics. */
enum class EDreamSettingCategoryContent : uint8
{
	None = 0,
	/** Shows the effective display information block above the rows. */
	DisplayInformation = 1 << 0,
	/** Offers the live scene behind the page, since the tab changes visible output. */
	ScenePreview = 1 << 1,
	/** Shows the colour-vision sample swatches beside the accessibility rows. */
	ColourSamples = 1 << 2,
	/** Hosts the gameplay benchmark action next to the graphics presets. */
	GameBenchmark = 1 << 3,
};
ENUM_CLASS_FLAGS(EDreamSettingCategoryContent)

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

/** Tab metadata owned by the registry. A category needs no definition to exist. */
struct DREAMSETTINGS_API FDreamSettingCategory
{
	FName Id;
	FText Label;
	FText Description;
	int32 Order = 0;
	EDreamSettingCategoryContent Content = EDreamSettingCategoryContent::None;
};

struct DREAMSETTINGS_API FDreamSettingDefinition
{
	FName Id;
	FName Category;
	FText Label;
	FText Description;
	EDreamSettingPresentation Presentation = EDreamSettingPresentation::None;
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
	/** Register a tab explicitly. Explicit metadata wins over a category inferred from its rows. */
	FDreamSettingsResult RegisterCategory(FDreamSettingCategory Category);
	/** Sorted tabs, including categories inferred from rows whose Category was never registered. */
	TArray<FDreamSettingCategory> Categories() const;
	FDreamSettingsResult Validate(TArray<FName>& OrderedIds) const;
	/** Returns the definition with host overrides applied; the stored copy is never mutated. */
	const FDreamSettingDefinition* Find(FName Id) const;
	TSharedPtr<IDreamSettingProvider> Provider(FName Id) const { return Providers.FindRef(Id); }
	const TMap<FName, FDreamSettingDefinition>& All() const { return Definitions; }
	/** Replace label, description or visibility for one registered setting. Unset fields keep their value. */
	FDreamSettingsResult OverrideDefinition(FName Id, const TOptional<FText>& Label,
		const TOptional<FText>& Description, const TOptional<bool>& bHidden = {});
	/** Replace a tab's label, description or order, creating the tab when only rows imply it. */
	FDreamSettingsResult OverrideCategory(FName Id, const TOptional<FText>& Label,
		const TOptional<FText>& Description, const TOptional<int32>& Order,
		const TOptional<EDreamSettingCategoryContent>& Content = {});
private:
	TMap<FName, FDreamSettingDefinition> Definitions;
	TMap<FName, TSharedPtr<IDreamSettingProvider>> Providers;
	TMap<FName, FDreamSettingCategory> ExplicitCategories;
	TMap<FName, FDreamSettingDefinition> DefinitionOverrides;
	TMap<FName, FDreamSettingCategory> CategoryOverrides;
};

/** Host edits to the finished catalogue. The provider behind a definition is never replaced. */
struct DREAMSETTINGS_API FDreamSettingsCatalogOverride
{
	TSet<FName> HiddenSettings;
	/** Explicit visibility, including showing a built-in entry again. Takes precedence over HiddenSettings. */
	TMap<FName, bool> SettingVisibility;
	TMap<FName, FText> SettingLabels;
	TMap<FName, FText> SettingDescriptions;
	TMap<FName, FText> CategoryLabels;
	TMap<FName, int32> CategoryOrder;
	TMap<FName, EDreamSettingCategoryContent> CategoryContent;
};

/** Applies every listed edit and returns the first failure; nothing is applied partially. */
DREAMSETTINGS_API FDreamSettingsResult ApplyDreamSettingsCatalogOverride(FDreamSettingsRegistry& Registry,
	const FDreamSettingsCatalogOverride& Override);

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
