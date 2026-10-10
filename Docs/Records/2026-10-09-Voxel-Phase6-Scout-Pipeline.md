# Phase 6 decoration pipeline (S2, S3): facts from research

Scout C, 2026-10-09. Read-only research for the workers of S2 (decoration core, ores) and S3 (streaming stages, Finalize), so they need not research it themselves.

- **Sources:** minecraft.wiki, the Mojang bug tracker (often only as search snippets, because the tracker pages would not load), engine docs, issue trackers and dev blogs.
- **Clean room:** no decompiled or mapped Minecraft code was read. Other projects are described in prose, with links, and no code was copied.
- **Confidence:** **C** confirmed (a primary source states it), **L** likely, **U** unverified. Two pivotal claims, the wiki's 3x3 statements and Luanti issue 16229, were re-checked by the lead.
- **Raw notes:** `notes-vanilla.md`, `notes-engines-1.md`, `notes-engines-2.md` and `notes-streaming.md` beside this file. The ring arithmetic comes from `tools/rings.py`.

## 0. Summary

1. **The 3x3 limits are vanilla's own.** The wiki says vanilla writes features only into the 3x3 area, and builds the feature list from the biomes of the chunk and its 8 neighbours (C). The plan's reach rule and its feature-set rule match vanilla.
2. **Vanilla depends on load order. Ours doesn't.** The wiki says same-seed worlds "differ slightly depending on the players' travel routes", because "the chunk generation order may determine which of two conflicting features overwrite or suppress the other" (C).
   - The fixed merge order is therefore stricter than vanilla, not a deviation from it.
   - "The earlier tree keeps its logs" is vanilla's own first-come rule, with the winner fixed.
