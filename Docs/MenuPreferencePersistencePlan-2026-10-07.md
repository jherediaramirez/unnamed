# Unnamed menu preference persistence review and plan

Implementation update: the recommended first batch is now implemented. See [Menu Preferences](MenuPreferences.md) and [the dated work record](Records/2026-10-07-Menu-Preferences.md). The review findings and proposed later batches below remain the original planning snapshot.

Yes. The menu can remember a player's choice before the feature that consumes it exists. Most variables already exist; native persistence, ownership and an honest indication of whether the effect is available are the missing parts.

Scope: all 33 routes reachable from the main menu, including Options, world creation and multiplayer forms. This is a review and proposed plan, not implementation. Read-only Unreal MCP queries reported /Game/Menu/L_Menu open and no Play-in-Editor session.

## Findings

1. This is an HTML/JavaScript menu hosted in Unreal SWebBrowser, not UMG widgets. Changes belong in MenuWeb/AdaptMenu.mjs and the runtime modules. Content/Menu/Web/index.html is generated. [Host](../Source/Unnamed/Menu/MenuPlayerController.cpp), [adapter](../MenuWeb/AdaptMenu.mjs).
2. state.settings already owns most requested variables, but persist() is deliberately empty and saved is empty at startup. Reload discards these choices. Another JavaScript variable alone will not solve it. [Mock transactions](../MenuWeb/Runtime/MenuPrototypeRuntime.js).
3. Five video settings, seven movement bindings, and two Hold/Toggle policies have native saving. Preserve those owners and existing values. [Video service](../Source/Unnamed/Menu/MenuVideoSettings.cpp), [input bridge](../Source/Unnamed/Menu/MenuInputBridge.cpp), [input save](../Source/Unnamed/Input/UnnamedInputSettings.cpp).
4. There are 111 distinct catalog setting IDs: seven already persist and 104 remain. Graphics API and Sound Device are fixed-choice placeholders among those 104. Language, packs, the multiplayer warning checkbox and telemetry choice sit outside that catalog. Repeated controls on multiple screens are one value.
5. All 55 remaining bindings are disabled. Recommend a separate dormant-binding path for 31 ordinary actions; defer the 23 Minecraft debug rows and fixed fullscreen remapping. No gameplay action needs to be activated to save a planned binding.
6. World drafts need their own document. newDraft() has no dataPacks member; the Data Packs page has no extra selectable packs and Done does not commit a pack list into the draft. This domain is not modeled end to end.
7. Source changed during inspection. The final reviewed HTML fixes Graphics API to Default (DirectX 12), while the earlier capture still contains OpenGL. The pre-bridge crouch label also changed. Native defaults must come from the authored input context, not old labels. The 697-record registry is historical coverage, not current runtime proof. Exact source hashes are in the companion inventory.

## Existing persistent values

| Values | Current owner | Boundary |
|---|---|---|
| Fullscreen, Max Framerate, VSync, GUI Scale, Menu Background Blur | UMenuVideoSettings; Saved/Config/MenuVideoSettings.ini | Fullscreen means windowed/borderless; GUI scale retains the preference when the viewport clamps rendering. |
| Walk Forward, Walk Backward, Strafe Left, Strafe Right, Sprint, Crouch, Jump | UUnnamedInputSettings through Enhanced Input | Preserve authored defaults and saved overrides; input intent does not move a character yet. |
| Sprint and Crouch Hold/Toggle | Same native input SaveGame | Never save held keys, current gait, stance requests or accumulated look input. |

The [JSON inventory](MenuPreferenceInventory-2026-10-07.json) includes all 111 setting IDs, all 62 binding IDs and all 59 rules, with defaults, constraints and proposed treatment.

## All remaining catalog settings

The following tables list each of the 104 remaining IDs exactly once. Ranges describe the current reference-derived menu contract, not future gameplay guarantees. Save means remember the desired value while its effect remains unavailable. Conditional Minecraft concepts should stay inert until retained or redesigned. Boolean defaults use On/Off even where the visible control displays Hold/Toggle.

Store underlying values and stable IDs, not translated labels, formatted percentages, DOM IDs or one boolean per language/biome.

### General

1 additional value.

| Control and stable ID | Default | Allowed stored value | Recommendation |
|---|---|---|---|
| FOV `fov` | 70 | 30 to 110; step 1 | Save desired degrees; define the future camera FOV convention before applying. |

### Video

25 additional values.

