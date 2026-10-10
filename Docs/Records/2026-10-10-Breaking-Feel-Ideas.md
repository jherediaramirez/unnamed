# Breaking feel: ideas (outline, cracks, particles, sounds, swing)

Parallel lane `feature/voxel-break` (worktree `Unnamed-break`, base `277f4b8`), steps 1-2 of Jesus's plan: read and measure, then ideas. Nothing is built yet. Jesus picks from section 10; step 3 prototypes his picks behind switches (default off), and step 4 makes his final picks the default.

## 1. Summary

- **Today** breaking is a placeholder. The outline is a thin opaque line box whose width shrinks with distance. The cracks are procedural polylines on the full cell. Break particles are 10 flat-coloured cubes that fall through the floor. Mining makes no particles, there is no sound at all, and the press swing is 0.25 s.
- **Vanilla 26.3** was established from its own data (crack textures, `sounds.json`, sound types dumped through the public API) and the wiki. Several numbers are not published: the stage mapping, particle counts, the hit-sound cadence, and the outline colour and width. A short black-box client probe (VP1, section 4.6) would measure them.
- **Recommended set:**
  - Outline: screen-constant vanilla outline, and the menu's High Contrast option.
  - Cracks: vanilla crack textures on the block's own shape, plus smooth growth between stages.
  - Particles: textured particles for breaks and hits, with collision and the menu's Particles setting, plus a gentle directional spray.
  - Sounds: vanilla break and hit sounds.
  - Swing: the 6-tick swing.
  - Better than vanilla: a short "break pop".
  - Section 6 has the full list. Each item gets its own switch.
- **Jesus's main call** is the asset source (Q1). Vanilla cracks and sounds mean extending the hash-checked block reference. That changes `SHA256SUMS.txt`, so `DA_Blocks` needs its digest re-stamped (Q2). Render-only switches touch no world hash, golden or saved world.

## 2. Scope, and the line with the other lanes

- **This lane:** the outline, crack stages, break and hit particles, break and hit sounds, and the swing and hit timing. All of it lives in presentation (`UFoundationInteractionView`). Nothing goes through `FFoundationAuthority`: the view already gets `Broken` and `Popped` events with cell and state, and the interaction mirror gives `bMining`, `MiningCell`, `MiningProgress`, `Target.Face` and `Step`.
- **Items lane (not here, ideas only):**
  - S4 owns the held-item mesh, transforms and the hand pose (its probe P1 shoots pose, swing and equip in the client).
  - S5 owns tool rules, tiers, mining speed and the delay between breaks.
  - S6 owns wear and the tool-break sound.
  - Section 5.7 lists the ideas that belong there.
- **Overlap with S4:** the swing's timing is ours and its pose is S4's. W1 proposes one shared swing clock that S4's renderer reads.
- **Scout H 4.4:** hardness 0 breaks instantly (`GetMiningDelta`), and real plants only add rows. Nothing there changes the presentation. Pops (plants losing support) already raise `Popped`, and vanilla shows the same destroy effect for them (to confirm in VP1).

## 3. Today against vanilla, gap by gap

Capture of ours (stone, bare hand, mining past a third): `C:\dev\projects\Unnamed\Tests\Evidence\2026-10-10\Playground-all\Runtime-20261010-063606\runtime-60-interaction-mining.png`. It shows a 1-2 px black box, and one or two thin dark polylines per face. A full break sequence (stages, particles at fixed times) comes with the first step-3 build.

