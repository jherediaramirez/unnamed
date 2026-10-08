# Vanilla Minecraft Java 26.3: how water works (reimplementation reference)

Legend: **[B]** verified from 26.3 client bytecode (`26.3.jar`, `javap -c -p -constants`); **[W]** from minecraft.wiki (not version-pinned, fetched through a summarizer); **[U]** uncertain or not traced.

Method: `C:\Users\jherediaramirez\AppData\Roaming\ModrinthApp\meta\versions\26.3\26.3.jar` (read-only). Extracted classes are in `C:\dev\sandbox\mc-26.3-blocks\classes\`, javap dumps in `...\scripts\javap\`. The jar ships readable names (e.g. `net/minecraft/world/level/material/FlowingFluid.class`), so the Mojang names below are the real ones.

## Corrections to the brief (confirmed in 26.3)

- There is no `updateInWaterStateAndDoFluidPushing` in 26.3 [B]. Fluid detection now lives in `Entity.updateFluidInteraction()`, backed by `world.entity.EntityFluidInteraction` (`update(Entity, boolean)`, `isEyeInFluid(TagKey)`, `getFluidHeight(TagKey)`, `applyCurrentTo(TagKey, Entity, double)`). `Entity.wasEyeInWater`, `wasTouchingWater`, `isInWater()` and `isUnderWater()` still exist [B].
- Nether evaporation and lava speed are dimension environment attributes, not code: `minecraft:gameplay/water_evaporates` and `minecraft:gameplay/fast_lava` are set in `data/minecraft/dimension_type/the_nether.json` [B].
- The gamerule key is `water_source_conversion` (snake case), field `GameRules.WATER_SOURCE_CONVERSION` [B].
- `ProtoChunk` post-processing calls `FluidState.tick()` immediately, it does not only schedule ticks [B].

## 1. Data model

- Two fluids: `Fluids.WATER` is `WaterFluid$Source` (`isSource()` true, `getAmount()` = 8). `Fluids.FLOWING_WATER` is `WaterFluid$Flowing` with `LEVEL` (`BlockStateProperties.LEVEL_FLOWING`, `IntegerProperty "level" 1..8`) and `FALLING` (boolean) [B].
- Height is `amount / 9`: 0.889 at amount 8, 0.111 at amount 1. A fluid with the same fluid directly above reports height 1.0 (`FlowingFluid.getHeight`, `getOwnHeight`) [B]. This matches the wiki's level-to-height values [W].
- Block state: `LiquidBlock.LEVEL` is `"level" 0..15` (16 states for `minecraft:water`) [B]. `LiquidBlock.getFluidState` indexes a 9-entry cache by `min(level, 8)`. Index 0 is the source (`getSource(false)`). Indices 1..7 are flowing, amount `8 - level`, not falling. Index 8 is flowing amount 8, falling [B; loop body partly filtered, constants confirmed]. Levels 9..15 behave like 8.
- Inverse (`FlowingFluid.getLegacyLevel`): source gives 0, otherwise `8 - min(amount, 8) + (falling ? 8 : 0)` [B].
- Waterlogging: `BlockStateProperties.WATERLOGGED`, `SimpleWaterloggedBlock` interface. `SlabBlock.getFluidState` returns `getSource(false)` when `WATERLOGGED` is true [B]. So a waterlogged slab reports a source state, which covers the bottom-slab case. The `SimpleWaterloggedBlock.placeLiquid` body was not traced [U].
- Bubble column: `BubbleColumnBlock` with `DRAG_DOWN` (boolean). `getFluidState()` returns `Fluids.WATER` source [B]. Block-state count is probably 2 [U].
- Water as blocks occupies 16 block-state IDs, one per `LEVEL` value. The network sends global IDs, so each level is a separate state [B for the count; wire format [U]].

## 2. Flow and spreading (FlowingFluid, WaterFluid)

Constants for water, all verified [B]: tick delay 5 (`getTickDelay`), slope search distance 4 (`getSlopeFindDistance`), drop-off 1 (`getDropOff`), falling reset to 7 on side spread, falling-from-above amount 8.

`FlowingFluid.tick` (per scheduled tick) [B]:
1. If not a source, compute `newFluid = getNewLiquid(level, pos, state)`, and `delay = getSpreadDelay()` (equals `getTickDelay()`).
2. Empty result: set `AIR`. Changed result: set `newFluid.createLegacyBlock()` and `scheduleTick(pos, fluid, delay)`.
3. Always call `spread(level, pos, state, fluid)`, including for sources.

`getNewLiquid` (derive the cell's state from neighbours) [B]:
- Over the four horizontal neighbours with the same fluid that pass the wall test: track the maximum amount and count sources.
- Infinite source rule: if the source count is at least 2 and `canConvertToSource` (the gamerule) is true, and the block below is `isSolid()` or is a source of this type, the result is `getSource(false)`.
- Water directly above that can pass into this cell gives `getFlowing(8, true)` (falling).
- Otherwise `newAmount = max - 1`. If that is at most 0 the result is empty, else `getFlowing(newAmount, false)`.
- The wiki also says a source forms from one horizontal source plus a source directly above [W]. The 26.3 code has no such rule [B, contradicts W].

`spread`:
- Empty fluid returns. Otherwise try the block below: if it can take this fluid, `spreadTo(below, DOWN, newBelow)`. If `sourceNeighborCount >= 3`, also run `spreadToSides` and return [B].
- Otherwise, a source always runs `spreadToSides`. A flowing cell skips it when `isWaterHole(below)` is true, which means it drops straight down [B].

`spreadToSides` [B]: `newAmount = amount - 1`. Falling water uses 7 instead. Stop if it is at most 0. Then, for each direction in `getSpread()`, call `spreadTo`.

`getSpread` / `getSlopeDistance` [B]: each horizontal direction starts with weight 1000. A neighbour that is a hole (`SpreadContext.isHole`, which is `isWaterHole` for the cell below) gets weight 0. Otherwise `getSlopeDistance(neighbour, depth=1, fromDir)` searches horizontal branches, skipping the way it came, and returns the depth at which a hole is found. It recurses only while `depth < 4`. Only directions with the minimal weight receive water, so ties split evenly.

Reach: a source has amount 8, so flat-ground reach is 7 horizontal blocks, matching the wiki [W, derived from [B]]. Downward flow is unlimited until blocked.

Wall and occupancy [B]:
- `canPassThroughWall` returns false if the debug flag `SharedConstants.DEBUG_DISABLE_LIQUID_SPREADING` is set. It returns false if either side's collision shape is a full block. It returns true if both are empty. Otherwise it uses face occlusion (`Shapes.mergedFaceOccludes`) with a per-thread cache of 200 entries.
- `canHoldAnyFluid`: true for `LiquidBlockContainer`, or blocks in `BlockTags.WASHED_AWAY_BY_FLUIDS`. That tag (read from `data/minecraft/tags/block/washed_away_by_fluids.json`) includes `#minecraft:air`, flowers, torches, redstone, crops, kelp, seagrass, snow, cobwebs, end rods and `minecraft:water`/`lava`.
- `canHoldSpecificFluid`: a `LiquidBlockContainer` asks `canPlaceLiquid`, otherwise true.
- `WaterFluid.beforeDestroyingBlock` calls `Block.dropResources` [B], so washed-away blocks drop items. `WaterFluid.entityInside` applies `InsideBlockEffectType.EXTINGUISH` [B].

