# Voxel world handoff: phase 6, into S6b (block models in the Playground) - October 10, 2026

Read this first if you are picking up the block models' Playground half. Phase 6's S6 (block models: rendering in the generated world) is done on `feature/voxel-s6` ([the S6 record](2026-10-10-Voxel-Phase6-S6.md)); the Playground part of the plan's S6 moved here at S6's 300k checkpoint (the night manager's decision). Facts were true at S6's commit; check `git status`, `git log -5` and newer records before trusting them. **Run S6b after S6 has merged into `feature/voxel-world`, and before S7** (S7's interaction scenario needs plants that render in the Playground).

## 1. Read these, and only these

Budget: about 80k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md) (its handoff pointer may be stale; this handoff wins).
2. **This handoff** and [the S6 record](2026-10-10-Voxel-Phase6-S6.md) (all of it; it is short).
3. **[Docs/VoxelWorld.md](../VoxelWorld.md):** the "Block models" section only (find it by heading).
4. **[Docs/Blocks.md](../Blocks.md):** "States and shapes" (the model blocks bullet), "Rendering", "Playground gallery"; **[Docs/Playground.md](../Playground.md)** as needed for the tour and probe commands.
5. **Code, by range:**
   - `Source/Unnamed/Foundation/FoundationBlockMeshes.*` (all; about 140 lines);
   - `Source/Unnamed/Foundation/FoundationInteractionView.cpp`: `MakeCustomData` and `MakeBlockComponent` (about lines 195-250), `AddInstance`, `RemoveInstance`, `RefreshWaterBelow` (about 253-300), the held block (about 655-690), and the header's `FSectionKey` and `DrawnKeys`;
   - `Source/Unnamed/Foundation/FoundationPlayground.cpp`: the showcase rows and galleries (about lines 100-160);
   - `Scripts/Editor/CreateBlockMaterials.py`: `UNPACK`, `BLOCK_HLSL` and `build()` (lines 30-60 and 154-268);
   - `Source/Unnamed/Foundation/FoundationTourProbe.cpp`: the gallery shots (22b-22d);
   - `Source/Unnamed/Foundation/FoundationBlockRegistry.h`: `FFoundationModelQuad`, `FoundationBlockModels`, `GetModelParts`, `GetModelQuads`, `GetLightEmission` (about lines 115-250).

## 2. Working agreement with this user

Unchanged (see [the S1b handoff](2026-10-09-Voxel-Handoff-Phase6-S1b.md), section 2): the manager (Nyx at night, Hemera by day) confirms a short plan first and approves the local commit after a clean Sonnet 5.5 review and green suites; it pushes, never you. Look calls are delegated to the manager: captures with numbers, a control pair proving the switch took effect, a recommendation; the other option one switch away. Context checkpoints at about 300k, then every 200k, measured with `MeasureAgentUsage` after every build, test run and subagent (S6 crossed 300k by 39k unmeasured; do not). Subagents: Sonnet 5.5 for reviews (split by path list over about 1,500 diff lines), Haiku 5.5 for log scraping; exact paths and line ranges; stop near 150k.

The machine: check `Get-Process java, javaw, UnrealEditor*` before every timing and state it beside each figure. Parallel lanes share the PC: never chain editor runs in one background command; respect quiet windows.

## 3. Where things stand

- **The voxel world draws every model state** (S6): baked quads in the chunk mesh's fifth section, offsets (`VoxelModels::GetOffset`, X and Y only), one alternative per part (`VoxelModels::PickAlternative`), the AO rule, tints per cell (dry foliage included), emission, water around water-holding states. `CaptureVoxel -Garden` shows every model block in `L_Generated`.
- **The Playground draws none.** `UFoundationInteractionView::AddInstance` returns early for `EFoundationBlockShape::Model`, the held block hides model blocks, and `AFoundationPlayground`'s galleries skip them (`IsModel`), so their layout is unchanged since S5.
- **Data:** `FFoundationBlockRegistry::GetModelParts` and `GetModelQuads` give the baked quads in Unreal block space and FaceInfo order, already wound like the cube faces (S6's `Mesher.ModelData` checks all of them against the cube convention). 16 quads repeat an earlier one of their model exactly; the voxel core drops them, and the Playground may too.

## 4. S6b: what to do

The design S6 worked out (not built):

- **Meshes:** `FoundationBlockMeshes` builds one transient `UStaticMesh` per baked model on first use. Each quad's normal and tangent follow its shade face. UV 0 is the quad's texture UV. UV 1 is (shade face, the quad's own code), where the own code holds the texture, the tint bit and the element light, with a marker bit, packed under 2^24 so the float is exact. Cube meshes keep UV 1's second channel at 0, so they read exactly as today.
- **Materials** (`CreateBlockMaterials.py`, the `M_Block*` four only):
  - when UV 1's second channel carries the marker, the face's code is the vertex's own, with no UV transform, instead of the per-instance face code;
  - the emission's light is the brighter of custom data 8 (the state's light) and the vertex's element light (the firefly bush's overlay);
  - the custom node's `Face` input becomes UV 1's two channels.
  - Re-running the script re-saves all eight block and chunk materials (official script, as S5). The chunk materials must come out unchanged in behaviour.
- **Instances:** one per part, each part's alternative picked and the whole state moved with S6's two functions, so the Playground and the voxel world agree cell for cell.
  - `FSectionKey::Mesh` widens from `uint8` to `uint16`: model meshes come after the cube kinds.
  - `DrawnKeys` must hold several keys per cell, for multipart states (vines, flowerbeds).
  - Custom data stays as today: the tint in 7 and the state's light (`GetLightEmission`) in 8.
  - The layer is the block's own, so opaque-texture models such as moss carpet use `M_BlockOpaque`.
- **Water-holding states:** drawn as the model only in the Playground, or with a water instance around them. Pick one and say so. Placement rules (kelp only in water) are S7's.
- **The held block:** its default state's model, centred like the cubes.
- **Galleries and showcase:** remove S5's skips (`AddInstance`, the held block, `IsModel` in the galleries), so every model block and state joins the registry and state galleries. Add a plant row to the showcase. Update the tour's gallery shots if the layout moves.
- **Plant shadows:** a decision for Jesus, below. Do not build an option before he answers.

**Checks** (the night manager's conditions, carried over):
1. **Cubes render the same after the material re-save.** Capture a cube-only view (no model blocks) before and after, plus a noise baseline (the same build captured twice), and report the % of pixels differing by more than 2/255 for both pairs. The re-saved assets must be newer than the final edit of `CreateBlockMaterials.py`.
2. After the final build: the Playground probe (`TestFoundationRuntime.ps1 -Editor -FrameRates 60`), the interaction suite (`-Suite interaction`), the tour (`-Suite tour`), `create-world.mjs --editor`, and the native suites `Unnamed.Foundation` and `Unnamed.Hud`.
3. The probe's FPS is not lower beyond noise with the galleries grown (old and new alternated, quiet machine).

## 5. A decision for Jesus: plant shadows

He answered "plants cast shadows near the camera (yes)". S6 found that the pinned RealtimeMesh draws one mesh batch per buffer set, with its shadow flag the OR of its sections' (`RealtimeMeshBufferSetProxy.cpp:290`), so a per-section switch cannot work. Plants therefore cast shadows at every distance. G2 control: switch on and off differed at noise (0.24% against 0.06%), and the whole cost of the plants, shadows included, is +0.30 ms in the plant-dense field view.

| Option | What | Cost |
|---|---|---|
| (a) Accept shadows at every distance | Nothing to build | None now; already inside +0.30 ms |
| (b) Model quads in their own component per column | A second RealtimeMesh component per column with model geometry only; shadow (and ray tracing) per component | About half a session: the pipeline builds two stream sets, the subsystem pools two components per column, and memory and draw cost need a flight A/B. Also opens plants leaving the ray-tracing scene, if a look ever wants it |
| (c) Shadow-pass distance fade in a model material | A model-only material (`M_ChunkModel`) whose opacity mask clips in the shadow pass beyond N metres from the camera | Small: one material and the fifth slot's name. Verify that the shadow-pass camera position is the view's under virtual shadow maps, which is unverified. Masked shadow cost stays near the camera |

**Recommendation:** (a) now, since the cost is measured and small, and (c) if a look call later wants far plants unshadowed.

## 6. Known issues, each with when it is due

| Issue | Due |
|---|---|
| The Playground draws no model state; galleries skip them | S6b |
| Plants cast shadows at every distance | Jesus's choice above |
| The 30-frame jitter shimmer test (S6's G1) | Open |
| Sea pickles and glow lichen glow near white in daylight (chunk emission is scaled for lava) | A later look pass |
| Model blocks place their default state; no support or facing rules | S7 |
| Generation places no model block | The decoration features that place them (S8) |
| Shelf mushroom's 32 x 32 textures hold vanilla's mip 1 | Kept (Jesus's answer 3, October 9); look up close when the Playground draws it |

## 7. Commands

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=C:\dev\projects\Unnamed\Scripts\Editor\CreateBlockMaterials.py" -unattended -nop4 -nosplash   # absolute -script= path
& Scripts\CaptureVoxel.ps1 -Garden [-ExecCmds 'r.EyeAdaptation.MethodOverride 3,voxel.Models 0']   # S6's garden in L_Generated; fix exposure for comparisons
& Scripts\TestFoundationRuntime.ps1 -Editor -Suite tour -FrameRates 60
node Scripts\MeasureAgentUsage.mjs --session <id>   # with CLAUDE_CONFIG_DIR pointing at a copy (C:\dev\sandbox\s6work\measure.ps1 shows how)
```

## 8. The short prompt for S6b

> Voxel world phase 6, session S6b: block models in the Playground. S6 (block models in the generated world) is done and merged into `feature/voxel-world`; S6b runs before S7.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-10-Voxel-Handoff-Phase6-S6b.md` first; its section 1 is the whole reading list (with line ranges), so keep to it.
>
> Build S6b as the handoff's section 4 says: one runtime mesh per baked model, built on first use, wound and shaded like the voxel world's; the `M_Block*` materials read a vertex's own code and light (cubes unchanged); one instance per part with S6's offset and alternative functions; the held block; S5's skips removed and the galleries and showcase grown; the tour shots updated. Prove cubes render the same after the material re-save (before/after with a noise baseline), then run the probe, the interaction suite, the tour and create-world after the final build.
>
> Send the manager a short plan (files and why) before building. Section 5 is a decision for Jesus (plant shadows); bring it to the manager, do not build an option first. Report as the handoff's section 2 says, and measure your context after every build, test run and subagent. End with a handoff into S7 (plant interaction and collision), whose handoff already exists: update it, do not replace it.
