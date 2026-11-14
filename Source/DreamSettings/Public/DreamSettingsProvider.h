#pragma once
#include "DreamSettingsModel.h"

/** Small host adapters can provide callbacks without introducing another UObject hierarchy. */
class DREAMSETTINGS_API FDreamCallbackSettingsProvider final : public IDreamSettingProvider
{
public:
    using FRead = TFunction<FDreamSettingsResult(FDreamSettingValue&)>;
    using FWrite = TFunction<FDreamSettingsResult(const FDreamSettingValue&)>;
    using FAvailability = TFunction<FDreamSettingsResult()>;
    FDreamCallbackSettingsProvider(FRead Read, FWrite Write, FAvailability Availability = {}, FWrite Validate = {})
        : Reader(MoveTemp(Read)), Writer(MoveTemp(Write)), Capability(MoveTemp(Availability)), Validator(MoveTemp(Validate)) {}
    FDreamSettingsResult ValidateApplied(const FDreamSettingValue& Value) const override { return Validator ? Validator(Value) : FDreamSettingsResult{}; }
    FDreamSettingsResult Availability() const override { return Capability ? Capability() : FDreamSettingsResult{}; }
    FDreamSettingsResult Read(FDreamSettingValue& Out) const override { return Reader(Out); }
    FDreamSettingsResult Write(const FDreamSettingValue& Value) override
    {
        auto Result = Availability();
        return Result.IsOk() ? Writer(Value) : Result;
    }
private:
    FRead Reader;
    FWrite Writer;
    FAvailability Capability;
    FWrite Validator;
};
