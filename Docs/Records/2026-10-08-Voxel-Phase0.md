# Voxel world phase 0 - October 8, 2026

Scaffolding for the chunk loader and world generator ([Voxel world](../VoxelWorld.md), [worldgen prompt](../worldgen-implementation-prompt.md)). Nothing generates or draws terrain yet: this phase adds the module split, the settings and console surface, the tests and the mesh component, and proves they build and run.

## What changed

- **Git:** the block system, HUD, Playground and research work was committed as checkpoint `ac3a6b1` on `refactor/code-review-cleanup`. Voxel work continues on `feature/voxel-world`. New `Tests/Evidence` runs (2.4 GB; two Insights traces over GitHub's 100 MB file limit) stay untracked until their storage is decided.
- **Module `UnnamedVoxel`:** `Source/UnnamedVoxel/` depends on `Core` only and compiles with `FPSemanticsMode.Precise`. It holds `LogUnnamedVoxel` (`VoxelLog.h`), `STATGROUP_Voxel` (`VoxelStats.h`) and `VoxelConstants` (Z -64..319, 24 sections of 16, sea level 63). `Unnamed.uproject` lists it before `Unnamed`, and `Unnamed.Build.cs` depends on it.
- **Unreal side:** `Source/Unnamed/Voxel/` holds:
  - `UVoxelWorldSettings` (Project Settings > Game > Unnamed Voxel): dev seed, view radius, unload margin, test area radius, apply budget, tasks in flight, ambient occlusion, greedy meshing.
  - `VoxelConsole`: the matching `voxel.*` console variables and `voxel.Info`.
- **RealtimeMeshComponent:** the MIT Core is vendored at `Plugins/RealtimeMeshComponent/`. The pinned commit is `b8669a0891b728d8fa8a31f43f76d3853c10f743` (2026-08-23, plugin 5.4). It is copied unedited apart from `.git` and `.github`, and enabled in `Unnamed.uproject`.
- **Tests:** `Unnamed.Voxel.Constants`, `.Settings` and `.MeshBackend` (`Source/Unnamed/Tests/VoxelTests.cpp`), and `Scripts/TestVoxel.ps1`.
- **Docs:** new [Voxel world](../VoxelWorld.md). [Naming conventions](../NamingConventions.md) gets the Voxel area, the module exception and the `voxel.` prefix; [Architecture](../Architecture.md) gets the layout rows; `CLAUDE.md` gets two modules, the voxel rules and the test command.

## Decisions

- **Settings and console variables.** Runtime code reads the console variables. The settings object pushes its values into them at project-setting priority, at startup and on edit, so a console, command-line or ini override wins for the session. The engine's own `ConsoleVariable` import runs only in the editor, so the push is explicit.
- **Precise floating point in the core.** With default semantics, Unreal builds the editor `/fp:precise` and game targets `/fp:fast` (`VCToolChain.cs`), so one seed could generate different blocks in a packaged build than in the editor. The core module opts into precise semantics.
- **No chunk collision cooking.** The Foundation pawn collides against block data (`GatherCollision`), so RealtimeMesh collision stays off and Phase 3 drops "near-only async collision".
- **Phase plan additions.** Phase 2W wires Create New World (saved worlds, seed, world type, game mode, difficulty) after the Phase 2 test area. Mining and placing on generated terrain move up to Phase 3. A block-shapes step comes before Phase 6.

## Verification

- **Editor build** (`Build.bat UnnamedEditor Win64 Development`): succeeded in 84 s, including the first RealtimeMesh compile. No warnings in project code. RealtimeMesh warns about 5.8 deprecations (`UseGPUScene`, `FRayTracingGeometry::Initializer`, `GetProjectionMatrix`), so it needs an upstream update before UE 5.9.
- **`Scripts\TestVoxel.ps1`:** 3/3 suites, 0.02 s of test time, 19.8 s including editor start. Evidence: `Tests/Evidence/2026-10-08/VoxelAutomation-20261008-141518/`, with `voxel-info.txt`.
- **`voxel.Info`:** prints the world range, the dev seed `"Unnamed"`, all seven `voxel.*` variables (each "set by ProjectSetting"), 25 x 25 loaded columns, unload beyond radius 14, a 9 x 9 test area, 30 task workers, 60 tasks in flight, and RealtimeMeshComponent loaded.
- **Override check:** `voxel.ViewRadius 16` then `voxel.Info` showed `voxel.ViewRadius = 16 (set by Console)`, 33 x 33 loaded columns and unload beyond radius 18. In editor mode, `quit` from `-ExecCmds` did not close the process, so tests use `-TestExit`.
- **Every native suite** (`Automation RunTests Unnamed`): 49/49 in 22 s.

## Next

Phase 1, the deterministic core:

- `FVoxelSeed`: numeric text as typed; other text through our own pinned 64-bit hash.
- SplitMix64, Xoroshiro128++ and salted `Hash64(seed, salt, x, y, z)`.
- Scalar gradient noise with derivatives, fBm, ridged and warp variants, and a batch API.
- `FVoxelSpline`: nested cubic Hermite.

It is done when tests show identical output across runs and across 8 threads sampling in shuffled order, and noise throughput is logged.
