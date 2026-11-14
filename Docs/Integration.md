# DreamSettings integration

## Current delivery

UE 5.8.4, Windows PC. Two Runtime modules: `DreamSettings` contains definitions, registry, sessions,
providers and persistence; `DreamSettingsUI` contains the DreamGUI FieldNotify presenter. Neither
depends on DreamMenus. The plugin descriptor requires DreamGUI because the default UI ships in this
same plugin. The current catalogue includes display modes, resolution, VSync/divisors, frame limits, gamma,
ten scalability groups, graphics presets, volumetric presets and common rendering overrides.
The reference UI generates category tabs and rows from that registry. Vendor upscaling, complete audio/input,
accessibility and bilingual release acceptance are still in progress; this is not the full product.

## Open a page from C++

1. Enable DreamSettings and DreamGUI. Add `DreamSettingsUI` and `DreamGUI` to the host module's dependencies.
2. Compile `Plugin.DreamSettings:Settings/Settings.dui` with DreamMCP. Save the generated
   `/DreamSettings/UI/WBP_DreamSettings` asset. Include that asset explicitly in the host's cook rules.
3. Create `UDreamSettingsViewModel` with a world-aware outer, and retain it in a `UPROPERTY`.
4. Call `Initialize(ULocalPlayer*)`. An initialization failure is exposed through `Status` and `Ready`.
5. Create the generated `UDreamUserWidget` using `CreateDreamWidget`. Pass the presenter to
   `SetViewModel("Settings", ViewModel)` in the before-alive callback. Attach it to that player's
   `UDreamScreenUISubsystem` screen root. Set scaling on the **screen root canvas**.
6. Wire `OnClosed` to the host's navigation/input restoration. `RequestClose` asks before discarding.
   Pair `Shutdown` with host teardown; this discards uncommitted audio previews and releases the lease.
7. Route platform characters to DreamGUI. With no custom viewport client, add this to DefaultEngine.ini:

```ini
[/Script/Engine.Engine]
GameViewportClientClassName=/Script/DreamGUIInput.DreamGameViewportClient
```

An existing viewport client can inherit `UDreamGameViewportClient`, or forward its `InputChar` override
to `DreamUITextInputRouter::RouteViewportCharacter` (declared in `Interaction/DreamUITextInputTarget.h`)
after console handling and before the base implementation. Keyboard navigation alone does not prove
this character route exists; validate actual typing. The engine's base viewport client leaves fields
using a US-keyboard fallback and does not provide correct character input for other layouts.

`ADreamMenusSettingsController` is the integration example, not a plugin requirement. Its input modes
are deliberately specific to the standalone demo. An existing game's router should restore its own
previous input mode and focus instead of copying that controller verbatim.

## Settings contracts

### Categories and catalogue overrides

Tabs are registry data, not UI strings. `RegisterCategory` adds a tab with its own localized label,
description and order; a category used by a definition but never registered is still listed, sorted by
its earliest row order and labelled with its raw ID. `Categories()` returns the merged, sorted list and
is what the presenter renders. Prefer registering a category before its first definition so the label
is translated rather than synthesized.

After the built-in catalogue is created, a host may adjust it without replacing providers:

```cpp
FDreamSettingsCatalogOverride Override;
Override.HiddenSettings.Add(TEXT("Graphics.UpscalingMethod"));
Override.SettingLabels.Add(TEXT("Display.VSync"), LOCTEXT("VSync", "V-Sync"));
Override.CategoryLabels.Add(TEXT("Display"), LOCTEXT("DisplayTab", "Video"));
Override.CategoryOrder.Add(TEXT("Display"), 5);
Subsystem->ApplyCatalogOverride(Override);
```

`ApplyDreamSettingsCatalogOverride` validates the whole request first; an unknown ID, an empty label
or an unknown category rejects it without applying any entry. Overrides take effect through
`Find`/`Categories` and therefore reach sessions, rows and validation, but they never touch a
provider, a stored value or a schema. Hiding removes a row from the page; it does not disable the
engine feature, so a host that must not expose HDR should also avoid saving it. Call the subsystem
entry point before the first settings page opens; it refuses while a session is open.

`EDreamSettingPresentation` declares how a row should be shown: `Percent` for percentage formatting
and parsing, `AudioSample` for the audition tone, `SubtitleSample` for the subtitle preview,
`HDRCalibration` for the brightness pattern and `HDRPeak` for the pair's peak point. A host-defined
setting that sets these flags gets the same controls as the plugin's own rows. Definitions without
flags fall back to the built-in ID and category checks, so existing catalogues behave unchanged.

`FDreamSettingCategory::Content` does the same for page capabilities. `DisplayInformation` shows the
effective resolution/mode block, `ScenePreview` offers the live scene, `ColourSamples` shows the
colour-vision swatches and `GameBenchmark` hosts the benchmark action. The built-in Display, Graphics
and Accessibility tabs receive these flags automatically, so an unmodified catalogue is unchanged;
a host tab opts in through `RegisterCategory` or `OverrideCategory`. Category identity therefore
never decides behaviour for a host that names its tabs itself.

