# Water and sky handoff: W3 part 1 done, into W3b (Playground parity, look shots, the flip)

October 11, 2026. W3 part 1 (fluid surfaces: the core rule, the mesher, `voxel.Fluids.SlopedTops`, the materials) is done on `feature/water-sky`; its record is [2026-10-11-WaterSky-W3-Part1.md](2026-10-11-WaterSky-W3-Part1.md). **W3b** finishes W3 (plan section 5): Playground parity for sloped cells, the look shots beside the vanilla probe, the mesh cost per touched section, and the `voxel.Fluids` flip to 1 with a 0/1 capture pair.

## Short prompt (W3b)

> Water and sky track, session W3b (a parallel lane): finish fluid meshing. Work in `C:\dev\projects\Unnamed-water` on `feature/water-sky`. Step 0, on the manager's go: merge `feature/voxel-world` once (never rebase or force), keeping both sides in `Docs/VoxelWorld.md` and the records README, then `RunChecks -Profile merge` against `C:\dev\tools\shift-managers\expected-hashes-main.txt` and one combined report (the handoff's step 0). Never touch other worktrees, never push, and make no `CLAUDE.md` edits. Read `Docs/Records/2026-10-11-WaterSky-Handoff-W3b.md` and follow its reading list. Then build: Playground parity for sloped fluid cells only (Q2 (a)), the look shots beside the vanilla probe's (`C:\dev\sandbox\vanilla-probes\facts.md`), tuning the corner rule and flow only where the shots call for it, the mesh cost per touched section, then flip `voxel.Fluids` to 1 with a 0/1 capture pair. `Mesher.Digest` stays identical at the default. Send the manager a short plan before building.

## Step 0: the merge (on the manager's go)

Merge `feature/voxel-world` into `feature/water-sky` once, on the manager's go. Never rebase or force.

- **Main has moved:** items S5-S7 and S5b; S9d part 2 (fallen trees, huge mushrooms) with new radius-8 and `Mesher.Digest` values; tools S2.
- **Conflicts:** keep both sides in `Docs/VoxelWorld.md` (S9d2's sections beside W3's "Fluid surfaces" bullet and test rows) and in `Docs/Records/README.md` (every index entry).
- **Build:** rebuild with the touched sources or `-DisableAdaptiveUnity`.
- **Checks:** run `Scripts\RunChecks.ps1 -Profile merge -ExpectedHashes C:\dev\tools\shift-managers\expected-hashes-main.txt` on the clean merge commit. Main's new digests must hold with W3's mesher at the default, unchanged: verify them, never re-record.
- **Report:** one combined report to the manager. Stop on red.
- **Set aside:** W3b code started before the merge goes into a WIP commit or a patch, never a bare `git stash`.

## Reading list (W3b)

1. This file.
2. [The W3 part 1 record](2026-10-11-WaterSky-W3-Part1.md): Decisions, What changed, Known gaps.
3. `Source/UnnamedVoxel/Meshing/VoxelFluidSurface.h` (whole; short): the rule and the packed cell.
4. `Source/UnnamedVoxel/Meshing/VoxelMesher.cpp`: 380-425 (`FluidFace`, `MakeFluidSurface`), 556-585 (the packed array, `GetFluidSideCode`) and 700-745 (the fluid case). Read in ranges.
5. For the parity: `Source/Unnamed/Foundation/FoundationBlockMeshes.h` (whole) and `.cpp` 85-170 (`GetUnitBox`, `GetKind`, `Build`); `FoundationInteractionView.cpp` 233-269 (`MakeCustomData`), 297-337 (`AddInstance`) and 376-426 (`RefreshWaterBelow`, `RebuildBlocks`, `ApplyChanges`).
6. For the shots:
   - `C:\dev\sandbox\vanilla-probes\facts.md`: every setup command and `/tp` pose of the vanilla water shots;
   - `C:\dev\sandbox\vanilla-probes\analysis\water_sheet.png`;
   - the 26 frames in `C:\dev\sandbox\vanilla-probes\out\20261011-004251`;
   - `Source/Unnamed/Voxel/VoxelFluidShots.h/.cpp` and `CaptureVoxel.ps1 -Fluids`.
