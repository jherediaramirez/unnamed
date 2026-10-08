# Dummy Block interaction foundation

Project: `C:\dev\projects\Unnamed\Unnamed.uproject`, Unreal 5.8.3. Follows the [player vitals](2026-10-07-Vitals.md) slice, whose health/recovery owner it reuses. Handoff: `C:\dev\output\minecraft-unreal-interaction-foundation-2026-10-07` (implementation prompt, plan, research, tuning baseline, source index).

A reusable, session-only interaction loop in a new **Interaction Lab** world: place and break a 100 cm Dummy Block, tell items apart by capabilities, and fight a separate training target. Placement while jumping and falling was the priority. Everything is standalone and single-player through a local authority boundary; nothing is saved or networked.

Pre-edit snapshot (HEAD, status, diffs, Git index, 16,222 file hashes, untracked HUD/vitals sources and assets): `C:\dev\backups\Unnamed\interaction-20261007-233803`. No commit, staging, reset or clean was part of this work.

## Play

Singleplayer → **Interaction Lab**, or `Scripts\LaunchFoundation.ps1 -InteractionLab`. The Foundation and Super Flat worlds are unchanged.

| Input (project defaults) | Interaction Lab |
|---|---|
| 1-9, wheel | Select a hotbar slot: 1 Dummy Block (unlimited), 2 practice pickaxe, 3 practice sword, 4-9 empty hand |
| Right mouse | Use: the target's own interaction, else the held item (place), else the offhand. Hold repeats |
| Left mouse | Press: attack the target in reach, mine the block in reach, or swing at nothing. Hold keeps mining; it never repeats attacks |
| F | Swap the selected stack with the offhand |
| Middle mouse | Pick: select the slot holding the targeted block's item |
| Q | Refused: dropped items do not exist in this lab |
| C, Space | Crouch (edge protection, bypasses block interactions while holding something), jump |
| F3, F3+F6 | Interaction lines in the overlay; lab controls (preset, supply, reset) and the existing player-vitals panel |

Stations (signs in the world): starter blocks east of spawn; **RESET LAB**, **SUPPLY** and **PLACEMENT PRESET** controls 3.5 blocks west of spawn (use them; crouch-use with blocks places on them instead); a jump-stack pad north; a 2.8 m low ceiling north-east; a three-block ledge west with steps; "break underfoot" blocks; an occluding wall in front of a block; a 2-block-deep water pool with a block in it; the **training target** south and a second target behind a wall.

## Architecture

All in the Foundation area (`Source/Unnamed/Foundation/`), pure structs where rules live:

| Piece | File | Owns |
|---|---|---|
| Grid | `FoundationGrid.h` | Faces, floor-division cells, the one 100 cm conversion, 16³ sections |
| Blocks | `FoundationBlocks.*` | `FFoundationBlockDefinition` (data), state + cell revision, `IFoundationBlockStorage` with the bounded sparse `FFoundationSectionStorage`, `FFoundationBlockWorld` (bounds, world revision, change log, collision gather) |
| Items | `FoundationItems.*` | `FFoundationItemDefinition` + ordered mining rules (data), item instances (identity, count, damage, revision), the hotbar/offhand inventory |
| Targeting | `FoundationTargeting.*` | One resolver: exact grid traversal, static anchors/occluders, entities; kind, cell/static/entity identity, face, point, distance, observed revision |
| Authority | `FoundationAuthority.*` | Commands and typed results, retransmission cache, sessions, placement validation and commit, mining sessions, melee, durability, finite supply, training targets, lab reset, events |
| Interaction | `FoundationInteraction.*` | The player's intent: held state, fresh presses, mining continuation and post-break delay, use repeat, the self-clearance retry, command sequences, the per-frame preview |
| Settings | `FoundationInteractionSettings.*` | Presets, block/item definitions, the empty hand, loadout, lab rules (Project Settings > Game > Unnamed Interaction) |
| Lab | `FoundationInteractionLab.*` | The map's layout, block world and authority; the collision provider, target adapter, safe spawn and rebasing |
| Presentation | `FoundationInteractionView.*` | Instanced cubes per section and block (stable instance IDs), outline, preview, cracks, effects, target labels, held item |
| Tags | `FoundationTags.*` | Native gameplay tags (`Block.Mineable.Pickaxe`, `Item.*`) |