`FDreamSettingsRegistry::Register` accepts a stable ID, localized label/description, kind, default,
range, step, preview policy, dependencies and an `IDreamSettingProvider`. Register extensions before
opening a session. `Validate` rejects duplicate IDs, missing dependencies and cycles.
`FDreamSettingValue` holds either a number or a stable string token. Choice IDs, not localized labels or
array indices, are persisted. Definitions can contain nonselectable display-only choices (Custom),
scalar sentinels (Automatic/Scene default), capability conditions, and hidden backing settings.
Composite definitions use `Derive` and `Expand`, with dependencies on their ordinary backing settings.
`StageBatch` validates the entire deferred batch before changing the draft. Composite values themselves
are not saved: their real backing values are the source of truth.

A provider reads the actual backend and writes only its own value, verifying acceptance. It must not
save files. Engine quality groups may change other providers; the session reads the whole registry after
each write, adopts side effects for unedited settings and reapplies explicit overrides in dependency order. `Stage` normalizes values, immediately previews
preview-enabled settings, and keeps deferred settings in the draft. `Reset` stages defaults without
applying them. `Revert` restores the opening/last-committed baseline, which may be non-default.

`ResetCategory(CategoryId)` stages the available ordinary defaults of just that category in one
operation, then recomputes derived controls. Other categories retain their drafts. A conflicting key
or unavailable default choice rejects the whole reset; no sibling is partially reset and nothing is
saved. Empty or unknown category IDs are errors, not aliases for a global reset. Like `Reset`, this
is a deferred operation: `Apply` commits the combined draft and `Revert` cancels it. The default UI
offers both **Reset category** (`ResetCurrentCategory` on the presenter) and **Reset all**
(`ResetDefaults`); both use the existing modal, capture and busy-state guard.

The session exposes Pending, Applied and Persisted separately. Dirty compares the draft to the
current session's committed baseline, not to defaults. Opening starts from actual engine values;
the disk snapshot remains available separately. If the host modified runtime settings without
saving, merely opening this page does not overwrite them with disk values.

`Apply` checks for external mutations and applies in dependency order. A clean page periodically adopts
engine startup/window changes; a dirty draft retains conflict protection. Ordinary changes then save.
Display mode and resolution changes first journal the safe state and wait 15 seconds for confirmation.
`Confirm` validates the actual window mode/viewport dimensions before saving. `Reject`, timeout and close
restore the baseline. The GameInstance ticker uses monotonic real time, including while gameplay is paused.
A custom store must implement `BeginRiskyChange`; its default fails closed. Failure restores
the previous committed runtime values and preserves the draft for retry. A rollback conflict or
failed backend restoration is reported separately. Destruction attempts preview cleanup and logs
failures. Only one process-wide machine-settings session can be open at once. The session requires
a LocalPlayer from its owning GameInstance; split-player preference storage is a later extension.

## Real engine integration

- `Display.VSync` reads `r.VSync`, uses `ECVF_SetByGameSetting`, and updates the existing
  `UGameUserSettings`. Higher-priority console/command-line policy is reported as an error. P1 does
  not call `ApplySettings` or apply other scalability values. This controls the game's presentation;
  the editor viewport's separate VSync setting is not the test target.
- `Audio.MasterVolume`, `MusicVolume`, `EffectsVolume`, `AmbientVolume`, `UIVolume` and `VoiceVolume`
  update actual SoundClass volumes. Configure the six soft references in
  `[/Script/DreamSettings.DreamSettingsConfiguration]` in the host's DefaultGame.ini.
  Master alone can fall back to `UAudioSettings::DefaultSoundClassName`. Other channels must
  be distinct descendants of Master. Missing or invalid routes are disabled with a reason.
  Sounds must use those classes or their descendants; unrelated routes are not controlled.
  Runtime changes do not save or dirty the assets. PIE teardown restores their original values.
- The page's Test sound and each channel's Listen command generate a short, quiet sine tone in C++
  through the requested route. Closing the page stops playback. The included `/DreamSettings/Audio`
  classes are optional host assets, not hard-coded route choices; configure and cook the classes
  your game uses. A child at 25% under Master at 50% has an effective gain of 12.5%.
- `Audio.OutputDevice` is a stable endpoint ID or `system`, never a localized device name.
  It enumerates devices asynchronously, checks the actual mixer endpoint after a swap, and
  follows the Windows default when `system` is selected. Switching is disabled in PIE because
  its audio environment is shared with the editor. Missing saved devices fall back with a message.

## Persistence

Uses the current `GGameUserSettingsIni`, preserving unrelated sections. VSync is stored as the
native `bUseVSync` under the current GameUserSettings class section. Native resolution, confirmed mode and
frame limit fields are synchronized too. Preferences live in `[DreamSettings.Preferences]`, with
`SchemaVersion=2`. Numbers retain their schema-1 representation; stable tokens use `id:` plus Base64.
`[DreamSettings.DisplayRecovery]` is in the same atomic file, so confirmation and journal removal are one
replacement. An interrupted unconfirmed display change is recovered during deferred settings initialization (first game tick or first menu open). Engine config cache is updated only after
successful disk replacement so subsequent `SaveSettings` retains the plugin section.

Writes use a temporary sibling file and Windows `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`.
A failed replacement keeps the original file; it is never deleted first. Future schema versions
and invalid numeric preferences are rejected without overwriting them. There is no silent reset of
corrupt preferences. Schema 1 numeric preferences remain readable and migrate on the next save.
Unknown or unavailable-plugin preferences are preserved. Simultaneous writes by a separate external process
remain outside this store contract.

## Verification

