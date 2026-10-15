# Use step 16a: lava, burning, the swim pose, fall rounding, Codex review 13 (record)

Lane `feature/survival-use` (worktree `C:\dev\projects\Unnamed-use`), survival track T3. Base: the step-17 landing (9efa2253) plus feature/survival 4b826e4 merged once (ffc0974f). Step 16b (pick block, Codex review 9, the bucket held-repeat test) is a later chat.

## 1. What was built

- **(a) Lava and burning, `foundation.Fire` (1; 0 = lava does no damage).** `FFoundationVitals::Step` reads a new fact, `bBodyInLava` (the pawn sets it from `Lab->GetFluid(Body) == Lava`, so it needs `foundation.Swim 1`). In order: water puts a fire out (`Extinguishes`); a burning player outside lava takes `OnFire` 1 when `FireTicks % 20 == 0`, then `FireTicks--`; in lava `FireTicks = max(FireTicks, 300)` and `Lava` 4. The existing damage cooldown turns lava's every-tick 4 into hits at ticks 1, 11, 21 (`hurt.lava_contact`, `hurt.lava_ignite`, `hurt.burning`, `hurt.cooldown`). Creative and Spectator neither ignite nor take the damage. Damage types from the 26.3 jar's data: `lava` (armor applies, exhaustion 0.1) and `on_fire` (in `bypasses_armor`, exhaustion 0); death messages "Player tried to swim in lava" and "Player burned to death" (T4 step 11 shows them in chat). Counters `LavaHurts`, `BurnHurts`, `Extinguishes` survive respawn; `UFoundationInteractionView::PlayFireSounds` plays `entity.player.hurt_on_fire`, `entity.generic.burn` and `entity.generic.extinguish_fire` from them (wiki volumes and pitches, provisional). `Describe()` adds `| fire N` while burning.
- **(b) The swim pose, `foundation.SwimPose` (1; 0 = the standing or crouching box in water).** `FFoundationSimulation::bSwimming`: last step's sprint with the eyes in water starts it, sprinting with the body in water keeps it (26.3 updateSwimming, lava excluded). `bLowPose` is the 0.6 x 0.6 box with the eyes at 0.4 (`player.pose_dimensions`; `UFoundationProfile::SwimmingHeightCm` 60, `SwimmingEyesCm` 40); after the swim it stays until the standing box fits, else the crouching one. `GetPoseEyesCm` replaces the two crouch-or-stand eye picks (`FoundationLab.cpp` fluid facts, `AFoundationPawn::GetEyePosition`). The eye moves with the crouch's smoothing. No arm or body animation.
- **(c) Fall rounding:** a test pins `fall.damage_thresholds` (3.5 and 3.9 blocks give 0, 4.0 gives 1); no code change.
- **Codex review 13 #1 (a waterlogged slab or stair was dry):** `FFoundationBlockRegistry::GetFluid` gives any `waterlogged=true` state a water source (DA_Blocks carries water only for model states). The voxel table sets `HeldFluid` for every state holding water, not only models, so swimming (`GetFluidBox`), the water fog and the far mesher see it; the near mesher draws held water around a slab (a lambda shared with the fluid and model cases; faces the slab itself covers are skipped; the top draws per cell). `FoundationBlockMeshes::HoldsFluid`/`PackFluid` and the Playground view (`AddInstance`) do the same. The Playground probe's expected instance count (`CountExpectedBlockInstances`) adds that water instance (the seed's waterlogged gallery states drew 507 more).
- **Codex review 13 #2 (no pour into a torch's cell):** the pour also replaces blocks tagged `Block.WashedAwayByFluids` (`minecraft:washed_away_by_fluids`). A replaced block pops its drop as item entities (`RollBlockDrops`, extracted from `BreakBlock`, rolled with an empty hand; minecraft.wiki "Water Bucket"). With item entities off it goes without one, as before. This also closes step 17's known issue "a plant a pour replaces gives no drop" (review 13 #4) whenever item entities are on.
- **Codex review 13 #3 (a double slab took water):** `TakesWater` excludes `type=double` (minecraft.wiki "Slab"); the water goes beside it.
- `CLAUDE.md`: `Reference/minecraft-entities-26.3/` and `Reference/minecraft-sounds-26.3/` join the hash-checked list (Jesus's tap, 2026-10-15).

## 2. Checks

- New tests: `Unnamed.Foundation.Vitals.Fire`, `Unnamed.Foundation.SwimPose`, `Unnamed.Foundation.Items.BucketReplace`, `Unnamed.Voxel.Mesher.WaterloggedSlab`; two fall thresholds in `Unnamed.Foundation.Vitals.Movement`.
- Hashes and goldens: the radius-8 hashes 9 of 9 equal and the voxel suite (with `Mesher.Digest`) green after #1: `Tests/Evidence/2026-10-15/Checks-final-20261015-015701/summary.txt`.
- Mutations (`C:\dev\sandbox\use-lane\mutate16.ps1`, runs under `C:\dev\sandbox\use-lane\runs\mut-*`): lava cadence, ignite 300, water extinguish, the stand-up check, the double-slab exclusion, the washed-away replacement, the slab's water mesh, the waterlogged registry fluid.
- The final run and end-to-end checks: see report A.

- Pinned count moved (manager's call A, 2026-10-15): the plant-survival matrix's lily pad difference from the 26.3 table (`Unnamed.Foundation.Blocks.Survival`, S8 "a lily pad pops on a water source held in a block") went 1,521 to 0 with #1; source: `Tests/Evidence/2026-10-15/Native-20261015-020856`. The pin and `Docs/Blocks.md` say so.
- Sonnet 5.5 pre-commit review (diff only, 1,044 lines, peak 90.8k): no high findings; seven lows, no code changes, listed under known issues (slab water sides, GetFluid lookup, pops from replaceable blocks, lava replacing too, creative pops) or accepted (the burn cadence after leaving lava is tested only from a fresh 300; the 3.5/3.9 fall asserts pin values, the switch gates live in the pawn and are untested natively).

## 3. Known issues
- Unchecked regions: waterlogged leaves (and every other waterlogged non-model state) now hold water for decoration and plant survival, so swamp and mangrove areas may generate differently outside the 9 hashed regions (a vanilla-ward change; the 9 hashes and `Mesher.Digest` held).

- Rain doesn't put out burning (settled; when: a weather-at-the-player query).
- Fire-block and soul-fire contact damage, buckets on cauldrons (tail step 16c).
- The first-person fire overlay (T4 step 11).
- Crawling (the low box under a 1-block gap without water) only as the leftover of a swim; lava halving the fall distance (not done).
- The fluid simulation still marks a double slab waterloggable (`VoxelFluidSystem::MakeCell`'s `bWaterloggable`); flowing water never enters a waterloggable cell, so nothing shows, but the flag is wrong (when: the next fluids pass).
- The HUD's debug line (`FoundationHud.cpp:289`, hot file) still shows the crouch-or-stand eye in the swim pose.
- A waterlogged Playground slab is drawn and its registry fluid is right, but the Playground's swimming reads its fixed water volumes, not the block world.
- A waterlogged slab's water sides sit in the slab's side planes over its lower half (the opaque slab and translucent water may shimmer there; when: a look check in the Playground, then an inset if needed).
- The pour's replacement and drop apply to lava too (vanilla replaces a non-solid block with either fluid) and in creative (a fluid replacing a block isn't a player break); the drop follows the hand-break rule (its table, else "drops itself"), so a replaced block without a table pops itself (when: foundation.Loot.AllBlocks 1 or the block's own table).
- `GetFluid` looks the `waterlogged` property up for every non-model state (cheap, a few properties; a per-state bit if it shows in a profile).
- Provisional: the three fire sounds' volumes and pitches, the swim-pose start rule and eye smoothing, the pour drop's empty-hand roll (rows in `provisional-values.md`, T3 16a).
