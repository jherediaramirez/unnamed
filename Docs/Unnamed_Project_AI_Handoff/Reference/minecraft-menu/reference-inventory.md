# Reference inventory — released Java Edition 26.3

Target: Windows PC, vanilla Java Edition 26.3, English (US), default resources. No Bedrock, Launcher, older-version screenshots, or mods were used as visual authorities.

## Verified source identity

| Source | Identity / inspected material |
|---|---|
| Official release article and detailed release notes | Java Edition 26.3; released September 15, 2026; new Video Settings structure and menu changes |
| Mojang version metadata | `id: 26.3`, `type: release`, Java runtime 25; client SHA-1 `e877b6a07acd633fb3bb475002175cec036e7b87` |
| Exact client JAR | SHA-1 verified; native GUI textures, font definitions, English strings, credits, flat-world preset data, and inspected screen/layout classes |
| Asset index 34 | SHA-1 `1e4e4a68dece1c0f51b8f93b45da8fe8da51bcd3`; content-hash downloads verified |
| Native panorama | Six indexed 1024 × 1024 PNGs. Indexed faces override the JAR's 1 × 1 placeholder entries |
| Fonts and UI | Native logo/subtitle, sprites, bitmap font providers, indexed Unifont, official built-in pack resources |
| Sounds and translations | Indexed sound definitions, click sound, nine menu tracks, credits track; 143 languages including the JAR's English (US) |
| Native screenshots / videos | **Unverified gap:** no independently confirmed exact-version Windows captures were accessible |

Source links are in `README.md`; byte-level asset paths and hashes are in `assets/asset-provenance.json`. Client screen code was inspected through decompilation, not by running Minecraft. The release's structures and defaults were read from the exact client rather than recalled from older menus.

## Resolution and GUI comparison profiles

| Profile | Physical viewport | GUI scale | Logical size | Reference status |
|---|---:|---:|---:|---|
| Desktop | 1920 × 1080 | Auto → 4 | 480 × 270 | Derived from native scale calculation; prototype captured |
| Smaller desktop | 1280 × 720 | Auto → 3 | 427 × 240 | Derived from native scale calculation; prototype captured |
| Native Windows visual reference | Unknown | Unknown | Unknown | Missing; no native reference resolution/scale is claimed |

GUI scale increments while the next scale leaves at least 320 × 240 logical pixels; dimensions are rounded upward. Force Unicode Font applies the native even-scale constraint. The interface uses GUI pixels throughout rather than responsive web breakpoints. Render/Simulation Distance use the native ≥1 GB Java-heap profile, giving maxima of 32 chunks; the lower-memory profile has a maximum of 16.

For the desktop profile, the logo clip is 256 × 44 GUI pixels at (112, 30); the subtitle clip is 128 × 14 at (176, 67). Main buttons are 200 × 20, at y=115,139,163; the three icons are at y=187; Options/Quit are 98 × 20 at y=211. The Options FOV/Online controls are 150 × 20 at y=28, the category grid begins at y=91 with 24-pixel spacing, and Done is 200 × 20 at y=244. Video's native list width is 310, row height 25, and scrollbar width 6 at logical x=403; clipped rows stay inside the scrolling viewport.

## Screen inventory

All rows below refer to inspected 26.3 client classes, strings, and assets. The Windows-specific rendering of these screens remains unverified without native captures.

