#include "DreamSettingsDisplay.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "HDRHelper.h"
#include "RHI.h"
#include "DynamicRHI.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

#define LOCTEXT_NAMESPACE "DreamSettings"
FDreamSettingsDisplayCapabilities UDreamSettingsDisplayLibrary::QueryCapabilities(UObject* WorldContextObject)
{
    FDreamSettingsDisplayCapabilities Out;
    UWorld* World=GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject,EGetWorldErrorMode::ReturnNull) : nullptr;
    Out.GameWorld=World && World->WorldType==EWorldType::Game;
    Out.HDRAllowed=IsHDRAllowed(); Out.HDROutputEnabled=IsHDREnabled();
    if (auto* Settings=GEngine ? GEngine->GetGameUserSettings() : nullptr) Out.PlatformSupportsHDR=Settings->SupportsHDRDisplayOutput();
    FDisplayMetrics Metrics;
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
    else FDisplayMetrics::RebuildDisplayMetrics(Metrics);
    Out.MonitorCount=Metrics.MonitorInfo.Num();
    auto* Viewport=World ? World->GetGameViewport() : nullptr;
    const auto Window=Viewport ? Viewport->GetWindow() : nullptr;
    if (!Window.IsValid()) return Out;
    Out.HasWindow=true; Out.WindowIsHDR=Window->GetIsHDR();
    const FVector2D TopLeft=Window->GetPositionInScreen(),BottomRight=TopLeft+Window->GetSizeInScreen();
    void* Handle=Window->GetNativeWindow().IsValid() ? Window->GetNativeWindow()->GetOSWindowHandle() : nullptr;
    const FVector2D Centre=(TopLeft+BottomRight)*.5;
    for (const auto& Monitor:Metrics.MonitorInfo)
        if (Centre.X>=Monitor.DisplayRect.Left && Centre.X<Monitor.DisplayRect.Right && Centre.Y>=Monitor.DisplayRect.Top && Centre.Y<Monitor.DisplayRect.Bottom)
        {
            Out.MonitorName=Monitor.FriendlyName.IsEmpty() ? Monitor.Name : Monitor.FriendlyName;
            Out.MonitorId=Monitor.ID;
            Out.DesktopResolution=FIntPoint(Monitor.DisplayRect.Right-Monitor.DisplayRect.Left,Monitor.DisplayRect.Bottom-Monitor.DisplayRect.Top);
            break;
        }
#if PLATFORM_WINDOWS
    if (Handle)
    {
        MONITORINFOEXW Info={}; Info.cbSize=sizeof(Info);
        if (::GetMonitorInfoW(::MonitorFromWindow(static_cast<HWND>(Handle),MONITOR_DEFAULTTONEAREST),&Info))
        {
            DEVMODEW Mode={}; Mode.dmSize=sizeof(Mode);
            if (::EnumDisplaySettingsW(Info.szDevice,ENUM_CURRENT_SETTINGS,&Mode))
            {
                Out.DesktopResolution=FIntPoint(Mode.dmPelsWidth,Mode.dmPelsHeight);
                Out.DesktopRefreshHz=Mode.dmDisplayFrequency>1 ? Mode.dmDisplayFrequency : 0;
            }
            if (Out.MonitorName.IsEmpty()) Out.MonitorName=Info.szDevice;
        }
    }