| Control and stable ID | Default | Allowed stored value | Recommendation |
|---|---|---|---|
| Reduce fps when `inactivityFpsLimit` | AFK | Minimized / AFK | Save the choice; its feature consumer remains pending. |
| Exclusive Fullscreen `exclusiveFullscreen` | Off | Off / On | Save requested mode only. Actual fullscreen remains windowed/borderless. |
| Brightness `gamma` | 0.5 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Graphics API `preferredGraphicsBackend` | Default | Default = Default (DirectX 12) | Reserve only. Current source fixes Default (DirectX 12) and disables the control. Do not restore old OpenGL/Vulkan choices. |
| Preset `graphicsPreset` | Fancy | Fast / Fancy / Fabulous! / Custom | Save atomically with its quality fields. Do not replay on load or overwrite functional blur. |
| Biome Blend `biomeBlendRadius` | 2 | 0 to 7; step 1 | Save the choice; its feature consumer remains pending. |
| Render Distance `renderDistance` | 12 | 2 to 32; step 1 | Save the choice; its feature consumer remains pending. |
| Chunk Builder `prioritizeChunkUpdates` | Threaded | Threaded / Semi Blocking / Fully Blocking | Conditional. Minecraft scheduling modes need an Unnamed meaning before use. |
| Simulation Distance `simulationDistance` | 12 | 5 to 32; step 1 | Save requested distance; future local-world/server authority owns the effective value. |
| Smooth Lighting `ambientOcclusion` | On | Off / On | Save intent; Smooth Lighting has no confirmed one-to-one Unreal mapping. |
| Clouds `cloudStatus` | Fancy | OFF / Fast / Fancy | Save the choice; its feature consumer remains pending. |
| Particles `particles` | All | All / Decreased / Minimal | Save the choice; its feature consumer remains pending. |
| Mipmap Levels `mipmapLevels` | 4 | 0 to 4; step 1 | Save the choice; its feature consumer remains pending. |
| Entity Shadows `entityShadows` | On | Off / On | Save the choice; its feature consumer remains pending. |
| Entity Distance `entityDistanceScaling` | 1 | 0.5 to 5; step 0.25 | Save the choice; its feature consumer remains pending. |
| Cloud Distance `cloudRange` | 128 | 2 to 128; step 1 | Save the choice; its feature consumer remains pending. |
| See-Through Leaves `cutoutLeaves` | On | Off / On | Save the choice; its feature consumer remains pending. |
| Improved Transparency `improvedTransparency` | Off | Off / On | Conditional. Saving intent does not provide the reference transparency algorithm. |
| Texture Filtering `textureFiltering` | None | None / RGSS / Anisotropic | Conditional. RGSS/Anisotropic need a supported renderer mapping. |
| Anisotropic Filtering `maxAnisotropyBit` | 2 | 1 to 3; step 1 | Save exponent 1..3 = 2x/4x/8x. Retain it when filtering disables the control. |
| Weather Effect Radius `weatherRadius` | 10 | 3 to 10; step 1 | Save the choice; its feature consumer remains pending. |
| Autosave Indicator `showAutosaveIndicator` | On | Off / On | Save indicator visibility; does not enable or schedule autosaving. |
| Show Vignette `vignette` | On | Off / On | Save the choice; its feature consumer remains pending. |
| Attack Indicator `attackIndicator` | Crosshair | OFF / Crosshair / Hotbar | Save the choice; its feature consumer remains pending. |
| Chunk Fade Time `chunkSectionFadeInTime` | 0.75 | 0 to 2; step 0.05 | Save seconds; no chunk fade implementation implied. |

### Music and sounds

16 additional values.

| Control and stable ID | Default | Allowed stored value | Recommendation |
|---|---|---|---|
| Master Volume `sound_master` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| Music `sound_music` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| Jukebox/Note Blocks `sound_record` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| Weather `sound_weather` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| Blocks `sound_block` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| Hostile Mobs `sound_hostile` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| Friendly Mobs `sound_neutral` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| Players `sound_player` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| Ambient/Environment `sound_ambient` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| Narrator/Voice `sound_voice` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| UI `sound_ui` | 1 | 0 to 1; step 0.01 | Save normalized volume 0..1; audio playback/mixer remains pending. |
| Device `soundDevice` | System Default | System Default | Reserve System Default. Only one choice exists; later enumerate native devices and store stable IDs. |
| Closed Captions `showSubtitles` | Off | Off / On | One boolean shared with Accessibility; caption generation is pending. |
| Directional Audio `directionalAudio` | Off | Off / On | Save the choice; its feature consumer remains pending. |
| Music Frequency `musicFrequency` | Default | Default / Frequent / Constant | Save the choice; its feature consumer remains pending. |
| Music Toast `musicToast` | Never | Never / Pause Menu / Pause Menu and Toast | Save the choice; its feature consumer remains pending. |

### Controls

6 additional values.

| Control and stable ID | Default | Allowed stored value | Recommendation |
|---|---|---|---|
| Attack/Destroy `toggleAttack` | Off | Off = Hold / On = Toggle | Save Hold/Toggle policy for a later attack consumer. |
| Use Item/Place Block `toggleUse` | Off | Off = Hold / On = Toggle | Save Hold/Toggle policy for a later use/place consumer. |
| Auto-Jump `autoJump` | Off | Off / On | Save the choice; its feature consumer remains pending. |
| Sprint Window `sprintWindow` | 7 | 0 to 10; step 1 | Save reference ticks; define real time units before implementing double-tap sprint. |
| Quit Shortcuts `quitShortcuts` | On | Off / On | Save intent with an inactive label; currently does not control Alt+F4. |
| Operator Items Tab `operatorItemsTab` | Off | Off / On | Save visibility preference; cannot grant operator permissions. |

