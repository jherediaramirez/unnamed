# Final scope before QC (Jesus, 2026-10-15 ~16:00, PM 4 chat: "Sounds good to me! Make sure everyone is aware.")

> **HARD STOP (Jesus, 2026-10-15 ~23:45):** only the builders already running finish (8b2b, 16c, B5, SMEAR2, 20e, B6a). Not built before QC: 20e2, 20d, B6b, B7, B8. See PROGRAM.md section 6, "HARD STOP".

Feature freeze. Lines finish FEATURES only; known bugs go to `QC-KNOWN-BUGS.md` (SM-Codex keeps it). Then SM-Codex's final audit, the final build, Jesus's QC, and the final merge into feature/voxel-world on his word only.

## Remaining (14 steps)
- **Line 1, SM-Containers:** 18-chest (facing, double chests, sneak = single; drawn as boxes, no lid) → 8b2b furnace particles → LINE DONE.
- **Line 2, SM-Mobs:** 20c (zombie, spider, night spawning, damage/knockback; Codex 30 #1/#2 ride along) → 20e (shearing, milk, breeding, variants) → 20d (creeper, skeleton; after T2 19c) → LINE DONE.
- **Line 3, SM-Player:** 19b (shield, no starting loadout, Shipping guard fix) → 13a2 (bass slider + footsteps) → 19c (bow and arrows) → 16c (fire-block damage, cauldrons) → LINE DONE.
- **Line 4, SM-Blocks:** B4 (ladders, vines, sugar cane drops, firefly_bush → short grass + hidden; NO chest no-mesh) → B5 (farmland, path, hoe/shovel) → B6 (growth) → B7 (beds) → B8 (block loot) → LINE DONE.
- **Line 5, SM-Atmosphere:** PARKED, LINE DONE 50f9b687 (local feature/survival-atmosphere, unmerged, unpushed; unverified WIP; handoff Docs/Records/2026-10-15-Atmosphere-T7a-Parked.md). SM-Atmosphere archived. Day/night stays off.

## Deferred until after QC
**First after QC: world saving** (Jesus, 2026-10-15 ~20:10, tap "First after QC": today only world.json settings save; block edits, inventory, player position/vitals and mobs are lost on quit). Day/night (+ dusk water), square sun/moon, chest lid, second sound batch (copper armor silent), T2 P5 item extras, T4 13b view bobbing, Adventure mode, survival pick-block, 13a1 fixes, beyond-vanilla mob AI, the new Steve character / full-body first person, all known-bug fixes (QC list).

## Rules still on
Token and LAND as before; every chat tracked in the track file's "Open chats"; tests inside WITH_DEV_AUTOMATION_TESTS; new Codex findings → QC list, not the next step.