Run only `Automation RunTests DreamSettings.Core` for core coverage. Tests use fake backends and a
unique Saved/Automation INI, never old EasyGameUI assets or the player's real preferences. They cover
non-default clean state, previews, cancellation, failure compensation, retry, external mutations,
duplicate/missing/cyclic definitions, cold reads, read-only destination failure and future schema.

UI acceptance also requires live PIE binding/event readback, layout/preview, actual provider values,
and cold reopening. Full bilingual fonts/text, key/gamepad navigation, theme factories and standalone
shipping cook are tracked in the host plan; a successful P1 compile is not their acceptance.

## Optional upscaler adapters

The core and UI remain two Runtime modules. Separate DreamSettingsDLSS, DreamSettingsFSR and DreamSettingsXeSS plugins implement `IDreamSettingsUpscaler` and register `DreamSettings.Upscaler` with `IModularFeatures` at PostEngineInit. They depend on their official vendor plugins; DreamSettings itself has no vendor link dependency. Install adapters before starting the game; unloading an adapter during a live settings session is not supported.

Standalone GameInstance subsystems are constructed before PostEngineInit modules. Catalog creation and preference recovery therefore run on first game tick or first menu open, after adapter registration. `GetRegistry()` can return null before that point.

`Graphics.UpscalingMethod` atomically stages compatible AA, temporal flags, vendor quality and render percentage. `Graphics.UpscalingQuality` derives from real backing values and asks the selected SDK for supported modes. Native AA edits switch back to engine upscaling. Direct render-percentage editing is disabled for vendor methods; quality controls still update that backing value. The automatic graphics benchmark preserves vendor quality.

`RelatedChanges` expands ordinary choices into an atomic draft, `OptionAvailability` rejects unsupported choices, and `EditCondition` blocks direct editing without blocking composite writes. Explicitly staged values remain authoritative after group side effects, even when they equal the opening value. `Expand` receives pending values so a quality selection can use the pending method and output resolution.

FSR and XeSS internally promote some CVars to SetByCode. Adapters preflight policy ownership and restore the pre-call priority after their own SDK calls, allowing subsequent user edits and compensation. External higher-priority ownership is rejected. No vendor source changes are required.

## Asynchronous providers

An independent backend can declare `bAsyncWrite` and return `EDreamSettingsError::Pending`
from Write. The owner must keep ticking the session and retaining the backend until PollWrite
finishes, including after Close requests cancellation. This is neither success nor failure:
no preferences are committed while it is pending. Editing is locked, but the game thread is not.
Preview, dependency-ordered and composite providers cannot declare asynchronous writes.
Async backends may mutate only their own value. They are applied before risky display writes,
so the display confirmation countdown cannot be consumed by an audio driver wait.

On completion, readback verifies the requested value. Failure, failed persistence or cancellation
restores the prior state; restoration can itself be asynchronous. The GameInstance subsystem
retains a closing session until restoration finishes, even if its UI is destroyed.
Destroying the owning GameInstance ends this lifetime; it is not a safe way to wait for rollback.

DynamicOptions and DynamicDescription expose live endpoint metadata. `bRetryUnavailable` retries
failed reads, and `bFollowExternalChanges` adopts hardware fallback as the new baseline without
discarding other edits or an explicit device draft. An unplugged pending choice is rejected on Apply.

UE's device-swap API has no cancellation handle. If a driver never acknowledges, the service
continues holding the operation and does not falsely report completion or allow a competing swap.
After a successful callback, physical endpoint verification is bounded to ten seconds. Physical
unplug/replug and a driver that never calls back still require hardware acceptance.

## Local player camera and input preferences

Implement the native `IDreamSettingsGameplayAdapter` interface on the host's camera/input owner,
then call `UDreamSettingsSubsystem::AttachGameplayAdapter(LocalPlayer, Adapter)` after its camera
and input components exist. The interface reads and writes actual host state and reports rejection.
The plugin does not require a particular pawn, controller, HUD or camera class. An unconfigured
host gets disabled controls with an explanation. Open the page after attaching the adapter.

The current player catalog contains horizontal FOV, mouse/controller sensitivity, X/Y inversion,
controller look dead zone and vibration. All are previewable and revertible. `FDreamLookPreferences`
provides reusable look math: mouse displacement is independent of frame duration; controller input
uses a radial dead zone with a rescaled outer range and a per-second rotation rate. The host must
actually use those values; storing an unused preference does not satisfy the adapter contract.

Each session combines the machine registry with its LocalPlayer catalog. Player preferences map to
`Player.<platform-user-id>.<SettingId>` in the existing atomic INI (controller ID is the fallback
when the platform user ID is unavailable). Session/UI IDs remain unchanged. The scoped store
preserves other players and includes their committed preferences in display recovery snapshots;
machine settings stay shared and retain the process-wide editing lease. These preferences are
distinct from the coordinated Enhanced Input key-profile serialization described below.

The LocalPlayer subsystem caches effective values when replacing a host adapter across a world
transition. A closing session whose camera temporarily disappeared retries restoration after the
new consumer attaches. Full seamless-travel and split-screen acceptance are still pending.

The DreamMenus reference uses a native floating camera pawn and `/Game/Input/IMC_SettingsDemo`.
It binds real Enhanced Input actions, blocks gameplay input while the page is open, and can reopen
the menu with Escape/controller Menu. The four movement actions expose primary and secondary
keyboard slots to EnhancedInputUserSettings. `SetupInputComponent` may precede UserSettings
creation: listen to `OnPostUserSettingsInitialized` and register the IMC then as well. The earlier
`AddMappingContext(bNotifyUserSettings=true)` call is not replayed automatically.

