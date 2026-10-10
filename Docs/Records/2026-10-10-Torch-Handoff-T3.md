# Torch handoff: into T3 (placeable torch with light)

Start here for the torch work. T2 (torch data) is done on `feature/voxel-light`; see [2026-10-10-Torch-T2.md](2026-10-10-Torch-T2.md). T0 + T1 (the light probe) is in [2026-10-10-Torch-Light-Probe.md](2026-10-10-Torch-Light-Probe.md). Scout G's research and draft plan is [2026-10-10-Torch-Scout-G-Research.md](2026-10-10-Torch-Scout-G-Research.md). Read its section 2 "T3" and section 4.1 (the torch's model and texture), then the probe record's "What T3 needs", then this file.

## Where it stands

| Session | What | State |
|---|---|---|
| T0 + T1 | Glow-cube capture and the local-light probe | done; rung 1, a MegaLights point light per torch |
| T2 | Torch data: reference, support table, item | done |
| **T3** | **Placeable torch with light** | **next, once S7 (placement and survival) has merged** |
| T4 | Held light, a setting (optional) | any time after T1, if Jesus wants it |
| T5 | Block-light field | later, or sooner if T3's light system fails |

T3 needs S6 (model rendering, merged), S7 (survival and its pop cascade) and T2. Check that S7 is in `feature/voxel-world` before starting. Then read S7's record for its survival API: T3's torch survival should join S7's cascade, not build a second one.

## What T2 gives T3

- **Blocks:** `torch` (state 4926) and `wall_torch` (4927-4930, `facing` north/south/west/east) are in `DA_Blocks` as model blocks, at light 14.
  - Their models are 303 (floor) and 304-307 (wall, one per facing).
  - Outline boxes come from the facts: floor 0.375-0.625 x 0-0.625; wall 0.34375-0.65625 wide, 0.1875-0.8125 high, 0.3125 deep against its wall. Collision is empty.
  - Hardness 0, so they break instantly.
- **Support:** `FFoundationBlockRegistry::GetTorchSupport(State)` gives a face mask per state, a bit per `EFoundationFace`: Up holds a floor torch, a side a wall torch facing away, Down never. It comes from the reference's `facts/block_support.json`, the torch's own `canSurvive` measured through the public API. `Unnamed.Foundation.Blocks.Support` checks every state against it.
- **Item:** the `torch` item carries `PlacesWallBlock = wall_torch`.
  - `FoundationBlockPlacement::GetItemBlock` picks the block by clicked face.
  - `HasTorchSupport` checks the support for the placed state.
  - `FFoundationAuthority::CheckPlacement` refuses with `CannotSurvive` when the support is missing, and always on an underside click.
  - `wall_torch` uses `EFoundationPlacementRule::WallAttached` (facing the clicked side).

## What T3 has to add on the T2 side

1. **Underside clicks (Nyx's answer Q3 for T2: refused until T3).** Vanilla places on another valid face, chosen by the item's look directions; scout G's probe P3 covers the order.
   - Measure it black-box: a headless 26.3 server run (`Reference/minecraft-worldgen-26.3/tools/feature_probe.py` drives one). Never read game code.
   - Then extend `GetItemBlock` / `CheckPlacement`, and drop the underside refusal and its test line in `FoundationTorchTests.cpp`.
2. **Survival:** a torch pops when its support goes. Use S7's one-level cascade, with `GetTorchSupport` as the torch's rule.
3. **Drops:** a broken or popped wall torch should drop a `torch` item. The provisional rule drops a block's own item, and `wall_torch` has none; its manifest entry records `vanilla_item: torch`.
4. **Replaceable clicks:** clicking the side of short grass puts the wall torch in the grass cell and checks the cell behind its facing. Confirm that against vanilla in the same probe as point 1.
5. **Not needed for the torch:** the coral fans' items also have wall blocks in `block_support.json` `items`, but the importer leaves them unwired until their own attachment is measured.

## The light-source system (from T1's numbers)

- **The pool:** the nearest K sources within R blocks. Start at K = 64 and cap it at 100. T1 measured +0.78 ms for 10 and +1.39 ms for 100 clustered MegaLights lights. R = 48 blocks. Merge sources closer than 2 blocks.
- **Each light:**
  - attenuation radius 1,300 cm (level 14 minus 1, in blocks), source radius 5 cm, inverse-square falloff;
  - intensity tuned in the capture, expected several times below the probe's 8,000 cd;
  - placed at the flame: a floor torch about 0.7 blocks up, a wall torch offset from its wall.
- **Lifecycle:** lights update when columns mesh or unmesh and when the authority applies an edit, never per frame. Presentation only, never in the authority.
- **Rendering:** the torch quads go through S6's model path. Its "second component, Visible in Ray Tracing off" keeps them out of shadows and ray tracing. The whole torch emits at 14 today (S6's rule). Switch to a flame-only overlay (the 4 flame texels, scout G 4.1) if the stick reads as glowing.
- **Particles (scout G Q5):** a small flame-and-smoke emitter within 16 blocks, or right after T3.
- **Measuring:** `Scripts\CaptureVoxel.ps1 -Lights` and `voxel.LightProbe` (the probe record). Reuse the probe cave, or build scout G's P4 room. Timings need Nyx's or Hemera's quiet window.
- **Gates** (scout G section 2):
  - a cave capture at 1, 10 and 100 placed torches, within 0.3 ms of T1 at the same N;
  - the leak test (T1's geometry);
  - mesh per column unchanged within noise;
  - Playground probe FPS not lower;
  - at most 0.2 ms of game-thread time per torch edit;
  - the look call against a vanilla screenshot;
  - Mesher.Digest and the radius-8 hashes unchanged.

## Gaps and known issues carried

- Since T2's merge with `feature/voxel-world` (S6b and the ore looks), the Playground draws model blocks too. A placed torch therefore shows in both worlds, with its whole model emitting at 14 in the voxel world. S6b's `Blocks.ModelMeshes` pins moved with the torch's 30 quads (2,440 baked, 2,424 drawn).
- `Docs/VoxelWorld.md` still says the voxel table copies 303 baked models; it is 308 since T2. The main lane owns that doc.
- FactsDump's `creative_tabs.json` cannot be re-dumped byte for byte: component patches print Java identity hashes. T2 kept the accepted file after a masked comparison.
- The `CLAUDE.md` commands list still lacks `CaptureVoxel.ps1 -Lights` (from T1; the manager's call).

## Questions for Jesus (still open)

1. **Glowing cubes and T3's light system:** should light-15 cubes get a pooled point light too? *Recommendation (T1):* yes, for every placed source at level 10 or more, never lava.
2. **MegaLights for the project:** T3 has to turn on `r.MegaLights.EnableForProject`, which costs +0.12 ms with no lights. That changes the project's default rendering, so it needs his yes.
3. **Scout G Q3 and Q5:** a held light as a setting, and particles in T3.

## Next

T3 as above, after S7 merges. T4 can run at any time if Jesus wants a held light.
