# Water and sky handoff: DN0 done, into W2a or DN-P

October 10, 2026. DN0 (the sky reference and the timeline sampler) is done on `feature/water-sky`; its record is [2026-10-10-WaterSky-DN0.md](2026-10-10-WaterSky-DN0.md). The lane's branch was merged into `feature/voxel-world` before DN0's own work (`a15d63f`, fast-forwarded by the manager), so W1 and DN1 are in the game branch.

**Next: W2a or DN-P (the manager's call).**
- **W2a** (fluid wiring and streaming) has its own handoff, [2026-10-10-WaterSky-Handoff-W2a.md](2026-10-10-WaterSky-Handoff-W2a.md), with notes from DN1 and DN0 at its end. Both its waits are met, and it needs no quiet window except for its flood trace. **Recommended next**, since it has no wait.
- **DN-P** (the moving-sun cost probe) is the sky's next step, before DN2. Its timed runs need the manager's quiet window. Its reading list and notes are below.

## Reading list (DN-P)

1. This file.
2. [The plan](2026-10-10-WaterSky-Plan.md):
   - "Rules for sources" (above section 1);
   - section 4, "Lane rules";
   - section 5, DN-P (about 15 lines) and DN2 (what the probe's gate feeds);
   - section 6: Q14 and Q16.
3. [The DN0 record](2026-10-10-WaterSky-DN0.md): "What DN0 adds" and "What the data and the probes showed". The sampler's angles are what the sweep turns.
4. `Source/UnnamedVoxel/Sky/VoxelTimeline.h` (about 130 lines): `VoxelSky::SampleAngles`, `DirectionFromAngle` and the attribute names.
5. The T0 and T1 probe pattern the plan names: the torch records' probe sections. Find them through [the records index](README.md): the torch T0/T1 entries; read only their probe method and gate lines.
6. **Not to read:** `Reference/minecraft-blocks-26.3/research/water_research.md` and `Docs/MinecraftBlockReference.md` section 4 (plan Q18). Shader code from the client jar is not a source either.

## Working agreement

- The plan's lane rules:
  - never re-record goldens or hashes;
  - one editor build at a time;
  - timed gates go in the manager's quiet window;
  - commits stay on `feature/water-sky`, and the manager merges and pushes;
  - no `CLAUDE.md` edits (DN0 used Q3 (a)'s two lines).
- Behaviour and numbers come from the game's data, black-box probes and the wiki; never from method bodies.
- **The sky reference** is hash-checked: regenerate it only through its `tools/build_reference.py`, after moving the folder away. Its verifier is in `CLAUDE.md`'s Commands.
- **The CEF slot rule:** before a CEF e2e, wait until no other `-game` editor runs and `C:\dev\sandbox\locks\cef-e2e.lock` doesn't exist; create it, run, delete it (also on failure). Before any other windowed `-game` run, wait while the lock exists. `C:\dev\sandbox\water-dn1\cef-e2e.ps1` does this.
- **Probe servers:** a headless 26.3 server on scout N's harness needs `gamerule advance_time false` saved. Stop it with `stop` (the harness's `Server.stop`), never by killing the task, or the world loses its rules (DN0's pitfall below).

## Settled decisions

- Q1 (a), Q2 (a) (Jesus), Q3 (a) (Jesus; built in DN0), Q14 B (Jesus), Q15 (a) (Jesus, when he's home), Q16 (manager): as in the DN0 handoff and the plan.
- **DN0's calls (manager):**
  - the colour probe ran (5 (d));
  - `sweep.json` only, with no probe scripts in the reference;
  - the registry's 51 attributes over scout N's 53;
  - the colour in-betweens stay unknown until DN2's probe.

## Code map (DN0)

| Piece | Where | Notes |
|---|---|---|
| Reference | `Reference/minecraft-sky-26.3/` (33 hashed files plus `SHA256SUMS.txt`) | `attributes.json` gives each attribute's base: the overworld's value, else the wiki default. `celestial.json` maps phase index to texture. `facts/` holds the sweep and the colour probe. |
| Sampler | `Source/UnnamedVoxel/Sky/VoxelTimeline.{h,cpp}` | `FVoxelTimeline::Apply(attribute, ticks, base)` is the one call a renderer needs per attribute. Pass `FVoxelWorldClock::GetSmoothTicks` as the ticks: the total, so the moon's 8-day period works too. |
| Reader | `Source/Unnamed/Voxel/VoxelSkyReference.{h,cpp}` | `ReadTimeline(root, "minecraft:day", ...)`, `ReadOverworldBase(root, attribute, ...)`; strict. |
| Tests | `Source/Unnamed/Tests/VoxelSkyTimelineTests.cpp` (7); TestVoxel expects 135 | Oracles from scout N 5 (a) to (d) and the sweep. |

## Known issues

| Issue | When |
|---|---|
| Colour in-betweens unknown (white-sky biome at a held tick 0 is no grey; no lerp rule fits every edge). | DN2's probe, before tuning sky colours |
| Nothing is wired: the Playground's fixed rig and L_Generated's sun don't move. | DN-P's probe cvar, then DN2 |
| `IsSameLook` (S9b) ignores W1's `Fluid` and `FluidLevel`; mesher face merge only. | W3, when fluids mesh |
| The CEF e2e shares a fixed CDP port across lanes (lock rule). | A per-run port (main lane) |

## What worked and what failed

- **Worked:**
  - **The pinned-facts reference:** the builder copies the probe outputs byte for byte under pinned SHA-256s, and the verifier re-samples the data against them. That way a wrong reading of the data (or a wrong default) fails the reference itself, not just a C++ test. Six verifier mutations, each on a fresh copy.
  - **One build per mutation:** the eight sampler mutations share one function, so each was built and run alone (about 35 s each, 5 minutes for all). Each failed exactly its target test.
  - **Batched `any_of` predicates** (250 terms a command, then halving) read an exact colour from a large candidate set. The colour codec also takes a packed int (`value:-8869889`), which allows ARGB values.
  - **Bisecting plateau edges** with single checks (about 12 commands an edge) pinned where each colour track holds its keyframe values.
- **Failed, or a pitfall:**
  - **A killed probe server** loses unsaved game rules: three exploration runs read a running clock, which made every colour edge look one tick early, and tick 0 white. Use a fresh world for facts, and stop servers cleanly.
  - **Shadowing a namespace constant** (`OpenSkyLight(float SkyLightLevel)` beside `VoxelSky::SkyLightLevel`) is C4459, an error in UE 5.8.
  - **Scout N's text against the data:** 16 day tracks (18), 53 attributes (51) and 256.42 at tick 22500 (the sweep says 258.291). Check counts against the data before pinning them.
  - **A plateau assumption:** the glow's post-dusk keys (`#00b33333` at 14192, `#00b23333` at 21807) are a slow ramp, not a plateau.

## Tokens

- **DN0's lead:** about 450k at the pre-commit report (393k before the review), measured with `Scripts/MeasureAgentUsage.mjs` on a copy of the transcript (the session started in `C:\dev`; `C:\dev\sandbox\water-dn0\measure.sh` does the copy). About 150k went on the reading list. Then:
  - about 100k on the colour probe's dead ends (scans assuming greys, then the running-clock runs);
  - the rest on the merge gate, the drafts, the reference, the mutations and the records.
- **Subagents:** the two Sonnet 5.5 pre-commit reviewers, the diff split by path at 1,762 lines, peaked at 84k (C++) and 75k (tools and docs).
- **For DN-P:** expect about 100k for the reading list. Keep probe output summarized by scripts.

## Evidence

- `C:\dev\sandbox\water-dn0\`:
  - `gate-m0.txt` (the merged tree) and `gate-g1.txt` (the final gate);
  - `mutations.txt` and `mut-trial\mutations.txt`;
  - `colour\` (`colour_final.py`, `colour_scan.json`, the exploration scripts and logs);
  - `review\` (the two review diffs).
- `Tests/Evidence/2026-10-10/WaterSky-DN0-*` (on disk only).

## Open questions (with options)

1. **How DN2 reads colours before the in-betweens are known.**
   - (a) Lerp the file values (as now) and probe in DN2.
   - (b) Probe first, in DN-P's session.
   - Recommended: (a). The ends and edges are exact, and the in-between error is a look detail.
2. **The sky's time source.**
   - (a) `GetSmoothTicks` (the clock's total plus the frame's share).
   - (b) The day timeline only (the total mod 24,000).
   - Recommended: (a). The moon timeline needs the total.

## Short prompt (DN-P)

> Water and sky track, session DN-P (a parallel lane): the moving-sun cost probe. Work in `C:\dev\projects\Unnamed-water` on `feature/water-sky`. Never touch other worktrees, never push, and make no `CLAUDE.md` edits. Read `Docs/Records/2026-10-10-WaterSky-Handoff-DN-P.md` and follow its reading list. Build DN-P as in the plan's section 5: `voxel.Sky.SunSweep <rate>` turning today's directional light along the vanilla orbit (from `VoxelSky::SampleAngles`), the listed cases and measurements, and a proposed DN2 cost gate. Timed runs only in the manager's quiet window; keep goldens and hashes identical, and follow the CEF slot rule for any windowed run. Send the manager a short plan before building.

## Short prompt (W2a)

> As in [the W2a handoff](2026-10-10-WaterSky-Handoff-W2a.md), "Short prompt (W2a)": its notes from DN1 and DN0 are at its end.