| Area | Ours today (code) | Vanilla 26.3 (source) | Gap |
|---|---|---|---|
| Outline | `DrawSelection` (`FoundationInteractionView.cpp:568-612`): `ULineBatchComponent` boxes. Each outline box is expanded 0.3 cm, colour (.02, .02, .02) opaque, thickness 0.9 world units, so the width shrinks with distance. Internal edges between a shape's boxes are drawn. | Colour, alpha and width are not published (VP1). It has a "High Contrast Block Outlines" accessibility option (24w40a, default off; wiki). Our menu already saves `highContrastBlockOutline`, with its effect pending. | Width varies with distance; colour and alpha unverified; the high-contrast option has no effect. |
| Cracks | `DrawCracks` (`:647-677`): stage = floor(progress × 10), clamped to 0-9, so stage 0 draws on the first frame. Per face: one 3-segment polyline per stage (8-16 cm segments), ink (.08, .08, .09), thickness 0.7. They are drawn on the full cell expanded 0.6 cm, not on a slab's or a model's shape. The darkening overlay (`:833`) is drawn only for untextured fallback blocks. | 10 textures `destroy_stage_0..9`, 16x16 (jar). They are cumulative: 4, 10, 18, 28, 41, 55, 70, 100, 129 and 145 crack pixels. The crack pixels hold only two greys, 61 and 155, on an alpha-1 white background, which fits a multiply blend where 128 is neutral (to confirm in VP1). The jar also has `destroy_oit_stage_0..9`, not analysed. The stage-to-progress mapping is not published (VP1). | Different art, and no pixel-grid look. They ignore the block's shape, and the lines don't light like the block. |
| Break particles | `Burst` (`:412-431`): 10 cubes of 12 cm in `BasicShapeMaterial`, in the block's `ParticleColor` (the default state's top face, averaged and tinted, so grass block is green). Speed 156-260 cm/s with an upward bias, gravity 980 cm/s², life 0.45-0.75 s, shrinking to 0, no collision. They come from a 96-component pool, each a separate moving `UStaticMeshComponent`. | The `block` particle, "produced when blocks are broken", textured "from the broken block" (wiki). Count, size, life, gravity and collision are not published (VP1). The texture is the model's `particle` texture (grass block's is dirt), which our baked reference does not keep. | No texture; the wrong colour for blocks whose particle texture differs; no collision; a small count. |
| Hit particles | None. | Not published whether mining emits particles (VP1). | Probably missing. |
| Break sound | None: the project has no gameplay audio (`2026-10-08-Interaction.md`). The menu uses vanilla UI sounds. | Per sound type, `block.<type>.break` at volume 1.0 and pitch 0.8 for type 1.0/1.0, attenuation 16 blocks (the wiki's sound tables). `sounds.json` per-file volume and pitch multiply on top. | Missing. |
| Hit sound | None. | `block.<type>.hit`, "plays while the block is being mined", at 0.25 and pitch 0.5. Netherrack's is 0.1375, a per-file 0.55. The cadence is not published (VP1). | Missing. |
| Swing | `UpdateHeldItem` (`:795-807`): a press starts a 0.25 s sine swing; while mining, a continuous \|sin\| bob with a 0.3 s period. | `swing_animation` (1.21.11): type whack, duration 6 ticks (0.3 s) by default. In 26.3 the wiki shows `attack_animation` and `interact_animation` with the same fields. How the swing repeats while mining is not published (VP1). | 0.25 s instead of 0.3 s, and a bob instead of discrete swings. |
| Delay between breaks | `FFoundationInteraction::MiningDelaySteps = 5`. | 6 ticks in survival, and none for blocks that break in 0.05 s or less (wiki, Breaking). | Items S5 (already planned with probe P5). |
| View cost | `GetLastViewMs` 0.167 ms (one sample, `runtime-60.json` of the run above). | - | The baseline for section 5's costs. |

## 4. Vanilla facts gathered

Sources: the client jar (SHA-1 `e877b6a0…`, matches the block reference), asset index 34 (`sounds.json` hash verified), the server jar's public API, and minecraft.wiki. No game code was read.

### 4.1 Crack textures

- **Format:** 10 PNGs at 16x16 with no `.mcmeta`. Stages 0-4 are LA, and stages 5-9 are palette with tRNS.
- **Alpha:** only 1 (the faint white background) and 255 (crack). Alpha 0 never occurs.
- **Cumulative:** every crack pixel of stage N stays a crack pixel in N+1.
- **New pixels per stage:** 4, 6, 8, 10, 13, 14, 15, 30, 29 and 16. 111 pixels are never cracked.
- **Greys:** 8 pixels turn from 155 to 61 at a later stage and never back. Everything else keeps its first grey.
- **Per-pixel map:** the first-stage map exists (`assets-facts.md`), which is what C2 needs.

### 4.2 Sounds

- **Sound types (public API black box):** `BlockState.getSoundType()` gives volume, pitch and five events.
  - All 1,286 blocks: 129 distinct types.
  - The 618 ids the block manifest lists (its blocks plus water) use 95 types. Their states never vary.
  - The biggest types: stone 178, wood 75, grass 43, wool 33, copper 32, glass 25.
  - Dirt uses `gravel`. Grass block, leaves and dandelion use `grass`. The torch uses `wood`. Water is `intentionally_empty`.
  - Gold, diamond and emerald blocks are `metal`, at pitch 1.5.
- **Events and files (asset index 34):**
  - Across all blocks: 126 groups, with 125 break and 114 hit events.
  - Our blocks need 92 break and 84 hit events: 762 ogg files, 6.86 MB.
  - All five kinds for every group would be 1,210 files, 10.8 MB.
  - Block entries carry only name, volume and pitch: no weights, no streaming, no references to other events.
- **Durations:**
  - Stone break 0.50 s. Glass break 0.69 s. Wool break 0.20 s. Deepslate break 0.83-1.00 s.
  - Stone hit 0.42-0.61 s. Wood hit 0.49-0.69 s. Sand hit 0.21 s.
- **The wiki's tables (Stone, Dirt, Grass Block, Sand, Gravel, Glass, Wool, Deepslate, Netherrack, Snow, Leaves, Dandelion, Torch):** break 1.0/0.8, hit 0.25/0.5, place 1.0/0.8, step 0.15/1.0, fall 0.5/0.75, attenuation 16.
  - How a type's own volume or pitch scales these (the metal type's 1.5) is not stated. Confirm it on a metal block's page before S1.

