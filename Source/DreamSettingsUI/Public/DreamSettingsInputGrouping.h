#pragma once
#include "CoreMinimal.h"
#include "DreamSettingsInputGrouping.generated.h"

UENUM(BlueprintType)
enum class EDreamSettingsInputGrouping : uint8
{
    None,
    DisplayCategory,
    MappingContext
};
