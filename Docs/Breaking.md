# Breaking

How mining and breaking a block look, sound and feel: the target outline, the crack overlay, break and hit particles, break and hit sounds, the swing's timing and the break pop. Every effect sits behind a `foundation.Break.*` switch whose 0 is the game as it was before the break lane. The defaults are Jesus's picks, confirmed from the in-game pack on 2026-10-10 (the pack is listed in [the prototype record](Records/2026-10-10-Breaking-Prototype.md)); every other option stays one switch away. The ideas and their vanilla facts are in the [ideas doc](Records/2026-10-10-Breaking-Feel-Ideas.md); Jesus's picks with their exact numbers are in [the prototype handoff](Records/2026-10-10-Breaking-Handoff-Prototype.md), section 4; the measured vanilla numbers are in [the VP1 record](Records/2026-10-10-Breaking-VP1-Probe.md).

The effects are presentation only. They read the authority's events, the block world and the local player's interaction snapshot; they never change gameplay state. They live in the interaction view, so the Playground and generated worlds share them.

## Switches

| Switch | Values (0 = today) | Default (Jesus's pick) |
|---|---|---|
| `foundation.Break.Outline` | 0 the view's lines; 1 vanilla (black 40%, 3 px at 1920 wide scaled by width/1920, the shape's outer edges only); 2 thinner (black 30%, 1 px, every box's edges; Jesus's 2026-10-10 pick, one switch away); 3 none. With 1 or 2, the menu's saved `highContrastBlockOutline` draws vanilla's high-contrast look instead: a 3 px rgb(87, 255, 225) core on a 9 px black halo (in 1, both scaled as its core). Widths are constant in screen pixels. | 1 (2026-10-11, Q2 a) |
| `foundation.Break.Cracks` | 0 the view's crack lines; 1 vanilla's 10 stages; 2 carved (crack 60, lit side 170, shadow side 90, 2x multiply) | 2 |
| `foundation.Break.CrackSmooth` | 0/1: the next stage's texels fade in with the stage's fraction | 1 |
| `foundation.Break.Particles` | 0 the view's cubes; 1 textured break particles (64 per outline box x the menu's Particles amount, with a 0.3 blocks/s velocity spread so they do not land in their spawn grid) and one hit chip per hit (off at Minimal) | 1 |
| `foundation.Break.ParticleCollision` | 0/1: particles land and lie on solid cells | 1 |
| `foundation.Break.ParticleSpray` | 0/1: break particles get 1.5 blocks/s along the mined face and 0.5 away from the camera; chips leave at 0.6 instead of 0.4 | 1 |
| `foundation.Break.Sounds` | 0 silent; 1 vanilla break and hit sounds per sound type; 2 the same with +-3% on a hit's pitch and at most two hit voices | 2 |
| `foundation.Break.Swing` | 0 the view's swing; 1 a 6-tick swing per press, repeating while mining, hits every 4 steps (vanilla's measured cadence); 2 the same swing with the hit on each swing's contact tick (tick 3 of 6) | 2 |
| `foundation.Break.Pop` | 0/1: at the break, a copy of the block shrinks (scale 1 - t^2) over 80 ms | 1 |
| `foundation.Place.Preview` | Not a break switch; the placing side of the same feel. 0 none, as vanilla: only the target's outline shows. 1 the destination box, its fill, the self-obstructed face square and the placed (white) and rejected (red X) flashes, the look until 2026-10-11. The preview's data (`FFoundationInteraction::Preview`, the F3 `place` line) is kept in both modes; the interacted flash on lab fixtures always shows. | 0 (2026-10-11) |

The menu's Particles pref (All 1, Decreased 0.67, Minimal 0.33) sets the particle amount; Jesus chose Minimal in the mock-up, and whether Minimal becomes the menu's default is his call (the prefs are read with `UMenuPreferences::FindPreference`; no menu file changes). Hits and the swing count reference steps (`FFoundationInteraction::Step`, 20 per second), never frame time, so they hold at any frame rate. The swing clock (`FoundationBreak::FSwingClock`: start step, duration, phase) is the interface the held-item renderer (items S4) reads; the pose itself stays the view's.

## Data flow

| Step | What | Where |
|---|---|---|
| Reference | The 10 destroy stages, `sounds.json` and the 762 break and hit oggs, each block's sound type (`facts/block_sounds.csv`), each state's particle texture (`block_particles.json`). Hash-checked, never edited. | `Reference/minecraft-blocks-26.3/` |
| Data | The carved and plain crack greys, the sound tables, the particle textures and their tint flags, and a digest of every input. Standard library only. | `Scripts/Editor/BreakImportData.py` |
| Import | The assets below; additive: an existing `DA_BreakAssets` is verified and never rebuilt, and one made from other data is refused. It never touches `DA_Blocks`. UE 5.8 imports `.ogg` natively. | `Scripts/Editor/ImportBreakAssets.py` → `/Game/Breaking` |
| Materials | `M_BreakCracks`, `M_BreakParticle` and `M_BreakOutline`; graphs are code, rebuilt on every run. | `Scripts/Editor/CreateBreakMaterials.py` |
| Runtime | `FFoundationBreakEffects`, owned by `UFoundationInteractionView`; its pools are made in BeginPlay, hidden, and `/Game/Breaking` loads the first frame a switch is on. | `Source/Unnamed/Foundation/FoundationBreakEffects.*` |

`/Game/Breaking` (`UUnnamedProjectSettings::BreakRoot`; Python `ProjectPaths.break_asset()`) is always cooked. It holds:

