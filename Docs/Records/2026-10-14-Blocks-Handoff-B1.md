# Blocks B1 handoff: the reference extension (survival track T5)

Lane `feature/survival-blocks` (worktree `C:\dev\projects\Unnamed-blocks`). B0 (the sound batch) is committed at `b3abe8e` on top of the merge of `feature/survival` `8cfbb86` (`ff0131b`). B1 is [the blocks plan](2026-10-14-Blocks-Plan.md), section 2. Manager: the SM-Blocks chat (session `session_01AmANvUc5PUpNmYL3yAvm4F`); report there, not to Jesus.

## 1. Reading list (in order)

1. [The blocks plan](2026-10-14-Blocks-Plan.md): section 1 (B1's row and the hot-file column), section 2 whole (2.1-2.11: which blocks, the tools, the chest stand-in, the survival matrix, galleries, cascades, runtime import, `poplar_sapling`, world numbers, StandIns, files, checks), section 3 (blocks other tracks use), section 4.
2. [B0's record](2026-10-14-Blocks-B0-Sounds.md), sections 1 and 3: how an append-only rebuild of a hash-checked reference was proven (the append proof, the regeneration proof, the T3D comparison). B1 repeats that pattern on `Reference/minecraft-blocks-26.3` and `minecraft-items-26.3`.
3. `Reference/minecraft-blocks-26.3/README.md` and its `tools/build_reference.py` / `verify_reference.py` (grep first; they are long). Plan 2.2 says what to copy from T2's item-reference append.
4. `C:\dev\tools\shift-managers\survival\codex\research\01-t5-block-import.md`, section 3 ("answered", lines 18-33): the import pre-flight below comes from it.
5. `C:\dev\tools\shift-managers\survival\value-swaps.md`, section 2, "T5 Block families" (lines 112-136): the report values B1 cites (section 5 below).
6. `C:\dev\tools\shift-managers\survival\PROGRAM.md` sections 4-6 (switch rule, vanilla first, no world-output change).
7. Values: `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md` (grep, never read whole; never open its Appendix H paths); cite its evidence ids. Any other wiki value goes in `C:\dev\tools\shift-managers\survival\provisional-values.md`.

## 2. Working agreement

- Plan first to SM-Blocks (every file against the hot files: `FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`; whether hashes, goldens or saved worlds move), then wait for the go.
- Switch `foundation.Blocks.Families` (0 = the B1 blocks absent from the creative inventory and gallery tail, and refuse placing).
- Hash-checked references change only through their own `tools/build_reference.py`: new entries last, no index shift, existing per-item files byte-identical, an append proof script, a regeneration proof, the verifier passing. Move the accepted folder aside to `C:\dev\sandbox\...`, never delete (SM-Blocks: no deletions at all without its word, including sandbox folders).
- Asset locks: `DA_Blocks` and `DA_Sounds` are this track's; **`DA_Items` is granted to T5 for B1** (SM-Blocks, 2026-10-14). On a merge of `feature/survival` never hand-merge them: take the incoming asset, re-run the import, show its verify output.
- Checks with `Scripts\RunChecks.ps1 -Profile quick|final|merge` (`-Flight smoke` for the final). `Scripts\QuietWindow.ps1 -Name <what>` before any heavy launch outside RunChecks. Builds after a restore: touch sources or `-DisableAdaptiveUnity`.
- Pre-commit review by a Sonnet 5.5 subagent on the diff only (split over ~1,500 lines); re-run every suite the fixes touch; report A, the manager's commit approval, commit, handoff, report B. Never push.
- No `git stash`; no CLAUDE.md edits; never re-record goldens or world hashes (B1 must keep the radius-8 hashes and `Mesher.Digest`; if anything moves, stop and report).
- Measure lead context with `node C:\dev\sandbox\b0-sounds\MeasureAgentUsage.mjs --session <id>` (a copy of `Scripts\MeasureAgentUsage.mjs` with `projectRoot = "C:/dev"`; the id is the newest `.jsonl` in `%USERPROFILE%\.claude\projects\C--dev`).

## 3. Settled decisions

- All 302 family blocks plus the PM's 8 (cauldrons, fires, campfires); `poplar_sapling` joins `VegetationBlocks`, gated on the radius-8 hashes (plan 2.1, 2.8).
- The chest stand-in box (plan 2.3).
- Any Java 26.3 texture may stand in; every stand-in gets a row in `Docs/StandIns.md` (asset, jar path, where used, step).
- B0: the sound events for doors, trapdoors, gates, buttons, plates, lever, hoe, shovel and planting are in `DA_Sounds` (events 193-250); which block plays which is B1's `block_use_sounds.csv` (plan 2A). The blast furnace's event is `block.blastfurnace.fire_crackle` (no underscore; B0 left it out under the misspelled name, B1 appended it as event 332).

## 4. The import pre-flight (Codex, CONFIRMED against the jar unless marked)

What a plain blockstate-to-model importer gets wrong for the B1 blocks; check each against `Scripts/Editor/ImportBlocks.py` and the block reference's builder before relying on it:

1. **Texture values that are objects:** `{sprite, force_translucent}` instead of strings.
2. **`uvlock`** (stairs).
3. **`shade_direction_override`** (`models/block/template_fire_floor.json` and others).
4. **Geometry outside the cell:** crop and stem `from` Y = -1 (`models/block/crop.json`); campfire flames to Y = 17 (`template_campfire.json`). B2's collision gather goes one cell lower for this.
5. **8 animated textures:** prismarine_stairs, prismarine_wall (`prismarine.png.mcmeta`), water_cauldron (`water_still`), lava_cauldron (`lava_still`), fire (`fire_0/1`), soul_fire (`soul_fire_0/1`), lit campfire and soul campfire (`*_fire`, `*_log_lit`). `glass.png.mcmeta` only sets `mipmap_strategy`.
6. **Tint hooks** (`tintindex`): pumpkin and melon stems (`stem_growth0..7`; the stem's age colour), water, lava and powder-snow cauldrons (`template_cauldron_{level1,level2,full}`; water = water colour; lava and powder snow: check the reference README "Tints", PLAUSIBLE).
7. **17 `force_translucent` panes** (clear glass too, `glass_pane_*`); water in the water cauldron is translucent. Cutout for plants, ladder, fire needs PNG alpha (PLAUSIBLE).
8. **The chest's particle-only models:** chest, trapped_chest, ender_chest models hold only `textures.particle` (block-entity rendered; items use `minecraft:special`), hence the stand-in box. The pitcher crop's upper half (age 0-2) is particle-only too: import it as empty, not entity-rendered.

Also: beds are ordinary model geometry (`template_bed_{head,foot}`); 74 multipart blocks (14 fences, 32 walls, 26 panes and bars, fire, soul fire: wall `none/low/tall` with a conditional post, pane "noside" pieces, bars posts and caps, bamboo fence's own directional models, fire's `when.OR` and weighted `apply`); random model choice for dirt_path (4 rotations), fire, soul fire; no builtin parents; random offsets: none in the 30 sampled, the other 280 unchecked (PLAUSIBLE).

## 5. Report values for T5 (value-swaps.md section 2)

Cite `catalog.blocks` (Runtime registry, default state) for B1's facts: stairs oak 2/3, 80 states, cobblestone 2/6, tool yes; oak fence 2/3, 32 states; cobblestone wall 2/6, 324, tool yes; glass pane 0.3, 32; iron bars 5/6, 32, tool yes; oak slab 2/3, 6; doors oak 3, 64, iron 5, 64, trapdoor 3, 64; buttons (stone, oak) 0.5, 24; lever 0.5, 24; ladder 0.4, 8; vine 0.2, 32, random tick; chest and trapped chest 2.5, 24; beds 0.2, 16; farmland 0.6, 8, random tick; dirt path 0.65, 1; wheat, carrots, potatoes 0, 8 (beetroots 4), random tick; oak sapling 0, 2, random tick; fire 0, light 15, 512 states; soul fire light 10; campfire 2, light 15, 32; cauldron 2, tool yes. (h/r = hardness/resistance where they differ.) The other rows (stair corners, hinges, gate placement, climbing, chest pairing, sleep, farmland, trampling, crop and sapling growth, special surfaces) belong to B2-B7; they are in the same table.

## 6. Code map (what B0 touched, for B1's sound side)

- `Reference/minecraft-sounds-26.3/tools/build_reference.py`: `event_order()` puts step and fall events of sound types new in `block_sounds.csv` after every earlier event; `SOUND_TYPE_BASE` (86) is frozen; `tools/own_files.txt` freezes homes. If B1 adds sound types to `block_sounds.csv`, rebuild this reference too (its `source` pins the CSV's SHA-256, so the verifier fails until it is rebuilt), update the verifier's group counts and `FoundationSoundTests.cpp`'s pins.
- `Scripts/Editor/SoundImportData.py`: per-block step and fall events by index into the events, in `block_manifest.json`'s order.
- `Scripts/Editor/ImportSounds.py`: `append()` accepts only an exact event prefix **and equal blocks** (see known issue 1).

## 7. Known issues

1. **When B1 adds blocks, `ImportSounds.py` will refuse `DA_Sounds`.** Its append path requires the block list to be equal, and B1 grows `block_manifest.json`. Options: (a, recommended) extend `append()` so the old blocks must be an exact prefix too (B1 appends blocks last, so it holds) and the new block entries are appended; (b) move `DA_Sounds` aside and rebuild it whole (old waves stay; the T3D comparison then shows the old events unchanged). Either way, SM-Blocks confirms in the plan.
2. `SoundImportData.py` requires the manifest and `block_sounds.csv` to list the same blocks; both come from the block reference's build, so B1 keeps them in step.
3. The native run's one warning (`Unnamed.Foundation.Blocks.Textures`: an engine HTTP probe timing out) is environmental and was there with B0's run.

## 8. What worked, what failed (B0)

- Worked: a sandbox copy of the reference folder next to a junction to the blocks reference (`C:\dev\sandbox\b0-sounds\dry1`) for dry builds; an append proof against `git archive HEAD` of the folder; the T3D export of a data asset (`AssetExportTask` with `ObjectExporterT3D`, `C:\dev\sandbox\b0-sounds\export_t3d.py`) for before and after; `unreal.log` lines land in `Saved\Logs\Unnamed*.log`, not the redirected console output.
- Failed or slow: Python patch scripts in a bash heredoc turn `\n` and `\t` inside replacement strings into real characters (fixed by hand; check with `ast.parse` and a grep for tabs). `Remove-Item` on a path is blocked by the harness; copy with `Get-ChildItem -File | Copy-Item` instead of copying and then pruning.

## 9. Token figures (B0)

Lead peak 169.7k (78 requests). One Sonnet 5.5 pre-commit reviewer, peak 66.4k. The first build in a fresh lane takes about 3 minutes; `RunChecks -Profile final -Flight smoke` took 26 minutes.

## 10. Evidence

- `C:\dev\projects\Unnamed-blocks\Tests\Evidence\2026-10-14\Checks-final-20261014-152439\summary.txt` (B0's final run, every suite folder listed).
- `C:\dev\sandbox\b0-sounds\`: `DA_Sounds-before.t3d`, `DA_Sounds-after.t3d`, `ue-import1.log`, `ue-import2.log`, `append_proof.py`, `scan.py`, `review.diff`.
- `C:\dev\sandbox\b0-sounds-old\`: the accepted sound reference before B0 and the first rebuild (kept; no deletes).

## 11. Open questions

1. Known issue 1: extend the append path (a) or rebuild `DA_Sounds` (b)? Recommended (a).
2. Push of `feature/survival-blocks`: Jesus's call (not asked yet).
3. A second sound batch: lines added to `sounds-batch.md` after 19:20 UTC on October 14 go into a small batch at the end of the program (none so far).