### Mouse

6 additional values.

| Control and stable ID | Default | Allowed stored value | Recommendation |
|---|---|---|---|
| Sensitivity `sensitivity` | 0.5 | 0 to 1; step 0.01 | Save raw 0..1; 0.5 displays as 100 percent. Native look scaling is pending. |
| Scroll Sensitivity `mouseWheelSensitivity` | 0 | -200 to 100; step 1 | Save raw -200..100; displayed multiplier is 10^(value/100), default 1x. |
| Discrete Scrolling `discreteMouseScroll` | Off | Off / On | Save the choice; its feature consumer remains pending. |
| Invert Mouse X `invertXMouse` | Off | Off / On | Save the choice; its feature consumer remains pending. |
| Invert Mouse Y `invertYMouse` | Off | Off / On | Save the choice; its feature consumer remains pending. |
| Allow Cursor Changes `allowCursorChanges` | On | Off / On | Save the choice; its feature consumer remains pending. |

### Chat

18 additional values.

| Control and stable ID | Default | Allowed stored value | Recommendation |
|---|---|---|---|
| Chat `chatVisibility` | Shown | Shown / Commands Only / Hidden | Save the choice; its feature consumer remains pending. |
| Colors `chatColors` | On | Off / On | Save the choice; its feature consumer remains pending. |
| Web Links `chatLinks` | On | Off / On | Save the choice; its feature consumer remains pending. |
| Prompt on Links `chatLinksPrompt` | On | Off / On | Save the choice; its feature consumer remains pending. |
| Chat Text Opacity `chatOpacity` | 1 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Text Background Opacity `textBackgroundOpacity` | 0.5 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Chat Text Size `chatScale` | 1 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Line Spacing `chatLineSpacing` | 0 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Chat Delay `chatDelay` | 0 | 0 to 6; step 0.1 | Shared with Accessibility. Definition label is a translation key; default means No Delay. |
| Width `chatWidth` | 1 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Focused Height `chatHeightFocused` | 1 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Unfocused Height `chatHeightUnfocused` | 0.44366197 | 0 to 1; step 0.01 | Preserve exact default 0.44366197 despite 0.01 slider steps. |
| Narrator `narrator` | OFF | OFF / Narrates All / Narrates Chat / Narrates System | Shared with Accessibility; speech service is absent. |
| Command Suggestions `autoSuggestions` | On | Off / On | Save the choice; its feature consumer remains pending. |
| Hide Matched Names `hideMatchedNames` | On | Off / On | Conditional on future identity/moderation design; no filtering promise now. |
| Reduced Debug Info `reducedDebugInfo` | Off | Off / On | Save the choice; its feature consumer remains pending. |
| Only Show Secure Chat `onlyShowSecureChat` | Off | Off / On | Conditional on future authenticated chat design; no authenticity guarantee now. |
| Save Unsent Chats `saveChatDrafts` | Off | Off / On | Save preference only. Message text needs a separate future draft model. |

### Accessibility

17 additional values.

| Control and stable ID | Default | Allowed stored value | Recommendation |
|---|---|---|---|
| High Contrast `highContrast` | Off | Off / On | Commit together with High Contrast pack membership; appearance currently uses the baseline. |
| Text Background `backgroundForChatOnly` | On | On = Chat / Off = Everywhere | true means Chat; false means Everywhere. |
| Notification Time `notificationDisplayTime` | 1 | 0.5 to 10; step 0.1 | Save the choice; its feature consumer remains pending. |
| View Bobbing `bobView` | On | Off / On | Save the choice; its feature consumer remains pending. |
| Distortion Effects `screenEffectScale` | 1 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| FOV Effects `fovEffectScale` | 1 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Darkness Pulsing `darknessEffectScale` | 1 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Damage Tilt `damageTiltStrength` | 1 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Glint Speed `glintSpeed` | 0.5 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Glint Strength `glintStrength` | 0.75 | 0 to 1; step 0.01 | Save the choice; its feature consumer remains pending. |
| Hide Sky Flashes `hideLightningFlash` | Off | Off / On | Save the choice; its feature consumer remains pending. |
| Monochrome Logo `darkMojangStudiosBackground` | Off | Off / On | Conditional. Retain only if an equivalent loading-logo option is intended. |
| Panorama Scroll Speed `panoramaSpeed` | 1 | 0 to 1; step 0.01 | Save desired speed; animation currently uses the fixed presentation baseline. |
| Hide Splash Texts `hideSplashTexts` | Off | Off / On | Save desired visibility; presentation currently uses the fixed baseline. |
| Narrator Hotkey `narratorHotkey` | On | Off / On | Save intent; Ctrl+B has no speech consumer yet. |
| Rotate with Minecarts `rotateWithMinecart` | Off | Off / On | Conditional on retaining minecarts and that experiment. Currently disabled; reserve an inert value. |
| High Contrast Block Outlines `highContrastBlockOutline` | Off | Off / On | Save the choice; its feature consumer remains pending. |

### Skin customization

8 additional values.

