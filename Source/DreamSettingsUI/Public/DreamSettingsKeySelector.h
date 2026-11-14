#pragma once
#include "Controls/DreamInputKeySelector.h"
#include "DreamSettingsKeySelector.generated.h"
class IInputProcessor;

/** DreamGUI selector with capture before Enhanced Input and UI action routing. */
UCLASS()
class DREAMSETTINGSUI_API UDreamSettingsKeySelector : public UDreamInputKeySelector
{
    GENERATED_BODY()
public:
    UDreamSettingsKeySelector();
    virtual void BeginDestroy() override;
    virtual void ApplyStyle() override;
    /** Use DisplayKey in DUI bindings so equal writes also refresh themed labels. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintGetter="GetDisplayKey",BlueprintSetter="SetDisplayKey",Category="Settings") FKey DisplayKey;
    UFUNCTION(BlueprintGetter) FKey GetDisplayKey() const { return GetSelectedKey(); }
    UFUNCTION(BlueprintSetter) void SetDisplayKey(FKey Value);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintGetter="GetIconFamily",BlueprintSetter="SetIconFamily",Category="Settings") FString IconFamily=TEXT("xbox");
    UFUNCTION(BlueprintGetter) FString GetIconFamily() const { return IconFamily; }
    UFUNCTION(BlueprintSetter) void SetIconFamily(const FString& Value);
    /** Native theme extension point; the default labels require no vendor assets. */
    virtual FText FormatButtonLabel(FKey Key) const;
    static FText ButtonLabelForFamily(FKey Key,const FString& Family);
protected:
    virtual void NativeOnConstruct() override;
    virtual void NativeOnDestruct() override;
private:
    void ReleaseInputCapture();
    void RefreshButtonLabel();
    void KeyChanged(FKey Value);
    void ListeningChanged(bool Value);
    TSharedPtr<IInputProcessor> InputCapture;
};