Integration: `AFoundationLab` gained virtual seams (collision gather, target trace, logical offset, safe spawn, authority); `AFoundationPawn` runs the interaction per reference step; `AFoundationController` routes attack/use/hotbar/wheel/pick/drop/swap to it in the Interaction Lab and keeps the token fixture (and its repeat and recovery callbacks) only in the Foundation lab; the HUD presenter shows real items; F3 and F3+F6 gained interaction lines and controls. `FFoundationVitals` gained a `PlayerAttack` damage kind on the existing `Hurt` path, the attack's half-tick strength sample and a cooldown-only tick for the target.

### Order inside one reference step

Each due 50 ms step: lab-session check → movement against the committed collision (lab cuboids plus blocks near the player) → vitals (one tick; death and eligibility come first) → interaction against the resulting pose, whose commits reach the collision provider before the next movement step. A fresh press acts only on the latest step of an engine update, and a hold that began with a still-pending press does not act on that update's earlier catch-up steps. Holds established earlier continue through every step. At most one placement commits per update; nothing is replayed, and time the movement loop discards gets no actions.

### Commands

A command carries the session, a per-player sequence, type, hand, the hand's item instance and revision, and the caller's target, destination and revisions. The authority resolves and validates everything again from its own facts (the pawn's gameplay eye and aim, body, movement and water state); claims are never trusted. Results are **pass**, **committed**, **rejected** or **canceled** with a typed reason, the world revision and the actual effects. A retransmitted sequence returns the stored result without applying anything; the cache keeps 128 results per player, an evicted sequence is refused, and a lab reset starts a new session so old commands are stale. Work is synchronous, so nothing is pending.

### Presets

| Preset | Held repeat | Self-clearance retry | Cadence |
|---|---|---|---|
| VanillaReference (default) | 0.20 s after every attempt | none | fresh presses do not wait |
| LucidFluid (experimental) | 0.10 s between commits; a rejection does not buy a new wait | one candidate, 0.10 s fixed expiry, rechecked every step while use stays held | fresh presses share the commit cadence |

The retry is captured only when the player's own body is the sole obstruction. It cancels on release (Hold) or toggle-off (Toggle), target or revision change, slot/hand/item change, mining, UI, focus loss, rebinding, possession, death or a lab reset, and never extends itself.

## Rules

- Reach: 450 cm blocks, 300 cm entities, strict `<`, from the gameplay eye along the current aim; third-person offset, hand and FOV never extend it. The body is excluded from targeting and included in obstruction.
- Grid traversal ties: at an exact edge or corner the traversal steps X before Y before Z, and a box entered exactly at an edge reports the X face, then Y. A grid block wins an exact tie with lab geometry; an entity must be strictly nearer. A ray starting inside a box does not hit it; a miss places nothing.
- Destination: the clicked cell when its block is replaceable, otherwise the clicked cell plus the face offset. Only designated, grid-aligned lab surfaces are anchors; lab geometry is never mined or replaced. Validation before commit: alive, same hand item and revision, reach and line of sight, anchor and destination revisions, build bounds, empty or replaceable destination, the survival rule, supply, lab geometry, other bodies and targets, then the player's own body. Contact is allowed; positive overlap (the movement solver's 1e-6 cm tolerance) is not. Full cubes need no support.
- Mining: `progress += speed / hardness / (eligible ? 30 : 100)` per step; speed is the first matching tool rule or the default, then efficiency (above 1), haste, fatigue and break speed hooks, ×0.2 with eyes in water, ×0.2 off the ground. Hardness 0 (or a step of 1 or more) breaks at once without the delay; negative never breaks. A break completes within 1e-6 of 1, so the nominal step counts are exact: Dummy by hand 45 steps (2.25 s), practice pickaxe 8 (0.40 s). A fresh click accumulates in its own step; a held continuation onto a new block starts one step later (as the reference does); a progressive break is followed by a 5-step (0.25 s) delay. Face changes keep progress; another cell, a new block revision, another stack or revision, release, aiming away or any cancel restarts it, and an abandoned break restarts attack recovery.
- Melee: charge `clamp((ticks + 0.5) / (20 / attacks per second), 0, 1)` from the vitals' own attack ticker; damage `base × (0.2 + 0.8 × charge²)`; a critical (×1.5) needs charge > 0.9, fall distance > 0, airborne, not sprinting/in water/climbing/riding/restricted, and a living target. Every issued attack and every whiff restarts recovery; a different main-hand item type restarts it through the vitals' equipment seam; held mining never becomes an attack. Accepted hits cost the attacker 0.1 exhaustion. The target's damage cooldown is the existing `Hurt` rule: 20 ticks; above 10 remaining only a stronger hit applies, and only its excess.
- Items (total values): empty hand and Dummy Block 1 damage, 4/s, speed 1; practice pickaxe 4, 1.2/s, speed 6 on `Block.Mineable.Pickaxe`, correct for drops; practice sword 6, 1.6/s, speed 1. Tools have 250 durability; with wear on, the pickaxe costs 1 per positive-hardness block and 2 per accepted hit, the sword 2 and 1. The last point breaks the tool once and empties its slot.
- Supply: the lab default is unlimited Dummy Blocks without tool wear; the finite fixture (64 blocks, wear on) uses the identical commit path. A finite break returns its item straight to the inventory (a lab stand-in for item entities) and is refused, block intact, when there is no room.