| Control and stable ID | Default | Allowed stored value | Recommendation |
|---|---|---|---|
| Cape `skin_cape` | On | Off / On | Save cosmetic visibility if this avatar layer is retained. |
| Jacket `skin_jacket` | On | Off / On | Save cosmetic visibility if this avatar layer is retained. |
| Left Sleeve `skin_left_sleeve` | On | Off / On | Save cosmetic visibility if this avatar layer is retained. |
| Right Sleeve `skin_right_sleeve` | On | Off / On | Save cosmetic visibility if this avatar layer is retained. |
| Left Pant Leg `skin_left_pants_leg` | On | Off / On | Save cosmetic visibility if this avatar layer is retained. |
| Right Pant Leg `skin_right_pants_leg` | On | Off / On | Save cosmetic visibility if this avatar layer is retained. |
| Hat `skin_hat` | On | Off / On | Save cosmetic visibility if this avatar layer is retained. |
| Main Hand `mainHand` | Right | Left / Right | Save hand enum; equipment behavior is pending. |

### Font

2 additional values.

| Control and stable ID | Default | Allowed stored value | Recommendation |
|---|---|---|---|
| Force Unicode Font `forceUnicodeFont` | Off | Off / On | Save desired font; rendering currently uses the baseline. |
| Japanese Glyph Variants `japaneseGlyphVariants` | Off | Off / On | Save desired glyph variant; switching is pending. |

### Online

5 additional values.

| Control and stable ID | Default | Allowed stored value | Recommendation |
|---|---|---|---|
| Friends List `friendsList` | Off | Off / On | Local UI preference; does not create an account or activate an external service. |
| Allow Requests `allowFriendRequests` | Off | Off / On | Save child preference; effective behavior needs Friends List and an account service. |
| In-Game Notification `inGameNotification` | Off | Off / On | Save child preference; friend notification service is pending. |
| Visibility `sharePresence` | Full | Full / Limited / Hidden | Save desired privacy choice; no presence service yet. |
| Allow Server Listings `allowServerListing` | On | Off / On | Save desired privacy choice; no protocol consumes it yet. |

### Shared settings across menus

| One saved ID | Menu locations |
|---|---|
| showSubtitles | Sounds and Accessibility |
| narrator | Chat and Accessibility |
| chatOpacity, chatLineSpacing, chatDelay, textBackgroundOpacity | Chat and Accessibility |
| menuBackgroundBlurriness | Video and Accessibility; already persistent |
| highContrast plus high_contrast pack membership | Accessibility and Resource Packs; update together |

## Values outside the settings catalog

| Control or group | Stored value | Commit and ownership | Recommendation |
|---|---|---|---|
| Language list, 143 choices | One locale ID, default en_us | User preference on Done/double-click; Cancel/Escape restores committed choice | Yes, as desired language. Current text remains English. |
| Resource Packs; Select, Remove, Move Up, Move Down | Ordered IDs: programmer_art, high_contrast; default empty | User preference on Done; synchronize highContrast | Yes, as desired pack order. Default pack is implicit and cannot be removed; resource rendering is pending. |
| Multiplayer Do not show this screen again | skipWarning boolean, default false | User preference on the existing checkbox change | Yes. It already skips the warning within a session; persistence extends that behavior. |
| Optional telemetry checkbox | Explicit boolean; missing currently means false | Local preference for the prototype display | Optional. This must not authorize a later unspecified telemetry implementation; obtain a fresh choice for a real service. |
| Server Name, Server Address, Server Resource Packs | Stable local ID, trimmed name, address, Enabled/Disabled/Prompt policy | Local favorites on Done; Cancel/Escape rolls back | Yes. Save user bookmarks without a connection; keep fictional examples separate. |
| Direct Connection address | Last committed address | Local history on Join; Cancel restores previous value | Yes. This remembers text, not a successful connection. |
| World Name, Game Mode, Difficulty, Allow Cheats | Name string, Survival/Hardcore/Creative, Peaceful/Easy/Normal/Hard, commands boolean | World draft | Yes. Hardcore forces Hard, commands off and bonus chest off. |
| World Type, Seed, Generate Structures, Bonus Chest | Default/Superflat/Large Biomes/AMPLIFIED/Single Biome, seed text, two booleans | Same world draft | Yes. Preserve seed text; no unsafe numeric conversion or implied generator. |
| Single Biome selection | One stable biome ID, default minecraft:plains | Child Done commits into parent draft | Conditional on keeping this generation option. Each row is a choice, not a variable. |
| Superflat layers; Remove Layer | Ordered block-ID/height list plus flat biome ID | Child Done commits into parent draft | Conditional. Default internal order: grass 1, dirt 2, bedrock 1. |
| Nine flat presets, editable preset text; Use Preset | Canonical parsed layers and biome, optionally preset ID | Use Preset commits into flat editor, outer Done into world draft | Yes if retained. Do not lose manual edits by saving only the selected preset. Validate IDs and total height before generation. |
| Minecart Improvements, Redstone Experiments, Villager Trade Rebalance | Three booleans, all false initially | World draft on Done | Conditional on retaining these Minecraft features; no implemented effect implied. |
| Game Rules | 59 typed rule-ID/value entries | World draft on Done after every field validates | Yes as versioned draft data. Eventual world/server owns authority. Full list below. |
| Data Packs | Future ordered pack-ID list in world draft | Separate from global Resource Packs | Reserve an empty field only. No selectable extra packs or draft commit path exists yet. |
| Edit World Name | Selected world's metadata or explicit sample-draft name | Save action | Useful later. Never treat persisted fictional rows as real world saves. |
| Selected world or server | One stable record ID | Optional session convenience; clear when missing/deleted | Not needed in first persistence batch. |
| World/language/rule/biome searches, selected tab, scroll, focus | Transient UI state | Memory only | No durable preference needed. |

