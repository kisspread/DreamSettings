#pragma once
#include "CoreMinimal.h"
#include "ViewModel/DreamViewModel.h"
#include "Containers/Ticker.h"
#include "DreamSettingsSubtitleViewModel.generated.h"

class ULocalPlayer;
class UDreamSettingsPlayerSubsystem;

/** Native SubtitleManager output presented by DreamGUI. The engine still owns cue priority and timing. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsSubtitleViewModel : public UDreamViewModel
{
    GENERATED_BODY()
public:
    bool Initialize(ULocalPlayer* Player);
    void Shutdown();
    virtual void BeginDestroy() override;
    bool Preview();
    void StopPreview();
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Subtitles") FText Caption;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Subtitles") bool HasText=false;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Subtitles") float FontSize=24;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Subtitles") FVector2D BandSize=FVector2D(0,108);
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Subtitles") FVector2D PreviewSize=FVector2D(0,60);
private:
    void SizeChanged(double Value);
    void Receive(const FText& Text);
    void RefreshPreviewLanguage();
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UDreamSettingsPlayerSubsystem> Preferences;
    FDelegateHandle SubtitleHandle,CultureHandle;
    FTSTicker::FDelegateHandle TickHandle;
    double PreviewUntil=0;
};
