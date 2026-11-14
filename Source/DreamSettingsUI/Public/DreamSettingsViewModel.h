#pragma once
#include "CoreMinimal.h"
#include "ViewModel/DreamViewModel.h"
#include "DreamSettingsModel.h"
#include "InputCoreTypes.h"
#include "DreamSettingsInputGrouping.h"
#include "DreamSettingsMenuShortcuts.h"
#include "DreamSettingsViewModel.generated.h"

class ULocalPlayer;
class UAudioComponent;
class USoundWaveProcedural;
class UDreamSettingsViewModel;
class UDreamSettingsNavigationMemory;
class UDreamSettingsSubtitleViewModel;
class UDreamSettingsDetails;
class UDreamSettingsPresentation;

/** One registry definition, independent of any visual theme. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingRowViewModel : public UDreamViewModel
{
    GENERATED_BODY()
public:
    void Initialize(TSharedPtr<FDreamSettingsSession> InSession, FName InId, UDreamSettingsViewModel* InOwner = nullptr);
    void Refresh();
    UFUNCTION(BlueprintPure,Category="Settings") FName GetSettingId() const { return Id; }
    FText GetValidationMessage() const { return NumericError; }
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FName NavigationId;
    UFUNCTION(BlueprintCallable, Category="Settings") void SetNumber(float Value);
    UFUNCTION(BlueprintCallable, Category="Settings") void CommitNumberText(const FString& Text);
    UFUNCTION(BlueprintCallable, Category="Settings") void Previous();
    UFUNCTION(BlueprintCallable, Category="Settings") void Next();
    UFUNCTION(BlueprintCallable, Category="Settings") void Reset();
    UFUNCTION(BlueprintCallable, Category="Settings") void PlaySample();
    UFUNCTION(BlueprintCallable, Category="Settings") void SetKey(FKey Value);
    UFUNCTION(BlueprintCallable, Category="Settings") void SetListening(bool Value);
    UFUNCTION(BlueprintCallable, Category="Settings") void ClearKey();
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool IsKey = false;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool IsOrdinary = true;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FKey SelectedKey;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FString IconFamily;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool HasAudioSample = false;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool HasSubtitleSample = false;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool HasSample = false;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool HasHDRPreview = false;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool IsHDRPeak = false;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText HDRInstructions;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText SampleLabel;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText Label;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText GroupTitle;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool HasGroupHeading=false;
    void SetGroupHeading(const FText& Title,bool Visible) { DREAM_VM_SET(GroupTitle,Title); DREAM_VM_SET(HasGroupHeading,Visible); }
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText Description;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText ValueText;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText DefaultValueText;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText ResetHint;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FString NumericText;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool IsDiscrete = false;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool Enabled = false;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool IsScalar = false;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") float Number = 0;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") float Minimum = 0;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") float Maximum = 1;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") float Step = 0.01f;
private:
    double NumberDisplayScale() const;
    FText NumericError;
    void Advance(int32 Direction);
    FName Id;
    TWeakPtr<FDreamSettingsSession> Session;
    TWeakObjectPtr<UDreamSettingsViewModel> Owner;
};

UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsCategoryViewModel : public UDreamViewModel
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly,Category="Settings") FName Id;
    TWeakObjectPtr<UDreamSettingsViewModel> Owner;
    UFUNCTION(BlueprintCallable, Category="Settings") void Select();
    void SetSelected(bool Value) { DREAM_VM_SET(Selected,Value); }
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool Selected=false;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText Label;
};

/** C++ presenter. A custom DUI can share this object without copying providers or the session. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsViewModel : public UDreamViewModel
{
	GENERATED_BODY()
public:
	bool Initialize(ULocalPlayer* Player);
	void Shutdown();
	virtual void BeginDestroy() override;
	TSharedPtr<FDreamSettingsSession> GetSession() const { return Session; }
	DECLARE_MULTICAST_DELEGATE(FClosed);
	FClosed OnClosed;
	/** Before dynamic rows disappear, so navigation can record stable identities. */
	FSimpleMulticastDelegate OnNavigationChanging;
	FSimpleMulticastDelegate OnDetailsOpened;
    FSimpleMulticastDelegate OnRowsPresented;
	FSimpleMulticastDelegate OnBenchmarkConfirmationOpened;
    FSimpleMulticastDelegate OnResetConfirmationOpened;
	FName GetCurrentCategory() const { return CurrentCategory; }
	UDreamSettingsNavigationMemory* GetNavigationMemory() const { return NavigationMemory.Get(); }
	void SetPreparingText(bool Value);
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool PreparingText=false;
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") TObjectPtr<UDreamSettingsSubtitleViewModel> Subtitles;
	void PreviewSubtitles();
	UFUNCTION(BlueprintCallable,Category="Settings") void OpenDetails();
	UFUNCTION(BlueprintCallable,Category="Settings") void CloseDetails();
	UFUNCTION(BlueprintCallable,Category="Settings") void SetDetailsSetting(FName Id);
	UFUNCTION(BlueprintCallable,Category="Settings") void SetPresentation(UDreamSettingsPresentation* Value);
	void ConfigureScenePreview(bool Allowed,bool AutomaticForGraphics);
	void ConfigureDescriptionAutoScroll(bool Enabled) { bAutoScrollDescriptions=Enabled; }
	bool AllowsDescriptionAutoScroll() const;
	void ConfigureInputGrouping(EDreamSettingsInputGrouping Mode,const TMap<FSoftObjectPath,FText>& ContextLabels);
    void ConfigureMenuShortcuts(const FDreamSettingsMenuShortcuts& Value);
    bool HandleMenuShortcut(FKey Key,bool Repeat);
    UFUNCTION(BlueprintCallable,Category="Settings") void RequestResetOptions();
    UFUNCTION(BlueprintCallable,Category="Settings") void CancelResetOptions();
    UFUNCTION(BlueprintCallable,Category="Settings") void ConfirmResetSelected();
    UFUNCTION(BlueprintCallable,Category="Settings") void ConfirmResetCategory();
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool ConfirmReset=false;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool CanResetSelected=false;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText ResetSelectedLabel;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText ResetCategoryLabel;
    UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText MenuShortcutHint;
	UFUNCTION(BlueprintCallable,Category="Settings") void ToggleScenePreview();
	void ConfigureGameBenchmark(bool Enabled,bool EnabledInShipping,const TArray<FKey>& Keys);
	bool IsGameBenchmarkShortcut(FKey Key) const;
	UFUNCTION(BlueprintCallable,Category="Settings") void RequestGameBenchmark();
	UFUNCTION(BlueprintCallable,Category="Settings") void ConfirmGameBenchmarkLaunch();
	UFUNCTION(BlueprintCallable,Category="Settings") void CancelGameBenchmark();
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool ShowGameBenchmark=false;
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool CanStartGameBenchmark=false;
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool ConfirmGameBenchmark=false;
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText GameBenchmarkHint;
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool ScenePreviewAvailable=false;
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool ScenePreviewActive=false;
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") float BackgroundOpacity=1;
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") FText ScenePreviewLabel;
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") TObjectPtr<UDreamSettingsDetails> Details;
	UPROPERTY(BlueprintReadOnly,FieldNotify,Category="Settings") bool ShowDetails=false;

	UFUNCTION(BlueprintCallable, Category="Settings") void SetVSync(bool Value);
	UFUNCTION(BlueprintCallable, Category="Settings") void SetMasterVolume(float Value);
	UFUNCTION(BlueprintCallable, Category="Settings") void Apply();
	UFUNCTION(BlueprintCallable, Category="Settings") void Revert();
	UFUNCTION(BlueprintCallable, Category="Settings") void ResetDefaults();
	UFUNCTION(BlueprintCallable, Category="Settings") void ResetCurrentCategory();
	UFUNCTION(BlueprintCallable, Category="Settings") void RequestClose();
	UFUNCTION(BlueprintCallable, Category="Settings") void KeepEditing();
	UFUNCTION(BlueprintCallable, Category="Settings") void DiscardAndClose();
	UFUNCTION(BlueprintCallable, Category="Settings") void ApplyAndClose();
	UFUNCTION(BlueprintCallable, Category="Settings") void PlayTestSound();
	void PlayTestSoundForSetting(FName SettingId);
	UFUNCTION(BlueprintCallable, Category="Settings") void AutoDetectGraphics();
	UFUNCTION(BlueprintCallable, Category="Settings") void KeepDisplay();
	UFUNCTION(BlueprintCallable, Category="Settings") void RevertDisplay();
	void SelectCategory(FName Category);
	void CaptureChanged(FName Id,bool bListening);
	void ChooseKey(FName Id,FKey Key);
	bool CanEditRow(FName Id) const;
	UFUNCTION(BlueprintCallable, Category="Settings") void SwapKey();
	UFUNCTION(BlueprintCallable, Category="Settings") void ReplaceKey();
	UFUNCTION(BlueprintCallable, Category="Settings") void CancelKeyConflict();
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool ActionsEnabled = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool ShowColourSamples = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool ConfirmKeyConflict = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool CanSwapKey = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText KeyConflictPrompt;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") TArray<TObjectPtr<UDreamSettingRowViewModel>> Rows;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") TArray<TObjectPtr<UDreamSettingsCategoryViewModel>> Categories;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText CategoryTitle;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText DisplayPrompt;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool ConfirmDisplay = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool PageEnabled = false;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool VSync = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") float MasterVolume = 1;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool Dirty = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool Ready = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool ConfirmClose = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText Status;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText DisplayInformation;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") bool ShowDisplayInformation=false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Settings") FText VolumeText = FText::AsPercent(1.0);
private:
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> PreviewAudio;
	UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> PreviewTone;
	void Refresh();
	void RefreshDetails();
	void RefreshScenePreview();
	bool bAutoScrollDescriptions=true;
	void GroupInputRows(TArray<TObjectPtr<UDreamSettingRowViewModel>>& Selected);
	EDreamSettingsInputGrouping InputGrouping=EDreamSettingsInputGrouping::DisplayCategory;
	TMap<FSoftObjectPath,FText> InputContextLabels;
	bool bInputGroupingDirty=false;
    FDreamSettingsMenuShortcuts MenuShortcuts;
    FName ResetSetting;
    void RefreshMenuShortcutHint();
	void RefreshGameBenchmark();
	bool bEnableGameBenchmark=!UE_BUILD_SHIPPING;
	TArray<FKey> GameBenchmarkKeys={EKeys::F8,EKeys::Gamepad_FaceButton_Top};
	bool bAllowScenePreview=true;
	bool bAutomaticGraphicsPreview=false;
	TOptional<bool> ScenePreviewOverride;
	UPROPERTY(Transient) TObjectPtr<UDreamSettingsPresentation> Presentation;
	FName DetailsSetting;
	void Close();
	void ResolveKeyConflict(bool bSwap);
	TArray<FName> FindKeyConflicts(FName Id,const FString& Key) const;
	FName CapturingRow, ConflictRow;
	FString ConflictKey;
	FDreamSettingsValues ConflictSnapshot;
	TSharedPtr<FDreamSettingsSession> Session;
	UPROPERTY(Transient) TArray<TObjectPtr<UDreamSettingRowViewModel>> AllRows;
	bool bCloseAfterDisplay = false;
	bool bCloseAfterOperation = false;
	FName CurrentCategory;
	TWeakObjectPtr<UDreamSettingsNavigationMemory> NavigationMemory;
};