The nine preset buttons are Classic Flat, Tunnelers' Dream, Water World, Overworld, Snowy Kingdom, Bottomless Pit, Desert, Redstone Ready, The Void. They update one layer/biome configuration.

Current world-draft defaults are New World, Survival, Normal, commands off, Default world type, blank seed, structures on, bonus chest off and plains biomes. Rule defaults are listed below. Missing experiment entries mean false.

Unfinished draft recovery differs from creation defaults: every new world should not silently inherit an old name and seed. Define an explicit save/resume path if durable drafts are added. Child Done commits into the draft; Cancel discards uncommitted changes. Current Create New World only opens a message and is not a durable commit point.

## Remaining keybinding rows

Recommend saving 31 ordinary planned bindings through a separate dormant map. Store the physical key/chord and intended context without activating input actions. Preserve Escape cancel, Backspace unbind, modifier sides and current movement reservations. Warn about unresolved future context conflicts, then resolve them before activation; do not compare all debug, inventory, creative and spectator keys as one namespace.

| Action | Stable ID | Reference default | Proposed treatment |
|---|---|---|---|
| Open/Close Inventory | `key.inventory` | E | Save dormant binding |
| Swap Item With Off Hand | `key.swapOffhand` | F | Save dormant binding |
| Drop Selected Item | `key.drop` | Q | Save dormant binding |
| Use Item/Place Block | `key.use` | Right Button | Save dormant binding |
| Attack/Destroy | `key.attack` | Left Button | Save dormant binding |
| Pick Block | `key.pickItem` | Middle Button | Save dormant binding |
| Open Chat | `key.chat` | T | Save dormant binding |
| List Players | `key.playerlist` | Tab | Save dormant binding |
| Open Command | `key.command` | / | Save dormant binding |
| Friends Screen | `key.friends` | O | Save dormant binding |
| Social Interactions Screen | `key.socialInteractions` | P | Save dormant binding |
| Take Screenshot | `key.screenshot` | F2 | Save dormant binding |
| Toggle Perspective | `key.togglePerspective` | F5 | Save dormant binding |
| Toggle Cinematic Camera | `key.smoothCamera` | Not Bound | Save dormant binding |
| Toggle Fullscreen | `key.fullscreen` | F11 | Defer remapping; F11/Alt+Enter remain active |
| Advancements | `key.advancements` | L | Save dormant binding |
| Quick Actions | `key.quickActions` | G | Save dormant binding |
| Toggle GUI | `key.toggleGui` | F1 | Save dormant binding |
| Toggle Spectator Shader Effects | `key.toggleSpectatorShaderEffects` | F4 | Save dormant binding |
| Hotbar Slot 1 | `key.hotbar.1` | 1 | Save dormant binding |
| Hotbar Slot 2 | `key.hotbar.2` | 2 | Save dormant binding |
| Hotbar Slot 3 | `key.hotbar.3` | 3 | Save dormant binding |
| Hotbar Slot 4 | `key.hotbar.4` | 4 | Save dormant binding |
| Hotbar Slot 5 | `key.hotbar.5` | 5 | Save dormant binding |
| Hotbar Slot 6 | `key.hotbar.6` | 6 | Save dormant binding |
| Hotbar Slot 7 | `key.hotbar.7` | 7 | Save dormant binding |
| Hotbar Slot 8 | `key.hotbar.8` | 8 | Save dormant binding |
| Hotbar Slot 9 | `key.hotbar.9` | 9 | Save dormant binding |
| Save Hotbar Activator | `key.saveToolbarActivator` | C | Save dormant binding |
| Load Hotbar Activator | `key.loadToolbarActivator` | X | Save dormant binding |
| Highlight Players | `key.spectatorOutlines` | Not Bound | Save dormant binding |
| Select On Hotbar | `key.spectatorHotbar` | Middle Button | Save dormant binding |
| Toggle Overlay | `key.debug.overlay` | F3 | Defer until Unnamed debug feature and chord are designed |
| Debug Modifier Key | `key.debug.modifier` | F3 | Defer until Unnamed debug feature and chord are designed |
| Debug Crash | `key.debug.crash` | C | Defer until Unnamed debug feature and chord are designed |
| Reload Chunks | `key.debug.reloadChunk` | A | Defer until Unnamed debug feature and chord are designed |
| Show Hitboxes | `key.debug.showHitboxes` | B | Defer until Unnamed debug feature and chord are designed |
| Clear Chat | `key.debug.clearChat` | D | Defer until Unnamed debug feature and chord are designed |
| Show Chunk Boundaries | `key.debug.showChunkBorders` | G | Defer until Unnamed debug feature and chord are designed |
| Show Advanced Tooltips | `key.debug.showAdvancedTooltips` | H | Defer until Unnamed debug feature and chord are designed |
| Copy Data | `key.debug.copyRecreateCommand` | I | Defer until Unnamed debug feature and chord are designed |
| Cycle Spectator | `key.debug.spectate` | N | Defer until Unnamed debug feature and chord are designed |
| Game Mode Switcher | `key.debug.switchGameMode` | F4 | Defer until Unnamed debug feature and chord are designed |
| Debug Options | `key.debug.debugOptions` | F6 | Defer until Unnamed debug feature and chord are designed |
| Toggle Lost Focus Pause | `key.debug.focusPause` | P | Defer until Unnamed debug feature and chord are designed |
| Dump Dynamic Textures | `key.debug.dumpDynamicTextures` | S | Defer until Unnamed debug feature and chord are designed |
| Reload Resource Packs | `key.debug.reloadResourcePacks` | T | Defer until Unnamed debug feature and chord are designed |
| Start/Stop Profiling | `key.debug.profiling` | L | Defer until Unnamed debug feature and chord are designed |
| Copy Location | `key.debug.copyLocation` | C | Defer until Unnamed debug feature and chord are designed |
| Dump Version Info | `key.debug.dumpVersion` | V | Defer until Unnamed debug feature and chord are designed |
| Profiling Chart | `key.debug.profilingChart` | 1 | Defer until Unnamed debug feature and chord are designed |
| Fps Charts | `key.debug.fpsCharts` | 2 | Defer until Unnamed debug feature and chord are designed |
| Network Charts | `key.debug.networkCharts` | 3 | Defer until Unnamed debug feature and chord are designed |
| Lightmap Texture | `key.debug.lightmapTexture` | 4 | Defer until Unnamed debug feature and chord are designed |
| Toggle Improved Transparency | `key.debug.improvedTransparency` | X | Defer until Unnamed debug feature and chord are designed |