Water and lava [B references, exact rules U]: `LiquidBlock` references `OBSIDIAN`, `COBBLESTONE` and `BASALT`. `LavaFluid` references `STONE` and `FluidTags.WATER`. The wiki gives: lava source touching water becomes obsidian; flowing-flowing becomes cobblestone; lava falling onto water becomes stone [W].

Evaporation: `BucketItem` reads `EnvironmentAttributes.WATER_EVAPORATES` [B; the method was not traced]. No tick-time evaporation was found in `FlowingFluid` or `WaterFluid` [B].

Scheduling: `LiquidBlock.onPlace` and `neighborChanged` call `Level.scheduleTick(pos, fluid, getTickDelay)` [B]. Fluid ticks live in `ServerLevel.fluidTicks` (`LevelTicks<Fluid>`), run by `LevelTicks.tick(gameTime, maxTicks, consumer)` [B]. `ServerLevel.shouldTickBlocksAt(long)` calls `DistanceManager.inBlockTickingRange` [B]. So fluids only tick in block-ticking chunks. Random ticks are for lava only, because `FlowingFluid.isRandomlyTicking()` is false and `LavaFluid`'s is true [B].

## 3. Effect on the player

Detection [B]: `EntityFluidInteraction` keeps per-fluid trackers with `fluidType`, `height` and `eyesInside`. `isEyeInFluid` checks `eyesInside` on a matching tracker. `getFluidHeight` returns the matching tracker's height. The threshold math inside `update()` was not traced [U].

