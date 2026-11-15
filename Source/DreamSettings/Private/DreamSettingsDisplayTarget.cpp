#include "DreamSettingsDisplayTarget.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "DreamSettings"
FDreamSettingsDisplayTarget::FDreamSettingsDisplayTarget(UGameInstance* InInstance) : Instance(InInstance) {}
bool FDreamSettingsDisplayTarget::IsPIE() const
{ return Instance.IsValid() && Instance->GetWorld() && Instance->GetWorld()->WorldType == EWorldType::PIE; }
FSceneViewport* FDreamSettingsDisplayTarget::Viewport() const
{
    const auto* Client = Instance.IsValid() ? Instance->GetGameViewportClient() : nullptr;
    return Client ? const_cast<FSceneViewport*>(Client->GetGameViewport()) : nullptr;
}
TSharedPtr<SWindow> FDreamSettingsDisplayTarget::PIEWindow() const
{
    const auto* Client = Instance.IsValid() ? Instance->GetGameViewportClient() : nullptr;
    const auto Window = Client ? Client->GetWindow() : nullptr;
    // This is the tag assigned by UEditorEngine::GeneratePIEViewportWindow.
    // An embedded viewport resolves to an editor window, which must not be changed.
    return IsPIE() && Window && Window->GetTag() == TEXT("PIEWindow") ? Window : nullptr;
}
FDreamSettingsResult FDreamSettingsDisplayTarget::Availability(bool bWindowMode) const
{
    if (!Viewport()) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,
        LOCTEXT("DisplayViewportMissing", "The game viewport is not ready."));
    if (bWindowMode && IsPIE() && !PIEWindow()) return FDreamSettingsResult::Fail(EDreamSettingsError::Unavailable,
        LOCTEXT("PIEWindowRequired", "Window mode needs New Editor Window (PIE). The embedded viewport has no separate game window."));
    return {};
}
EWindowMode::Type FDreamSettingsDisplayTarget::Mode() const
{
    if (IsPIE())
    {
        if (const auto Window = PIEWindow()) return Window->GetWindowMode();
        return EWindowMode::Windowed;
    }
    return GEngine->GetGameUserSettings()->GetFullscreenMode();
}
FIntPoint FDreamSettingsDisplayTarget::Size() const
{
    if (IsPIE() && Viewport()) return Viewport()->GetSizeXY();
    return GEngine->GetGameUserSettings()->GetScreenResolution();
}
FDreamSettingsResult FDreamSettingsDisplayTarget::SetMode(EWindowMode::Type NewMode)
{
    auto Result = Availability(true); if (!Result.IsOk()) return Result;
    if (IsPIE())
    {
        const FIntPoint RenderSize = Size();
        auto Window = PIEWindow();
        Window->SetWindowMode(NewMode);
        if (NewMode == EWindowMode::Windowed) Window->Resize(FVector2D(RenderSize));
        Viewport()->SetFixedViewportSize(RenderSize.X, RenderSize.Y);
    }
    else
    {
        auto* Settings = GEngine->GetGameUserSettings();
        Settings->SetFullscreenMode(NewMode);
        Settings->ApplyResolutionSettings(false);
    }
    return {};
}
FDreamSettingsResult FDreamSettingsDisplayTarget::SetSize(FIntPoint NewSize)
{
    auto Result = Availability(false); if (!Result.IsOk()) return Result;
    if (IsPIE())
    {
        if (auto Window = PIEWindow(); Window && Window->GetWindowMode() == EWindowMode::Windowed)
            Window->Resize(FVector2D(NewSize));
        // PIE renders to an offscreen target. Keep the requested render resolution even
        // when a fullscreen window or an embedded editor panel has a different size.
        Viewport()->SetFixedViewportSize(NewSize.X, NewSize.Y);
    }
    else
    {
        auto* Settings = GEngine->GetGameUserSettings();
        Settings->SetScreenResolution(NewSize);
        Settings->ApplyResolutionSettings(false);
    }
    return {};
}
#undef LOCTEXT_NAMESPACE
