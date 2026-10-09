# Voxel world handoff: the phase 3 plan - October 8, 2026

Read this first if you are picking up the voxel world in a new chat. It continues the [handoff into phase 3](2026-10-08-Voxel-Handoff-Phase3.md). That record still holds the project overview, the working agreement, what each step did, the open issues, the commands and the gotchas, and none of it is repeated here. This record adds what the planning session found:

- a map of the code phase 3 touches, so the next session does not explore again;
- where the code differs from that handoff;
- the proposed plan, with its design and test spec;
- the risks;
- how to run the next test.

**Status: the plan is proposed, not approved.** The user asked to test the phase 2W build first, then ended the session. Before writing code:
1. ask what they found while testing;
2. confirm the plan and its assumptions (section 5) with them.

Facts were true at `9dce565`. Check `git status`, `git log` and newer [records](README.md) before trusting them.

## 1. Read these, in this order

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md` and the project's [CLAUDE.md](../../CLAUDE.md).
2. **This record.**
3. **[The handoff into phase 3](2026-10-08-Voxel-Handoff-Phase3.md):**
   - read sections 3 (working agreement), 6 (known issues), 7 (phase 3's shape), 8 (subagents) and 9 (commands and gotchas);
   - skim the rest.
4. **[Docs/VoxelWorld.md](../VoxelWorld.md)**, the living doc.
5. **[The worldgen prompt](../worldgen-implementation-prompt.md):** "Unreal side", "Job graph", phase 3's acceptance criteria, the performance targets and "How to work with me".
6. **[NamingConventions.md](../NamingConventions.md)** before adding a file or folder.
7. **Code:** open only what section 3 lists for the step at hand.

## 2. What this session did

- **Checked the state, changing nothing:**
  - **Git:** `feature/voxel-world` at `9dce565`, the previous handoff, one commit after `4987cdb`. No tracked changes; the only untracked files are the old `Tests/Evidence` folders.
  - **Editor build:** up to date (0 actions, 3.1 s).
  - **`Scripts\TestVoxel.ps1`:** 21/21 in 22 s (`Tests/Evidence/2026-10-08/VoxelAutomation-20261008-173505/`, 0.3 MB, untracked).
  - **Bench:** matched VoxelWorld.md: `Hash64` 5.4 ns, 3D fBm 130 ns, 115 million samples per second on 30 workers.
  - **`voxel.Info`:** load 25 x 25, unload beyond 14, 30 task workers, 60 tasks in flight.
- **Mapped the code** with two read-only Haiku 5.5 explorers, one for the editing path and one for the voxel module. Then checked their open points by hand: the interaction view, targeting, `FFoundationBlockWorld` and the base `GetAuthority`. The result is section 3.
- **Proposed the phase 3 plan** (section 5). It is not approved yet.
- **Started the game** for the user to test phase 2W: `Scripts\LaunchMenu.ps1 -Editor -Width 1600 -Height 900`.
- **Changed no code, scripts or assets.** The only edits:
  - this record and its entry in the [index](README.md);
  - a pointer at the top of the previous handoff;
  - the project CLAUDE.md's link to the latest handoff.

  None is committed: the user did not ask.
- **Memory:** added the explorers' token cost to the token-budget note.

## 3. Code map for phase 3

Paths start at `Source/Unnamed/`, except those beginning `UnnamedVoxel/`, which start at `Source/UnnamedVoxel/`. Line numbers are at `9dce565`.

### Editing path (Foundation)

| What | Where | Notes |
|---|---|---|
| `IFoundationBlockStorage` | `Foundation/FoundationBlocks.h:71-83` | All pure virtual: `Get`, `Set`, `Empty`, `GetSolidCount`, `ForEachSolid(Min, Max, Visit)` (an inclusive range) and `GetAllocatedSize`. The only implementer is `FFoundationSectionStorage` (h:86, cpp:17-100). |
| `FFoundationCell` | `FoundationBlocks.h:54-58` | A state plus a `Revision` (0 = never changed). `FFoundationBlockState` is a `uint16 Id` (`FoundationBlockRegistry.h:12`), the same state ids as the voxel block table. |
| `FFoundationBlockWorld` | `FoundationBlocks.h:111-159`, `.cpp:102-201` | <ul><li>**Storage:** creates its own `TUniquePtr<IFoundationBlockStorage>` in the constructor and has no setter.</li><li>**Bounds:** `IsKnown` checks only the inclusive `BoundsMin` and `BoundsMax`.</li><li>**Writes:** `Set` stamps the next world revision and records a change. `Clear` empties the storage, and the revision keeps counting.</li><li>**Change log:** `TakeChanges`. Past 65,536 pending changes the log is dropped and `bChangesOverflowed` is set.</li><li>**Also:** `GatherCollision(Region, Out)`, `GetCollisionBox`, `GetSelectionBox` and `OriginCm`.</li></ul> |
| `FFoundationAuthority` | `Foundation/FoundationAuthority.h:259-337` | <ul><li>Holds the world by value (h:262). The entry point is `Execute(Player, Command, Vitals)` (h:276).</li><li>**Commands** (h:10-28): `UseOn` places; `MineStart`, `MineContinue` and `MineAbort` mine; `SetCreativeSlot` needs `Rules.bCreativeInventory` (cpp:429-433); `Drop` is refused.</li><li>**`UseOn`** (cpp:465-567) checks the target, the reach (450 or 300 cm, h:185) and the revision, then calls `CheckPlacement` (cpp:242-287). That checks `IsKnown`, occupancy, support, supply, and overlap with statics, targets, other players and the player.</li><li>**Publishes:** the `FFoundationResult`, the world's change log, a 128-entry event ring (`GetEvents`) and `OnInteraction` (h:286).</li></ul> |
| Targeting | `Foundation/FoundationTargeting.h:61-73`, `.cpp:63-129` | `FoundationTargeting::Resolve` steps cell by cell from the eye (a DDA), reading through `World.IsKnown` and `World.Get`, so it works over any storage. |
| `AFoundationPlayground` | `Foundation/FoundationPlayground.h:46-73`, `.cpp:152-268` | <ul><li>Owns its authority by value (h:73).</li><li>**`BuildLayout`** (cpp:152-228): sets the registry, the bounds (cpp:157-161) and `OnInteraction`, calls `Authority.Reset()` (cpp:225) and creates the view (cpp:226-227).</li><li>**Authority access:** `GetAuthority` (h:46). `GetAuthorityState` (h:48) exists only here.</li><li>**`TraceTarget`** goes through `Authority.Resolve` (cpp:236).</li><li>**`GatherCollision`** reads the world on every call (cpp:263-268).</li></ul> |
| `UFoundationInteractionView` | `Foundation/FoundationInteractionView.h:19-137`, `.cpp` | <ul><li>**What it draws:** every block as an instance, grouped by section, from the change log (`TakeChanges` at cpp:294 and 311). Also the selection outline, mining cracks, the held item, break particles, floating numbers, targets and labels.</li><li>**Playground only:** `GetLab` casts its owner to `AFoundationPlayground` (cpp:48-50). It reads `GetAuthorityState` (cpp:97-99, 173) and `GetAuthority` (cpp:159, 682-685).</li><li>[Foundation architecture](../Foundation/Architecture.md) (lines 61-65) says the view changes nothing, yet it drains the change log.</li></ul> |
| `AFoundationLab` | `Foundation/FoundationLab.h:33-92` | <ul><li>**Virtual:** `BuildLayout`, `TraceTarget` (:54), `GatherCollision` (:60), `ApplyLogicalOffset` (:62), `GetSafeSpawn` (:64), `GetFallOutZ` (:66), `GetAuthority` (:68, returns null) and `ConfigurePlayer` (:71).</li><li>**Not virtual:** both `IsInWater` overloads (:56-57). They read the lab's water boxes and are called only from `FoundationPawn.cpp:160-161`.</li></ul> |
| Creative screen | `Foundation/FoundationPanels.cpp:447-546` | <ul><li>`OpenInventory` (:511-546) casts the lab to `AFoundationPlayground` (:518), then calls `OpenCreative` (:520).</li><li>`OpenCreative` (:447-509) reads the authority, the registry and the item rules, and the hotbar and offhand through `GetAuthorityState` (:477-484). It writes `SetCreativeSlot` through `GetAuthority` (:490-491).</li><li>The cast at :376 serves the Playground's debug options; leave it.</li></ul> |
| HUD | `Hud/HudPresenter.cpp:185-202` | Reads `GetAuthority()`, so it works as soon as the voxel lab returns an authority. |

### Voxel side

| What | Where | Notes |
|---|---|---|
| `AVoxelWorld` | `Voxel/VoxelWorld.h`, `.cpp` | <ul><li>**Overrides:** `BuildLayout`, `Tick`, `GatherCollision`, `GetSafeSpawn`, `GetFallOutZ`, `ApplyLogicalOffset` and `ConfigurePlayer`. Not `TraceTarget`, `GetAuthority` or `IsInWater`.</li><li>**`GatherCollision`** (cpp:392-409) returns a box per solid cell of the region, read from `Columns`.</li><li>**State:** `Columns` (`TMap<FIntPoint, TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>>`), `Chunks`, `BuildTask`, `Applying`, `BuildSerial`, `bReady`, `SpawnCell` and the shot state.</li><li>**Build:** `StartBuild` (cpp:238) launches one `UE::Tasks` task. It runs `FVoxelArea::Build` (cpp:244), then a `ParallelFor` for the stream sets (cpp:264).</li><li>**Apply:** `Tick` polls the task (cpp:363), reads `voxel.ApplyBudgetMs` (cpp:376) and calls `ApplyColumns`. That checks the budget only after each column (cpp:309), hence the overshoot.</li><li>**Spawn:** `GetSpawnCell` (cpp:411) and `ReturnPlayerToSpawn` (cpp:447).</li><li>**Saved world:** `GetSavedWorld` (cpp:136) and `MakeTerrain` (cpp:174-186).</li><li>**Shots:** `-VoxelShot` (cpp:131) and `UpdateShots` (cpp:453-551).</li><li>**Used by other code:** `Find`, `Regenerate`, `GetSeed`, `GetState`, `IsReady`, `GetColumnCount`, `GetSectionMeshCount`, `GetBuildReport` and `OriginCm`.</li></ul> |
| `UVoxelChunkComponent` | `Voxel/VoxelChunkComponent.h:37-52`, `.cpp:41-116` | <ul><li>**API:** `SetMesh(Streams, Cards, LayerMask, SectionMask)`, static `BuildStreams` and `BuildCards`. `Setup` makes four material slots.</li><li>**One buffer set**, keyed (0, 0): removed when the mask is empty (:80), otherwise updated (:85) or created (:88).</li><li>**`UpdateSectionConfig`** runs once per layer (:103).</li><li>**Bounds and render transform** update only when the section mask changes (:94-95).</li><li>**Cards** go in one `FRealtimeMeshUpdateContext` (:115).</li><li>That is up to six RealtimeMesh updates per `SetMesh`. Updating and removing already work, which pooling needs.</li></ul> |
| Columns | `UnnamedVoxel/Storage/VoxelColumn.h:48-55` | `Sections[24]` and `Heightmaps[4][256]`. `Build` is the only mutator; `GetAllocatedSize` reports memory. Shared read-only as `TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>` (`UnnamedVoxel/Generation/VoxelArea.h:25`). |
| Sections | `UnnamedVoxel/Storage/VoxelSection.h:51-55` | Palette, words, bits (0, 4, 8 or 16), a uniform state and a non-air count. `Set` exists, but only tests call it today. |
| Stages | `UnnamedVoxel/Terrain/VoxelTerrain.h:16-20`, `UnnamedVoxel/Meshing/VoxelMesher.h:46-49` | <ul><li>**Terrain:** `IVoxelTerrain::Generate(FIntPoint, FVoxelColumn&, TArray<uint16>& Dense)`, `GetSurfaceHeight` and `IsDry`.</li><li>**Mesher:** `GatherPadded(Neighborhood, SectionIndex, Padded)` and `MeshSection(Table, Padded, SectionIndex, Options, Out)`.</li><li>**Output:** `FVoxelMeshData` holds four layers (`UnnamedVoxel/Meshing/VoxelMeshData.h:45`).</li></ul> |
| Memory accounting | `UnnamedVoxel/Generation/VoxelArea.cpp:110-136` | Columns only. Mesh data, stream sets and cards are not counted yet. |
| Settings and console | `Voxel/VoxelWorldSettings.h:28-56`, `Voxel/VoxelConsole.cpp:17-124` | <ul><li>`ViewRadius`, `UnloadMargin` and `MaxTasksInFlight` exist as settings and console variables, but only `voxel.Info` reads them. `GetMaxTasksInFlight` (console cpp:31-35) turns 0 into twice the workers.</li><li>Only `ApplyBudgetMs` drives anything.</li><li>Commands: `voxel.Info`, `voxel.Seed`, `voxel.Regenerate` and `voxel.Bench`.</li></ul> |
| Stats and log | `UnnamedVoxel/VoxelStats.h:8-13`, `Voxel/VoxelWorld.cpp:37`, `UnnamedVoxel/VoxelLog.h:7` | `STATGROUP_Voxel` holds the generate, gather and mesh cycle stats, plus `STAT_VoxelApplyColumns`. The log category is `LogUnnamedVoxel`. |
| Tests | `Tests/Voxel*Tests.cpp` | 21 tests, and `Scripts/TestVoxel.ps1` expects 21: <ul><li>`VoxelTests.cpp`: Constants, Settings, MeshBackend;</li><li>`VoxelTerrainTests.cpp`: Blocks.Table, Terrain.Generate, Terrain.Flat;</li><li>`VoxelMesherTests.cpp`: Shapes :222, Borders :295, Cards :358;</li><li>`VoxelStorageTests.cpp`, `VoxelSplineTests.cpp`, `VoxelRandomTests.cpp`, `VoxelNoiseTests.cpp` and `VoxelDeterminismTests.cpp`.</li></ul> |

## 4. Where the code differs from the previous handoff

1. **Returning an authority is not enough for mining to work.** The selection outline, mining cracks, held item and break effects live in `UFoundationInteractionView`. It works only for `AFoundationPlayground`, and it also draws every block as an instance from the change log. It needs to accept any lab with an authority, and to leave block drawing to the chunks.
2. **The block world's storage cannot be swapped.** `FFoundationBlockWorld` creates its own and has no setter, and `IsKnown` knows only fixed bounds.
3. **`SetMesh` makes up to six RealtimeMesh updates**, not two: the buffer set, up to four section configs, then the cards.
4. **`Scripts/CaptureVoxel.ps1:29`** reports "of 4" when it checks for five shots (cosmetic).

As expected, `AVoxelWorld` has no authority, `TraceTarget` or `IsInWater` yet, and the streaming settings exist but nothing uses them.

## 5. The phase 3 plan (proposed)

From the prompt, phase 3 is done when:
- flying fast shows no holes;
- the game thread never exceeds the apply budget on chunk work;
- memory stays flat while circling.

Also in scope:
- mining and placing on generated terrain, through the authority;
- near-only collision (already read from block data);
- the generated-water fix;
- a debug fly camera.

### Assumptions to confirm with the user

1. **A square load area**, as `voxel.Info` already reports: 25 x 25 columns at radius 12, unloaded beyond 14.
   - `voxel.ViewRadius 4` then reproduces phase 2's 9 x 9 area, for like-for-like screenshots.
   - `voxel.TestAreaRadius` retires, and the `Unnamed.Voxel.Settings` test and `voxel.Info` change with it.
   - A circle would load 441 columns instead of 625, about 30% fewer.
2. **Copy-on-write per column**, not per section. A column is about 10 KB, so copying it on an edit is cheap and simpler.
3. **Edited columns stay in memory** when they unload, so edits survive flying away. They are lost on quit until phase 10's delta saves; the phase record must say so.
4. **Unloaded columns collide as solid**, so the pawn can never fall into a hole. The player spawns once its 3 x 3 columns exist.
5. **The per-section mesh cache is measured.** Each column keeps its sections' `FVoxelMeshData`, so an edit remeshes only dirty sections. If the cache costs more than the block data, keep it only within a few columns of the player and remesh whole columns elsewhere; edits happen within reach anyway.
6. **The interaction view stays the presentation** for the outline, cracks, held item and effects in generated worlds, with its block drawing off.

### Steps

| Step | Files | What and why |
|---|---|---|
| 1. Planner | `UnnamedVoxel/Streaming/VoxelStreamingPlanner.h/.cpp` (new folder and files) | A pure function. <ul><li>**Inputs:** the center column, radius, margin, the view direction in 2D, and the columns already present.</li><li>**Load list:** nearest first, biased toward the view. Ties break by coordinates, so the order is deterministic.</li><li>**Unload list:** columns beyond radius + margin.</li><li>Generation reaches radius + 1 and meshing reaches the radius, because a section meshes from its 3 x 3 neighbours.</li><li>Run it only when the center column, the radius or the view sector changes.</li></ul> |
| 2. Pipeline | `Voxel/VoxelColumnPipeline.h/.cpp` (new, plain C++) | <ul><li>**One record per column:** its data (shared, read-only), a state (queued, generating, generated, meshing, ready, applied), a serial, a data version, the cached section meshes and a dirty-section mask.</li><li>**Jobs:** launches `UE::Tasks` jobs up to `GetMaxTasksInFlight()`, nearest first. First mesh jobs, for columns whose 3 x 3 neighbours are generated; then generation.</li><li>**A mesh job** gathers and meshes its sections, then builds the stream set and cards.</li><li>**Stale results**, whose serial or data version changed, are dropped and requeued.</li><li>Plain C++, so tests drive it without a world.</li></ul> |
| 2. Subsystem | `Voxel/VoxelWorldSubsystem.h/.cpp` (new) | A `UTickableWorldSubsystem`, idle until `AVoxelWorld` starts it. Each tick: <ul><li>the focus is the local player's camera, so the fly camera works through the view target;</li><li>the pipeline updates and polls;</li><li>ready columns apply within `voxel.ApplyBudgetMs`, on components taken from a pool.</li></ul> It reports through `stat Voxel` counters (loaded, queued, in flight, applied per frame, apply ms, memory), a log line every second, and Insights scopes on every stage. |
| 2. Lab | `Voxel/VoxelWorld.h/.cpp` | Becomes the lab on top of the subsystem. <ul><li>**Stays:** the saved world, terrain, spawn, shots and player rules.</li><li>**Goes:** the fixed area. `FVoxelArea` stays for `voxel.Bench Columns` and the tests.</li><li>**`IsReady`** means every column within the radius is on screen.</li><li>**`GetState` and `GatherCollision`** read the pipeline's columns. Collision adds a full-height box for any unloaded column the region touches.</li></ul> |
| 3. Budget and pooling | `Voxel/VoxelChunkComponent.h/.cpp`, the subsystem | <ul><li>**Budget:** estimate each column's apply cost from its quad count (a running mean of measured ms per quad), and stop before the budget, not after.</li><li>**One update:** a single `FRealtimeMeshUpdateContext` for the buffer set, section configs and cards. Check the API at the pinned commit with a narrow grep in `Plugins/RealtimeMeshComponent`.</li><li>**Pooling:** unloading hides a component and removes its mesh; applying reuses one (move it, then `SetMesh`).</li></ul> |
| 4. Fly camera and flight capture | `Voxel/VoxelFlyCamera.h/.cpp` (new), `Voxel/VoxelConsole.cpp`, `Scripts/CaptureVoxel.ps1` | <ul><li>**`voxel.Fly [blocks per second]`** toggles a camera detached from the pawn, steered with WASD, Space, Ctrl and the mouse (read from the player controller's key state). The pawn holds still.</li><li>**`CaptureVoxel.ps1 -Flight`** flies a scripted path: straight legs at, say, 20, 40 and 80 blocks per second, then several laps of a circle about three view radii wide.</li><li>**Every frame goes to `flight.csv`:** time, position, speed, loaded, queued, in flight, applied, chunk game-thread ms (the whole subsystem tick plus the apply), holes, column bytes, mesh-cache bytes, process memory, and frame and GPU ms.</li><li>**A hole** is a column within `ViewRadius - 1` of the camera, inside the view frustum, that should have a mesh but is not on screen. The radius + 1 ring is generated only, so it never counts.</li><li>**Output:** `flight.csv`, a `flight.txt` summary and a few shots, in `Tests/Evidence/<date>/VoxelFlight-<time>/`.</li><li>Also fix the "of 4" message.</li></ul> |
| 5. Editing | `UnnamedVoxel/Storage/VoxelColumn.h/.cpp`, `UnnamedVoxel/Meshing/VoxelMesher.h/.cpp` | <ul><li>**`FVoxelColumn::SetState(cell, state)`:** the section's `Set`, then recompute the four heightmaps for that X, Y.</li><li>**A remesh rule** in the mesher: the sections whose padded 18^3 box holds the cell. That is up to 8 sections, across up to 4 columns.</li></ul> |
| 5. Storage adapter | `Voxel/VoxelBlockStorage.h/.cpp` (new) | `IFoundationBlockStorage` over the pipeline's columns. <ul><li>**`Get`:** the column's state, plus the revision from a sparse map of edited cells.</li><li>**`Set`:** copy the column, set the state, bump its data version, and mark the affected sections dirty. The pipeline then remeshes them and rebuilds the column's stream set and cards.</li><li>**`IsLoaded`:** true when the column is generated.</li><li>**`ForEachSolid`:** skips sections that are all air.</li><li>**`Empty`:** drops edits and revisions.</li></ul> |
| 5. Foundation | `Foundation/FoundationBlocks.h/.cpp`, `FoundationLab.h/.cpp`, `FoundationInteractionView.h/.cpp`, `FoundationPanels.cpp`, `FoundationPlayground.h` | <ul><li>**Storage:** add `FFoundationBlockWorld::SetStorage`. `IsKnown` also asks a new storage virtual, `IsLoaded`, which defaults to true so the Playground is unchanged.</li><li>**Water:** both `IsInWater` overloads become virtual. `AVoxelWorld` answers from block data: a fluid cell, with its surface at `FluidAmount / 9` of a block, as the previous handoff's issues table describes.</li><li>**View:** accepts any `AFoundationLab` with an authority, and gets a switch for block drawing. It still drains the change log when drawing is off.</li><li>**Creative screen:** gated on `GetAuthority()` instead of the Playground cast.</li><li>**`GetAuthorityState`** moves to the lab, or is replaced by `GetAuthority`.</li></ul> |
| 5. Voxel lab authority | `Voxel/VoxelWorld.h/.cpp` | <ul><li>**Owns an `FFoundationAuthority`**, set up the way `AFoundationPlayground::BuildLayout` does it: registry, rules and `OnInteraction`. Read cpp:152-228 first.</li><li>**Bounds:** Z -64 to 319, with wide X and Y. **Storage:** the voxel adapter.</li><li>**Overrides** `GetAuthority` and `TraceTarget`; the latter goes through `Authority.Resolve`, as the Playground's does.</li><li>**Creates the view** with block drawing off.</li></ul> |
| Tests | `Tests/VoxelStreamingTests.cpp`, `Tests/VoxelEditTests.cpp` (new), `Scripts/TestVoxel.ps1` | Written by a Sonnet 5.5 subagent from the spec below, in files separate from the implementation. |
| 6. Docs | `Docs/VoxelWorld.md`, `Docs/Architecture.md`, `Docs/Foundation/Architecture.md`, `Docs/NamingConventions.md` (if needed), `CLAUDE.md`, `Docs/Verification.md`, a phase 3 record and the index | Done by a Sonnet 5.5 subagent, given the measured numbers. |

Send screenshots after steps 2, 4 and 5. For before-and-after montages, compare with `Tests/Evidence/2026-10-08/VoxelShots-20261008-171008/` (phase 2W, the dev seed).

### Test spec

- **`Unnamed.Voxel.Streaming.Planner`:**
  - The load set is the square of the radius.
  - Nearest columns come first; at equal distance, ahead comes before behind. Ties are deterministic.
  - Nothing unloads until it is beyond radius + margin. Moving back and forth across a column border loads and unloads nothing new (hysteresis).
  - Moving one column adds one row and removes one row, exactly.
  - Radius changes work.
  - Negative coordinates and columns millions of blocks out work.
- **`Unnamed.Voxel.Streaming.Pipeline`** (no world):
  - Streaming the same radius from two directions, or in shuffled order, gives identical block hashes and mesh hashes per column.
  - Unloading and reloading gives identical results.
  - An unloaded column's in-flight result is never applied.
  - Work in flight never exceeds the limit.
  - No column meshes before its 3 x 3 neighbours exist.
- **`Unnamed.Voxel.Edit.*`:**
  - Edits are made at section and column borders, edges and corners.
  - The sections remeshed are exactly those whose padded box holds the cell.
  - After the incremental remesh, every section's mesh equals a full rebuild of the edited data (the same idea as `Mesher.Borders`).
  - Heightmaps match a column rebuilt from the edited dense buffer.
  - Revisions count up.
  - An edited column survives unloading and reloading.
  - `IsKnown` is false in unloaded columns, and placing there is refused.
  - Mining and placing on generated terrain work through `FFoundationAuthority::Execute`.
- **The existing 21:** keep them unchanged, except `Unnamed.Voxel.Settings` if `TestAreaRadius` retires. Update `TestVoxel.ps1`'s expected count.

### Verification at the end of the phase

- **Builds:** an editor build with no warnings in project code, and a `-DisableAdaptiveUnity` build before committing new files.
- **Native suites:** `Scripts\TestVoxel.ps1`, then every native suite (68 before this phase).
- **Foundation and menu**, because Foundation files change:
  - the Playground probe (`TestFoundationRuntime.ps1 -Editor -FrameRates 60`, 103 checks) and the Playground interaction suite;
  - `Tests/create-world.mjs --editor` (32/32). Its timings may need a look, since the first view is now 625 columns, not 81;
  - `Tests/foundation-menu.mjs --editor` (36/36);
  - `npm test` and `source-check.mjs`.
- **Phase 3's acceptance:** the flight capture's holes, chunk game-thread ms and memory slope, plus screenshots.

## 6. Risks and open questions

| Risk | Suggested handling |
|---|---|
| **Lumen at 625 columns**, 8 times today's area: more cards, surface-cache churn while flying, and GPU time (3.0 ms at 81 columns) | Preview it today with `voxel.TestAreaRadius 12`, then `voxel.Regenerate`, watching `stat unit`. Measure it in the flight capture. Options: fewer cards for far columns, Lumen's card-capture budget, or accepting the cost. |
| **The world's edge looks into the terrain.** The outer meshed ring's faces toward unmeshed neighbours are culled where the neighbour is solid. Minecraft shows the same at its render distance. | Distance fog until phase 9's far LOD. Or mesh the outer ring against air and remesh it when its neighbours arrive, at the cost of churn while moving. Ask the user. |
| **The `section` shot** relies on phase 2's edge, which was meshed against air | Replace it (for example, a cut through the camera's own column), or keep an edge ring meshed against air in captures only. |
| **The budget guarantee** covers all chunk work on the game thread, not only `SetMesh` | Plan only when the center changes, poll results without blocking, and count the whole tick in the stat and in the flight capture. |
| **Races between edits and jobs** | A data version per column; stale results are dropped and requeued. Tests cover it. |
| **The change log has one reader** | Only the view drains it. The remesh trigger lives in the storage adapter, not in the log. |
| **Authority rules** for generated worlds | Copy what the Playground's `BuildLayout` sets. Creative gets the creative inventory through the player's mode. Read that code before deciding. |
| **World rebasing** | `AVoxelWorld::OriginCm` and the authority world's `OriginCm` move together in `ApplyLogicalOffset`. |
| **RealtimeMesh at the pinned commit** | Keep the workarounds: one buffer set per column, padded layers, and the column's own bounds. A pooled component moves, so check its bounds and render transform on reuse, even when its section mask is unchanged. |

Still open from the previous handoff, unchanged:
- Lumen card occlusion (phase 4);
- software Lumen (before phase 4's caves; ask the user);
- Hardcore's permadeath;
- the world types that still generate the default terrain;
- missing block shapes;
- stale generated docs;
- the old evidence decision (ask the user).

## 7. How to run the next test

- **First, ask the user what they found** while testing the phase 2W build at the end of this session, and fold it into the plan.
- **Start the game:**
  - From the menu: `& Scripts\LaunchMenu.ps1 -Editor -Width 1600 -Height 900`. Without `-Editor` it opens the old `SuperFlatDevelopment` package, which predates Create New World.
  - Or open `Content/Maps/L_Generated` in the editor and press Play.
- **What the user sees today:**
  - Create New World opens the world in about 0.3 s.
  - A 9 x 9-column area, its edges cut down to bedrock.
  - Walking, jumping, sprinting and crouching, with collision from block data. Superflat builds its layers.
  - None of these yet on generated terrain: streaming, mining, placing, the creative inventory, swimming, flying. The Playground still has mining, placing and the creative inventory.
  - Console (`~`): `stat Voxel`, `stat unit`, `voxel.Seed <text>`, and `voxel.TestAreaRadius <n>` followed by `voxel.Regenerate`.
- **During phase 3, after each step:**
  1. Run `TestVoxel.ps1` with the planner tests.
  2. Run `CaptureVoxel.ps1` twice to show the streamed world: once at the default radius, and once with `voxel.ViewRadius 4` (like for like with phase 2W).
  3. Check that `stat Voxel` and the per-second log line never show the apply over budget.
  4. Run the flight capture, and give the user `voxel.Fly` to try.
  5. Check mining and placing at column borders and corners, swimming in generated water, the creative inventory in a Creative world, and an edit surviving flying away and back.
- **Evidence:** fresh folders only. A Haiku 5.5 subagent scrapes `flight.csv` into tables.
- **Sending files:** in this session `SendUserFile` refused with "this session is not on a project thread". If it fails again, give the user the paths of the PNGs and montages instead.

## 8. Subagents: what this session learned

- **Explorers cost too much.** The two Haiku 5.5 explorers took about 9 minutes and 266k and 310k tokens. That alone exceeds the user's 100k to 200k milestone budget.
  - Section 3 replaces them: do not explore again.
  - Answer follow-ups with targeted greps and header reads. Those cost a few thousand tokens here.
- **For the rest of phase 3:**
  - **Sonnet 5.5:** the two test files from the spec above, a review of the diff before each commit, and the docs at the end.
  - **Haiku 5.5:** only scraping logs and `flight.csv` into tables.
- **Never run builds, editor runs or captures in parallel.**
- **Report token use** at the end of the phase.
