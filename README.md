# DreamSettings

Real implementations of a game's settings, for UE 5.8 - not mock sliders. Meant to be driven by DreamGUI
settings pages (see the DreamMenus project), but the settings logic itself is plain Runtime C++ so any
UI can sit on top.

Planned scope (nothing implemented yet - this is the scaffold):

- **Display** - resolution, window mode, refresh rate, VSync, HDR; detect supported modes
- **Graphics** - quality presets and per-effect scales (shadow, texture, AA, post, view distance...), scalability system
- **Audio** - master / music / SFX / voice channels over SoundClasses, output device
- **Controls** - sensitivity, inversion, key rebinding (Enhanced Input), gamepad
- **Accessibility** - colour-blind filters, subtitles, UI scale, motion reduction
- **Language** - culture switching, font fallbacks

Layout:

```
Source/DreamSettings   Runtime module (scaffold)
Content/               future fonts / icons / assets
Resources/Icon128.png
```

Independent repository: it is embedded in DreamMenus' Plugins folder but versioned on its own;
DreamMenus' .gitignore excludes it.