3. **No surveyed engine documents a push with write logs and a fixed merge order.** Engines either:
   - write directly into neighbours (vanilla, Vintage Story, Godot Voxel multipass, Luanti's overlap), which is where all the documented order-dependence bugs are; or
   - pull: each chunk recomputes the features that overlap it (Veloren, Terasology, Cuberite, Terra). That is order-independent but costs a recomputation per overlapping chunk, which is the plan's reason to keep design B as the oracle only.
4. **The nearest twins of our design:**
   - Luanti issue 16229 (2025, open) proposes "a 3x3 chunks area where at least the previous stage is complete" per stage (C).
   - Godot Voxel's multipass generator (2023) is the same idea, and its author names the problem our merge solves: results depend on which chunk writes first (C).
5. **The biggest gap in the plan:**
   - The pipeline must handle a column that reloads next to a column that is already final. Unloading goes outside-in, so returning (the flight's laps, any reversal) meets final columns whose pre-decoration snapshot and log buckets were already freed.
   - Unless snapshots and logs can be recomputed on a miss, the reloaded neighbour can never decorate, and a hole stays in view.
   - Godot Voxel names the same risk (duplicates or lighting bugs when forgotten partial chunks regenerate), and so does vanilla's MC-224729 (overhangs lost when a neighbour is saved early).
6. **The ring figures in 3.1 are low.** The rings are dilations of the view set, not circles:
   - At `ViewRadius` 12 the sets are 545 in view, 657 final, 777 decorated and **905 generated**: +38% over today's 657, not 707.
   - A straight flight generates +14% new columns per column moved. The first column to mesh needs 49 generated columns.
7. **Memory is dominated by logs, not snapshots.** Standing still at R 12, 232 whole logs are held, or 120 log-equivalents if logs are split by target column. At the plan's 40 to 180 KB per log, that is 9 to 42 MB, or 5 to 22 MB split, against 248 snapshots at about 4.7 MB.
8. **The freeze pass at Finalize** fixes a confirmed vanilla bug, MC-119599 (snow under leaves at chunk borders, still open). Record it as a decided difference.
9. **The S2 mutation check, as written, cannot fail `Decoration.LoadOrder`.** A swapped ore order is still a fixed order, so every load order gives the same hash. Recommendation 3 gives mutations that do fail.
10. **Vanilla facts still open:** the neighbour radius per status, the generation rings per load level, and whether far writes are dropped. Probes P1 to P5 (section 2.6) settle them with S1b's harness. None of them blocks S2 or S3.

## 1. Recommendations, ranked

Each says what it changes in S2 or S3.

1. **S3: treat snapshots and log buckets as caches of pure functions, recomputed on a miss.** The "Kept until" column of 3.1 then becomes an eviction policy, not a correctness rule.
   - **Rules:**
     - A Decorate that lacks a neighbour's snapshot regenerates it, since Generate is pure.
     - A Finalize that lacks a source's bucket re-runs that source's Decorate. The worst case is bounded: 9 Decorates and 25 Generates for one column. That is the bounded form of cascading generation; the unbounded form is section 4.5's first pitfall.
     - **A bucket aimed at a column that is already final is dropped, never applied.** Re-applying it after reload would overwrite the player's edits: `IfReplaceable` would put back a log the player removed.
   - **Cheap default:** keep the snapshot of each final column that still has a non-final neighbour. That band is about 112 columns, roughly 2 MB at R 12, so most misses cost nothing.
   - **Tests:**
     - `Pipeline.EvictionFuzz`: evict random snapshots and buckets at random times; every final column equals the oracle.
     - `Pipeline.ReturnTrip`: fly out past the unload ring and back; hashes equal the first visit, and edits are intact.
     - A regenerations-per-lap counter in `voxel.Info`.
   - **Why:** the flight's three laps hit this case, so S3's gate (no holes, flat memory) cannot pass without it.
2. **S2: split each write log into 9 buckets by target column.**
   - Each bucket is emitted already in (step, order index, sequence) order. Finalize then does a 9-way merge keyed (step, order index, source offset), not a sort.
   - Free each bucket when its target finalizes.
   - **Gains:** held memory falls about 48% (232 logs to 120 log-equivalents at R 12). Sorting 5k to 15k writes would take a large share of Finalize's 0.2 ms allowance.
   - **Test:** a property test that the merge equals a reference stable sort on random logs.
3. **S2: replace the mutation check with mutations that must fail.**
   - The merge replays in arrival order: `LoadOrder` fails.
   - Decorate reads a neighbour's decorated state: `LoadOrder` and `Oracle` fail.
   - One random stream is shared across features: new `Decoration.SaltStability` fails (remove one placed feature from the data; every other feature's writes are unchanged).
   - The order indices are swapped: `Golden` and the vanilla-order check fail.
   - **Add adversarial orders to `LoadOrder`:** row-major, reverse, spiral in and out, checkerboard, "all Decorates before any Finalize" against "Finalize as soon as ready", and 1, 2 and 30 workers with random job delays.
   - **Why:** Factorio 2.0.34 shows how a random stream that advances only when a placement succeeds makes output order-dependent.
4. **S3: readiness means final, not generated.** VoxelWorld.md says:
   - a cell is loaded "when its column is generated" (`FVoxelBlockStorage`);
   - "columns not generated yet count as solid";
   - the spawn waits for the 3x3 to be "generated", and its height comes from the terrain function.

   All three must switch to final, and the spawn height to the final `MOTION_BLOCKING`. Otherwise reads see pre-decoration blocks, and the player can spawn inside a trunk or in a lake. Edits to a non-final column must be impossible.
   - **Tests:** `Pipeline.ReadsOnlyFinal`, and a spawn test on a forest seed.
5. **S3: build the rings as dilations, and correct the expected figures.**
   - The figures are 905, 777, 657 and 545 at R 12 (Appendix A). The unload distance must be measured from the generated ring, R+3+margin.
   - The gate's "about +33%" becomes "+38% (905)".
   - First view: +248 Generates, 777 Decorates and 657 Finalizes. That is about 1.7 s of CPU at the allowances, about 60 ms over 30 workers, plus three more dependent stages before the centre column meshes.
6. **S3: scheduler rules and a seeded test scheduler.**
   - **Rules:**
     - Enqueue a job only when its inputs are ready, never recursively. Godot Voxel's recursive prototype reached about 100,000 tasks.
     - Rank jobs by the view distance they unblock (gap minus stages left), so a near Finalize isn't starved by a far Generate it waits on.
     - Count the demand per record (Paper, 2018) and cancel only at zero.
     - Release a cancelled job's slot when it drains, not when the cancel is requested.
   - **Tests:**
     - A single-threaded harness that picks the next ready job with a seeded random generator.
     - Random focus teleports and reversals, and oscillation across the unload ring.
     - After every step, the invariant checker in 4.6. A failure prints its seed so it can be replayed.
7. **S2 now, S8 and S11 later: give write modes an optional support condition** (for example "the block below is in tag X") that is checked at replay against the merged target column.
   - A vertical support lies in the same column, so the check is exact.
   - A horizontal support across a column border can only be checked at Decorate, as a hint.
   - Without it, plants can float over a neighbour's lake air. That is the class of Luanti 16229 ("the top block will remain stone") and 9830 (a heightmap fooled by neighbour leaves).
   - S2 only needs the slot in the write-mode enum; S8 uses it.
8. **S2: `Decoration.Reach` also counts reads outside the 3x3,** such as rule tests, air-exposure checks and heightmap queries.
   - A read past the window has no data. Vintage Story returns air with a warning, and Paper's `LimitedRegion` throws.
   - Gate: 0 reads and 0 writes outside the 3x3.
9. **S2: in `Decoration.OreHistogram`, use an effect-size bound instead of a bare chi-square p-value.** Examples: total variation distance, or the largest relative error per Y bin above a minimum count.
   - With 10^5 to 10^6 blocks, chi-square rejects differences too small to see. Pin the seeds.
10. **S3: in `voxel.Info`, report per stage the record counts, the bytes in snapshots and buckets, the outstanding demand, and the regenerations.**
    - This is Paper's `/paper holderinfo` lesson: the chunk-system rewrite shipped with an introspection command.
    - Check unloads against the 2 ms budget per column, not per batch. A hobby engine's per-batch check still gave 9.7 ms frames.

## 2. Vanilla 26.3: what is generated when, the write area, the edge (Q1)

### 2.1 Chunk statuses

- **The statuses in order:** `empty`, `structure_starts`, `structure_references`, `biomes`, `terrain`, `features`, `initialize_light`, `light`, `spawn`, `full`. Every status before `full` is a proto-chunk (C, wiki Chunk format, 2026-10-09).
- **Changes:**
  - 26.3 snapshot 10 merged `noise`, `surface` and `carvers` into one `terrain` status (L). The wiki says this was read from the game, not from the changelog, and the changelog only says "terrain now generates faster".
  - 1.20 (23w16a) removed `liquid_carvers` and `heightmaps` and added `initialize_light` (C).
  - 26.4 snapshot 1 renamed the NBT field `Status` to `status` (C).
- **What each produces** (wiki World generation, C):
  - structure starts and references;
  - biomes;
  - terrain: noise, surface rules and carvers;
  - features: "Features and structure pieces are placed and heightmaps are generated";
  - light sources, then light levels;
  - mobs;
  - full, which also runs the block updates deferred from earlier statuses.
- **Neighbour radius and required status for each status:** not documented (U). Probe P2.
- **Our mapping:** Generate = `structure_*` + `biomes` + `terrain` (phase 6 has no structures). Decorate + Finalize = `features`. Light is not modelled; block light is 0 by design (3.4).

### 2.2 The features status

- **Write area:** "Features can place block outside the current chunk's boundaries but are limited in the nearby 3×3 area." (C; quoted by the lead from the wiki.)
- **Feature list:** the game "determines a list of biomes that appear in that chunk or the 8 surrounding chunks" and builds the feature list from them (C). This is exactly plan 3.1 "Which features run". The `biome` placement modifier then keeps a position only if its own biome lists the feature.
- **The 11 decoration steps:** `raw_generation`, `lakes`, `local_modifications`, `underground_structures`, `surface_structures`, `strongholds`, `underground_ores`, `underground_decoration`, `fluid_springs`, `vegetal_decoration`, `top_layer_modification` (C, quoted). Freezing (`freeze_top_layer`) is the last step of a chunk's own pass.
- **One global order per step:** biome lists whose orders contradict each other make chunk generation fail with "Feature order cycle found" (L; a 2022 forum crash report and MC-252369, resolution unseen). This supports the plan's topological sort per step.
- **Size limits in the data docs** (wiki Custom feature, C):
  - a geode's size is "ultimately limited to within the adjacent chunks";
  - a huge fungus can't pass the world ceiling;
  - trees, lakes and icebergs: no stated limit.

  By arithmetic, a feature at a chunk edge can reach at most 16 blocks into a neighbour.
- **Seeding:** vanilla's per-index scheme (decoration seed + index + 10000 x step) is documented only in secondary sources, which we don't use. Q4 already chose id-hash salts, so this doesn't matter. P5 can confirm vanilla's index dependence if it's ever wanted.
- **Light:** `initialize_light` and `light` come after `features` (C). Light is not computed while features run.

### 2.3 What happens at the area's edge

- **Writes outside the 3x3:**
  - Vanilla logs "Detected setBlock in a far chunk", with the chunk, the position, the status and the feature (L; seen in mod listings for 1.18.2 to 1.21.1).
  - Whether the write is dropped is undocumented (U). The logged warning implies the area is bounded. Probe P3.
- **Reads outside the area:** undocumented (U). Other engines return air with a warning (Vintage Story) or throw (Paper's `LimitedRegion`, SPIGOT-8011, L).
- **Structures** are pulled, not pushed: a structure crossing a chunk edge "only places the part inside the current chunk" from stored references (C). This is relevant to later phases, not to phase 6.
- **Vanilla's border bugs** (tracker snippets; statuses to re-check live):
  - **MC-119599** (confirmed, unresolved, through at least 1.20.5): snow under tree leaves. Snow lands first, then a later neighbour's tree grows over it. Our Finalize freeze removes this by construction (3.4).
  - **MC-224729:** an overhang into a neighbour that is still generating is silently lost when that neighbour is saved. The NeoForge mod ChunkTreeFix (1.21.1, updated 2026-07) still works around it. Related: MC-188086 (trees, geodes and huge mushrooms clipped after rejoining) and MC-125007 / MC-133454 (features cut at borders in the 1.13 era). All are persistence races; for us they are the reload case of recommendation 1.
  - **MC-55596:** chunks of the same seed differ by load order (unresolved, L).

### 2.4 Load levels (wiki Chunk, C)

- **Levels:** 31 and below entity ticking; 32 block ticking; 33 border (readable, nothing runs); 34 and above "inaccessible", where world generation still happens.
- **Spread:** levels spread outward from a ticket by +1 per chunk up to 44. A player's ticket is level 31.
- **The rings vanilla actually reaches:** which status each outer level reaches is undocumented (U). Probe P2 reads it off a saved world: that is vanilla's version of our generated, decorated, final and mesh rings.

### 2.5 What this means for "Minecraft-like" tests

- Vanilla itself differs at borders between load orders, so block-for-block comparison with vanilla worlds near borders is meaningless. Compare distributions, as the plan already does. P1 measures how big vanilla's own border differences are, which gives a tolerance.

### 2.6 Probes for S1b's harness

All probes:

- **Server:** S1b's headless 26.3 server, console only, `save-all flush`, then the Python region reader.
- **Before running:** check `Get-Process java, javaw, UnrealEditor*`.
- **Settings:** view and simulation distance 2, no players, and `/gamerule spawnChunkRadius 0` if 26.3 has it (it was added in 1.20.5; U for 26.3).
- **Worlds:** single-biome worlds through a world-preset datapack with a fixed biome source.

P1 to P4 need generated worlds, not the flat test world, so each is a longer run than the ore probes: minutes, not seconds.

| # | Question | Steps | Reading the result |
|---|---|---|---|
| P1 | How large is vanilla's own order dependence? | Two worlds, same seed and preset (forest; then `snowy_taiga`). World A: `/forceload add` the chunks of a 12x12 area one at a time, row-major, waiting for each. World B: the same chunks in reverse spiral order. Flush, then diff the central 8x8 chunks block by block. | Differences only within about 16 blocks of chunk borders give the size and kind of vanilla's first-come effect, and a tolerance for any vanilla comparison. Zero differences mean that order doesn't matter in that setup. |
| P2 | Rings and neighbour radius per status | Forceload one chunk far from spawn, wait, flush, then read the `status` of every chunk within 12. Repeat with a 3x3 block forced. | The status per ring is vanilla's generation pyramid: how many outer rings sit at `features`, at `terrain` and below. Compare with our R+1, R+2 and R+3. |
| P3 | Far writes: dropped or kept? | A datapack world where one placed feature sits at a fixed position at local (15, 15) of a chunk, using `fixed_placement`, with the widest extent the 26.3 codecs allow. Check the limits in the worldgen reference (a big blob-foliage tree or a large geode). Generate the 5x5 around it. Grep `logs/latest.log` for "Detected setBlock in a far chunk"; read which chunks hold its blocks. | No blocks beyond the 3x3 plus the warning means dropped. This confirms our "drop and count". A matching probe with `/place feature` shows the uncut shape for comparison. |
| P4 | Snow on leaves, by order | `snowy_taiga` world. Order A: forceload chunk X's 3x3, flush; then forceload the column of chunks east of it, whose trees overhang X. Order B: the reverse. | Leaves without snow, or snow under leaves, in one order only confirms MC-119599 in 26.3. This is evidence for the decided change in 3.4. |
| P5 | (optional) Feature set across biome jitter | A two-biome world (`checkerboard` biome source, scale 2). Biome B alone lists a marker feature (`simple_block` of a unique block, `in_square`, `biome` filter). Count markers inside chunks of biome A near the border. | Markers in A-chunk cells whose jittered biome is B confirm that the 3x3 feature list plus the per-position `biome` filter applies, as 3.1 assumes. |

## 3. How other engines handle features across chunk borders (Q2)

| Engine | Approach | Order-independent? | Known problems | Cost |
|---|---|---|---|---|
| Vanilla | Staged push into the 3x3 at `features`, first-come | No (wiki, C) | MC-119599, MC-224729, MC-55596 | 3x3 resident; outer rings at earlier statuses |
| Luanti | Overgenerate and crop: an 80^3 mapchunk with a 16-node shell; neighbours' decorations regenerate into the shell | No (C) | #7392 dust missing only if the upper chunk generated first; #9830 heightmap takes shell leaves for ground; #16229 cut trees, stone under bushes | 112^3 working volume for 80^3 output (about 2.7x) |
| Minestom | Push through `GenerationUnit.fork()`; a fork is "applied to the instance whenever possible" | Undocumented | No ordering, persistence or unloaded-target contract | Fork lists held until applied |
| Paper / Bukkit | Staged `generateNoise/Surface/Bedrock/Caves`, then `BlockPopulator` pushing into a `LimitedRegion` (buffer 16 = 48x48) | Undocumented | The buffer "may or may not" contain generated terrain; outside the region it throws; the old API guaranteed only N/E/S/W and told plugins to defer corner writes themselves | 9 populators see the same blocks; no conflict rule |
| Forge (1.10 to 1.14) | Old population with offset +8 | No | "Cascading worldgen lag": a write into a missing neighbour generated it, recursively | Unbounded |
| Cuberite | Per-chunk finishers; grid structures pulled: every overlapping chunk redraws a structure, with an LRU cache | Yes | Structures above `MaxStructureSize` are cropped | Recomputation per overlap |
| Terra | Distributors and locators (pull, pure in position) | Yes, unless scripts read blocks | TerraScript `getBlock` force-loads chunks: a cascade | Recomputation |
| Vintage Story | Passes Terrain (no neighbours), TerrainFeatures, Vegetation, NeighbourSunLightFlood, PreDone ("neighbouring chunks can still write content into it"), Done; direct writes | Not documented as an issue | Reading a missing neighbour gives air plus a warning; per-pass radius undocumented | Column plus 8 neighbours resident |
| Veloren | Pull: a coarse world simulation precomputed at creation; each chunk's `Canvas` asks for nearby spots and draws what reaches it | Yes, if "all chunks within the bounds of a structure" use identical parameters | Mismatched parameters cut structures; 2D plots can't stack | Recomputation per intersecting chunk |
| Terasology | Pull: facets with borders; a provider's border adds up through its dependencies | Yes | Border handling incomplete (2014); an L-system tree is computed once per chunk it covers | Wider facets, recomputation |
| Godot Voxel (multipass, 2023) | Passes; pass 0 has no neighbours; later passes read and write up to 2 blocks; a chunk is final when no neighbour can still write it | No: the author says the result "can depend on which runs first" | Recursive tasks reached about 100k; 21 chunks loaded for a view of 16; forgotten partial chunks regenerate into duplicates or lighting bugs | Partial chunks in memory, or streamed to disk |
| Factorio | A chunk creates only entities inside it; others when their chunk generates | No (2.0.34) | A failed spawn doesn't advance the shared random generator, so later placements shift; closed with "no solution without side effects" | — |
| Hytale (V2, docs WIP) | Stages with growing output margins (overgenerate) | Prop order "currently non-deterministic", to be fixed | — | Margins |
| Infinite WFC (Boris the Brave) | Layered blocks offset by half a block; bounded dependency depth | Yes | Naive chunking is order-dependent | About 4x work |
| Distant Horizons, Cubic Chunks | Run Minecraft's generator (DH) or cube population | — | Undocumented; DH #809 and #607 show seams and holes at the LOD/real-chunk boundary | — |

What to take from it:

- **Order dependence shows up exactly where a feature reads or overwrites a neighbour's already-decorated blocks.** The evidence: vanilla's wiki, Luanti #7392, #9830 and #16229, Godot #545, and Factorio 2.0.34.
  - The plan's rule that Decorate reads only snapshots plus its own writes removes the reading half.
  - The fixed merge removes the overwriting half.
- **Pull designs never wait on neighbours but recompute.** Big features force wide borders: Terasology's borders add up, and Cuberite crops above a size. This confirms keeping design B as the oracle only.
- **Persistence races are the push design's typical bug:** vanilla MC-224729, and Godot's duplicates on regeneration. Our logs avoid them only if a bucket can't arrive twice and can't go missing. That is recommendation 1.

## 4. Staged streaming with neighbour dependencies (Q3)

### 4.1 Rings

- **Vanilla:**
  - Tickets spread levels outward, and generation happens beyond the border level (C). Status per level is undocumented (P2).
  - A Paper-era summary says the maximum status degrades with each level beyond 33 (L). That is a "generation pyramid" of the same shape as our R+1 to R+3.
- **Godot Voxel:**
  - Its pyramid of nested boxes, one per pass, was the proposed fix for task explosion. It was untested when written; the shipped design isn't stated (C).
  - The author's cost note: three passes need 21 chunks loaded for a view of 16.
- **Vintage Story:** `ChunkPeekOptions.UntilPass` asks for a column only up to a given pass, the same idea as "ring N reaches stage S" (C).
- **Ours:** see Appendix A. The extra share shrinks with the radius: x1.53 generated over final at R 8, x1.38 at R 12, x1.29 at R 16.

### 4.2 Cancellation and unloading

- **Paper (2018, C via commit prose):**
  - Each pending chunk counts its requesters, and a cancel decrements the count. The work leaves the queue only at zero.
  - A task's cancel claims the same run-once flag as its run, so a cancelled task never executes.
  - It re-checks "still in range" before running.
  - The 2022 rewrite staggers unloads (at least 50 per tick or 5% of the queue). It also fixed full-status updates being dropped during the unload tick.
- **Paper's leak fix:** partially generated outer-ring chunks with no loaded neighbours could stay resident. Something must own the half-staged records.
- **A hobby engine (rayengine PR #33, Oct 2026; not verified by the lead):** a cancelled running job keeps its reservation until it drains, so the caps stay honest.
- **Godot Voxel:** partial chunks are kept out of gameplay in a separate map. Forgetting far partial chunks risks duplicates when the area regenerates (C).

### 4.3 Memory caps for pending neighbour data

- **No surveyed engine caps snapshots or logs specifically.**
  - Paper caps rates per player instead (`player-max-chunk-load-rate` and others; at the cap, work queues).
  - Godot plans to stream partial chunks to disk.
- **Our live set is bounded by the ring geometry:** 248 snapshots and 120 to 232 log-equivalents at R 12. So a cap is a bug detector, not a policy: log when it is exceeded. Under recommendation 1, eviction is always safe, so a cap can never cause a hole, only extra work.

### 4.4 Avoiding holes

- **Priority:** nearest first, with urgent work for blocking needs (Paper). Rank a dependency by the view column it unblocks; that ranking is our synthesis, and no engine publishes a heading-weighted priority.
- **Keep the old mesh until the new one is applied,** and count old and new together for admission (rayengine; hobby, L).
- **Budget unloads too, checked before each column.** clear_skies (hobby, L) went from 14 ms frames to under 2 ms. A per-batch check still gave 9.7 ms.
- **Fix the flight route:** a heading that drifts makes runs incomparable (clear_skies). Our flight is already scripted.

### 4.5 Pitfalls with sources

- **Cascading generation (Forge 2017 to 2018):** a write into a neighbour that doesn't exist yet generates it, which spills further; the extreme case is a stack overflow. Ours: a write never triggers generation, because it goes to a log. The racy existence check of Forge's deferral workaround still warned now and then, so test the log path under concurrency.
- **Recursive dependency tasks:** Godot Voxel's reached about 100k tasks.
- **Persistence races:** MC-224729, and Godot's duplicates on regeneration.
- **A shared or conditional random generator:** Factorio 2.0.34.
- **Output that depends on batch shape:** a hobby voxel engine (st-voxel-action PR #50) found that 32 m and 64 m windows around the same point disagreed on 87% of shared cells. `Decoration.Oracle` (3x3) against `LoadOrder` (9x9) covers this, and it should stay.
- **Thrash at the unload boundary:** a known need for hysteresis, with no published numbers. Our margin is at least 2.
- **A priority-ordered queue holding a job that is never ready:** avoid it by enqueuing only ready jobs (synthesis).
- **Light or mesh seams when a neighbour changes after use:** Godot says to spread light only into chunks that won't change again. Ours meshes only a final 3x3, which matches.

### 4.6 Testing: what others do, and designs to adopt

- **What others publish:**
  - Paper's rewrite shipped a test checklist, a 63,001-chunk generation benchmark and introspection commands.
  - C2ME ships checked thread-local randoms to catch generator misuse across threads.
  - Deterministic simulation testing (seeded scheduler, replayable failures): RisingWave's first-in-random-out queue; Shuttle for randomised schedules; Loom for exhaustive interleavings.
- **What isn't published:** no voxel project publishes shuffled-load-order hash tests. The bug record (MC-55596, Factorio) shows what they would catch.

Designs to adopt, each with its pass criterion:

1. **Seeded scheduler (S3).** A single-threaded harness picks any ready job with a seeded random generator, with virtual frame time. Run 1,000+ seeds with random moves, teleports and reversals. Pass: no invariant violation; a failure prints its seed.
2. **Invariant checker, after every step in tests:**
   - no Mesh before the 3x3 is final, and no Finalize before the 3x3 is decorated;
   - no bucket applied to a final column;
   - no record unloaded while a queued or running job references it;
   - snapshot and log bytes within the ring bound;
   - jobs in flight at most the cap.
3. **`Pipeline.EvictionFuzz`:** evict caches at random. Pass: every final hash equals the oracle.
4. **`Pipeline.ReturnTrip`** (and the flight's laps): out past unload and back. Pass: hashes equal the first visit, edits are intact, and no holes after settling.
5. **Cancellation storm:** reverse or teleport every 1 to 5 frames for 10,000 frames, with edits injected. Pass: no stale result applied (a counter); after the storm the full view is meshed within N frames and equals the baseline.
6. **Boundary oscillation:** cross the unload ring every few frames. Pass: at most one generate/unload cycle per column.
7. **Soak:** the flight's laps with memory sampled per category. Pass: each lap starts within 5% of lap 1, which is the plan's "memory flat".
8. **Edit against neighbour stages:** an edit in C before, between and after its neighbours' Decorates and C's Finalize. Pass: equal to "generate, decorate, finalize, then replay edits". No edit is lost or applied twice.

## 5. The plan checked against the research (Q4)

| Plan point | Research | Verdict | Test or gate it suggests |
|---|---|---|---|
| 3.1 Push with write logs; design B as oracle | No engine documents a fixed merge order. Direct-write engines carry the order-dependence bugs; pull engines pay recomputation per overlap. | Supported, and novel, so the tests carry the weight | As planned, plus rec. 3 |
| 3.1 Stage table and "Kept until" | Reload next to a final column needs freed data (Godot #545, MC-224729) | **Challenged** | Rec. 1: cache semantics, `EvictionFuzz`, `ReturnTrip`, "final drops buckets" |
| 3.1 Working copy: own writes visible, never a neighbour's decoration | Luanti #7392 and #9830 come from reading neighbour-decorated state; ours avoids that | Supported | `LoadOrder`; mutation (b) |
| 3.1 Supports near borders | Decorate sees a neighbour's pre-decoration terrain: plants can float over a neighbour's lake air, and ores can sit exposed to a neighbour's geode air (deterministic) | Partly challenged | Rec. 7 (support slot); note ore exposure as a known difference |
| 3.1 Merge key and write modes | Vanilla's own rule is first-come by load order (wiki); fixing the winner is stricter | Supported; reframe "known difference from vanilla" as "vanilla's rule with a fixed order" | Rec. 2 (bucketed merge), property test |
| 3.1 Leaves' `distance` from the tree's own logs | Exact unless trees overlap; a later leaf-decay phase must recompute after the merge, across columns | Fine for phase 6 | Note for the leaf-decay phase |
| 3.1 Reach: drop and count outside the 3x3 | The wiki's "limited in the nearby 3×3 area"; geodes "within the adjacent chunks"; vanilla logs far writes | Supported | Rec. 8 (count reads too); P3 |
| 3.1 Which features run (3x3 biome set plus `biome` modifier) | The wiki: biomes of "that chunk or the 8 surrounding chunks" | Confirmed | Optional P5 |
| 3.1 Heightmaps: `_WG` from the snapshot, the others from the working copy | Luanti #9830 (heightmap contaminated by neighbour decorations) is avoided by `_WG` from the snapshot | Supported | — |
| 3.1 Rings, about 707 generated (+33%) | Dilations give 905 (+38%); +14% per straight move | **Correct the figures** | Rec. 5 |
| 3.1 Memory (5k to 15k writes, 8 to 12 bytes) | Logs dominate: 232 held at R 12 (9 to 42 MB), 120 log-equivalents bucketed; no engine caps this | Supported; measure | S2 reports peak and steady; rec. 2 |
| 3.1 Edits replay after Finalize | Vintage Story's PreDone (still receiving neighbour writes) is the state edits must wait out | Supported, plus "final drops buckets" | Design 8 in 4.6 |
| 3.1 `voxel.Decoration 0` | — | Fine | Keep the stage rings under 0, so 0 against 1 isolates decoration cost |
| 3.2 One stream per (column, step, feature id hash) | Factorio 2.0.34 (stream shifted by failed spawns); vanilla's index seeds reseed on a data change | Supported | `Decoration.SaltStability` (rec. 3) |
| 3.2 Global order per step, checked against vanilla | "Feature order cycle found" shows vanilla needs one consistent order | Supported | As planned |
| S2 `Decoration.Oracle` | Shares the merge with A, so it catches staging bugs, not merge-order bugs | Keep, but know its blind spot | Mutations (a) to (d) |
| S2 `Decoration.LoadOrder` | Bug record (MC-55596, Factorio) | Supported; widen the orders | Rec. 3 |
| S2 `OreHistogram` chi-square | Overpowered at large sample sizes | Adjust | Rec. 9 |
| S2 mutation check | Can't fail `LoadOrder` as written | **Fix** | Rec. 3 |
| S2 gate: Finalize at most 0.2 ms | A sort of 10k writes eats much of it | Supported with buckets | Rec. 2 |
| S3 stages, gating, rings | Godot multipass; Luanti #16229; Vintage Story passes; Paper demand counting | Supported | Rec. 5 and 6 |
| S3 lifetimes, cancellation, unload | Paper refcount and run-once claim; release on drain; leaks of half-staged records | Supported, with rec. 1 | Designs 1 to 6 in 4.6 |
| S3 readiness of cells, collision, spawn | VoxelWorld.md ties them to "generated" | **Change to final** | Rec. 4 |
| S3 freeze pass at Finalize | MC-119599 is confirmed and unresolved in vanilla | Supported; a decided difference | `Freeze.AfterDecoration`; P4 as evidence |
| S3 tests: two focus paths | Too few orders to catch scheduler races | Widen | Seeded scheduler, storm, return trip |
| S3 gate: flight, first view at most 4.0 s, about +33% columns | Laps exercise reloads; first view adds 248 Generates, 777 Decorates, 657 Finalizes | Supported; fix the figure | Rec. 5; report regenerations per lap |

## Appendix A. Ring arithmetic (`tools/rings.py`)

Gap metric as in VoxelWorld.md; each ring is the previous one dilated by the 8 neighbours. Today's needed set (657) matches.

| R | View | Final (+1) | Decorated (+2) | Generated (+3) | Generated / today's generated |
|---|---|---|---|---|---|
| 8 | 269 | 349 | 437 | 533 | 1.53 |
| 12 | 545 | 657 | 777 | 905 | 1.38 |
| 16 | 933 | 1077 | 1229 | 1389 | 1.29 |

- **New generated columns per column moved at R 12:** east 29 to 33 (+14%), diagonal 41 to 49. One column alone needs 49 generated before it can mesh.
- **Standing still at R 12:**
  - 248 snapshots of non-final columns;
  - 232 whole logs, of 777, with a non-final target;
  - 1,080 buckets of 6,993, equal to 120 log-equivalents;
  - final columns with a non-final neighbour: about 112 (657 - 545).

## Appendix B. Links

Vanilla:

1. https://minecraft.wiki/w/World_generation (3x3 quote, biome list, travel routes, 11 steps; read 2026-10-09)
2. https://minecraft.wiki/w/Chunk_format (statuses, 23w16a and 26.3 snapshot 10 notes)
3. https://minecraft.wiki/w/Chunk (load levels, tickets)
4. https://minecraft.wiki/w/Custom_feature (geode and huge fungus limits)
5. https://minecraft.wiki/w/Java_Edition_26.3-snapshot-10
6. https://bugs.mojang.com/browse/MC-119599 (snow under leaves)
7. https://bugs.mojang.com/browse/MC-224729 and https://www.curseforge.com/minecraft/mc-mods/chunktreefix (overhang lost on save)
8. https://bugs.mojang.com/browse/MC-188086, https://bugs.mojang.com/browse/MC-125007, https://bugs.mojang.com/browse/MC-133454 (cut features)
9. https://bugs.mojang.com/browse/MC-55596 (load-order differences)
10. https://bugs-legacy.mojang.com/browse/MC-252369 and https://forums.minecraftforge.net/topic/115306-my-game-crashes-occasionally-1182 (feature order cycle)
11. https://www.modpackindex.com/mod/53309/detected-setblock-be-gone (far-chunk warning)
12. https://docs.spongepowered.org/7.4.0/en/plugin/wgen/index.html (old population offset by 8)

Engines:

13. https://raw.githubusercontent.com/luanti-org/luanti/master/doc/lua_api.md (mapchunks)
14. https://github.com/luanti-org/luanti/issues/16229 (multi-stage proposal, 2025-06-05)
15. https://github.com/luanti-org/luanti/issues/9830 and https://github.com/luanti-org/luanti/issues/7392
16. https://minestom.net/docs/world/generation and https://javadoc.minestom.net/net.minestom.server/net/minestom/server/instance/generator/GenerationUnit.html
17. https://jd.papermc.io/paper/1.21.4/org/bukkit/generator/LimitedRegion.html, https://jd.papermc.io/paper/1.21.4/org/bukkit/generator/BlockPopulator.html, https://jd.papermc.io/paper/1.21.4/org/bukkit/generator/ChunkGenerator.html
18. https://hub.spigotmc.org/jira/si/jira.issueviews:issue-html/SPIGOT-4237/SPIGOT-4237.html (trees cut, 2018)
19. https://forums.minecraftforge.net/topic/65334-cascading-worldgen-lag and https://forums.minecraftforge.net/topic/60801-112-cascading-worldgen-lag/
20. https://github.com/cuberite/cuberite/blob/master/src/Generating/GridStructGen.h (header comment only)
21. https://github.com/PolyhedralDev/TerraWiki/wiki/TerraScript-Functions (getBlock cascades)
22. https://wiki.vintagestory.at/index.php/Modding:WorldGen_Concept and https://apidocs.vintagestory.at/api/Vintagestory.API.Server.EnumWorldGenPass.html
23. https://docs.veloren.net/veloren_world/canvas/struct.Canvas.html and https://veloren.net/devblog-30
24. https://forum.terasology.org/goto/post?id=10377 and https://forum.terasology.org/threads/can-we-reduce-faceted-worldgen-api-complexity.1711/post-14301
25. https://github.com/Zylann/godot_voxel/issues/545 (multipass; checked by the lead) and https://voxel-tools.readthedocs.io/en/latest/api/VoxelGeneratorMultipassCB/
26. https://forums.factorio.com/viewtopic.php?p=662621 (2.0.34 order dependence)
27. https://hytalemodding.dev/en/docs/official-documentation/worldgen/worldgen-tutorial/optimization-wip and https://hytale.com/news/2026/1/the-future-of-world-generation
28. https://www.boristhebrave.com/2021/11/08/infinite-modifying-in-blocks/ and https://marian42.de/article/wfc/
29. https://gitlab.com/distant-horizons-team/distant-horizons/-/issues/809
30. https://github.com/OpenCubicChunks/CubicChunks/wiki/How-it-internally-works%3F

Streaming and testing:

31. https://github.com/PaperMC/Paper/pull/8177 (chunk system rewrite, 2022)
32. https://ayakael.net/mirrors/papermc/commit/70b156939f58095bc3a5c3270ae6fbfe12e80291 (cancel pending loads, 2018; prose only)
33. https://docs.papermc.io/paper/reference/global-configuration/ (chunk rate caps)
34. https://github.com/RelativityMC/C2ME-fabric (README)
35. https://github.com/nayupls1/rayengine/pull/33 (hobby; not verified by the lead)
36. https://github.com/generatives/clear_skies/pull/27 (hobby; not verified by the lead)
37. https://github.com/desphyxia/st-voxel-action/pull/50 (hobby; window-size dependence)
38. https://risingwave.com/blog/deterministic-simulation-a-new-era-of-distributed-system-testing/, https://github.com/awslabs/shuttle, https://docs.rs/loom
