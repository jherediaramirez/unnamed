# QC bug status — Minecraft 26.3, 2026-10-16

**Updated 2026-10-16 ~14:45 in `C:\dev\projects\Minecraft 26.3`** (the single project folder). The three paused fixes (LOW02 R40-4, LOW03 R17-1 and R47-4) were finished and committed on `feature/survival`. It was pushed to GitHub on 2026-10-16: eb52cea7 + 1d81fbf6 R40-4, e8275a71 R17-1, 1215000b R47-4, 0c8d9a74 RunChecks quoting fix, plus the status docs. Final checks on the committed code are GREEN:
- native: 583/583
- voxel: 211 suites
- radius-8 and digest hashes: 9/9 against `expected-hashes-survival.txt`
- interaction runtime: 155/155
- mobs runtime: 25/25
- npm: 85/85
- source-check and all Reference verifiers

Evidence is in `Tests\Evidence\2026-10-16\` (`Checks-final-20261016-141746`, `Checks-final-20261016-143002`, `Minecraft-26.3-fixes\`). The radius-8 hash step failed on the first run only because `RunChecks.ps1` didn't quote the project path, which now contains a space (fixed in 0c8d9a74).

## 2026-10-16 evening: 12 low fixes for an editor check (branch `fix/qc-low-editor-12`, not merged)

Jesus asked for the low-severity bugs that are quick to fix and that he can check himself in the editor. Twelve were fixed on `fix/qc-low-editor-12` (from `feature/survival` at 48e5d18d). Build GREEN; native `Unnamed.Foundation` 313/313 and `Unnamed.Hud`+`Unnamed.Vitals`+`Unnamed.Mobs` 38/38 (logs under `%TEMP%\claude\qc12-*.log`). The full RunChecks gate was not run, and nothing is merged. Each fix waits on Jesus's editor check.

| ID | Fix | Source of the value |
|---|---|---|
| R24-4 | Drowning hits play `entity.player.hurt_drown` (a drown-hit counter in vitals) | 26.3 `damage_type/drown.json` |
| RAX-3 | A breaking tool plays `entity.item.break` at volume 0.8, pitch 0.8-1.2 | vanilla `LivingEntity.breakItem` (provisional, not measured) |
| RA3-6 | `entity.player.hurt_on_fire` plays at pitch 1 | `survival2.r23.fire_sounds` (run 2 measured 1.0) |
| R37-1 | A chest opens under glowstone, redstone block, observer, soul sand and mud | wiki "Conductivity"; Chest.Blocked test adds glowstone |
| R47-1 | A spectator uses no item on a mob (no dye, milk or shears) | as `UseOn` for blocks |
| R44-2 | A lava bucket on a cauldron with water above does nothing | MC-203748 (fixed 24w33a) |
| RA2-7 | XP orb billboard centre 17.5 cm up (was 10) | `survival2.r23.xp_quad` (edges 0.025 and 0.325) |
| R40-5 | A stuck arrow can be picked up from the 7th step after it hits | vanilla `shakeTime` 7; Arrows.Pickup test checks the shake |
| R43-1 | Furnace particles hold still while paused | wiki (Esc pauses) |
| K-MOB-4 | Mob footsteps every 1/0.6 blocks (about 167 cm), not every block | vanilla `Entity.move` moveDist x 0.6 |
| R24-2 | A full 256-character chat line keeps its text; an insertion keeps only what fits | `survival.r4.open_fields`; vanilla edit box |
| K-MOB-9 | Monsters do not spawn on glass, stained or tinted glass, or barrier (`Block.Impermeable`) | vanilla `isValidSpawn(never)` |

Totals with these: 28 landed + 12 fixed on the branch, 1 scope closure, 115 remaining (97 bug/probe rows, 18 deferred/post-QC).

Before this update, everything was stopped at `eb52cea7` (landing 54); see [reference/QC-STOP-2026-10-16.md](reference/QC-STOP-2026-10-16.md) for that snapshot.

## Reconciled totals

| Category | Count | Meaning |
|---|---|---|
| Landed code fixes | 28 | 27 with automated evidence; R37-2 has static lifetime proof plus a passing tour/gate |
| Approved scope closure | 1 | K-PLAYER-2; not a new audible acceptance result |
| Paused candidate fixes | 0 | (LOW02/LOW03 finished 2026-10-16) |
| Other remaining bug/probe rows | 109 | 100 open, 1 partial, 7 needing permitted evidence, 1 needing reproduction |
| Deferred / post-QC | 18 | 15 deferred/not built/out of scope; 3 after QC |
| Total main inventory | 156 | 3 high, 36 medium, 117 low; 156 unique IDs |

**127 rows remain outside landed fixes and the scope closure** (109 bug/probe rows plus 18 deferred). Their severity is 0 high, 33 medium, 94 low. All three recorded highs have landed code. Of the 28 landed rows, 18 need human replay, 4 retain packaged acceptance obligations, and 6 are test/probe-only. Automated checks do not establish complete gameplay acceptance.

The original inventory audit was completed at 13:34 EDT on eb52cea7. Original discovery evidence and plausible `(P)` markers are retained; they are not labels for the quality of the later fix verification. Historical entries outside the main table are listed separately below. No new bug investigation or fix was started for this report.

## Landed fixes

The symptom identifies the bug corrected. Evidence limits and pending acceptance stay visible.

| ID | Severity | Batch / landing | Bug corrected | Acceptance / limitation |
|---|---|---|---|---|
| K-BUILD-4 | high | Q24 / 51 | The packaged Win64 Development game exits at launch with code 777006 (CrashDuringStaticInit) and writes no log, windowed or -NullRHI; so no packaged session can run (the exit bar needs them). Found by Q02 (SPLIT from R35-3: the package now stages the sky reference, but its runtime proof waits on this). Not in the editor -game play copy. | Packaged acceptance sessions pending; Packaged startup root cause and launch evidence recorded; comprehensive packaged human sessions remain pending. |
| RQ02-1 | low | Q24 / 51 | Q02's staging source-check misses reference roots built from separate TEXT("Reference") + TEXT("<root>") pieces (no such reader exists today). | Packaged acceptance sessions pending; Future-proofing source-check gap; no current split-path reader existed at discovery. Script mutation evidence, not a new gameplay feature. |
| RQ02-2 | low | Q24 / 51 | The dev command `voxel.BiomeMap` reads `Reference/minecraft-worldgen-26.3/facts/biome_lookup_samples.json`, which packages don't stage (dev command only). | Packaged acceptance sessions pending |
| R37-2 | low | Q03 / 50 | The double-chest tour case can write through a dangling map reference (possible tour crash). | Test/probe-only; no human replay required; Static lifetime proof plus a passing tour/full gate; no executed RED for the dangling-reference root cause. |
| R20-3 | low | Q03 / 50 | The death probe's "click on Respawn respawns" passes even if the click does nothing. | Test/probe-only; no human replay required |
| R20-4 | low | Q03 / 50 | `/kill @e` test claims "no orbs left" but never spawns an orb. | Test/probe-only; no human replay required |
| R30-5 | low | Q03 / 50 | Spawn-cycle tests can't catch a wrong period (400) or cluster size (4). | Test/probe-only; no human replay required |
| R9-2 | low | Q03 / 50 | The "EatMotion 0" probe can't tell the two eating motions apart. | Test/probe-only; no human replay required |
| R38-1 | med | Q06 / 53 | Master Volume resets to full after opening a world (the slider still shows the saved value). | Human replay pending; NullRHI automated test proves map-load subscription/reapplication, not audible device volume; human audio replay pending. |
| R38-2 | low (P) | Q06 / 53 | With Bass partly up, the first attach plays at the full +5 dB until the next change. | Human replay pending; Tracker retains original plausible marker even though a project-behavior RED/GREEN was recorded; do not silently treat the discovery Evidence column as fix-verification status. |
| R38-3 | low | Q06 / 53 | Lily pads (and other thin inside-step blocks) are silent underfoot. | Human replay pending |
| R38-4 | low | Q06 / 53 | Footsteps go silent when your centre is past a block edge you still stand on. | Human replay pending; Shoreline failures corrected in re-review. Mixed-height wrong sound remains RQ06-1; carpet-edge guard had no failing RED. Support tie order is provisional. |
| R46-1 | low | Q03 / 50 | The smear probe can still pass if the mob models load with no geometry (a mob counts as drawn once its layers exist). | Test/probe-only; no human replay required; Mutation RED preceded the final faceless-pivot adjustment; final GREEN and full gate passed, but that last adjustment was not mutation-tested again.; Closes the empty-model edge left by historical R41-4; do not count that link as a new independent bug. |
| RA3-5 | low | Q01 / 48 | The hurt tilt doesn't depend on the attacker's direction. | Human replay pending; Native behavior/wiring tested; front/back tilt pitch remains provisional and visual replay is pending. |
| R34-2 | low | Q01 / 48 | Shield wear ignores difficulty scaling of mob hits (Hard: 5 instead of 7; Peaceful still wears). | Human replay pending |
| RA1-1 | med | Q04 / 52 | Clicking outside the creative inventory with a held stack deletes it (no drop). | Human replay pending; Original outside-click bug has observed RED/GREEN. RefusedThrow follow-up guard has static failability proof only; its attempted RED failed compilation and was not re-run. |
| RA1-2 | low | Q04 / 52 | Turning a container switch off leaves an open furnace/barrel/chest menu working, and a lit furnace stays lit. | Human replay pending; Primary fix landed; separate low furnace-icon edge remains RQ04-2. |
| RA1-3 | low | Q04 / 52 | Pressing the other mouse button during a drag doesn't cancel the drag. | Human replay pending |
| RA1-4 | low | Q04 / 52 | The creative inventory ignores rebound inventory and hotbar keys (hardcoded E and 1-9). | Human replay pending; Keyboard and thumb-button repair landed; stale mouse-position edge remains RQ04-1. |
| RA1-5 | low | Q04 / 52 | Creative middle-drag (clone across slots) exists in the authority but no screen sends it. | Human replay pending |
| K-BLOCK-4 | low | CODEX-LOW-01 / 54 | `foundation.Blocks.Interact 0`: buttons and levers never pop when their support goes. | Human replay pending |
| R36-2 | low | CODEX-LOW-01 / 54 | In creative, breaking the top half of a sunflower (any two-half plant) drops the plant. | Human replay pending |
| R35-1 | high | Q01 / 48 | Zombie hits ignore a raised shield (full damage, knockback, no shield wear). | Human replay pending |
| R35-2 | low | Q01 / 48 | A killing zombie hit still lets the player finish an action that step (e.g. the last bite of an apple), so that item is missing from the death drops. | Human replay pending |
| R35-3 | high | Q24 / 51 | In a packaged build zombies never burn in daylight, and open-sky night monster spawning never happens (sky light stays 15): the sky reference `Reference/minecraft-sky-26.3` isn't staged (audit cross-track pass, still present at c79ee9ad). | Packaged acceptance sessions pending; One ID across Q02 staging and Q24 packaged proof; count once, final resolution at Q24.; Packaged night light and sun burning were recorded; whole packaged views suite was not green. RQ24-1 tracks the broad zombie counter, RQ24-2 tracks dead-player test conditions. |
| R40-4 | low | CODEX-LOW-02 / Minecraft 26.3, 1d81fbf6 | Setting `foundation.Items.Bow 0` mid-draw keeps the draw (slowdown, zoom). | Human replay pending (draw, Bow 0, keep holding). RED/GREEN and independent review READY_FOR_GATE 0/0/0 from LOW02; `Bow.SwitchMidDraw` passes in the 583/583 final native run. |
| R17-1 | low | CODEX-LOW-03 / Minecraft 26.3, e8275a71 | `foundation.Fire 0` doesn't put out a fire already burning. | Human replay pending (burn, Fire 0). RED from LOW03 (health 17→16 with Fire 0); final interaction runtime 155/155, Fire 0 keeps health 17→17, overlay off; on/off captures viewed. Player path only; mob callers keep their default. |
| R47-4 | low | CODEX-LOW-03 / Minecraft 26.3, 1215000b | `foundation.Mobs.Farm 0` keeps cold/warm animals drawn as their variant. | Human replay pending (Farm 0/1 near warm/cold animals). RED from LOW03 (12 stale-skin failures); `Mobs.FarmViewSwitch` passes in the final native run; mobs runtime 25/25, on/off/restored captures viewed (sheep largely hide the cows). Presentation only; stored variant kept. |

## Closed by scope decision

| ID | Decision | Still outstanding |
|---|---|---|
| K-PLAYER-2 | Jesus closed the Master/Bass/footsteps scope; this is separate from the 25 code fixes. | Category sliders are K-PLAYER-8 after QC; Q06 human audio replay remains pending. |

## Paused changes

None. LOW02 (R40-4) and LOW03 (R17-1, R47-4) were finished on 2026-10-16 and are in the landed table above.

## All other remaining bug/probe rows (109)

No work on these is active. Batch labels preserve the existing plan and do not authorize starting it.

### Build and tests (9)

| ID | Severity | Batch | State | Remaining symptom/scope |
|---|---|---|---|---|
| K-BUILD-1 | med | Q17 | Open | Native test run crashed in the `FVoxelDensityScratch` destructor during `FVoxelFreezeMovedTest` (20a saw one in `Mesher.Digest` too; 20e1 a third, in `ReallocGrow1` during `Voxel.Mesher.Digest`, attempt 1). |
| K-BUILD-2 | low | Q18 | Open | `create-world` test times out attaching to CEF on its last relaunch when other lanes' editors run (green on rerun). |
| K-BUILD-3 | med | Q18 | Open | Voxel automation tests (`Unnamed.Voxel.Pipeline.Stages`, `Unnamed.Voxel.Pipeline.ShiftPlan`) fail under shared machine load with only `LogHttpConnection: ...socket_send_failure` errors and no assertion; green alone and on rerun. Seen 3 times across lanes: an outside MCP client on the test editor's MCP server (SM-Mobs 20c2), and Unnamed-inv `Checks-final-20261015-074605` and `Checks-merge-20261015-225844` (`VoxelAutomation-20261015-230259`). |
| K-BUILD-5 | low | Q18 | Open | A Windows audio device change during a native run fails whichever tests are running: `LogAudioMixer: FAudioMixerWasapi::PostDeviceSwap - null device swap result!` is logged as an error (seen 2026-10-16 11:49:42 local in CODEX-LOW-01's merge gate: Unnamed.Voxel.Mesher.FluidPlayground, .Mesher.Models, .Noise.Throughput; unrelated to the batch). |
| RQ24-1 | low | Q18 | Open | The packaged views suite's noon-zombie case passes on any zombie burning in the sun, not the summoned one. |
| RQ24-2 | low | Q18 | Open | Packaged views shots (render distance 32, held items at FOV 110) fail intermittently because the player is dead ("slain by Zombie"): night monsters now spawn in the package (after Q02/Q24), and the views suite doesn't run in Peaceful or with spawning off. Not a rendering bug. |
| K-MENU-1 | low | Q18 | Open | Editor menu-transitions check: 1 FAIL at step N, Create New World hit its frame cap (load 8.7 s against a 9.67 s cap). Not caused by Q24 (reviewer); not proven pre-existing (no base run). |
| RQ04-1 | low | Q21 | Open | A hotbar key bound to a mouse thumb button acts at the last mouse-move position in the creative screen (same as the keyboard path). |
| RQ04-2 | low | Q21 | Open | With foundation.Furnace 0, the furnace screen's flame icon can still show if the close is refused (full inventory, a carried stack, Items.Entities 0). |

### Player and feel (30)

| ID | Severity | Batch | State | Remaining symptom/scope |
|---|---|---|---|---|
| R24-1 | med | Q15 | Open | Camera keeps wobbling on the death screen. |
| R24-2 | low | Q15 | Open | Typing into a full 256-character chat line deletes the end of the line. |
| R24-3 | low | Q20 | Open | Fire overlay leaves an unburnt gap in the screen centre; alpha 0.9 not 229/255. |
| R24-4 | low | Q15 | Open | Drowning plays the generic hurt sound. |
| R24-5 | low | Q16 | Open | Fire overlay animation freezes while chat or the death screen is open. |
| R20-1 | low | Q15 | Open | A world's "keep inventory", "respawn immediately" and "show death messages" rules are ignored. |
| R20-5 | low (P) | - | Needs permitted evidence/probe | A creative player killed with `/kill` keeps the inventory. |
| R17-2 | med | Q05 | Open | After a swim ends under a 1-block ceiling, you crawl at walking speed (should be sneak speed 0.3). |
| R44-1 | low | Q07 | Needs permitted evidence/probe | The 20-tick fire grace never refills, so separate short touches of fire add up and ignite you at once. |
| R44-2 | low | Q20 | Open | A lava bucket fills a cauldron that has water directly above it. |
| R9-1 | low | Q20 | Open | Eating crumbs fly behind the player in front-facing third person. |
| R34-3 | med (P) | Q13 | Needs permitted evidence/probe | With an axe in the main hand and a shield in the offhand, right-clicking a log strips it instead of raising the shield. |
| K-PLAYER-3 | low | Q16 | Open | The creative inventory screen still pauses the game (furnace ambience stops under it). |
| K-PLAYER-4 | low | - | Open | No screen can be driven by keyboard (Tab/Enter on death-screen buttons etc.). |
| K-PLAYER-5 | med | Q10 | Open | The first-person held item (e.g. an axe) clips into nearby blocks in tight spaces. The fix must keep held items looking the same at every FOV setting (an earlier FOV fix exists; do not regress it). Jesus: "we never fixed the clipping that happens with our game ... The axe will completely clips through ... It needs to also be perfect with FOV". |
| RQ06-1 | low | Q21 | Open | Footsteps at an edge: when the centre cell has a LOWER holding top (farmland 93.75) and the feet rest on the stone beside it, the step plays farmland (centre accepted before the box search). Only the sound choice; nothing goes silent. |
| R38-5 | low | Q05 | Open | Crawling while holding sneak still plays footsteps. |
| R41-1 | low | Q22 | Open | A mob that shifts into another draw slot (a mob removed, the nearest-128 reorder) loses its motion vector for a frame (brief smear). |
| R41-2 | low | Q22 | Open | A teleported mob streaks for one frame (stale previous transform). |
| R41-3 | low | Q22 | Partial fix; residual remains | Break and crumb particles and furnace particles still write no motion vectors and can ghost under TSR; XP orbs are translucent and write none at all (orb ghosting needs its own look). Arrows fixed in SMEAR2. |
| K-PLAYER-6 | low | Q16 | Open | Losing window focus while a survival screen (inventory, furnace) is open doesn't pause as it does in play (carried T1 known issue). |
| RA3-1 | med | Q05 | Open | Chat (T) stops the survival simulation: burning, air, mobs and the authority freeze while chat is open (vanilla chat doesn't pause). |
| RA3-2 | med | Q05 | Open | Falling below the world teleports the player back to spawn with inventory, instead of void damage and death. |
| RA3-3 | low | Q05 | Needs permitted evidence/probe | Starting to eat while sprinting keeps the sprint speed and FOV. |
| RA3-4 | med | Q09 | Open | `/kill @e` kills only the player (plus items and orbs); mobs and stuck arrows survive. |
| RA3-6 | low | Q15 | Open | The on-fire hurt sound has a random pitch (0.8-1.2); vanilla is fixed at 1. |
| RA3-7 | low | Q15 | Open | A milk bucket can be obtained (milking) but not drunk. |
| RA6-2 | low | Q16 | Open | Pausing draws mobs, arrows, items and orbs one tick back for the whole pause (an arrow jumps back). |
| RA6-4 | low | - | Open | "Main Hand: Left" flips the HUD only; held items stay in the right hand. |
| RAX-1 | med (P) | Q16 | Open | Pause stops the player and authority but not world time or fluids (lava keeps spreading behind the pause menu). |

### Items and inventory (18)

| ID | Severity | Batch | State | Remaining symptom/scope |
|---|---|---|---|---|
| R34-1 | med | Q13 | Open | A dropped shield is invisible on the ground (it can still be picked up). |
| R40-1 | low | Q13 | Open | An arrow that hits a mob during its hurt cooldown (or a raised shield) vanishes instead of bouncing back. |
| R40-2 | low | Q20 | Open | Arrow spread never changes arrow speed (renormalised). |
| R40-3 | low | Q13 | Open | A stuck arrow keeps its critical flag; if its block breaks and it falls onto a mob it crits. |
| R40-5 | low | Q13 | Open | An arrow can be picked up the tick after it sticks (no 7-tick shake delay). |
| R40-6 | low | Q20 | Open | Bow zoom lags at low frame rates (FOV sampled once per frame, not per tick). |
| R40-7 | low (P) | - | Needs permitted evidence/probe | A dislodged arrow falls straight down instead of arcing out. |
| K-ITEMS-6 | low | Q20 | Open | Arrows don't inherit the shooter's velocity (provisional, R24 request). |
| K-ITEMS-5 | low | - | Open | With two equal recipes, the project picks "first by id", not vanilla's ordered/cached recipe. |
| RA2-1 | med | Q14 | Open | Armor wear from a hit can land on the replacement piece if armor is swapped that tick (the worn piece returns unbroken). |
| RA2-2 | med (P) | Q14 | Open | Environmental damage right after equipping armor uses the old armor total (lava: 4 HP instead of 3.36 with an iron chestplate). |
| RA2-3 | low | Q14 | Open | An orb about to expire is deleted before a matching new reward can merge into it (XP lost). |
| RA2-4 | low | Q21 | Open | With `foundation.Mobs 0`, mobs already alive stay hittable by arrows (invisible, take damage, drop loot). |
| RA2-5 | low | Q14 | Open | XP from blocks spawns with item-drop scatter/velocity instead of at the block centre with XP velocity. |
| RA2-6 | low | Q14 | Open | XP reward merging doesn't use one random id group (merges more often than vanilla). |
| RA2-7 | low | Q14 | Open | XP orb billboards sit 7.5 cm too low (dip into the floor). |
| RA6-1 | low (P) | Q22 | Open | Dropped translucent blocks (stained glass) may ghost (translucent material writes no velocity). |
| RAX-3 | low | Q13 | Open | A breaking tool, bow, shield or armor piece makes particles but no `entity.item.break` sound. |

### Containers (10)

| ID | Severity | Batch | State | Remaining symptom/scope |
|---|---|---|---|---|
| K-CONT-1 | low | Q12 | Open | A barrel or chest stays open if its last viewer left while its column was unloaded (no 5-tick opener recheck). |
| K-CONT-5 | low | Q12 | Open | A double chest's screen closes by the distance to the opened half only (vanilla checks both halves). |
| R37-1 | low | Q19 | Open | A chest under glowstone (or another opaque but non-conductive block) won't open. |
| K-CONT-2 | med | Q22 | Needs reproduction | Double chests draw as two separate single-chest boxes; no animated lid yet. |
| K-CONT-3 | low | - | Open | Furnace sound timing is a guess (1 tick in 50 per lit block, 16 blocks). |
| K-CONT-4 | low | Q22 | Open | Furnace, smoker and blast-furnace particles are small and faint; the smoker's smoke barely shows at 2 blocks (sizes, grey, drag, rise and frame timing are provisional). |
| R43-1 | low | Q16 | Open | Furnace particles keep moving and fading behind the pause menu. |
| R43-2 | low | Q22 | Open | With all `foundation.Break.*` switches and `foundation.Place.Sound` at 0, furnace particles stop facing the camera (edge-on, invisible from some sides). |
| K-CONT-7 | low | Q22 | Open | Furnace smoke is lit by its own quad's normal, so its shade changes with the view angle. |
| K-CONT-8 | low | - | Open | Chest gaps: spectators never count as viewers, the both-sides pairing tie is provisional, ender chest / trapped-chest redstone / cats on chests not built. |

### Blocks (18)

| ID | Severity | Batch | State | Remaining symptom/scope |
|---|---|---|---|---|
| R33-1 | med | Q12 | Open | A button pressed just before its column unloads stays pressed for good. |
| R16-2 | low | Q22 | Open | Overlapping stained glass, panes and ice blend in a fixed order (wrong colours from one side). |
| R17-3 | low | Q22 | Open | Waterlogged slabs and stairs look dry from far away. |
| R45-1 | med | Q08 | Open | Crops don't grow under a glass roof (glass counts as blocking the sky); monster spawn light may treat glass roofs as dark too. |
| K-BLOCK-3 | low | - | Open | Pressed wall buttons, fence bars and open gates sample their 1 px edges about 1 px off. |
| R39-1 | med | Q07 | Open | Falling into vines (or onto a ladder) at the landing spot still deals full fall damage. |
| R39-2 | med | Q07 | Open | A waterlogged ladder can't be climbed underwater. |
| R36-1 | low | Q19 | Open | Tall grass and large ferns washed away by water never drop seeds when both halves go in one flush. |
| K-BLOCKS-B6a-3 | low | Q19 | Open | Crops in darkness (light below 8 without sky) never pop; vanilla crops break there. |
| RA4-1 | med (P) | Q12 | Open | Farmland drying to dirt under a standing player doesn't push them up; they can sink through. |
| RA4-2 | med | Q07 | Open | Placing a single slab into a water source deletes the water (vanilla waterlogs it). |
| RA4-3 | low (P) | Q19 | Open | Stair placement can be refused as "obstructed" when only the stair's empty upper half overlaps the player. |
| RA4-4 | low | Q19 | Open | Vines can't attach to a stair's full back face. |
| RA4-5 | med | Q08 | Open | Crops get torch light through solid walls (growth light = nearest emitter minus nothing). |
| RA4-6 | low (P) | - | Needs permitted evidence/probe | Random ticks stop beyond 4 chunks of the player (provisional radius). |
| RA4-7 | low (P) | - | Needs permitted evidence/probe | Wall post updates don't propagate down a wall column. |
| RA4-8 | low | Q19 | Open | `foundation.Blocks.Growth 0` still lets wheat seeds plant anywhere (on grass, stone). |
| RA4-10 | low | Q19 | Open | Arrows don't press wooden buttons. |

### Mobs (23)

| ID | Severity | Batch | State | Remaining symptom/scope |
|---|---|---|---|---|
| R30-4 | low | Q21 | Open | An animal can spawn 23.5 blocks away (under the 24 minimum) when grass tops the column. |
| K-MOB-1 | low | Q21 | Open | Chunk packs are rolled once per column per session; pack members pile on chunk edges. |
| K-MOB-4 | low | Q21 | Open | Mob footsteps play about 1.67x too often (a step every 100 cm against vanilla's distance x 0.6 cadence). |
| K-MOB-5 | med | Q11 | Open | Mobs find no path across the Playground floor (the collision query returns tall boxes in open cells); generated worlds are fine. |
| R42-1 | med | Q08 | Open | With natural spawning off, or in the Playground, a spider soon treats a lit room as dark and attacks. |
| R42-2 | med | Q11 | Open | Spiders can't climb a wall to reach a player on a ledge above (ground A* gives up first). |
| R42-3 | low | Q07 | Open | A spider's wall climb doesn't clear its fall damage. |
| K-MOB-6 | low | Q23 | Open | Monster cycle under the cap spikes about 1.5-3.1 ms per step. |
| K-MOB-7 | low | Q23 | Open | A freshly placed torch protects from spawns about 1 s late (emitter light lists 20 steps stale). |
| K-MOB-8 | low | Q21 | Open | Spiders spawn about a third as often as zombies in caves (their 1.4-block box rarely fits). |
| K-MOB-9 | low | Q08 | Open | Monsters may spawn on glass floors (vanilla refuses). |
| K-MOB-10 | low | Q23 | Open | `ForEachSolid` walks every loaded column per call (pre-existing perf). |
| R47-1 | low | Q21 | Open | A spectator can dye or milk a mob (and shear, once shears exist); the dye/bucket is used up. |
| R47-2 | low | Q21 | Open | Sheep colours ignore the biome: cold biomes should be mostly black, warm mostly brown. |
| RA5-1 | med | Q09 | Open | Mobs in a fire block take no damage and don't ignite (only the player gets fire contact). |
| RA5-2 | med | Q11 | Open | Farm animals sink in water and can't swim out (no float). |
| RA5-3 | low | Q11 | Open | Spider paths ignore its 1.4-block width (picks 1-wide gaps it can't fit). |
| RA5-4 | med | Q08 | Open | Torches stop monster spawns through opaque walls (light = emitter minus distance, no walls). |
| RA5-5 | low | Q21 | Open | Mobs use stone friction on ice. |
| RA5-6 | med | Q09 | Open | Sword sweep hits only training targets, never mobs. |
| RA5-7 | low | Q09 | Open | Sprint hits add no extra knockback to mobs but still end the player's sprint. |
| RA6-3 | med | Q09 | Open | Burning mobs show no flames (only the red hurt flash). |
| RAX-2 | low (P) | Q11 | Open | Open doors block mob paths (thin full-height box read as solid). |

### World and atmosphere (1)

| ID | Severity | Batch | State | Remaining symptom/scope |
|---|---|---|---|---|
| K-WORLD-2 | med | Q17 | Open | The voxel world's feature reader crashes now and then on its parallel JSON read, under machine load (seen twice: B2a's checks, and `operator new` in Unnamed-inv's `VoxelAutomation-20261015-145649\Crashes`). |

## Deferred and post-QC rows (18)

These stay visible without treating unbuilt features as completed bug fixes.

| ID | Severity | State | Remaining scope |
|---|---|---|---|
| R5-1 | med | Deferred / not built | Axe stripping works in Adventure mode. |
| K-PLAYER-8 | low | After QC | The Music & Sounds category sliders (Music, Blocks, Hostile, Players, ...) are saved but never applied in game; only Master Volume and Bass work (13a's plan: "the other sliders wait"). Wiring them needs per-category sound classes, new work past the freeze. |
| K-ITEMS-1 | low | Deferred / not built | Copper armor equips silently. |
| K-ITEMS-2 | low | Deferred / not built | Leather armor can't be dyed; dropped two-layer items draw untinted. |
| K-ITEMS-3 | low | Deferred / not built | Turtle helmet water breathing, enchantments and netherite knockback resistance do nothing. |
| K-ITEMS-4 | low | Deferred / not built | 48 cooking items and 114 fuels from vanilla are missing (no recipe for them). |
| K-CONT-6 | low | After QC | Chest lid animation deferred until after QC; ender chest, trapped-chest redstone and the cat-on-top rule are out of scope. |
| R18-2 | low | Deferred / not built | (Multiplayer only) items moved through a shared furnace keep the giver's instance id. |
| RA1-6 | low (P) | Deferred / not built | The Saved Hotbars creative tab shows but saves/loads nothing. |
| R12-a | med | Deferred / not built | Adventure mode can still place blocks and use most blocks (no mode in block use). |
| K-BLOCKS-B6a-1 | low | Deferred / not built | A grown pumpkin or melon stem keeps fruiting into its other free sides (no attached stem: the block table lacks attached_pumpkin_stem and attached_melon_stem); the torchflower crop stops at age 1 (no torchflower block). |
| K-BLOCKS-B6a-2 | low | Deferred / not built | Only wheat_seeds plants; carrot and potato never plant (eat only), and beetroot_seeds, pumpkin_seeds, melon_seeds, torchflower_seeds and pitcher_pod do not exist as items. |
| RA4-9 | low | Deferred / not built | Iron doors/trapdoors and pressure plates are placeable but nothing powers them (no redstone). |
| R30-3 | low | Deferred / not built | (Multiplayer only) a pack counts toward the wrong player's local cap. |
| K-MOB-2 | low | Deferred / not built | Grazing and other missing sounds are silent; no grazing particles. |
| K-MOB-11 | low | Deferred / not built | Dyeing a sheep is silent. |
| R47-3 | low | Deferred / not built | Cold and warm chickens lay plain eggs (vanilla: blue / brown eggs; neither item exists yet). |
| K-WORLD-1 | med | After QC | At dusk and dawn, water stays dark beside lit ground. |

Additional feature scope outside those bug-row counts:

- World saving: only world.json settings persist; first after QC.
- Parked day/night look, square sun/moon and dusk-water visual work.
- Beds/sleeping, full block loot, remaining crop/plant growth and planting, shears/missing dyes, creeper/skeleton gameplay not built under freeze.
- Adventure mode, survival pick-block, view bobbing, second sound batch, item extras, new body/full character and beyond-vanilla AI deferred.

## Historical entries outside the 156 rows

Thirteen additional named IDs: 10 recorded fixes, 1 delivered feature, 1 workaround and 1 partial-then-linked fix history. Earlier omitted run-1 fixes have no invented count. These were checked against existing records, not retested now.

| ID | Recorded result | Landing | Original issue / evidence limit |
|---|---|---|---|
| R31-1 | historical recorded fixed | 34 | Tests outside automation guards broke Shipping/game targets. Review34 verifies the guard fix and new testguards check; editor/full gate is not itself a Shipping package run. |
| R31-2 | historical recorded fixed | 34 | Pumpkin blur ignored the full-inventory gate. |
| R31-3 | historical recorded fixed | 34 | XP orb reward merging used the wrong previous position. |
| R31-4 | historical recorded fixed | 34 | Netherite item fire wear was incorrect. |
| K-PLAYER-1 | historical recorded fixed | 34 | New survival players received a starting loadout. |
| K-BLOCK-1 | historical recorded fixed | 36 | Popped sugar-cane segments did not each drop an item. |
| K-BLOCK-2 | historical workaround | 36 | Firefly bushes rendered black. Original black rendering remains unfixed; default workaround generates short grass and hides firefly bushes. Switch 0 brings back the original issue. |
| K-PLAYER-7 | historical recorded fixed | 41 | Moving mob parts/item drops smeared. Mob motion vectors and motion blur setting corrected the original report; R41-1/R41-2/R41-3 remain separate residual cases. |
| R30-1 | historical recorded fixed | 42 | Global spawn cap counted every loaded creature. Historical merge gate skipped build; recorded GREEN is not a fresh compile in that invocation. |
| R30-2 | historical recorded fixed | 42 | Frozen mobs skipped the local spawn-cap census. Historical merge gate skipped build; recorded GREEN is not a fresh compile in that invocation. |
| K-MOB-3 | historical delivered feature | 42 | Zombie, spider, night spawning, despawn and Peaceful behavior were not yet delivered. Feature delivery recorded separately from bug-fix totals; residual mob bugs remain in main rows. |
| R15-5 | historical recorded fixed | 45 | Stem tint used age 0 at every age. |
| R41-4 | historical partial then linked edge fix | 46 | Smear probe passed without valid drawn subjects. Normal case fixed in SMEAR2; empty-model edge became main row R46-1 and later landed in Q03. Keep the partial original closure and linked later evidence visible; no new unresolved duplicate row is added. R46-1 final faceless-pivot adjustment was not re-mutated. |

**R41-3 stays open in the main table:** arrows were corrected, while particle/XP residuals remain. **K-BLOCK-2 is a workaround:** the original black firefly-bush renderer is not fixed. R41-4 links to R46-1 for the later empty-model edge; this is not an extra new closure. Historical human acceptance was not established by this audit.

## Evidence and acceptance limits

- R37-2 has static root-cause proof and a green tour/gate, without an executed failing reproduction.
- R46-1 had mutation RED before its last faceless-pivot adjustment; that final adjustment was not re-mutated.
- Q04's original outside-click bug had RED/GREEN; its refused-throw follow-up guard has static proof only.
- Q06 proves audio setting wiring, not what a physical audio device sounds like. Mixed-height footsteps remain RQ06-1; the support tie order is provisional.
- Q24 recorded packaged startup, night light and daylight-burning proof; the whole packaged views suite was not green. RQ24-1/2 and the two final packaged acceptance sessions remain.
- LOW01's complete merge gate was GREEN with native 581/581, voxel 211, survival hashes 9/9, runtime 195/150/51, create-world 38/38, npm 85/85 and seven reference verifiers. One network timeout warning remained; no tests failed or were skipped.
- LOW02's quick gate covers 6/6 selected hashes, not the full 9/9 merge gate. LOW03 has no clean committed quick or final review.

## Source records

- [Canonical tracker](../QC-KNOWN-BUGS.md)
- [Landing ledger](../LANDINGS.md)
- [Human replay checklist](../play-checklist.md)
- [Independent inventory audit](../codex/reviews/qc-CODEX-LOW-02/status-audit.md) and [machine-readable inventory](../codex/reviews/qc-CODEX-LOW-02/completion-inventory.json), preserved as the 13:34 snapshot
- [LOW01 report](batches/CODEX-LOW-01.md), [LOW02 report](batches/CODEX-LOW-02.md), [LOW03 report](batches/CODEX-LOW-03.md)
- [LOW03 incomplete review checkpoint](../codex/reviews/qc-CODEX-LOW-03/STOP-CHECKPOINT.md)

**STOPPED. No automatic continuation.**
