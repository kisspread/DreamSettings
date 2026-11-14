# DreamSettings

Real implementations of a game's settings, for UE 5.8 - not mock sliders. Meant to be driven by DreamGUI
settings pages (see the DreamMenus project), but the settings logic itself is plain Runtime C++ so any
UI can sit on top.

The C++ registry/session/store now supports typed choices, composite presets, safe display confirmation,
recovery journals, six audio routes, asynchronous output-device transactions, per-player camera/input preferences and native Enhanced Input key profiles. A generated DreamGUI reference page presents display, graphics, audio, gameplay and controls
categories with apply, revert, per-row/category/global default reset, automatic quality detection, unsaved-close confirmation and keyboard/mouse/gamepad rebinding with swap/replace conflict prompts. Xbox, PlayStation and Nintendo button labels are a per-player previewable preference. See [integration](Docs/Integration.md)
for contracts and limitations. The reference catalogue covers the legacy EasyOptions settings and
related settings workflows; final host release acceptance is recorded in DreamMenus.

Implemented behaviour and validation boundaries:

- **Display** - HDR controls/calibration, monitor capabilities and interrupted-process display recovery are implemented and verified in the available environment. Physical HDR output and refresh-interval presentation timing remain hardware acceptance work.
- **Graphics** - per-feature visual/hardware acceptance; optional current-vendor adapters are available in DreamMenus
- **Audio** - physical unplug/replug and cold-start device restoration acceptance
- **Controls** - scrolling edges, keyboard/gamepad profiles across cold starts, optional secondary keys, category/context grouping, configurable menu shortcuts and modal focus restoration are verified. Sensitivity, inversion, FOV, dead zone and vibration gate use a native host adapter. Physical-device combinations remain a separate acceptance boundary.
- **Accessibility** - native colour correction, independent DreamGUI subtitles, interface scale, subtitle size and reduced motion support preview/revert/persistence. Both reference themes consume these preferences; custom themes must do the same. Details support long content, optional automatic scrolling and manual-reader ownership.
- **Language** - Simplified Chinese/English resources, CJK fonts, current-page glyph preparation and language restoration across native processes are implemented. The independent default-theme package has passed bilingual resource verification; each consuming host must validate its own cooked resources.
- **Host integration** - configurable scene preview and a separate gameplay benchmark entry/confirmation/interface. The host supplies the benchmark level, workload and results. The module never silently saves before handing control to it.

Layout:

```
Source/DreamSettings     Runtime core, providers, storage and automation tests
Source/DreamSettingsUI   Runtime DreamGUI FieldNotify presenter
DUI/Settings/           Default page source
Content/UI/             Generated layout asset
Content/Audio/          Optional six-channel reference SoundClasses
Docs/Integration.md     C++ integration and persistence contracts
Resources/Icon128.png
```

Independent repository: it is embedded in DreamMenus' Plugins folder but versioned on its own;
DreamMenus' .gitignore excludes it.
