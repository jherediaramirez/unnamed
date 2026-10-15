# Use step 17: buckets (record)

Lane `feature/survival-use` (worktree `C:\dev\projects\Unnamed-use`), from `045169bd` (8474c32 plus the 10b landing merge of `feature/survival`). Survival program track T3, scout AC's plan step 17 (items S9). Handoff: [2026-10-14-Use-Handoff-Step17.md](2026-10-14-Use-Handoff-Step17.md). Next on this lane: step 16 (survival fixes).

## 1. What was built

`foundation.Items.Use 1` (the eating switch): an empty bucket takes a water or lava source, or a waterlogged block's water; a water or lava bucket pours a source, and water waterlogs a dry waterloggable block. Details: `Docs/Items.md`, "Buckets (step 17)".

- `FFoundationAuthority::UseBucket`, called first in `UseItemStart`: an instant item use (vanilla's `Item.use`, not a use on a block) with its own ray from the player's facts. `FFoundationRay::bFluidSources` (`FoundationTargeting`) makes the empty bucket's ray stop at fluid sources, the eye's own cell included. Survival swaps a single bucket in place; stacks give one and add the result (dropped with item entities on, else `InventoryFull` with nothing changed). Creative keeps the used bucket; Adventure passes (both minecraft.wiki, provisional).
- Client (`FFoundationInteraction::UsePipeline`): a hand holding a bucket (`FoundationBucket::IsBucket`) sends `UseItemStart` after `UseOn` passes; only a use with a duration is held; a fill, a pour or a full-inventory refusal waits the held repeat.
- Events `Filled`, `Emptied`, `Extinguished` (the controller's event log names them). `SettleAfterFluid` sends `Extinguished` when a water or lava cell becomes stone, obsidian, cobblestone or basalt (switch 1 only).
- Sounds from T5's batch: `item.bucket.fill` / `fill_lava` at the feet, `item.bucket.empty` / `empty_lava` at the cell, 1/1 (`survival.r5.play_calls`, F 3629-3632); `block.lava.extinguish` 0.5, 2.6 + 0.8 (U - V) (wiki, provisional).
- Fluid delays: no change. `VoxelFluidRules::GetParams` already matches `extra.fluids` (water 5 ticks, drop-off 1; lava 30, drop-off 2; search 5/3 is the report's slope 4/2 counted from the spreading cell). No hash, golden or saved data moves.
- No `DA_Items`, `DA_Blocks` or `DA_Sounds` change; no new assets (no StandIns row).

## 2. Checks

- `Tests/Evidence/2026-10-14/Checks-final-20261014-195026/summary.txt`: GREEN after 28 min with the flight smoke and `expected-hashes-main.txt`. Native 369 of 369, voxel 207 suites, hashes 9 of 9, runtime-all 165 checks, interaction 120 checks, tour, create-world, flight, npm, source check, verifiers. Machine: other lanes' UnrealEditor-Cmd runs beside it (the run waited on Unnamed-ent's CEF lock once).
- Before the review fixes: `Checks-final-20261014-191725` GREEN (same steps).
- Captures (scenario 13e): `Tests/Evidence/2026-10-14/Playground-interaction/Runtime-20261014-200524/runtime-60-interaction-buckets-on.png` (the poured water beside the stone, an empty bucket in hand) and `-buckets-off.png` (switch 0: no water, the water bucket kept).
- Mutations (`C:\dev\sandbox\use-lane\runs\`): `mut-creative-keep-191057`, `mut-waterlog-flip-191127`, `mut-extinguish-191216`, `mut-held-repeat2-191344`, `mut-extinguish-switch-194931` each fail a test. `mut-held-repeat-191250` survived under VanillaReference alone (its attempt cadence already waits); the test now also runs LucidFluid.
- Pre-commit review (Sonnet 5.5, diff only, 957 lines, peak 81.3k): four findings, all taken: the extinguish event behind the switch, the full-inventory refusal's held repeat, the same-source pour documented, two test claims corrected (the unreachable washed-away term removed).

## 3. Known issues

- A plant a pour replaces goes without its drop. When: a pass over pops with item entities.
- Vanilla's rising-spread lava delay (x4 on a 3/4 roll, `extra.fluids`) is not modelled. When: the fluids track.
- Creative, Adventure and the extinguish sound's numbers are wiki values. When: a vanilla capture.
- No test pins the full-inventory refusal's held repeat (review finding 2). When: step 16.
- Cauldrons (T5 B1), milk and mob buckets (mobs).
