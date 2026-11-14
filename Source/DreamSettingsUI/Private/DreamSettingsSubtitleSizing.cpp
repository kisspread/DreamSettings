#include "DreamSettingsSubtitleSizing.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"

void UDreamSettingsSubtitleSizing::SetMinimumSize(FVector2D Value)
{
    if (!FMath::IsFinite(Value.X) || !FMath::IsFinite(Value.Y)) return;
    MinimumSize=FVector2D(FMath::Max(0.0,Value.X),FMath::Max(0.0,Value.Y));
    if (GetWidget()) if (auto* Box=Cast<UDreamLayoutContainerSizeBox>(GetWidget()->GetLayoutContainer()))
        Box->SetMinDesiredSize(MinimumSize);
}
void UDreamSettingsSubtitleSizing::OnEnable()
{ Super::OnEnable(); SetMinimumSize(MinimumSize); }
