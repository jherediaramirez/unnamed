# QC feature status (survival program, feature/survival)

**Snapshot note, 2026-10-16:** This document describes the pre-QC feature freeze. Current fixes, counts and remaining acceptance are in [QC-STATUS-2026-10-16.md](qc/QC-STATUS-2026-10-16.md). All work is stopped by Jesus; no new feature work is authorized.

For Jesus's QC: what is a feature, what is only partly there, and what was never built. Bugs in built features are in `QC-KNOWN-BUGS.md`; this file says what to expect at all. Built from each line's LINE DONE list (PM 5), after Jesus's hard stop (2026-10-15 ~23:45: "The ones building right now will finish but we will not build any new features").

Status words: **BUILT** = in and meant to work (bugs go to QC-KNOWN-BUGS.md). **PARTIAL** = in, but with a known gap. **NOT BUILT** = not in the game yet; don't file it as a bug.

## Program-wide (not built)

- **World saving:** only world settings (world.json) save. Block edits, inventory, player position/vitals and mobs are lost on quit. First after QC (Jesus).
- **Day/night look:** the world clock runs, but the sky stays daytime unless `voxel.Atmosphere.Cycle 1` (console, F12 in the play copy). Twilight, square sun/moon and dusk water are parked (T7).
- **Deferred by Jesus:** Adventure mode, survival pick-block, view bobbing (13b), second sound batch, item extras (P5), beyond-vanilla mob AI, per-part hitboxes (idea only).

## The full-body player (M1a, 2026-10-16, after QC by Jesus's decision)

- **BUILT:** the Unnamed Player (a real Steve) in every view (`foundation.Body.Set 1`, default; 0 = Better Steve as before): idle, walk, sprint, crouch, crouch-walk, jump/fall/land (Soft), the head look on standing and crouching grids, a swim pose. In first person you see your own body with no head, legs and feet looking down, and your shadow keeps the head (`foundation.Body.FirstPerson`). Record: `Docs/Records/2026-10-16-Player-Body-M1.md`.
- **PARTIAL:**
  - The first-person corrections' standing values are PLAN's proposal, not yet tuned by Jesus. Crouch uses his setting B.
  - Held items still draw at the camera, not in the body's hand (LEGACY-HAND).
  - Swimming and flying use placeholders.
  - The dead player's world body stands.
- **NOT BUILT:**
  - The pickaxe strike through the real hand (M1b, next).
  - Rise/Firm landings, turn-in-place clips, the strike chain, other tools (M2).
  - New clips such as swim strokes, eat, use and death (M3; the swim clips are being authored).
  - Armor on the body.
  - Packaged builds are unchecked: the body folders may not be cooked.

## Line 1: containers (T1) — LINE DONE 27d0dfa3

- **BUILT:** furnace store/tick/fuel/XP (8a1); furnace screen + 10 cooking outputs incl. charcoal (8a2); lit state, smoker, blast furnace, crackle/smoke sounds, light 13 (8b1); output double-click, fuel-slot drag cap, shift-craft partial fit, pause-aware ambience (8b2a); barrel 27 slots with open state and sounds (9); chest + trapped chest, single 27 / double 54, facing, pairing, sneak = single, blocked by a block above (18-chest); furnace/smoker/blast-furnace particles (8b2b).
- **PARTIAL:** double chests draw as two single boxes; particles small/faint (look and timing values provisional); double-chest reach checks only the opened half; no 5-tick opener recheck (chest/barrel); the creative screen and pause menu still pause the game.
- **NOT BUILT:** chest lid animation (after QC); ender chest; trapped-chest redstone; cat-on-top rule; two-player furnace ids (multiplayer only).

## Line 3: player (T2 gear, T3 use, T4 feel) — LINE DONE ed35e737

- **BUILT:** lava/fire burning, swim pose, fall rounding (16a); fire/soul-fire contact damage and cauldrons fill/empty with water/lava buckets (16c); death screen, respawn point, drops on death, death messages (11a); hurt tilt/roll, hurt/death sounds, fire overlay, chat fixes (11b); player footsteps, working Master Volume, Bass slider (+5 dB default, Jesus's choice) (13a2); armor logic and armor items (19a1/19a2); shield, no starting loadout (19b); bow and arrows (19c).
- **PARTIAL:** first-person held poses (shield raise, bow draw) are simple placeholders (Jesus: keep visuals simple until the new body); 13a1's and 16b's planned fixes went to the QC list instead of being built.
- **NOT BUILT:** view bobbing and held-item sway (13b, on hold for the new body); Adventure mode (`/gamemode adventure` is refused); survival pick-block; a water cauldron putting out a burning player; spectral/tipped arrows; item extras (P5).

## Line 4: blocks (T5) — LINE DONE 20435b2d

- **BUILT:** block sounds (B0); 310 family blocks (B1); shapes, connections and collision for stairs/slabs/fences/walls/panes etc. (B2a/B2b); doors, trapdoors, fence gates (B3a); buttons and levers (B3b); ladders, vines and climbing, sugar cane/cactus/bamboo drops, firefly bush → short grass (B4a/B4b); hoe/shovel: farmland and dirt paths (B5); random ticks: wheat, carrots/potatoes/beetroot growth, pumpkin/melon stems with fruit, farmland moisture (B6a).
- **PARTIAL:** growth is B6a only: only wheat seeds plant; no attached-stem look, torchflower or pitcher plant; saplings don't grow into trees; grass/mycelium don't spread; sugar cane doesn't grow taller.
- **NOT BUILT:** the rest of growth (B6b); beds, sleeping and bed spawn (B7); per-block loot tables, e.g. ores, glass, leaves dropping saplings (B8); chest lid; the second sound batch; farmland trampling.

## Line 2: mobs (T6) — LINE DONE c79ee9ad

- **BUILT:** the mob framework (physics, hits, knockback, hurt/death, loot + XP on the killing hit, sounds, box-model view, fall/lava/fire hazards); cow, pig, sheep, chicken (wander, panic, tempt, look at the player, pathfinding, grazing, eggs); animals spawned with new terrain and the creature spawn cycle; zombie (targets, chases, hits for 3, knockback, burns in daylight); night monster spawning (cap 70, light rule, despawn, Peaceful removes monsters); spider (hostile only in the dark, leaps, climbs walls); sheep colours with tinted wool, dyeing, wool drops by colour, milking, cold/warm cow/pig/chicken variants.
- **PARTIAL:** shearing works in code but there is no shears item, so players can't shear; dyeing supports all 16 colours but only green and lime dye items exist; creeper and skeleton exist only as data (their spawn entries spawn nothing).
- **NOT BUILT:** shears and the 14 missing dye items (20e2); breeding, love mode, baby animals; creeper (fuse, explosion, block damage) and skeleton (bow AI) (20d); the dye sound.

## Looks shipped, but half-wired (final audit, SM-Codex 2, 2026-10-16; `codex/audit/report.md`)

Treat these as PARTIAL: the UI or data is there, the behaviour isn't.
- The recipe book is inert; Saved Hotbars do nothing; creative middle-drag doesn't work.
- Campfire recipes exist but are unused (no campfire cooking).
- The axe disabling a shield works only in the test lab, not from a real attacker.
- Milk can be collected but not drunk.
- "Main Hand: Left" only flips the HUD.
- Iron doors and pressure plates have no power source (no redstone).
- Arrows don't press buttons.
- Sword sweep and sprint knockback skip mobs.

Final build for QC: `feature/survival` at **c79ee9ad** (landing 47). Bugs: `QC-KNOWN-BUGS.md` (145 rows after the audit: 2 high, 36 med, 107 low).