### 4.3 Particles, swing, timing, outline (wiki)

- **Particles:** `block` is "produced when blocks are broken", textured "from the broken block". `block_crumble` belongs to the creaking only.
  - The Particles option: All, Decreased (the default) or Minimal. Decreased "reduces the amount of particles for some particle events".
  - 1.21.11: decorated pots use the terracotta particle texture. 26.1 fixed transparent frogspawn particles.
- **Swing:** `swing_animation` {type none/whack/stab (default whack), duration (default 6 ticks)}.
  - Haste raises mining speed 20% per level, and Mining Fatigue multiplies it by 0.3^min(level, 4). The Haste page contradicts itself on when the arm stops moving.
- **Breaking:** the block breaks "after the cracking animation is finished and completely covers the block". Other players see the cracks. Progress resets on release and on a tool switch. 26.2 centred bamboo's crack texture (MC-307473).
- **Outline:** "High Contrast Block Outlines" (24w40a, Accessibility, default off) is "to make targeted block's outlines more visible".

### 4.4 Not published, so VP1 measures them

The stage for a given progress, and whether stage 0 shows on the first tick. Whether cracks follow slabs, flowers, leaves and glass, and whether they fade on release. Particle count, size, life, gravity and collision for a break. Whether mining emits particles. The hit-sound cadence in ticks. The swing's repeat while mining. The outline's colour, alpha and pixel width at 1080p. What High Contrast looks like. Which particles Decreased and Minimal drop.

### 4.5 Where the files are

