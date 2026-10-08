# Records

Short dated records of completed work, problems, fixes, and remaining gaps.

## October 8, 2026

- [Voxel world handoff](2026-10-08-Voxel-Handoff.md): start here in a new chat. It covers the project, the working agreement with the user, state at the end of phase 2 (uncommitted), known bugs with suggested fixes, the phase 2W plan, commands and gotchas.

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
