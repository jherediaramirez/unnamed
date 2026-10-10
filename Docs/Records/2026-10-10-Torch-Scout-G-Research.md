# The torch and light in caves: facts and a draft plan

Scout G, October 10, 2026 (about 01:00 to 02:30). Read-only research for the Unnamed game (`C:\dev\projects\Unnamed`, branch `feature/voxel-world` at `8f91b1f`). Jesus asked for "a torch. Especially for the caves."

**Supersedes items plan Q4 for the torch.** On October 9 Jesus accepted "torch as an item only, no placement, while blocks stay full cubes" (`Docs/Records/2026-10-09-Items-Phase-Plan.md` Q4; scout F's tier B). His new direction is a real, placeable torch that lights caves. The item part of Q4 still holds: icon, stack 64, creative tab. Placement and light are a block-and-renderer project that waits for S6 (model rendering) and S7 (placement and survival).

Sources: the game's data (the 26.3 jar's blockstates, models, textures and recipe; our blocks reference measured through the server's public API), minecraft.wiki, Epic's UE 5.8 documentation and the local UE 5.8 engine source (cvar defaults only). No Minecraft code was read. Detail notes from three research agents are in `notes/`. Links are in [Appendix A](#appendix-a-links).

---

## 1. Summary and recommendations

1. **Glowing cubes already light caves in `L_Generated`.** No session is needed to try it. Glowstone, sea lantern, shroomlight, the three froglights and jack o'lantern are already in `DA_Blocks` at light 15, and crying obsidian at 10. Each has a creative item that places a glowing state. In chunks their light becomes emission (vertex G = light x 17; the chunk material's emissive is colour x light / 15 x 8000). Lumen cards cover the opaque and cutout layers, so that emission enters Lumen's surface cache and lights nearby walls, as still lava's does ("the walls glow", phase 5 S3b). **Not yet measured:**
   - how far a single cube's light reaches;
   - whether a cube still lights the walls once it is off screen (the "hidden faces in a card" limit);
   - how long the light takes to appear after placement, and how noisy it is.

   The Playground keeps block-material emission 4, so glowstone barely glows there.
2. **A torch cannot light a cave through emission alone.** The flame is the top 2 x 2 pixels of the texture, a 12.5 cm cube. Its emitting area is about 1/80 of a glowstone block's. Epic documents small, bright emitters as noisy, and says small objects are culled from Lumen's scene, so only screen traces see them. Scout D's rule R4 also keeps model quads out of the cards. The light would fade whenever the torch left the screen. **Emission gives the flame its look, and a real light has to light the cave.**
3. **Recommended renderer: one UE point light per torch, with MegaLights ray-traced shadows (option b+), plus emissive torch quads for the look.**
   - **Shadows are required.** An unshadowed 13-block light leaks through stone into neighbouring caves and onto the surface above a shallow cave.
   - **MegaLights gives fixed-cost, ray-traced shadows** against the ray-tracing geometry our chunks already have (`Docs/VoxelWorld.md`: "with hardware ray tracing the chunks are in Lumen's scene anyway").
   - **VSM shadows cost a lot per light here.** A point light is six cube faces, our chunks are non-Nanite, and block edits invalidate cached pages.
   - **Lumen still bounces the light.** Its surface-cache lighting picks up to 8 lights per card tile (`r.LumenScene.DirectLighting.MaxLightsPerTile` 8 in 5.8).
   - **Status in 5.8:** the engine's cvar text still says "Experimental", `r.MegaLights.EnableForProject` defaults to 0, and Epic's 5.8 news says "entering Production Ready".
   - **Not proven on this PC.** No published per-light millisecond figures exist, so a probe session must measure it first (T1).
4. **The fallback, and the long-term complement, is a vanilla-style block-light field (option c).** It is a 0-15 flood fill stored at 4 bits per block, baked per vertex and added as an emissive term.
   - **Strengths:** it is exactly vanilla's reach, it bends around corners as vanilla's light does, and it costs nothing at render time.
   - **Gameplay needs it anyway:** mob spawning needs block light 0, snow melts at 12 or more, ice at 11 or more.
   - **It scales.** Worldgen will place thousands of light sources (glow lichen at 7, glow berries at 14, amethyst), and per-source point lights cannot cover those.
   - **Costs:** a remesh of up to 27 sections per torch edit, a flat look (no normals or specular), and some double bounce through Lumen.
   - **When:** build it when mobs or worldgen light sources need it, or sooner if T1 fails.
5. **How many torches a cave holds is unknown.** Nobody publishes millisecond figures for this. The cost structure:
   - MegaLights has a fixed floor ("a few ms" at 1080p to 1440p, an unmeasured vendor claim) and then grows with the number of lights overlapping each pixel. Large radii and clusters of torches hurt; Epic says to merge clustered lights and narrow their attenuation.
   - VSM point lights cost per light.
   - Unshadowed lights are cheap but unusable here, because of the leaks.

   T1 measures 0, 1, 10 and 100 lights in one cave.
6. **Held light (a light follows the player holding a torch) is not vanilla.** Java and Bedrock have none. It is mod territory: LambDynamicLights is off by default. Technically it is one movable point light, nearly free with MegaLights, and an S session. Recommendation: a "Dynamic lights" setting, off by default, after T1. It is not the interim, because the glowing cubes already are one.
7. **Vanilla torch facts that matter:**
   - **Blocks and light:** two blocks, `torch` and `wall_torch` (`facing` n/s/e/w), both light 14. Level 1 reaches 13 blocks in taxicab distance. Soul torch 10, copper torch 14, redstone torch 7 lit, lantern 15 (standing or `hanging`).
   - **Placement:** on the top face or a side face of a supporting block, never the underside. A click on the underside snaps to another valid face.
   - **Supports:** leaves are invalid supports even though they are full cubes in our set. Glass, fences, walls and upside-down slabs or stairs are valid.
   - **Behaviour:** no collision, breaks instantly, pops as an item when its support goes.
   - **Particles:** flame and smoke.
   - **Item:** `torch` x4 from coal or charcoal over a stick. It is first in the Functional Blocks tab.

## 2. Draft plan

Sessions are named T0 to T5 so they don't clash with phase 6's S numbers. Sizes use the phase 6 plan's S/M/L.

| Session | What | Size | Needs | Can start |
|---|---|---|---|---|
| T0 | Glow-cube cave capture (and Jesus can try it in game today) | S | nothing | now |
| T1 | Local-light probe: MegaLights vs VSM, 0/1/10/100 lights in a cave | M | nothing | now (merge with T0 as one session) |
| T2 | Torch data: torch family into the blocks reference, facts, item | S to M | S5 tools (done) | any time; best after S6 lands |
| T3 | Placeable torch with light | M to L | S6, S7, T1, T2 | after S7 |
| T4 | Held light, a setting (optional) | S | T1 | after T1 |
| T5 | Block-light field for gameplay (spawning, melting), optionally rendered | M to L | mobs or worldgen light sources | later; sooner if T1 fails |

### T0 + T1. Light probe session (recommended as one session, now)

Both are captures in `L_Generated` with no new blocks, and the renderer decision (Q1) should be settled by numbers before T3. They need no S6 or S7.

- **Scope:**
  - **`CaptureVoxel.ps1 -Lights`** (new switch, the same pattern as `-Edits`, which carves through the authority): in the `cave` view, place glowing cubes through `FFoundationAuthority`, glowstone first, then sea lantern, shroomlight, ochre froglight and jack o'lantern in turn. Shots after 0, 30 and 120 frames, plus an "away" shot with the camera turned so the cube is off screen but its lit wall is still in view.
  - **`voxel.LightProbe <N> [megalights|vsm|none]`** (new console command, development only): spawns N movable point lights at deterministic cave-floor cells near the camera, each 1 m above the floor, attenuation radius 1,300 cm, small source radius. "none" means unshadowed. Lights are pooled and removed by `voxel.LightProbe 0`.
  - **MegaLights on for the run only:** `r.MegaLights.EnableForProject 1` through `-ExecCmds` (or a PostProcessVolume override). The project default stays off until Q1 is answered.
- **Captures and numbers** (1600 x 900, the phase 5 method, control pairs, Minecraft closed):
  - **Glow cubes:** mean sRGB of wall patches 2, 5 and 10 blocks from the cube, before and after. Lag in frames until within 2/255 of the settled value. The away shot against the on-screen shot: a drop of more than 3/255 means the surface cache doesn't see the cube (hidden-face cards), so it lights only while on screen. Two consecutive frames' difference (noise). GPU ms before and after.
  - **Local lights:** GPU ms (`stat gpu` mean over 120 frames; `ProfileGPU` once per case for the MegaLights, ShadowDepths and Lumen passes) at N = 0, 1, 10 and 100 for each of megalights, vsm and none. N = 0 with MegaLights on gives its fixed floor.
  - **Leak test:** a light in a cave 3 blocks under the surface; the surface patch above changes by no more than 1/255 with shadows, and by how much with none.
  - **Edit test:** mine the block next to a light, then count frames until the shadow is correct (VSM page invalidation against ray tracing).
  - **Noise test:** temporal standard deviation of a static view over 30 frames, per mode at N = 10 and 100.
- **Gates (proposed, for Jesus to adjust):**
  - **Glow cubes:** a cave wall 5 blocks away rises at least 20/255. If the away-shot drop is over 3/255, record it as a known limit, and the T3 light system then covers glowing cubes too.
  - **MegaLights:** passes if N = 10 adds at most 1.0 ms and N = 100 at most 2.5 ms over N = 0 lights with MegaLights off, the floor included, and noise at N = 10 is within 2x of the N = 1 case.
  - **VSM:** it is the choice if MegaLights fails and VSM's N = 10 adds at most 1.5 ms. A pool of the nearest 8 to 16 shadowed lights is then the T3 design.
  - **Neither:** if both fail, T5's field becomes the renderer and point lights are dropped.
- **Files:**
  - `Scripts/CaptureVoxel.ps1`;
  - the VoxelShot code that handles `-VoxelShotEdits` and `-VoxelShotCaves` (game module, `Source/Unnamed/Voxel/`);
  - a small `voxel.LightProbe` command next to the other `voxel.*` commands;
  - `Docs/VoxelWorld.md` Lighting (a "Local lights" paragraph with the table).
- **Evidence:** `Tests/Evidence/<date>/VoxelShots-*-lights`, plus a montage with the numbers.

### T2. Torch data

- **The reference:** the torch family enters `Reference/minecraft-blocks-26.3` through its tools: `torch`, `wall_torch`, and, if Jesus wants the batch (Q4), `soul_torch`, `soul_wall_torch`, `copper_torch`, `copper_wall_torch`, `redstone_torch` (`lit`), `redstone_wall_torch`, `lantern`, `soul_lantern`, `copper_lantern` (`hanging`, `waterlogged`) and its oxidation and waxed variants. That means manifest entries, baked models (`bake_models.py` already handles element rotation, which the wall torch's -22.5 degrees about z needs) and textures.
- **`FactsDump.java`, through the public API at runtime as today:**
  - per state: outline and collision boxes, light, `replaceable` and occluding faces;
  - a new **support table**: for every imported state and each face, whether a floor torch survives on top of it and whether a wall torch survives against its side. For the lantern, standing on top and hanging below.
  - It records vanilla's answer, so no rule needs guessing: leaves are invalid, glass is valid, a bottom slab's top face is unconfirmed.
- **Items:** one `torch` item. Its placement chooses `torch` or `wall_torch` from the clicked face, so `wall_torch` gets no item of its own (S5's rule). The items S2 registry records `places_block: torch` today, and needs the wall alternative.
- **Gate:** no column change (hashes identical), `Blocks.Registry` counts updated, and the blocks verifier passes.

### T3. Placeable torch with light (after S6, S7, T1 and T2)

- **Rendering (S6's model path):**
  - **Mesher and lighting face:** the torch's quads go through the generic model path; `shade_direction_override` "up" gives the lighting face. The torch's quads stay out of shadows and out of the ray-tracing scene, which R4's "second component, Visible in Ray Tracing off" already covers. Otherwise the light at the flame sits inside its own geometry and shadows itself.
  - **Emission:** the whole torch emits at its block light, 14. That matches vanilla, where the torch is drawn at its own light 14 and reads fully lit. Switch to a flame-only overlay (the 2 x 2 flame texels; see 4.1) if the stick reads as glowing in the capture.
- **Placement and survival (S7's code):**
  - click top face = `torch`; click side face = `wall_torch` facing away from the wall; click underside = try the other valid faces, as vanilla does (order to be confirmed, probe P3);
  - the support table from T2;
  - no collision, the outline box from facts, instant break;
  - pop when the support goes (S7's one-level cascade);
  - lava destroys it without a drop, and flowing water drops it (later, when water flows).
- **The light-source system (game module, presentation only, never in the authority):**
  - **Which blocks get a light:** every loaded block state whose `GetLightEmission` is at least `voxel.LightMinLevel` (default 10) and which is a placed model light (torch, lantern), or, if T0's away test failed, a glowing cube too. Lava never gets one.
  - **Which lights are live:** a pool of `UPointLightComponent`s for the nearest K sources within R blocks of the camera (start at K = 64, R = 48; T1's numbers set them). Sources closer than 2 blocks merge into one light at their centroid with summed intensity. This is Epic's MegaLights advice for clustered lights, and the torch-spam case.
  - **Light shape:** the light sits at the flame (a floor torch about 0.7 blocks up; a wall torch offset from the wall). Its intensity and attenuation come from the light level. Attenuation radius = (level - 1) blocks, so 13 for a torch, with the falloff exponent tuned in a capture against a vanilla screenshot (probe P4).
  - **Lifecycle:** lights update when columns mesh or unmesh and when the authority applies an edit, never per frame.
  - **Settings:** `r.Visibility.LocalLightPrimitiveInteraction 0` if T1 shows CPU cost per light.
- **Particles:** a small Niagara flame-and-smoke emitter at each pooled torch within 16 blocks, 2 to 4 particles per second (rates confirmed by probe P5). Defer it if the session runs long.
- **Tests:**
  - `Blocks.Placement`: torch truth tables from the T2 support table;
  - `Blocks.Survival`: torch pops;
  - `Voxel.Mesher.Models`: torch quads;
  - a new `Voxel.Lights` test: pool selection, merging, cap and determinism given a camera position;
  - an interaction scenario: place a torch on a cave wall, mine its support, and the torch pops.
- **Gates:**
  - a cave capture with 1, 10 and 100 placed torches (the T1 method), within 0.3 ms of T1's probe at the same N;
  - the leak test;
  - mesh per column unchanged within noise;
  - the Playground probe's FPS not lower beyond noise;
  - the light system's game-thread time per edit reported (at most 0.2 ms for one torch placement);
  - the look call: a montage of a torch-lit cave beside a vanilla screenshot of the same layout (P4).

### T4. Held light (optional, a setting)

- **When it is on:** when the selected hotbar item's block has light at least 1 (torch 14, lantern 15, glowstone 15...), one movable point light follows the camera, at the hand offset.
- **Shadows:** MegaLights gives it shadows almost for free. Without MegaLights, a moving VSM point light re-renders its six faces every frame, so use no shadows and a short radius instead (about 8 blocks; it leaks through thin walls).
- **The setting:** "Dynamic lights", off by default, as in the mods.
- **Gate:** at most 0.3 ms GPU with it on, in the `cave` view.

### T5. Block-light field (later)

- **What:** a per-section 4-bit block light, flood-filled from emitters with removal by reverse flood fill, across section and column borders, in the deterministic core (`Source/UnnamedVoxel`, UObject-free).
- **First use, gameplay:** spawn rules and melting. Whether to render it (vertex B, added as emission) is a look call once worldgen places glow lichen and glow berries (S8, S11).
- **Cost to watch:** column generation stays under 5 ms with initial light, and a torch edit remeshes at most 27 sections.

## 3. Questions for Jesus

1. **How should torches light caves?**
   - (a) **Recommended:** a real UE point light per torch with MegaLights ray-traced shadows, if T1's probe passes its gates. It gives the most realistic look and matches how the sun and sky already work. The cost is fixed but needs measuring.
   - (b) VSM-shadowed lights for the nearest 8 to 16 torches.
   - (c) A vanilla-style light field: exact vanilla reach, light around corners, unlimited torches, but a flat look.
2. **The interim:** glowstone and friends should already light caves in a generated world, so you can try it now. Should the probe session (T0 + T1) run next, before S6 and S7 finish? *Recommendation:* yes, as one M session. It settles question 1 with numbers and doesn't touch phase 6's files beyond the capture script.
3. **Held light (non-vanilla):** *Recommendation:* a "Dynamic lights" setting, off by default, built after T1 (S size). Not the interim.
4. **Scope of the first torch session:** *Recommendation:* `torch` and `wall_torch` only, with soul, copper and redstone torches and the lanterns in one later batch. They reuse the same pipeline, but the lantern hangs (a new face rule) and the redstone torch needs redstone.
5. **Flame and smoke particles with the torch (T3) or later?** *Recommendation:* in T3 if time allows, otherwise straight after. A torch without its flicker reads as unfinished.

## 4. Findings

### 4.1 What we have today

**Light-emitting blocks in `DA_Blocks`.** Light is measured per state by `FactsDump.java` through the 26.3 server's public API (`facts/block_state_facts.json`). All of these are imported (`reports/blocks-imported.json`):

| Block | Light | Shape in our set | Default (creative) state glows? |
|---|---|---|---|
| glowstone, sea lantern, shroomlight | 15 | full cube | yes |
| ochre, verdant, pearlescent froglight | 15 | full cube (axis) | yes |
| jack o'lantern | 15 | full cube (facing) | yes |
| redstone lamp | 15 lit / 0 | full cube | no (default `lit=false`) |
| copper bulb (x8 with waxed) | 15/12/8/4 lit by oxidation, 0 unlit | full cube | no (default unlit) |
| respawn anchor | 0/3/7/11/15 by charge | full cube | no (0 charges) |
| furnace, smoker, blast furnace | 13 lit / 0 | full cube | no |
| crying obsidian | 10 | full cube | yes |
| redstone ore, deepslate redstone ore | 9 lit / 0 | full cube | no |
| sculk catalyst | 6 | full cube | yes |
| magma block | 3 | full cube | yes (dim) |
| lava (fluid) | 15 | fluid | worldgen only |
| cave vines (berries), glow lichen, sea pickle (by count, 6 to 15 waterlogged), amethyst cluster and buds (5/4/2/1), firefly bush (2), brown mushroom (1) | as listed | model (S6) | not drawn until S6 |

No torch, lantern, end rod, campfire, candle or beacon is in the reference. None of them is a full cube.

**How the cubes render now:**
- **Chunks:** `VoxelMesher.cpp:261` writes vertex G = `LightEmission * 17`. `M_ChunkOpaque`, `M_ChunkCutout` and `M_ChunkTranslucent` compute emissive = base colour x (G / 255 = light / 15) x `LightEmissive`, with `CHUNK_LIGHT_EMISSIVE = 8000` in `Scripts/Editor/CreateBlockMaterials.py`.
- **Cards:** `UVoxelChunkComponent::BuildCards` makes cards from the opaque and cutout layers per section and direction (cap 32). Card capture includes emissive, so the cubes are in Lumen's surface cache and their light bounces, but only for faces a card captures. A face behind another face of the same direction in the same section is missing (`Docs/VoxelWorld.md` Limits, "Hidden faces in a card"), and then only screen traces see its emission.
- **Exposure:** in the `cave-lava` view at the cave exposure, 36% of pixels clipped. Expect glowing cubes to clip similarly in a dark cave; `CHUNK_LIGHT_EMISSIVE` is the knob for all of them together.
- **The Playground:** ISM block materials with emission 4 (`BLOCK_LIGHT_EMISSIVE`), so glowing blocks read almost unlit under the 75,000 lux sun. Left as is on purpose (phase 5 S3b).

**The torch's texture and model (26.3 jar):**
- **Floor model:** `template_torch` is one element from (7,0,7) to (9,10,9): a 2 x 10 x 2 pixel stick with `ambientocclusion: false` and `shade_direction_override: up`. Sides use UV [7,6,9,16], the top [7,6,9,8], and the bottom [7,13,9,15] with `cullface: down`.
- **Wall model:** `template_torch_wall` is the same element at x -1 to 1, y 3.5 to 13.5, rotated -22.5 degrees about z around (0, 3.5, 8). The blockstate's y = 0/90/180/270 gives facing east, south, west and north.
- **Texture:** `torch.png` is 16 x 16 RGBA. The flame is rows 6 and 7, columns 7 and 8 only: `#FFD800`, `#FF8F00`, `#FFFF97` and `#FFFFFF`. The stick below is browns `#9F7F50` to `#372A17`. So the flame is the top 2 pixels of each side, plus the top face. An emissive-mask rule: texels with max channel at least 0xF0 and R at least 0xF0, which matches exactly the 4 flame texels.
- **Other templates:**
  - Redstone torch: `template_redstone_torch` adds four 3 x 3 pixel glow planes around the tip. The unlit `template_torch_unlit` (the redstone torch off) has no shade override.
  - Lantern: `template_lantern` is a 6 x 7 x 6 body, a 4 x 2 x 4 cap and two crossed handle planes. `template_hanging_lantern` is 1 pixel higher with a longer handle. The lantern texture is animated (`.mcmeta`).
  - Copper torch: exists in 26.3 (`copper_torch`, `copper_wall_torch`; copper lanterns with oxidation and waxed variants), with a `copper_fire_flame` particle.
- **Particles:** `particles/flame.json` and `small_flame.json` both use texture `flame`, and soul torches use `soul_fire_flame`. Smoke is `particles/smoke.json`.
- **Item and recipe:** the item model is `item/generated` with `layer0: block/torch` (a flat sprite of the block texture). The recipe is shaped: coal or charcoal over a stick gives 4.
- **Creative tabs** (`facts/creative_tabs.json`): Functional Blocks starts with torch, soul torch, copper torch, redstone torch, lantern, soul lantern, copper lantern... The redstone torch also appears in Redstone Blocks.

### 4.2 Vanilla light facts (minecraft.wiki, current wiki fetched 2026-10-10)

Full notes: `notes/vanilla-light-facts.md`. Items marked "probe" are not on the wiki text; probes are in 4.6.

- **Light levels:**
  - torch, wall torch, copper torch: 14
  - soul torch: 10
  - redstone torch: 7 lit, 0 unlit
  - lantern and copper lantern: 15; soul lantern: 10
  - end rod: 14
  - lit campfire: 15; soul campfire: 10
  - candles: 3 per candle
  - fire: 15; soul fire: 10
  - beacon and conduit: 15
  - The cube emitters as in the table above.
- **Propagation:** block light drops 1 per block in taxicab distance through non-opaque blocks, and opaque blocks stop it. In Java, water, ice, leaves, cobweb, slime and honey reduce only sky light. The level shown is max(sky, block). So a torch gives level 1 at 13 blocks.
- **Gameplay:**
  - Hostile mobs spawn only at block light 0 (since 1.18).
  - Snow forms at 10 or less and melts at 12 or more; ice forms at 9 or less and melts at 11 or more. So a torch (14) melts snow within 2 blocks and ice within 3, and a soul torch (10) melts neither.
  - Our generated world has snow and ice, so torch placement near snow eventually needs this rule (T5 or a small rule in T3).
- **Torch placement:**
  - **Faces:** floor on a block's top face, wall on a side face, never the underside.
  - **Valid supports:** the top or sides of most solid blocks, fences, glass, panes, iron bars, upside-down slabs and stairs, and jack o'lanterns.
  - **Invalid:** leaves. Bottom-slab tops and hoppers are unconfirmed; probe P1.
  - **Underside clicks:** they snap to another valid face in the same cell (stated for the redstone torch, and assumed the same for torches).
- **Behaviour:**
  - It drops as an item when its support is removed, when water flows into it, or when a piston pushes it. Lava destroys it with no drop.
  - It isn't waterloggable in Java (the underwater torch is Bedrock/Education only).
  - No collision, hardness 0, stack 64.
- **Redstone torch:** burns out after more than 8 forced changes within 60 ticks.
- **Lantern:**
  - It stands on a block's top face or hangs from the bottom face of a block, a chain, iron bars or panes.
  - States `hanging` and `waterlogged`, hardness 3.5.
  - Recipe: 8 iron nuggets around a torch.
- **Held light:** none in vanilla Java or Bedrock. Bedrock's Vibrant Visuals (2025) adds optional point lights to placed torches and lanterns, and players report glare and flat contrast because vanilla block light still applies on top.
- **Brightness curve:** the wiki gives only reference points: at the lowest brightness setting, Overworld day about 98%, night 17% and total dark 5%. It gives no formula. We don't use vanilla's lightmap anyway; the look is set by captures.

### 4.3 Renderer options for torch light (UE 5.8, HW-RT Lumen, RealtimeMesh chunks)

Full notes: `notes/ue-lighting.md`, `notes/voxel-projects-lighting.md`.

**(a) Emissive flame quads lit through Lumen GI, like lava.**
- **Cost:** none per torch beyond the quads.
- **Look:** the flame glows. The cave gets a faint, noisy glow near the torch at best.
- **Limits:**
  - Epic says there is "a limit to how small and bright emissive areas can be" before noise, and that small objects are culled from Lumen's scene, so only screen traces pick them up. The light then depends on the view and fades when the torch leaves the screen.
  - Model quads are not carded (R4).
  - The flame's area is about 0.08 m², against 6 m² for a glowstone block.
  - 5.8 adds `r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal.SkipUnlitHits` against tiny-emissive noise (from a third-party digest of the release notes).
- **Verdict:** the flame's look, not the cave's light. For glowing cubes it does work (large, carded), with the hidden-face and lag limits T0 measures.

**(b) A UE point light per torch.**
- **Unshadowed:** cheap per light in the clustered deferred path (no official numbers), but it leaks through walls. A 13-block radius crosses rock into other caves and up to the surface. Unusable in a voxel world, except for a held light with a short radius.
- **VSM-shadowed:**
  - **Cost:** a point light is six 16K virtual maps. Non-Nanite meshes (our chunks) are culled per mip per face, so one mesh counts up to 48 times (`r.Shadow.Virtual.MarkPixelPagesMipModeLocal` 1 or 2 reduces this).
  - **Caching and its limits:** cached pages are cheap, but a moved light, or casters edited or rebuilt in its radius, invalidate them. Mining near torches and column rebuilds while streaming will cause re-renders. Epic calls many small shadowed lights "still a work in progress" and suggests turning shadows off on distant lights.
  - **Rough expectation, not measured:** around 10 shadowed torches near the camera before the frame suffers.
- **MegaLights (b+):**
  - **How it works:** stochastic direct lighting. Each pixel samples a few important lights, traces shadow rays only to those, and is denoised.
  - **Cost:** constant overhead, with quality (noise, ghosting) worse as more important lights overlap a pixel.
  - **Shadows:** ray tracing by default (`r.MegaLights.DefaultShadowMethod` 0: "guarantees fixed MegaLights cost ... dependent on the BVH representation quality"). VSM per light is optional and "has a significant per light cost".
  - **Requirements:** hardware ray tracing and the deferred renderer, ideally with Lumen HWRT sharing the ray-tracing scene. All of which we have.
  - **Status:** 5.5 experimental, 5.7 beta, 5.8 "entering Production Ready" (Epic news, June 2026). The 5.8 engine's cvar text still says experimental, and `r.MegaLights.EnableForProject` defaults to 0.
  - **Turning it on:** the project setting, or a PostProcessVolume override. Per light, "Allow MegaLights".
  - **CPU:** `r.Visibility.LocalLightPrimitiveInteraction 0` removes most per-light CPU cost.
  - **Artifacts:**
    - noise and ghosting where lights cluster (Epic: merge them, tighten radii);
    - leaks where the BVH is simplified (our chunks' BVH is the exact mesh);
    - lights inside geometry waste samples, so keep torch quads out of the ray-tracing scene;
    - alpha-masked surfaces only through screen traces by default, which matters for leaves and plants.
  - **Numbers:** none from Epic. A vendor blog claims a "few ms" floor at 1080p to 1440p and suggests MegaLights from about 50 lights up (no measurements). Arm reports 190+ shadowed lights on mobile, with overlap in one region as the bottleneck.
- **Lumen bounce of local lights:** movable point lights light Lumen's surface cache, up to 8 per card tile by intensity and attenuation (`r.LumenScene.DirectLighting.MaxLightsPerTile`, valid 4/8/16/32), with hardware-ray-traced shadowing (`r.LumenScene.DirectLighting.HardwareRayTracing` 1). So torch light bounces off cave walls, and the surface cache updates over several frames.
- **Look against vanilla:** the light has a normal response and shadows, and stops at corners except through bounce. Vanilla's light wraps around corners (taxicab flood fill) and is the same on every face apart from fixed face shading. With inverse-square falloff, a point light is brighter near the flame and dimmer far away than vanilla. Tune the attenuation radius to 13 blocks and the falloff by capture.

**(c) A Minecraft-style block-light field baked into vertex data.**
- **Cost:**
  - Flood fill on the CPU per section, 4 bits per block, at generation for every worldgen emitter (lava, glow lichen and so on), which is added column time.
  - A torch placed or removed relights up to a 27-block-wide cube and remeshes up to 27 sections; under S6's gate of 0.2 ms per section, that is about 5 ms of async mesh work per edit.
  - Render time: zero.
  - The vertex B channel is free today (written as 255).
- **Look:** exactly vanilla's reach, corner-wrapping and levels, but flat: no normals, no specular, no shadows.
- **Combining with Lumen:** added as emissive (albedo x curve(level) x K). Our cards capture emissive, so lit surfaces become large, dim emitters, which is Lumen's good case. That gives a soft extra bounce: tunable, possibly a feature.
- **Limits:** nobody publishes a field-plus-Lumen combination, so it is our own experiment. Day/night needs no relight, because sky light stays Unreal's.
- **Who does it:** Minecraft (1 byte per block, block and sky), Luanti (0-14, day/night banks), Seed of Andromeda (RGB flood fill), shader packs (Rethinking Voxels, Photon: coloured flood fill as an optional layer).

**(d) Hybrids.**
- **H1, recommended path:** MegaLights point lights for placed torches and lanterns (a pooled nearest K, merged clusters), plus emissive torch quads. The field arrives later for gameplay, and is rendered only if worldgen light sources need it. Least code first, the best look, and a clean fallback.
- **H2, most robust, most work:** the field renders everything at vanilla reach, plus MegaLights for the nearest few bright sources for normals and specular. Needs careful balance so near-torch areas aren't doubled (Vibrant Visuals' complaint is exactly this doubling).
- **H3, cheap and flawed:** unshadowed point lights, with the radius cut to stay inside caves. Leaks and wrong light. Not recommended.

**Scale warning for any per-source light design.** Worldgen will place many emitters: glow lichen (7) in caves, cave vines with glow berries (14) in lush caves, amethyst geodes (1-5), magma (3) and lava pools. Per-source point lights don't scale to those. The light system should cover only placed lights at level 10 or more (or the nearest K by brightness, merged). Dim worldgen emitters stay emissive (Lumen), or move to the field later.

### 4.4 What other projects do

- **Minecraft RTX (Bedrock, 2020):** emissive PBR textures in a path tracer with heavy denoising. No published light counts.
- **Vibrant Visuals (Bedrock 1.21.90, 2025):** deferred PBR plus optional point lights on torches and lanterns, with optional shadows. Player feedback: glare, and flat contrast because vanilla block light stays on top. That is the double-counting lesson for H2.
- **Vintage Story:** torch 14, lantern 20, firepit 16; held sources light the area; the developer found coloured light troublesome in 2016.
- **Luanti:** levels 1-14 plus sun 15, one byte per node, day/night banks.
- **Veloren:** lanterns since 0.3, shadow-mapped point lights since 0.8. No published counts.
- **Teardown:** custom ray-marched voxels. Not comparable.
- **Shader packs:** they read vanilla's per-vertex block light and colour it. Coloured-light packs add a voxelized flood fill (Photon, Rethinking Voxels), which is cheap with no denoiser but coarse.
- **Dynamic-light mods (Java):**
  - LambDynamicLights (off by default) rebuilds the chunks around a moving light, which is expensive; its README recommends Sodium with deferred chunk updates. RyoamicLights is a port.
  - Wick, Truly Bright (baked into Sodium's vertices) and DynamicLights Reforged also exist.
  - Lesson: baking a moving light into meshes costs remeshes, and a real light avoids that. So T4 uses a point light, never the field.
- **Unreal voxel projects:** no published torch-light write-ups or light-count numbers found. Voxel Plugin 2 advertises Lumen support only.

### 4.5 The cheap interim

- **Glowing cubes, now:** placeable today in `L_Generated` from the creative inventory (phase 3 placing, all 600 block items).
  - **What's missing:**
    1. evidence (T0);
    2. the Playground's emission (4), unchanged on purpose;
    3. a known limit if the away test fails;
    4. possibly clipping in caves, where the shared knob is `CHUNK_LIGHT_EMISSIVE`.
  - **Code needed:** none, unless T0 shows a problem.
- **Held light:**
  - **Cost:** one pooled point light following the camera while the selected item's block emits light. An S session (T4): the selection already exists in the loadout and no hand rendering is needed.
  - **Shadows:** with MegaLights, shadowed and nearly free. Without it, unshadowed with an 8-block radius, or VSM re-rendered every frame because the light moves.
  - **Distance from vanilla:** total, since vanilla has none. As a setting, off by default, it matches the mods.

### 4.6 Probes for later workers (no runs were made tonight)

- **P1, support truth table:** extend `FactsDump.java` (public API at runtime, empty world, as today). For each imported state placed at the origin, record whether `torch` survives at (0,1,0), whether `wall_torch` survives at each side with `facing` pointing away, and, for the lantern, whether it survives standing on top and hanging below. Output into `facts/`.
- **P2, outline boxes:** the same run, recording `getShape` for torch, wall torch (four facings) and lantern (hanging or not).
- **P3, underside click (black-box, vanilla client):** in a creative superflat world, click the underside of a block that has a free side and floor, with the player facing north, east, south and west. Record which torch results; repeat next to a fence. Screenshots with F3.
- **P4, look reference (vanilla client):** a 9 x 4 x 9 stone room underground, one torch in the middle of the floor, brightness 50%, smooth lighting on, F1 screenshots from a fixed spot at night. Then the same room with 4 torches. T3's capture builds the same room for a montage.
- **P5, particle rate:** a single torch, F1, a 10-second video at 20 tps; count the flame and smoke particles that appear; repeat for a wall torch (where they spawn).
- **P6, emissive mask:** the flame-mask rule in 4.1 (max channel at least 0xF0 and R at least 0xF0) checked against `soul_torch.png` and `copper_torch.png` before T3 uses it.

---

## Appendix A. Links

**Minecraft (minecraft.wiki, fetched 2026-10-10):**
- [Torch](https://minecraft.wiki/w/Torch), [Soul Torch](https://minecraft.wiki/w/Soul_Torch), [Redstone Torch](https://minecraft.wiki/w/Redstone_Torch), [Lantern](https://minecraft.wiki/w/Lantern)
- [Light](https://minecraft.wiki/w/Light): levels table, propagation, spawning, melting
- [Model](https://minecraft.wiki/w/Model), [Blockstates definition](https://minecraft.wiki/w/Blockstates_definition)
- Vibrant Visuals feedback: [feedback.minecraft.net post 42562399390221](https://feedback.minecraft.net/hc/en-us/community/posts/42562399390221-Adjust-Vanilla-Lighting-When-Using-Point-Light-Vibrant-Visuals)

**Unreal Engine 5.8:**
- [Lumen technical details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine) (small objects culled; emissive)
- [Lumen global illumination and reflections](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine)
- [Using the emissive material input](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-emissive-material-input-in-unreal-engine)
- [Virtual shadow maps](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine)
- [MegaLights](https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine)
- [Arm: hundreds of dynamic lights with MegaLights](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/lighting-at-scale-bringing-hundreds-of-dynamic-lights-to-mobile-with-unreal-megalights)
- Local engine source, cvar defaults only: `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLights.cpp` (`r.MegaLights.EnableForProject` 0, `r.MegaLights.DefaultShadowMethod` 0) and `Lumen/LumenSceneDirectLighting.cpp` (`r.LumenScene.DirectLighting.MaxLightsPerTile` 8)

**Voxel lighting:**
- [0fps: voxel lighting (2018)](https://0fps.net/2018/02/21/voxel-lighting/) (flood fill, credits Seed of Andromeda)
- [Luanti light](https://docs.luanti.org/for-players/light/)
- [Vintage Story light sources](https://wiki.vintagestory.at/Light_sources)
- [LambDynamicLights](https://github.com/LambdAurora/LambDynamicLights)
- [Rethinking Voxels shader](https://modrinth.com/shader/kmwfVOoi)

**Project:**
- `Docs/VoxelWorld.md`: RealtimeMeshComponent, Lighting (cave exposure and glow; Limits)
- `Docs/Records/2026-10-09-Voxel-Phase6-Plan.md`: S5 to S7
- `Docs/Records/2026-10-09-Voxel-Handoff-Phase6-S6.md`
- `C:\dev\sandbox\scout-block-models\facts.md`: R1, R4, G3
- `Docs/Records/2026-10-09-Items-Scout-Research.md` (torch line), `Docs/Records/2026-10-09-Items-Phase-Plan.md` Q4 (in the items worktree)

## Appendix B. Raw data and how to regenerate it

- **`jar-assets/`:** extracted from `%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar` with `unzip` for the torch, lantern and particle blockstates and models, `torch.png`, the recipe and the loot table.
- **`scripts/lightblocks.mjs <blocks reference>`:** lists every emitting block in `facts/block_state_facts.json` with its manifest shape. **`scripts/imp.mjs`:** which emitters are imported.
- **`scripts/png.mjs <png> x0,x1,y0,y1`:** prints RGBA texels (no dependencies).
- **`notes/`:** the three research agents' full notes.
  - `vanilla-light-facts.md`: the light table came through a summarizing fetch; spot-check ids before relying on them.
  - `ue-lighting.md`
  - `voxel-projects-lighting.md`
