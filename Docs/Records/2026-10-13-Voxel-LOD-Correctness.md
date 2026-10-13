# Far LOD correctness: three review findings fixed (A1-A3)

2026-10-13 · `feature/voxel-lod3`, from `f0fde83`. Source: the independent far LOD review, findings A1-A3. The review and
the action plan are in `C:\dev\sandbox\lod-review` (`review.md`, `action-plan.md`, `fix\summary.md`).

## Fixes
- **A1: a dropped mesh no longer strands a column in the wrong kind.** SetFocus drops a waiting mesh whose drawn column
  now wants the other kind, and now sets `MeshedKind` to `None`. Before, it kept the dropped kind. Then a quick move
  back counted the column as meshed while it showed the other kind, and it stayed that way while the player stood still.
- **A2: a far mesh over the budget no longer stops the apply loop.**
  - `GetVoxelApplyStep` (`VoxelWorldSubsystem.h`) holds the loop's rules as one pure function.
  - A far mesh that does not fit is skipped, and the frame goes on with near meshes only (`PeekReady`/`PopReady`
    `bNearOnly`).
  - Turned down as a frame's first apply, a far mesh waits one frame (`WaitedFarColumn`), then goes alone, first in its
    frame.
  - Before, a far mesh whose estimate passed 85% of the budget broke the loop every frame. The first estimate of 1,601
    quads is 1.701 ms, so it broke the loop as the frame's first apply too, and an urgent far mesh blocked the near
    meshes behind it.
- **A3: a far wall under a near overhang shows.**
  - `VoxelFarMesher::SummarizeBorder` reads the edge cells of a neighbour drawn in full into a `FVoxelFarBorder`: open
    where the cell does not cover the face, by `OpaqueFaces`, as the full mesher culls.
  - `Mesh(..., Borders)` draws the far column's wall there, below the heightfield walls.
  - Far jobs pass borders for the edges toward columns in view and record them (`MeshedCellEdges`).
  - `NeedsMesh` (used by `TryMesh` and `IsComplete`) treats a far mesh that missed an edge now in view as out of date. On
    a shifted plan the walk starts no later than the first far column beside a column that came into view.
  - Border walls stay when the neighbour leaves view. They lie inside the neighbour's far prism, so they cost quads but
    show nothing.

## Tests
- **Unnamed.Voxel.Pipeline.FarRing**
  - Case 7 (A1): a quick return after a drop.
  - Case 8 (A2): near-only peeks and takes past an urgent far mesh.
  - Every idle check (A3) requires each far mesh to have read its in-view edges cell by cell.
  - Case 2: a far column beside a column that came into view may take one extra mesh.
  - Case 6: the waiting counts are the old pins plus the far columns beside new view columns (computed; 4).
- **Unnamed.Voxel.Pipeline.ApplyStep** (A2): the loop's steps, including the 1,601-quad far mesh first in its frame.
- **Unnamed.Voxel.FarMesher case 5** (A3): the review's counterexample.
  - Ground through 80. Beside it, a neighbour with a roof at 90 over air from 61 to 89, and a cave at 30-32.
  - With the border: 184 unit faces, exactly the open cells. Without it: none.
  - A neighbour solid under its ground changes nothing.
- **Mutations**: each of the following fails its test.
  - Drop the `None` (case 7).
  - Far mesh never first (ApplyStep).
  - Ignore `Borders` (case 5).
  - Drop `NeedsMesh`'s edge term (FarRing idle check).
- **Untested guard**: removing the walk-start lowering after `CarryProgress` failed no test, ShiftPlan included. The
  column that came into view changes kind, which already stops the carry before its far neighbours. Kept as a guard.
- The first A3 design marked far columns dirty on ring moves. ShiftPlan caught it: its full and shifted paths diverged.
  It was replaced by `NeedsMesh`.

## Checks
- `TestVoxel.ps1`: 187 of 187. The mesher and far-mesher digests are unchanged; the digest meshes without borders.
- `RunChecks.ps1 -Profile final -Flight smoke`: green, 9 of 9 hashes equal
  (`Tests/Evidence/2026-10-13/Checks-final-20261013-114223`).
- Two Sonnet reviews found nothing serious.

## Open
- **Far-hole frames rose in the smoke flight**: 3,168 to 4,479, with coarse frames 4,353 to 4,872. The worst frame is
  5.47 to 5.35 ms and near holes are none in both.
- These two flights ran on a loaded machine. A quiet-window A/B (f0fde83's DLL against this one, ABABAB) decides whether
  A3's extra far meshes explain it; see the handoff.