The 23 debug rows include repeated base keys and a Debug Modifier, so plain displayed letters are insufficient. Debug Crash is not a useful new gameplay feature. The fullscreen row is special: a saved remap should not appear active while the fixed F11/Alt+Enter routing still owns the shortcut.

## Complete world rule catalog

All 59 entries can live in a draft without a simulation. These are Minecraft reference rules and bounds, not promises that Unnamed will implement each. Preserve a namespace/version and migrate supported rules when the real world contract is chosen. Client preferences never silently change an existing server's rules.

| Rule | Stable ID | Default | Stored type and current bounds |
|---|---|---|---|
| Advance time of day | `advance_time` | On | Boolean |
| Update weather | `advance_weather` | On | Boolean |
| Allow Nether | `allow_entering_nether_using_portals` | On | Boolean |
| Drop blocks | `block_drops` | On | Boolean |
| In block interaction explosions, some blocks won't drop their loot | `block_explosion_drop_decay` | On | Boolean |
| Enable Command Blocks | `command_blocks_work` | On | Boolean |
| Broadcast command block output | `command_block_output` | On | Boolean |
| Deal drowning damage | `drowning_damage` | On | Boolean |
| Do elytra movement check | `elytra_movement_check` | On | Boolean |
| Thrown Ender Pearls vanish on death | `ender_pearls_vanish_on_death` | On | Boolean |
| Drop entity equipment | `entity_drops` | On | Boolean |
| Deal fall damage | `fall_damage` | On | Boolean |
| Deal fire damage | `fire_damage` | On | Boolean |
| Fire spread radius | `fire_spread_radius_around_player` | 128 | Integer, -1 to 2147483647 |
| Forgive dead players | `forgive_dead_players` | On | Boolean |
| Deal freeze damage | `freeze_damage` | On | Boolean |
| Global sound events | `global_sound_events` | On | Boolean |
| Respawn immediately | `immediate_respawn` | Off | Boolean |
| Keep inventory after death | `keep_inventory` | Off | Boolean |
| Lava converts to source | `lava_source_conversion` | Off | Boolean |
| Require recipe for crafting | `limited_crafting` | Off | Boolean |
| Enable player Locator Bar | `locator_bar` | On | Boolean |
| Broadcast admin commands | `log_admin_commands` | On | Boolean |
| Command modification block limit | `max_block_modifications` | 32768 | Integer, 1 to 2147483647 |
| Command context limit | `max_command_forks` | 65536 | Integer, 0 to 2147483647 |
| Command chain size limit | `max_command_sequence_length` | 65536 | Integer, 0 to 2147483647 |
| Entity cramming threshold | `max_entity_cramming` | 24 | Integer, 0 to 2147483647 |
| Minecart max speed | `max_minecart_speed` | 8 | Integer, 1 to 1000 |
| Snow accumulation height | `max_snow_accumulation_height` | 1 | Integer, 0 to 8 |
| Drop mob loot | `mob_drops` | On | Boolean |
| In mob explosions, some blocks won't drop their loot | `mob_explosion_drop_decay` | On | Boolean |
| Allow destructive mob actions | `mob_griefing` | On | Boolean |
| Regenerate health | `natural_health_regeneration` | On | Boolean |
| Do player movement check | `player_movement_check` | On | Boolean |
| Player's Nether Portal delay in creative mode | `players_nether_portal_creative_delay` | 0 | Integer, 0 to 2147483647 |
| Player's Nether Portal delay in non-creative mode | `players_nether_portal_default_delay` | 80 | Integer, 0 to 2147483647 |
| Sleep percentage | `players_sleeping_percentage` | 100 | Integer, 0 to 2147483647 |
| Projectiles can break blocks | `projectiles_can_break_blocks` | On | Boolean |
| Enable pvp | `pvp` | On | Boolean |
| Do raids | `raids` | On | Boolean |
| Random tick speed rate | `random_tick_speed` | 3 | Integer, 0 to 2147483647 |
| Reduce debug info | `reduced_debug_info` | Off | Boolean |
| Respawn location radius | `respawn_radius` | 10 | Integer, 0 to 2147483647 |
| Send command feedback | `send_command_feedback` | On | Boolean |
| Announce advancements | `show_advancement_messages` | On | Boolean |
| Show death messages | `show_death_messages` | On | Boolean |
| Enable Spawner Blocks | `spawner_blocks_work` | On | Boolean |
| Spawn mobs | `spawn_mobs` | On | Boolean |
| Spawn Monsters | `spawn_monsters` | On | Boolean |
| Spawn pillager patrols | `spawn_patrols` | On | Boolean |
| Spawn phantoms | `spawn_phantoms` | On | Boolean |
| Spawn Wandering Traders | `spawn_wandering_traders` | On | Boolean |
| Spawn Wardens | `spawn_wardens` | On | Boolean |
| Allow spectators to generate terrain | `spectators_generate_chunks` | On | Boolean |
| Vines spread | `spread_vines` | On | Boolean |
| Allow TNT to be activated and to explode | `tnt_explodes` | On | Boolean |
| In TNT explosions, some blocks won't drop their loot | `tnt_explosion_drop_decay` | Off | Boolean |
| Universal anger | `universal_anger` | Off | Boolean |
| Water converts to source | `water_source_conversion` | On | Boolean |

