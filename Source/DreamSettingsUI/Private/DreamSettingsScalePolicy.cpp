#include "DreamSettingsScalePolicy.h"
#include "DreamSettingsPlayer.h"
#include "Engine/LocalPlayer.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"

bool UDreamSettingsScalePolicy::BindPlayer(ULocalPlayer* Player)
{
    Release();
    BoundPlayer=Player;
    auto* RootCanvas=GetWidget() ? GetWidget()->GetRootCanvas() : nullptr;
    // Keep a custom theme's sizing contract intact. Such themes can consume the preference directly.
    if (!Player || !RootCanvas || RootCanvas->GetScaleMode()!=EDreamCanvasScaleMode::ScaleWithScreenSize) return false;
    Preferences=Player->GetSubsystem<UDreamSettingsPlayerSubsystem>();
    if (!Preferences.IsValid()) return false;
    Canvas=RootCanvas; AuthoredResolution=RootCanvas->GetReferenceResolution();
    Preferences->OnUIScaleChanged.AddUObject(this,&ThisClass::Changed);
    Changed(Preferences->GetUIScale()); return true;
}
void UDreamSettingsScalePolicy::OnEnable()
{
    Super::OnEnable();
    if (BoundPlayer.IsValid()) BindPlayer(BoundPlayer.Get());
}
void UDreamSettingsScalePolicy::Changed(double Value)
{
    if (Canvas.IsValid() && FMath::IsFinite(Value) && Value>0)
        Canvas->SetReferenceResolution(AuthoredResolution/Value);
}
void UDreamSettingsScalePolicy::Release()
{
    if (Preferences.IsValid()) Preferences->OnUIScaleChanged.RemoveAll(this);
    if (Canvas.IsValid()) Canvas->SetReferenceResolution(AuthoredResolution);
    Preferences.Reset(); Canvas.Reset();
}
void UDreamSettingsScalePolicy::OnDisable() { Release(); Super::OnDisable(); }
void UDreamSettingsScalePolicy::OnUnregister() { Release(); Super::OnUnregister(); }
