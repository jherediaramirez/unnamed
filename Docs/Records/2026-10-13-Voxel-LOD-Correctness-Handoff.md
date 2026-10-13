# Far LOD correctness handoff: A1-A3 done, the far-hole A/B next

2026-10-13 · `feature/voxel-lod3`. Record: [Far LOD correctness](2026-10-13-Voxel-LOD-Correctness.md).

## State
- A1, A2 and A3 are committed, one commit each. The checks are green and the hashes equal.
- The merge of `feature/voxel-world` waits for the far-hole A/B.

## Next: the A/B
- Compare f0fde83's DLLs with A3's, the same smoke flight (`CaptureVoxel.ps1 -Flight`), alternated ABABAB in one
  quiet-window-gated chain.
- Per run, report:
  - far-hole, coarse and near-hole frames
  - the worst chunk-work frame
- If A3 explains the rise, the likely levers are:
  - **Fewer far remeshes on a move.** Only far columns that read the edge as a heightfield and now border an open cell
    matter; most borders are solid.
  - **Merged border wall runs.** Today they go per cell, so there are more quads per far mesh.

## Not this pass
Larger radii, renderer or batching redesign, review findings A4-A6, tree proxies, the fog seam.
