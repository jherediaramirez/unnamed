# Menu & HUD Redesign — What We Have

Phase 1 of the redesign: an inventory of the two Minecraft 26.3 references and the current Unreal build, with a first Keep / Change / Later / Remove call for every HUD element. No source, config or Content changed in this phase.

Decisions so far:

- **Completely new look.** Minecraft fonts, textures, logo and panorama are reference only and will not ship in the redesign. The references tell us *what* the HUD and menu must show and how they behave, not how they look.
- **HUD first**, then the menu.
- Design work happens in the Claude Design canvas **"Unnamed — HUD & Menu Redesign"**. It holds the reference renders, the element inventory, a style-free zone layout and three new-look directions (A Field Kit, B Hearth, C Quiet), each with Everyday / In danger / Creative states.

## HUD reference — `Reference/minecraft-hud-26.3`

- **Origin.** Added from `minecraft-hud-26.3.zip`. The sha256 is recorded in `Reference/source-archive-sha256.txt`, and the files are byte-identical to the archive: 245 files, about 3.7 MB.
- **What it is.** Research material, not an implementation. It contains:
  - 194 original vanilla 26.3 PNGs, 29 JSON/MCMeta files and 3 Unicode font archives;
  - `HUD-RESEARCH.md`, which covers layout formulas, gameplay meaning, visibility rules and Unreal recommendations;
  - `PNG-GUIDE.md`, which explains every PNG;
  - `catalog.html` and `layout.html`, which are viewers;
  - `manifest.csv` / `manifest.json`, which list every PNG with its hash;
  - `evidence/`, with provenance and verification records;
  - `tools/verify_reference.py`.
- **PNGs by group:**

| Group | PNGs |
|---|---|
| Health | 44 |
| Effect icons | 40 |
| Boss bars | 22 |
| Toasts | 13 |
| Camera/item overlays | 11 |
| Locator | 8 |
| Hunger | 6 |
| Player list | 6 |
| Fonts | 5 |
| Attack indicator | 5 |
| Context bar | 5 |
| Spectator | 5 |
| Hotbar | 4 |
| Mode switcher | 3 |
| Air | 3 |
| Armor | 3 |
| Mount hearts | 3 |
| Chat/notifications | 3 |
| Effects | 2 |
| Tooltips | 2 |
| Crosshair | 1 |

- **Overlap with the menu reference.** `sprites/hud`, `sprites/boss_bar` and `sprites/toast` are byte-identical to the copies already in `Reference/minecraft-menu/assets`. New in this package:
  - the 40 effect icons;
  - camera overlays;
  - fonts and their providers;
  - spectator and mode-switcher art;
  - ping icons;
  - the research documents.
- **What the research itself did not verify:**
  - no captured vanilla frames, so the layout is source-derived;
  - no browser render of the HTML viewers;
  - no Unreal import.
- **New in this phase.** We rendered `layout.html` in headless Chromium in four states (default, under pressure, Hardcore with a mount, Creative). These renders are on the canvas.

### Finding: the research targets code that is not in GitHub

`HUD-RESEARCH.md` §11 and `evidence/preservation.json` describe a local first-person foundation that this repository does not contain:

- `Source/Unnamed/Foundation/` (`FoundationController`, `FoundationActions.h`, `FoundationSimulation.h`, `FoundationRuntimeProbe`);
- `Source/Unnamed/Menu/MenuPlayerController.*`;
- `Source/Unnamed/UnnamedProjectSettings.cpp`;
- `Source/Unnamed/Tests/SuperFlatTests.cpp`;
- `Docs/Foundation/`.

According to the research, that code already has a text crosshair, F3 diagnostics, temporary hotbar text and Left Shift sprint / C crouch. It does not have real health, food, inventory or damage.

**That local work needs to be pushed before any HUD implementation starts.** Otherwise the HUD would be built against a project state that no longer exists.

## HUD element inventory

The calls are a first pass, to be confirmed on the canvas. "Needs" names the game system that must exist before the element shows real data. Until then, the element shows labelled preview data.

