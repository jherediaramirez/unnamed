# Voxel world handoff: phase 6, into S1b (facts and feature probes) - October 9, 2026

Read this first if you are picking up the voxel world in a new chat. Phase 6's S1 is done and committed (`985c559`): the decoration data the overworld reaches is in the worldgen reference, and the strict reader turns it into `FVoxelFeatureGraph` with its per-step order. The night manager split S1 at its 300k checkpoint: the jar-side tooling moved to **S1b** (this handoff). S1b is `WorldgenFacts.java`'s additions and the feature probe harness with the ore probes. It ends with a handoff into S2 and **does not start S2**; Jesus reviews S1 and S1b before S2. This handoff replaces [the S1 handoff](2026-10-09-Voxel-Handoff-Phase6-S1.md) as the entry point. Facts were true at `985c559`; check `git status`, `git log -5` and newer records before trusting them.

## 1. Read these, and only these

Budget: about 100k of reading. Everything S1 settled is summarized here, so the S1 handoff and the plan are needed only in the ranges below.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md) (its handoff pointer is stale; this handoff wins).
2. **This handoff.**
3. **[The phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md):** lines 42 to 78 (1.3 how the data gets in, 1.4 code-made parts), 131 to 137 (3.3 ore veins), 225 to 248 (S1), 315 to 323 (S4, the vein probe's user) and 643 to 667 (Jesus's answers).
4. **[The scout's research](2026-10-09-Voxel-Phase6-Scout-Research.md):** lines 563 to 599 (section 5: the facts to dump, the probe harness).
5. **Code, in ranges:**
   - `Reference/minecraft-worldgen-26.3/tools/WorldgenFacts.java`, all 253 lines (the file S1b extends; it runs no `WorldLoader` yet).
   - `Reference/minecraft-worldgen-26.3/tools/build_reference.py`, lines 1 to 30 (usage, `--probes`) and 155 to 200 (arguments, the probe staging check, the copy).
   - `C:\dev\sandbox\scout-s5s6\java\SurfaceProbe.java`, lines 37 to 95 and 156 to 175: the working `WorldLoader` bootstrap (dedicated-server packs, `updateComponentsAndStaticRegistryTags`), `RandomState`, and running the jar's own `surfaceSystem().buildSurface` on a hand-made `ProtoChunk`. The ore-vein probe is this with the overworld tree's `ore_vein` rule. The sandbox is disposable but still there.
   - `Source/UnnamedVoxel/Decoration/VoxelFeatureOrder.h` (50 lines) and `Source/Unnamed/Tests/VoxelFeatureTests.cpp` lines 313 to 400 (`Features.Order`, which the vanilla comparison joins).
   - `Source/Unnamed/Voxel/VoxelFeatureReader.cpp` lines 41 to 299, only if you need which fields hold int, float or height providers (the schema table: one line per type).

## 2. Working agreement with this user

Unchanged from [the S1 handoff](2026-10-09-Voxel-Handoff-Phase6-S1.md), section 2 (manager, look calls, context checkpoints at about 300k and every 200k measured with `MeasureAgentUsage`, subagent rules, commits on `feature/voxel-world`, the machine). Points that matter most for S1b:
- **The manager:** Nyx at night, Hemera by day. Send a short plan (files and why) and wait for the go-ahead; report before and after committing; the manager pushes, never you.
- **The reference** (`Reference/minecraft-worldgen-26.3/`) is hash-checked. It changes only through its tools: `WorldgenFacts.java` writes `facts/`, and `build_reference.py` copies data and staged probe facts and rewrites `manifest.json` and `SHA256SUMS.txt`. Every existing `data/` and `facts/` file must stay byte-identical: the 4 existing facts that `WorldgenFacts.java` regenerates included. `SHA256SUMS.txt` may change only by new entries, plus the lines of the tools and `README.md` that S1b edits. `verify_reference.py` must pass. Report the new files with their sizes.
- **Probe outputs** enter `Reference/` only through `build_reference.py --probes <staging folder>`. `feature_probe.py` writes to a staging folder; it never writes into `Reference/`.
- **The server** runs under `C:\dev\sandbox\s1-feature-probe\` (an `eula.txt` there is fine, as on Jesus's local test servers), bound to `127.0.0.1` on a spare port, `online-mode=false`, so no firewall prompt. Check `Get-Process java, javaw, UnrealEditor*` before each run. If Minecraft (`javaw`) is running, leave it alone and say so. Make sure no `java` is left running at the end.
- **Public API only** in `WorldgenFacts.java`: no reflection into private internals (the scout's `x0()` reflection on `OreVeinRule` constants is not a pattern to copy). If the vein densities need private API, record the blocks per position and seed only.

**Jesus's direction on ores** (October 9, 19:28, to the night manager): "use Minecraft actual data as far as where these ores can be found as a guidance and than we can search better ways to do it too. Something that looks better."
- **Reading:** vanilla 26.3's ore data is the baseline and the default. Better-looking options come later as look calls, with vanilla one switch away.
- **S1b's ore probes are the measured baseline.** Carry this direction, verbatim, into S2's short prompt and handoff.

**Research for S2** (both in progress at the time of writing):
- `C:\dev\sandbox\scout-decoration-pipeline\facts.md` (scout C, the decoration pipeline);
- `C:\dev\sandbox\scout-ores\facts.md` (scout E, ores).

Name them in S2's reading list once they exist. S2's worker commits their findings into `Docs/Records` (the sandbox is disposable).

## 3. Where things stand

| Phase | Scope | Status |
|---|---|---|
| 0 to 5 | Scaffolding to biomes and surface | Done |
| 6 plan, S0 | Decoration plan; AMPLIFIED speed-up | Done (`e930c52`) |
| 6 S1 | Worldgen data and reader | **Done** (`985c559`) |
| 6 S1b | Facts and feature probes | **Next** |
| 6 S2 to S12 | Decoration, after Jesus's review | |

**S1 built** ([Voxel world](../VoxelWorld.md), "Worldgen data"):
- **Reference:** `build_reference.py` reaches decoration from the 56 biomes' `features` and `carvers` lists key by key. A string names a placed feature, feature or provider only at the keys that hold one (`PLACED_KEYS`, `PROVIDER_KEYS`), because provider ids such as `flower_plain` are also placed-feature names. It copies 210 placed features, 182 features, 8 providers, 3 carvers and 127 block tags: the tags the data names, the 24 the code reads by name (`CODE_TAGS`, each with its use), and every tag those include. That is 530 files, 458,814 bytes. `verify_reference.py` resolves every reference, requires every file to be reached, and finds all 1,102 block ids in the blocks reference's registry list. `--probes` is in place for S1b.
- **Core** (`Source/UnnamedVoxel/Decoration/`): `FVoxelFeatureGraph` (typed nodes, children before parents, `Ref`s to named entries, raw tags with `ExpandTag`, per-biome step lists, `Validate`). `FVoxelFeatureOrder`: per step, a topological sort with the smallest id first; 0, 4, 5, 4, 4, 0, 34, 7, 3, 109 and 1 features, 171 in all. `VoxelFeatureSeeds`, Jesus's answer 4: the salt is FNV-1a 64 over the id's UTF-8 bytes plus `Mix64`; streams use `Hash64(seed, DecorationSalt ^ salt, x, z, step)` and patterns `Hash64(seed, PatternSalt, data seed)`.
- **Reader** (`Source/Unnamed/Voxel/VoxelFeatureReader.cpp`): a strict schema with one line per kind and type. Errors carry the file and the field path. It writes the files back (530 of 530 equal as JSON). It loads and parses in parallel, then reads; a read takes 33 to 46 ms in tests. `VoxelTerrainFactory::Make` reads it at world start; an unreadable set refuses a saved world. The graph is held in `FVoxelDensityTerrainSettings::Features` and generation does not use it yet.
- **Tests:** 5 new (`Features.Read`, `.Strict`, `.RoundTrip`, `.Order`, `.Seeds`), 66 `Unnamed.Voxel` in all. Mutation checks: unknown fields accepted fails `Strict`, a flipped tie-break fails `Order`, a changed salt fails `Seeds`.

**S1's gates** (world-start time in [VoxelWorld.md](../VoxelWorld.md), "Worldgen data"):
- Radius-8 hashes unchanged in both load orders (`0xeb1c3d76838e2481`, `0xcb42685280f8b57c`, `0xc4403ff66465fcf0`), goldens unchanged.
- The density bench, S0 and S1 DLLs alternated, 19:06 to 19:09, with no java/javaw/UnrealEditor running. Medians of 3, ms:

  | Preset | Old | New |
  |---|---|---|
  | Overworld | 3.32 | 3.22 |
  | Large biomes | 3.97 | 3.59 |
  | AMPLIFIED | 3.99 | 4.13 |

  That is run-to-run noise; no column code changed.

## 4. S1b: what to do

Scope (decided by Nyx for Jesus, October 9):

1. **`WorldgenFacts.java`:**
   - **Bootstrap:** add the `WorldLoader` bootstrap (SurfaceProbe's main, lines 156 to 175). The existing four facts must come out byte-identical: run, then `git diff --stat Reference/` shows no facts change.
   - **`facts/provider_histograms.json`:** every distinct int, float and height provider in the reachable data, decoded with the jar's codecs (`IntProvider.CODEC`, `FloatProvider.CODEC`, `HeightProvider.CODEC`) and sampled 10^6 times from a fixed seed. Write ints as counts per value and floats as fixed bins plus percentiles. Height providers need a `WorldGenerationContext` for min Y -64 and height 384; SurfaceProbe builds one from a `NoiseBasedChunkGenerator`.
     - Which fields hold which provider: the reader's schema (`int_provider`, `float_provider`, `height_provider`), or decode by key in Java.
     - Constants (bare numbers) need no histogram.
     - S2's `Providers.Distribution` test compares against this file.
   - **`facts/feature_order.json`:** vanilla's per-step order as the server builds it. Use `FeatureSorter.buildFeaturesPerStep` over the overworld biome source's `possibleBiomes()`, the same call `ChunkGenerator` makes (check signatures with `javap -public`; the jar is unobfuscated). Add the comparison to `Features.Order`:
     - every pair the biome lists constrain must agree with ours;
     - report how many unconstrained pairs differ (our ties go by id, vanilla's by its own walk).
   - **`facts/ore_vein_samples.json`:** the overworld tree's `ore_vein` rule (inside `material_rule/overworld.json`), run by the jar's surface system on stone-filled chunks over the vein Y ranges for 2 or 3 seeds. Record the block per position, plus the three densities (`ore_vein/vein_toggle`, `vein_ridged`, `vein_gap`) if the public API reaches them. S4 builds the evaluator against it.
2. **`tools/feature_probe.py` (new) and the 24 ore probes:**
   - **The 24 features:** `ore_andesite ore_clay ore_coal ore_coal_buried ore_copper_large ore_copper_small ore_diamond_buried ore_diamond_large ore_diamond_medium ore_diamond_small ore_diorite ore_dirt ore_emerald ore_gold ore_gold_buried ore_granite ore_gravel ore_infested ore_iron ore_iron_small ore_lapis ore_lapis_buried ore_redstone ore_tuff`.
   - **A suggested design** (not yet tried):
     - **World:** a superflat world from `server.properties`' `generator-settings`, with layers and no decoration: bedrock, about 47 deepslate, 32 tuff spanning Y -16 to 15, then stone, so blobs meet stone, deepslate and the `height_specific_ore_replaceables` band.
     - **Placement:** `/forceload` the area, then `/place feature minecraft:<ore> x y z` on a grid spaced wider than any blob (24 blocks), about 100 placements per ore per band.
     - **Air exposure:** add a set with a 1-block air plane through the origin (`/fill` in pieces under the block limit) to measure `discard_chance_on_air_exposure`.
     - **Reading:** `save-all flush`, stop, then a stdlib Anvil and NBT reader that diffs each placement's cell against the layer it was placed in.
   - **Output per ore:** block counts per state, extents relative to the origin, the target state by Y in the tuff band, and the exposed against solid ratio, all as distributions (vanilla's random differs per call).
   - **Staging and copy:** stage to a folder outside `Reference/`, then run `build_reference.py --probes <folder>`.
3. **Then stop:** a handoff into S2 (the decoration core, with ores). Do not start S2.

**Gate:** no column stage changes. That means goldens, the radius-8 hashes and `Unnamed.Voxel` (66 plus any new tests). The density bench need not run again unless C++ outside the tests changes.

## 5. Notes from S1

- **Engine strings:**
  - `FJsonObject::Values` keys are `UE::TSharedString` in 5.8; use `Pair.Key.ToView()`.
  - `FString` equality and `TMap<FString>` lookups ignore case, so the reader compares words and type names with `ESearchCase::CaseSensitive`.
  - `FJsonValue::TryGetString` converts numbers to strings, so check `Type == EJson::String`.
- **Reading time:** opening 587 small files one after another took 55 ms, more than parsing them. The prefetch (`ParallelFor`) brought the whole read to 33 to 46 ms in tests, and 37.6 to 39.3 ms at world start (gate 50 ms).
- **`build_reference.py` runs as `python -I`**, so it cannot import a sibling module. The verifier keeps its own copy of the key sets and code tags. The C++ schema was written separately from the inventory, and `Features.Read` requires every file to be read, so a reference key the Python walker misses would fail there.
- **Templates and block entities** are read (desert well, sulfur spring, fossils) but deferred to phase 7, as Jesus answered (Q2).
- **Patches:** multi-line patches go through a Python script written with the Write tool (Bash heredocs with mixed quotes break). Anchors move after clang-format, so re-check them.

## 6. Known issues, each with when it is due

| Issue | Due |
|---|---|
| `CLAUDE.md` lines no worker or manager may edit | **Questions for Jesus**, below |
| The decoration read is close to its 50 ms gate: 33 to 46 ms in tests, 37.6 to 42.0 ms at world start (editor and package), but 174.9 ms on the package's first run after staging (151.8 ms loading cold files). Levers: one packed file per folder at build time, or a cache per reference root (also halves `Check` plus `Make`) | Accepted by Nyx (gate met warm); the cold first run is on Jesus's morning list, levers for a later session |
| 85 decoration blocks are not in the block table yet | S5 |
| Ore veins give nothing; the freeze pass runs before trees | S4; S3 |
| The others in [the S1 handoff](2026-10-09-Voxel-Handoff-Phase6-S1.md), section 6 | As listed there |

**Questions for Jesus** (each a `CLAUDE.md` line a worker may not edit; recommendation first):
1. **Entry handoff:** the project notes still name [the phase 6 handoff](2026-10-09-Voxel-Handoff-Phase6.md) as where a new chat starts. Recommend pointing at the records index (`Docs/Records/README.md`, "the first voxel handoff"), so the line stops going stale.
2. **Test count:** the Commands block says `TestVoxel.ps1` runs 61 `Unnamed.Voxel` tests; it is 66 since S1. Recommend dropping the number, or updating it.
3. **Facts path:** the reference rule says facts come "only through `tools/WorldgenFacts.java`, then `build_reference.py`". If S1b adds `tools/feature_probe.py`, recommend "only through `tools/WorldgenFacts.java` and `tools/feature_probe.py`, then `build_reference.py`". The probe facts already enter only through `build_reference.py --probes`.

## 7. Code map: S1

| What | Where |
|---|---|
| Reachability, code tags, `--probes` | `Reference/minecraft-worldgen-26.3/tools/build_reference.py` |
| Decoration checks | `Reference/minecraft-worldgen-26.3/tools/verify_reference.py` (after the biome checks) |
| Graph, `Validate`, `ExpandTag` | `Source/UnnamedVoxel/Decoration/VoxelFeatureGraph.h/.cpp` |
| Order, seeds | `Source/UnnamedVoxel/Decoration/VoxelFeatureOrder.h/.cpp` |
| Strict reader, schema, writer, prefetch | `Source/Unnamed/Voxel/VoxelFeatureReader.cpp` (declared in `VoxelWorldgenReader.h`) |
| World start | `Source/Unnamed/Voxel/VoxelTerrainFactory.cpp` (`Make`); `FVoxelDensityTerrainSettings::Features` |
| Tests | `Source/Unnamed/Tests/VoxelFeatureTests.cpp`; `Scripts/TestVoxel.ps1` (66) |
| Docs | `Docs/VoxelWorld.md` ("Status", "Worldgen data", "Tests"); the reference's `README.md` |

## 8. What worked and what failed in S1

**Worked; repeat it:**
- **An inventory first:** a 60-line Python script listed every reachable type with its fields and value shapes, the schema was written from it, and the strict reader's own errors found the 3 gaps in 3 runs.
- **Errors with field paths:** `features[0].data.feature.features[1].feature.templates[0].data: unexpected an object` pointed straight at the gap.
- **Combined mutation run:** three mutations in separate code paths, one build and one test run, then revert.
- **Split reviews:** two Sonnet reviewers on about 1,300 diff lines each, with data excluded, peaked under 101k each.

**Failed first; avoid it:**
- **Reachability by id alone:** provider, placed-feature and feature names collide, so the walk has to know which keys hold which kind.
- **Splitting `[block/state]` at every `/`:** the schema parser has to split at the top level only.
- **The 300k checkpoint report was sent at 362k:** big file writes (the reader, about 25k tokens) jump the counter. Measure right after any large write.

## 9. Commands and tokens

```powershell
uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/build_reference.py [jar] [--probes <staging>]
uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/verify_reference.py
& C:\dev\sandbox\phase5s2\build.ps1 [-NoUnity]
& C:\dev\sandbox\phase6s1\runtests.ps1 [-Filter Unnamed.Voxel.Features] [-Label x]   # one filter, short report
& C:\dev\sandbox\phase6s0\gate.ps1 -Name <label>          # radius-8 hashes + every Unnamed.Voxel test
& C:\dev\sandbox\phase6s1\final-suites.ps1                # every pre-commit check, plus the pre-S1 world
& C:\dev\sandbox\phase5s6\measure.ps1 -Session <id>        # MeasureAgentUsage for a session started in C:\dev
```

JDK 25: `C:\Users\jherediaramirez\.gradle\jdks\eclipse_adoptium-25-amd64-windows.2`. Classpath: `C:\dev\sandbox\mc-26.3-blocks\datagen\versions\26.3\server-26.3.jar` plus the 39 jars under `datagen\libraries` (absolute entries). Run `WorldgenFacts` outside the project, because the game leaves a `logs/` folder in the working directory.

**S1's cost** (session `0b399291-df94-5350-b338-7e70971d27b3`, measured in the wrap-up):

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | 456k (362.5k at the 300k checkpoint) | 38.0M (37.3M cache reads) | 229k |
| Review of the reader (Sonnet 5.5) | 86.5k | 0.32M | 0.1k |
| Review of the core, tests and tools (Sonnet 5.5) | 94.3k | 0.36M | 10.5k |

## 10. The short prompt for S1b

> Voxel world phase 6, session S1b: facts and feature probes. S1 (worldgen data and reader) ran before it.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-09-Voxel-Handoff-Phase6-S1b.md` first; its section 1 is the whole reading list (with line ranges), so keep to it.
>
> Build S1b only: `WorldgenFacts.java`'s `WorldLoader` bootstrap, provider histograms, vanilla's per-step order (with a `Features.Order` comparison) and the ore-vein probe; then `tools/feature_probe.py` with the 24 ore probes. Run the server as the handoff's section 2 says. Probe outputs enter `Reference/` only through `build_reference.py --probes`. Every existing reference file stays byte-identical, and `verify_reference.py` must pass.
>
> Send the manager a short plan (files and why) before building. Report as the S1 handoff's section 2 says, and measure your context at each checkpoint.
>
> Ores follow Jesus's direction (handoff section 2): vanilla 26.3's data is the baseline and the default, with better-looking options later as look calls; carry it into S2's prompt and handoff, and name the two scout research files there.
>
> After S1b the chain stops for Jesus's review: end with a handoff into S2 (the decoration core, with ores) and do not start S2.