#endif
    if (!GUsingNullRHI && GDynamicRHI)
    {
        // HDRGetMetaDataForWindow returns early with SDR defaults when engine HDR is disabled.
        // Query the RHI list separately, using the renderer's largest-intersection selection rule.
        FDisplayInformationArray Displays; RHIGetDisplaysInformation(Displays);
        if (!Displays.IsEmpty())
        {
            const FIntRect WindowRect(static_cast<int32>(TopLeft.X),static_cast<int32>(TopLeft.Y),static_cast<int32>(BottomRight.X),static_cast<int32>(BottomRight.Y));
            int32 Best=0; int64 BestArea=0;
            for (int32 Index=0;Index<Displays.Num();++Index)
            {
                const auto& Rect=Displays[Index].DesktopCoordinates;
                const int64 Width=FMath::Max(0,FMath::Min(WindowRect.Max.X,Rect.Max.X)-FMath::Max(WindowRect.Min.X,Rect.Min.X));
                const int64 Height=FMath::Max(0,FMath::Min(WindowRect.Max.Y,Rect.Max.Y)-FMath::Max(WindowRect.Min.Y,Rect.Min.Y));
                if (Width*Height>BestArea) { BestArea=Width*Height; Best=Index; }
            }
            const auto& Display=Displays[Best]; Out.MonitorCapabilitiesKnown=true;
            Out.CurrentMonitorSupportsHDR=Display.bHDRSupported;
            Out.MonitorMinimumNits=Display.MinimumLuminanceInNits; Out.MonitorMaximumNits=Display.MaximumLuminanceInNits;
            Out.MonitorFullFrameNits=Display.MaximumFullFrameLuminanceInNits;
        }
        FHDRMetaData Metadata={}; HDRGetMetaDataForWindow(Metadata,TopLeft,BottomRight,Handle);
        Out.OutputMetadataSupportsHDR=Metadata.bHDRSupported;
        Out.OutputFormat=static_cast<int32>(Metadata.DisplayOutputFormat);
        Out.MinimumNits=Metadata.MinimumLuminanceInNits; Out.MaximumNits=Metadata.MaximumLuminanceInNits;
        Out.FullFrameNits=Metadata.MaximumFullFrameLuminanceInNits; Out.PaperWhiteNits=Metadata.HDRPaperWhiteInNits;
    }
    return Out;
}
bool UDreamSettingsDisplayLibrary::IsHDRPresentationActive(const FDreamSettingsDisplayCapabilities& C)
{
    const bool DisplayFormat=C.OutputFormat>=static_cast<int32>(EDisplayOutputFormat::HDR_ACES_1000nit_ST2084)
        && C.OutputFormat<=static_cast<int32>(EDisplayOutputFormat::HDR_ACES_2000nit_ScRGB);
    return C.HasWindow && C.HDRAllowed && C.PlatformSupportsHDR && C.CurrentMonitorSupportsHDR && C.OutputMetadataSupportsHDR
        && C.HDROutputEnabled && C.WindowIsHDR && DisplayFormat;
}
FText UDreamSettingsDisplayLibrary::Describe(const FDreamSettingsDisplayCapabilities& C)
{
    if (!C.HasWindow) return LOCTEXT("DisplayWindowMissing","Display information becomes available when a game window is open.");
    FNumberFormattingOptions Numbers; Numbers.UseGrouping=false;
    const FText Rate=C.DesktopRefreshHz>0 ? FText::Format(LOCTEXT("DisplayHertz","{0} Hz"),FText::AsNumber(C.DesktopRefreshHz)) : LOCTEXT("DisplayRateUnknown","Refresh rate unavailable");
    const FText HDR=IsHDRPresentationActive(C) ? LOCTEXT("HDRStatusActive","HDR output is active.")
        : !C.HDRAllowed ? LOCTEXT("HDRStatusGameDisabled","HDR output is not enabled in this game.")
        : !C.PlatformSupportsHDR || !C.CurrentMonitorSupportsHDR ? LOCTEXT("HDRStatusDisplayUnavailable","HDR is unavailable for this window. Check Windows HDR and display settings.")
        : LOCTEXT("HDRStatusAvailable","HDR is available on the current display.");
    return FText::Format(LOCTEXT("DisplayInformationHDR","{0} · {1} × {2} · {3}. This is the desktop mode, not the game's frame rate.\n{4}"),
        FText::FromString(C.MonitorName),FText::AsNumber(C.DesktopResolution.X,&Numbers),FText::AsNumber(C.DesktopResolution.Y,&Numbers),Rate,HDR);
}
#undef LOCTEXT_NAMESPACE
