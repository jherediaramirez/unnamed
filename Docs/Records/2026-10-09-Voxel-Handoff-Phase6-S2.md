# Voxel world handoff: phase 6, into S2 (the decoration core, with ores) - October 9, 2026

Read this first if you are picking up the voxel world in a new chat. Phase 6's S1 (`985c559`) and S1b (`7130f6c`) are done and committed. S1 put the decoration data in the worldgen reference and read it into `FVoxelFeatureGraph`; S1b dumped facts from the jar and built the feature probe harness with the 24 ore probes. **S2 is next:** the decoration core, with ores. It ends with a handoff into S3. This handoff replaces [the S1b handoff](2026-10-09-Voxel-Handoff-Phase6-S1b.md) as the entry point. Facts were true at `7130f6c`; check `git status`, `git log -5` and newer records before trusting them.

## 1. Read these, and only these

Budget: about 120k of reading. Everything S1 and S1b settled is summarized here.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md) (its handoff pointer is stale; this handoff wins).
2. **This handoff.**
3. **[The phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md):** lines 66 to 78 (1.4 code-made parts), 96 to 130 (3.1 cross-column writes, 3.2 randomness and order), 145 to 173 (3.5 the 5 ms limit and the gates), 249 to 283 (S2) and 643 to 667 (Jesus's answers).
4. **Scout C, the decoration pipeline:** `C:\dev\sandbox\scout-decoration-pipeline\facts.md`, lines 10 to 60 (summary; recommendations 1 to 3) and 82 to 86 (recommendation 7, the support condition). Section 4 below takes recommendations 2, 3 and 7 into S2 and leaves 1 for S3.
5. **Scout E, ores:** `C:\dev\sandbox\scout-ores\facts.md`, lines 31 to 128 (the vanilla baseline per ore, and the numbers S2 needs) and 244 to 251 (its open questions). Lines 129 to 205 are look-call proposals for after S2 lands; read their headings only, so the core leaves room for their switches.
6. **The sandbox is disposable:** commit both scouts' findings into `Docs/Records` early in S2, as their own records (`2026-10-09-Voxel-Phase6-Scout-Pipeline.md`, `...-Scout-Ores.md`), unchanged.
7. **Docs:** [VoxelWorld.md](../VoxelWorld.md) lines 107 to 146 ("Worldgen data", including S1b's facts and probe findings).
8. **The reference's facts formats:** [its README](../../Reference/minecraft-worldgen-26.3/README.md), the table rows for `facts/provider_histograms.json` and `facts/feature_probes/`; one probe file whole (`facts/feature_probes/ore_iron.json`, about 9 KB), and the docstring of `tools/feature_probe.py`.
9. **Code, headers first:** `Source/UnnamedVoxel/Decoration/VoxelFeatureGraph.h` (153 lines) and `VoxelFeatureOrder.h` (50); `Generation/VoxelArea.h` (44); `Storage/VoxelColumn.h` (66); `Blocks/VoxelBlockTable.h` (133). Open `VoxelBench.cpp` (638), `Source/Unnamed/Voxel/VoxelBlocks.cpp` (145) and `VoxelConsole.cpp` (283) only where S2 hooks in. The reader's schema (`Source/Unnamed/Voxel/VoxelFeatureReader.cpp` lines 41 to 299) only to find which fields hold which kinds.

## 2. Working agreement with this user

Unchanged from [the S1 handoff](2026-10-09-Voxel-Handoff-Phase6-S1.md), section 2: the manager, look calls, context checkpoints at about 300k and every 200k measured with `MeasureAgentUsage`, subagent rules, commits on `feature/voxel-world`, the machine. The points that matter most for S2:
- **The manager:** Nyx at night, Hemera by day. Send a short plan (files and why) and wait for the go-ahead; report before and after committing; the manager pushes, never you.
- **Look calls** go to the manager with captures, their numbers, a control pair that proves the switch took effect, and a recommendation. Keep the other option one switch away.
- **The reference** (`Reference/minecraft-worldgen-26.3/`) is hash-checked and changes only through its tools. S2 should not need to change it. A new probe goes into a fresh staging folder, then `build_reference.py --probes`; a probe file already there cannot change, because a new run gives new random numbers.
- **Server runs** (only if S2 adds probes): under `C:\dev\sandbox\`, bound to `127.0.0.1` on a spare port, `online-mode=false`. Check `Get-Process java, javaw, UnrealEditor*` before each run and leave no java running.

**Jesus's direction on ores** (October 9, 19:28, to the night manager), verbatim: "use Minecraft actual data as far as where these ores can be found as a guidance and than we can search better ways to do it too. Something that looks better."
- **Reading:** vanilla 26.3's ore data is the baseline and the default. Better-looking options (scout E's L1 to L3) come later as look calls, with vanilla one switch away.
- **S1b's ore probes are the measured baseline** (`facts/feature_probes/`, section 3). S2's ore feature is matched to them.

## 3. Where things stand

| Phase | Scope | Status |
|---|---|---|
| 0 to 5 | Scaffolding to biomes and surface | Done |
| 6 plan, S0 | Decoration plan; AMPLIFIED speed-up | Done (`e930c52`) |
| 6 S1 | Worldgen data and reader | Done (`985c559`) |
| 6 S1b | Facts and feature probes | **Done** (`7130f6c`) |
| 6 S2 | The decoration core, with ores | **Next** |
| 6 S3 to S12 | Streaming stages, ore veins, block models, the rest | |

**S1b built** ([VoxelWorld.md](../VoxelWorld.md), "Worldgen data"):
- **`tools/WorldgenFacts.java`** loads the registries as a dedicated server does (`WorldLoader` over the vanilla pack, then `updateComponentsAndStaticRegistryTags`) after the four older facts, which came out byte-identical. Three new facts:
  - `facts/provider_histograms.json` (121 KB): 119 distinct providers (71 int, 14 float, 34 height), found by the keys the reader's schema types. Each has its JSON, every use (`placed_feature/ore_iron_upper.placement[2].height`), 10^6 samples from a fixed seed (ints and heights as `[value, count]` pairs, floats as 64 equal bins over `range`), and mean, standard deviation and 11 percentiles. Heights use min Y -64 and height 384. **`Providers.Distribution` compares against it.**
  - `facts/feature_order.json` (7 KB): vanilla's per-step order (`FeatureSorter.buildFeaturesPerStep` over the normal preset's 56 biomes in `possibleBiomes()` order). `Features.Order` now checks ours: all 3,479 constrained pairs agree; 918 unconstrained pairs differ (reported only). A swapped constrained pair fails it.
  - `facts/ore_vein_samples.json` (393 KB): the overworld material rule run by the jar's surface system on stone chunks (3 seeds, 2 chunks each, Y -64 to 56), with block counts per Y and 5,575 recorded positions with the six `overworld/ore_vein/*` densities (`toggle`, `mask`, `copper_density`, `iron_density`, `richness`, `gap`; the S1b handoff's `vein_toggle`, `ridged` and `gap` were the old names). No vein block sits where its type's density is 0 or below; above 0, 67 to 69% hold one (`VEIN_SOLIDNESS` 0.7). **S4 builds the evaluator against it.**
- **`tools/feature_probe.py`** (new, 546 lines, stdlib): `run`, `read` or `all`. A headless 26.3 server, a layered superflat world (bedrock, deepslate to Y -17, tuff Y -16 to 15, stone to 83, a 1-block air layer at Y 84, stone to 111), `/tick freeze`, then `/place feature` 100 times per band at deepslate Y -40, tuff 0, stone 44 and the air layer 84, on a 32-block grid. A stdlib Anvil and NBT reader diffs each cell against its layer. Controls: a 3x3x3 gold box per band per ore read back as 27 every time, nothing else changed; contamination (cell edges, window edges, outside cells) 0 everywhere. The run took 224 s for 9,600 placements.
- **The 24 probe files** (`facts/feature_probes/ore_*.json`, 7 to 22 KB each, 282 KB in all) hold per band: placements, placed and failed counts, blocks per state as distributions (mean, std, min, max, percentiles, histogram), what was replaced, extents, block totals per offset (`by_dx`, `by_dy`, `by_dz`) and per absolute Y; and the exposure counts with their ratio.
- **`verify_reference.py`** checks the new facts: vanilla's order holds each step's listed features, every histogram counts all its samples and matches the data at each use, the vein samples name reference files and 26.3 blocks, every probe names a reached feature. `REFERENCE OK: 786 files hashed, ... 119 provider histograms, 5575 ore vein samples, 24 feature probes`.

**What the ore probes measured** (the baseline S2's `ore` feature matches; numbers per placement):

| Ore (size, discard) | Mean blocks: deepslate / tuff / stone / air | Failed of 100 | Y offsets | Exposure ratio |
|---|---|---|---|---|
| iron (9, 0) | 6.7 / 6.7 / 6.6 / 6.1 | 0 | -3 to 0 | 1.00 |
| coal (17, 0) | 22.0 / 22.1 / 22.0 / 18.9 | 0 | -3 to 0 | 1.09 |
| copper_large (20, 0) | 32.9 / 32.4 / 32.3 / 28.1 | 0 | -4 to 1 | 0.96 |
| diamond_large (12, 0.7) | 11.5 / 11.4 / 11.9 / 7.3 | 0 | -3 to 0 | 0.33 |
| diamond_buried (8, 1.0) | 5.8 / 5.8 / 5.5 / 2.3 | 31 in air | -3 to 0 | 0.0 |
| emerald (3, 0) | 0.41 / 0.53 / 0.30 / 0.37 | 60 to 73 | -2 to -1 | 0.59 |
| iron_small (4, 0) | 1.8 / 1.9 / 1.9 / 1.7 | 24 to 30 | -3 to 0 | 0.88 |
| dirt, gravel, clay (33, 0) | 111 to 114 (air 92) | 0 | -5 to 2 | 0.95 to 1.12 |
| andesite, diorite, granite, tuff (64, 0) | 680 to 687 (air 571 to 578) | 0 | -6 to 3 | 0.98 to 1.09 |

- **Blobs sit below their origin.** X and Z offsets reach -10 to 9 at size 64 and -1 to 0 at sizes 3 to 8.
- **Tuff is height-specific in 26.3** (`height_specific_ore_replaceables`): the stone ore matches tuff at Y 0 and up, the deepslate ore at Y 8 and down, and the stone ore is listed first. **The probe settles the overlap: the stone ore wins.** Every two-state ore gives its stone ore at Y 0 and its deepslate ore below 0. The small blobs reach no higher than Y 0, so Y 1 to 8 is covered only by the larger ores.
- **The air exposure ratio** (blocks next to the air layer against the rest, divided by the same in solid stone) follows `discard_chance_on_air_exposure`: about 1 at 0, 0.5 to 0.6 at 0.5, 0.33 at 0.7, 0 at 1.
- **Small sizes often place nothing:** emerald (size 3) fails 60 to 73% of placements, `ore_diamond_small` and `ore_iron_small` (size 4) 24 to 35%. A failed placement counts as 0 blocks in the distributions.
- **The air band's mean is lower even at discard 0** (iron 6.1 against 6.7): the air layer itself is never replaced.
- **Provenance:** the review fixes changed `feature_probe.py` after the run, so the night manager had all 24 probes run again with the final tool. Re-reading the original saved world gave the 24 committed files byte for byte. A fresh server run cannot (vanilla's random in `/place feature` is not seeded per run): all 24 differed in bytes, but all 91 band means agreed within 2.4 standard errors, none over 3. The committed files are the first run's (`C:\dev\sandbox\s1-feature-probe\rerun\` holds the second).

**Scout C's probes P2 and P4** (run in S1b's 30-minute timebox, through one-off scripts in `C:\dev\sandbox\s1-feature-probe\p2p4\`, normal worlds, seed 263; commit these numbers with the scout records):
- **P2, rings per status:** one chunk forced far from spawn (chunk 200, 200): rings 0 to 2 `full`, ring 3 `initialize_light`, 4 `terrain`, 5 `biomes`, 6 to 12 `structure_starts`, every chunk within 12 saved. A 3x3 forced: the same, shifted out by one ring (rings 0 to 3 `full`). So vanilla brings 2 rings around a forced area to `full`, and generates partial statuses out to 10 rings beyond those. Compare with our R+1, R+2 and R+3 in S3.
- **P4, snow on leaves by order:** snowy_taiga at chunk X (-40, 4), X's 3x3 and the column east of it, in both orders. 730 blocks of X differ between the orders (leaves, logs, snow, grass, ores, dripstone). Order A: 4 columns with snow under leaves, 28 with top leaves bare; order B: 0 and 23. With one chunk per order and no same-order repeat, order and run-to-run noise are not separated yet: **repeat the same order before reading it as MC-119599** (a follow-up).
- The 26.3 server rejects `spawn_chunk_radius` and `spawnChunkRadius` as gamerule names; spawn chunks stayed at the default (the probed rings are far away).

**S1b's gates:** the four older facts byte-identical; `data/` and `manifest.json` unchanged; `SHA256SUMS.txt` changed by 28 new entries and the lines of `README.md`, `tools/WorldgenFacts.java` and `tools/verify_reference.py`; `verify_reference.py` passes; radius-8 hashes unchanged in both load orders (`0xeb1c3d76838e2481`, `0xcb42685280f8b57c`, `0xc4403ff66465fcf0`); `Unnamed.Voxel` 66 of 66. No C++ outside the tests changed, so the density bench did not run.

## 4. S2: what to do

Scope as [the plan's S2](2026-10-09-Voxel-Phase6-Plan.md) (lines 249 to 283), with these amendments from the night manager (Nyx) and scout C. Send them in your plan; the manager confirms.

1. **The mutation check, replaced** (scout C, recommendation 3). The plan's "ore order swapped in the merge fails `LoadOrder`" cannot fail: a swapped order is still a fixed order, so every load order gives the same hash. Use these four, which must fail:
   - the merge replays in arrival order: `LoadOrder` fails;
   - Decorate reads a neighbour's decorated state: `LoadOrder` and `Oracle` fail;
   - one random stream shared across features: a new `Decoration.SaltStability` fails (remove one placed feature from the data; every other feature's writes are unchanged);
   - the order indices swapped: `Golden` and the vanilla-order check fail.
   - Widen `LoadOrder` with adversarial orders: row-major, reverse, spiral in and out, checkerboard, "all Decorates before any Finalize" against "Finalize as soon as ready", and 1, 2 and 30 workers with random job delays.
2. **The write log in 9 buckets by target column** (scout C, recommendation 2): each bucket is emitted in (step, order index, sequence) order, so Finalize does a 9-way merge keyed (step, order index, source offset), not a sort, and a bucket is freed when its target finalizes. Memory about 48% lower at R 12. Test: the merge equals a reference stable sort on random logs. Propose it in your plan; it shapes S3.
3. **S3's recompute-on-miss** (scout C, recommendation 1; S3's, not S2's): snapshots and log buckets are caches of pure functions, recomputed on a miss. A Decorate without a neighbour's snapshot regenerates it; a Finalize without a source's bucket re-runs that source's Decorate (at worst 9 Decorates and 25 Generates for one column); **a bucket aimed at a column that is already final is dropped, never applied** (re-applying would undo the player's edits). Cheap default: keep the snapshots of final columns that still have a non-final neighbour (about 112 columns, 2 MB at R 12). Tests `Pipeline.EvictionFuzz` and `Pipeline.ReturnTrip`. S2's interfaces should not rule this out: keep Generate and Decorate pure functions of (seed, column).
4. **The ore feature against the probes:** compare distributions, not blocks. Per ore and band: the mean and the histogram of blocks per placement, the failure rate, the Y offset range and `by_dy` shape, the extents, the tuff rule by Y, and the exposure ratio. Run our blob in the same layered column the probe used, so the numbers compare directly.
5. **`Providers.Distribution`:** our int and height providers sampled 10^6 times against `provider_histograms.json`, with a chi-square bound per provider (the ores reach `uniform`, `trapezoid` and constants; S2 needs only those its features reach).
6. **A support-condition slot in the write-mode enum** (scout C, recommendation 7): a write may carry an optional condition ("the block below is in tag X"), checked at replay against the merged target column. A vertical support is in the same column, so the check is exact; one across a column border is only a hint at Decorate. Without it, plants float over a neighbour's lake air (Luanti 16229 and 9830). **S2 adds only the slot; S8 uses it.**
7. **Scout E's three data facts** (its lines 114 to 128):
   - **Tries per column**, ore features only, rock blobs included: about 241 in an ordinary biome, about 355 in an emerald biome (+100 emerald, +14 infested), about 291 in badlands; magma (44 to 52) and the disks come on top. Size the bench and the 0.6 ms allowance on these.
   - **Wasted tries and an exact early-out:** in flat land about 112 of those tries (iron upper, coal upper) land above any stone. Skipping a try whose blob box lies wholly above the highest solid block of the 3x3 snapshot is exact, because ores replace only rock, **provided each try is seeded from (feature seed, column, try index)** so a skip does not shift later tries (Jesus's answer 4 allows it).
   - **Probes S2 should match:** blocks per blob per `size`, the tuff band's target, and the exposure ratio at discard 0.5, 0.7 and 1.0. S1b measured all three (section 3). **The tuff result as measured:** in the Y 0 to 8 overlap the stone ore wins; every two-state ore gives its stone ore at Y 0 and its deepslate ore at Y -3 to -1 (`ore_iron`: 614 `deepslate_iron_ore` at Y -3 to -1 against 57 `iron_ore` at Y 0, 100 placements).
8. **Ores follow Jesus's direction** (section 2): vanilla's data is the default. Leave room for scout E's switches (`voxel.Look.OreHost`, `voxel.Look.OreGlint`, `voxel.Ore.Shape`) without building them.

**Gate** (the plan): Decorate (ores) at most 0.6 ms and Finalize at most 0.2 ms per column; log and working-copy memory reported; goldens re-recorded under the decided change "ores on".

## 5. Notes from S1b

- **The jar after `Bootstrap`:** Minecraft wraps `System.out` into its logger, so a tool's prints come out as `[main/INFO]` lines; don't filter them away.
- **Single density values through the public API:** `RandomState.sampleBlockValueUncached(function, x, y, z)` and a `NoiseChunk`'s `cachingSamplers().sampleValue(...)`. On the vein densities they agree within 2.4e-7.
- **The vanilla order needs no private API:** `WorldPresets.getNormalOverworld(lookup)` gives the overworld generator; `FeatureSorter.buildFeaturesPerStep` is public; `ChunkGenerator` keeps its own result private.
- **The 26.3 dedicated server:** console answers are prefixed `System chat: `; an empty server pauses after 60 s unless `pause-when-empty-seconds=-1`; `/tick freeze` keeps gravel from falling while `place`, `fill` and `forceload` still run; region palettes write a default state as its id alone and any other as `{"id", "properties"}`, and a palette mixing the two wraps the bare ids as `{"": id}` (`feature_probe.py`'s `state_str` reads all three since the P4 world showed it); `level-type=minecraft\:flat` with a `generator-settings` JSON (layers, `"features":false`) works as written.
- **Placements:** 100 per ore per band was plenty. The 32-block grid left every blob inside its cell (size 64 spans 20 blocks).
- **Shell:** the PowerShell safety layer blocks `Remove-Item` on a variable path inside a script and `Get-Process java | Stop-Process`; stop servers with `stop` and only your own PID.

## 6. Known issues, each with when it is due

| Issue | Due |
|---|---|
| `CLAUDE.md` lines no worker or manager may edit | **Questions for Jesus**, below |
| The decoration read is close to its 50 ms gate (33 to 46 ms in tests; 174.9 ms on a package's first run after staging) | Jesus's morning list; levers for a later session |
| `feature_probe.py` is 546 lines, over the 500 its spec asked for | Accepted; split it if a later session extends it |
| `feature_probe.py`'s latent limits (S1b review): a change 2 or more blocks past a cell border would count for the neighbouring cell unflagged (only the edge rows are flagged; the 24 ores stay 6 or more blocks inside); a section whose palette holds only layer blocks is skipped, so a feature that places stone, deepslate, tuff, air or bedrock would read as nothing | Before probing such features (trees, geodes): widen the cells, and compare those sections block by block |
| Scout C's probes P1, P3 and P5, and a same-order repeat of P4 | Follow-ups through the harness; none blocks S2 or S3 |
| 85 decoration blocks are not in the block table yet | S5 |
| Ore veins give nothing; the freeze pass runs before trees | S4; S3 |

**Questions for Jesus** (each a `CLAUDE.md` line a worker may not edit; recommendation first):
1. **Entry handoff:** the project notes still name [the phase 6 handoff](2026-10-09-Voxel-Handoff-Phase6.md). Recommend pointing at the records index (`Docs/Records/README.md`), so the line stops going stale.
2. **Test count:** the Commands block says `TestVoxel.ps1` runs 61 `Unnamed.Voxel` tests; it is 66 since S1. Recommend dropping the number.
3. **Facts path:** the reference rule says facts come "only through `tools/WorldgenFacts.java`, then `build_reference.py`". S1b added `tools/feature_probe.py`. Recommend "only through `tools/WorldgenFacts.java` and `tools/feature_probe.py`, then `build_reference.py`".

## 7. Code map: S1b

| What | Where |
|---|---|
| Registries, provider histograms, vanilla's order, vein samples | `Reference/minecraft-worldgen-26.3/tools/WorldgenFacts.java` (`loadRegistries`, `writeProviderHistograms`, `writeFeatureOrder`, `writeOreVeins`) |
| Probe harness: server driver, Anvil and NBT reader, statistics | `Reference/minecraft-worldgen-26.3/tools/feature_probe.py` |
| Checks on the new facts | `Reference/minecraft-worldgen-26.3/tools/verify_reference.py` (after the spline facts) |
| Vanilla-order comparison | `Source/Unnamed/Tests/VoxelFeatureTests.cpp` (`Features.Order`, its last block) |
| Docs | `Docs/VoxelWorld.md` ("Status", "Worldgen data", "Tests"); the reference's `README.md` |

## 8. What worked and what failed in S1b

**Worked; repeat it:**
- **Public API found with `javap -public` first:** five minutes of signatures replaced any guess at the internals (`sampleBlockValueUncached`, `cachingSamplers`, `getNormalOverworld`).
- **A control in every probe region:** the gold boxes proved the reader before any ore number was trusted.
- **Black-box agreement checks beside the data:** vein blocks against densities, exposure ratios against the data's discard chances. Each would have caught a wrong reading.
- **The probe subagent ran from an exact spec** (paths, world layout, output schema, controls, run order with a trial first): one 23-minute run, about 120k of its context.

**Failed first; avoid it:**
- **A doc line written from one example:** "blobs never go above their origin" held for iron and gold, not for sizes 20 and up. Check every file before generalizing.
- **The probe's first full run** died after 22 ores on the server's empty-pause message; the trial with 2 ores was too short to hit the 60 s pause.
- **A reader proven only on a flat world:** every state in the ore world was a default state, so the palette's compound forms first appeared in P4's natural world. Exercise a reader on a natural chunk too.

## 9. Commands and tokens

```powershell
& C:\dev\sandbox\s1b-facts\run.ps1 [-NoRun]                  # compile and run WorldgenFacts (outside the project)
uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/feature_probe.py all [--grid 10] [ore ...] --server-dir <dir> --staging <dir>
uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/build_reference.py [jar] [--probes <staging>]
uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/verify_reference.py
& C:\dev\sandbox\phase5s2\build.ps1 [-NoUnity]
& C:\dev\sandbox\phase6s1\runtests.ps1 [-Filter Unnamed.Voxel.Features] [-Label x]
& C:\dev\sandbox\phase6s0\gate.ps1 -Name <label>          # radius-8 hashes + every Unnamed.Voxel test
& C:\dev\sandbox\phase5s6\measure.ps1 -Session <id>        # MeasureAgentUsage for a session started in C:\dev
```

**S1b's cost** (session `44f370e4-0d51-5b3c-930a-759d7f6cd82a`, measured at the handoff):

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | 322k (301.0k at the 300k checkpoint, already in the wrap-up) | 22.8M (22.4M cache reads) | 149k |
| Probe harness and the 24 ore probes (Sonnet 5.5) | 119.1k | 3.2M | 22.6k |
| P2 and P4 (Sonnet 5.5) | 62.1k | 1.1M | 0.2k |
| Pre-commit review (Sonnet 5.5) | 91.5k | 0.5M | 0.1k |

## 10. The short prompt for S2

> Voxel world phase 6, session S2: the decoration core, with ores. S1 and S1b (worldgen data and reader; facts and feature probes) ran before it.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-09-Voxel-Handoff-Phase6-S2.md` first; its section 1 is the whole reading list (with line ranges), so keep to it.
>
> Build S2 only: the plan's S2 (decoration context, random streams and step order, the write log and merge, Finalize, the ore modifiers, providers and rule tests, the `ore` feature, `voxel.Bench Decorate`, `voxel.OreHistogram`), with the handoff's section 4 amendments: scout C's four mutations that must fail in place of the merge-order swap, the write log in 9 buckets, a support-condition slot in the write-mode enum, and scout E's tries per column with the exact early-out. Match the ore feature to S1b's probes (`facts/feature_probes/`) and the providers to `facts/provider_histograms.json`, as distributions. Commit both scouts' research into `Docs/Records` early.
>
> Ores follow Jesus's direction, verbatim: "use Minecraft actual data as far as where these ores can be found as a guidance and than we can search better ways to do it too. Something that looks better." Vanilla 26.3's data is the baseline and the default; better-looking options come later as look calls, with vanilla one switch away. Research: `C:\dev\sandbox\scout-decoration-pipeline\facts.md` (scout C, the pipeline) and `C:\dev\sandbox\scout-ores\facts.md` (scout E, ores).
>
> Send the manager a short plan (files and why) before building. Report as the handoff's section 2 says, and measure your context at each checkpoint. End with a handoff into S3 (streaming stages and Finalize, with scout C's recompute-on-miss) and do not start S3.