## Decisions and deviations

- Java's shared 10-tick miss throttle is not copied; attack recovery, mining and placement keep separate timers.
- Recovery resets on an item *type* change, as the reference's same-item check does; another stack of the same item or wear does not reset it.
- `FFoundationVitals::GetAttackStrength()` is the HUD sample; attacks use the half-tick sample.
- The training target reuses the player's health owner and `Hurt` path but only its damage cooldown ticks (no food, air or regeneration); it stays down until a lab reset. The player keeps the vitals milestone's automatic respawn, now at a safe spawn above any blocks built there.
- Dropping is refused (no item entities); pick block selects a hotbar slot only.
- The block item is `dummy_block_item` (items and blocks are separate registries); the handoff's return ID `dummy_block` is read as this item.
- No sounds: the project has no approved gameplay audio. Feedback is visual.

## Defects found and fixed

- **Looking fully down flipped the view up** (pre-existing in the Foundation): mouse look clamped pitch to ±90°, but the engine camera manager limits it to ±89.9° and turns -90° into +89.9°. Mouse look now clamps to the camera manager's limits; a runtime check drives a large downward mouse move and requires the view to stay down. This is what jump-stacking with a mouse needs.
- During development: a tap arriving during a hitch reached back to the update's first catch-up step through the held path. Holds now start at their press's step (native and runtime regression checks).
- **The first use after a respawn could stall** (found by the full LucidFluid run, whose worst press-to-commit was 500 ms): respawning or falling out of the lab restarts the movement step count, which the interaction's cadence and the authority's one-placement-per-step limit used as their clock. With LucidFluid's commit cadence, a placement made late before death held back every use after the respawn until the restarted count caught up (up to minutes). The pawn now feeds a step count that never restarts (`AFoundationPawn::ReferenceSteps`). A runtime check places, dies, respawns and must place again at once; it fails without the fix (`placed at step 33 after 33; movement restarted at 7 steps`) and passes with it.
- **Closing the game window during play crashed** (pre-existing; found by the OS-level input session, reproduced on `VitalsDevelopment20261007-r2` in the Foundation course). Destroying the window deactivates the application; the controller's focus-loss handler then opened the pause menu, whose browser host dereferenced the already-unregistered game viewport (`Assertion failed: IsValid()`, exit code 3). Focus loss now opens the menu only while the game viewport exists and no exit is requested. Window-close checks on the editor game and the candidate exit with code 0 in both labs.
- Hardening, no measured effect: break particles and floating damage numbers were registered as components on first use; the view now creates both fixed pools when the lab starts. A suspected first-break hitch turned out to be ordinary frame-time noise (single 5-11 ms frames that appear in unrelated phases at some caps and not others, before and after the change).

