# Quality of life: feel more like Minecraft (2026-10-11 to 12)

The lane `feature/voxel-qol` (worktree `Unnamed-qol`, from 37d8c71), beside the far LOD lane. Brief: `C:\dev\sandbox\scout-qol\build-prompt.md`; facts: scout V (`C:\dev\sandbox\scout-qol\facts.md`) and probe W (`C:\dev\sandbox\probe-feel\facts.md`). Every item keeps the look before it one switch away.

## Jesus's answers (2026-10-11, through Manager 8)

- Q1 placement preview: (a) gone, as vanilla: no box, no placed flash, no rejected red X; `foundation.Place.Preview 1` is the old drawing.
- Q2 outline: (a) vanilla's: black 40%, a true 3 px, the shape's outer edges only; the thin one one switch away. The 1440p vanilla measurement was skipped: the width is scaled from the 1080p number (below).
- Q3 UI size: (a) every screen follows Options > GUI Scale, Auto by default (6 at 2560 x 1440).
- Q4 FOV 110: (a) new profiles and Reset only, no migration. A saved profile keeps its fov (his stays 70 until he moves the slider; his saved GUI Scale is 2).
- Q5: G2 (engine warnings hidden in play) and G1 (place sound) in this build; G6 + G7 (view bobbing, hand sway) and G3 (Render Distance) go to the next chat.

## What changed (one commit each, in this order)