Fall damage and splash: touching water or lava calls `resetFallDistance()`. A splash plays on first water contact [B].

Current push: `applyCurrentTo(WATER, entity, 0.014)` each tick. Lava uses 0.007 when `FAST_LAVA` is set (Nether), else 0.00233 [B]. The flow vector comes from `FlowingFluid.getFlow` [B].

Movement in water, `LivingEntity.travelInWater(input, gravity, falling, oldY)` [B]:
- Horizontal drag `f = sprinting ? 0.9 : getWaterSlowDown()` (0.8).
- Acceleration `f1 = 0.02`. `f2 = WATER_MOVEMENT_EFFICIENCY` (default 0, max 1), halved when not on ground. If `f2 > 0`: `f += (0.546 - f) * f2` and `f1 += (getSpeed() - f1) * f2`.
- Dolphin's Grace sets `f = 0.96`.
- `moveRelative(f1, input)`, then `move(SELF, dm)`. If horizontally blocked and climbable, vy becomes 0.2. Then `dm *= (f, 0.8, f)`, then `getFluidFallingAdjustedMovement(gravity, falling, dm)`, then `jumpOutOfFluid(oldY)`.
- Gravity: `Attributes.GRAVITY` default 0.08 [B]. The fluid-falling adjustment uses constants 0.005, 0.003, 16.0 and -0.003 [B; branch structure partly inferred].
- Jump in water: `jumpInLiquid` adds 0.04 to vy. Sinking: `goDownInWater` subtracts 0.04 [B]. Callers not traced [U].
- Ledge exit: `jumpOutOfFluid` sets vy to 0.3 when horizontally blocked and the space about 0.6 above is free [B].
- Swim steering: `Player.travel` uses `getLookAngle().y`. A target of 0.085 applies when `y < -0.2`, else 0.06. Constants [B]; full semantics [U].

Swimming state, `Entity.updateSwimming` [B]:
- If already swimming: stay while `sprinting && isInWater() && !passenger`.
- Otherwise start if `sprinting && isUnderWater() && !passenger` and the fluid at the feet block is WATER.
- So starting needs sprint, eyes and feet in water. Continuing needs sprint and touching water.
- `Player.getDesiredPose` returns `Pose.SWIMMING` when swimming [B]. `updatePlayerPose` checks `canPlayerFitWithinBlocksAndEntitiesWhen(SWIMMING)` [B].
- Swim hitbox: not located in 26.3 bytecode. `LivingEntity.getDimensions` special-cases only SLEEPING [B]. The 0.6 value is [U].

Air [B]:
- `Entity.getMaxAirSupply()` = 300.
- Each tick with eyes in water and not inside a bubble column: `decreaseAirSupply` subtracts 1, unless `OXYGEN_BONUS` (Respiration, +1 per level) is positive and `random < 1/(1+bonus)`.
- Exempt: `canBreatheUnderwater` (`EntityTypeTags.CAN_BREATHE_UNDER_WATER`), Water Breathing, or an invulnerable player.
- When air is at most -20: air becomes 0 and `drown` damage 2.0 (one heart) applies. That is one heart every 20 ticks.
- Regain: `increaseAirSupply` adds 4 per tick, capped at max, when not submerged or in a bubble column.

Mining [B]: `Player.getDestroySpeed` multiplies by `SUBMERGED_MINING_SPEED` when eyes are in water (default 0.2, range 0 to 20). It divides by 5 when not on ground. Aqua Affinity adds +4.0 with `add_multiplied_total` (`data/minecraft/enchantment/aqua_affinity.json`), so 0.2 becomes 1.0.

Depth Strider: `water_movement_efficiency` +0.33333334 per level, `add_value` (`depth_strider.json`) [B]. Respiration: `oxygen_bonus` +1.0 per level (`respiration.json`) [B].