`C:\dev\sandbox\break-ideas\`:
- `assets-facts.md`, `destroy_stages_sheet.png` and `destroy_stages_pixels.csv` (crack textures and the per-pixel first-stage map);
- `block_sound_events.csv` and `block_event_durations.csv` (sounds.json);
- `soundtypes\summary.md` and `block_sound_types.csv` (sound types for every state);
- `wiki-facts.md` (each fact with its URL and quote).

The sandbox is disposable. Anything step 3 keeps goes through the reference tools (section 7).

### 4.6 Probe VP1: the 26.3 client, black box

What it would do:
- **Setup:** a windowed client from the Modrinth install, run offline in a sandbox game dir. The world is a flat sandbox world made by the dedicated server.
- **Input:** synthesized. Mining Fatigue stretches a stone break so every stage can be shot.
- **Measurements:**
  - F2 shots at fixed ticks;
  - particle counts from the F3 screen;
  - a WASAPI loopback recording for the hit cadence;
  - shots of the outline at 1, 3 and 6 blocks, with High Contrast on and off.
- **Cost:** about 30 minutes, with no one using the keyboard or mouse during it.

It is a Java run, so `Get-Process java, javaw, UnrealEditor*` is checked first. It could share a session with the items lane's P1 (hand pose and swing). It needs the manager's OK and a time Jesus isn't playing (Q5).

## 5. Ideas

V matches vanilla; B is better than vanilla. Effort: S is under 2 h, M is half a day, L is a day or more. Frame cost is an estimate until step 3 measures it (the view's `LastViewMs`, `stat unit`, `ProfileGPU`). Every idea gets its own switch (proposed name `foundation.Break.<Area>`, registered in a new file of this lane; default 0 = today).

### 5.1 Outline

| ID | Kind | What it changes | Effort | Frame cost | Risk | Test |
|---|---|---|---|---|---|---|
| O1 | V | Constant screen-space width: thickness recomputed per frame from the distance. Vanilla's colour, alpha and pixel width, from VP1. A multi-box shape draws only its outer edges. | S | ~0 (12 lines per box) | Line batches may ignore alpha; if so, a thin translucent mesh outline instead (M). | `Outline 0/1`; shots at 1, 3 and 6 blocks; measured line px width and colour against VP1. |
| O2 | V | High Contrast Block Outlines, driven by the menu's saved `highContrastBlockOutline` (its look from VP1). | S | ~0 | Small: reads `UMenuPreferences`, and the menu files are in no other lane's list. | Toggle the pref; a control pair of shots. |

### 5.2 Cracks

| ID | Kind | What it changes | Effort | Frame cost | Risk | Test |
|---|---|---|---|---|---|---|
| C1 | V | The vanilla `destroy_stage` textures as a 16x16x10 nearest-filtered array. They are drawn over the target's own shape: the cube, slab and layer meshes, or a model's parts, which the view already builds for the held block. The material is translucent, Modulate and unlit, 16 texels per block, projected in world space per face, so a slab shows half a crack. Stage from progress uses the mapping VP1 measures. The Playground and generated worlds share one code path. | M | 1-4 draws while mining, ≤ 0.02 ms GPU (est.) | Modulate above 1 (the 155 grey brightens) may clamp; TSR shimmer on 16-px cracks; sorting against water and glass; z-fighting (needs a small inflation and depth bias). | `Cracks 0/1`. Stages 0, 3, 6 and 9 on stone, grass block, glass, oak leaves, a bottom slab and a dandelion, with TAA off. Crack-pixel area per stage against the texture counts (4…145 of 256). |
| C2 | B | Smooth growth: within a stage, the next stage's new pixels fade in with fractional progress (from the cumulative per-pixel map), so frames at stage boundaries match C1 exactly. On a 9 s obsidian break this turns 10 jumps into a steady spread. | S (on C1) | +1 texture sample | Low; the 8 pixels that change grey use the next stage's grey. | `Cracks 2`. A boundary frame equals C1's (diff ≈ 0), and crack area grows monotonically across 20 frames. |
| C3 | B | The crack centred on the hit point of the face being mined (the other faces stay standard). | S | 0 | The vanilla textures don't tile, so a shifted crack shows seams; it reads less like Minecraft. | `Cracks 3`; shots at three hit points. Not recommended. |
| C4 | fallback | If vanilla textures are declined (Q1): our own pixel-grid cracks, generated deterministically into the same array. | M | as C1 | Art quality. | as C1. |

### 5.3 Particles

| ID | Kind | What it changes | Effort | Frame cost | Risk | Test |
|---|---|---|---|---|---|---|
| P1 | V | Textured break particles. Camera-facing quads in one instanced component, each with custom data: a texture layer, a sub-rectangle of the block's particle texture, a tint. The material is lit with an up-facing normal, so caves stay dark. Particles collide with solid cells, and count, size, life and gravity come from VP1. They replace the 10 cubes for `Broken` and `Popped` (critical hits and tool breaks keep the cube pool). The particle texture is the model's (grass block → dirt), which needs Q7. | M-L | ≤ ~0.05 ms CPU per burst frame (est., ≤ 200 instances); GPU negligible | TSR smear on small fast sprites (responsive AA or velocity output); Lumen on the sprites; instance updates per frame. | `Particles 0/1`. Shots at +50, +150 and +400 ms after the break; the probe JSON logs counts. `LastViewMs` p99 over a 10 s mining loop. |
| P2 | V | Hit particles while mining, if VP1 shows vanilla makes them: small sprites at the hit point on the hit face, on the cadence VP1 measures. | S (on P1) | small | None beyond P1. | `Particles 1` with a mining sequence; the count per second against VP1. |
| P3 | V | The menu's Particles setting (All, Decreased, Minimal) scales counts the way VP1 shows. | S | 0 | As O2. | Each pref value: counts in the probe JSON. |
| P4 | B | Directional spray: break particles get a small extra push out of the mined face and away from the camera, and hit chips leave the hit face. | S | 0 | Subtle by design; tunable. | `Particles 2`; shots at +100 ms. |
| P5 | B | Particles fade (dithered) over the last 20% of their life instead of vanishing. | S | 0 | Vanilla pops them out; fading may read as softer. | `Particles 3`. Not recommended at first. |

### 5.4 Sounds

| ID | Kind | What it changes | Effort | Frame cost | Risk | Test |
|---|---|---|---|---|---|---|
| S1 | V | Break and hit sounds by vanilla sound type: 92 break and 84 hit events, 762 oggs, 6.86 MB. Break at 1.0/0.8 and hit at 0.25/0.5 times each file's own volume and pitch (the type's own scaling confirmed first, section 4.2). A random file per play, linear attenuation over 16 blocks. Hits follow the mining-step cadence VP1 measures; pops play the break sound too. | M-L | CPU negligible; memory ~7 MB compressed | UE's ogg import (to verify; otherwise the import script decodes to WAV); the volume relative to the menu's pending audio prefs; headless test runs have no audio device, so tests read an event log. | `Sounds 0/1`. A probe event log (time, event, file, volume, pitch) checks mapping and cadence. A loopback recording of a mining sequence for Jesus to listen to. |
| S2 | B | Small random pitch spread (±3%) on hits and a per-event voice limit, against machine-gun repetition. | S | 0 | Drifts from vanilla's fixed pitch. | `Sounds 2`; the event log and recording. |
| S3 | — | Place, step and fall sounds: the same import, but not breaking. | - | - | - | Left for the placement and movement work (Q3). |

### 5.5 Swing and timing

| ID | Kind | What it changes | Effort | Frame cost | Risk | Test |
|---|---|---|---|---|---|---|
| W1 | V | A 6-tick (0.3 s) swing per press, and discrete repeated swings while mining at the cadence VP1 measures, instead of today's bob. One swing clock (start, duration, phase) that S4's held-item renderer reads later. The pose stays as is until S4. | S | 0 | A merge seam with S4's later `UpdateHeldItem` changes; the clock is the agreed interface. | `Swing 0/1`. The hand pivot's transform logged per frame over 2 s of mining, giving the period and phase; a 60 FPS frame strip. |
| W2 | V | Hit sounds and particles counted in reference steps (the interaction's `Step`), not frame time, so they hold at any FPS. | S | 0 | None. | Test: event cadence at 30, 60 and 144 FPS. |
| W3 | B | Hit sound and chips land on the swing's contact phase. | S | 0 | Changes the vanilla rhythm if vanilla's cadences don't line up (VP1 tells). | `Swing 2`; the recording beside VP1's. Alternate only. |

### 5.6 Extras (better than vanilla)

| ID | Kind | What it changes | Effort | Frame cost | Risk | Test |
|---|---|---|---|---|---|---|
| X1 | B | Break pop: at the break, a copy of the block (its mesh and custom data, as the held block draws) shrinks to nothing over about 80 ms while the particles fly. The world removes the block at the commit, as today. | S-M | 1 instance for 80 ms | Feels soft if too long; one frame of double draw if the chunk update lags. | `Pop 0/1`; shots at +16, +50 and +100 ms. |
| X2 | B | Hit pulse: the crack shell grows 1-2% for a moment on each hit. | S | 0 | Vanilla blocks never move; z-fight risk. | Not recommended; listed for completeness. |

### 5.7 Items-lane ideas (proposals only)

- **I1 (S5):** the delay between breaks. Ours is 5 steps; the wiki says 6 ticks, and none for blocks that break in 0.05 s or less. S5 already plans probe P5 for it.
- **I2 (S5):** creative breaking is instant for every tool (wiki); its held-repeat delay is not published.
- **I3 (S4/S7):** swing duration from the held item's `attack_animation` (26.3; 6 ticks by default). W1's clock takes a duration, so S4 can feed it.
- **I4 (S6):** the tool-break sound `entity.item.break` (`random/break`) on `ToolBroke`. It comes from the same sound import if S1 is picked.

## 6. Recommended set

- **Prototype (step 3), each behind its switch:** O1, O2, C1, C2, P1, P2, P3, P4, S1, S2, W1, W2, X1. W3 is a recorded alternate.
- **Not now:** C3, P5, X2. C4 only if Q1 declines vanilla textures.
- **Order:** C1-C2 and W1-W2 first: they are the most visible and need only the 10 crack PNGs. Then P1-P4, S1-S2, O1-O2 and X1. VP1 runs before the side-by-side pack.
- **Defaults (step 4):** Jesus's final picks from the pack.
- **Total effort:** one long session for the prototypes and the pack, and a shorter one for defaults, tests and review.

## 7. Assets and the hash-checked reference (Jesus's call)

Vanilla cracks, sounds and particle textures are new facts and assets. By the rules they enter only through `Reference/minecraft-blocks-26.3`'s own tools, with no second build path.

- **R1, extend the block reference (recommended):**
  - `FactsDump.java` writes a new `facts/block_sounds.csv`: per imported block, its sound type's volume, pitch and five events, through public `BlockState.getSoundType()`.
  - `build_reference.py`:
    - copies `textures/block/destroy_stage_0..9.png` byte for byte;
    - adds a sound section from asset index 34 (a new pinned source: index SHA-1 `abfaa525…`): a `block_sounds.json` aggregate (event → files, volume, pitch) and the 762 oggs byte for byte;
    - writes `facts/block_particles.json`: each imported block's particle texture from its jar model, with any texture not yet in `textures.json` appended last.
  - `verify_reference.py` checks the sounds against the asset index when it is present. The README gets the new source and files.
  - Existing files stay byte-identical and no texture, layer or model index moves (a diff proof as in torch T2).
  - Size: about 7 MB.
- **Consequence:** `SHA256SUMS.txt` changes, and `DA_Blocks.SourceSha256` is that file's hash (`BlockImportData.py:68-71`). So `ImportBlocks.py` stops with "DA_Blocks was imported from different reference data; preserve it and migrate explicitly" until `DA_Blocks` is re-stamped (Q2).
  - The item reference is untouched: its inputs (`block_facts.csv`, `creative_tabs.json`, `block_manifest.json`) stay byte-identical.
  - No world hash, golden or saved world changes.
  - The new UE assets (crack array, sounds, a sound-type table) come from a new import script of this lane, never by editing `DA_Blocks`'s content.
- **R2, cracks only:** the 10 PNGs alone. It is the same kind of change, needs the same re-stamp, and has no sounds.
- **R3, nothing from vanilla:** original cracks (C4), particles in our flat colours, no sounds. There is no reference change.

## 8. How step 3 compares them

- **Scene:** a fixed Playground row at 2.5 blocks: stone, grass block, oak planks, glass, oak leaves, sand, a bottom slab, a dandelion. The same eye pose for every switch.
- **Captures:**
  - progress 0.05, 0.35, 0.65 and 0.95;
  - +16, +50, +150 and +400 ms after the break.
  - Taken twice: TAA off for diffs, and TAA on for the look.
- **Control pairs:**
  - For each switch, 0 against 1 on the same frame.
  - The control frame must not be black, saturated or empty: mean luma 0.05-0.95, under 2% clipped pixels, and variance inside the block's rectangle above a floor.
  - The changed pixels must sit inside the block's rectangle and be close to 0 outside it.
- **Outputs:**
  - contact sheets per idea, beside VP1's frames;
  - cost: `LastViewMs` mean and p99 over a 10 s loop, `stat unit`, and `ProfileGPU`'s translucency pass, with the machine state logged;
  - sounds: the event log, and the recording if it is allowed.
- **Tests (pure functions):**
  - the stage mapping and the per-pixel map from the 10 PNGs;
  - particle spawn per outline box;
  - sound selection: block → type → event → file, volume and pitch;
  - the hit cadence against reference steps;
  - the swing clock.

## 9. Files step 3 would touch, against the other lanes

- **New:**
  - `Source/Unnamed/Foundation/FoundationBreakEffects.h/.cpp` (cracks, particles, sounds, swing clock, the `foundation.Break.*` switches);
  - `Source/Unnamed/Tests/FoundationBreakTests.cpp`;
  - `Scripts/Editor/ImportBreakAssets.py` (+ `BreakImportData.py`) and the re-stamp script if Q2 (i);
  - `Content/Breaking/*` (crack array, crack and particle materials, sound waves, an attenuation, the sound-type table);
  - `Docs/Breaking.md`, plus this lane's record and handoff.
- **Changed:**
  - `FoundationInteractionView.h/.cpp`: calls into the effects from `HandleEvents`, `DrawSelection`, the crack draw and `UpdateHeldItem`;
  - `FoundationTourProbe.cpp` and `Scripts/TestFoundationRuntime.ps1`: a break-sequence capture suite;
  - with R1: the block reference's tools, README, `SHA256SUMS.txt` and new files, plus the `DA_Blocks` re-stamp.
- **Against the lanes:**
  - None of these is in S9's, T3's or items S3's lists. I stay out of `FoundationInteractionProbe.cpp` (T3 and S3), `VoxelConsole.cpp`, `VoxelWorld.cpp` and `Docs/Blocks.md`.
  - `UMenuPreferences` is only read (O2, P3).
  - The one later seam is S4's held-item work in `UpdateHeldItem` (section 2).
- **Numbers that hashes, goldens or saved worlds depend on:**
  - The switches are render-only with default 0, so they shift none.
  - R1 or R2 changes the block reference's `SHA256SUMS.txt` and `DA_Blocks`'s digest, and nothing else.

## 10. Questions for Jesus

1. **Q1, asset source:**
   - (a) R1, vanilla cracks, sounds and particle textures through the block reference. **Recommended**: it is the route the project already takes for block textures, and it is the only way to match vanilla.
   - (b) R2, vanilla cracks only, no sounds for now.
   - (c) R3, all original.
2. **Q2, if (a) or (b), the `DA_Blocks` digest:**
   - (i) a small migration that changes only `SourceSha256`, with a T3D export proving nothing else moved. **Recommended.**
   - (ii) a full `DA_Blocks` re-import by the documented route.
   - (iii) change `BlockImportData.py` to hash only the files it reads. This is a rule-level change; not recommended.
3. **Q3, sound scope:**
   - (a) break and hit only, 6.86 MB. **Recommended**: it is this lane's scope.
   - (b) all five kinds now, so placing and walking can use them later.
4. **Q4, better-than-vanilla picks to prototype:** C2 smooth cracks, P4 spray, S2 pitch spread, X1 break pop, and W3 as an alternate. **Recommended: all five**, each one switch away. C3, P5 and X2 not now.
5. **Q5, the client probe VP1:** about 30 minutes of a windowed 26.3 client driven by synthesized input, measuring what the wiki leaves out (section 4.4).
   - (a) run it before step 3's side-by-side pack, at a time Jesus isn't playing. **Recommended.**
   - (b) skip it and tune by eye from the wiki facts.
   - (c) run it together with the items lane's P1.
6. **Q6, the menu's saved Particles and High Contrast Block Outline prefs:**
   - (a) drive O2 and P3 from them. **Recommended**: they exist in the menu, so this is small.
   - (b) switches only for now.
7. **Q7, particle textures:**
   - (a) the model's `particle` texture through R1 (grass block → dirt, decorated pot → terracotta). **Recommended.**
   - (b) the default state's side texture: no extra fact, but wrong for grass and a few others.

## 11. Evidence and token use

- **Evidence:** section 4.5's sandbox files, and the capture of ours in section 3.
- **Lead context:** 197k measured (MeasureAgentUsage) after the research, before this doc.
- **Subagent peaks:**
  - sound-type dump (Sonnet 5.5): 57k;
  - crack textures and sounds.json (Haiku 5.5): 124k;
  - wiki (Sonnet 5.5): 75k.
