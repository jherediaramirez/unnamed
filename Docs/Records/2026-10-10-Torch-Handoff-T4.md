# Torch handoff: after T3 (the look round, then particles)

Start here for the next torch work. T3 (placeable torch with light) and T3b (the look round) are done on `feature/voxel-t3`. Their records are [2026-10-10-Torch-T3.md](2026-10-10-Torch-T3.md) and [2026-10-10-Torch-T3b.md](2026-10-10-Torch-T3b.md).

T3b changed the default look:
- MegaLights is on for the whole project.
- The torch lights are warm A (600 cd, 2,200 K).
- The torch glows at level 1 instead of 14.
- The flame's quads are cut from the stick, so a flame-only glow is one switch away (`voxel.TorchGlow 0`).

## Reading list

1. [2026-10-10-Torch-T3b.md](2026-10-10-Torch-T3b.md), all of it: the look round, why the stick is tan for now, and T3c.
2. [2026-10-10-Torch-T3.md](2026-10-10-Torch-T3.md), all of it: what was built, the gates, the look pack and the known issues.
3. [2026-10-10-Torch-Light-Probe.md](2026-10-10-Torch-Light-Probe.md), "Method" and "What T3 needs": the capture method the gates reuse.
4. [2026-10-10-Torch-Scout-G-Research.md](2026-10-10-Torch-Scout-G-Research.md), section 2 "T4" and "T5", section 4.1 (the torch's model and texture), and section 4.6 P5 (the particle-rate probe).
5. `Tests/Probes/README.md`, only if placement changes again.
6. [Blocks.md](../Blocks.md): the torch paragraph, "Model meshes" (the flame cut) and "Light sources".

## Where it stands

| Session | What | State |
|---|---|---|
| T3 | Placement by look, survival, drop, the light pool, the gates | done |
| T3b | The look round: warm A, MegaLights on, the flame cut; the default glow 1 (manager's call B) | done |
| **T3c** | **The brown stick: a glow under level 1 for the torch's stick, so `voxel.TorchGlow 0` can become the default** | **next (short), after S9b's commit: it touches `VoxelMesher.cpp`, which S9b edits** |
| Particles | A flame-and-smoke emitter at pooled torches | after T3c (Jesus: "later, not in T3") |
| T4 | Held light (a setting) | only if Jesus asks for it |
| T5 | Block-light field (gameplay light: spawning, melting) | later |

## Working agreement (as in T3)

- **Reporting:** report to the manager chat named in your prompt. Plans go first, with every file listed against the main lane's dirty files. A commit needs a clean Sonnet 5.5 review of the diff and passing suites. The manager pushes.
- **Look calls:**
  - Anything that changes the default game's look (the torch glow, the light's look, MegaLights project-wide) is Jesus's call, sent through the manager with captures, numbers and valid control frames.
  - Keep the other option one switch away.
- **Game behaviour** comes only from the game's data, the wiki and black-box runs of the public API (`javap -public` is fine; no bytecode bodies, no reflection into internals).
- **Timed runs** need a quiet window. Check `Get-Process java, javaw, UnrealEditor*` and log the machine state with every figure. Alternate old and new DLL sets when comparing (T3's `gate-chain.ps1` shows how).
- **Builds:** finish every code change with a `-DisableAdaptiveUnity` build. T3 hit two unity-only name clashes: a parameter named `Sources` against `VoxelFeatureReader.cpp`'s file-scope array.
- **Subagents:** one deliverable of about 100k each. T3's Opus agents overshot to 150-230k whenever their prompt held several deliverables.

## Settled decisions

- Torches place, survive and drop as measured. Probe P3's matrix lives in `Tests/Probes`, not in the blocks reference (Jesus, Q1 (a)).
- One MegaLights point light per light source at level 10 or more: torches, glowing cubes and emitting models, never lava. The pool takes the nearest 64 within 48 blocks (at most 100) and merges sources under 2 blocks apart.
- The lights are presentation only. The pool is live only while `r.MegaLights.EnableForProject` is 1, which is now the project's setting (Jesus, T3b: "it's worth it").
- Warm A for the lights: 600 cd, 2,200 K, inverse square (Jesus, T3b).
- Only the flame glows, the stick in its real brown with no glow (Jesus, T3b). Built as the flame cut (`FoundationBlockMeshes::GetDrawnModelQuads`). It is not the default yet: the default glow is 1, warm A as Jesus saw it (manager's call B), until T3c gives the stick a glow under level 1.
- Particles and a held light are not in T3 (Jesus).

## The look round (done in T3b)

Jesus decided both looks after seeing T3's look pack: warm A with MegaLights on for the whole project, and only the flame glowing. He accepted the MegaLights cost (+0.13 ms GPU and about +1.4 ms of game thread with no lights) as "worth it". T3b built both. The flame-only stick came out black in a cave, so the default is warm A's whole-torch glow 1 for now. The details are in [the T3b record](2026-10-10-Torch-T3b.md).

## T3c: the brown stick (next)

- **Goal:** `voxel.TorchGlow 0` (only the flame glows) with the stick reading its texture's brown at the cave exposure floor. torch.png's stick texels average sRGB 110/87/55. Then make 0 the default.
- **Why it needs code:** glow steps are whole levels. Level 1 (vertex G 17) gives the stick 202/176/126, a tan about 1.8 times the texture; 0 gives 9/3/1. Lighting cannot fill the gap: the torch quads are lit as up, so the 2 x 2 top face shadows the stick from its own light, and unshadowed the stick would be blown out.
- **Route:** a per-quad glow finer than a level, for the stick quads only.
  - **Voxel world:** a field on `FVoxelModelQuad` in vertex-G units (the core struct). `FEmitter::Model` (`VoxelMesher.cpp`) then emits the brightest of the state's G, the element light's G and that field.
  - **Playground:** the own code's element light (bits 16-19, under 2^21) has no room. Either carry the glow in UV 1's first channel next to the shade face, or widen the field and check the float's precision. Either way `CreateBlockMaterials.py` changes and `M_Block*` are re-saved (Content).
  - `GetDrawnModelQuads` sets the stick quads' glow.
- **Sweep:**
  - Run `CaptureVoxel.ps1 -TorchLook` at stick G 2, 4, 6, 8 and 11 (about 0.1 to 0.65 of a level).
  - Take the stick's mean sRGB in the crop: the box from (730, 330) to (860, 820) of `look-crop-ml1.png`, as T3b measured.
  - Pick the value nearest 110/87/55 that isn't blown.
  - Send the pick to the manager with captures, using T3b's warm A capture as the control.
- **Pins:** none should move. No generated world holds a torch, so `Mesher.Digest` stays, and the drawn quad counts stay at 2,444. Re-run `Voxel.Lights` (its flame rows) and `Blocks.ModelMeshes`.
- **When:** after S9b's commit (S9b edits `VoxelMesher.cpp`), in the main lane or as a lane after it.

## Particles (after T3c)

- **Scope** (scout G section 2, T3 "Particles"): a small Niagara flame-and-smoke emitter at each pooled torch within 16 blocks, at 2 to 4 particles per second.
- **First measure the rates black-box:** probe P5 in a vanilla client, or a public-API run if one can observe particle spawns.
- **Placement:** emitters follow the pool's picks (`FVoxelLights`), never per frame.
- **Gate:** GPU and game-thread cost at 10 and 100 torches, with T3's `-TorchGates` cases as the base.

## Code map (T3)

- **Placement:** `Source/Unnamed/Foundation/FoundationBlockPlacement.*`:
  - `GetNearestLooks` (vanilla's order, yaw wrapped to [0, 360));
  - `GetClickLooks`;
  - `GetTorchState` (the measured rule);
  - `IsTorchSupported` (the support table).
- **Survival:** `FoundationAuthority.cpp` `CanPlantSurvive` (torch-rule blocks) inside `SettlePlants`. `FoundationBlockRegistry.cpp` sets `bTorchRule` and gives a wall block its standing block's drop.
- **Pick:** `FoundationInteraction.cpp` `Pick` matches `PlacesWallBlock`.
- **The light core:** `Source/Unnamed/Voxel/VoxelLightSources.*`: the light table, the light points, `GatherSources` (the column scan) and `PickLights`.
- **The pool:** `Source/Unnamed/Voxel/VoxelLights.*` (`FVoxelLights`): every `voxel.Light*` cvar, `voxel.TorchGlow` (`DefaultTorchGlow`), `voxel.LightsInfo`, the falloff option and the torch-glow table copy.
- **The flame cut (T3b):**
  - `FoundationBlockMeshes::GetDrawnModelQuads` (`TorchFlameV`, `TorchFlameLight`, `CutAtTorchFlame`) is used by `VoxelBlocks.cpp` `CopyModels` and by `UFoundationInteractionView::GetBlockMesh`.
  - `GetDrawnLight` gives torch-rule instances `voxel.TorchGlow` in `MakeCustomData`.
- **Hooks:**
  - `VoxelColumnPipeline.*`: the mesh job gathers sources, and `SetLightTable`;
  - `VoxelWorldSubsystem.cpp`: hand-over on apply, release and stop;
  - `VoxelWorld.*`: the pool's owner, the tables, `-VoxelTorchGlow`.
- **Captures:** `VoxelLightShots.*`, driven by `Scripts/CaptureVoxel.ps1`:
  - `-Lights` (T1);
  - `-TorchGates`;
  - `-TorchRoom` (P4 room, the intensity, temperature and falloff sweep);
  - `-TorchLook` (pairs at the current settings);
  - `-TorchGlow <n>`.
- `VoxelLightProbe.*`: `voxel.LightProbe N torches`.
- **Tests:**
  - `FoundationTorchTests.cpp`: `Blocks.Torch`, `.Support`, `.TorchProbe`, `.TorchSurvival`;
  - `VoxelLightTests.cpp`: `Voxel.Lights`;
  - `VoxelLightPoolTests.cpp`: `Voxel.LightPool`;
  - interaction scenario `10c. Torches`.

## Known issues (with when)

- **The torch's stick is tan, not brown** (glow 1). When: T3c.
- **Glow lichen reads near-white** at cave exposure (whole-model emission at 7). T3 and T3b left it untouched; it needs its own look call (main lane or a look session).
- **Lit full cubes have no per-state light:** DA_Blocks imports per-state light only for model blocks. A lit furnace, lit copper bulb, lit redstone lamp or lit redstone ore reads 0, so it neither glows nor gets a pool light.
  - When: the next blocks-import session (importer change plus reimport).
- **Coral fans:** `CheckPlacement` sends every item with `PlacesWallBlock` through the torch rule, and `bTorchRule` covers every `ItemWallBlock` pair.
  - When: before the coral wall fans, signs, banners or heads are wired. Give them their own rule or a narrower marker.
- **Lava and water:** lava should destroy a torch without a drop, and flowing water should drop it.
  - When: water and lava flow (later phases).
- **`voxel.LightMinLevel`** takes effect as columns remesh (`voxel.Regenerate` for all).
- **The Playground has no torch lights** (the pool lives on `AVoxelWorld`). The Playground's 75,000 lux sun hides them anyway.
- **Merged lights** move slightly when a group's seed changes. Cosmetic.
- **Columns keep their sources while the pool is off.** Memory only, about 25 µs of scan per column with emitters.
- **The CLAUDE.md commands list** lacks `CaptureVoxel.ps1 -Lights`, `-TorchGates`, `-TorchRoom`, `-TorchLook` and `-TorchGlow`. That's the manager's call; T3 did not edit CLAUDE.md.

## What worked and what didn't

- **Worked:**
  - The in-process `GameTestServer` through `MinecraftServer.spin`, with a connectionless `ServerPlayer`: a clean public-API route to real placement behaviour. 64,896 trials took about 15 s.
  - Old/new DLL sets: `git restore --source=<old>` on Source and Scripts, a `-DisableAdaptiveUnity` build, then copy the DLLs aside. Swapping them while no editor ran gave clean A/B pairs.
  - Same-run comparisons (torches against probe lights) cancel machine drift; T1's base had moved by 0.2 ms.
- **Didn't:**
  - Byte-identical captures are impossible, even from one build (new against new: 0.23-1.0/255 mean). Compare against a same-build noise pair instead.
  - Room exposure doesn't settle: it sits on the project's auto-exposure floor (EV100 6) in every dark view.
  - Large multi-deliverable agent prompts overshot their context.

## Token figures (T3)

Lead: 418.6k at the start of the wrap-up, 475.0k at the record (67.7M processed). Subagent peaks (Opus 5.5 unless noted): probe 111k, survival 206k, placement 206k, light core 192k, pool 228k, review fixes 152k, gate cases 175k, room 168k, falloff 144k, warm look 188k; Sonnet 5.5 reviews 74k, 92k, 92k, 118k, 95k.

## Evidence

All under `C:\dev\sandbox\t3-torch\` (local):
- the look pack (`look-pack\`);
- the gates (`window-1\`);
- the control (`control\`);
- the room runs (`room-1\`, `room-2\`);
- the suites (`lead-all-unnamed-1`, `lead-testvoxel-1`, `falloff-*`);
- placement changes (`placement-changes.txt`).

The probe sandbox is `C:\dev\sandbox\t3-torch-probe\`.

## Open questions (with options)

1. **Glow lichen's white glow:** a look call for the main lane. The options are the torch's: a lower glow level, or a glow under level 1 once T3c builds one.
2. **T3c's Playground route:** UV 1's first channel, or a wider own-code field (see T3c). *Recommendation:* UV 1's first channel; it leaves the own code's precision bound alone.

## For the next session in this worktree (S7b)

What T3b changed that S7b meets:
- `Config/DefaultEngine.ini` turns MegaLights on, which costs about 1.4 ms of game thread with no lights.
- `CopyModels` copies the drawn quads, so the voxel table has 2,444 model quads (`Mesher.ModelData`).
- `MakeCustomData` reads `voxel.TorchGlow` for torches.

## Short prompt (T3b, as sent)

T3's handoff didn't contain the prompt that started T3b. Here it is, verbatim:

```text
Torch T3b: the torch look round, on Jesus's confirmed decisions. Work in the worktree C:\dev\projects\Unnamed-s5 on branch feature/voxel-t3, which holds T3's five commits (03a69d1..a1316ae), not yet merged. Read Docs/Records/2026-10-10-Torch-Handoff-T4.md in full, and sections 4 and 5 of Docs/Records/2026-10-10-Torch-T3.md.

Jesus confirmed:
1. Warm A with MegaLights on for the whole project. Warm A is voxel.TorchGlow 1, voxel.LightIntensity 600 and voxel.LightTemperature 2200, inverse square. MegaLights is r.MegaLights.EnableForProject=1 in Config/DefaultEngine.ini.
2. Only the flame glows. The stick shows its real brown texture with no glow, torch-only; glow lichen stays as it is.

Plan first. List every file, plus every pinned count, golden or hash that would move, against the main lane's dirty files; the flame-only routes are in the handoff. Send the list to the manager for confirmation. Stop before anything touches shared emission or mesher code or moves Mesher.Digest.

Then:
- Build the change.
- Take a final pair against today's look: CaptureVoxel.ps1 -TorchLook, with -TorchGlow for the old look.
- In a quiet window, re-run the T3 gates (-TorchGates) and the Playground probe.
- Commit when green.
- On the manager's go, and after S9a has committed, merge feature/voxel-world into the branch once (no rebase, no force). Rebuild with -DisableAdaptiveUnity, then run every suite plus the radius-8 hashes and Mesher.Digest. TestVoxel's expected count becomes main's count plus T3's 2; say how you got it.
- Send one combined report.

Report to the manager chat that started you.
```