Bubble columns [B]:
- Inside: drag-down gives `vy = max(-0.3, vy - 0.03)`. Upward gives `vy = min(0.7, vy + 0.06)`. Fall distance resets.
- Above the column top: drag-down gives `max(-0.9, vy - 0.03)`, upward gives `min(1.8, vy + 0.1)`.

Wiki swim speeds (2.20 blocks/s on the surface, 1.81 downstream, 0.39 upstream, sprint about 3.92 m/s) [W, not checked here]. Underwater fog and visibility [U].

## 4. Storage and chunk loading

- `LevelChunkSection` (16x16x16) tracks `nonEmptyBlockCount`, `fluidCount`, `tickingBlockCount` and `tickingFluidCount` [B]. There is no separate fluid layer; fluid comes from `BlockState.getFluidState()`.
- Palette thresholds, `chunk.Strategy.createForBlockStates` [B]: bits 0 is a single-value palette. Bits 1 to 4 use a linear palette (4 bits). Bits 5 to 8 use a hashmap palette. Bits 9 and up use the global `IdMap` palette. Bits are `ceil(log2(palette size))`. Biomes use a separate table (0 to 3) [B; biome details not traced].
- The global-palette in-memory width comes from a constructor argument (4 for block states). I did not resolve how it relates to the wire width [U].
- `SerializableChunkData` keys [B, from string constants]: `sections`, `block_states`, `biomes`, `block_ticks`, `fluid_ticks`, `PostProcessing`, `Status`, `InhabitedTime`, `isLightOn`. The `palette` and `data` subkeys were not checked [U]. The load path for saved ticks was not traced [U].
- Worldgen: `ProtoChunk.markPosForPostProcessing(BlockPos)` adds a packed offset to a per-section `ShortList postProcessing` [B]. `LevelChunk.postProcessGeneration(ServerLevel)` then, for each marked position with a non-empty fluid, calls `FluidState.tick(level, pos, state)` immediately. It then runs the block tick and neighbour update, sets the block, and clears the list [B]. Callers in worldgen (feature and aquifer placement) were not traced [U]. `Aquifer$NoiseBasedAquifer` references `Blocks.WATER`, `LAVA` and `AIR` [B].
- Chunk-level gating: fluid ticks run only where `inBlockTickingRange` holds [B]. Entity ticking is a separate range.

## 5. Implementation checklist

- Per voxel: block id plus a 4-bit level (0 to 15) for water, or a waterlogged flag. Derive fluid amount and falling as in section 1. Store nothing extra for fluids.
- Use a per-chunk scheduled-tick queue keyed by (position, fluid, due time). Water delay is 5 ticks. Process only in block-ticking chunks.
- Implement `getNewLiquid` exactly, including the infinite-source rule gated by `water_source_conversion`.
- Implement `spread` with the hole check, the side-spread rule (falling resets to 7), and the 4-step slope search with 1000 as the no-drop weight.
- Wall rule: full-block collision blocks flow. Otherwise use face occlusion. Washed-away tag and `LiquidBlockContainer` decide replacement.
- Player: fluid detection (feet and eyes), current push 0.014, `travelInWater` constants, swim state machine, air 300, -1 per tick, +4 regain, drown at -20 for 2 hearts per 20 ticks, mining 0.2 and divide by 5, fall-distance reset.
- Worldgen: a post-process list per section, then an immediate fluid tick at each marked position.

## Open items

- Swim hitbox dimensions, the `EntityFluidInteraction` thresholds, the `spreadTo` body, the `SimpleWaterloggedBlock.placeLiquid` body, the palette wire width, and worldgen callers of `markPosForPostProcessing`.
- Sources: [W] https://minecraft.wiki/w/Water (fetched; summarizer truncated at 100k of 246k chars). [W] https://minecraft.wiki/w/Swimming (fetched). [W] https://minecraft.wiki/w/Drowning (fetched, but the air section was not reached, so air numbers are from bytecode only). [W] https://minecraft.wiki/w/Java_Edition_26.3 (partial; no water change seen in the first 100k characters). Version context from search snippets only: https://docs.fabricmc.net/26.1.2/develop/porting/mappings/ and https://fabricmc.net/2026/03/14/261.html.
