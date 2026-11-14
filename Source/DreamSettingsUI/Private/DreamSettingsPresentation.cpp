#include "DreamSettingsPresentation.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "DreamSettingsHelp"
UDreamSettingsPresentation::UDreamSettingsPresentation()
{
    auto Add=[&](const TCHAR* Id,FText Body) { Settings.FindOrAdd(Id).Body=Body; };
    Add(TEXT("Display.WindowMode"),LOCTEXT("WindowMode","Choose fullscreen, borderless or windowed presentation. Display changes must be kept within 15 seconds or the previous mode is restored."));
    Add(TEXT("Display.Resolution"),LOCTEXT("Resolution","Choose the game's output resolution. Borderless mode follows the desktop; internal rendering resolution is controlled separately."));
    Add(TEXT("Display.VSync"),LOCTEXT("VSync","Synchronize presentation with the display to reduce tearing. This may change input latency; the result depends on the display and rendering mode."));
    Add(TEXT("Display.SyncInterval"),LOCTEXT("SyncInterval","Choose how many display refresh intervals to wait between presentations while VSync is enabled. This is not an independent monitor refresh-rate setting."));
    Add(TEXT("Display.FrameRateLimit"),LOCTEXT("FrameLimit","Limit the game's frame rate to reduce unnecessary GPU work. Zero removes this limit; VSync or other engine policies may still limit presentation."));
    Add(TEXT("Display.Gamma"),LOCTEXT("Gamma","Adjust the brightness curve of the displayed image. Check both dark and bright detail and use small changes; this does not replace HDR brightness controls."));
    auto& Gamma=Settings.FindChecked(TEXT("Display.Gamma"));
    Gamma.Image=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/DreamSettings/Help/T_Grayscale.T_Grayscale")));
    Gamma.ImageCaption=LOCTEXT("GrayCaption","Reference gray steps. This illustration is not a measurement or automatic display calibration.");
    Add(TEXT("Graphics.Preset"),LOCTEXT("Preset","Change the quality groups together. Editing a group separately produces a custom preset. Auto-detect prepares a recommendation which you can review before applying."));
    Add(TEXT("Graphics.ResolutionScale"),LOCTEXT("ResolutionScale","Change the internal render resolution relative to the output. Lower values reduce pixel work but soften the image. An active upscaler can own this value and disable manual editing."));
    Add(TEXT("Graphics.UpscalingMethod"),LOCTEXT("Upscaler","Choose an available temporal reconstruction method. Vendor methods require their plugin and compatible hardware; unavailable methods cannot be selected."));
    Add(TEXT("Graphics.UpscalingQuality"),LOCTEXT("UpscalerQuality","Balance internal resolution against reconstruction quality. Available choices and their resolution ratios depend on the selected upscaler."));
    Add(TEXT("Graphics.Sharpness"),LOCTEXT("Sharpness","Adjust supported sharpening. Strong sharpening can emphasize edges and noise; the active rendering method decides whether this control is available."));
    Add(TEXT("Graphics.AAMethod"),LOCTEXT("AAMethod","Choose how jagged edges are smoothed. Temporal methods use previous frames; upscaling methods may require a compatible anti-aliasing method."));
    Add(TEXT("Graphics.AAQuality"),LOCTEXT("AAQuality","Change the quality of the active anti-aliasing path. Its visual and performance effects depend on the selected method."));
    Add(TEXT("Graphics.ViewDistanceQuality"),LOCTEXT("ViewDistance","Adjust the view-distance quality group. The game decides which objects and detail levels respond to this setting."));
    Add(TEXT("Graphics.ShadowQuality"),LOCTEXT("Shadows","Adjust shadow quality and distance. The exact effect depends on the active shadow method and the scene."));
    Add(TEXT("Graphics.GlobalIlluminationQuality"),LOCTEXT("GI","Adjust indirect-lighting quality. Only lighting paths enabled by the game can respond to this setting."));
    Add(TEXT("Graphics.ReflectionsQuality"),LOCTEXT("Reflections","Adjust reflection quality. The result depends on the reflection method, scene and supported rendering features."));
    Add(TEXT("Graphics.PostProcessQuality"),LOCTEXT("PostProcess","Adjust the quality of the game's post-processing effects. Individual effects must also be enabled by the game."));
    Add(TEXT("Graphics.TextureQuality"),LOCTEXT("Textures","Adjust texture streaming quality. Higher quality can use more video memory; the visible result depends on the source textures and available memory."));
    Add(TEXT("Graphics.EffectsQuality"),LOCTEXT("Effects","Adjust the quality group used by visual effects. The game decides which effects and detail levels change."));
    Add(TEXT("Graphics.FoliageQuality"),LOCTEXT("Foliage","Adjust foliage quality and density where the game supports scalable vegetation."));
    Add(TEXT("Graphics.ShadingQuality"),LOCTEXT("Shading","Adjust the engine's shading quality group. The affected materials and lighting features depend on the game."));
    Add(TEXT("Graphics.VolumetricsQuality"),LOCTEXT("Volumetrics","Adjust volumetric fog grid detail and temporal sampling. Higher settings can increase GPU cost in scenes using volumetric effects."));
    Add(TEXT("Graphics.Anisotropy"),LOCTEXT("Anisotropy","Improve texture clarity when viewing surfaces at an angle. The result depends on the texture and sampler settings."));
    Add(TEXT("Graphics.MotionBlur"),LOCTEXT("MotionBlur","Adjust motion-blur quality. The game's camera and post-processing settings decide the blur amount."));
    Add(TEXT("Graphics.HardwareLumen"),LOCTEXT("HardwareLumen","Allow hardware ray tracing for Lumen when the project and graphics device support it. This does not enable ray tracing for an unsupported project or GPU."));
    Add(TEXT("Graphics.VirtualShadowMaps"),LOCTEXT("VSM","Select virtual shadow maps when supported by the project. Shadow quality and scene complexity also affect the result and cost."));
    Add(TEXT("Graphics.ContactShadows"),LOCTEXT("ContactShadows","Enable supported contact-shadow rendering for fine detail near surfaces. Lights and materials must be configured to use it."));
    Add(TEXT("Gameplay.CameraFOV"),LOCTEXT("FOV","Adjust the gameplay camera's field of view. A wider view shows more of the surroundings and changes perceived perspective."));
    Add(TEXT("Input.MouseSensitivity"),LOCTEXT("MouseSensitivity","Scale mouse look movement. This affects the game's camera adapter, not the Windows pointer speed."));
    Add(TEXT("Input.GamepadSensitivity"),LOCTEXT("GamepadSensitivity","Scale camera rotation from the gamepad stick. Rotation is time-based so the selected response is independent of frame rate."));
    Add(TEXT("Input.InvertX"),LOCTEXT("InvertX","Reverse horizontal camera look input."));
    Add(TEXT("Input.InvertY"),LOCTEXT("InvertY","Reverse vertical camera look input."));
    Add(TEXT("Input.GamepadIcons"),LOCTEXT("GamepadIcons","Choose the button-label family shown in the interface. This changes presentation, not the physical input mapping."));
    Add(TEXT("Accessibility.ColorCorrectionType"),LOCTEXT("ColorType","Choose a color-vision correction mode. Compare the color samples and adjust correction intensity for your preference."));
    Add(TEXT("Accessibility.ColorCorrectionIntensity"),LOCTEXT("ColorIntensity","Adjust color-vision correction strength. Zero preserves the uncorrected image."));
    Add(TEXT("Localization.Culture"),LOCTEXT("Language","Choose one of the game's packaged interface languages. The regional number/date locale is retained separately."));
    Add(TEXT("Audio.MasterVolume"),LOCTEXT("MasterVolume","Scale all configured audio channels. Individual channel volumes multiply with the master volume."));
    Add(TEXT("Audio.MusicVolume"),LOCTEXT("MusicVolume","Adjust sounds routed through the music channel. Listen plays a reference sound through this route."));
    Add(TEXT("Audio.EffectsVolume"),LOCTEXT("EffectsVolume","Adjust sounds routed through the effects channel. Listen plays a reference sound through this route."));
    Add(TEXT("Audio.AmbientVolume"),LOCTEXT("AmbientVolume","Adjust sounds routed through the ambient channel. Listen plays a reference sound through this route."));
    Add(TEXT("Audio.UIVolume"),LOCTEXT("UIVolume","Adjust sounds routed through the interface channel. Listen plays a reference sound through this route."));
    Add(TEXT("Audio.OutputDevice"),LOCTEXT("OutputDevice","Choose an audio output endpoint or follow the system default. Switching is asynchronous; keep the page open until the operation finishes."));
    Add(TEXT("Accessibility.UIScale"),LOCTEXT("UIScale","Change the size of the interface. The setting also affects the host's persistent DreamGUI HUD when the scale policy is installed."));
    auto& Scale=Settings.FindChecked(TEXT("Accessibility.UIScale"));
    for (const auto& Pair:TArray<TPair<double,FText>>{{.8,LOCTEXT("ScaleSmall","Smaller interface elements leave more room for content.")},{1,LOCTEXT("ScaleDefault","The interface is at its default scale.")},{1.05,LOCTEXT("ScaleLarge","Larger interface elements can reduce the number of visible rows.")}})
    { FDreamSettingsHelpVariant V; V.Minimum=Pair.Key; V.Additional=Pair.Value; Scale.Variants.Add(V); }
}
FDreamSettingsHelpContent UDreamSettingsPresentation::Resolve(FName Id,const FDreamSettingValue& Value) const
{
    FDreamSettingsHelpContent Result=Settings.FindRef(Id);
    const FDreamSettingsHelpVariant* Selected=nullptr;
    for (const auto& V:Result.Variants)
        if (Value.bToken ? V.ForToken && V.Token==Value.Token : !V.ForToken && FMath::IsFinite(V.Minimum) && V.Minimum<=Value.Number)
            if (!Selected || Value.bToken || V.Minimum>=Selected->Minimum) Selected=&V;
    if (Selected)
    {
        Result.Additional=Selected->Additional;
        if (Selected->OverrideImage) { Result.Image=Selected->Image; Result.ImageCaption=Selected->ImageCaption; }
    }
    return Result;
}
#undef LOCTEXT_NAMESPACE
