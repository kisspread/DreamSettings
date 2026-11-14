#include "DreamSettingsModel.h"
#include "DreamSettings.h"
#include "Misc/Base64.h"
#include "HAL/PlatformTime.h"
#include <cwchar>

#define LOCTEXT_NAMESPACE "DreamSettings"
const FName DreamSettingIds::VSync(TEXT("Display.VSync"));
const FName DreamSettingIds::MasterVolume(TEXT("Audio.MasterVolume"));

bool FDreamSettingValue::Equals(const FDreamSettingValue& Other) const
{
    return bToken == Other.bToken && (bToken ? Token == Other.Token : FMath::IsNearlyEqual(Number, Other.Number,
        FMath::Max(1.0, FMath::Max(FMath::Abs(Number), FMath::Abs(Other.Number))) * 1.e-6));
}
FString FDreamSettingValue::Encode() const
{
    return bToken ? TEXT("id:") + FBase64::Encode(Token) : FString::Printf(TEXT("%.17g"), Number);
}
bool FDreamSettingValue::Decode(const FString& Text, FDreamSettingValue& Out)
{
    if (Text.StartsWith(TEXT("id:")))
    {
        FString Token;
        if (!FBase64::Decode(Text.Mid(3), Token)) return false;
        Out = Id(MoveTemp(Token));
        return true;
    }
    const FString Trimmed = Text.TrimStartAndEnd();
    TCHAR* End = nullptr;
    const double Number = std::wcstod(*Trimmed, &End);
    if (Trimmed.IsEmpty() || !End || *End != 0 || !FMath::IsFinite(Number)) return false;
    Out = Number;
    return true;
}
FDreamSettingsResult FDreamSettingDefinition::Normalize(const FDreamSettingValue& Input, FDreamSettingValue& Output) const
{
    if (Kind == EDreamSettingKind::Choice || Kind == EDreamSettingKind::Key)
    {
        if (!Input.bToken || Input.Token.IsEmpty() || (Kind == EDreamSettingKind::Choice
            && !GetOptions().ContainsByPredicate([&](const auto& Option) { return Option.Id == Input.Token && Option.bSelectable; })))
            return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue, LOCTEXT("Choice", "Select a supported option."));
        Output = Input;
        return {};
    }
    if (Kind == EDreamSettingKind::Scalar && !Input.bToken && FMath::IsFinite(Input.Number) && SpecialValues.Contains(Input.Number))
    { Output = Input; return {}; }
    if (Input.bToken || !FMath::IsFinite(Input.Number) || Input.Number < Min || Input.Number > Max
        || (Kind == EDreamSettingKind::Toggle && Input.Number != 0 && Input.Number != 1))
        return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue, LOCTEXT("Range", "The value is outside the supported range."));
    Output = FMath::Clamp(Min + FMath::RoundToDouble((Input.Number - Min) / Step) * Step, Min, Max);
    return {};
}
FDreamSettingsResult FDreamSettingsRegistry::Register(FDreamSettingDefinition Definition, TSharedPtr<IDreamSettingProvider> InProvider)
{
    FDreamSettingValue Normalized;
    TSet<FString> Options;
    for (const auto& Pair : Definition.SpecialValues)
        if (Definition.Kind != EDreamSettingKind::Scalar || !FMath::IsFinite(Pair.Key))
            return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidDefinition, LOCTEXT("InvalidSpecial", "Only scalar settings can define finite special values."));
    for (const auto& Option : Definition.Options)
    {
        if (Option.Id.IsEmpty() || Options.Contains(Option.Id))
            return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidDefinition, LOCTEXT("DuplicateOption", "Choice IDs must be unique and non-empty."));
        Options.Add(Option.Id);
    }
    if ((Definition.bAsyncWrite && (Definition.bPreview || !Definition.Dependencies.IsEmpty() || Definition.Derive))
        || (!InProvider && !Definition.Derive) || (bool(Definition.Derive) != bool(Definition.Expand)) || Definition.Id.IsNone() || Definitions.Contains(Definition.Id) || !FMath::IsFinite(Definition.Min)
        || !FMath::IsFinite(Definition.Max) || Definition.Min > Definition.Max || !FMath::IsFinite(Definition.Step)
        || Definition.Step <= 0 || (Definition.Kind == EDreamSettingKind::Toggle && (Definition.Min != 0 || Definition.Max != 1 || Definition.Step != 1))
        || !Definition.Normalize(Definition.Default, Normalized).IsOk() || !Normalized.Equals(Definition.Default))
        return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidDefinition, LOCTEXT("Definition", "Invalid or duplicate setting definition."));
    Providers.Add(Definition.Id, InProvider);
    Definitions.Add(Definition.Id, MoveTemp(Definition));
    return {};
}
FDreamSettingsResult FDreamSettingsRegistry::Validate(TArray<FName>& OrderedIds) const
{
    OrderedIds.Reset();
    TSet<FName> Visiting, Visited;
    TFunction<bool(FName)> Visit = [&](FName Id)
    {
        if (Visited.Contains(Id)) return true;
        const auto* Definition = Find(Id);
        if (!Definition || Visiting.Contains(Id)) return false;
        Visiting.Add(Id);
        for (FName Dependency : Definition->Dependencies) if (!Visit(Dependency)) return false;
        Visiting.Remove(Id);
        Visited.Add(Id);
        OrderedIds.Add(Id);
        return true;
    };
    TArray<FName> Ids;
    Definitions.GetKeys(Ids);
    Ids.Sort([&](FName A, FName B) { return Definitions[A].Order == Definitions[B].Order ? A.LexicalLess(B) : Definitions[A].Order < Definitions[B].Order; });
    for (FName Id : Ids) if (!Visit(Id))
        return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidDefinition, LOCTEXT("Dependencies", "Missing or cyclic setting dependency."));
    return {};
}
FDreamSettingsSession::FDreamSettingsSession(TSharedRef<FDreamSettingsRegistry> InRegistry, TSharedRef<IDreamSettingsStore> InStore)
    : Registry(MakeShared<FDreamSettingsRegistry>(*InRegistry)), Store(InStore) {}
