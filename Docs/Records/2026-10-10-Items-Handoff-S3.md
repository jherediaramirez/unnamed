# Items handoff: into S3 (icons and tooltips)

Read this first if you are picking up the items phase in a new chat. Items S2 is done and committed (`180f620` on `feature/items-1`, a parallel lane next to the voxel chain): `DA_Items` and its importer, `ItemRoot`, the `BuildRules` merge, the loadout `stone, iron_pickaxe, iron_sword`, and the practice tools retired. **S3 is next:** item icons in the in-game hotbar, the durability bar, the attack tooltips and the item texture array for S4. This was true at `180f620`; check `git status`, `git log -5` and newer records before trusting it.

## 1. Read these, and only these

Budget: about 80k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md).
2. **This handoff.**
3. **[The items phase plan](2026-10-09-Items-Phase-Plan.md):** sections 1 (decisions), 3 (sessions and lane rules) and 4 ("S3", "S4").
4. **[Docs/Items.md](../Items.md):** data flow, ids, the merge, presentation until S3 and S4.
5. **[Scout F's research](2026-10-09-Items-Scout-Research.md):** section 3 ("Models and rendering", for the texture array), and appendix C, probe P2 (the durability bar's colours).
6. **[Docs/Blocks.md](../Blocks.md):** "Creative inventory" (how block icons draw).
7. **Code, headers first:**
   - `Source/Unnamed/Foundation/FoundationItemAssets.h/.cpp` (`DA_Items`, `MakeDefinition`, the temporary `MaterialTint`);
   - `Source/Unnamed/Hud/HudPresenter.cpp` around `EHudItemIcon` (the in-game hotbar's icon choice) and the HUD painter that draws `Pickaxe`, `Sword` and `Token`;
   - `Source/Unnamed/Hud/HudCreativeScreen.cpp` (`ItemSprite`, the brush list: `ItemRoot/Gui` first, then `BlockRoot/Gui`);
   - `Scripts/Editor/ImportItems.py` and `ItemImportData.py` (`import_sprites`, the asset fields).

## 2. Working agreement

- **The manager:** Nyx at night, Hemera by day. Send a short plan first and report before and after committing; the manager confirms plans within Jesus's decisions and approves the end-of-session local commit after a clean Sonnet 5.5 pre-commit review (diff only) and passing checks. Never push; the manager merges.
- **The lane:** S3 runs where the manager says. Never touch another lane's folder.
- **Machine turns:** the main lane has priority. Before each build, editor run or suite, check `Get-Process UnrealEditor*, UnrealBuildTool`; S2 waited for 30 s of no such process before each step (`C:\dev\sandbox\items-s2\waitidle.ps1`, `runtests.ps1`). On "quiet window", finish the current step and start nothing until "window over".
- **Context:** checkpoints at about 300k and every 200k after, measured with `Scripts/MeasureAgentUsage.mjs` (S2's helper: `C:\dev\sandbox\items-s2\measure.sh`; change its session id).
- **Subagents:** give each exact paths and a stop near 150k.
- **Game code:** data, reports, published docs and black-box runs only. `javap -public` for signatures is fine. No decompiled method bodies, no reflection into private internals.

## 3. Where things stand

| Session | Area | Status |
|---|---|---|
| S1 | Item reference, phase plan, `CLAUDE.md` id rule | **Done** (`318c48f`) |
| S2 | Item registry and rules | **Done** (`180f620`) |
| S3 | Icons and tooltips | **Next** |
| S4 to S9+ | Held items, mining by tier, loot, durability, combat, item entities, behaviours | Planned ([plan](2026-10-09-Items-Phase-Plan.md), section 3) |

**S2 built:**
- **`DA_Items`** (`UFoundationItemAssets`, `/Game/Items`, `UUnnamedProjectSettings::ItemRoot`, Python `ProjectPaths.item_asset()`; always cooked). Per item: id, name, protocol id, group, material, kind, max stack, max damage, attack totals, the tool component (default speed, ordered rules with the selector verbatim and its `Block.*` tag name, damage per block, can destroy in creative), damage per hit, blocking disable time, enchantability, repair selector and items, `Item.*` tag names, creative positions, the model definition's cases and sprite names. Also the 70 models (layers, parent, GUI light, every display context's transform in Mojang units) and the 70 sprites (size, opaque pixel count, RGBA SHA-256, opacity mask), plus the reference's digest.
- **Sprites:** the 70 item PNGs as loose GUI textures in `/Game/Items/Gui/item/<name>` (the `BlockRoot/Gui` settings: no mips, nearest, UI group). The creative screen draws them, so Tools & Utilities (31 items), Combat (22, with TNT last), Food & Drinks and Ingredients show at the 26.3 positions. The plan's `T_ItemIcons` atlas is **not** built; decide in S3 whether the in-game hotbar needs it or can use these textures.
- **Tags:** `Config/Tags/MinecraftItemTags.ini`, the 43 item tags as `Item.*` (`ItemImportData.py --write-tags`). The native `Item.Tool.Pickaxe` and `Item.Weapon.Sword` were removed (only the practice tools used them).
- **Rules:** `BuildRules` merges block items, then `DA_Items`, then project items; a clash logs an Error and skips the later item (`MergeItems`). Unregistered tags (`incorrect_for_diamond_tool`, `incorrect_for_netherite_tool`: empty) and the swords' `minecraft:cobweb` rule have empty tags and match nothing; `#sword_instantly_mines` matches bamboo.
- **Loadout:** `stone, iron_pickaxe, iron_sword`. `practice_pickaxe` and `practice_sword` are retired ids (`CLAUDE.md`).
- **Presentation left for S3 and S4:** the in-game hotbar and the held view still use the `Icon` key (`pickaxe`/`sword` by kind, everything else the generic token) and `IconColor` from `MaterialTint` in `FoundationItemAssets.cpp`. Replace both with the sprites (hotbar, S3) and the extruded meshes (held view, S4), then delete `MaterialTint`.

## 4. S3: what to do

From the plan (section 4, "S3"), adjusted for what S2 already did:

1. **Hotbar sprites (Q9):** the in-game hotbar draws items as flat sprites and blocks as 3D icons, like the creative screen. The `Icon` key stays only where S4 still needs it.
2. **Durability bar:** 13 x 2 px under the icon, hidden at full durability; its colour ramp from probe P2 (scout F's appendix C) or published data. The creative screen and the hotbar both draw it.
3. **Tooltips:** the creative screen's tooltip gains the "When in Main Hand" lines (attack damage, attack speed) from the jar's lang keys, formatted as vanilla.
4. **Texture array for S4:** `T_ItemTextures` from the 70 sprites (the 7 spear in-hand sprites are 32 x 32: a second array or a region), with `DA_Items` pointing at each sprite's layer. Adding fields to `DA_Items` needs a reimport: `ImportItems.py` refuses to touch a `DA_Items` from other data, and only checks ids and counts on the same data, so move the asset away deliberately and rerun (then restart the editor).
5. **Tests:** pin the hotbar's icon choice per item kind, the durability bar's pixels and colours at a few damage values, and the tooltip lines for the iron pickaxe and the netherite sword; tour shots of the hotbar and a tooltip.
6. **Gate:** every native suite, the interaction probe with 0 failures, `TestVoxel.ps1` (radius-8 hashes and goldens unchanged), the three verifiers, `npm test`, `source-check.mjs`; the tour accepted by the manager (look call).

**Not in S3:** held meshes (S4), break timing by tier (S5), loot (S5b), durability wear rules (S6), combat by item (S7).

## 5. Notes and known issues

- **Merge order and saves:** inventory slots hold indices into the merged list for the session only. Inventories are not saved yet (voxel phase 10); when they are, save ids.
- **`DA_Items` is loaded once per process** (`LoadDefault`, rooted like `DA_Blocks`): a reimport needs an editor restart.
- **Hard-coded counts** in the tests (63 items, 77 tool rules, 31 tools, 22 combat entries) are deliberate tripwires for a reference rebuild.
- **Break times, hoe wear, spears:** as in the S2 handoff, section 5. S2 tested no break times; hoes wear 2 per hit (the data); spears have no tool component, so mining with one costs nothing.
- **Evidence:** `Tests/Evidence/` is git-ignored; S2's runs are in the worktree's `Tests/Evidence/2026-10-10/`.

## 6. Code map: S2

| What | Where |
|---|---|
| Asset and conversion | `Source/Unnamed/Foundation/FoundationItemAssets.h/.cpp` |
| Merge and loadout | `Source/Unnamed/Foundation/FoundationInteractionSettings.h/.cpp` (`BuildRules`, `MergeItems`) |
| Paths | `UUnnamedProjectSettings::ItemRoot`, `GetItemAssetPackage/Path`; `Scripts/Editor/ProjectPaths.py` `item_asset()`; `Config/DefaultGame.ini` (always cook `/Game/Items`) |
| Import | `Scripts/Editor/ItemImportData.py` (data, hashes, tags), `Scripts/Editor/ImportItems.py` (editor) |
| Creative screen sprites | `Source/Unnamed/Hud/HudCreativeScreen.cpp` |
| Tests | `Source/Unnamed/Tests/FoundationItemTests.cpp` (`Unnamed.Foundation.Items.Registry`, `.Rules`, `.Merge`); `HudCreativeTests.cpp` (`.Tabs`: tools 31, combat 22) |
| Tour shots | `Source/Unnamed/Foundation/FoundationTourProbe.cpp` (`22e-creative-tools`, `22f-creative-combat`) |
| S2 scratch | `C:\dev\sandbox\items-s2\` (patch scripts, `runtests.ps1`, `waitidle.ps1`, logs, the mutation patch `mutate.py`) |

## 7. What worked and what failed in S2

- **Worked:**
  - Copying the block pipeline's shape: a stdlib data module (`ItemImportData.py`) that the editor script imports, the same SHA guard, the same GUI texture settings.
  - Keeping each rule's selector verbatim next to its resolved tag: the `.Rules` test can check every selector against `tags.json` and the block registry, which is how the empty-tag and cobweb cases stay honest.
  - Env-gated mutations in one temporary build (9 mutations, 2 builds instead of 9), run one at a time and then reverted, with a grep for the gate string before the clean rebuild.
  - A Sonnet 5.5 diff reviewer at about 100k: no serious findings; its test-strength notes added the display-transform checks.
- **Failed or changed course:**
  - **A queued chain of editor runs broke a quiet window.** The tour followed the probe in one background command; a watcher polling the task's buffered output reacted late, and the classifier refused `Stop-Process`. Launch each editor step as its own command and check the processes right before it.
  - **A misplaced temporary patch:** the first display mutation landed between an `if` and its `else`, so the missing-asset warning fired on every load. A clean run caught it. Re-read a mutation patch's context before building.
  - **Tab expectations:** Combat holds TNT (22 entries, not 21), and Spawn Eggs shows (the spawners), so no category tab is empty any more; the hide rule is now tested on a model with no items.
  - **Mixed line endings:** `Config/DefaultGame.ini` mixes CRLF and LF; a whole-file rewrite changed 120 lines until the insertion matched the neighbouring line's ending.

## 8. Commands

```powershell
uv run --no-project --python 3.13 python -I Scripts/Editor/ItemImportData.py --write-tags   # item tags, then restart the editor
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\ImportItems.py" -unattended -nop4 -nosplash   # verifies, never replaces
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/verify_reference.py
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' UnnamedEditor Win64 Development -Project="$PWD\Unnamed.uproject" -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -ExecCmds="Automation RunTests Unnamed" -TestExit="Automation Test Queue Empty" -unattended -NullRHI
& Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60
& Scripts\TestFoundationRuntime.ps1 -Editor -Suite tour -FrameRates 60
& Scripts\TestVoxel.ps1
Set-Location Tests; npm test; node source-check.mjs
```

## 9. Token use in S2

| Agent | Peak context | Notes |
|---|---|---|
| Lead (Opus 5.5) | about 360k at the end (342k at report A) | About 155k after the reading list, with a large system prompt. Waiting for machine turns cost little. |
| Pre-commit reviewer (Sonnet 5.5) | 102.8k | Diff only, excluding the generated assets |

## 10. Short prompt for S3

> Items phase, session 3: icons and tooltips (item sprites in the in-game hotbar, the durability bar, the "When in Main Hand" tooltip lines and the item texture array for S4). Read `Docs/Records/2026-10-10-Items-Handoff-S3.md` first and follow it; it holds the reading list, the decisions, the scope, the tests and the gate. Work only in the folder and branch the manager names; builds and native suites take turns with the main lane. Send the manager a short plan before building.
