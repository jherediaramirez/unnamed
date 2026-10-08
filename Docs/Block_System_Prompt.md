# Prompt: Minecraft 26.3 block system for Unnamed (Unreal Engine 5.8)

Copy everything below the line into a new Claude Code chat started in `C:\dev\projects\Unnamed`.

---

<role>
You are the lead engineer adding Minecraft Java 26.3's simple blocks to the Unnamed Unreal Engine 5.8 project. You own the architecture, the integration across files, reviewing every change, and the final write-up. You may delegate well-bounded work to smaller-model subagents (see <subagents>). Decisions and final verification stay with you.
</role>

<goal>
Replace the single `dummy_block` world with a data-driven block registry that holds **every full-cube block and slab from vanilla 26.3** (529 blocks: 428 cubes + 101 slabs), plus `air` and `water`. Requirements:

- Exact IDs and textures.
- Correct render layers: opaque, cutout and translucent.
- Biome tint and animated textures.
- Rotation and placement rules: `axis`, `facing`, and slab top/bottom/double.
- A way to see and place every block in the Playground.

This milestone gives the **next** milestone, the chunk loader, a finished block vocabulary to store, mesh and save.
</goal>

<decisions_already_made>
These come from the user. Do not re-litigate them.

1. **IDs match Mojang.**
   - At runtime, each block's numeric id is its 26.3 `protocol_id`, and each state's id is its 26.3 block-state id (e.g. `stone` = 1, `water` = 37 with states 89–104, `oak_slab` = 15177–15182). Gaps from non-imported blocks are fine: 35,723 states fit in `uint16`.
   - **Save data never stores those numbers.** It stores the string id plus state properties, the way Minecraft's chunk palette does (`{Name:"oak_slab", Properties:{type:"bottom", waterlogged:"false"}}`).
   - The numbers are rebuilt from the manifest at startup. Mojang shifts them every version: 26.3 inserted `poplar_planks` at 23.
2. **Ids have no namespace**, in snake_case, matching the existing `dummy_block`. Keep `dummy_block` and every existing saved id working.
3. **Water is the only block that will get behavior**, and that behavior arrives with the chunk loader.
   - In this milestone, register all 16 water states and render static water at the correct surface heights.
   - Flow simulation, swimming physics and waterlogging are **out of scope**: they need scheduled ticks and block-ticking chunks.
   - Keep the hooks obvious, and keep the existing still-water volumes and vitals water facts working.
4. **Blocks first, chunk loader next.** Do not build paletted chunk storage, world generation or streaming here. Keep `IFoundationBlockStorage` as the boundary the chunk loader will implement.
</decisions_already_made>

<research_inputs>
Read in this order before designing anything.

1. `Docs\MinecraftBlockReference.md` is the main research doc (generated). It covers:
   - §1 the id policy, §2 how Mojang orders and tags blocks, §3 per-column import notes (axis, facing, random rotation, tint, animation, layers, slabs).
   - §4 water: the source/flow data model, flow rules, and player effects. Read it to design hooks, not to implement them.
   - §5 the full 529-row table. §6 chunk storage and loading, for context on what comes next.
