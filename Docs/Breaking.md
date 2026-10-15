# Breaking

How mining and breaking a block look, sound and feel: the target outline, the crack overlay, break and hit particles, break and hit sounds, the swing's timing and the break pop. Every effect sits behind a `foundation.Break.*` switch whose 0 is the game as it was before the break lane. The defaults are Jesus's picks, confirmed from the in-game pack on 2026-10-10 (the pack is listed in [the prototype record](Records/2026-10-10-Breaking-Prototype.md)); every other option stays one switch away. The ideas and their vanilla facts are in the [ideas doc](Records/2026-10-10-Breaking-Feel-Ideas.md); Jesus's picks with their exact numbers are in [the prototype handoff](Records/2026-10-10-Breaking-Handoff-Prototype.md), section 4; the measured vanilla numbers are in [the VP1 record](Records/2026-10-10-Breaking-VP1-Probe.md).

The effects are presentation only. They read the authority's events, the block world and the local player's interaction snapshot; they never change gameplay state. They live in the interaction view, so the Playground and generated worlds share them.

## Switches

| Switch | Values (0 = today) | Default (Jesus's pick) |
|---|---|---|
| `foundation.Break.Outline` | 0 the view's lines; 1 vanilla (black 40%, 3 px at 1920 wide scaled by width/1920, the shape's outer edges only; blended into the final image after tone mapping, as vanilla's, about 0.6x in daylight: the strips draw M_BreakOutlineMask into custom depth and the post-process M_BreakOutlineShade darkens over them, unless high contrast or `foundation.Break.OutlineTonemapped 0`, the earlier blend before tone mapping); 2 thinner (black 30%, 1 px, every box's edges; Jesus's 2026-10-10 pick, one switch away); 3 none. With 1 or 2, the menu's saved `highContrastBlockOutline` draws vanilla's high-contrast look instead: a 3 px rgb(87, 255, 225) core on a 9 px black halo (in 1, both scaled as its core). Widths are constant in screen pixels. | 1 (2026-10-11, Q2 a) |
| `foundation.Break.Cracks` | 0 the view's crack lines; 1 vanilla's 10 stages; 2 carved (crack 60, lit side 170, shadow side 90, 2x multiply) | 2 |
| `foundation.Break.CrackSmooth` | 0/1: the next stage's texels fade in with the stage's fraction | 1 |
| `foundation.Break.Particles` | 0 the view's cubes; 1 textured break particles (64 per outline box x the menu's Particles amount, with a 0.3 blocks/s velocity spread so they do not land in their spawn grid) and one hit chip per hit (off at Minimal) | 1 |
| `foundation.Break.ParticleCollision` | 0/1: particles land and lie on solid cells | 1 |
| `foundation.Break.ParticleSupport` | 0/1: a resting particle checks the cell under it every frame and falls again once its block is gone (2026-10-13; 0 leaves it where it landed) | 1 |
| `foundation.Break.ParticleLife` | 0 1.5-2.3 s; 1 0.2-2.0 s (0.2 s / (0.1 + 0.9u): median 0.36 s, 1 in 6 past 0.8 s), shrinking to nothing over the last 0.1 s; 2 the same lives popping out at full size, as vanilla. The spread is a fit to VP1's black-box counts (9 fragments left at 0.8 s at All, the last gone at about 2 s), unverified: VP1 could not measure the spawn count or single lives (2026-10-13) | 1 |
| `foundation.Break.ParticleSpray` | 0/1: break particles get 1.5 blocks/s along the mined face and 0.5 away from the camera; chips leave at 0.6 instead of 0.4 | 1 |
| `foundation.Break.Sounds` | 0 silent; 1 vanilla break and hit sounds per sound type; 2 the same with +-3% on a hit's pitch and at most two hit voices | 2 |
| `foundation.Break.Swing` | 0 the view's swing; 1 a 6-tick swing per press, repeating while mining, hits every 4 steps (vanilla's measured cadence); 2 the same swing with the hit on each swing's contact tick (tick 3 of 6) | 2 |
| `foundation.Break.Pop` | 0/1: at the break, a copy of the block shrinks (scale 1 - t^2) over 80 ms | 1 |
| `foundation.Place.Sound` | Not a break switch: the place sound. 0 silent, the behaviour until 2026-10-11. 1 a place plays the placed block's place event at its break's volume and pitch (1.0 and 0.8 x the sound type's, as minecraft.wiki's Sounds tables list for both, e.g. Stone). The block reference carries only the break and hit events' files, so 66 blocks (28 place events, such as deepslate, amethyst, calcite, sculk and the copper bulbs) play their break event instead (Docs/KnownDifferences.md). Independent of `foundation.Break.Sounds` (break and hit sounds only). Logged in the sound log with `bPlace`; the tour's break pack turns it off. | 1 (2026-10-11) |
| `foundation.Place.Preview` | Not a break switch; the placing side of the same feel. 0 none, as vanilla: only the target's outline shows. 1 the destination box, its fill, the self-obstructed face square and the placed (white) and rejected (red X) flashes, the look until 2026-10-11. The preview's data (`FFoundationInteraction::Preview`, the F3 `place` line) is kept in both modes; the interacted flash on lab fixtures always shows. | 0 (2026-10-11) |
| `foundation.Blocks.Families` | Not a break switch: the survival B1 blocks (310 family blocks, see [Blocks](Blocks.md)). 0 hides them from the creative inventory and the Playground gallery tails, and refuses to place one (`UnknownBlock`). The creative screen is cached, so set it at launch. | 1 (2026-10-14) |
| `foundation.Blocks.Shapes` | Not a break switch: survival B2's shapes (see [Blocks](Blocks.md), "Shapes and collision (B2a)"). 1: the collision gatherers also take the boxes that leave the cells just below and above a region (1.5-tall fences, walls and closed gates; the pitcher crop's lower half), and stairs, fences, walls, panes, bars and gates place and update their shapes (B2b, "Shapes and connections (B2b)"). 0: B1's gather and default-state placement. | 1 (2026-10-15) |
| `foundation.Sound.Events` | Not a break switch: the sound events of `DA_Sounds` played through `FFoundationBreakEffects::PlayEvent` (see [Other sound events](#other-sound-events)). 0 silent, the behaviour until 2026-10-13; 1 played and logged in the sound log (neither `bHit` nor `bPlace`). Nothing calls it yet: the item-entity, eating, damage, container and footstep steps wire their own events. | 1 (2026-10-13) |
| `foundation.Sound.Trace` | Debug, not a look switch: 1 logs each break, hit, place and event sound's play call with its time (`FPlatformTime::Seconds`, Windows' QPC clock + 16777216), to set against a loopback recording of the output | 0 |

The menu's Particles pref (All 1, Decreased 0.67, Minimal 0.33) sets the particle amount; Jesus chose Minimal in the mock-up, and whether Minimal becomes the menu's default is his call (the prefs are read with `UMenuPreferences::FindPreference`; no menu file changes). Hits and the swing count reference steps (`FFoundationInteraction::Step`, 20 per second), never frame time, so they hold at any frame rate. The swing clock (`FoundationBreak::FSwingClock`: start step, duration, phase) is the interface the held-item renderer (items S4) reads; the pose itself stays the view's.

Sound output (2026-10-12): the Windows audio settings in `Config/DefaultEngine.ini` render 256-frame blocks with 2 queued, so WASAPI asks for a 960-frame (20 ms) buffer (the startup log's request) instead of the template's 1,920 frames (40 ms) at 1,024 x 1. The old values are one command-line override away: `-ini:Engine:[/Script/WindowsTargetPlatform.WindowsTargetSettings]:AudioCallbackBufferFrameSize=1024,[/Script/WindowsTargetPlatform.WindowsTargetSettings]:AudioNumBuffersToEnqueue=1`. A sound is played in the frame its block changes; a placement waits for the next reference step (up to 50 ms), as vanilla's tick does.

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
- `DA_BreakAssets` (`UFoundationBreakAssets`, `FoundationBreakAssets.h`): the 251 events with their files' volume and pitch (break and hit events, then place events; rebuilt in survival B1), and per block (928 with water and lava, the reference's order) its break, hit and place events (a block whose place files the reference lacks has its break event as its place event: 92 blocks; 2 are silent), its sound type's volume and pitch (metal's pitch is 1.5), and its particle texture, tint flag and per-state overrides. `shelf_mushroom_particle` and the bamboo fence and fence gate particles are not in `T_BlockTextures`, so they fall back to `shelf_mushroom_stage0` and `bamboo_planks`.