## Adding another ordinary block (data only)

Definitions are data in `UFoundationInteractionSettings` (Project Settings > Game > Unnamed Interaction; stored in `[/Script/Unnamed.FoundationInteractionSettings]` in `Config/DefaultGame.ini`). Add a block definition and an item that places it, give a hotbar slot, and use tags for tool rules; no interaction code changes:

```ini
[/Script/Unnamed.FoundationInteractionSettings]
; Listing an array replaces the C++ defaults, so the existing entries are listed too (Project Settings writes them).
+Blocks=(Id="dummy_block",DisplayName="Dummy Block",Hardness=1.5,Tags=(GameplayTags=((TagName="Block.Mineable.Pickaxe"))),DropItem="dummy_block_item")
+Blocks=(Id="dummy_brick",DisplayName="Dummy Brick",Hardness=3,bRequiresCorrectToolForDrops=True,Tags=(GameplayTags=((TagName="Block.Mineable.Pickaxe"))),DropItem="dummy_brick_item",Color=(R=0.8,G=0.45,B=0.35,A=1))
+Items=(Id="dummy_brick_item",DisplayName="Dummy Brick",MaxStack=64,PlacesBlock="dummy_brick",Icon="block",IconColor=(R=0.8,G=0.45,B=0.35,A=1),Tags=(GameplayTags=((TagName="Item.Block"))))
; ...plus the existing three items, then:
+Loadout="dummy_block_item"
+Loadout="practice_pickaxe"
+Loadout="practice_sword"
+Loadout="dummy_brick_item"
```

A new tag goes in `Config/DefaultGameplayTags.ini`. `Unnamed.Foundation.Interaction.DataDriven` builds exactly such a brick through the same code: it places, takes 15 pickaxe steps (6/3/30) and returns its own item.

## Verification

Evidence: `Tests/Evidence/2026-10-08/Interaction-20261008/`.

Probe input below is application-level Enhanced Input injection through the real router and controller; the OS-level sessions are separate. All runs are on the delivered candidate unless named otherwise.

| Check | Result | Evidence |
|---|---|---|
| Editor build (UnnamedEditor Development), formatting | Succeeded with no warnings; no `.clang-format` differences in changed C++ | - |
| Native automation, all `Unnamed.*` | 37/37, including 9 new `Unnamed.Foundation.Interaction.*` suites | `Native-Final-r3/` |
| Node units, reference and asset check | 53/53; 938 assets and the reference hash unchanged | `npm-test.log`, `source-check.log` |
| Interaction Lab, VanillaReference | 61/61 at 30/60/120/144 FPS caps (29.1/57.7/113.6/135.7 FPS) | `Package-r3-Interaction/` |
| 15 FPS stress cap | 61/61 (14.7 FPS) | `Package-r3-Interaction-15fps/` |
| Full LucidFluid run | 61/61 at 30/60/120/144 | `Package-r3-Interaction-LucidFluid/` |
| Foundation regression with HUD captures | 74/74 at 30/60/120; `VitalsDevelopment20261007-r2` on the same settings 74/74 | `Package-r3-Foundation/`, `Baseline-VitalsR2-Foundation/` |
| CEF and native menu: capture, relaunch persistence, travel to both labs | 33/33, normal exits | `Package-r3-Menu/results.json` |
| Window close (title-bar X) during play | Exit code 0 in both labs; `VitalsDevelopment20261007-r2` exits with code 3 (assertion) | `WindowClose-r3-*`, `WindowClose-Baseline-VitalsR2-Foundation/` |
| OS-level mouse and keyboard play (Win32 `SendInput`) | Place, jump-stack, mine down, sword hit, whiff, falling critical, strict reach, preset station, falling placement A/B (r2, 28 captures); place, mine, clean close (r3) | `OsInput-r2/README.md`, `OsInput-r3/README.md` |
| Packaging | 738 packages (731 cooked, 7 skipped by platform), 0 errors, 0 warnings; `Content/` (1,176 files) hash-identical before and after | `build-package-r3.log`, `content-hashes-*-r3.json` |
| Editor game (`-game`) | Interaction 61/61 with each preset at 60 FPS and with VanillaReference at 15 FPS | `Editor-Interaction-*` |