| Item | Switch (old look) | Default now |
|---|---|---|
| FOV 110 for new players: `patch.data` in `MenuWeb/Patches/Preferences.mjs`, regenerated schema and page; the pawn's fallback 110 | the FOV slider, or a saved profile | 110 ("Quake Pro") |
| No placement preview: `DrawSelection` stops after the outline; no placed or rejected flash (the interacted flash stays); the preview's data is kept | `foundation.Place.Preview 1` | 0 |
| One GUI scale: the creative inventory, its tooltips and the loading screen resolve through `HudModel::GetScale(framebuffer, setting)`, as the HUD does | Options > GUI Scale Auto gives the old frames | the setting (Auto) |
| Vanilla outline: `foundation.Break.Outline 1` draws the merged shape's outer edges (`FoundationBreak::MergeOutlineEdges`) as square strips sized so their silhouette is the asked width (`OutlineStrip`), 3 px x max(1, width / 1920), black 40% | `foundation.Break.Outline 2` (thin, 1 px, 30%) | 1 |
| Engine text: Unreal's on-screen messages hidden in normal play, kept in probe and capture runs (`FoundationScreenMessages`) | `foundation.ScreenMessages 1` | -1 (0 always hides) |
| Place sound: a Placed event plays the block's place event at its break's volume and pitch (minecraft.wiki's Sounds tables, e.g. Stone 1.0 / 0.8 for both); `DA_BreakAssets` re-imported through `ImportBreakAssets.py` | `foundation.Place.Sound 0` | 1 |
| Capture tool: `Scripts\TestFoundationRuntime.ps1 -Editor -Suite views` (`FoundationViewsProbe.cpp`, poses in `Scripts/Views/DevSeed.txt`): player-view shots with the HUD, outline and held item in the dev seed's world (day, forest, water, cave, a torch-lit cave, the preview pair, outline variants by day and in a cave) | | |

The re-import's before/after dump: the 20 crack layer hashes identical; the first 176 events identical and 65 `*.place` events added, using only waves already imported; every block field but `PlaceEvent` identical; place events: 550 blocks their own, 66 their break event (files not in the block reference), water and lava silent. The source digest moved from 08400473... to c0dea972... as its script changed.

## Measured (2560 x 1440)

- GUI Scale 3: the creative panel at scale 3 from x 987 (585 px; before: scale 6 from x 690, 1170 px), the loading bar from x 978 (before: x 678, scale 6), the hotbar 546 px from x 1008. At Auto every frame is the old one (creative pixel-identical).
- FOV 70 to 110: the same block at the same distance draws 0.50 x as wide (385 to 191 px; 0.49 expected).
- Outline, by day (`C:\dev\sandbox\qol-lane\shots\analysis\`): mode 1 is 4 px (median 3.8 to 4.2) and darkens the scene behind it to about 0.86 x (0.84 to 0.89 on block sides); mode 2 is 1 px; High Contrast a 5 px cyan core in a 11 to 13 px halo. Vanilla (VP1, 1080p) is 3 px at exactly 0.60 x. In an unlit cave modes 1 and 2 can't be seen; High Contrast can.
- Engine text: 2290 yellow pixels in a probe run's loading shot, 0 with the text hidden; the log keeps the `[VSM]` line either way.
- Place sound: the interaction probe's stone place logs `block.stone.place`, volume 1.000, pitch 0.800.

## Open items

- **The outline is fainter than vanilla's** (0.86 x against 0.60 x): the 40% black blends in the scene before tone mapping, vanilla's on the final image. Decided (Manager 8, 2026-10-12): a required item of the brightness pass, which owns tone mapping and `M_BreakOutline`'s eye-adaptation inverse: mode 1 reaches vanilla's about 0.60 x in daylight, measured as here (`C:\dev\sandbox\qol-lane\shots\analysis\`).
- **Vanilla's outline width at 1440p is unmeasured:** the width rule px = 3 x max(1, width / 1920) is an assumption (4 px at 2560). A VP1 run at 2560 x 1440 (`run_probe.py --step outline`, guiScale 0) settles it on Jesus's go.
- **The menu's Auto keeps its 416 x 240 floor** (`MenuWeb/Patches/Video.mjs`), against the native screens' 320 x 240: below 16:9 the menu can be a step smaller (1280 x 960: menu 3, HUD 4). Recorded in `Docs/VideoSettings.md`.
- **66 blocks place with their break sound** (28 place events: deepslate and its family, amethyst, calcite, sculk, the copper bulbs and others): the block reference has only the break and hit events' files. Extending it is Jesus's call (`Docs/KnownDifferences.md`).
- **The held item shrinks with a wider FOV:** every probe and new player now runs at FOV 110. Whether vanilla's hand keeps its size at 110 is unchecked (a black-box vanilla shot settles it).
- **`Tests/registry.mjs` needs the packaged capture `review-preferences-v1-*`**, which this worktree lacks; `Docs/ControlRegistry.md` and `.json` got a hand edit of the fov default only, and the json's `htmlSHA256` stays the capture's.
- `Tests/video-persistence.mjs:59` (not in `npm test`) predates the native preferences; it now expects 110 and no longer separates session-only from saved.
- `BreakImportData.py`'s summary line would over-count a block's own place event if a non-fluid block were silent on place (none is); fixing it changes the import digest, so it waits for the next re-import.
- The views suite: no stairs yet (no stair block), the sky ignores the clock (no sunset shot), and the caves within 64 blocks of spawn are unlit.
- The interaction suite's timing checks (held repeat cadence, the wall torch's pop, an attack while rising) fail now and then when another editor runs beside it, as probe W saw.
- Gaps still open: G3 Render Distance and G6 + G7 (next chat, below); G4 the crosshair doesn't invert; G5 the survival tab's player model; G8 screenshot naming; G9 the other saved options with no effect.

## Questions (no CLAUDE.md edits made)

- Add the views suite (`& Scripts\TestFoundationRuntime.ps1 -Editor -Suite views -Width 2560 -Height 1440`) and `-ExecCmds` to CLAUDE.md's commands?

## Next chat (short prompt)

> Unnamed, a small feel build after the QoL lane (record `Docs/Records/2026-10-11-QoL-Minecraft-Feel.md`; scout V's gap notes `C:\dev\sandbox\scout-qol\notes\gaps.md` items 1, 4, 5). Jesus picked: G6 + G7, view bobbing (the saved `bobView` option, on by default as vanilla) and hand sway (an equip dip on item change, a look lag), both on the camera and hand pivot (`FoundationPawn.cpp` near the camera set, `FoundationInteractionView.cpp`'s hand pivot), each with a switch keeping today's still camera; and G3, Options > Render Distance driving `voxel.ViewRadius` and the far ring (cap it where the ray tracing record says it is proven). Numbers from the wiki or black-box vanilla runs only. Captures with `Scripts\TestFoundationRuntime.ps1 -Editor -Suite views` (player view in the generated world). Plan first, one commit per item.