## Native key profiles and coordinated saving

Configure EnhancedInputDeveloperSettings.UserSettingsClass as
`/Script/DreamSettings.DreamSettingsInputUserSettings` (or a compatible subclass), enable user settings,
and register player-mappable IMCs before opening the page. The session snapshots the active profile's
registered button slots. Mapping identity includes profile, mapping name, slot and hardware.
Numeric preferences stay in the scoped INI; keys use standard Enhanced Input SaveGame serialization.

The default store coordinates both files under ProjectSavedDir using a recovery journal. Recovery
runs during GameInstance subsystem initialization, before local player settings load. Custom stores
that participate must implement PrepareSave and NotifyCommitted. Do not concurrently call native
SaveSettings or modify these files in another process. A failed recovery blocks new settings sessions.

UE 5.8 GGameUserSettingsIni is a cache key. Filesystem writes resolve the config branch IniPath;
cache updates continue to use the cache key. Cache synchronization must not call EmptySection:
that can flush an empty section over a just-committed file. The implementation updates keys in
memory, and regression tests verify unchanged disk bytes and a subsequent native config flush.
Split-screen, cold-start and actual interrupted-process acceptance are still in progress.

The reference page uses UDreamSettingsKeySelector, a DreamGUI InputKeySelector subclass. Its Slate
input preprocessor captures before Enhanced Input/menu routing, including mouse buttons and wheels,
and consumes the corresponding release. Visuals remain DreamGUI. Escape/controller Menu cancel;
keys are single buttons, not chords. The presenter locks other editing while listening and offers
validated atomic swap/replace/cancel for conflicts. Rejected captures restore the displayed key.

The reference host also registers `/Game/Input/IMC_GamepadDemo`, with primary/secondary gamepad
buttons for vertical movement. `DreamMenus.SetupGamepadInput` creates these separate assets only
when none already exist. Hosts provide their own registered mappings; the plugin does not require
these reference actions. Open the session only after all intended contexts have registered.

For a button mapping with a primary slot but no secondary slot on the same hardware device, the
catalog adds an unbound secondary row. Opening or editing the page does not mutate Enhanced Input;
the first Apply builds an empty native slot from the registered source mapping and its exact device
identity, then calls normal `MapPlayerKey`. This also supports generic/custom device identifiers
which are not present in the platform hardware registry. Native auto-creation alone cannot resolve
those identifiers. Defaults for user-added secondary slots remain unbound across re-registration
and SaveGame reload. Existing authored secondary defaults and stable binding IDs are preserved.
The source context must still be registered when creating a slot; otherwise the write is unavailable.

## Controller button presentation

`Input.GamepadIcons` is a scoped, previewable preference with stable `xbox`, `playstation`, and
`nintendo` tokens. It changes labels, not physical input assignments. The player subsystem restores
it independently of the gameplay adapter when opening settings; hosts can also call
`LoadPresentationSettings` explicitly. `GetGamepadIconFamily` and `OnGamepadIconFamilyChanged`
let other game prompts use the same preference and react to preview/revert.

The default selector renders button letters/symbols with DreamGUI text. Bind its `DisplayKey` and
`IconFamily` properties to the row's `SelectedKey` and `IconFamily`. This preserves branded labels
on equal-value binding updates and restyling, while listening/unbound prompts remain localized.
Override native `FormatButtonLabel` or supply another DreamGUI row for custom presentation.
There is no automatic brand detection: Windows controller translation can obscure the brand.

## Navigation scopes

Attach native `DreamSettingsNavigationScope` to the page and its dialogs. Set the root's
`bRequestedActive=true`; bind each dialog's `bRequestedActive` to its presenter confirmation flag.
A DUI `if` controls visibility, not the behaviour's enabled lifecycle, so automatic OnEnable scope
activation would let hidden dialogs capture navigation. `InitialFocusName` names the preferred
control. The scope waits for dynamic controls and restores remembered focus after page bindings
reenable it. Back routes through the C++ presenter, including safe display rejection.

The reference host enables DreamGUI's Slate input source with `SlateInputConsumePolicy=WhenHandled`
and `InputModeWithoutScope=Game`. UI-handled keys must not also open the menu through gameplay input.
Its BackKeys list uses raw INI key names: Escape, Gamepad_FaceButton_Right, Gamepad_Special_Right.
Adapt the host input configuration to other screens; the reusable scope itself uses Menu mode.
Basic navigation, modal return, cross-category/reopen row memory and forward Tab scrolling across
multiple rows are verified. Physical devices, split-screen/travel and complete scrolling-edge
acceptance remain pending.

For stable page memory, enable `bRememberPageFocus` on the root settings scope only. Attach a
`DreamSettingsFocusAnchor` to each dynamic row, binding `SettingId` to `Row.NavigationId`, and to
each category control, binding `CategoryId` to its category `Id`. Do not derive these identities
from translated labels. The presenter exposes `OnNavigationChanging` before rows are replaced
or shut down. The page scope records a setting ID and the named child-control path in the current
local player's `DreamSettingsNavigationMemory`, never a persistent widget pointer. Each category
has its own last row. Reopening uses the remembered category; missing categories fall back to Display.
Missing/disabled controls fall back to another navigable control in that row, then its category.
Restored row controls are revealed through their containing DreamScrollBox. Modal return still
prefers the native stack's valid live focus. This memory is presentation-only and not saved to disk.