## Buttons that should remain actions

Do not create saved booleans such as hasClickedPlay or createWorldPressed. A command can read saved choices without being a preference itself.

| Buttons | Treatment |
|---|---|
| Singleplayer, Multiplayer, Friends List, Language, Accessibility, Options, category links and tabs | Open/navigate screens. Persist their contents where appropriate. |
| Back, Done, Cancel, Proceed, Save, confirmation choices, Use Preset | Commit, roll back or navigate a domain. Preserve their transaction boundaries. |
| Play Selected World, Create New World, Re-Create, Join Server, Refresh | World/network commands. Parameters may be stored; no success behavior is needed in this phase. |
| Add Server, Edit, Delete, Restore Entry, pack selection/order, Remove Layer | Validated list/draft mutations. Save the resulting committed records, not button state. |
| Quit Game, Hard Reset, Reset binding, Reset Keys | Existing workflow commands. Extend save/flush/reset coverage for new domains. |
| Open Pack Folder, Open World Folder, Open Backups Folder, Make Backup, Optimize World, Reset Icon | File/world operations. No useful selected value to save. |
| Credits, Attribution, Licenses, Accessibility Guide, Xbox Settings, Privacy Statement, Give Feedback, Show Data, Open Link, Copy | View/navigation/external actions. Do not invent success flags. |
| Exclusive Fullscreen Mode slider | Disabled monitor-mode placeholder. Reserve a future monitor ID, dimensions and refresh-rate descriptor; do not save its dummy 0/1 thumb position. |

## Proposed implementation order

### 1 Define the preference contract

Create a native catalog with stable IDs, type, default, bounds/choices, owning domain and effect availability. Use this inventory as an audit seed; preserve native video/input defaults and resolve current source/capture differences first. Return canonical state to the browser so translated labels and repeated controls cannot become different settings.

Track persistence separately from effect support: saved does not mean applied. A concise status such as “Saved for later; feature not available yet” should accompany controls with no consumer. Platform-fixed informational controls remain disabled. Do not imply restarting can activate a renderer feature that has no adapter.

The existing video and Enhanced Input stores remain authoritative. The new store must not duplicate those seven existing catalog fields or the seven movement bindings.

### 2 Add native storage for ordinary preferences

Add a restricted UMenuPreferences service and browser adapter following the existing MenuVideo/MenuInput bridge pattern. A versioned native-owned file such as Saved/Config/MenuPreferences.json can hold ordinary preferences, desired locale and ordered resource-pack IDs. Expose bounded snapshot/change/commit/reset/flush operations for known IDs, never arbitrary paths, console commands or UObject properties.

