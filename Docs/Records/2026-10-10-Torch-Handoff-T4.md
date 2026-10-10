# Torch handoff: after T3 (the look round, then particles)

Start here for the next torch work. T3 (placeable torch with light) is done on `feature/voxel-t3`; its record is [2026-10-10-Torch-T3.md](2026-10-10-Torch-T3.md). Nothing in T3 changed the default game's look: the torch lights are live only with MegaLights on, and every look option is a switch at today's value.

## Reading list

1. [2026-10-10-Torch-T3.md](2026-10-10-Torch-T3.md), all of it: what was built, the gates, the look pack and the known issues.
2. [2026-10-10-Torch-Light-Probe.md](2026-10-10-Torch-Light-Probe.md), "Method" and "What T3 needs": the capture method the gates reuse.
3. [2026-10-10-Torch-Scout-G-Research.md](2026-10-10-Torch-Scout-G-Research.md), section 2 "T4" and "T5", section 4.1 (the torch's model and texture), and section 4.6 P5 (the particle-rate probe).
4. `Tests/Probes/README.md`, only if placement changes again.
5. [Blocks.md](../Blocks.md), the torch paragraph and "Light sources".

## Where it stands

| Session | What | State |
|---|---|---|
| T3 | Placement by look, survival, drop, the light pool, the gates | done; the look round waits for Jesus's pick |
| **T3 look round** | **Apply Jesus's pick (warm A) and, if he confirms, MegaLights on** | **next (short)** |
| Particles | A flame-and-smoke emitter at pooled torches | after the look round (Jesus: "later, not in T3") |
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
- The lights are presentation only. The pool is live only while `r.MegaLights.EnableForProject` is 1. `Config/` stays at 0 until Jesus decides.
- Particles and a held light are not in T3 (Jesus).

## The look round (next)

Jesus's direction: "It shouldn't look blown out. I want it to give.. warm look like you would see in tomb raider. You should still see the texture under it."

The pack (paths in the T3 record, section 4) compared today's look with two warm options at the same views (the torch room with 1 and 4 torches, a torch crop, the probe cave with 10 torches), with MegaLights on and off.

**Jesus's decisions (confirmed through the manager, October 10, 2026):**
- (a) Warm A (600 cd, 2,200 K, inverse square), with MegaLights on for the whole project (`r.MegaLights.EnableForProject=1`). He weighed the cost: "it's worth it".
- (b) Only the flame glows. The stick shows its real texture (vanilla's brown) with no glow. This is torch-only: glow lichen stays as is.

Then:

1. **Defaults:** in `Source/Unnamed/Voxel/VoxelLights.cpp`, set the file-local cvars' defaults:
   - `voxel.TorchGlow` 14 -> 1 (warm A), or 0 for the stick once the flame glows on its own (b);
   - `voxel.LightIntensity` 1000 -> 600;
   - `voxel.LightTemperature` 3000 -> 2200;
   - `voxel.LightFalloff` stays 0.
   - Update `Docs/Blocks.md` "Light sources" and the tests that pin those defaults (`Voxel.Lights`, `Voxel.LightPool`).
2. **The glow change** alters the default (MegaLights off) look too: a low-glow torch lights nothing without MegaLights. That is why it goes with (a).
   - It is torch-only and changes no golden, because no generated world holds a torch.
   - Re-run TestVoxel (Mesher.Digest) to prove it.
3. **MegaLights project-wide** (confirmed, a): `r.MegaLights.EnableForProject=1` in `Config/DefaultEngine.ini` [/Script/Engine.RendererSettings].
   - Cost with no lights: +0.13 ms GPU and about +1.4 ms of game thread.
   - Cost with torches: +0.6 / +0.9 / +1.45 ms GPU at 1 / 10 / 100.
   - Re-measure the Playground probe FPS and the T3 gates (`CaptureVoxel.ps1 -TorchGates`, quiet window), because the floor now applies everywhere.
4. **Flame-only (b), confirmed:** the flame texels keep a glow and the stick gets 0. The routes:
   - **Element light (most likely):** S6b's per-quad element light, the firefly bush's overlay path (Blocks.md "Model meshes": UV 1 bits 16-19, "emit the brighter of the state's light and the element light").
     - Split the torch's quads so the flame part, the top 2 pixels of each side plus the top face (UV rows 6-8), carries an element light. The stick part gets none, and the torch state's glow goes to 0.
     - The split happens where the torch's model quads are built, for both the voxel table and the Playground's model meshes.
     - It changes the torch's quad count, so `Blocks.ModelMeshes` (2,440 baked, 2,424 drawn) and the mesher's model pins move.
   - **A per-texel emissive mask in the chunk and block materials:** Content, shared with every emissive model block. Lichen must not change.
   - Both touch shared code or pinned counts. **Plan first, list the files and the pins that would move, and stop before committing.** A pinned count, golden or `Mesher.Digest` that moves needs the manager's go; this lane does not re-record them.
   - `voxel.TorchGlow 0` alone (the stick lit only by its light, the flame dark) is the free fallback to show beside it: `-TorchLook -TorchGlow 0`.
5. **Re-run** the look capture (`CaptureVoxel.ps1 -TorchLook`) after the change, plus the Playground probe and the e2e checks (create-world, flight).
6. **Merge (one, for T3 and T3b together):** `feature/voxel-t3` was not merged in T3. T3b commits its change on `feature/voxel-t3` when green. Then, on the manager's go and after S9a has committed, it merges `feature/voxel-world` in once, never with a rebase or force.
   - Rebuild with `-DisableAdaptiveUnity`.
   - Run every suite once, plus the radius-8 hashes and `Mesher.Digest`, and send one combined report.
   - `Scripts/TestVoxel.ps1`'s expected count becomes main's count plus T3's 2 (`Voxel.Lights`, `Voxel.LightPool`). Say how you got it.
   - `Docs/VoxelWorld.md` and the records index are the main lane's. Add T3's lines (the light pool in VoxelWorld.md, this record and handoff in the index) after the merge.

Options Jesus may ask for that T3 did not build:
- **Flame-only emission:** the 4 flame texels glow and the stick doesn't. It needs a per-texel mask in the chunk materials or split torch quads in the mesher. Either touches shared code: glow lichen goes through the same path. Ask before touching it.
- **Exposure:** caves and the torch room sit on the project's auto-exposure floor (EV100 6; the read-back exposure scale there is 0.0078125 = 2^-7, which T3's notes called "EV 7"). A darker or brighter cave changes the whole project's look.

## Particles (after the look round)

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
- **The pool:** `Source/Unnamed/Voxel/VoxelLights.*` (`FVoxelLights`): every `voxel.Light*` cvar, `voxel.TorchGlow`, `voxel.LightsInfo`, the falloff option and the torch-glow table copy.
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

- **The torch glows white with MegaLights off:** S6's whole-model emission at 14. It is today's default until the look round.
- **The same root makes glow lichen read near-white** at cave exposure. Untouched by T3; it needs its own look call (main lane or a look session).
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

1. **The torch look:** warm A, confirmed by Jesus.
2. **MegaLights project-wide:** on, confirmed by Jesus ("it's worth it").
3. **Glow lichen's white glow:** a look call for the main lane, with the same options as the torch (a lower glow level, or a texel mask).
4. **Flame-only glow** (confirmed by Jesus): which route?
   - (a) split torch quads carrying an element light (the pins move);
   - (b) a material mask (shared materials).
   - *Recommendation:* (a). It is torch-only, and its pin moves come from a known source. Plan it and send the manager the list of pins that move before you build.