- `T_BreakCracks`: 16 x 16 x 20, the carved stages in layers 0-9 and vanilla's own stages in 10-19, uncompressed, nearest, one mip, linear (128 is neutral under the 2x multiply).
- `Sounds/<dir>/<name>`: the 762 sound waves, named as in `sounds.json` (`dig/stone1`).
- `SA_Break` (linear over 16 blocks) and `SC_BreakHits` (two voices, oldest stops).
- `DA_BreakAssets` (`UFoundationBreakAssets`, `FoundationBreakAssets.h`): the 176 events with their files' volume and pitch, and per block (618, the reference's order) its break and hit events, its sound type's volume and pitch (metal's pitch is 1.5), and its particle texture, tint flag and per-state overrides. `shelf_mushroom_particle` is not in `T_BlockTextures`, so `shelf_mushroom` falls back to `shelf_mushroom_stage0`.

```powershell
uv run --no-project --python 3.13 python -I Scripts/Editor/BreakImportData.py   # self-check and summary
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\Unnamed.uproject" -run=pythonscript "-script=$PWD\Scripts\Editor\ImportBreakAssets.py" -unattended -nop4 -nosplash
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\Unnamed.uproject" -run=pythonscript "-script=$PWD\Scripts\Editor\CreateBreakMaterials.py" -unattended -nop4 -nosplash -AllowCommandletRendering
```

The script paths must be absolute: a relative `-script=` resolves against the engine's binaries folder.

## How it draws

- **Outline:** translucent strips in two instanced components (halo, core), one per edge quarter, widths recomputed per frame from each strip's view depth. Outline 1 first merges the target's boxes (each 0.3 cm larger) to the shape's outer edges (`FoundationBreak::MergeOutlineEdges`: no seams between boxes, concave edges kept, so a stair has 18), and sizes each square strip so its silhouette is the asked px from any side (`OutlineStrip`: side w / k, k = (|r_U| + |r_V|) / |r_UV|). Outline 2 draws every box's 12 edges with strips w wide, as before. Width rule (2026-10-11): vanilla measured 3 px at 1920 x 1080 (VP1); outline 1 draws 3 px x max(1, width / 1920), so 4 px at 2560 and 6 px at 3840, an assumption until a 1440p vanilla measurement. Only block targets use it; statics and entities keep the view's lines. The unlit colour goes through an eye-adaptation inverse, or the cyan core reads black in daylight.
- **Cracks:** a Modulate overlay on the mined state's own meshes (the cube, slab or layer mesh, or the model's parts, as the view's `AddInstance` places them), scaled 1.002 with a small push toward the camera. The material projects the stage per face from the mesh's cell-local position, so a slab shows half a crack, and it shows over transparent texels as vanilla does. It resets on release.
- **Particles:** one instanced component of camera-facing quads (1,024 pooled), lit with an up normal so caves stay dark; each quad shows a random 4 x 4 texel patch of the state's particle texture from `T_BlockTextures`, tinted only when a tinted face of the model uses that texture (grass block's dirt is not; leaves are). Collision queries the block world's collision boxes.
- **Sounds:** `UGameplayStatics::SpawnSoundAtLocation`, plus a log of every play (time, event, file, volume, pitch), because the probes run with `-nosound`.
- **Pop:** a copy of the state as the held block draws it, shrinking about its centre; the world removes the block at the commit, as before.

## Checks

- `Unnamed.Foundation.Break.*` (14 tests): the stage mapping, the carved rule on hand-made masks and on the reference's PNGs, `T_BreakCracks` against the rule, particle counts and spawn bounds, hit chips, the hit cadence at 30, 60 and 144 FPS and through a hitch, the swing clock, the pop curve, pixel widths, outline 1's geometry (merged edge counts, the width rule, each strip's silhouette 3 px within 0.05 through a pinhole projection), sound selection and `DA_BreakAssets` spot values, particle collision.
- The capture pack: `& Scripts\TestFoundationRuntime.ps1 -Editor -BreakPack -FrameRates 60` runs the tour suite with `-FoundationTourBreak` (the break cases only; the normal tour is unchanged without it). A row of 8 blocks (stone, grass block, oak log, glass, oak leaves, sand, bottom oak slab, dandelion), each seen alone from 2.5 blocks; mining frames frozen at progress 0.05, 0.35, 0.65 and 0.95 per crack setting, outline frames per outline setting, break frames at +16, +50, +150 and +400 ms for today and the picks, each with TAA on and off; the sound log as CSV; `LastViewMs` over 10 s of mining. It refuses to start while `java` or `javaw` runs (Jesus playing), logs the machine state, and writes to `Tests/Evidence/<date>/Playground-break/`.

## Known issues

- **The high-contrast core colour goes through the tonemapper:** it reads about rgb(135, 230, 215) instead of vanilla's (87, 255, 225). When: if Jesus wants it exact (a post-tonemap pass).
- **A state with more parts than any block's default state** loses its extra parts from the crack overlay and the pop (the pools are sized like the held block's).
- **Vanilla's outline (1)** drew every box of a multi-box shape and measured 4 px, not 3, on a silhouette edge (a square strip seen at an angle projects wider); both are fixed in 1 since 2026-10-11 (merged outer edges, strips sized by the view angle). The thinner outline (2) still draws every box's edges with plain square strips: it measures 1 px; its high-contrast look a 4 px core and a 7 px halo (the pack's `pack-summary.md`).- **Cracks reset on release;** VP1 saw vanilla's last for about 0.5 s.
- **Swing 2 moves the hit sound** from vanilla's measured 4 ticks to 6, by design (Jesus's pick); Swing 1 keeps vanilla's cadence.
- **The delay between breaks** stays `FFoundationInteraction::MiningDelaySteps = 5`; the wiki's 6 belongs to items S5.
