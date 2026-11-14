#pragma once
#include "CoreMinimal.h"
#include "Controls/DreamTextInput.h"
#include "DreamSettingsNumberInput.generated.h"

/** Keeps a local text edit separate from the latest value supplied by the setting session. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsNumberInput : public UDreamTextInput
{
    GENERATED_BODY()
public:
    UDreamSettingsNumberInput();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintSetter=SetBoundText,Category="Settings") FString BoundText;
    UFUNCTION(BlueprintCallable,Category="Settings") void SetBoundText(const FString& Value);
    UPROPERTY(BlueprintAssignable,Category="Settings") FDreamTextInputChangedEvent OnNumberSubmitted;
    virtual void NativeOnConstruct() override;
    virtual void NativeOnDisable() override;
    virtual void NativeOnDestruct() override;
private:
    void ActivationChanged(bool Active);
    void Submitted(const FString& Value);
    void EnabledChanged(bool Enabled);
    void DiscardEdit();
    FString TextAtStart;
    bool Committing=false,Discarding=false;
    FDelegateHandle ActivationHandle,EnabledHandle;
};