Validate types, finite numbers, ranges, enums and list IDs. Preserve explicit false, zero and empty values. Permit exact existing defaults, including the non-grid unfocused chat-height default. Missing old fields receive defaults. Malformed or future-version files must not be destructively overwritten; retain a recoverable last-good copy and use checked write/replace semantics. Report an unsaved error honestly and acknowledge persistence only after a successful write.

Save completed edits when changed, coalescing slider drags. Rehydrate before enabling edits and revision-check async replies. PIE and MenuSettingsSessionOnly remain nonpersistent; tests use isolated user directories and files. Keep current Quit deadlines and retry behavior.

### 3 Connect eligible controls and preserve dependencies

Route scalar edits through the new adapter. Add explicit commits for language, packs and warning/telemetry fields, which bypass ordinary setSetting(). Update the registry and screen evidence only from captures of the exact resulting HTML.

Commit coupled changes together: graphics preset plus its quality fields, and highContrast plus pack membership. Do not replay the preset during load: the current Fancy default label differs from its preset expansion (render distance defaults to 12 but that preset writes 16). Restore individual saved values as authoritative and use Custom when appropriate. A preset must not overwrite the functional blur.

Keep child preferences when temporarily disabled, unless a deliberate rule requires resetting them. Currently Friends List off clears Allow Requests and In-Game Notification; the recommended persistence design retains their choices and gates effective behavior. Anisotropy should likewise remember its value when Texture Filtering disables its control.

### 4 Add dormant bindings and structured drafts separately

Extend UUnnamedInputSettings with a versioned dormant-binding map, or an equivalent typed input store, for the 31 recommended rows. Do not register them in the active movement context. When an action is implemented, resolve context/chord conflicts and migrate the saved choice once into Enhanced Input, preserving the user's override. Keep debug bindings and fixed fullscreen remapping deferred.

Save user server favorites/history separately from fictional examples. Add durable world drafts after defining save/resume and Cancel semantics; include rules, experiments, biome, layers and a future data-pack list. Do not serialize the entire browser state, callbacks, navigation stack, native observed values or fictional progress. No world generation, networking, audio playback or gameplay implementation is required by these storage batches.

### 5 Extend reset and shutdown deliberately

New stores participate in pending-edit drain, checked saving and final native flush. A failed new save must not be silently ignored by Quit. Hard Reset must explicitly reset new preference fields; document reload alone stops being a reset when those values persist. Define separately whether it clears favorites/history and how dormant bindings reset. World drafts and actual world/player saves stay outside preference reset by default.

Child Done commits to its parent draft, not globally. Cancel/Escape cannot leak uncommitted language, packs, server or world fields into durable storage. Ordinary immediate preference pages retain their change-on-edit behavior. Report partial failures across independent stores instead of claiming an all-or-nothing reset when only some domains were written.

### 6 Verify memory separately from feature behavior

- Native round trips for each eligible ID; defaults, false/zero, allowed enums/ranges, old-version migration, unknown IDs, corrupt/incomplete files and write failures.
- Browser coverage for every control/shared alias, slider completion, dependencies, coupled transactions, nested Done, Cancel/Escape, and invalid rule text hidden by a search filter.
- Full document reload and independent packaged-process relaunch restore choices while preserving existing video/input values.
- Actual CEF/native checks at 1280x720 and 1920x1080 cover scrolling, focus, staged dialogs and redraw. Use new capture names; old evidence remains historical.
- Quit with pending new edits and failure injection; confirmed reset; preservation of old packages, authored input defaults, unrelated settings and world/player files.
- Dormant input does not activate gameplay. Stored audio/world/chat/network choices do not accidentally gain effects. Each future consumer needs its own functional acceptance; a persistence test cannot certify its effect.

## Recommended first batch

Implement the native preference store and eligible ordinary scalar, locale and resource-pack choices. Keep Graphics API and Sound Device informational and disabled Minecraft-specific rows reserved. Preserve the existing five video values, seven movement bindings and two policies. Follow with dormant bindings and server favorites. Handle durable world drafts as a separate batch because their commit/resume behavior differs from personal preferences.

## Review evidence

- Source snapshot: 2026-10-07T16:40:24.102Z.
- Reviewed HTML SHA-256: 2024d23c87f23eb1f651c076a317925ee00f47de8d23d1d1b7587068e8943130.
- Historical registry HTML SHA-256: 9182c3021bd2307f3efa135be2bfbdfc37220923d79732733c43904e4c0411c9; matches this snapshot: false.
- Direct source review includes the seven runtime-added skin settings. Table generation checks 111 catalog IDs, 104 remaining IDs, 55 remaining binding rows and 59 rules for complete coverage.
- Unreal MCP was used only to read the current level and PIE state. No editor interaction, setting mutation, build, package, test suite or service launch was performed by this review.
- This task added only this plan and its JSON inventory under Docs. It did not edit existing source/assets/configuration/evidence. Concurrent work changed source independently during inspection.
- Exact file hashes and structured records are in [MenuPreferenceInventory-2026-10-07.json](MenuPreferenceInventory-2026-10-07.json).