2. `C:\dev\sandbox\mc-26.3-blocks\`, the raw research outputs:
   - `out\block_manifest.json` is **the import input**. It has one entry per block in registry order: `id`, `protocol_id`, `default_state_id`, `state_id_range`, `properties`, `shape`, `template`, `faces{down,up,north,south,east,west}`, `all_textures`, `render_layer`, `force_translucent`, `tinted`, `animated`, `random_rotation`, `rotates_by`, `tex_size`, `tags`, `where`, `y_range`. Water is under `fluids`.
   - `textures\block\*.png` + `.png.mcmeta`: 553 textures, byte-identical to the jar. `textures\colormap\grass.png`, `foliage.png`.
   - `out\blocks_registry.csv`: all 1,286 blocks with state-id ranges and properties. `out\block_tags.csv`: resolved tags. `out\edge_cases.txt`: cube-like blocks deliberately excluded.
   - `out\water_research.md`: water notes with bytecode evidence markers. `out\worldgen_heights.md`.
   - `reports\reports\blocks.json`: Mojang's data-generator report, with every state and its id.
   - `scripts\build_manifest.py` (+ `classify_blocks.py`, `build_blocks.py`, `build_worldgen.py`) regenerate everything. `classes\` and `scripts\javap\` hold the unobfuscated 26.3 classes already extracted.
3. Source jars (read-only):
   - Client: `%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar`, SHA-1 `e877b6a07acd633fb3bb475002175cec036e7b87`. This is the same client pinned by `Reference\minecraft-hud-26.3`.
   - Server: `C:\dev\sandbox\mc-26.3-blocks\downloads\server.jar`, SHA-1 `33680f5f2ac32864d6d7cf5e56a705fdb3e05f4c`.
   - 26.x jars are unobfuscated, so `javap -c -p` gives real Mojang names. The JDK 25 path is in `C:\Users\jherediaramirez\.claude\projects\C--dev\memory\lucid-mods-build-environment.md`. Python runs through `uv` only (`uv run --no-project --python 3.13 python -I …`); `python` on PATH is the Windows Store stub.
4. Project docs and code:
   - `CLAUDE.md`, `Docs\Architecture.md`, `Docs\NamingConventions.md`.
   - `Docs\Foundation\Architecture.md`, especially the "Playground" section.
   - `Docs\Records\2026-10-08-Interaction.md`, `Docs\AgentTeam.md`.
5. Current block code you will extend:
   - `Source\Unnamed\Foundation\FoundationBlocks.h/.cpp`: `FFoundationBlockDefinition`, `FFoundationBlockState{uint16 Block, uint16 Data}`, `IFoundationBlockStorage`, `FFoundationSectionStorage`, `FFoundationBlockWorld`.
   - `FoundationGrid.h` (axes: X east, Y south, Z up; 100 cm blocks; faces), `FoundationTags.*`, `FoundationItems.*`.
   - `FoundationInteractionSettings.*` (current definitions and loadout), `FoundationAuthority.*` (the only writer), `FoundationInteractionView.*` (instanced-cube presentation), `FoundationPlayground.*`.
   - `Scripts\Editor\CreatePlaygroundMap.py`.
6. Precedent to mirror: `Reference\minecraft-hud-26.3\` (README, manifest, pinned SHA-1, implementation prompt) together with `Scripts\Editor\HudImportData.py` / `ImportHud.py` and `Content\Hud\`. This is how this repo turns extracted 26.3 assets into Unreal assets reproducibly.
</research_inputs>

<scope>
In scope:

1. **Bring the research into the repo.** `C:\dev\sandbox` is disposable.
   - Create `Reference\minecraft-blocks-26.3\` modeled on the HUD reference: README with both SHA-1 pins, `block_manifest.json`, the CSVs, `textures\` (block + colormap), the research markdown files, a `tools\` copy of the generator scripts, and a file-hash list.
   - Update the paths in `Docs\MinecraftBlockReference.md` to point there.
2. **Hardness data.** The manifest lacks hardness, blast resistance and `requiresCorrectToolForDrops`, which the authority's mining needs.
   - Extract them for all 529 blocks from the 26.3 `Blocks` class (`BlockBehaviour$Properties.strength(...)`, `requiresCorrectToolForDrops()`), with minecraft.wiki as a cross-check.
   - Record the source of each value and merge it into the reference manifest.
3. **Registry.**
   - A generated registry asset (DataTable or PrimaryDataAsset, authored by an editor Python script from the manifest; definitions stay data) plus runtime lookup tables: string id → block, protocol id → block, state id → (block, property values), and the reverse.
   - Tags from `block_tags.csv` mapped to the project's GameplayTag style (check `FoundationTags`: e.g. `mineable/pickaxe` → `Block.Mineable.Pickaxe`, `needs_iron_tool` → a tool-tier tag).
   - If definitions move out of `UFoundationInteractionSettings`, update the CLAUDE.md rule and the architecture docs in the same change.
4. **State model.** Replace or extend `FFoundationBlockState` so a cell holds the 26.3 state id, and every state has properties.
   - In scope: `axis`, `facing`, `lit`, slab `type`, water `level`. Carry `waterlogged` as an inert property.
   - Add a saved-form codec: state ⇄ `{id, properties}`.
   - Per-state selection and collision shapes: a bottom slab is Z 0–0.5, a top slab 0.5–1, a double slab full.
5. **Placement rules (vanilla `getStateForPlacement`):**
   - `axis` from the clicked face.
   - `facing` opposite the player's horizontal look (furnace-likes face the player).
   - Slab half from the hit point and face. Placing a slab into a matching single slab makes `type=double`.
   - All placement goes through `FFoundationAuthority` commands.
6. **Textures and materials (Unreal).**
   - Import every block PNG as a source texture with no compression artifacts (uncompressed RGBA8 / `UserInterface2D`-class settings), sRGB on, **Nearest** filtering, and mips generated in a way that does not bleed cutout alpha.
   - Pack the faces into **one `UTexture2DArray`**: all faces are 16×16. Split animated strips (`.mcmeta` with an `animation` key) into frames on separate layers. Keep a frame table (first layer, count, `frametime`, frame order).
   - Three materials sample the array by layer index: opaque, masked (cutout) and translucent. Glass has `force_translucent`.
   - Tint: grass top and side overlay use the grass colormap, leaves use the foliage colormap, with plains defaults. Verify the fixed birch/spruce/mangrove colors against 26.3 `BlockColors`/`FoliageColor` bytecode before hardcoding them.
   - Animation: the layer offset is driven by a time parameter at 20 ticks/s.
   - Design the face → layer table so the future chunk mesher reuses it unchanged.
7. **Presentation.**
   - Extend `UFoundationInteractionView` so instanced cubes show per-face textures. Suggested approach: per-instance custom data carries the 6 face layers + tint, and the material picks the layer from the object-space normal. Remap faces on the CPU for `axis`/`facing` rather than rotating instances.
   - Slabs need a half-height mesh whose side UVs cover only half the texture. Do not scale a cube: that stretches the texture.
   - Water renders at surface height `amount/9` (a source is 8/9 tall), translucent and tinted.
   - Leave `rand-rot` as optional polish.
8. **Lab access.**
   - A **block gallery**: every block in registry order on a grid, plus each block's state variants, like Minecraft's debug world type.
   - A way to pick any block to place (e.g. a picker panel ordered by creative tab, or a chat command like `/give <id>`).
   - Keep the existing loadout and Playground scenarios working.

Out of scope (leave clean seams and say so in the record):
- Chunk loader, paletted storage, world generation, saving worlds.
- Water flow, swimming, waterlogging behavior.
- Loot tables (use "drops itself" as a clearly labeled provisional rule).
- Non-cube blocks (stairs, fences, plants…), and the excluded edge cases.
- Distribution licensing: the textures are Mojang's, as the HUD sprites already are.
</scope>

<ask_the_user_first>
**Item ids for 529 blocks.** These become permanent saved ids. The project currently uses a separate `dummy_block_item`, while Minecraft uses item id = block id. Read `NamingConventions.md` and the interaction record, then ask the user before creating them. Recommend one option.

Also ask before anything that deletes, renames or overwrites existing assets, maps, saved ids, evidence or packages.
</ask_the_user_first>

<subagents>
You may spawn subagents with the Agent tool when a smaller model can do a bounded piece of work more cheaply or in parallel. Pick the model per task:

- **Sonnet 5.5 (`model: "sonnet"`) for code you will keep:**
  - the editor Python import script (textures → Texture2DArray, registry asset);
  - the frame-splitting step;
  - the saved-form codec and its tests;
  - the placement-rule functions and their tests;
  - the material-authoring script;
  - the gallery layout.

  Building and coding favor Sonnet.
- **Haiku 5.5 (`model: "haiku"`) for mechanical or read-only work:**
  - extracting the hardness table from bytecode/wiki;
  - validating the manifest against the jar (hashes, counts, ids);
  - inventorying the current call sites of `FFoundationBlockState` / `FindBlock`;
  - reading test or build logs and summarizing failures;
  - review passes.

  The repo's agent team (`Docs\AgentTeam.md`: `haiku-reviewer`, `haiku-fixer`, installed by `Scripts\InstallAgentTeam.ps1`) fits review passes.

Rules that keep this efficient:
- **Brief each subagent fully.** Give it exact file paths, the decision already made, the interface it must match, the files it may edit (one owner per file, never two agents on one file), how to verify, and a capped report length.
- **Spend the budget wisely.** Keep each subagent's context under about 100k tokens by slicing the work.
  - The research round for this milestone spent about 1M tokens across four open-ended Haiku agents, so tight scopes matter.
  - Measure with `node Scripts\MeasureAgentUsage.mjs`.
- **Parallelize safely.** Launch independent agents in one message. **Only you run `Build.bat`, the editor commandlets and the test suites**: concurrent Unreal builds collide on the build mutex and the Live Coding lock. If the Unnamed editor is open and blocks a build, ask the user to close it.
- **Review before accepting.** Read every subagent diff before accepting it. Do not delegate architecture decisions, cross-cutting refactors, or the final verification.
- **Skip agents when it's cheaper.** Do the work yourself when it is small, or when explaining it would cost more than doing it.
</subagents>

<project_rules>
- Follow `CLAUDE.md` exactly. In particular:
  - gameplay changes go through `FFoundationAuthority`;
  - never rename saved ids;
  - evidence goes in fresh `Tests\Evidence\<date>\<name>\` folders;
  - format with the repo `.clang-format`;
  - editor Python lives in `Scripts\Editor\` with VerbNoun names;
  - asset paths live in `UUnnamedProjectSettings` / `ProjectPaths.py`.
- Name new classes after the area table in `NamingConventions.md`. Blocks belong with Foundation unless that table says otherwise.
- The working tree already has many unrelated uncommitted changes. Before editing, record `git status` and write a short plan.
  - Do not reset, clean, broad-stage, commit or push unless the user asks.
  - Do not replace maps or overwrite accepted packages.
- Earlier Minecraft2/M2-prefixed code and paths belong to a different project; they are not targets.
- Commands: build, test suites, Playground probe and packaging are listed in `CLAUDE.md` § Commands.
</project_rules>

<plan_of_work>
1. Read the inputs. Run `git status`. Write a short plan to the user, including your registry-asset choice and the item-id question.
2. Create the reference folder and verify both SHA-1s and every texture hash against the jar.
3. Extract the hardness data (a subagent can do this while you design the state model).
4. Build the registry, the state model, the codec and the placement rules. Add unit tests in the `Unnamed` automation suite.
5. Build the texture-array import and the materials. Extend the presentation for cubes, slabs and static water.
6. Add the gallery and the block picker to the Playground.
7. Verify (below). Write `Docs\Records\<date>-Blocks.md` and update the architecture docs.
</plan_of_work>

<acceptance>
1. Build passes, and `Automation RunTests Unnamed` passes, including the new tests:
   - registry counts (529 blocks + air + water);
   - every manifest protocol id and state-id range reproduced exactly;
   - every face resolves to a valid array layer;
   - imported pixels identical to the jar PNGs;
   - codec round-trip for every in-scope state;
   - placement-rule truth tables;
   - per-state slab shapes.
2. `Scripts\TestFoundationRuntime.ps1 -Editor -Suite interaction` still passes.
3. Fresh evidence folder with gallery screenshots that show:
   - grass tint and side overlay;
   - leaves cutout;
   - glass, stained glass and ice translucent;
   - logs in all 3 axes;
   - a furnace facing each way;
   - bottom, top and double slabs;
   - static water at source and flowing heights;
   - an animated block (sea lantern or magma) captured on two different frames.
4. The record lists what was verified, the remaining gaps, and the seams left for the chunk loader and water behavior.
</acceptance>

<final_report>
End with a short summary: what was built, test and evidence paths, decisions made (and which ones the user made), deferred items, and the subagent token usage from `MeasureAgentUsage.mjs`.
</final_report>