FDreamSettingsSession::~FDreamSettingsSession()
{
    if (bOpen)
    {
        const auto Result = Close();
        if (!Result.IsOk()) UE_LOG(LogDreamSettings, Error, TEXT("Session cleanup: %s"), *Result.Message.ToString());
    }
}
FDreamSettingsResult FDreamSettingsSession::Publish(FDreamSettingsResult Result)
{
    LastResult = MoveTemp(Result);
    OnChanged.Broadcast();
    return LastResult;
}
FDreamSettingsResult FDreamSettingsSession::Open()
{
    if (bOpen) return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, LOCTEXT("AlreadyOpen", "This session is already open.")));
    auto Result = Registry->Validate(Order);
    if (!Result.IsOk()) return Publish(Result);
    Result = Store->Load(Persisted);
    if (!Result.IsOk()) return Publish(Result);
    FDreamSettingsValues Initial;
    for (FName Id : Order)
    {
        if (Registry->Find(Id)->Derive) continue;
        FDreamSettingValue Value = Registry->Find(Id)->Default;
        Result = Registry->Provider(Id)->Read(Value);
        if (!Result.IsOk()) { ReadFailures.Add(Id, Result); Value = Registry->Find(Id)->Default; }
        Initial.Add(Id, Value);
    }
    UpdateDerived(Initial);
    Baseline = Applied = Pending = MoveTemp(Initial);
    bOpen = true;
    return Publish({});
}
FDreamSettingsResult FDreamSettingsSession::Availability(FName Id, bool bDirectEdit) const
{
    if (const auto* Error = ReadFailures.Find(Id)) return *Error;
    const auto* Definition = Registry->Find(Id);
    if (!Definition) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable, LOCTEXT("Unknown", "The setting or session is unavailable."));
    auto Result = Definition->Derive ? FDreamSettingsResult{} : Registry->Provider(Id)->Availability();
    if (Result.IsOk() && Definition->Derive)
        for (FName Dependency : Definition->Dependencies) { Result = Availability(Dependency,false); if (!Result.IsOk()) break; }
    if (Result.IsOk() && Definition->Condition) Result = Definition->Condition(Pending);
    if (Result.IsOk() && bDirectEdit && Definition->EditCondition) Result = Definition->EditCondition(Pending);
    return Result;
}
bool FDreamSettingsSession::IsDirty() const
{
    if (IsBusy() || !UncertainBackends.IsEmpty() || bAwaitingConfirmation) return true;
    for (const auto& Pair : Pending) if (!Pair.Value.Equals(Baseline.FindChecked(Pair.Key))) return true;
    for (const auto& Pair : Applied) if (!Pair.Value.Equals(Baseline.FindChecked(Pair.Key))) return true;
    return false;
}
FDreamSettingsResult FDreamSettingsSession::CheckExternalChanges() const
{
    if (!UncertainBackends.IsEmpty())
        return FDreamSettingsResult::Fail(EDreamSettingsError::Rollback, LOCTEXT("Uncertain", "A backend could not be restored. Retry Revert before applying more changes."));
    for (FName Id : Order)
    {
        if (ReadFailures.Contains(Id) || Registry->Find(Id)->Derive) continue;
        FDreamSettingValue Current;
        auto Result = Registry->Provider(Id)->Read(Current);
        if (!Result.IsOk()) return Result;
        if (!Current.Equals(Applied.FindChecked(Id)))
            return FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, FText::Format(LOCTEXT("ExternalSettingChange", "{0} changed outside this page ({1} to {2}). Close and reopen it before applying."),
                Registry->Find(Id)->Label, FText::FromString(Applied.FindChecked(Id).Encode()), FText::FromString(Current.Encode())));
    }
    return {};
}
FDreamSettingsResult FDreamSettingsSession::Stage(FName Id, FDreamSettingValue Value)
{
    const auto* Definition = Registry->Find(Id);
    if (!bOpen || !Definition) return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue, LOCTEXT("Unknown", "The setting or session is unavailable.")));
    if (IsBusy() || bAwaitingConfirmation) return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, LOCTEXT("ConfirmFirst", "Keep or revert the display changes first.")));
    auto Result = Availability(Id);
    if (!Result.IsOk()) return Publish(Result);
    FDreamSettingValue Normalized;
    Result = Definition->Normalize(Value, Normalized);
    if (!Result.IsOk()) return Publish(Result);
    if (Normalized.bToken && Definition->OptionAvailability)
    {
        Result = Definition->OptionAvailability(Normalized.Token, Pending);
        if (!Result.IsOk()) return Publish(Result);
    }
    if (Definition->RelatedChanges)
    {
        FDreamSettingsValues Changes;
        Result = Definition->RelatedChanges(Normalized, Pending, Changes);
        if (!Result.IsOk()) return Publish(Result);
        Changes.Add(Id, Normalized);
        return StageBatch(Changes);
    }
    if (Definition->Derive)
    {
        FDreamSettingsValues Changes;
        Result = Definition->Expand(Normalized, Pending, Changes);
        if (!Result.IsOk()) return Publish(Result);
        return StageBatch(Changes);
    }
    if (Definition->bPreview)
    {
        Result = CheckExternalChanges();
        if (!Result.IsOk()) return Publish(Result);
        Result = Registry->Provider(Id)->Write(Normalized);
        if (!Result.IsOk())
        {
            const auto Recovery = Registry->Provider(Id)->Write(Applied.FindChecked(Id));
            if (!Recovery.IsOk())
            {
                FDreamSettingValue Actual;
                if (Registry->Provider(Id)->Read(Actual).IsOk()) Applied.Add(Id, Actual);
                UncertainBackends.Add(Id);
            }
            return Publish(Recovery.IsOk() ? Result : FDreamSettingsResult::Fail(EDreamSettingsError::Rollback, Recovery.Message));
        }
        Applied.Add(Id, Normalized);
    }
    Pending.Add(Id, Normalized);
    StagedOverrides.Add(Id);
    UpdateDerived(Applied);
    UpdateDerived(Pending);
    return Publish({});
}
FDreamSettingsResult FDreamSettingsSession::StageBatch(const FDreamSettingsValues& Changes)
{
    if (!bOpen || IsBusy() || bAwaitingConfirmation) return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, LOCTEXT("BatchUnavailable", "Finish the display confirmation before editing settings.")));
    FDreamSettingsValues Normalized;
    FDreamSettingsValues Candidate = Pending;
    for (const auto& Pair : Changes) Candidate.Add(Pair.Key, Pair.Value);
    for (const auto& Pair : Changes)
    {
        const auto* Target = Registry->Find(Pair.Key);
        if (!Target || Target->Derive || Target->bPreview)
            return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::InvalidDefinition, LOCTEXT("CompositeTarget", "Composite controls require deferred, ordinary settings.")));
        auto Result = Availability(Pair.Key,false);
        FDreamSettingValue Value;
        if (Result.IsOk()) Result = Target->Normalize(Pair.Value, Value);
        if (Result.IsOk() && Value.bToken && Target->OptionAvailability) Result = Target->OptionAvailability(Value.Token, Candidate);
        if (!Result.IsOk()) return Publish(Result);
        Normalized.Add(Pair.Key, Value);
    }
    for (const auto& Pair : Normalized) { Pending.Add(Pair.Key, Pair.Value); StagedOverrides.Add(Pair.Key); }
    UpdateDerived(Pending);
    return Publish({});
}
void FDreamSettingsSession::UpdateDerived(FDreamSettingsValues& Values) const
{
    for (FName Id : Order) if (const auto& Derive = Registry->Find(Id)->Derive) Values.Add(Id, Derive(Values));
}
FDreamSettingsValues FDreamSettingsSession::PersistentValues(const FDreamSettingsValues& Values) const
{
    FDreamSettingsValues Out = Persisted;
    for (const auto& Pair : Values)
        if (!ReadFailures.Contains(Pair.Key) && !Registry->Find(Pair.Key)->Derive) Out.Add(Pair.Key, Pair.Value);
    return Out;
}
FDreamSettingsResult FDreamSettingsSession::CaptureApplied()
{
    FDreamSettingsResult Failure;
    for (FName Id : Order)
    {
        if (ReadFailures.Contains(Id) || Registry->Find(Id)->Derive) continue;
        FDreamSettingValue Actual;
        auto Result = Registry->Provider(Id)->Read(Actual);
        if (Result.IsOk()) Applied.Add(Id, Actual);
        else { UncertainBackends.Add(Id); Failure = Result; }
    }
    UpdateDerived(Applied);
    return Failure;
}
FDreamSettingsResult FDreamSettingsSession::SuspendWrite(FName Id, FDreamSettingValue Target, FDreamSettingsResult Result)
{
    WaitingSetting=Id; WaitingTarget=MoveTemp(Target);
    return Publish(Result);
}
FDreamSettingsResult FDreamSettingsSession::StartRestore(bool bDiscardDraft, FDreamSettingsResult ResultAfter)
{
    bRestoring=true; bApplyInProgress=false; bAwaitingConfirmation=false;
    bDiscardAfterRestore=bDiscardDraft; AfterRestoreResult=MoveTemp(ResultAfter);
    RestoreFailure={}; RestoreFailedIds.Reset();
    auto Result=Restore(Baseline);
    return Result.IsPending() ? Result : FinishRestore(Result);
}
FDreamSettingsResult FDreamSettingsSession::FinishRestore(FDreamSettingsResult Result)
{
    bRestoring=false; bCancelRequested=false;
    if (Result.IsOk() && bRecoveryArmed)
    {
        const auto SafeValues=PersistentValues(Baseline);
        Result=Store->Save(SafeValues);
        if (Result.IsOk()) { bRecoveryArmed=false; Persisted=SafeValues; }
    }
    if (Result.IsOk())
    {
        if (bDiscardAfterRestore) { Pending=Baseline; StagedOverrides.Reset(); }
        if (bCloseRequested) { bOpen=false; bCloseRequested=false; }
        Result=AfterRestoreResult;
    }
    RestoreFailedIds.Reset(); RestoreFailure={};
    return Publish(Result);
}
void FDreamSettingsSession::PollOperation()
{
    const FName Id=WaitingSetting;
    auto Result=Registry->Provider(Id)->PollWrite();
    if (Result.IsPending()) return;
    WaitingSetting=NAME_None;
    FDreamSettingValue Actual;
    const auto Read=Registry->Provider(Id)->Read(Actual);
    if (Read.IsOk()) { Applied.Add(Id,Actual); UpdateDerived(Applied); }
    else { Result=Read; UncertainBackends.Add(Id); }
    if (Result.IsOk() && !Actual.Equals(WaitingTarget))
        Result=FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("AsyncReadback", "The device did not reach the requested state."));
    if (bRestoring)
    {
        if (!Result.IsOk())
        {
            UncertainBackends.Add(Id); RestoreFailedIds.Add(Id);
            RestoreFailure=FDreamSettingsResult::Fail(EDreamSettingsError::Rollback,Result.Message);
        }
        else UncertainBackends.Remove(Id);
        Result=Restore(Baseline);
        if (!Result.IsPending()) FinishRestore(Result);
    }
    else if (bCancelRequested || bCloseRequested) StartRestore(true);
    else if (!Result.IsOk()) StartRestore(false,Result);
    else Apply();
}
FDreamSettingsResult FDreamSettingsSession::Restore(const FDreamSettingsValues& Values)
{
    bool bNeeded = !UncertainBackends.IsEmpty();
    for (FName Id : Order) bNeeded |= !Applied.FindChecked(Id).Equals(Values.FindChecked(Id));
    if (!bNeeded) return {};
    // Detect external changes before any restoration; subsequent changes can be our own group side effects.
    for (FName Id : Order)
    {
        if (ReadFailures.Contains(Id) || Registry->Find(Id)->Derive) continue;
        FDreamSettingValue Current;
        auto Result = Registry->Provider(Id)->Read(Current);
        if (!Result.IsOk() || !Current.Equals(Applied.FindChecked(Id)))
            return FDreamSettingsResult::Fail(EDreamSettingsError::Rollback, LOCTEXT("RollbackConflict", "An external change prevents restoring the preview."));
    }
    FDreamSettingsResult Failure=RestoreFailure;
    // Restore groups before their dependent overrides, just as on apply.
    for (FName Id : Order)
    {
        if (RestoreFailedIds.Contains(Id) || ReadFailures.Contains(Id) || Registry->Find(Id)->Derive || (!UncertainBackends.Contains(Id) && Applied.FindChecked(Id).Equals(Values.FindChecked(Id)))) continue;
        auto Result = Registry->Provider(Id)->Write(Values.FindChecked(Id));
        if (Result.IsPending()) { RestoreFailure=Failure; return SuspendWrite(Id,Values.FindChecked(Id),Result); }
        auto ReadResult = CaptureApplied();
        if (!ReadResult.IsOk()) Result = ReadResult;
        if (Result.IsOk() && Applied.FindChecked(Id).Equals(Values.FindChecked(Id))) UncertainBackends.Remove(Id);
        else { UncertainBackends.Add(Id); Failure = FDreamSettingsResult::Fail(EDreamSettingsError::Rollback, Result.Message); }
    }
    for (FName Id : Order)
    {
        if (!Applied.FindChecked(Id).Equals(Values.FindChecked(Id)))
        {
            UE_LOG(LogDreamSettings, Warning, TEXT("Restore mismatch %s: actual %s expected %s"),*Id.ToString(),*Applied.FindChecked(Id).Encode(),*Values.FindChecked(Id).Encode());
            UncertainBackends.Add(Id);
            Failure = FDreamSettingsResult::Fail(EDreamSettingsError::Rollback, LOCTEXT("RestoreReadback", "A backend could not restore its previous value."));
        }
    }
    return Failure;
}
FDreamSettingsResult FDreamSettingsSession::Apply()
{
    if (!bOpen || IsBusy() || bAwaitingConfirmation) return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, LOCTEXT("ApplyUnavailable", "This session cannot apply changes right now.")));
    auto Result = CheckExternalChanges();
    if (!Result.IsOk()) return bApplyInProgress ? StartRestore(false,Result) : Publish(Result);
    bool bRisky = false;
    TSet<FName> Changed;
    for (FName Id : Order)
    {
        if (Registry->Find(Id)->Derive || (!StagedOverrides.Contains(Id) && Pending.FindChecked(Id).Equals(Baseline.FindChecked(Id)))) continue;
        Result = Availability(Id,false);
        const auto* Definition=Registry->Find(Id);
        if (Result.IsOk() && Pending.FindChecked(Id).bToken && Definition->OptionAvailability)
            Result=Definition->OptionAvailability(Pending.FindChecked(Id).Token,Pending);
        if (!Result.IsOk()) return bApplyInProgress ? StartRestore(false,Result) : Publish(Result);
        Changed.Add(Id);
        bRisky |= Registry->Find(Id)->bRequiresConfirmation && !Pending.FindChecked(Id).Equals(Baseline.FindChecked(Id));
    }
    if (bRisky && !bRecoveryArmed)
    {
        Result = Store->BeginRiskyChange(PersistentValues(Baseline));
        if (!Result.IsOk()) return Publish(Result);
        bRecoveryArmed = true;
    }
    bApplyInProgress=true;
    // An audio driver may take seconds to acknowledge a swap. Never leave an unconfirmed
    // display mode on screen while waiting for it; independent async writes go first.
    TArray<FName> ApplyOrder=Order;
    ApplyOrder.StableSort([&](FName A,FName B) { return Registry->Find(A)->bAsyncWrite && !Registry->Find(B)->bAsyncWrite; });
    for (FName Id : ApplyOrder)
    {
        if (!Changed.Contains(Id) || Applied.FindChecked(Id).Equals(Pending.FindChecked(Id))) continue;
        Result = Registry->Provider(Id)->Write(Pending.FindChecked(Id));
        if (Result.IsPending()) return SuspendWrite(Id,Pending.FindChecked(Id),Result);
        const auto ReadResult = CaptureApplied();
        if (!ReadResult.IsOk()) Result = ReadResult;
        if (Result.IsOk() && !Applied.FindChecked(Id).Equals(Pending.FindChecked(Id)))
            Result = FDreamSettingsResult::Fail(EDreamSettingsError::Backend, LOCTEXT("Readback", "The engine did not accept the requested setting."));
        if (!Result.IsOk())
        {
            UE_LOG(LogDreamSettings, Warning, TEXT("Apply %s failed: %s (actual %s expected %s)"),*Id.ToString(),*Result.Message.ToString(),*Applied.FindChecked(Id).Encode(),*Pending.FindChecked(Id).Encode());
            break;
        }
    }
    if (Result.IsOk())
    {
        for (FName Id : Order)
        {
            if (!Changed.Contains(Id)) Pending.Add(Id, Applied.FindChecked(Id));
            else if (!Pending.FindChecked(Id).Equals(Applied.FindChecked(Id)))
                Result = FDreamSettingsResult::Fail(EDreamSettingsError::Backend, LOCTEXT("CombinationRejected", "The requested combination of settings could not be applied."));
        }
    }
    if (!Result.IsOk())
    {
        return StartRestore(false,Result);
    }
    bApplyInProgress=false;
    if (bRisky)
    {
        bAwaitingConfirmation = true;
        ConfirmationDeadline = FPlatformTime::Seconds() + 15;
        return Publish({});
    }
    return Commit();
}
FDreamSettingsResult FDreamSettingsSession::Commit()
{
    for (FName Id : Order)
    {
        if (!Registry->Find(Id)->bRequiresConfirmation || Pending.FindChecked(Id).Equals(Baseline.FindChecked(Id))) continue;
        const auto Validation = Registry->Provider(Id)->ValidateApplied(Pending.FindChecked(Id));
        if (!Validation.IsOk())
        {
            return StartRestore(false,Validation);
        }
    }
    FDreamSettingsValues ToSave = PersistentValues(Pending);
    auto Result = Store->Save(ToSave);
    if (!Result.IsOk())
    {
        return StartRestore(false,Result);
    }
    Persisted = MoveTemp(ToSave);
    Baseline = Applied = Pending;
    StagedOverrides.Reset();
    bAwaitingConfirmation = bRecoveryArmed = bApplyInProgress = false;
    return Publish({});
}
FDreamSettingsResult FDreamSettingsSession::Confirm()
{
    if (!bAwaitingConfirmation) return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, LOCTEXT("NoConfirmation", "No display change is awaiting confirmation.")));
    if (FPlatformTime::Seconds() >= ConfirmationDeadline)
    {
        const auto Recovery = Reject();
        return Recovery.IsOk() ? Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, LOCTEXT("ConfirmationExpired", "The confirmation expired. Previous display settings were restored."))) : Recovery;
    }
    auto Result = CheckExternalChanges();
    return Result.IsOk() ? Commit() : Publish(Result);
}
FDreamSettingsResult FDreamSettingsSession::Reject() { return Revert(); }
double FDreamSettingsSession::GetConfirmationSecondsLeft() const { return bAwaitingConfirmation ? FMath::Max(0.0, ConfirmationDeadline - FPlatformTime::Seconds()) : 0; }
void FDreamSettingsSession::Tick(double NowSeconds)
{
    if (IsBusy()) { PollOperation(); return; }
    bool bRefresh=false;
    for (FName Id : Order)
    {
        const auto* D=Registry->Find(Id);
        if (D->Derive || !(D->bRetryUnavailable || D->bFollowExternalChanges)) continue;
        FDreamSettingValue Actual;
        const auto Result=Registry->Provider(Id)->Read(Actual);
        if (!Result.IsOk())
        {
            if (D->bRetryUnavailable && !ReadFailures.Contains(Id)) { ReadFailures.Add(Id,Result); bRefresh=true; }
            continue;
        }
        if (ReadFailures.Contains(Id) || (D->bFollowExternalChanges && !Actual.Equals(Applied.FindChecked(Id))))
        {
            // Losing an endpoint temporarily must not erase an explicit, still-unapplied selection.
            if (Pending.FindChecked(Id).Equals(Applied.FindChecked(Id)) && !StagedOverrides.Contains(Id)) Pending.Add(Id,Actual);
            ReadFailures.Remove(Id); Baseline.Add(Id,Actual); Applied.Add(Id,Actual); bRefresh=true;
        }
    }
    if (bRefresh) { UpdateDerived(Baseline); UpdateDerived(Applied); UpdateDerived(Pending); Publish({}); }
    if (bAwaitingConfirmation && NowSeconds >= ConfirmationDeadline) Reject();
    else if (bAwaitingConfirmation) OnChanged.Broadcast();
    else if (bOpen && !IsDirty())
    {
        // Engine startup, a monitor change or another game system may update values while the page is idle.
        // Adopt those only while no user edit or preview is pending; dirty drafts retain conflict protection.
        FDreamSettingsValues Current = Applied;
        bool bChanged = false;
        for (FName Id : Order)
        {
            if (ReadFailures.Contains(Id) || Registry->Find(Id)->Derive) continue;
            FDreamSettingValue Value;
            if (!Registry->Provider(Id)->Read(Value).IsOk()) return;
            bChanged |= !Value.Equals(Applied.FindChecked(Id));
            Current.Add(Id, Value);
        }
        if (bChanged)
        {
            UpdateDerived(Current);
            Baseline = Applied = Pending = MoveTemp(Current);
            StagedOverrides.Reset();
            Publish({});
        }
    }
}
FDreamSettingsResult FDreamSettingsSession::Revert()
{
    if (!bOpen) return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable, LOCTEXT("Closed", "The session is not open.")));
    if (IsBusy())
    {
        bCancelRequested=true;
        return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Pending,LOCTEXT("RevertAfterDevice", "Waiting for the device operation before restoring previous settings.")));
    }
    return StartRestore(true);
}
FDreamSettingsResult FDreamSettingsSession::ResetOne(FName Id)
{
    if (!bOpen || IsBusy() || bAwaitingConfirmation || !Registry->Find(Id)) return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, LOCTEXT("ResetUnavailable", "Defaults cannot be restored right now.")));
    if (Registry->Find(Id)->Derive || Registry->Find(Id)->RelatedChanges) return Stage(Id, Registry->Find(Id)->Default);
    auto Result = Availability(Id);
    const auto* Definition=Registry->Find(Id);
    if (Result.IsOk() && Definition->Default.bToken && Definition->OptionAvailability)
        Result=Definition->OptionAvailability(Definition->Default.Token,Pending);
    if (Result.IsOk()) { Pending.Add(Id, Registry->Find(Id)->Default); StagedOverrides.Add(Id); }
    UpdateDerived(Pending);
    return Publish(Result);
}
FDreamSettingsResult FDreamSettingsSession::Reset()
{
    return ResetDefaultsInCategory(NAME_None);
}
FDreamSettingsResult FDreamSettingsSession::ResetCategory(FName Category)
{
    if (Category.IsNone() || !Order.ContainsByPredicate([&](FName Id) { return Registry->Find(Id)->Category==Category; }))
        return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable, LOCTEXT("ResetUnavailable", "Defaults cannot be restored right now.")));
    return ResetDefaultsInCategory(Category);
}
FDreamSettingsResult FDreamSettingsSession::ResetDefaultsInCategory(FName Category)
{
    if (!bOpen || IsBusy() || bAwaitingConfirmation) return Publish(FDreamSettingsResult::Fail(EDreamSettingsError::Conflict, LOCTEXT("ResetUnavailable", "Defaults cannot be restored right now.")));
    FDreamSettingsValues Candidate=Pending;
    TArray<FName> ToReset;
    for (FName Id:Order)
    {
        const auto* Definition=Registry->Find(Id);
        if ((Category.IsNone() || Definition->Category==Category) && !Definition->Derive && Availability(Id,false).IsOk())
        { Candidate.Add(Id,Definition->Default); ToReset.Add(Id); }
    }
    for (FName Id:Order)
    {
        const auto* Definition=Registry->Find(Id);
        const bool ValidateReset=ToReset.Contains(Id) || (Category.IsNone() && Definition->Kind==EDreamSettingKind::Key);
        if (ValidateReset && Candidate.FindChecked(Id).bToken && Definition->OptionAvailability)
        {
            const auto Result=Definition->OptionAvailability(Candidate.FindChecked(Id).Token,Candidate);
            if (!Result.IsOk()) return Publish(Result);
        }
    }
    for (FName Id : ToReset)
    { Pending.Add(Id,Candidate.FindChecked(Id)); StagedOverrides.Add(Id); }
    UpdateDerived(Pending);
    return Publish({});
}
FDreamSettingsResult FDreamSettingsSession::Close()
{
    if (!bOpen) return {};
    bCloseRequested=true;
    return Revert();
}
#undef LOCTEXT_NAMESPACE