The Interaction Lab scenarios cover multiple movement substeps per update, repeated jump-stacking with each preset and with real 150 ms stalls, placing while falling and landing on the new block, a 200 ms injected hitch (the press commits on the latest of 4-5 catch-up steps), cancelling before clearance, a crouched ledge build, the one-step low ceiling, breaking the block underfoot, hand, pickaxe and underwater mining step counts, sword damage, the target's damage cooldown, falling and rising criticals, a wall in the way, the reset station, the finite fixture with consumption, return and tool breakage, pick block, the refused drop, offhand fallback, Toggle use, damage, death and respawn (including the respawn clock), focus loss, and the cost cases below. `FinalResults.json` indexes every run; `changed-files.json` lists every authored file this work added or changed.

### Preset comparison (LucidFluid run after the reference)

| Measure | VanillaReference | LucidFluid |
|---|---|---|
| Jump-stack, use and jump held 4 s | 8 blocks; first after 5-6 steps (8 at 15 FPS) | 8 blocks; first after 3-4 steps (5 at 15 FPS) |
| One-step low-ceiling window | Placed at 30-144 FPS, missed at 15 FPS; the native deterministic case misses | Placed at every cap (self-clearance retry) |
| Held use while walking off a 5-block tower (OS input) | No placement (the window is 2 ticks, the repeat 4); fell to the floor, 2 damage | Placed mid-fall and landed on it, no damage |
| Held use toward the player's own body (60 FPS) | 6 attempts, 3 placed | 8 attempts, 3 placed, 5 self-clearance retries started, none committed (the body never cleared) |
| Fresh press to commit | Within the press's 50 ms step plus frame alignment | Same, except a press inside the 0.1 s commit cadence or waiting on a retry (worst 100-132 ms) |

Both presets build at the same rate because the jump cadence, not the use cadence, bounds jump-stacking. LucidFluid's gains are in the short windows: a body that clears the cell for one or two steps.

## Performance

Machine: Intel Core i9-14900KF (24 cores, 32 threads), NVIDIA GeForce RTX 4070 Ti SUPER (driver 32.0.16.1088), 63.8 GB RAM, Windows 11 Home 25H2 (10.0.26200), 2560×1440 desktop. Probe runs use the Development package at 1280×720 offscreen with reduced rendering cost (GI, reflections, shadows and AA off, 50% screen percentage), as every earlier probe did.

| Measure | Result | Budget |
|---|---|---|
| Interaction logic per engine update (collision gather and interaction steps) | p50 0.006-0.014 ms, p95 0.016-0.028 ms, p99 at most 0.042 ms, max at most 0.20 ms (15-144 FPS caps) | p95 0.5 ms |
| 4,096 cubes through the authority's command path | 4.2-4.5 ms in total (about 1.1 µs per placement); 4,109 instances in 7 section meshes; world storage 460,784 bytes; process memory +0.4 MB | 4,096 cubes |
| Idle with 4,096 cubes, against the seeded lab (60 FPS) | Game thread 1.42 vs 1.37 ms, render thread 5.3 vs 4.6 ms, GPU 4.2 vs 3.0 ms; frame p99 17.0 vs 16.8 ms | - |
| 1,000 place/break cycles | 1.9-2.0 ms in total; memory +0.15-0.18 MB; the result cache stays at 128 per player | 1,000 cycles |
| Whole-frame p99, Foundation course, before and after (CSV profiler, every frame, 60 FPS cap, three runs each) | `VitalsDevelopment20261007-r2` 18.13 ms, r3 18.17 ms (+0.2%, runs vary by ±0.2 ms); game thread p50 1.63 ms for both | Investigate above 5% |

