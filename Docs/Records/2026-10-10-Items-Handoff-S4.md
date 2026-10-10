# Items handoff: into S4 (held-item rendering)

Read this first if you are picking up the items phase in a new chat. It is also the record of items S3 (icons and tooltips), a parallel lane next to the voxel chain (`feature/items-s3`, worktree `C:\dev\projects\Unnamed-items`). **S4 is next:** the extruded held-item mesh from each sprite's opacity mask, its material and the display transforms. This was true at the S3 commit; check `git status`, `git log -5` and newer records before trusting it.

## 1. Read these, and only these

Budget: about 80k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md).
2. **This handoff.**
3. **[The items phase plan](2026-10-09-Items-Phase-Plan.md):** sections 1 (decisions, especially Q7: approach C), 3 (sessions and lane rules) and 4 ("S4").
4. **[Docs/Items.md](../Items.md):** data flow, "Presentation" (what S3 drew, and the known issues).
5. **[Scout F's research](2026-10-09-Items-Scout-Research.md):** section 3 ("Models and rendering": `builtin/generated` as observed, the display transform semantics, approach C), and appendix C, probe P1 (the hand pose).
6. **Code, headers first:**
   - `Source/Unnamed/Foundation/FoundationItemAssets.h/.cpp`: `DA_Items` with the models' display transforms, the sprites' masks and array layers, the soft `ItemTextures` and `ItemTextures32`, and the temporary `MaterialTint`;
   - `Source/Unnamed/Foundation/FoundationInteractionView.cpp` around line 742: the held view that still draws the cube pickaxe and sword from the `Icon` key;
   - `Scripts/Editor/ImportItems.py`: how the arrays are built (`UFoundationBlockImport::BuildTextureArray`, mip 0 only).

## 2. Working agreement

- **The manager:** Hemera by day and Nyx at night. Send a short plan first and report before and after committing. The manager confirms plans within Jesus's decisions and approves the end-of-session local commit after a clean Sonnet 5.5 pre-commit review (diff only) and passing checks. Never push; the manager merges.
- **The lane:** S4 runs where the manager says. Never touch another lane's folder. `FoundationInteractionView` belonged to the voxel S7 lane on the night of October 10. Ask the manager who holds it before editing.
- **Machine turns:** builds, imports and native suites do not wait for an idle machine; with three lanes running it rarely is. Only timed gates need a quiet window. On "QUIET WINDOW HOLD", finish the step you are on and start nothing heavy until "window over". Start each editor step as its own command; never queue a chain that a hold could catch.
- **Context:** checkpoints at about 300k and every 200k after, measured with `Scripts/MeasureAgentUsage.mjs` (S3's helper: `C:\dev\sandbox\items-s3\measure.sh`; change its session id).
- **Subagents:** give each exact paths and a stop near 150k.
- **Game code:** data, published docs and black-box runs of its public API only. `javap -public` for signatures is fine. No decompiled method bodies, no constants read out of bytecode, no reflection into private internals. S3's black-box tool, `Reference/minecraft-items-26.3/tools/ItemFacts.java`, is the pattern.

## 3. Where things stand

| Session | Area | Status |
|---|---|---|
| S1 | Item reference, phase plan, `CLAUDE.md` id rule | **Done** (`318c48f`) |
| S2 | Item registry and rules | **Done** (`180f620`) |
| S3 | Icons and tooltips | **Done** (`d7fcf03`) |
| S4 | Held-item rendering | **Next** |
| S5 to S9+ | Mining by tier, loot, durability, combat, item entities, behaviours | Planned ([plan](2026-10-09-Items-Phase-Plan.md), section 3) |

**S3 built:**

- **Black-box facts:** `tools/ItemFacts.java` runs the 26.3 server jar's own `ItemStack` through its public API only, after loading the vanilla pack as a dedicated server does.
  - `facts/durability_bar.json`: `isBarVisible`, `getBarWidth` and `getBarColor` at every damage value (4,261) of the 7 max damage values.
  - `facts/tooltips.json`: every item's `getTooltipLines` with no player and the creative flag.
  - Two facts the run showed: the tooltip puts a blank line before "When in Main Hand:", and a zero amount shows no line.
- **The item reference, rebuilt through its tools:** the accepted folder was moved to `C:\dev\sandbox\items-s3\accepted-0273eb0`.
  - New: `lang.json` (6 keys from `en_us.json`: `item.modifiers.mainhand`, two attribute names, and the `attribute.modifier` equals, plus and take forms) and the two facts files.
  - `manifest.json` gains the `lang_keys` count, `lang.json` in `generated`, and a `dumped` list.
  - Every existing per-item file is byte-identical, and no index moved. The verifier checks the facts against the data: one bar entry per max damage with its items, every damage value, the ramp's ends, and every item's no-player tooltip rebuilt from `items.json` and `lang.json`.
- **`DA_Items` (reimported):**
  - Each item's attribute `Modifiers`, verbatim.
  - Each texture's `Layer`.
  - Soft references to `T_ItemTextures` (63 sprites, 16 x 16) and `T_ItemTextures32` (the spears' 7 in-hand sprites, 32 x 32): mip 0, nearest filtering, BGRA8, built with `BuildTextureArray`.
  - `Lang`.
  - A T3D diff against the pre-session asset differs only in those fields and the reference digest. A second import writes nothing. The 70 sprite assets are byte-identical.
- **Hotbar:** `EHudItemIcon` is now Token, Block and Sprite. Items draw their own sprite (`FFoundationItemDefinition::Sprite`, loaded by the HUD presenter, 63 brushes). The original pickaxe and sword HUD art is deleted.
- **Durability bar:** `HudModel::GetDurabilityBar` is single-precision under `#pragma float_control(precise)`, matches all 4,261 table rows, and clamps the width for damage past the max. The hotbar and the creative screen's hotbar row draw it (black 13 x 2 at the item's (2, 13), 1 px fill).
- **Tooltips:** `HudTooltip::GetModifierLines`. The creative screen shows the player form (" 4 Attack Damage", " 1.2 Attack Speed" in dark green); the no-player form equals the black-box table for all 63 items.
- **Creative text shadow:** it quartered linear colour (white's shadow drew 137). It now quarters the sRGB bytes, as `HudDrawList` does (63). This changes the creative screen's counts and tooltips slightly; the look call shows it.

## 4. S4: what to do

From the plan (section 4, "S4"), with S3's inputs:

1. **Mesh builder (Q7, approach C):** a pure C++ function from a sprite's opacity mask (`FFoundationItemTextureAsset::Mask`: one hex string per row, top row first, leftmost pixel in the highest bit) to quads. It makes front and back quads one pixel apart, plus a side face at each opaque-to-transparent edge, coloured by that edge pixel. Cache one mesh per item.
2. **Texture:** sample `ItemTextures` (16 x 16) or `ItemTextures32` (32 x 32) at `Layer`. They are soft references, so load them where the held view is built. They hold mip 0 only. If the held view or S8's dropped items need mips, rebuild the arrays: move them and `DA_Items` away, change `ImportItems.py`'s mip count, and reimport. First find what vanilla does: the project's mip generator is a port for block sprites only.
3. **Material:** a cutout item material.
4. **Transforms:** `generated`, `handheld` and `spear_in_hand` from `DA_Items`' models (Mojang units, verbatim), and the hand anchor measured with probe P1 (scout F's appendix C).
5. **Replace** the cube pickaxe and sword in `FoundationInteractionView`, then delete `MaterialTint` and the `Icon` key's tool cases.
6. **Tests and gate:** as the plan says (`Unnamed.Foundation.Items.Mesh`: quad counts for known masks, closed silhouette, front and back mirror; shots per material against probe P1, a look call).

## 5. Notes and known issues

- **(a) Tooltip layout:** a multi-line tooltip's 10 px line pitch and the 2 px gap under the name are not confirmed by any vanilla capture; no published source gives them (`HudTooltip::GetLineY`, `GetHeight`). The tooltip is not moved back on screen at the right or bottom edge either.
- **(b) Creative clicks drop damage:** `SetCreativeSlot` carries only the item and count, so moving a worn tool in the creative screen repairs it. Fix it in the authority (S6, durability, is the natural place).
- **(c) Base-value tooltip form unprobed:** `Player` needs a live level, which the black-box run cannot build. So the in-game form is not in the facts: the player's base attack damage 1 and attack speed 4 added, the leading space, dark green, and a zero amount's line (the diamond hoe shows " 1 Attack Damage" here). Its text and number format are checked against `lang.json` and the no-player table.
- **(d) Search tab:** vanilla's search tab adds each item's tab names to its tooltip; ours does not.
- **(e) Heap corruption during S3, cause not proven:** with hard references from `DA_Items` to the new arrays, 2 of 4 full native runs crashed in voxel worker threads (heap access violations in `FVoxelBlendedNoise::Sample` and `ParseWorldgenJson`). Subsets never crashed. Under `-NullRHI` those arrays never get platform data (`GetArraySize()` is 0). With soft references nothing loads them except the registry test, and 3 full runs in a row passed (153/153). The likely trigger is loading platform-less arrays with `DA_Items` in every test, but nothing pinned it down. If S4 loads the arrays in tests, run the full suite at least twice, and read their source data, not platform data, under `-NullRHI`.
- **A timing flake in the interaction probe:** "a weak hit inside the target's cooldown adds nothing" (the `weak repeat` case) clicks 0.2 s of wall time after the strong hit. On a loaded machine the target's hurt cooldown can run out in between, and the hit lands (strength 0.52, damage 2.5). It failed once at 09:11 with two other editors running, then passed on the re-run. S3 does not touch combat.
- **Merge order and saves:** as before: slots hold indices into the merged list for the session only; save ids when inventories are saved (voxel phase 10).
- **`DA_Items` is loaded once per process:** a reimport needs an editor restart.
- **Hard-coded counts:** 63 items, 63 HUD item sprites, 70 textures (63 + 7 layers), 6 lang keys and 4,261 bar values are deliberate tripwires for a reference rebuild.
- **Evidence:** `Tests/Evidence/` is git-ignored. S3's runs are in `C:\dev\sandbox\items-s3\logs\` (native suites, imports, exports, mutations) and the worktree's `Tests/Evidence/2026-10-10/`.

## 6. Code map: S3

| What | Where |
|---|---|
| Black-box facts | `Reference/minecraft-items-26.3/tools/ItemFacts.java` (README, "Rebuild": compile and run against the unbundled server jar, then `--sums-only`) |
| Reference tooltip text | `Reference/minecraft-items-26.3/lang.json` (`build_reference.py`, `OPERATIONS`, `DUMPED`) |
| Asset fields | `Source/Unnamed/Foundation/FoundationItemAssets.h` (`FFoundationItemModifierAsset`, `Layer`, `ItemTextures`, `ItemTextures32`, `Lang`, `FindItem`); `FoundationItems.h` (`Sprite`) |
| Import | `Scripts/Editor/ItemImportData.py` (`modifier`, `array_layers`, `lang`), `Scripts/Editor/ImportItems.py` (`ARRAYS`, `check_layers`) |
| Bar | `Source/Unnamed/Hud/HudModel.h/.cpp` (`FHudDurabilityBar`, `GetDurabilityBar`); drawn in `HudDrawList.cpp` and `HudCreativeScreen.cpp` (`FPainter::Stack`) |
| Hotbar sprites | `HudPresenter.cpp` (`ApplyInventory`, `CreateView`: `ItemSprites`), `HudDrawList.cpp` |
| Tooltips | `Source/Unnamed/Hud/HudCreative.h/.cpp` (`FHudTooltipLine`, `HudTooltip`), `HudCreativeScreen.cpp`, `Foundation/FoundationPanels.cpp` (`Details`, stack damage) |
| Tests | `Source/Unnamed/Tests/HudItemTests.cpp` (`Unnamed.Hud.Items.DurabilityBar`, `.Icons`, `.Tooltips`); `FoundationItemTests.cpp` (modifiers, sprite, layers and pixels, arrays, lang); `HudRuntimeProbe.cpp` (63 item sprites); `FoundationInteractionProbe.cpp` (HUD sprites) |
| Tour | `FoundationTourProbe.cpp`: `19h-hotbar-worn` (iron pickaxe 125 and sword 200 of 250); `22e` and `22f` hover the iron pickaxe and the netherite sword |
| S3 scratch | `C:\dev\sandbox\items-s3\`: `runfacts.ps1`, `fitbar.py` (the float32 fit), `editor.ps1`, `runtests.ps1`, `export_t3d.py` and `t3dcheck.py`, `mutate.py` (width, color, amount), `sheet.py` (the look-call sheet), `moved-DA_Items` and `moved-0737` (the replaced assets) |

## 7. What worked and what failed in S3

- **Worked:**
  - Running the game's public API black box for numbers that exist only in code. The bar's table fixed the C++ ramp to the exact byte, and a float32 fit in Python (`fitbar.py`) found the formula before any build.
  - Comparing the C++ against the whole black-box table rather than a few points. All three mutations failed the tests (bar width +1, hue shifted, amounts printed with one decimal).
  - A T3D export diff of `DA_Items` with the new fields stripped: it proved the reimport changed nothing else.
- **Failed or changed course:**
  - **Heap corruption with hard array references:** see 5(e). Two full runs crashed, subsets never did, and bisecting by subsets proved nothing for a crash that hits only some runs. Treat a crash in unrelated code after your change as yours (the manager had 8 clean full runs elsewhere), and confirm a fix with repeated full runs.
  - **Platform data under `-NullRHI`:** texture arrays loaded in automation have no platform data; test their source data.
  - **A `Player` for the tooltip run:** its constructor needs a `Level`, so the base-value form stayed unprobed.
  - **A queued run that waited for idle** would have started inside a quiet window; it was stopped in time. Don't queue editor steps behind waits.
  - **Shell state:** a Bash `cd` into the sandbox changed the session's working directory; use absolute paths.

## 8. Commands

```powershell
# Black-box facts (after build_reference.py; outside the project, the game writes logs/):
& C:\dev\sandbox\items-s3\runfacts.ps1 -Reference Reference\minecraft-items-26.3 -Source Reference\minecraft-items-26.3\tools\ItemFacts.java
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/build_reference.py --sums-only
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/verify_reference.py
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\ImportItems.py" -unattended -nop4 -nosplash   # verifies, never replaces
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' UnnamedEditor Win64 Development -Project="$PWD\Unnamed.uproject" -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -ExecCmds="Automation RunTests Unnamed" -TestExit="Automation Test Queue Empty" -unattended -NullRHI
& Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60
& Scripts\TestFoundationRuntime.ps1 -Editor -Suite tour -FrameRates 60
& Scripts\TestVoxel.ps1
Set-Location Tests; npm test; node source-check.mjs
```

## 9. Token use in S3

| Agent | Peak context | Notes |
|---|---|---|
| Lead (Opus 5.5) | about 460k at the commit (452k at the gate report) | 174k after the reading list. The probe, the reference rebuild, the code and the crash hunt took the rest. |
| Pre-commit reviewer (Sonnet 5.5) | 111.5k | Diff only (about 1,060 changed lines); no serious findings. Fixed: a hidden-bar comparison that could not fail, the width clamp, empty-texture guards, comments. |

## 10. Short prompt for S4

> Items phase, session 4: held-item rendering (the extruded mesh from each sprite's opacity mask, a cutout material, the display transforms, replacing the cube pickaxe and sword in the held view). Read `Docs/Records/2026-10-10-Items-Handoff-S4.md` first and follow it; it holds the reading list, the decisions, the S3 inputs (the texture arrays and masks in `DA_Items`), the known issues and the gate. Work only in the folder and branch the manager names; builds and native suites take turns with the main lane. Send the manager a short plan before building.
