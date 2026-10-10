# Items handoff: into S5 (mining by tool and tier), with probe P1 pending

Read this first if you are picking up the items phase in a new chat. It is also the record of items S4 (held-item rendering), a parallel lane next to the voxel chain (`feature/items-s4`, worktree `C:\dev\projects\Unnamed-items`). **S5 is next:** mining speed and drops by tool and tier against the black-box truth table. **Probe P1** (vanilla's hand pose) still waits for a screen hold; until it runs, the held view's pose is provisional. This was true at the S4 commit; check `git status`, `git log -5` and newer records before trusting it.

## 1. Read these, and only these

Budget: about 80k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md).
2. **This handoff.**
3. **[The items phase plan](2026-10-09-Items-Phase-Plan.md):** sections 1 (decisions), 3 (sessions, order and lane rules) and 4 ("S5: mining by tool and tier").
4. **[Docs/Items.md](../Items.md):** "Data flow", "Presentation" (the held view and its known issues) and "Tests".
5. **[Scout F's research](2026-10-09-Items-Scout-Research.md):** appendix B (break times computed from the data) and appendix C, probe P5 (the delay between breaks, hoe and sword wear).
6. **[The item reference's README](../../Reference/minecraft-items-26.3/README.md):** the `facts/break_times.json` row and the paragraph on exact arithmetic (whole-number inverses, the deepslate example).
7. **Code:**
   - `Source/Unnamed/Foundation/FoundationItems.h/.cpp`: `FFoundationItemDefinition` (`MiningRules`, `DefaultMiningSpeed`, `GetMiningSpeed`, `IsCorrectForDrops`);
   - `Source/Unnamed/Foundation/FoundationAuthority.cpp`: where mining asks those (search `GetMiningSpeed`);
   - `Source/Unnamed/Foundation/FoundationItemAssets.cpp`: `MakeDefinition` (the rules from `DA_Items`);
   - `Source/Unnamed/Tests/FoundationItemTests.cpp` (`.Rules`) and `FoundationInteractionTests.cpp` (the mining tests).

## 2. Working agreement

- **The manager:** the "Shift manager handover" chat (`session_01PuWLpgQipHqcox6dnoviYH` at the S4 commit, after a handover from `session_01Rm6t4pNqopFBUje9vnNw8Q`; a fresh manager sends its own id). Report there with `send_message` instead of waiting for Jesus. Send a short plan first and report before (report A) and after (report B) committing. The manager confirms plans within Jesus's decisions and approves the local commit after a clean Sonnet 5.5 pre-commit review (diff only) and passing suites; it pushes and merges. Never push.
- **The lane:** the worktree and branch the manager names (S4: `C:\dev\projects\Unnamed-items`, `feature/items-s4`). Run git only there. Never use bare `git stash` (the stack is shared by every worktree). No `CLAUDE.md` edits; questions go in the handoff. Never re-record goldens or world hashes; stop and report if your work would.
- **Machine turns:** builds, imports, native suites and captures do not wait for an idle machine. Only timed gates need a quiet window (ask the manager). On "QUIET WINDOW HOLD", finish the step you are on, reply "held", and start nothing heavy until "WINDOW OVER". Start each editor step as its own command.
- **Windowed runs and the CEF lock:** before a windowed `-game` run, wait until `C:\dev\sandbox\locks\cef-e2e.lock` is absent and no other `-game` UnrealEditor runs. Before `create-world.mjs` or `foundation-menu.mjs` (they attach to CEF on 127.0.0.1:18946), write the lock yourself (lane and time) and delete it after, also on failure. `C:\dev\sandbox\items-s4\gamerun.ps1` waits for both, logs the machine state, and stops on a `CANCEL` file.
- **Context:** measure with `Scripts/MeasureAgentUsage.mjs` after every build, test run or subagent result (`C:\dev\sandbox\items-s4\measure.sh`; change its `SID`). At about 300k and every 200k after, start the wrap-up or send the manager a case.
- **Subagents:** 5.5 models, exact paths and line ranges, one deliverable each, a stop near 150k.
- **Game code:** data, published docs and black-box runs of the public API only. `javap -public` for signatures is fine. No decompiled method bodies, no constants read out of bytecode, no reflection into private internals. S3's `Reference/minecraft-items-26.3/tools/ItemFacts.java` is the pattern.
- **Line endings:** the repository stores LF (`* -text` in `.gitattributes`). Python's `write_text` on Windows writes CRLF; patch with `read_bytes` and `write_bytes`.

## 3. Where things stand

| Session | Area | Status |
|---|---|---|
| S1 | Item reference, phase plan, `CLAUDE.md` id rule | **Done** (`318c48f`) |
| S2 | Item registry and rules | **Done** (`180f620`) |
| S3 | Icons and tooltips | **Done** (`d7fcf03`) |
| S4a | Combat probe flake (test only) | **Done** (`4b3e358`) |
| S4 | Held-item rendering | **Done** (`e724bf4`); the pose is provisional until P1 |
| S4 fix | The creative screen's textures kept alive through garbage collection | **Done** (`d985dae`), its own commit |
| P1 | Vanilla hand-pose shots | **Waiting** for a screen hold (section 7) |
| S5 | Mining by tool and tier | **Next** |
| S5b to S9+ | Ore loot, durability, combat, item entities, behaviours | Planned ([plan](2026-10-09-Items-Phase-Plan.md), section 3) |

## 4. What S4 built (the record)

- **The mesh builder (Q7, approach C):** `Foundation/FoundationItemMesh.h/.cpp`, pure functions plus one mesh factory.
  - `ReadMask` reads `FFoundationItemTextureAsset::Mask` (one hex string per row, top first, the leftmost pixel in the highest bit) and refuses rows of the wrong length or not hex.
  - `BuildQuads` is 26.3's `builtin/generated`: a front and a back quad of the whole sprite (Z 8.5 and 7.5 in model pixels, one model pixel deep whatever the sprite's size), plus one side per opaque pixel edge that borders a transparent pixel or the sprite's border. A side's four UVs are its pixel's texel centre, so nearest filtering never bleeds a neighbour into it. Quads wind counter-clockwise seen from outside in Mojang's right-handed space (`AddQuad` swaps corners 1 and 3 otherwise).
  - `ToUnreal` maps model pixels to centimetres about the model centre: Mojang (x, y, z) is Unreal (-z, x, y), a mirror, which turns the winding into the clockwise one Unreal draws.
  - `MakeDisplayTransform` takes a display context verbatim: scale (s_z, s_x, s_y), rotation `FQuat(Y, -a) * FQuat(Z, -b) * FQuat(X, c)` (Mojang's Rx · Ry · Rz conjugated by the mirror), translation P(t / 16) in model blocks.
  - `BuildMesh` makes a transient `UStaticMesh` (one slot "Item", full-precision UVs, UV 1's X holding the array layer), like `FoundationBlockMeshes`.
- **The model lookup:** `UFoundationItemAssets::FindModelView(Id, Context, View)` returns the model case naming the context, else the select's fallback, then the model's first layer and that context's transform. Spears hold `item/<mat>_spear_in_hand` (32 x 32) in first person and show `item/<mat>_spear` in the GUI.
- **The material:** `M_HeldItem` (`Scripts/Editor/CreateItemMaterials.py`), masked at 0.5, matte like the blocks. A custom node samples the `Sprites` parameter (`T_ItemTextures` by default) at the layer in UV 1. It compiles with `-AllowCommandletRendering` (182 pixel-shader instructions).
- **The held view:** `FoundationInteractionView`.
  - The cube pickaxe and sword are gone. One `HeldItem` component under `HandPivot` (first-person primitive, no shadow, no collision) gets the item's mesh, material and display transform when the held item changes (`SetHeldItem`).
  - Meshes are cached per sprite (`ItemMeshes`). Two dynamic instances of `M_HeldItem`, one per sprite array, are made the first time an item is held, which is also when the soft-referenced arrays load (`GetItemMaterial`).
  - Held blocks are unchanged. Probes read `GetHeldSprite` and `GetHeldItem`.
- **The glow (the manager's look call B):** lit by the scene alone, the display rotation turns a sprite's face away from the Playground's sun and the tour's items read at 0.26 to 0.38 of their sprites' luma. `M_HeldItem` has a `Glow` scalar (emissive = sprite colour x Glow), set from `foundation.Held.Glow` (default 16,000 nits; the view pushes it to both instances when it changes). A sweep (0, 1,000, 3,000, 10,000, 30,000) put ratio 1 at 12,400 to 18,600 per item under the 75,000 lux sun at EV100 14.5; at 16,000 the four A/B items read 0.99 to 1.12. `foundation.Held.Glow 0` is the scene-lit look (option A). The tour shoots four items again at 0 (`19i-held-*-glow0`).
- **The creative screen's textures (its own commit):** Slate brushes do not keep their textures alive, so a garbage collection while the creative screen was open freed them and the paint asserted (`Index >= 0`). It surfaced once the held shots made the tour about 15 s longer, long enough to reach the periodic collection. `SHudCreativeScreen` now keeps a `TStrongObjectPtr` per brush texture, and the tour forces a full collection in every creative case. Mutation: with the references removed, the forced collection crashes the tour at once (`Runtime-tour-mut-gc-170844`).
- **The `Icon` key** now only says `block`. `MaterialTint` and the tools' `Icon` cases are deleted. `IconColor` stays white for `DA_Items` items; the HUD uses it only for a block icon's fallback cube.
- **Provisional:** the hand space is scaled to `UFoundationInteractionView::HeldModelBlockCm` = 50 cm per model block (at 100 cm a 0.68-scale pickaxe would fill most of the screen at the pivot). The pivot's rest stays (42, 24, -24) cm, and the rotation order is unconfirmed. P1 fits all three (section 7).

## 5. S5: what to do

From the plan (section 4, "S5"):

1. **Mining speed and drops by tool and tier**, through the authority's rules: the first matching rule's speed, then `correct_for_drops`, against `facts/break_times.json` (the hand and the 35 tools on every imported block: ticks and drops).
2. **Tests:** `Unnamed.Foundation.Mining.Tiers` against the whole table for imported blocks, not a sample (S3 learned that a whole black-box table catches what spot values miss). An interaction scenario: iron ore with a wooden, then a stone pickaxe.
3. **Gate:** every native suite, and one `CaptureVoxel.ps1 -Edits` run (it takes a quiet turn on the main lane's capture tools).
4. **Break-time arithmetic:** the table is exact. Where 1 / d is a whole number (35,393 of 46,296 entries), the game's single-precision steps can land one tick either side. Probe P5 settles the in-game count; until then, test against the exact table and flag the whole-inverse cases.
5. **Wear:** S6 (durability) may fold into S5 if small (plan, section 3). Creative clicks drop damage (section 8, item S3-b); S6 is the natural place to fix it.

## 6. Settled decisions

- **Jesus's nine answers** (plan, section 1): ids are the 26.3 names; `DA_Items` holds non-block items; approach C for held meshes; copper in from the start; vanilla GUI icons.
- **S4 (the manager's calls under Jesus's away rule):**
  - **P1 is deferred** to a screen hold batched with the sky shots (SP1). S4 was built with a provisional hand anchor and look call.
  - **Held blocks keep their current pose.** Vanilla's `block/block` first-person pose (rotation [0, 45, 0], scale 0.4) is only in `Reference/minecraft-blocks-26.3/assets/minecraft/models/block/block.json`, not in `DA_Blocks`; vanilla draws flowers, torches and saplings in hand as flat generated sprites.
  - **Swing and equip curves stay as they are**, and the break lane's `foundation.Break.Swing` hook stays.
  - **No mips for the hand**; the arrays stay mip 0. Dropped items (S8) may need mips.
  - **The hand-space scale** (50 cm per model block) is provisional, flagged with the anchor.
- **The look call** (the manager's, under the away rule): B, the glow, accepted on the A/B sheet ("B reads as vanilla's sprite colours"); 16,000 stays the default and A is one switch away. The size and the orientation wait for P1, flagged as provisional.

## 7. Probe P1 (vanilla's hand pose), ready for a screen hold

Run it when Jesus is home with the monitor on and hands off, batched with the sky shots (SP1).

- **Harness:** copy `C:\dev\sandbox\break-vp1` to `C:\dev\sandbox\items-p1` and add a `held` step to `scripts/run_probe.py` (`STEPS`).
  - **Options and window:** `options.txt` with `fov:0.0` (70), `bobView:false`, `guiScale:2`, `hudHidden:false`, and F1 before each shot. Launch the client at 1920 x 1080 (`--width 1920 --height 1080`) and check the client area; it may need fullscreen if the work area is smaller.
  - **Scene:** the flat world, the player standing still facing +z at pitch 0, sky and ground behind.
- **For each item:**
  1. `/item replace entity VP1 weapon.mainhand with minecraft:<id>`.
  2. Wait at least 1 s for the equip dip to end.
  3. F1, then an F2 shot (the framebuffer; it works with the display off).
  4. Repeat in `weapon.offhand` with the main hand empty.
- **Items:** `iron_pickaxe`, `iron_sword`, `stick`, `apple`, `stone`, `iron_spear`, plus `mace` and `fishing_rod` for later, and one empty-hand shot. Optional: the same in third person (F5, back view).
- **Command,** from `C:\dev\sandbox\items-p1\scripts`, after the dry run checks out: `uv run --no-project --python 3.13 --with mss --with numpy --with pillow python -I run_probe.py --step held --no-dry-run --accept-eula`. The real run refuses to start while a Minecraft java process runs. Check `Get-Process java, javaw, UnrealEditor*` and the CEF lock first.
- **Use:**
  - Project the extruded mesh (`FoundationItemMesh`, the same quads) at FOV 70 vertical and 1920 x 1080 onto the shots' silhouettes. Fit the camera-space hand anchor and the hand scale, and confirm the rotation order. A Python fit over the quads is enough; the mesh is pure data.
  - Then set `HandPivot`'s rest, `HeldModelBlockCm` and the first-person FOV (`SetEnableFirstPersonFieldOfView`, `SetFirstPersonFieldOfView`) to match. The Playground camera uses Unreal's default 90 degrees horizontal; vanilla's 70 vertical is 102.45 horizontal at 16:9.
  - Compare the held items' brightness with vanilla's in daylight and retune `foundation.Held.Glow` if needed (the A/B numbers are in section 4).
  - Re-shoot the tour's `19i-held-*` and send the manager a side-by-side.

## 8. Known issues, and when they are due

| # | Issue | When |
|---|---|---|
| S4-a | The hand's anchor, the hand scale (50 cm per model block) and the rotation order are provisional; the look call too. | P1 (section 7) |
| S4-b | Held blocks keep the old pose (rotation (10, 35, 0), scale 0.11), not `block/block`'s first-person transform. | With P1, when blocks get a display transform (needs `DA_Blocks` or the block reference to carry it) |
| S4-c | The offhand is not drawn; the left-hand contexts (mirrored Y and Z rotations) are unused. | When the offhand gets a use (shield, Tier C) or P1's offhand shots |
| S4-d | No equip dip, and the swing is the project's own curve (or the break lane's). `SwapAnimationScale` and `bHandAnimationOnSwap` are imported but unused. | P1's animation frames |
| S4-e | A missing `M_HeldItem` or sprite array is logged once and never retried; the items then draw nothing. | Only if assets go missing |
| S4-f | When the pawn changes, the old pivot and its components stay registered and hidden (the old behaviour, now with a held mesh). | If pawns get swapped in play (respawn as a new pawn) |
| S4-g | The arrays have mip 0 only: fine in hand, likely shimmer for small dropped items. | S8 (item entities) |
| S4-h | The glow ignores the light level: held items read daylight-bright in caves and at night (vanilla lights them by the light at the player). Scale `Glow` by the light at the eye; no such query exists in the Playground's world. | When a light query exists (the voxel lights, or the day and night lane), with P1 |
| S4-i | Orientation: the blades point up-right (tip near and to the right), which the data gives under the assumed Rx · Ry · Rz order; my memory of vanilla is up-left, not trusted. | P1 |
| S4-j | Size: at 50 cm per model block the pickaxe spans about x 0.71-1.0 and y 0.4-1.0 of the screen and runs off the right and bottom edges. | P1 |
| S3-a | A multi-line tooltip's 10 px pitch and 2 px gap are not confirmed by a vanilla capture; the tooltip is not moved back on screen at the right or bottom edge. | A later HUD probe |
| S3-b | Creative clicks drop damage (`SetCreativeSlot` carries only the item and count). | S6 (durability) |
| S3-c | The in-game base-value tooltip form is unprobed (`Player` needs a live level). | If a live-level black-box run becomes possible |
| S3-d | The search tab does not add each item's tab names to its tooltip. | A later HUD session |
| S3-e | Heap corruption in the voxel worker threads: `EXCEPTION_ACCESS_VIOLATION` in `VoxelWorldgenJson::ParseWorldgenJson` under `VoxelWorldgen::ReadFeatures`' `ParallelFor` (`VoxelFeatureReader.cpp:1122`). S3 saw it in 2 of 4 full runs with hard references from `DA_Items` to the arrays. S4 saw it once in 5 full runs (`C:\dev\sandbox\items-s4\logs\native-full-3\run.log`, during `Unnamed.Voxel.Mesher.Digest`; dump `C:\dev\projects\Unnamed-items\Saved\Crashes\UECC-Windows-4449015D46B5D6535B8E899791007DD6_0000`), with no array or material loaded by any test; the other four passed 191/191. The manager confirmed it is the known intermittent `ReadFeatures` crash (voxel S10 hit it once at its merge), on the small-fixes list. It looks like a race in the main lane's parallel JSON reading rather than items code, but nothing pinned it down. Never load the arrays or `M_HeldItem` in `-NullRHI` tests (S4's mesh test checks the material's package exists without loading it), and run the full suite twice. | Main lane (report it) |
| — | `DA_Items` is loaded once per process; a reimport needs an editor restart. Hard-coded counts (63 items, 63 sprites, 70 textures, 6 lang keys, 4,261 bar values) are tripwires for a reference rebuild. Slots hold item indices for the session only; save ids when inventories are saved (voxel phase 10). | Standing |

## 9. Code map: S4

| What | Where |
|---|---|
| Mesh builder | `Source/Unnamed/Foundation/FoundationItemMesh.h/.cpp` (`ReadMask`, `BuildQuads`, `ToUnreal`, `MakeDisplayTransform`, `BuildMesh`) |
| Model lookup | `FoundationItemAssets.h` (`FFoundationItemModelView`, `FindModelView`) |
| Held view | `FoundationInteractionView.h/.cpp` (`UpdateHeldItem`, `SetHeldItem`, `GetItemMaterial`, `HeldModelBlockCm`, `GetHeldSprite`, `GetHeldItem`, `foundation.Held.Glow`, `AppliedGlow`) |
| Creative screen fix | `Hud/HudCreativeScreen.h/.cpp` (`BrushTextures`); the tour's `Creative` cases (`CollectGarbage`) |
| Material | `Scripts/Editor/CreateItemMaterials.py` → `Content/Items/M_HeldItem` |
| Tests | `Source/Unnamed/Tests/FoundationItemMeshTests.cpp` (`Unnamed.Foundation.Items.Mesh`); `FoundationItemTests.cpp` (no tool `Icon`) |
| Probes | `FoundationInteractionProbe.cpp` (`RecordHeld` in "select pickaxe", "select sword", "wheel"); `FoundationTourProbe.cpp` (i3: `19i-held-*` with a `Flush` after each, `*-glow0`, slot 4, the selection and the glow restored) |
| S4 scratch | `C:\dev\sandbox\items-s4\`: `editor.ps1` (one editor Python run with `-abslog`), `runtests.ps1` (native tests to `logs\<name>`), `gamerun.ps1`, `mut\mutate_mesh.py` (the three mutations), `look.py` (look-call numbers and sheets), `bright.py` and `glowfit.py` (rendered against sprite luma), `absheet.py` (the glow A/B sheet), `review\s4-r1.diff` and `s4-r2-delta.diff` (the reviewed diffs), `measure.sh` |

## 10. What worked and what failed in S4

- **Worked:**
  - Designing the axis map and the rotation conjugation on paper first (the S4a addendum), then pinning them with a test against the written-out matrices. The three mutations (reversed order, side UVs at the texel corner, no border sides) all failed the mesh test.
  - Checking closure with directed edges: every edge matched by its reverse. Counting undirected edges "exactly twice" would wrongly fail two pixels touching at a corner (four faces share that depth edge); none of the 70 sprites has one, but the synthetic case keeps the path tested.
  - Keeping the arrays and the material out of `-NullRHI` tests (`FPackageName::DoesPackageExist` instead of a load).
  - Measuring the look instead of eyeballing it: rendered against sprite luma on the item's pixels (the difference from the empty-hand control) turned "dark" into 0.26 to 0.43, and a five-value sweep found the glow in one run.
  - A forced garbage collection as the regression guard for the creative screen: it turned a timing-dependent crash into an immediate one, so the mutation proves the fix.
- **Failed or changed course:**
  - A Python patch wrote CRLF into an LF file (the whole file showed as changed); restored with `sed -i 's/\r$//'`. Use `read_bytes` and `write_bytes`.
  - The first build failed on C4456: a local named `Held` shadowed the held-block loop's `Held` (renamed `InHand`).
  - `clang-format` on a `/tmp` path from Git Bash reads nothing (it is a Windows program); use a temporary file inside the worktree.
  - `UnrealEditor-Cmd` with `*>` redirection wrote an empty log and exited 1; run editor Python through `editor.ps1` (`Start-Process` with `-abslog`).
  - The first held shots were each one item late: a screenshot is taken on a later frame, after the next case's setup had already swapped the item (and the swap motion-blurred the spear and the bucket). Every shot needs a `Flush` case after it, as the tour's other shots have.
  - A glow near 1 did nothing: the Playground is exposed at EV100 14.5 for a 75,000 lux sun, so emissive needs thousands of nits (the chunk materials use 8,000).
  - The longer tour crashed in the creative screen (see section 4), and one full native run hit S3-e.
  - Shell heredocs with mixed quotes broke; write patch scripts to a file and run them.

## 11. Commands

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' UnnamedEditor Win64 Development -Project="$PWD\Unnamed.uproject" -WaitMutex
& C:\dev\sandbox\items-s4\editor.ps1 -Script C:\dev\projects\Unnamed-items\Scripts\Editor\CreateItemMaterials.py -Name materials -Extra '-AllowCommandletRendering'
& C:\dev\sandbox\items-s4\runtests.ps1 -Tests 'Unnamed.Foundation.Items' -Name items      # or -Tests 'Unnamed' for every native suite
& C:\dev\sandbox\items-s4\gamerun.ps1 -OutputDirectory <fresh dir> -Suite interaction      # waits for the CEF lock; also -Suite tour
uv run --no-project --python 3.13 --with pillow --with numpy python -I C:\dev\sandbox\items-s4\look.py <new tour dir> <old tour dir> <out dir>
Set-Location Tests; npm test; node source-check.mjs
```

## 12. Evidence

`Tests/Evidence/` is git-ignored; the runs stay in the worktree. Best first:

- **The glow's A/B sheet:** `C:\dev\sandbox\items-s4\look-3\sheet-glow-ab.png` (B at 16,000 against A at 0, four items, each tile with its luma numbers); all held shots at 1:1: `look-3\sheet-closeups.png`; `look-3\look.json`.
- **The final tour:** `Tests/Evidence/2026-10-10/Items-S4/Runtime-tour-final-172035` (27 checks, 0 failures, 0 "pending kill"; `runtime-60-tour-19i-held-*.png`, `*-glow0`).
- **The final runtime probe:** `Items-S4/Runtime-all-final-171842` (119 of 119 at 60 fps; the held checks: the iron pickaxe 148 of 148 triangles, the iron sword 164 of 164, both on `T_ItemTextures`, none for the empty hand).
- **Native suites:** `C:\dev\sandbox\items-s4\logs\native-full-5` (191 of 191 on the final code); `native-full-3` (the S3-e crash; dump in the worktree's `Saved/Crashes/UECC-Windows-4449015D46B5D6535B8E899791007DD6_0000`); `mut-order`, `mut-uv`, `mut-border` (the mesh mutations).
- **The creative screen:** `Items-S4/Runtime-tour-final-170515` (the crash), `Items-S4/Runtime-tour-mut-gc-170844` (the mutation).
- **The glow sweep:** `Items-S4/Runtime-tour-glowsweep-170221` (0, 1,000, 10,000, 30,000 and the 3,000 default).
- **End to end:** `Tests/Evidence/2026-10-10/Worlds/Create-1791667709842` (create-world, no failure); `Tests/Evidence/2026-10-10/VoxelFlight-20261010-173708` (the flight: holes in 93 of 9,411 frames, at most 13 columns, all in the circle laps, the voxel lanes' known trees-on holes; not quiet).
- **Material and builds:** `C:\dev\sandbox\items-s4\logs\materials-3` (182 then 186 pixel-shader instructions with `-AllowCommandletRendering`), `build-*.log`.

## 13. Token use in S4

| Agent | Peak context | Notes |
|---|---|---|
| Lead (Opus 5.5) | about 460k at the handoff (92k at the plan confirmation, 254k after the mesh mutations, 345k at the look call, 435k at report A) | The code took about 160k. The look round (off-by-one shots, the dark faces, the glow sweep) took about 90k, and the creative-screen crash and the wrap-up runs most of the rest. |
| Pre-commit reviewer r1 (Sonnet 5.5) | 76.4k | Diff only (1,209 lines); no serious bugs. Fixed: the selection restore, the mesh set before the material, a warning for a missing array, comments. |
| Pre-commit reviewer r2 (Sonnet 5.5) | 64.2k | The delta (527 lines); no serious bugs. Fixed: slot 4's contents restored, the glow's restore value read at run time. |

## 14. Open questions

1. **The next items session: settled.** The manager chose S5 (mining by tool and tier, the plan's order: S5 → S6 → S7 → S5b → S8, with S6 folded in if small). P1 goes with Jesus's SP1 screen hold.
2. **The merge of main:** at the S4 commit the lane was holding for the manager's "merge now" (after the voxel S10 fast-forward lands): merge `feature/voxel-world` once, rebuild with touched sources or `-DisableAdaptiveUnity`, run every suite once (native full, runtime all, tour, npm, source-check, `TestVoxel.ps1` at main's count, radius-8 and Digest equal to main's values) and create-world under the CEF lock, then one combined report. Check `git log` for whether that merge happened before starting S5.
3. **The P1 fit's scope.** (a) Anchor, hand scale and rotation order only. **Recommended:** it is small and the mesh is pure data. (b) Also the equip and swing curves from P1's animation frames. That is larger; it is better as its own session after S5.

## 15. Short prompt for S5

> Items phase, session 5: mining speed and drops by tool and tier against the black-box truth table (`facts/break_times.json`). Read `Docs/Records/2026-10-10-Items-Handoff-S5.md` first and follow it; it holds the reading list, the working agreement, the S4 record (held items, provisional pose until probe P1), the known issues with when they are due, and the commands. Work only in the folder and branch the manager names; builds and native suites take turns with the main lane, and windowed runs wait for the CEF lock. Send the manager a short plan before building.