Steady Foundation phases (no captures or menu) before and after: median game thread 1.41/1.34/1.31 ms against 1.40/1.32/1.30 ms at 30/60/120 FPS, frame p95 unchanged, mean frame rate identical. The probe's own PNG captures add 120-150 ms to the following frame and opening or closing the pause menu in the focus cases 70-90 ms; both predate this work and are left out of these conclusions. CPU cost is far under budget, so no GPU work was undertaken. Evidence: `FinalResults.json`, `FrameP99-BeforeAfter/frame-percentiles.json`.

## Delivered candidate

`C:\dev\projects\Unnamed\Packages\InteractionDevelopment20261008-r3\Windows\Unnamed.exe` (Development, Win64; 1,060 files, 1,426 MiB).

```powershell
& 'C:\dev\projects\Unnamed\Scripts\LaunchFoundation.ps1' -PackageName InteractionDevelopment20261008-r3 -InteractionLab
```

Without `-InteractionLab` it opens the menu: Singleplayer → Interaction Lab.

| File | SHA-256 |
|---|---|
| `Windows\Unnamed.exe` (launcher, 171,520 bytes) | `3731FB8F33C0060E14BE728C388133C5F757A4705CFDC1AB173F2BDF14786D4F` |
| `Windows\Unnamed\Binaries\Win64\Unnamed.exe` (game, 333,804,544 bytes) | `91573D29184DF4694DABC706069431ED20F24EF07F97687E58E6E19507BACB57` |

Built with `Scripts\BuildFoundation.ps1 -Package -PackageName InteractionDevelopment20261008-r3`. The intermediate builds `InteractionDevelopment20261008` and `-r2` are kept: r2 added the respawn clock fix, r3 the window-close fix and effect pools. Data and configuration: `UFoundationInteractionSettings` (defaults in `FoundationInteractionSettings.cpp`, overrides in `Config/DefaultGame.ini` under `[/Script/Unnamed.FoundationInteractionSettings]`), the `interaction-lab` world in `UnnamedProjectSettings.cpp`, the map in `MapsToCook`, the lab layout in `FoundationInteractionLab.cpp`.

## Remaining gaps

- **Human acceptance.** No person has played this build. OS-level input was synthesized (Win32 `SendInput` through the normal Windows, Slate and Enhanced Input path); physical devices, the feel of jump-stacking, the choice between presets and mouse feel still need a human pass.
- **Persistence.** Blocks, inventory, wear and target health last for the session only; leaving the world or resetting the lab discards them.
- **Networking.** Commands, sequences, sessions and the result cache form a local, synchronous authority boundary. There is no transport, prediction or reconciliation, and nothing networked was tested.
- **Content scope.** One block, three practice items and two static training targets. No item entities (drop is refused; finite breaks return items straight to the inventory), knockback, sweeping, shields or mobs; no block states or non-cube shapes; efficiency, haste and fatigue hooks are zero.
- **Presentation.** Original placeholder art (tinted cubes, line outline and cracks, primitive held items, no arm) and no sounds, since the project has no approved gameplay audio.
- **Reference behaviour kept.** VanillaReference keeps the reference's 4-tick held repeat, so held use can miss one- or two-step windows (a low ceiling at 15 FPS, a 5-block fall) that LucidFluid catches. This is the presets' intended difference, not a defect.
- **Platforms.** Windows Development build only; Shipping, other platforms and gamepads are untested.
- **Unchanged, noted.** The Foundation lab's token fixture still times its held use and wheel pulses on the movement step count, which restarts on respawn; the Interaction Lab no longer depends on it.
