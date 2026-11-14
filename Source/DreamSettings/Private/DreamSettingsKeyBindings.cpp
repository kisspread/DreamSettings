#include "DreamSettingsKeyBindings.h"
#include "DreamSettingsInputUserSettings.h"
#include "DreamSettingsFileTransaction.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedActionKeyMapping.h"
#include "InputMappingContext.h"
#include "GameFramework/InputSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "Internationalization/Text.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
namespace
{
    FDreamSettingsResult MissingInput()
    { return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("InputUnavailable","The player's input profile is not ready. Register player-mappable contexts before opening settings.")); }
    class FKeyProvider final : public IDreamSettingProvider
    {
    public:
        FKeyProvider(TSharedRef<FDreamSettingsKeyBindings> InKeys,FName InId):Keys(InKeys),Id(InId) {}
        FDreamSettingsResult Read(FDreamSettingValue& Out) const override { return Keys->Read(Id,Out); }
        FDreamSettingsResult Write(const FDreamSettingValue& Value) override { return Keys->Write(Id,Value); }
    private:
        TSharedRef<FDreamSettingsKeyBindings> Keys; FName Id;
    };
    class FKeyStore final : public IDreamSettingsStore
    {
    public:
        FKeyStore(TSharedRef<IDreamSettingsStore> InStore,TSharedRef<FDreamSettingsKeyBindings> InKeys):Store(InStore),Keys(InKeys) {}
        FDreamSettingsResult Load(FDreamSettingsValues& Out) override
        {
            auto Result=Store->Load(Out); if (!Result.IsOk()) return Result;
            for (const auto& Binding:Keys->GetBindings())
            { FDreamSettingValue Value; Result=Keys->Read(Binding.Id,Value); if (!Result.IsOk()) return Result; Out.Add(Binding.Id,Value); }
            return {};
        }
        FDreamSettingsResult Save(const FDreamSettingsValues& Values) override
        {
            FDreamSettingsPreparedWrite Ini,Input;
            auto Result=FDreamSettingsFileTransaction(FPaths::ProjectSavedDir()).Recover(); if (!Result.IsOk()) return Result;
            Result=Store->PrepareSave(WithoutKeys(Values),Ini); if (!Result.IsOk()) return Result;
            Result=Keys->PrepareSave(Input); if (!Result.IsOk()) return Result;
            Result=FDreamSettingsFileTransaction(FPaths::ProjectSavedDir()).Commit({Ini,Input});
            if (Result.IsOk()) Store->NotifyCommitted(); return Result;
        }
        FDreamSettingsResult BeginRiskyChange(const FDreamSettingsValues& Values) override { return Store->BeginRiskyChange(WithoutKeys(Values)); }
        FDreamSettingsResult LoadRecovery(FDreamSettingsValues& Out) override { return Store->LoadRecovery(Out); }
    private:
        FDreamSettingsValues WithoutKeys(const FDreamSettingsValues& Values) const
        { auto Out=Values; for (const auto& Binding:Keys->GetBindings()) Out.Remove(Binding.Id); return Out; }
        TSharedRef<IDreamSettingsStore> Store; TSharedRef<FDreamSettingsKeyBindings> Keys;
    };
}
const FDreamSettingsKeyBinding* FDreamSettingsKeyBindings::Find(FName Id) const
{ return Bindings.FindByPredicate([Id](const auto& Binding) { return Binding.Id==Id; }); }
FDreamSettingsResult FDreamSettingsKeyBindings::Initialize(ULocalPlayer* Player,FDreamSettingsRegistry& Registry)
{
    Bindings.Reset();
    const auto* Enhanced=Player ? Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    auto* Native=Enhanced ? Enhanced->GetUserSettings() : nullptr;
    if (!Native) return {}; // Enhanced Input is optional for hosts with no registered gameplay input.
    auto* Profile=Native->GetActiveKeyProfile(); if (!Profile) return MissingInput();
    if (Profile->GetPlayerMappingRows().IsEmpty()) return {};
    Settings=Cast<UDreamSettingsInputUserSettings>(Native);
    if (!Settings.IsValid()) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,LOCTEXT("InputSettingsClass","Set Enhanced Input's UserSettingsClass to DreamSettingsInputUserSettings to enable coordinated key saving."));
    auto AddBinding=[&](const FPlayerKeyMapping& Key,EPlayerMappableKeySlot SlotId,bool Optional)
    {
        FDreamSettingsKeyBinding Binding;
        Binding.Mapping.MappingName=Key.GetMappingName(); Binding.Mapping.Slot=SlotId;
        Binding.Mapping.HardwareDeviceId=Key.GetHardwareDeviceId().HardwareDeviceIdentifier;
        Binding.Mapping.ProfileIdString=Native->GetActiveKeyProfileId(); Binding.Mapping.bCreateMatchingSlotIfNeeded=false;
        Binding.DefaultKey=Optional ? EKeys::Invalid : Key.GetDefaultKey(); Binding.bOptionalSlot=Optional;
        Binding.bGamepad=Key.GetPrimaryDeviceType()==EHardwareDevicePrimaryType::Gamepad;
        Binding.CategoryLabel=Key.GetDisplayCategory();
        if (const FString* Source=FTextInspector::GetSourceString(Binding.CategoryLabel); Source && !Source->IsEmpty())
            Binding.CategoryId=FBase64::Encode(*Source);
        for (const auto& Context:Native->GetRegisteredInputMappingContexts())
        {
            if (!Context) continue;
            const bool Contains=Context->GetMappingsForProfile(Native->GetActiveKeyProfileId()).ContainsByPredicate([&](const FEnhancedActionKeyMapping& Mapping)
            { return Mapping.GetMappingName()==Key.GetMappingName() && Mapping.Key.IsGamepadKey()==Binding.bGamepad; });
            if (Contains) Binding.Contexts.Add({FSoftObjectPath(Context.Get()),Context->ContextDescription});
        }
        Binding.Contexts.Sort([](const auto& A,const auto& B) { return A.Path.ToString()<B.Path.ToString(); });
        Binding.Id=FName(*(TEXT("Keys.")+FBase64::Encode(Binding.Mapping.ProfileIdString)+TEXT(".")+
            FBase64::Encode(Binding.Mapping.MappingName.ToString())+TEXT(".")+FString::FromInt(static_cast<int32>(SlotId))+TEXT(".")+FBase64::Encode(Binding.Mapping.HardwareDeviceId.ToString())));
        const FText Slot=SlotId==EPlayerMappableKeySlot::First ? LOCTEXT("PrimaryKey","Primary") :
            SlotId==EPlayerMappableKeySlot::Second ? LOCTEXT("SecondaryKey","Secondary") : FText::AsNumber(static_cast<int32>(SlotId)+1);
        Binding.Label=FText::Format(LOCTEXT("KeySlotLabel","{0} — {1}"),Key.GetDisplayName(),Slot);
        Bindings.Add(MoveTemp(Binding));
    };
    for (const auto& Pair:Profile->GetPlayerMappingRows()) for (const auto& Key:Pair.Value.Mappings)
    {
        if (Key.GetDefaultKey().IsAxis1D() || Key.GetDefaultKey().IsAxis2D() || Key.GetDefaultKey().IsAxis3D()) continue;
        AddBinding(Key,Key.GetSlot(),Key.GetSlot()==EPlayerMappableKeySlot::Second && !Key.GetDefaultKey().IsValid());
        if (Key.GetSlot()!=EPlayerMappableKeySlot::First) continue;
        bool HasSecondary=false;
        for (const auto& Other:Pair.Value.Mappings)
            if (Other.GetSlot()==EPlayerMappableKeySlot::Second && Other.GetHardwareDeviceId()==Key.GetHardwareDeviceId()) { HasSecondary=true; break; }
        if (!HasSecondary) AddBinding(Key,EPlayerMappableKeySlot::Second,true);
    }
    Bindings.Sort([](const auto& A,const auto& B) { return A.Id.LexicalLess(B.Id); });
    int32 Order=200;
    for (const auto& Binding:Bindings)
    {
        FDreamSettingDefinition Definition; Definition.Id=Binding.Id; Definition.Category=Binding.bGamepad ? TEXT("GamepadKeys") : TEXT("KeyboardKeys");
        Definition.Label=Binding.Label; Definition.Description=LOCTEXT("KeyDescription","Choose a key. Escape cancels capture. Changes take effect when applied.");
        Definition.InputCategoryId=Binding.CategoryId; Definition.InputCategoryLabel=Binding.CategoryLabel; Definition.InputContexts=Binding.Contexts;
        Definition.Kind=EDreamSettingKind::Key; Definition.Default=FDreamSettingValue::Id(Binding.DefaultKey.GetFName().ToString()); Definition.Order=Order++;
        Definition.OptionAvailability=[Self=AsShared(),Id=Binding.Id](const FString& Key,const FDreamSettingsValues& Values) { return Self->Validate(Id,Key,Values); };
        auto Result=Registry.Register(MoveTemp(Definition),MakeShared<FKeyProvider>(AsShared(),Binding.Id)); if (!Result.IsOk()) return Result;
    }
    return {};
}
FDreamSettingsResult FDreamSettingsKeyBindings::Read(FName Id,FDreamSettingValue& Out) const
{
    const auto* Binding=Find(Id); const auto* Native=Settings.Get();
    if (!Binding || !Native || Native->GetActiveKeyProfileId()!=Binding->Mapping.ProfileIdString) return MissingInput();
    const auto* Profile=Native->GetActiveKeyProfile(); const auto* Mapping=Profile ? Profile->FindKeyMapping(Binding->Mapping) : nullptr;
    if (!Mapping)
    {
        auto Primary=Binding->Mapping; Primary.Slot=EPlayerMappableKeySlot::First;
        if (!Binding->bOptionalSlot || !Profile || !Profile->FindKeyMapping(Primary)) return MissingInput();
        Out=FDreamSettingValue::Id(TEXT("None")); return {};
    }
    Out=FDreamSettingValue::Id(Mapping->GetCurrentKey().GetFName().ToString()); return {};
}
TArray<FName> FDreamSettingsKeyBindings::FindConflicts(FName Id,const FString& KeyName,const FDreamSettingsValues& Candidate) const
{
    TArray<FName> Result; const auto* Binding=Find(Id); if (!Binding || KeyName==TEXT("None")) return Result;
    for (const auto& Other:Bindings)
    {
        const auto* Value=Candidate.Find(Other.Id);
        if (Other.Id!=Id && Other.bGamepad==Binding->bGamepad && Value && Value->bToken && Value->Token==KeyName) Result.Add(Other.Id);
    }
    return Result;
}
FDreamSettingsResult FDreamSettingsKeyBindings::Validate(FName Id,const FString& KeyName,const FDreamSettingsValues& Candidate) const
{
    const auto* Binding=Find(Id); if (!Binding) return MissingInput();
    if (KeyName==TEXT("None")) return {};
    const FKey Key{FName(*KeyName)};
    if (!Key.IsValid() || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D() || !Key.IsBindableInBlueprints()
        || Key==EKeys::AnyKey || Key.IsGamepadKey()!=Binding->bGamepad)
        return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue,LOCTEXT("UnsupportedKey","Choose a single button for this input device."));
    if (Key==EKeys::Escape || Key==EKeys::Tilde || Key==EKeys::Gamepad_Special_Right)
        return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue,LOCTEXT("ReservedKey","This key is reserved for the menu or console."));
    const auto Conflicts=FindConflicts(Id,KeyName,Candidate);
    if (!Conflicts.IsEmpty()) return FDreamSettingsResult::Fail(EDreamSettingsError::Conflict,
        FText::Format(LOCTEXT("KeyConflict","This key is already assigned to {0}."),Find(Conflicts[0])->Label));
    return {};
}
FDreamSettingsResult FDreamSettingsKeyBindings::Write(FName Id,const FDreamSettingValue& Value)
{
    FDreamSettingValue Before; auto Result=Read(Id,Before); if (!Result.IsOk()) return Result;
    if (!Value.bToken) return FDreamSettingsResult::Fail(EDreamSettingsError::InvalidValue,LOCTEXT("KeyToken","A key identifier is required."));
    Result=Validate(Id,Value.Token,{}); if (!Result.IsOk()) return Result;
    auto Args=Find(Id)->Mapping; Args.NewKey=FKey(FName(*Value.Token));
    const bool Optional=Find(Id)->bOptionalSlot;
    const bool Creating=Optional && !Settings->GetActiveKeyProfile()->FindKeyMapping(Args);
    if (Creating && !Args.NewKey.IsValid()) return {}; // Reverting an untouched virtual row is read-only.
    if (Creating)
    {
        auto* Profile=Settings->GetActiveKeyProfile();
        auto PrimaryArgs=Args; PrimaryArgs.Slot=EPlayerMappableKeySlot::First;
        const auto* Primary=Profile->FindKeyMapping(PrimaryArgs);
        auto* Row=Profile->FindKeyMappingRowMutable(Args.MappingName);
        if (!Primary || !Row) return MissingInput();
        // Native bCreateMatchingSlotIfNeeded resolves hardware IDs through the platform registry.
        // A registered mapping may instead use a generic/custom ID absent from that registry.
        // Build an empty slot from its real authored mapping and exact device identity, then use
        // normal MapPlayerKey for the user change, dirty tracking and notifications.
        bool Created=false;
        for (const auto& Context:Settings->GetRegisteredInputMappingContexts())
        {
            if (!Context) continue;
            for (const auto& Source:Context->GetMappings())
                if (Source.GetMappingName()==Args.MappingName && Source.Key==Primary->GetDefaultKey() && Source.Action==Primary->GetAssociatedInputAction())
                {
                    FEnhancedActionKeyMapping Blank=Source; Blank.Key=EKeys::Invalid;
                    const FHardwareDeviceIdentifier Device=Primary->GetHardwareDeviceId();
                    Row->Mappings.Add(FPlayerKeyMapping(Blank,Args.Slot,Device)); Created=true; break;
                }
            if (Created) break;
        }
        if (!Created) return MissingInput();
    }
    FGameplayTagContainer Failures; Settings->MapPlayerKey(Args,Failures);
    if (!Failures.IsEmpty()) return FDreamSettingsResult::Fail(EDreamSettingsError::Backend,FText::FromString(Failures.ToStringSimple()));
    FDreamSettingValue Actual; Result=Read(Id,Actual);
    if (!Result.IsOk()) return Result;
    if (!Actual.Equals(Value)) return FDreamSettingsResult::Fail(EDreamSettingsError::Backend,LOCTEXT("KeyReadback","Enhanced Input did not accept the requested key."));
    if (auto* Player=Settings->GetLocalPlayer()) if (auto* Enhanced=Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) Enhanced->RequestRebuildControlMappings();
    return {};
}
FDreamSettingsResult FDreamSettingsKeyBindings::PrepareSave(FDreamSettingsPreparedWrite& Out) const
{
    if (!Settings.IsValid()) return MissingInput();
    Out.Filename=Settings->GetNativeSaveFilename();
    if (Out.Filename.IsEmpty() || !UGameplayStatics::SaveGameToMemory(Settings.Get(),Out.Bytes))
        return FDreamSettingsResult::Fail(EDreamSettingsError::Storage,LOCTEXT("SerializeInput","Unable to serialize the native input profile."));
    return {};
}
TSharedRef<IDreamSettingsStore> FDreamSettingsKeyBindings::WrapStore(TSharedRef<IDreamSettingsStore> Inner)
{ return Bindings.IsEmpty() ? Inner : MakeShared<FKeyStore>(Inner,AsShared()); }
#undef LOCTEXT_NAMESPACE