```powershell
uv run --no-project --python 3.13 python -I Scripts/Editor/BreakImportData.py   # self-check and summary
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\Unnamed.uproject" -run=pythonscript "-script=$PWD\Scripts\Editor\ImportBreakAssets.py" -unattended -nop4 -nosplash
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\Unnamed.uproject" -run=pythonscript "-script=$PWD\Scripts\Editor\CreateBreakMaterials.py" -unattended -nop4 -nosplash -AllowCommandletRendering
```

The script paths must be absolute: a relative `-script=` resolves against the engine's binaries folder.

## Other sound events

The sounds beyond breaking and placing (2026-10-13) come from their own hash-checked reference, `Reference/minecraft-sounds-26.3/` (its README has the event list and the rules). A separate reference keeps the block reference and `/Game/Breaking` byte-identical: `BreakImportData.py` requires the block reference's `sounds/` to hold exactly the break, hit and place files.

- **Events (333):** 21 named events, then the 86 step and 86 fall events of the imported blocks' sound types (`facts/block_sounds.csv`), then the survival program's batch (B0, 2026-10-14): 133 named events appended last, so no earlier index moves; survival B1 then appended 6 more (3 step and 3 fall: `cherry_sapling`, `ladder`, `weeping_vines`), and then the blast furnace's `block.blastfurnace.fire_crackle` (event 332), and `DA_Sounds` and `DA_BreakAssets` were appended or rebuilt to match (`ImportSounds.py` takes the old block entries as an ordered subsequence; `DA_BreakAssets` has no append path). Nothing plays them until a step calls `PlayEvent`; the list is in the reference's README.
  - Items: `entity.item.pickup`, `entity.item.break`, `item.axe.strip`.
  - Eating: `entity.generic.eat`, `entity.player.burp`.
  - Player damage: `entity.player.hurt` and its drown, freeze, on-fire and sweet-berry-bush variants, `.death`, `.small_fall`, `.big_fall`.
  - Containers: `block.furnace.fire_crackle`, `block.barrel.open` and `.close`, `block.chest.open` and `.close`.
  - Water: `entity.player.splash`, `.splash.high_speed`, `entity.player.swim`.
  - The batch: doors, trapdoors, fence gates, buttons, pressure plates, lever, hoe, shovel, crop planting (T5); smoker (T1); experience, armor, shield, arrows (T2); buckets, drink, fire and lava (T3); the first mobs, player attacks and generic entity sounds (T6).
