#pragma once
#include "CoreMinimal.h"
#include "GenericPlatform/GenericWindow.h"
#include "DreamSettingsModel.h"

class UGameInstance;
class FSceneViewport;
class SWindow;

// Resolves the owning viewport on each operation; never targets an unrelated PIE client
// or the editor's root window through GEngine->GameViewport.
class FDreamSettingsDisplayTarget
{
public:
    explicit FDreamSettingsDisplayTarget(UGameInstance* InInstance);
    bool IsPIE() const;
    FSceneViewport* Viewport() const;
    TSharedPtr<SWindow> PIEWindow() const;
    FDreamSettingsResult Availability(bool bWindowMode) const;
    EWindowMode::Type Mode() const;
    FIntPoint Size() const;
    FDreamSettingsResult SetMode(EWindowMode::Type Mode);
    FDreamSettingsResult SetSize(FIntPoint Size);
private:
    TWeakObjectPtr<UGameInstance> Instance;
};