| Element | Where | Needs | Call | Why |
|---|---|---|---|---|
| Hotbar, 9 slots | Bottom centre | Inventory, item rendering | Keep | Core loop. Restyle the slots; keep counts, durability and cooldown. |
| Offhand slot | Beside hotbar | Inventory, handedness | Keep | Shown only when the offhand holds something. |
| Selected item name | Above vitals | Inventory | Keep | Fades after about 2 s. |
| Health (44 heart variants) | Above hotbar, left | Health, damage, effects | Change | Keep the meaning. Convey poison, wither, frozen and absorption without 44 sprites. |
| Armor row | Above health | Equipment | Change | Fold into the health group as one shield value. |
| Food row (+ Hunger) | Above hotbar, right | Hunger, saturation | Keep | Restyle; keep the low-food shake. |
| Air bubbles | Above food | Drowning | Keep | Underwater only. Stays visible until refilled. |
| Mount hearts | Replaces food | Mounts | Later | Not until mounts exist. |
| XP bar + level | Above hotbar | Experience | Keep | Shares one track with locator and jump. |
| Locator bar | XP track | Waypoints | Change | Move to a top compass strip so it no longer competes with XP. |
| Mount jump bar | XP track | Mounts | Later | Comes with mounts. |
| Crosshair (inverting) | Screen centre | Camera | Keep | Our own design, readable on snow and in caves. |
| Attack indicator | Crosshair or hotbar | Combat cooldown | Change | One placement, under the crosshair. |
| Boss bars (7 colours × 4 notch styles) | Top centre, stacking | Boss events | Keep | Restyle with fewer colour variants. |
| Effect tiles (40 icons) | Top right, two rows | Status effects | Change | Add remaining-time text; vanilla only pulses. |
| Toasts | Top-right queue | Advancements, recipes, tutorial | Keep | Restyle into one toast style. |
| Chat | Bottom left | Chat, commands | Keep | Needed for commands even in single player. |
| Subtitles / captions | Bottom right | Audio events | Keep | Accessibility. |
| Action-bar message | Above item name | Game messages | Keep | Short status lines. |
| Title / subtitle | Screen centre | Game messages | Keep | Big moments such as death and area names. |
| Saving indicator | Bottom right | Save system | Keep | Small and quiet. |
| Debug overlay (F3) | Full screen | Diagnostics | Keep | Keep the project's existing F3. |
| Camera overlays (vignette, pumpkin, frost, spyglass, nausea, underwater, fire, portal) | Full screen | Rendering, post-process | Change | These are post-process effects, not HUD widgets. |
| Scoreboard sidebar | Right edge | Scoreboards | Remove | No scoreboard system planned. |
| Player list + ping (Tab) | Top centre | Multiplayer | Later | Only if multiplayer is planned. |
| Spectator picker, F3+F4 mode switcher | Bottom / centre | Spectator, debug | Remove | Debug-only; drop for now. |

### Behaviour worth keeping, whatever the look

These come from `HUD-RESEARCH.md`; section numbers are given for each.

- **One shared track.** XP, locator and mount jump share one track, chosen by a small state machine. The level number stays visible on its own. (§6)
- **Air.** Air follows the player's eyes, not their feet. It drains over 15 s, then drowning deals one heart per second. It refills in 3.75 s and stays visible until full. (§4)
- **Health.** Display health catches up after damage, which is why vanilla hearts blink. Low health (two hearts or less) adds a small jitter, timed by game ticks and not by frame rate. (§2)
- **Attack ready marker.** It shows only on a full charge with a valid target. It does not show whenever the cooldown ends. (§7)
- **Visibility by mode:**
  - Creative hides vitals and XP.
  - Spectator swaps the hotbar.
  - F1 hides the HUD but keeps captions and the sleep fade.
  - The HUD never takes input. (§10)
- **Architecture.** Gameplay owns the facts. The HUD reads an immutable snapshot and keeps only presentation history (blink and fade timers). Widgets never change health, food or items. (§11)

## Menu reference — `Reference/minecraft-menu` (summary)

- **Reference format.** A single vanilla-JS `index.html` with 939 assets. It has 36 routes, all title-menu or options screens, and no in-game screens.
- **How it runs today.** `Scripts/AdaptMenu.mjs` adapts it into `Content/Menu/Web`, which is hosted in a full-viewport `SWebBrowser` (`Source/Unnamed/MenuHost.cpp`).
- **Screens.** The Unreal build keeps 33 screens; Realms was removed. The full list is in `Docs/ScreenInventory.md`.
- **Controls.** 9 of 697 controls are real (`Docs/ControlRegistry.md`):
  - Quit Game, Fullscreen, Max Framerate, VSync, GUI Scale, Menu Blur (two controls);
  - Hard Reset and its confirm dialog.
- **Worth carrying into the redesign:**
  - the native settings logic in `Source/Unnamed/MenuVideoSettings.cpp`: borderless switching with rollback, FPS cap, VSync, ini persistence and validation;
  - the quit sequence in `Source/Unnamed/MenuApplication.cpp`.
- **Likely retired by a native, new-look UI:**
  - the browser adapter pipeline and runtime JS;
  - `Content/Menu/Web` (about 70 MB);
  - the HTML-specific capture suites.

First-pass screen calls (menu work comes after the HUD):

| Call | Screens |
|---|---|
| Keep | Title, Settings hub, Video, Audio, Controls + Key Binds, Accessibility, Worlds list + simplified Create World, Credits, Quit |
| Merge | Mouse into Controls; Font into Language/Accessibility; Chat settings into Accessibility |
| Relocate | Difficulty and in-world toggles into a new Pause menu |
| Remove | Telemetry, Friends/Online, Resource/Data Packs, Experiments, Superflat presets/biomes, Skin Customization, Link Confirm, Edit-World folder/backup buttons; Multiplayer screens unless multiplayer is planned |
| Add | Pause menu, Death screen (the bridge between menu and HUD) |

## Next

1. **Push the local Foundation work** (`Source/Unnamed/Foundation`, `Source/Unnamed/Menu`, `Docs/Foundation`) so the HUD can be planned against the real project.
2. **Choose a direction on the canvas** (A, B, C, or a mix) and confirm or change the calls above.
3. **Plan the native HUD.** Recommended approach:
   - native Slate/UMG, not the browser;
   - a gameplay → HUD snapshot adapter, with labelled preview data for the systems that don't exist yet;
   - settings and quit services moved into a `UGameInstanceSubsystem`, so they survive the change from menu to gameplay.
4. **Then redesign the menu** in the chosen look.
