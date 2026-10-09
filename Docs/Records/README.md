# Records

Short dated records of completed work, problems, fixes, and remaining gaps.

## October 9, 2026

- [Voxel world handoff: phase 5, into session 4](2026-10-09-Voxel-Handoff-Phase5-Session4.md): start here in a new chat. Session 3b (commit `5524d5d`) gave the generated map eye adaptation (histogram metering between EV100 6 and 12.5, with the compensation and local exposure's middle grey moved so daylight keeps exactly its old exposure) and the chunk materials an emission of 8000, so caves read and still lava glows; the same-cave comparison kept Lumen cards over hit lighting. It covers what to read, the working agreement, both look calls and their knobs, the session 4 plan (biome source, storage, debug map, minimap), notes for sessions 4 to 6, known issues, and what worked and failed.
- [Voxel world handoff: phase 5, into session 3b](2026-10-09-Voxel-Handoff-Phase5-Session3b.md): superseded by the session 4 handoff. Session 3 (commit `f10f5b6`) added cross-column corner sharing (a shared cache of edge corner columns, exact; caves-on columns under 5 ms in the bench's best case) and a batch `old_blended_noise` (exact; no measurable gain yet, groundwork for the normal-noise lever). It covers what to read, the working agreement with Hemera and the new context checkpoints, session 3b's cave brightness fix and cave-lighting comparison, the profile and the normal-noise lever for session 6, known issues with when each is due, and the session's token cost.
- [Voxel world handoff: phase 5, into session 3](2026-10-09-Voxel-Handoff-Phase5-Session3.md): superseded by the session 3b handoff. Session 2 (commit `eaa6ef3`) added the overworld surface rules, conditions, 56 biomes and the code-built biome parameter list to the worldgen reference, the biome and surface rule readers, still lava, powder snow and snow layers through the block reference, and a hard error for saved worlds whose terrain cannot be built. It covers what to read, the working agreement with the day manager Hemera, session 3's performance levers, the cave brightness proposal, known issues with when each is due, and the session's token cost.
- [Voxel world phase 5 plan, biomes and surface](2026-10-09-Voxel-Phase5-Plan.md): read right after the phase 5 handoff. Plan only, nothing built. The data check (surface rules live in 26.3's new `material_rule` and `material_condition` registries, not in the noise settings; 67 biome files; the overworld parameter list is code-built), the block gaps (`powder_snow` and snow layers), the design for biome lookup, storage, surface rules and the snow line, five build sessions with files, tests and acceptance captures, and the questions for Jesus with recommendations.
- [Voxel world phase 4, terrain](2026-10-09-Voxel-Phase4.md): real terrain on generator 2, from 26.3's own density graph with our own noise, evaluator and aquifers. Covers the five sessions (worldgen data, noise and climate, every op and column density, caves and still lava, performance and benches), the decisions confirmed with the user and those made overnight by the manager for Jesus's review, the acceptance table (all four criteria met), the performance against the 5 ms limit and 2 ms target (overworld 4.53 ms mean with caves, Large Biomes and AMPLIFIED slightly over, none at 2 ms), the flight (3 of 10,107 frames over the 2 ms budget, worst 2.12 ms), the problems found, measurements and known issues.

## October 8, 2026

- [Voxel world handoff: phase 4, into session 5](2026-10-09-Voxel-Handoff-Phase4-Session5.md): start here in a new chat. Session 4 (commit `b306f87`) turned caves on, added our own aquifers (water, still lava, walls) with exact skipping, still lava in the voxel table, lazy interval_select, and the cave-lighting comparison (Lumen cards kept). It covers what to read, the working agreement and settled decisions, timings before and after with the performance levers for session 5, the session 5 brief, a code map, open questions for Jesus, what worked and failed, commands and token use. It replaces the session 4 handoff below.
- [Voxel world handoff: phase 4, into session 4](2026-10-09-Voxel-Handoff-Phase4-Session4.md): history. Session 3 (commit `416a190`) confirmed every density op against the jar, added our own base 3D noise, column density with height-bound skipping, and generator 2 terrain on the three presets (caves off). It covers what to read, the working agreement (including the cave-lighting decision), the plan, what session 3 did and verified with timings, known performance issues, the session 4 brief (caves, aquifers, lava) and its watch item, a code map, open questions for Jesus, what worked and failed, commands and token use. It replaces the session 3 handoff below.
- [Voxel world handoff: phase 4, into session 3](2026-10-09-Voxel-Handoff-Phase4-Session3.md): the handoff session 3 started from. Session 2 (commit `003bf7b`) added our own 26.3-style noise, a density-graph evaluator, the climate sampler, splines and climate maps. It covers what to read, the working agreement (overnight chats cannot send files), the plan, what session 2 did and verified, the session 3 brief (3D density, generator 2), a code map, open questions for Jesus, known issues, what worked and failed, commands and token use. It replaces the session 2 handoff below.

- [Voxel world handoff: phase 4, into session 2](2026-10-08-Voxel-Handoff-Phase4-Session2.md): the handoff session 2 started from. Session 1 (commit `6c2c608`) brought in the 26.3 worldgen data (`Reference/minecraft-worldgen-26.3/`) and read it into a density graph. It covers what to read, the working agreement (including the overnight manager chat), the phase 4 plan and decisions, what session 1 did and verified, the session 2 brief (noise, climate, splines, debug views), a code map, known issues, what worked and failed, commands and token use. It replaced the phase 4 handoff below.

- [Voxel world handoff into phase 4](2026-10-08-Voxel-Handoff-Phase4.md): the handoff session 1 started from. It covers:
  - what to read, in order;
  - the working agreement with the user;
  - where every phase stands, and what phase 3 did;
  - a code map for phase 4 (terrain);
  - the user's answers to the phase 4 questions, and a five-session plan;
  - known issues, including three deferred from the pre-commit review;
  - what worked and what failed;
  - commands and gotchas.

  It replaces the earlier handoffs.

- [Voxel world phase 3, streaming and editing](2026-10-08-Voxel-Phase3.md): an endless world around the player, with mining and placing on it. Covers the planner's circle of columns (545 in view, 657 generated at radius 12), the column pipeline and its stale-result rules, the 2 ms game-thread budget, per-column edit lists and the storage adapter, the shared Foundation changes, `voxel.Fly` and the acceptance flight (no holes, 0 of 16,736 frames over budget, flat memory), the edge fog, the decisions confirmed with the user, the problems found (including the probe that had lowered the user's graphics settings), measurements and known issues.

- [Voxel world handoff: the phase 3 plan](2026-10-08-Voxel-Handoff-Phase3-Plan.md): the planning session's handoff. It covers:
  - the verified state at `9dce565`;
  - a code map of what phase 3 touches, so there is no need to explore again;
  - where the code differs from the previous handoff;
  - the proposed phase 3 plan (not yet approved), with its assumptions, files, design and test spec;
  - risks, how to run the next test, and what the explorers cost.

- [Voxel world handoff into phase 3](2026-10-08-Voxel-Handoff-Phase3.md): read it with the plan handoff above. It covers what to read, the working agreement, what phases 0 to 2W and step 2L did, known issues with suggested fixes, phase 3 (streaming and editing) and a way to build it, where subagents (Haiku 5.5, Sonnet 5.5) help, commands and gotchas. It replaces the earlier handoff.

- [Voxel world phase 2W, Create New World](2026-10-08-Voxel-Phase2W.md): real saved worlds. Covers `Saved/Worlds/<id>/world.json` and its ids, the native create, open, rename and delete calls, the native world list without sample saves, Superflat's flat terrain, game mode and difficulty reaching the player (Peaceful added), the end-to-end CEF suite and timings.

- [Voxel world step 2L, lighting](2026-10-08-Voxel-Lighting.md): shaded slopes were near black because the chunks had no Lumen cards and the sky was dim against the sun. Covers the cards each column now builds, the generated map's sky light of 3, the experiments (hit lighting, software Lumen, no Lumen GI, sky levels), brightness and GPU measurements, and the cards' limits.

- [Voxel world handoff](2026-10-08-Voxel-Handoff.md): the first handoff, at the end of phase 2. It covers the project, the working agreement with the user, state at the end of phase 2 (uncommitted), known bugs with suggested fixes, the phase 2W plan, commands and gotchas.

- [Voxel world phase 2](2026-10-08-Voxel-Phase2.md): the first generated chunks on screen. Covers the registry block table, palette sections, columns with heightmaps, a stand-in heightmap terrain, greedy meshing with neighbour borders and ambient occlusion, chunk materials, a 9 x 9-column walkable test area in `L_Generated`, offscreen screenshots, the RealtimeMesh workarounds, and timings.

- [Voxel world phase 1](2026-10-08-Voxel-Phase1.md): the deterministic core. It covers seed parsing with a pinned 64-bit text hash, `Hash64` and salts, Xoroshiro128++, hashed-lattice gradient noise with fBm, ridged, damped and warp, nested Hermite splines, `voxel.Seed` and `voxel.Bench`, the 8-thread shuffled determinism test, and timings.

- [Voxel world phase 0](2026-10-08-Voxel-Phase0.md): the chunk loader's scaffolding. Covers the Core-only `UnnamedVoxel` module with precise floating point, voxel settings mirrored to `voxel.*` console variables, `voxel.Info`, the vendored RealtimeMeshComponent, `Unnamed.Voxel` tests, and the checkpoint commit and branch.

- [Blocks](2026-10-08-Blocks.md): all 529 Java 26.3 full-cube blocks and slabs plus static water with exact protocol and state ids; textures, render layers, tints, animation; vanilla placement rules; block gallery; 1:1 creative inventory and 3D hotbar icons; dummy block retired; tests and gallery evidence.

- [Playground](2026-10-08-Playground.md): one built-in world (`playground`) replaces Player Foundation, Super Flat and Interaction Lab; retired IDs; the dev grid material; anchor rule; token fixtures and line grids removed; movement course moved east; verification pending.

- [Dummy Block interaction foundation](2026-10-08-Interaction.md): the Interaction Lab world (since retired; now the Playground); block world, items, targeting, authority and interaction state; placing, mining and melee on Java 26.3 rules; VanillaReference and LucidFluid presets; jump/fall placement; the mouse-look pitch fix; tests, package and evidence.

## October 7, 2026

- [Player vitals - HUD milestone 2](2026-10-07-Vitals.md): review of the milestone-1 HUD; real session-only health, armor, food, air, falls, XP, attack recovery, mode and difficulty on Java 26.3 rules; lab water column and jump stair; tests and package evidence.

- [Java 26.3 HUD milestone 1](2026-10-07-Hud.md): native Slate presentation, original PNG import/registry, explicit preview controls, test and package evidence.

- [Code-review cleanup](2026-10-07-Code-Review-Cleanup.md): independent review applied step by step - formatting, layout, world catalog, menu host component, diagnostics split, shared menu JS, consistency, naming, evidence and docs; behavior changes and verification.

- [Personal controls restored](2026-10-07-Personal-Controls.md): Left Shift sprint / C crouch in the first-person foundation, Controls resets and fresh profiles; verified r3 package with custom saves retained.

- [Player Foundation](2026-10-07-Player-Foundation.md): version-pinned Java 26.3 research, box-based first-person movement, existing Controls integration, action fixtures, expanded F3 and measured Development-package evidence.

- [Super Flat test world](2026-10-07-Super-Flat.md): large editable level, the playable Singleplayer entry, preview camera, collision checks and tested package.

- [Schema import fix](2026-10-07-Schema-Import-Fix.md): resolves the unexpected DataTable import dialog; current editor module and package.

- [Menu preference persistence](2026-10-07-Menu-Preferences.md): 101 additional choices, language/packs, native saving, reset/quit integration and current package.

- [Today's work](2026-10-07-Menu-Work.md): implemented functionality and where to find it.
- [Independent review fixes](2026-10-07-Review-Fixes.md): the five findings, changes, and verification.
- [Known gaps](2026-10-07-Known-Gaps.md): intentional mocks, untested acceptance gates, and deferred work.
- [GitHub backup](2026-10-07-GitHub-Backup.md): private repository, preserved recovery history, safeguards, and checkpoint commands.
- [Keybindings capture fix](2026-10-07-Input-v2.md): C crouch, Shift sprint, Unreal key capture, native API display and current testing instructions.
- [Keybindings foundation](2026-10-07-Keybindings.md): native movement bindings, saved policies, intent rules, verification and package.

Project: `C:\dev\projects\Unnamed`.
Playground: no packaged candidate yet (see the [Playground record](2026-10-08-Playground.md)). Newest preserved package: `C:\dev\projects\Unnamed\Packages\InteractionDevelopment20261008-r3\Windows\Unnamed.exe` (retired worlds); earlier checkpoints include `Packages\VitalsDevelopment20261007-r2` and `Packages\SuperFlatDevelopment`.

Add a new `YYYY-MM-DD-Topic.md` for future work. Keep historical records intact and link the newest record here. Broader ownership and evidence remain in [Architecture](../Architecture.md) and [Verification](../Verification.md).