## Native colour correction

The machine registry provides `Accessibility.ColorCorrectionType` and
`Accessibility.ColorCorrectionIntensity`. It uses Slate's native final-window correction, with
integer renderer levels 0–10 (distinct from the UMG wrapper's documented normalized 0–1 input).
The service owns and initializes this process-wide state; the renderer has no public getter, so
hosts must route writes through the registry. External direct renderer writes cannot be detected.
The service is disabled in PIE to protect the shared editor window, and on non-rendering hosts.

The reference page includes labelled DreamGUI colour samples. Standalone validation captures the
final presented backbuffer and verifies three modes, zero intensity, revert and INI persistence.
This does not replace cold-start, HDR and additional-device acceptance.

## Language and localization resources

`Localization.Culture` is a machine-wide previewable preference. The provider calls native
`SetCurrentLanguage`, preserving the user's regional locale and asset-group language overrides.
It is unavailable in PIE, where changing language would also change the editor. Stable culture
identifiers come from `DreamSettingsConfiguration.SupportedCultures` (default `en`, `zh-Hans`);
the reference selector displays each culture's native name. Unsupported cultures and missing
non-native plugin `.locres` files are unavailable, rather than appearing to apply successfully.

The plugin descriptor loads its `Content/Localization/DreamSettings` target in game processes.
Ship that target with the plugin; a host's own translated text belongs in its separate target.
Configure the host's `[Internationalization] LocalizationPaths`, packaging `CulturesToStage`,
and an ICU `InternationalizationPreset` covering the cultures (the reference host uses EFIGSCJK).
Use a DreamGUI font with CJK glyphs; the reference host selects the same
`/DreamGUI/DefaultFont_DistanceField_CJK` font as DreamGUIDemo.

The reference project's `Config/Localization/*_Gather.ini` collects this plugin's C++/UI plus
the three optional adapter sources, and separately the host C++/Input assets. It never gathers
the obsolete EasyGameUI assets or vendor SDKs. Run UE GatherText with those configs, apply/check
the reviewed glossary using `python Tools/localize_settings.py apply` / `check`, then run the
`*_Compile.ini` configs. The JSON glossary, PO/archive/manifest and compiled locres are versioned;
re-gather after adding or changing source text. Adding a new language also requires translated
resources and host packaging/font support; editing the supported-cultures list alone is insufficient.

The reference page attaches `DreamSettingsTextPreparation` to its root. Opening a page or changing
the native language activates `PreparingText`; an opaque final overlay named `TextPreparationCover`
binds to that flag and contains a font-free DreamGUI throbber. Keep the page underneath alive so its
normal shaped glyphs can be requested. The component checks the page's font/fallback queues without
blocking the game thread, then leaves two quiet frames for geometry/atlas updates before releasing
the overlay and restoring actions. The page navigation scope restores focus after this temporary
disabled state. Custom themes can replace the overlay and set `CoverName`; they must retain an opaque
font-independent loading layer. This is UI presentation policy, not a dependency of the Core provider.

## Native subtitles

`Accessibility.Subtitles` previews the engine's real `SetSubtitlesEnabled` state and participates
in the same revert/default/save transaction as other settings. It is disabled in PIE (process-wide
state), and when the host forces native subtitles off. The provider reads the native value, so
external changes are not hidden behind a second cached boolean.

`UDreamSettingsSubtitleViewModel` listens to `FSubtitleManager::OnSetSubtitleText`; the engine retains
cue timing and priority while DreamGUI draws `Caption`/`HasText`. Initialize it with a local player
and call Shutdown during host teardown. It listens only in Game worlds. The engine delegate is
process-wide, so this is shared native subtitle output, not a per-player dialogue routing service.
Native toggle/forced-off changes clear existing text even when the native manager emits no empty cue.

The reference host retains a separate subtitle model and `/DreamSettings/UI/WBP_DreamSubtitles`
widget behind the settings page, so closing settings does not remove gameplay captions. Include this
asset in the host's cook rules alongside WBP_DreamSettings. Its bottom band and typography can be
replaced with a themed DUI. A host using another subtitle renderer can use the setting provider alone.

The settings model owns a separate preview listener, available through its FieldNotify `Subtitles`
object. Row.HasSample/Row.SampleLabel selects audio Listen or subtitle Preview. The four-second sample
uses the native movie-subtitle owner API; stopping it removes only that owner's sample. Native audio
cues still use SubtitleManager.QueueSubtitles. Custom dialogue middleware must route text through
the native manager or supply its own DreamGUI presenter.

## Reduced menu motion

`Accessibility.ReducedMotion` is a player-scoped presentation preference, default off, with immediate
preview, revert and coordinated INI persistence. It loads without a gameplay adapter. Custom menus
can read `UDreamSettingsPlayerSubsystem::GetReducedMotion()` and subscribe to `OnReducedMotionChanged`.
Core does not depend on DreamGUI and does not disable gameplay or camera animation.

Attach `UDreamSettingsMotionPolicy` to the DreamGUI root that owns the menu. With a `Settings` view
model it resolves the local player automatically; other roots call `BindPlayer(LocalPlayer)`.
The policy makes ScrollBox wheel/navigation scrolling and native focus-ring moves immediate, and
sets Throbber animation off, including hidden loading layers. It follows rebuilt widgets and restores
each instance's previous values when the preference turns off or the policy is detached. It does not
edit shared style assets. Pointer drag momentum, custom tweens, material animation and particle effects
remain host-controlled; subscribe to the same preference when implementing them in a theme.

The native focus ring requires DreamGUI's `SetAnimDuration`/`GetAnimDuration` API. Blueprint handlers
overriding selection events implement their own motion policy. Keep a visible static focus indication.

## Interface scale and subtitle size

Player presentation preferences also include `Accessibility.UIScale` (0.80–1.25, step 0.05, default 1)
and `Accessibility.SubtitleSize` (18–40, step 2, default 24). Both load without a gameplay adapter and
provide native change delegates on `UDreamSettingsPlayerSubsystem`. Preview, defaults, revert and
persistence use the normal session. The size is in authored interface units; canvas scaling also
affects the final pixel size of subtitles in that canvas.

For a `ScaleWithScreenSize` canvas, add `UDreamSettingsScalePolicy` to the persistent screen root and
call `BindPlayer(LocalPlayer)` once after configuring its reference resolution. It divides the authored
reference resolution by the preference, preserving the canvas's aspect/match rule. The reference host
keeps this root after settings close, so its HUD stays scaled. The policy restores the original
reference resolution on detach and reapplies on reactivation. Do not attach one to each page sharing
the same canvas. Rebind after changing the theme's reference resolution. Binding returns false for
other scaling modes; custom/DPI themes consume `GetUIScale()` and `OnUIScaleChanged` in their own sizing
policy, without modifying engine-wide UMG settings.

`UDreamSettingsSubtitleViewModel` exposes FieldNotify `FontSize`, `BandSize` and `PreviewSize`, updated
from the player's size preference. Bind the actual text size and available rectangle together. In DUI,
bind FontSize directly and attach `DreamSettingsSubtitleSizing` to a SizeBox, binding its `MinimumSize`
to the model's BandSize or PreviewSize. The component forwards changes to the SizeBox's native setter;
direct bindings to layout containers or nested AnchorData fields are not currently supported by DUI. Native
subtitle output still requires a Game world, but font preferences can be inspected in PIE. The reference
band reserves at least 4.5 font units of height and the preview 2.5, with ordinary SizeBox measurement
allowing text to grow beyond the minimum; unusually long cues need a host-specific
wrapping/paging policy and must be validated with that game's dialogue.

## HDR backend (integration in progress)

The core catalog includes `Display.HDR`, `Display.HDRCalibration`, `Display.HDRPeakNits` and
`Display.HDRPaperWhite`. HDR enable uses the unsafe display confirmation transaction; a confirmed
request must also match the current window's HDR presentation. Calibration and brightness preview
through native GameUserSettings and renderer CVars and support compensation after display capability
loss. Parameter controls require an active HDR presentation; PIE writes are unavailable.

The store mirrors all four native fields atomically. Do not call GameUserSettings.SaveSettings outside
the coordinator. A forced SDR launch policy preserves the stored HDR request when other preferences
are saved. A host must enable its own HDR policy and validate its renderer and UI composition before
shipping HDR: the reference host currently runs SDR, and HDR pixels, DreamGUI HDR composition and a
calibration pattern have not yet passed acceptance. Native setter/CVar readback alone is insufficient.

## Held value arrows

`UDreamSettingsStepButton` extends DreamGUI's native button. Bind `OnStep` to the row's `Previous`
or `Next` action and bind `RepeatEnabled` to `IsScalar`. Keep the inherited button's normal press/release
activation methods. The reference layout uses the same arrow for choices and toggles with repetition
disabled, so a held toggle cannot cycle repeatedly.

A short press emits one step on click. Default repeat timing is a 0.4 second delay, a 0.15 second
interval, then a 0.05 second interval after 1.5 seconds. Themes can change `InitialDelay`,
`RepeatInterval`, `AccelerationDelay` and `AcceleratedInterval`. Timing uses real time, with at most
one edit per UI tick and no catch-up burst after a stalled frame. Release after repetition emits no
additional step. Pointer exit cancels the current hold; re-entering does not resume it until a new
press. Disabling, hiding/removing the widget and application deactivation also cancel it.

All actions still go through the setting session, including range/step validation, preview and revert.
The control does not write configuration files. Bind the value action only to `OnStep`, rather than
also to `OnClicked`. Temporary ticking is enabled only while a repeating press is held. This requires
DreamGUI's fix for opting out of ticking inside `NativeOnDisable`; the reference host also keeps
screen-space UI active while gameplay is paused.

## Direct numeric entry

The reference scalar row also provides `UDreamSettingsNumberInput`: bind `BoundText` to the row's
`NumericText` and `OnNumberSubmitted` to `CommitNumberText`. Its local edit is separate from the latest
session value. Enter or focus loss submits; Escape cancels. If another source updates the session
during editing, the local text stays intact, and cancelling reveals the newer session value without
writing the old activation value back. Disabling or removing the control discards its local edit.

The presenter validates the entire input, then uses `Session.Stage` for range, step and backend checks.
Rejected text restores the current value and reports the reason through the row's description. Audio
volume and UI scale use percentage units, with an optional `%`; other values use their original units.
The decimal separator is `.`, with no grouping separators or exponent syntax. Full-width decimal
digits, signs, point and percent characters are accepted. Typing does not save preferences.


## Setting help presentation

Pass a `UDreamSettingsPresentation` to `UDreamSettingsViewModel::SetPresentation` to customize help
without changing backend definitions. Its `Settings` map uses the registry's stable setting IDs.
Empty `Body` falls back to the definition's description. Token variants match exactly; scalar variants
select the highest `Minimum` no greater than the current value. `OverrideImage` can replace or clear
the base image and caption. These are presentation assets and never saved player preferences.

Bind a details entry to `OpenDetails` and its tooltip to `Details.Title`; bind reset tooltips to
`Row.ResetHint`. The reference DUI uses `ShowDetails`, an independent navigation scope, a scrollable
body and a close button outside the scroll. `RequestClose` closes this modal first. Default values
come from definitions, while unavailable/validation state stays separate from the help body.

Images load asynchronously only while details are open. Bind the whole `Details.ImageBrush`, and
preserve its proportions with ScaleToFit. Closing or changing targets cancels the prior request.
In editor builds the presenter also waits for texture compilation before publishing dimensions.
Caption text is exposed early for font preparation. Include custom presentation assets and soft
referenced textures in the host's asset/cook rules, and custom FText asset paths in localization
gather settings. Independent host packaging of the new help assets remains to be verified.


## Host-selected layouts

`UDreamSettingsLayout` is a primary data asset containing `ScreenClass` and optional `Presentation`
soft references in the `UI` asset bundle. The default class is the plugin's neutral page. A host can
preload that bundle, then call `Resolve` before opening a session. Resolve also loads synchronously
when necessary; a broken explicit reference returns an error and never silently changes themes.
Set the resolved help and call `Layout->ConfigureViewModel(Presenter)` before `Initialize`, then bind the presenter as `Settings` on
the selected DreamGUI widget. The host retains ownership of its screen canvas, input mode, session
lifetime and close callback. Replacing a layout never changes providers or saved values.

`VisibleCategories` and `InitialCategory` restrict a page to part of the catalogue. An empty list
keeps every registry category; a non-empty list shows only those tabs in that order, which is how a
host embeds, for example, just display and audio in a pause menu. A restricted page always opens at
`InitialCategory` (or the first visible tab) rather than the player's remembered last visit, so an
embedded page starts where its host placed it. Category shortcuts cycle within the restricted list.
Restriction is presentation only: settings in hidden tabs still load, apply and save, and
another page without a restriction shows them normally. A single-category embed still includes the
page header and navigation chrome; replacing that layout is the `ScreenClass` path.

Settings rows render from `for Row in Settings.Rows` inside the page DUI. DreamGUI components cannot
currently take a view model as a prop and a `for` body only binds single-hop member paths, so a row
cannot be extracted into a reusable `RowTemplate` component. Themes therefore repeat the row block
and any row-level change must be made in each theme. Keep the row's focus anchor, control paths and
the presenter's row view model intact when copying it.

The DreamMenus host demonstrates a configurable `SettingsLayout` and a separate Warcraft layout
under `/Game/UI/Warcraft3`. That host path is not a dependency of this plugin. Register layout assets
with AssetManager and include their UI bundle and help textures in the host cook rules; config-only
soft references alone are not a packaging acceptance result.

### Live scene behind the menu

Layouts allow scene preview by default (`bAllowScenePreview`). Display and graphics pages offer
**Show scene / Hide scene**. Set `bHideBackgroundForGraphicsOptions` to start each visit to the
graphics category with the scene visible, or disable `bAllowScenePreview` for a host with no useful
scene behind its menu. A manual choice applies only to the current category visit. These are
presentation options, not saved graphics preferences.

A custom theme binds its decorative background's render opacity to `BackgroundOpacity` and may
show a translucent contrast layer when `ScenePreviewActive`. Bind the action's visibility to
`ScenePreviewAvailable`, label to `ScenePreviewLabel`, and click to `ToggleScenePreview`. The bundled
themes use a dark translucent layer to keep controls readable while the live 3D scene shows through.
The presenter suspends compositing during modal dialogs, loading, input capture and busy operations,
then resumes the requested view. The label continues to describe that requested view during font
preparation, so its eventual text is prepared before the cover disappears. Closing discards the
presentation choice; toggling never edits a setting, applies a rendering preset or saves a file.

### Automatic description scrolling

Layouts expose `bAutoScrollDescriptions` (true by default). The bundled details scroll boxes attach
`UDreamSettingsDescriptionScroll`: after two seconds, overflowing content moves at 18 local UI units
per second, pauses for two seconds at each end, then reverses. The component's `UnitsPerSecond`,
`InitialDelay` and `EndPause` are editable by a theme. Text that fits does not move; the editor's design
preview does not animate it.

The player's reduced-motion preference and the Controls, Keyboard/mouse and Gamepad categories
disable automatic reading. Font preparation pauses it. Any scroll from outside this component,
including wheel, keyboard, dragging or a host's explicit scroll command, gives the reader control
for the rest of that details visit. Closing and reopening starts a fresh visit, including a
close/open pair in the same frame. Movement is bounded and elapsed time is capped after a hitch;
the component neither edits nor saves a setting. A custom theme attaches it to a `Native.ScrollBox`
under the `Settings` presenter and applies layout options through `ConfigureViewModel`.

### Input mapping groups

`UDreamSettingsLayout::InputGrouping` selects `DisplayCategory` (the default), `MappingContext`,
or `None`. Register the player's mappable Enhanced Input contexts before opening settings. The
catalog reads each mapping's display category and every registered context that supplies that
mapping for the active profile and device. Optional secondary slots inherit that metadata.

`InputContextLabels` supplies localized titles keyed by the context's soft object path; an empty
override falls back to `UInputMappingContext::ContextDescription`. Unnamed categories or contexts
display **Other bindings**. A mapping shared by multiple contexts appears once per device/slot,
under a combined context title. Distinct context identities remain distinct even if their visible
titles match. Category identity uses source text, and context identity uses sorted asset paths;
translated titles never become persistent setting IDs. Joined titles retain `FText` localization
history, so changing the language updates them without recreating the input profile.

Custom row templates bind a noninteractive heading's visibility to `Row.HasGroupHeading` and its
text to `Row.GroupTitle`. Keep the setting's focus anchor and control paths intact. Grouping only
reorders presentation rows; it does not alter defaults, create native slots, apply a draft or save.
`ConfigureInputGrouping` can update an open presenter. Changes made during capture, a modal or a
busy operation are deferred until the page is usable, preserving focus and pending values.

### Dynamic text readiness

The bundled `UDreamSettingsTextPreparation` covers the page while its actual text geometry requests
are pending. It listens to language changes, page activation, details/benchmark/reset prompts and
`OnRowsPresented`. Replacing a category or regrouping its rows therefore prepares newly introduced
glyphs before exposing the first interactive frame. Selecting the same unchanged category does not
restart preparation. The component waits for bindings/layout and font uploads without blocking the
game thread; `PreparingText` disables editing and navigation restores focus when the page is ready.

Keep this behaviour and a font-independent cover in custom themes. Programmatic callers that select
a category and then edit controls or open details should wait for `ActionsEnabled`, just as player
input does. Direct session operations remain independent of presentation readiness.

### Menu shortcuts

`UDreamSettingsLayout::MenuShortcuts` configures four presentation actions. Defaults are Q/E and
the controller shoulders for previous/next category, F5/controller View/Share for Apply, and F6/controller
left face button for Reset. Each action accepts an array of keys; an empty array disables it. These
are menu commands, independent of the player's saved gameplay bindings. The footer binds
`MenuShortcutHint`, including the selected controller-brand labels.

The page accepts only its user's unmodified, non-repeat key press. Active text editing, key capture,
modal dialogs, text preparation and busy operations suppress these commands. Category navigation
wraps around and retains each category's focus bookmark and all pending edits. Apply uses the same
transaction as the Apply button and does nothing on a clean page. Reset opens a choice of the named
setting and current category, with Cancel initially focused. Both choices stage defaults; neither
implicitly applies or saves. Existing row, category and global reset buttons remain available.

`ConfigureMenuShortcuts` strips invalid, analog, mouse, touch, modifier and reserved native
submit/back/directional keys. Duplicate assignments keep the first action in the order previous,
next, apply, reset. An enabled gameplay benchmark shortcut has priority over these actions; hosts
should assign distinct keys. A custom DUI binds `ConfirmReset`, `ResetSelectedLabel`,
`ResetCategoryLabel`, `CanResetSelected`, `ConfirmResetSelected`, `ConfirmResetCategory` and
`CancelResetOptions`, and includes a modal navigation scope and the text preparation behaviour as
the bundled themes do. The page routes Back to cancel the prompt and restores the previous focus.

### Host gameplay benchmark

**Game benchmark** is separate from **Auto-detect**. Auto-detect still stages scalability
recommendations. A gameplay benchmark belongs to the host: its level, measured workload, results
screen and return path are not implemented by this settings module.

Implement `IDreamSettingsGameBenchmark` from `DreamSettingsGameBenchmark.h` and register one shared
instance per local player using `UDreamSettingsSubsystem::SetGameBenchmark(Player, Launcher)`.
`Availability` returns a localized reason when the host cannot run it. `Start` accepts or rejects
the launch synchronously; success means the request has been accepted, not that the benchmark has
finished. Queue travel or external UI work until the call returns: the settings presenter closes
after acceptance. The host then owns the asynchronous benchmark and return navigation. Keep weak
references to UObjects in a native launcher and unregister with `SetGameBenchmark(Player, nullptr)`
on teardown. Calling the setter again also refreshes an open page's capability display.

Launch requires the owning local player's active, open, clean session with no pending display
confirmation or asynchronous settings write. Reentrant starts are rejected. The subsystem keeps
a local shared reference during dispatch, so a launcher can unregister itself safely. Failure
keeps the settings page open and displays the host's message; nothing implicitly applies or saves.

`UDreamSettingsLayout` configures `bEnableGameBenchmark`, `bEnableGameBenchmarkInShipping` (false
by default), and `GameBenchmarkKeys` (F8 and the controller's top face button by default). An empty
key array disables dedicated input and leaves the Graphics-page action. With a registered host and
configured keys, the action/shortcut is available throughout the menu. The bundled pages show an
unavailable reason when no host is registered. Configuration is applied through `ConfigureViewModel`.

The page scope accepts only the owning user's unmodified, non-repeat key press. Numeric editing,
key capture, modal dialogs and busy/dirty states prevent launch. Button and shortcut both call
`RequestGameBenchmark`, which opens a confirmation with Cancel initially focused. Confirmation
calls `ConfirmGameBenchmarkLaunch`; Back calls `CancelGameBenchmark`. Custom themes should include
the confirmation scope, font preparation behaviour and the existing action guards as the bundled
pages do. The confirmation's captions are prepared before its cover retires.

