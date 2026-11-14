#include "DreamSettingsDetails.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "DreamSettingsHelp"
void UDreamSettingsDetails::Update(const FDreamSettingDefinition& D,const FDreamSettingValue& Value,const FText& FallbackBody,
    const FText& CurrentValue,const FText& DefaultValue,const FText& State,const UDreamSettingsPresentation* Presentation,bool LoadImage)
{
    const auto Content=Presentation ? Presentation->Resolve(D.Id,Value) : FDreamSettingsHelpContent{};
    DREAM_VM_SET(SettingId,D.Id); DREAM_VM_SET(Title,D.Label);
    FText Description=Content.Body.IsEmpty() ? FallbackBody : Content.Body;
    if (Description.IsEmpty() && D.Kind==EDreamSettingKind::Key)
        Description=LOCTEXT("KeyHelp","Select a key or button for this action. Conflicts can be swapped or replaced; changes are saved when you apply the page.");
    if (Description.IsEmpty()) Description=LOCTEXT("GenericHelp","Change this setting in the current draft. Apply keeps the changes; Revert restores the values from before editing.");
    DREAM_VM_SET(Body,Description);
    DREAM_VM_SET(Additional,Content.Additional); DREAM_VM_SET(HasAdditional,!Additional.IsEmpty());
    DREAM_VM_SET(ValueText,FText::Format(LOCTEXT("CurrentValue","Selected: {0}"),CurrentValue));
    DREAM_VM_SET(DefaultText,FText::Format(LOCTEXT("DefaultValue","Default: {0}"),DefaultValue));
    DREAM_VM_SET(StateText,State); DREAM_VM_SET(HasState,!State.IsEmpty());
    DREAM_VM_SET(ImageCaption,Content.ImageCaption);
    DREAM_VM_SET(HasImageCaption,!Content.Image.IsNull() && !ImageCaption.IsEmpty());
    RequestImage(LoadImage ? Content.Image : TSoftObjectPtr<UTexture2D>());
}
void UDreamSettingsDetails::SetImage(UTexture2D* Image)
{
    ImageBrush=FDreamUIImageBrush(); ImageBrush.SetResourceObject(Image);
    ImageBrush.DrawAs=EDreamUIImageBrushDrawType::Image;
    if (Image) ImageBrush.ImageSize=FVector2f(Image->GetSizeX(),Image->GetSizeY());
    BroadcastFieldValueChanged(ThisClass::FFieldNotificationClassDescriptor::ImageBrush);
    DREAM_VM_SET(HasImage,Image!=nullptr);
}
void UDreamSettingsDetails::RequestImage(TSoftObjectPtr<UTexture2D> Image)
{
    const auto Path=Image.ToSoftObjectPath();
    if (Path==RequestedImage) return;
    const uint64 Revision=++ImageRevision;
    CancelImageWait();
    if (ImageHandle) ImageHandle->CancelHandle();
    ImageHandle.Reset(); RequestedImage=Path;
    SetImage(nullptr);
    DREAM_VM_SET(ImageStatus,FText::GetEmpty()); DREAM_VM_SET(HasImageStatus,false);
    if (Path.IsNull()) return;
    DREAM_VM_SET(ImageStatus,LOCTEXT("ImageLoading","Loading illustration…")); DREAM_VM_SET(HasImageStatus,true);
    if (auto* Resident=Image.Get()) { FinishImage(Resident,Revision); return; }
    ImageHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,FStreamableDelegate::CreateWeakLambda(this,[this,Revision,Image]
    {
        FinishImage(Image.Get(),Revision);
    }));
}
void UDreamSettingsDetails::CancelImageWait()
{
    if (ImageReadyHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(ImageReadyHandle);
    ImageReadyHandle.Reset(); PendingImage=nullptr;
}
void UDreamSettingsDetails::FinishImage(UTexture2D* Image,uint64 Revision)
{
    if (Revision!=ImageRevision) return;
#if WITH_EDITOR
    // A loaded texture can still be compiling. UE returns the default texture's 32x32 size then;
    // publishing that size stretches a later real image. Wait without blocking the game thread.
    if (Image && Image->IsCompiling())
    {
        PendingImage=Image;
        ImageReadyHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this,Revision](float)
        {
            if (Revision!=ImageRevision) return false;
            if (PendingImage && PendingImage->IsCompiling()) return true;
            auto* Ready=PendingImage.Get(); PendingImage=nullptr; ImageReadyHandle.Reset();
            FinishImage(Ready,Revision); return false;
        }));
        return;
    }
#endif
    if (Image && (Image->GetSizeX()<=0 || Image->GetSizeY()<=0)) Image=nullptr;
    SetImage(Image);
    DREAM_VM_SET(ImageStatus,Image ? FText::GetEmpty() : LOCTEXT("ImageUnavailable","Illustration unavailable."));
    DREAM_VM_SET(HasImageStatus,Image==nullptr);
}
void UDreamSettingsDetails::Reset()
{
    ++ImageRevision;
    CancelImageWait();
    if (ImageHandle) ImageHandle->CancelHandle();
    ImageHandle.Reset(); RequestedImage.Reset(); SetImage(nullptr);
    DREAM_VM_SET(SettingId,NAME_None); DREAM_VM_SET(HasImageStatus,false); DREAM_VM_SET(ImageStatus,FText::GetEmpty());
}
void UDreamSettingsDetails::BeginDestroy() { Reset(); Super::BeginDestroy(); }
#undef LOCTEXT_NAMESPACE