7. Plan section 5, W3 (scope, tests, evidence, gate), and section 6 Q5.
8. Not to read: `Reference/minecraft-blocks-26.3/research/water_research.md` and `Docs/MinecraftBlockReference.md` section 4 (bytecode-derived, banned: plan Q18).

## Working agreement

- The lane rules (plan section 4):
  - never re-record goldens or hashes;
  - one editor build at a time;
  - timed gates in the manager's quiet window;
  - commits stay on `feature/water-sky` and the manager merges;
  - no `CLAUDE.md` edits.
- Behaviour comes from the wiki, scout M's probes and now the vanilla client shots, never from game code. Tune the corner rule and flow by shots side by side with vanilla's, posed the same.
- Look calls go to the manager (Jesus's away rule: closer to vanilla wins). Send the captures with numbers, a control pair proving the switch took effect, and a recommendation, and keep the other option one switch away.
- Suites through `Scripts\RunChecks.ps1` (`-Profile quick|final`, `-Flight smoke`); the end-to-end checks after the last code change.
- Subagent briefs: one deliverable, well under 100k. Diff-only Sonnet reviewers.
- Set work aside with a WIP commit or a patch, never a bare `git stash`.

## Settled decisions

- Everything in the W2a, W2b and W3 part 1 records and handoffs, including:
  - Q5 (b) is the default (`SlopedTops 1`), with (a) behind `SlopedTops 2`, used for shots only; flipping the default is a later main-lane look call;
  - the materials pick the texture array from the table's fourth channel;
  - lava sources keep `lava_still` sides under 1.
- Q2 (a): Playground parity covers sloped cells only. Flat cells keep today's boxes and the interaction probe's instance counts, and the z-fight stays on the small-fixes list.
- Q3: waterlogged slabs drawn wet are deferred.
- The flip: `voxel.Fluids` goes to 1 in W3b, with a 0/1 capture pair (the manager's call, October 10).

## Code map

| Piece | Where | Notes for W3b |
|---|---|---|
| Surface rule | `UnnamedVoxel/Meshing/VoxelFluidSurface.h/.cpp` | `Pack`, `Make(Ring, Mode, Out)`, `GetCorner`, `GetFlowUVs`. The Playground calls the same functions with a ring packed from `FFoundationBlockWorld` cells. |
| Mesher | `VoxelMesher.cpp` `FluidFace`, the fluid case | Sloped tops and their sides draw per cell; flowing lava's sides take `Table.GetFlowCode(Lava)`. |
| Switch | `VoxelFluidSystem.cpp` (`voxel.Fluids.SlopedTops`, `FVoxelFluidSystem::GetSlopedTops`); `VoxelWorldSubsystem.cpp`; `VoxelColumnPipeline::SetSlopedTops` | The Playground reads `GetSlopedTops()` when it draws an instance. |
| Flow codes | `FVoxelBlockTable::SetFlowCode/GetFlowCode`; `VoxelBlocks::MakeTable` | The Playground can look textures up itself (`FindTexture`, as `MakeCustomData` does). |
| Materials | `Scripts/Editor/CreateBlockMaterials.py` 85-95 (`FLOW_SAMPLE`), 137-145 (`CHUNK_FLOW_SAMPLE`), 182 (`MATERIALS`) | Playground water and lava pick the flow array from the custom-data code's table entry. A sloped Playground top can carry the flow code as its model code (UV 1 with `ModelCodeBit`, as model quads do) or set custom data 5 per instance. |
| Playground water | `FoundationBlockMeshes` (kinds: `WaterFirst..WaterFull`), `FoundationInteractionView::AddInstance`/`RefreshWaterBelow` | Today a water cell's look depends only on the cell above. A sloped cell depends on its 3 x 3 ring and the cells above it, so a change must refresh the fluid cells around it, 3 x 3 x 2. |
| Shots | `Voxel/VoxelFluidShots.h/.cpp`, `CaptureVoxel.ps1 -Fluids breach\|flood` | Add W3's scenes (waterfall, 7-cell spread, terraces, shoreline pool, lava spread and fall) posed as facts.md's. |
| Tests | `Tests/VoxelFluidMeshTests.cpp` | Add the Playground parity check here: its built mesh against `VoxelMesher` for the same ring. |

## Known issues

| Issue | When |
|---|---|
| The Playground draws sloped cells as stepped boxes. | W3b |
| `voxel.Fluids` stays 0 (flowing water only in captures that switch it on). | W3b |
| The corner rule and flow direction are untuned; sloped tops split along their first diagonal. | W3b, beside vanilla |
| The mesh cost of sloped (unmerged) tops per touched section isn't measured. | W3b (indicative, with the machine state; a gate only in a quiet window with the flight) |
| Waterlogged slabs draw dry. | a later water session, with a digest check |
| Water under lava draws no surface between them. | W4/W5, if a probe shows it matters |
| Mode 1 keeps the see-through step beside a source under more water (equal levels). | the main lane's flip to (a) |
| The Playground's water draws every face (z-fight against solids). | the small-fixes list |
| From W2b: dry waterloggable plants never entered; lava onto a waterlogged slab turns it to stone; drained cells stay as edits; budget overshoot by one act; settling cost in a big flood; generated edges don't wake. | as W2b's handoff says |

## What worked and what failed (W3 part 1)

- **Worked:**
  - **Shared corners instead of strips.** Each corner comes from the same four cells for every neighbour, so tops meet exactly and no strip is needed between cells of one fluid. A property test over random fields pins it.
  - **Mode 1's identity proved two ways:** `Mesher.FluidGenerated` (mode 0 equals mode 1 on a generated area) and `Mesher.Digest` (mode 1 equals the goldens). A mutation of the equal-level exception fails both.
  - **The materials' control pair:**
    - swap the saved old `.uasset`s in, capture, swap back, capture;
    - a second same-material run measures the noise (voxel shots differ 1-4% between runs; the tour 0% but moving shots);
    - the breach scene is the positive control (sloped tops look different with the old materials).
- **Failed, then fixed:**
  - My random-field test shifted the hash right before `ToIndex` (which reads the top 32 bits), so every cell came out "full" and the test checked nothing. It now shifts left, and reports how many corners it checked.
  - `FMath::IsNearlyZero` defaults to 1e-8, too tight for float sums of ninths.
  - PowerShell `Set-Content -NoNewline` with an array joins the lines with no newlines: use `git diff --output=`.
  - A replacement with `` `r`n `` in an LF file mixed line endings; the repository is LF (`* -text`).
- **Process:** a screen hold paused the windowed captures. The headless work (builds, `-NullRHI` suites, the material commandlet with `-AllowCommandletRendering`) went on.

## Tokens

- W3 part 1's lead: about 213k after the reading list, 329k at the checkpoint, 407k at the final checks, 431k with the records (MeasureAgentUsage). One Sonnet reviewer, 76.1k peak.
- Expect about 100k for W3b's reading list. The images cost about 1.5k each; compare them by numbers first (`imgdiff.py`) and look at a few.

## Evidence

- The W3 part 1 record's Evidence section. The first look at slopes is `Tests/Evidence/2026-10-11/WaterSky-W3-ctl-after-breach/breach-20s.png`.
- The vanilla probe: `C:\dev\sandbox\vanilla-probes\facts.md`, `analysis\water_sheet.png`, `out\20261011-004251` (water reaches 7 cells and lava 3; the settle pairs match).
- `C:\dev\sandbox\water-w3\`: the scripts (`build.ps1`, `runtests.ps1`, `mutate.ps1`, `materials.ps1`, `imgdiff.py`, `aaoff\TestFoundationRuntime.ps1`) and the old materials (`materials-before\`).

## Open questions (with options)

1. **The diagonal a sloped top splits along:**
   - (a) the first diagonal (now);
   - (b) the one whose ends differ less, which looks more planar;
   - (c) whichever matches vanilla's shots.
   
   Recommended: (c), decided from the waterfall-ring and terrace shots.
2. **Corner weighting:**
   - (a) sources set the corner (now);
   - (b) a weighted mean of sources and flowing cells.
   
   Recommended: keep (a) unless the vanilla spread shot shows sources sagging at their corners. If it does, add (b) behind a value and send the pair.
3. **The Playground's sloped top code:**
   - (a) the flow code as the vertex's own code (`ModelCodeBit` in UV 1, as model quads do), so one mesh fits any instance;
   - (b) per-instance custom data 5.
   
   Recommended: (a), so the mesh carries its flow UVs and code together.