- **Files:** 389 `.ogg` files live in the sound reference (278 of them the batch's). The other 366, mostly footsteps, already are the block hit sounds' files: they stay in the block reference, and `DA_Sounds` points at their `/Game/Breaking/Sounds` waves.
- **Import:** `Scripts/Editor/SoundImportData.py` (standard library; digest) and `Scripts/Editor/ImportSounds.py` → `/Game/Sounds` (`UUnnamedProjectSettings::SoundRoot`; Python `ProjectPaths.sound_asset()`; always cooked). It is additive like the break import, never saves anything under `/Game/Breaking`, and refuses a `DA_BreakAssets` whose block order differs. A `DA_Sounds` from older sound data is refused unless its events are an exact prefix of the new data and its blocks equal it; then only the new waves are imported and the new events appended. It makes:
  - `Sounds/<dir>/<name>`, the 389 waves;
  - `SA_Sound16` and `SA_Sound12` (linear over 16 blocks, and 12 for the chest's `attenuation_distance`);
  - `DA_Sounds` (`UFoundationSoundAssets`, `FoundationSoundAssets.h`): the events with their files' volume and pitch and their attenuation, and per block (DA_BreakAssets' order) its step and fall events.
- **Play:** `FFoundationBreakEffects::PlayEvent(Event, Location, Volume, Pitch)`, reached through `UFoundationInteractionView::GetBreakEffects()`. The caller passes vanilla's call-site volume and pitch (pickup: 0.2 and 1.6-3.4), each times the file's own. It picks a file from its own random stream, so the block sounds' draws never shift, and logs like the block sounds. `DA_Sounds` loads on the first call.

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-sounds-26.3/tools/verify_reference.py
uv run --no-project --python 3.13 python -I Scripts/Editor/SoundImportData.py   # self-check and summary
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\Unnamed.uproject" -run=pythonscript "-script=$PWD\Scripts\Editor\ImportSounds.py" -unattended -nop4 -nosplash
```

## How it draws

- **Outline:** translucent strips in two instanced components (halo, core), one per edge quarter, widths recomputed per frame from each strip's view depth. Outline 1 first merges the target's boxes (each 0.3 cm larger) to the shape's outer edges (`FoundationBreak::MergeOutlineEdges`: no seams between boxes, concave edges kept, so a stair has 18), and sizes each square strip so its silhouette is the asked px from any side (`OutlineStrip`: side w / k, k = (|r_U| + |r_V|) / |r_UV|). Outline 2 draws every box's 12 edges with strips w wide, as before. Width rule (2026-10-11): vanilla measured 3 px at 1920 x 1080 (VP1); outline 1 draws 3 px x max(1, width / 1920), so 4 px at 2560 and 6 px at 3840, an assumption until a 1440p vanilla measurement. Only block targets use it; statics and entities keep the view's lines. The unlit colour goes through an eye-adaptation inverse, or the cyan core reads black in daylight.
- **Cracks:** a Modulate overlay on the mined state's own meshes (the cube, slab or layer mesh, or the model's parts, as the view's `AddInstance` places them), scaled 1.002 with a small push toward the camera. The material projects the stage per face from the mesh's cell-local position, so a slab shows half a crack, and it shows over transparent texels as vanilla does. It resets on release.
- **Particles:** one instanced component of camera-facing quads (1,024 pooled), lit with an up normal so caves stay dark; each quad shows a random 4 x 4 texel patch of the state's particle texture from `T_BlockTextures`, tinted only when a tinted face of the model uses that texture (grass block's dirt is not; leaves are). Collision queries the block world's collision boxes each frame, and a resting particle re-checks the cell under it (`ParticleSupport`).
- **Sounds:** `UGameplayStatics::SpawnSoundAtLocation`, plus a log of every play (time, event, file, volume, pitch), because the probes run with `-nosound`.
- **Pop:** a copy of the state as the held block draws it, shrinking about its centre; the world removes the block at the commit, as before.

## Checks

- `Unnamed.Foundation.Break.*` (16 tests): the stage mapping, the carved rule on hand-made masks and on the reference's PNGs, `T_BreakCracks` against the rule, particle counts and spawn bounds, hit chips, the hit cadence at 30, 60 and 144 FPS and through a hitch, the swing clock, the pop curve, pixel widths, outline 1's geometry (merged edge counts, the width rule, each strip's silhouette 3 px within 0.05 through a pinhole projection), sound selection and `DA_BreakAssets` spot values, particle collision, a resting particle falling when its block goes (`Support`) and the life modes (`Life`).
- `Unnamed.Foundation.Sound.*` (2 tests): `Pick` (an event's file, volume and pitch) and `Assets` (every `DA_Sounds` event resolves to waves with a length under `/Game/Sounds` or `/Game/Breaking`, the attenuations, spot values, the block order against `DA_BreakAssets`).
- The capture pack: `& Scripts\TestFoundationRuntime.ps1 -Editor -BreakPack -FrameRates 60` runs the tour suite with `-FoundationTourBreak` (the break cases only; the normal tour is unchanged without it). A row of 8 blocks (stone, grass block, oak log, glass, oak leaves, sand, bottom oak slab, dandelion), each seen alone from 2.5 blocks; mining frames frozen at progress 0.05, 0.35, 0.65 and 0.95 per crack setting, outline frames per outline setting, break frames at +16, +50, +150 and +400 ms for today and the picks, each with TAA on and off; the sound log as CSV; `LastViewMs` over 10 s of mining. It refuses to start while `java` or `javaw` runs (Jesus playing), logs the machine state, and writes to `Tests/Evidence/<date>/Playground-break/`.
- The fall pair: `-ExtraArgs '-FoundationTourBreakFall'` with `-BreakPack` runs only Jesus's 2026-10-13 case. Two dirt blocks stand stacked east of the row; the player mines the upper one, then the one under it as soon as the upper one goes. Frozen frames at +50, +150, +300 and +600 ms after the second break, particles All, for `old` (support 0, life 0), `support` (1, 0) and `new` (1, 1), 24 shots `brk-fall-<config>-<age>ms`. The `break fall: <config>` checks count the particles within 1 cm of the gone block's top face: some hang there at +300 ms in `old`, none from +150 ms in the others. The full pack keeps `ParticleSupport` and `ParticleLife` at 0, so its frames are as before.

## Known issues

- **The high-contrast core colour goes through the tonemapper:** it reads about rgb(135, 230, 215) instead of vanilla's (87, 255, 225). When: if Jesus wants it exact (a post-tonemap pass).
- **A state with more parts than any block's default state** loses its extra parts from the crack overlay and the pop (the pools are sized like the held block's).
- **Vanilla's outline (1)** drew every box of a multi-box shape and measured 4 px, not 3, on a silhouette edge (a square strip seen at an angle projects wider); both are fixed in 1 since 2026-10-11 (merged outer edges, strips sized by the view angle). The thinner outline (2) still draws every box's edges with plain square strips: it measures 1 px; its high-contrast look a 4 px core and a 7 px halo (the pack's `pack-summary.md`).- **Cracks reset on release;** VP1 saw vanilla's last for about 0.5 s.
- **Swing 2 moves the hit sound** from vanilla's measured 4 ticks to 6, by design (Jesus's pick); Swing 1 keeps vanilla's cadence.
- **The delay between breaks** stays `FFoundationInteraction::MiningDelaySteps = 5`; the wiki's 6 belongs to items S5.