| Entry / screen | Connected screens and prototype behavior | Native source evidence |
|---|---|---|
| Main Menu | Singleplayer, Multiplayer, Realms, Friends, Language, Accessibility, Options, Quit, copyright/credits | `TitleScreen`, `LogoRenderer`, `SplashRenderer`, `Panorama` |
| Options | FOV, Online, Skin Customization, Music & Sounds, Video, Controls, Language, Chat, Resource Packs, Accessibility, Telemetry, Credits & Attribution | `OptionsScreen`, `Options` |
| Online | Friends settings, request-dependent controls, Xbox settings confirmation; no account access | `OnlineOptionsScreen`, English strings |
| Skin Customization | Seven model-part toggles and Main Hand | `SkinCustomizationScreen`, `Options` |
| Music & Sounds | Master and category volumes, subtitles, music frequency/toast, sound-device behavior | `SoundOptionsScreen`, `Options`, indexed sound definitions |
| Video Settings | Display, Quality & Performance, Preferences; native rows, preset slider and dependent state | `VideoSettingsScreen`, `Options`, `GraphicsPreset` |
| Controls | Mouse Settings and Key Binds; sprint/sneak, auto jump, sprint movement/camera controls | `ControlsScreen`, `Options` |
| Mouse Settings | Sensitivity, wheel sensitivity, discrete scrolling, X/Y inversion, cursor changes | `MouseSettingsScreen`, `Options` |
| Key Binds | 62 bindings in eight categories, capture, conflicts, individual Reset, Reset All | `KeyBindsScreen`, `KeyMapping`, binding categories |
| Language | Native 143-language inventory, search, select, Font Settings | `LanguageSelectScreen`, indexed `pack.mcmeta` / language JSON |
| Font Settings | Force Unicode Font, Japanese Glyph Variants | `FontOptionsScreen`, font definitions |
| Chat Settings | Native chat visibility, color/link settings, dimensions, opacity, delay, drafts and related controls | `ChatOptionsScreen`, `Options` |
| Accessibility | Native accessibility controls and links to Controls/Font; browser speech for narrator | `AccessibilityOptionsScreen`, `Options` |
| Resource Packs | Available/Selected columns, native default plus official built-ins, select/remove/reorder, folder/import endpoint | `PackSelectionScreen`, native transferable-list sprites |
| Telemetry Data | Required/optional events and their native property names; optional checkbox, data/policy links | `TelemetryInfoScreen`, `TelemetryEventType` registry; seven registered event types |
| Credits & Attribution | Credits, Attribution, Licenses; native-style link confirmation | `CreditsAndAttributionScreen`, `CommonLinks`, `ConfirmLinkScreen` |
| Credits scroll | Actual native credits JSON and credits audio; keyboard acceleration/reversal/skip | `WinScreen`, credits JSON, indexed `music/game/end/alpha.ogg` |
| Singleplayer | Searchable fictional world list, selection, Play/Edit/Delete/Re-Create/Create; disabled controls without selection | `SelectWorldScreen`, `WorldSelectionList` |
| Edit World | Rename and save/cancel; native backup/folder/optimization actions stop reversibly | `EditWorldScreen` |
| Create New World — Game | World name, mode, difficulty, cheats; Hardcore constraints; create endpoint | `CreateWorldScreen`, `WorldCreationUiState` |
| Create New World — World | Seed, world type, customization, structures, bonus chest | `CreateWorldScreen`, `WorldCreationUiState` |
| Create New World — More | Game Rules, Experiments, Data Packs | `CreateWorldScreen` |
| Game Rules | 59 rules, grouped/searchable; native defaults and integer validation; staged Done/Cancel | `WorldCreationGameRulesScreen`, `AbstractGameRulesScreen`, rule registry and registry-fix mapping |
| Experiments | Current experiment toggles, warning, staged changes | `ExperimentsScreen`, current feature flags |
| Data Packs | Default selected pack and native two-column structure; no import/validation engine | `PackSelectionScreen`, create-world data-pack route |
| Superflat customization | Layer selection/removal, Presets, staged Done/Cancel | `CreateFlatWorldScreen` |
| Superflat Presets | Nine actual preset definitions and order, editable layer/biome string, invalid-input endpoint | `PresetFlatWorldScreen`, native worldgen JSON and visible-preset tag |
| Single Biome customization | Searchable native biome names, staged selection | `CreateBuffetWorldScreen`, English biome strings |
| Multiplayer warning | Native safety notice and local “do not show again” state | `SafetyScreen` |
| Multiplayer list | Fictional offline entries, selection, Join, Direct Connection, Add, Edit, Delete, Refresh, Back | `JoinMultiplayerScreen`, `ServerSelectionList` |
| Add / Edit Server | Name, address validation, server resource-pack cycle; staged Done/Cancel | `ManageServerScreen`, English strings |
| Direct Connection | Address validation, keyboard submit, reversible connection endpoint | `DirectJoinServerScreen` |
| Realms entry | Native logo/feature images, Join Code, invitations/news and account/subscription endpoints | Realms client entry/PDP/join-code classes and current assets |
| Friends entry | Account-dependent reversible explanation | Current main-menu Friends button and account strings |
| Link / confirmation / message | Open, Copy, Cancel; confirmation actions; prototype endpoints | `ConfirmLinkScreen`, `ConfirmScreen`, native button styles |
| Quit | Harmless simulated exit and Return to Title | Browser limitation; no real process exit |

## Main settings schema

The full settings definitions are embedded as `DATA.definitions` in `index.html`. FOV is 30–110 in steps of 1, default 70 (Normal); 110 is Quake Pro. Max Framerate is 10–260 in steps of 10, default 120; 260 displays Unlimited. GUI Scale cycles Auto and the native scales available to the current viewport.

Video settings reproduce the release's grouped list rather than the older two-column screen: Display includes Exclusive Fullscreen Mode, Max Framerate/VSync, Reduce fps/GUI Scale, Fullscreen/Exclusive Fullscreen, Brightness/Graphics API. Quality & Performance includes the full-width Preset slider, Biome Blend/Render Distance, Chunk Builder/Simulation Distance, Smooth Lighting/Clouds, Particles/Mipmap Levels, Entity Shadows/Entity Distance, Menu Background Blur/Cloud Distance, See-Through Leaves/Improved Transparency, Texture Filtering/Anisotropic Filtering, and Weather Effect Radius. Preferences contains Autosave Indicator/Show Vignette and Attack Indicator/Chunk Fade.

Preset changes update the corresponding quality values together and manual edits select Custom. Sliders retain native ranges/steps, including Render Distance 2–32, Simulation Distance 5–32, Mipmap Levels 0–4, Entity Distance 50–500% in 25% steps, Menu Background Blur 0–10, Cloud Distance 2–128, Weather Radius 3–10, and Chunk Fade 0–2 seconds in 0.05-second steps. Hardware-only settings store state; monitor-mode selection is disabled because the browser cannot enumerate Windows display modes.
