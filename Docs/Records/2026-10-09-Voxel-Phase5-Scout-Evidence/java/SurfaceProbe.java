import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.mojang.serialization.JsonOps;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.RegistryAccess;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.RegistryOps;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.ChunkPos;
import net.minecraft.world.level.LevelHeightAccessor;
import net.minecraft.world.level.biome.*;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.PalettedContainerFactory;
import net.minecraft.world.level.chunk.ProtoChunk;
import net.minecraft.world.level.chunk.UpgradeData;
import net.minecraft.world.level.levelgen.*;
import net.minecraft.world.level.levelgen.blending.Blender;
import net.minecraft.world.level.levelgen.densityfunction.DensityVolume;
import net.minecraft.world.level.levelgen.material.condition.MaterialCondition;
import net.minecraft.world.level.levelgen.material.condition.NoiseThresholdCondition;
import net.minecraft.world.level.levelgen.material.rule.MaterialRule;
import net.minecraft.world.level.levelgen.synth.NormalNoise;

// Black-box probe of the 26.3 jar's material (surface) rules: hand-made chunks of stone, water and air go through the
// jar's own MaterialSystem.buildSurface with marker rules decoded from JSON; only the resulting blocks are recorded.
// usage: java -cp <classes>;<classpath> SurfaceProbe <outDir> <experiment...>
public class SurfaceProbe {
    static RegistryAccess access;
    static HolderLookup.Provider lookup;
    static RegistryOps<JsonElement> ops;
    static Holder<NoiseGeneratorSettings> settings;
    static final LevelHeightAccessor H = LevelHeightAccessor.create(-64, 384);
    static BlockState STONE, WATER, AIR, GOLD;
    static Path out;

    interface Fill { BlockState at(int x, int y, int z); }
    interface BiomeAt { Holder<Biome> at(int qx, int qy, int qz); }

    static final Map<Long, RandomState> STATES = new HashMap<>();
    static RandomState state(long seed) {
        return STATES.computeIfAbsent(seed, s -> RandomState.create(lookup.lookupOrThrow(Registries.NOISE), s, settings.value()));
    }
    static Holder<Biome> biome(String id) {
        return lookup.lookupOrThrow(Registries.BIOME).getOrThrow(ResourceKey.create(Registries.BIOME, Identifier.withDefaultNamespace(id)));
    }
    static MaterialRule rule(String json) { return MaterialRule.DIRECT_CODEC.parse(ops, JsonParser.parseString(json)).getOrThrow(); }
    static MaterialRule markIf(String cond) {
        return rule("{\"type\":\"minecraft:condition\",\"if_true\":" + cond + ",\"then_run\":{\"type\":\"minecraft:block\",\"result_state\":\"minecraft:gold_block\"}}");
    }
    static MaterialRule overworld() {
        return lookup.lookupOrThrow(Registries.MATERIAL_RULE).getOrThrow(ResourceKey.create(Registries.MATERIAL_RULE, Identifier.withDefaultNamespace("overworld"))).value();
    }
    static float noise(long seed, String key, double x, double y, double z) {
        return state(seed).getOrCreateNoise(ResourceKey.create(Registries.NOISE, Identifier.withDefaultNamespace(key))).get(x, y, z);
    }
    static float noise2(long seed, String key, double x, double z) {
        return state(seed).getOrCreateNoise(ResourceKey.create(Registries.NOISE, Identifier.withDefaultNamespace(key))).get(x, z);
    }

    static ProtoChunk run(long seed, int cx, int cz, BiomeAt biomes, Fill fill, MaterialRule rule) {
        RandomState rs = state(seed);
        ProtoChunk chunk = new ProtoChunk(new ChunkPos(cx, cz), UpgradeData.EMPTY, H, PalettedContainerFactory.create(access), null);
        BlockPos.MutableBlockPos p = new BlockPos.MutableBlockPos();
        for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++) for (int y = -64; y < 320; y++) {
            int x = cx * 16 + lx, z = cz * 16 + lz;
            BlockState s = fill.at(x, y, z);
            if (!s.isAir()) chunk.setBlockState(p.set(x, y, z), s, 0);
        }
        Heightmap.primeHeightmaps(chunk, EnumSet.of(Heightmap.Types.WORLD_SURFACE_WG, Heightmap.Types.OCEAN_FLOOR_WG));
        BiomeResolver resolver = biomes::at;
        chunk.fillBiomesFromNoise(resolver);
        Set<Holder<Biome>> possible = new HashSet<>();
        for (int qx = cx * 4 - 2; qx < cx * 4 + 6; qx++) for (int qz = cz * 4 - 2; qz < cz * 4 + 6; qz++) for (int qy = -16; qy < 80; qy++) possible.add(biomes.at(qx, qy, qz));
        BiomeManager bm = new BiomeManager(resolver, BiomeManager.obfuscateSeed(seed));
        NoiseBasedChunkGenerator gen = new NoiseBasedChunkGenerator(new FixedBiomeSource(possible.iterator().next()), settings);
        WorldGenerationContext ctx = new WorldGenerationContext(gen, H);
        DensityVolume vol = new DensityVolume(16, 384, 16, cx * 16, -64, cz * 16);
        try (NoiseChunk nc = new NoiseChunk(rs, Beardifier.EMPTY, settings.value(), (x, y, z) -> new Aquifer.FluidStatus(63, WATER), Blender.empty(), vol)) {
            rs.surfaceSystem().buildSurface(rs, bm, ctx, chunk, nc, rule, possible);
        }
        return chunk;
    }
    static ProtoChunk run(long seed, int cx, int cz, String biome, Fill fill, MaterialRule rule) {
        Holder<Biome> b = biome(biome);
        return run(seed, cx, cz, (qx, qy, qz) -> b, fill, rule);
    }
    static Fill stoneTo(int top) { return (x, y, z) -> y <= top ? STONE : AIR; }
    static Fill layers(int[][] spans) { // {fromY, toY, kind} kind 1 stone, 2 water; rest air
        return (x, y, z) -> { for (int[] s : spans) if (y >= s[0] && y <= s[1]) return s[2] == 1 ? STONE : WATER; return AIR; };
    }
    static String id(BlockState s) { return BuiltInRegistries.BLOCK.getKey(s.getBlock()).getPath(); }
    static JsonArray golds(ProtoChunk c, int x, int z) {
        JsonArray a = new JsonArray();
        for (int y = 319; y >= -64; y--) if (c.getBlockState(new BlockPos(x, y, z)).is(Blocks.GOLD_BLOCK)) a.add(y);
        return a;
    }
    static String ranges(JsonArray ys) { // descending ints -> "70..68,39"
        StringBuilder b = new StringBuilder();
        int i = 0;
        while (i < ys.size()) {
            int hi = ys.get(i).getAsInt(), lo = hi;
            while (i + 1 < ys.size() && ys.get(i + 1).getAsInt() == lo - 1) { lo--; i++; }
            if (b.length() > 0) b.append(',');
            b.append(hi == lo ? "" + hi : hi + ".." + lo);
            i++;
        }
        return b.toString();
    }
    static String column(ProtoChunk c, int x, int z, int top, int bottom) { // run-length ids from top down
        StringBuilder b = new StringBuilder();
        String last = null; int n = 0;
        for (int y = top; y >= bottom; y--) {
            String s = id(c.getBlockState(new BlockPos(x, y, z)));
            if (s.equals(last)) n++;
            else { if (last != null) b.append(last).append(n > 1 ? "*" + n : "").append(' '); last = s; n = 1; }
        }
        b.append(last).append(n > 1 ? "*" + n : "");
        return b.toString();
    }
    static int countDown(ProtoChunk c, int x, int z, int from) {
        int n = 0;
        for (int y = from; y >= -64 && c.getBlockState(new BlockPos(x, y, z)).is(Blocks.GOLD_BLOCK); y--) n++;
        return n;
    }
    static void write(String name, JsonElement e) throws Exception {
        Files.writeString(out.resolve(name), e.toString(), StandardCharsets.UTF_8);
        System.out.println("WROTE " + name);
    }

    // Surface depth per column, measured with under_floor on stone up to y=80 (count of marked blocks minus one).
    static final Map<String, Integer> DEPTH = new HashMap<>();
    static int surfaceDepth(long seed, int x, int z) {
        String k = seed + ":" + x + ":" + z;
        if (!DEPTH.containsKey(k)) {
            int cx = Math.floorDiv(x, 16), cz = Math.floorDiv(z, 16);
            ProtoChunk c = run(seed, cx, cz, "plains", stoneTo(80), markIf("\"minecraft:under_floor\""));
            for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++)
                DEPTH.put(seed + ":" + (cx * 16 + lx) + ":" + (cz * 16 + lz), countDown(c, cx * 16 + lx, cz * 16 + lz, 80) - 1);
        }
        return DEPTH.get(k);
    }

    public static void main(String[] args) throws Exception {
        out = Path.of(args[0]);
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        var repo = net.minecraft.server.packs.repository.ServerPacksSource.createVanillaTrustedRepository();
        repo.reload();
        var packs = new net.minecraft.server.WorldLoader.PackConfig(repo, net.minecraft.world.level.WorldDataConfiguration.DEFAULT, false, true);
        var init = new net.minecraft.server.WorldLoader.InitConfig(packs, net.minecraft.commands.Commands.CommandSelection.DEDICATED,
            net.minecraft.server.permissions.PermissionSet.ALL_PERMISSIONS);
        net.minecraft.core.LayeredRegistryAccess<net.minecraft.server.RegistryLayer> layers = net.minecraft.server.WorldLoader.load(init,
            ctx -> new net.minecraft.server.WorldLoader.DataLoadOutput<Boolean>(Boolean.TRUE, ctx.datapackDimensions()),
            (net.minecraft.server.WorldLoader.ResultFactory<Boolean, net.minecraft.core.LayeredRegistryAccess<net.minecraft.server.RegistryLayer>>)
                (resources, reloadable, registries, cookie) -> { reloadable.updateComponentsAndStaticRegistryTags(); return registries; },
            net.minecraft.util.Util.backgroundExecutor(), Runnable::run).get();
        access = layers.compositeAccess();
        lookup = layers.compositeAccess();
        ops = lookup.createSerializationContext(JsonOps.INSTANCE);
        settings = lookup.lookupOrThrow(Registries.NOISE_SETTINGS).getOrThrow(NoiseGeneratorSettings.OVERWORLD);
        STONE = Blocks.STONE.defaultBlockState(); WATER = Blocks.WATER.defaultBlockState(); AIR = Blocks.AIR.defaultBlockState(); GOLD = Blocks.GOLD_BLOCK.defaultBlockState();
        for (int i = 1; i < args.length; i++) {
            long t = System.nanoTime();
            switch (args[i]) {
                case "x0" -> x0();
                case "depth" -> depth();
                case "stone" -> stoneDepth();
                case "y" -> yAbove();
                case "water" -> water();
                case "noise" -> noiseThreshold();
                case "gradient" -> gradient();
                case "steep" -> steep();
                case "prelim" -> prelim();
                case "biome" -> biomeCond();
                case "bands" -> bands();
                case "golden" -> golden();
                case "extra" -> extra();
                case "ext" -> extensions();
                default -> throw new IllegalArgumentException(args[i]);
            }
            System.out.printf("%s %.1fs%n", args[i], (System.nanoTime() - t) / 1e9);
        }
    }

    static void x0() throws Exception {
        JsonObject o = new JsonObject();
        o.addProperty("volume6", new DensityVolume(1, 2, 3, 4, 5, 6).toString());
        o.addProperty("volume9", new DensityVolume(1, 2, 3, 4, 5, 6, 7, 8, 9).toString());
        ResourceKey<NormalNoise> surf = ResourceKey.create(Registries.NOISE, Identifier.withDefaultNamespace("surface"));
        o.add("noise3d_encoded", MaterialCondition.DIRECT_CODEC.encodeStart(ops, new NoiseThresholdCondition(surf, -0.5, 0.5, true)).getOrThrow());
        o.add("noise2d_encoded", MaterialCondition.DIRECT_CODEC.encodeStart(ops, new NoiseThresholdCondition(surf, -0.5, 0.5, false)).getOrThrow());
        o.add("temperature_encoded", MaterialCondition.DIRECT_CODEC.encodeStart(ops, net.minecraft.world.level.levelgen.material.condition.TemperatureCondition.INSTANCE).getOrThrow());
        o.addProperty("overworld_rule_decoded", overworld().getClass().getSimpleName());
        JsonObject consts = new JsonObject();
        for (var f : net.minecraft.world.level.levelgen.material.rule.OreVeinRule.class.getFields())
            if (java.lang.reflect.Modifier.isStatic(f.getModifiers()) && f.getType().isPrimitive()) consts.addProperty(f.getName(), String.valueOf(f.get(null)));
        o.add("ore_vein_constants", consts);
        write("x0_basics.json", o);
        System.out.println(o.toString().substring(0, Math.min(900, o.toString().length())));
    }

    // Surface depth, hole, and the secondary depth ranges against the surface and surface_secondary noises.
    static void depth() throws Exception {
        JsonArray rows = new JsonArray();
        long seed = 0;
        int[][] chunks = {{0, 0}, {1, 0}, {0, 1}, {5, -3}, {-40, 17}, {300, -200}};
        String[] conds = {"\"minecraft:under_floor\"", "{\"type\":\"minecraft:hole\"}", "\"minecraft:deep_under_floor\"", "\"minecraft:very_deep_under_floor\""};
        for (int[] ch : chunks) {
            ProtoChunk[] cs = new ProtoChunk[conds.length];
            for (int k = 0; k < conds.length; k++) cs[k] = run(seed, ch[0], ch[1], "plains", stoneTo(80), markIf(conds[k]));
            for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++) {
                int x = ch[0] * 16 + lx, z = ch[1] * 16 + lz;
                JsonArray r = new JsonArray();
                r.add(x); r.add(z);
                r.add(countDown(cs[0], x, z, 80));
                r.add(cs[1].getBlockState(new BlockPos(x, 80, z)).is(Blocks.GOLD_BLOCK) ? 1 : 0);
                r.add(countDown(cs[2], x, z, 80));
                r.add(countDown(cs[3], x, z, 80));
                r.add(noise(seed, "surface", x, 0, z)); r.add(noise2(seed, "surface", x, z));
                r.add(noise(seed, "surface_secondary", x, 0, z)); r.add(noise2(seed, "surface_secondary", x, z));
                rows.add(r);
            }
        }
        JsonObject o = new JsonObject();
        o.addProperty("columns", "x,z,n_under_floor,hole_at_top,n_deep_under_floor(range6),n_very_deep_under_floor(range30),surface(x,0,z),surface2d(x,z),surface_secondary(x,0,z),surface_secondary2d(x,z)");
        o.addProperty("profile", "plains, seed 0, stone y<=80, air above; n_* = marked blocks counted down from y=80");
        o.add("rows", rows);
        write("depth.json", o);
    }

    // Stone depth above/below on hand-made profiles: runs split by air, water above, one-block runs, world bottom.
    static void stoneDepth() throws Exception {
        long seed = 0;
        Map<String, int[][]> profiles = new LinkedHashMap<>();
        profiles.put("cave", new int[][]{{-64, 39, 1}, {50, 70, 1}});                // air 40..49 and above 70
        profiles.put("underwater", new int[][]{{-64, 60, 1}, {61, 70, 2}});         // water 61..70
        profiles.put("thin", new int[][]{{-64, 66, 1}, {68, 68, 1}, {70, 70, 1}});  // one-block runs at 70 and 68
        profiles.put("water_pocket", new int[][]{{-64, 40, 1}, {41, 45, 2}, {46, 60, 1}}); // water inside stone
        Map<String, String> conds = new LinkedHashMap<>();
        conds.put("floor_off0_range6_noSD", "{\"type\":\"minecraft:stone_depth\",\"offset\":0,\"add_surface_depth\":false,\"secondary_depth_range\":6,\"surface_type\":\"floor\"}");
        conds.put("floor_off0_range6_SD", "{\"type\":\"minecraft:stone_depth\",\"offset\":0,\"add_surface_depth\":true,\"secondary_depth_range\":6,\"surface_type\":\"floor\"}");
        for (String surf : new String[]{"floor", "ceiling"}) for (int off : new int[]{0, 2}) for (boolean add : new boolean[]{false, true})
            conds.put(surf + "_off" + off + (add ? "_addSD" : ""), "{\"type\":\"minecraft:stone_depth\",\"offset\":" + off + ",\"add_surface_depth\":" + add + ",\"secondary_depth_range\":0,\"surface_type\":\"" + surf + "\"}");
        JsonObject o = new JsonObject();
        int[][] cols = {{0, 0}, {7, 9}, {15, 15}};
        JsonObject sd = new JsonObject();
        for (int[] c : cols) sd.addProperty(c[0] + "," + c[1], surfaceDepth(seed, c[0], c[1]));
        o.add("surface_depth_by_column", sd);
        for (var pe : profiles.entrySet()) {
            JsonObject po = new JsonObject();
            for (var ce : conds.entrySet()) {
                ProtoChunk ch = run(seed, 0, 0, "plains", layers(pe.getValue()), markIf(ce.getValue()));
                JsonObject co = new JsonObject();
                for (int[] c : cols) co.addProperty(c[0] + "," + c[1], ranges(golds(ch, c[0], c[1])));
                po.add(ce.getKey(), co);
            }
            o.add(pe.getKey(), po);
        }
        write("stone_depth.json", o);
    }

    static void yAbove() throws Exception {
        long seed = 0;
        JsonObject o = new JsonObject();
        String[][] cases = {
            {"abs70_m0", "{\"absolute\":70}", "0", "false", "100"},
            {"abs70_m1", "{\"absolute\":70}", "1", "false", "100"},
            {"abs70_m2", "{\"absolute\":70}", "2", "false", "100"},
            {"abs70_mneg1", "{\"absolute\":70}", "-1", "false", "100"},
            {"above_bottom5_m0", "{\"above_bottom\":5}", "0", "false", "100"},
            {"below_top10_m0", "{\"below_top\":10}", "0", "false", "319"},
            {"abs61_m0_addStone_top60", "{\"absolute\":61}", "0", "true", "60"},
            {"abs62_m0_addStone_top60", "{\"absolute\":62}", "0", "true", "60"},
            {"abs63_m1_addStone_top60", "{\"absolute\":63}", "1", "true", "60"},
            {"abs63_mneg1_addStone_top60", "{\"absolute\":63}", "-1", "true", "60"}};
        int[][] cols = {{0, 0}, {3, 4}, {7, 9}, {12, 1}, {15, 15}};
        JsonObject sd = new JsonObject();
        for (int[] c : cols) sd.addProperty(c[0] + "," + c[1], surfaceDepth(seed, c[0], c[1]));
        o.add("surface_depth_by_column", sd);
        for (String[] k : cases) {
            ProtoChunk ch = run(seed, 0, 0, "plains", stoneTo(Integer.parseInt(k[4])),
                markIf("{\"type\":\"minecraft:y_above\",\"anchor\":" + k[1] + ",\"surface_depth_multiplier\":" + k[2] + ",\"add_stone_depth\":" + k[3] + "}"));
            JsonObject co = new JsonObject();
            co.addProperty("stone_top", Integer.parseInt(k[4]));
            for (int[] c : cols) co.addProperty(c[0] + "," + c[1], ranges(golds(ch, c[0], c[1])));
            o.add(k[0], co);
        }
        write("y_above.json", o);
    }

    // Water: stone to 60, then water from 61 up to 60+lx (lx=0: no water), air above. Plus an air gap case.
    static void water() throws Exception {
        long seed = 0;
        JsonObject o = new JsonObject();
        String[][] conds = {
            {"not_underwater(off-1,m0,noStone)", "\"minecraft:not_underwater\""},
            {"off0_m0_noStone", "{\"type\":\"minecraft:water\",\"offset\":0,\"surface_depth_multiplier\":0,\"add_stone_depth\":false}"},
            {"off-1_m0_addStone", "{\"type\":\"minecraft:water\",\"offset\":-1,\"surface_depth_multiplier\":0,\"add_stone_depth\":true}"},
            {"not_under_deep_water(off-6,m-1,addStone)", "\"minecraft:not_under_deep_water\""},
            {"off0_m1_noStone", "{\"type\":\"minecraft:water\",\"offset\":0,\"surface_depth_multiplier\":1,\"add_stone_depth\":false}"}};
        Fill f = (x, y, z) -> { int lx = Math.floorMod(x, 16); return y <= 60 ? STONE : (y <= 60 + lx ? WATER : AIR); };
        JsonObject sd = new JsonObject();
        for (int lx = 0; lx < 16; lx++) sd.addProperty(lx + ",5", surfaceDepth(seed, lx, 5));
        o.add("surface_depth_by_column", sd);
        o.addProperty("profile", "column lx: stone y<=60, water 61..60+lx, air above; z=5");
        for (String[] c : conds) {
            ProtoChunk ch = run(seed, 0, 0, "plains", f, markIf(c[1]));
            JsonObject co = new JsonObject();
            for (int lx = 0; lx < 16; lx++) co.addProperty("lx" + lx, ranges(golds(ch, lx, 5)));
            o.add(c[0], co);
        }
        // Gaps: water above an air gap (cave under the sea floor), and water directly on stone with air under the stone.
        int[][][] gaps = {{{-64, 40, 1}, {51, 60, 1}, {61, 70, 2}}, {{-64, 40, 1}, {46, 60, 1}, {61, 61, 2}, {62, 70, 2}}};
        String[] gapNames = {"cave41-50_under_stone51-60_under_water61-70", "stone46-60_water61-70_cave41-45"};
        for (int g = 0; g < gaps.length; g++) {
            JsonObject go = new JsonObject();
            for (String[] c : conds) go.addProperty(c[0], ranges(golds(run(seed, 0, 0, "plains", layers(gaps[g]), markIf(c[1])), 0, 5)));
            o.add(gapNames[g], go);
        }
        write("water.json", o);
    }

    static void noiseThreshold() throws Exception {
        long seed = 0;
        JsonObject o = new JsonObject();
        JsonArray rows = new JsonArray();
        ProtoChunk[] cs = new ProtoChunk[2];
        for (int k = 0; k < 2; k++) cs[k] = run(seed, k, 0, "plains", stoneTo(70),
            markIf("{\"type\":\"minecraft:noise_threshold\",\"noise\":\"minecraft:surface\",\"min_threshold\":-0.2,\"max_threshold\":0.3}"));
        for (int k = 0; k < 2; k++) for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++) {
            int x = k * 16 + lx, z = lz;
            JsonArray r = new JsonArray();
            r.add(x); r.add(z); r.add(cs[k].getBlockState(new BlockPos(x, 70, z)).is(Blocks.GOLD_BLOCK) ? 1 : 0);
            r.add(cs[k].getBlockState(new BlockPos(x, 0, z)).is(Blocks.GOLD_BLOCK) ? 1 : 0);
            r.add(noise(seed, "surface", x, 0, z)); r.add(noise2(seed, "surface", x, z));
            rows.add(r);
        }
        o.addProperty("columns", "x,z,marked_at_y70,marked_at_y0,surface(x,0,z),surface2d(x,z); thresholds [-0.2,0.3]");
        o.add("rows", rows);
        // Exact bounds: thresholds set to one column's own value.
        int x = 3, z = 5;
        double v = noise(seed, "surface", x, 0, z);
        JsonObject b = new JsonObject();
        b.addProperty("value", v);
        double[][] bounds = {{v, v + 1}, {v - 1, v}, {Math.nextUp(v), v + 1}, {v - 1, Math.nextDown(v)}, {v, v}};
        String[] names = {"min=v", "max=v", "min=nextUp(v)", "max=nextDown(v)", "min=max=v"};
        for (int i = 0; i < bounds.length; i++) {
            ProtoChunk ch = run(seed, 0, 0, "plains", stoneTo(70), markIf("{\"type\":\"minecraft:noise_threshold\",\"noise\":\"minecraft:surface\",\"min_threshold\":"
                + bounds[i][0] + ",\"max_threshold\":" + bounds[i][1] + "}"));
            b.addProperty(names[i], ch.getBlockState(new BlockPos(x, 70, z)).is(Blocks.GOLD_BLOCK));
        }
        o.add("exact_bounds_col_3_5", b);
        // 3D variant: read the encoded field name from x0; here try "is_3d" style through the encoder.
        ResourceKey<NormalNoise> surf = ResourceKey.create(Registries.NOISE, Identifier.withDefaultNamespace("surface"));
        JsonElement enc = MaterialCondition.DIRECT_CODEC.encodeStart(ops, new NoiseThresholdCondition(surf, 0.0, 1e300, true)).getOrThrow();
        ProtoChunk ch = run(seed, 0, 0, "plains", stoneTo(70), markIf(enc.toString()));
        JsonArray r3 = new JsonArray();
        for (int y = 70; y >= -64; y -= 1) {
            JsonArray r = new JsonArray();
            r.add(y); r.add(ch.getBlockState(new BlockPos(3, y, 5)).is(Blocks.GOLD_BLOCK) ? 1 : 0); r.add(noise(seed, "surface", 3, y, 5));
            r3.add(r);
        }
        o.add("is3d_col_3_5_rows_y_marked_value", r3);
        write("noise_threshold.json", o);
    }

    static void gradient() throws Exception {
        JsonObject o = new JsonObject();
        String[][] g = {{"bedrock_floor", "{\"above_bottom\":0}", "{\"above_bottom\":5}", "-64", "-55"},
                        {"deepslate", "{\"absolute\":0}", "{\"absolute\":8}", "-4", "12"}};
        for (String[] k : g) for (long seed : new long[]{0, 1}) {
            String cond = "{\"type\":\"minecraft:vertical_gradient\",\"random_name\":\"minecraft:" + k[0] + "\",\"true_at_and_below\":" + k[1] + ",\"false_at_and_above\":" + k[2] + "}";
            MaterialRule r = markIf(cond);
            int lo = Integer.parseInt(k[3]), hi = Integer.parseInt(k[4]);
            int[] count = new int[hi - lo + 1];
            int cols = 0;
            for (int cx = 0; cx < 4; cx++) for (int cz = 0; cz < 4; cz++) {
                ProtoChunk ch = run(seed, cx, cz, "plains", stoneTo(20), r);
                for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++) {
                    cols++;
                    for (int y = lo; y <= hi; y++) if (ch.getBlockState(new BlockPos(cx * 16 + lx, y, cz * 16 + lz)).is(Blocks.GOLD_BLOCK)) count[y - lo]++;
                }
            }
            JsonObject f = new JsonObject();
            for (int y = lo; y <= hi; y++) f.addProperty(String.valueOf(y), count[y - lo] / (double) cols);
            o.add(k[0] + "_seed" + seed + "_fraction_by_y_over_" + cols + "_columns", f);
        }
        // Determinism: same chunk twice; different seed; different random_name; same position via a different chunk run order.
        String base = "{\"type\":\"minecraft:vertical_gradient\",\"random_name\":\"minecraft:deepslate\",\"true_at_and_below\":{\"absolute\":0},\"false_at_and_above\":{\"absolute\":8}}";
        String other = base.replace("minecraft:deepslate", "minecraft:something_else");
        ProtoChunk a = run(0, 2, 2, "plains", stoneTo(20), markIf(base));
        run(0, 9, 9, "plains", stoneTo(20), markIf(base));
        ProtoChunk a2 = run(0, 2, 2, "plains", stoneTo(20), markIf(base));
        ProtoChunk s1 = run(1, 2, 2, "plains", stoneTo(20), markIf(base));
        ProtoChunk n1 = run(0, 2, 2, "plains", stoneTo(20), markIf(other));
        ProtoChunk d1 = run(0, 2, 2, "desert", stoneTo(20), markIf(base));
        int same = 0, diffSeed = 0, diffName = 0, diffBiome = 0, total = 0;
        for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++) for (int y = 1; y <= 7; y++) {
            BlockPos p = new BlockPos(32 + lx, y, 32 + lz);
            boolean v = a.getBlockState(p).is(Blocks.GOLD_BLOCK);
            total++;
            if (v == a2.getBlockState(p).is(Blocks.GOLD_BLOCK)) same++;
            if (v != s1.getBlockState(p).is(Blocks.GOLD_BLOCK)) diffSeed++;
            if (v != n1.getBlockState(p).is(Blocks.GOLD_BLOCK)) diffName++;
            if (v != d1.getBlockState(p).is(Blocks.GOLD_BLOCK)) diffBiome++;
        }
        JsonObject d = new JsonObject();
        d.addProperty("positions", total); d.addProperty("same_on_rerun", same); d.addProperty("differ_other_seed", diffSeed);
        d.addProperty("differ_other_random_name", diffName); d.addProperty("differ_other_biome", diffBiome);
        o.add("determinism_y1to7_chunk2_2", d);
        write("vertical_gradient.json", o);
    }

    // Steep: planar slopes per chunk; the top height rises by s blocks per column toward one direction.
    static void steep() throws Exception {
        JsonObject o = new JsonObject();
        String[] dirs = {"+x", "-x", "+z", "-z"};
        MaterialRule r = markIf("{\"type\":\"minecraft:steep\"}");
        for (String d : dirs) for (int s = 1; s <= 5; s++) {
            final int slope = s;
            Fill f = (x, y, z) -> {
                int lx = Math.floorMod(x, 16), lz = Math.floorMod(z, 16);
                int t = switch (d) { case "+x" -> lx; case "-x" -> 15 - lx; case "+z" -> lz; default -> 15 - lz; };
                return y <= 60 + slope * t ? STONE : AIR;
            };
            ProtoChunk ch = run(0, 0, 0, "plains", f, r);
            JsonArray grid = new JsonArray(); // rows lz=0..15, chars lx=0..15: '#' steep at the top block
            for (int lz = 0; lz < 16; lz++) {
                StringBuilder b = new StringBuilder();
                for (int lx = 0; lx < 16; lx++) {
                    int t = switch (d) { case "+x" -> lx; case "-x" -> 15 - lx; case "+z" -> lz; default -> 15 - lz; };
                    int top = 60 + slope * t;
                    boolean atTop = ch.getBlockState(new BlockPos(lx, top, lz)).is(Blocks.GOLD_BLOCK);
                    boolean below = ch.getBlockState(new BlockPos(lx, top - 20, lz)).is(Blocks.GOLD_BLOCK);
                    b.append(atTop ? (below ? '#' : 't') : (below ? 'b' : '.'));
                }
                grid.add(b.toString());
            }
            o.add("rise_toward_" + d + "_by_" + s, grid);
        }
        // Single step: flat at 60, one 10x10 plateau at 70 in the middle (lx,lz 3..12).
        Fill plateau = (x, y, z) -> {
            int lx = Math.floorMod(x, 16), lz = Math.floorMod(z, 16);
            int top = (lx >= 3 && lx <= 12 && lz >= 3 && lz <= 12) ? 70 : 60;
            return y <= top ? STONE : AIR;
        };
        for (int h : new int[]{3, 4, 10}) {
            final int hh = h;
            Fill pf = (x, y, z) -> {
                int lx = Math.floorMod(x, 16), lz = Math.floorMod(z, 16);
                int top = (lx >= 3 && lx <= 12 && lz >= 3 && lz <= 12) ? 60 + hh : 60;
                return y <= top ? STONE : AIR;
            };
            ProtoChunk ch = run(0, 0, 0, "plains", pf, r);
            JsonArray grid = new JsonArray();
            for (int lz = 0; lz < 16; lz++) {
                StringBuilder b = new StringBuilder();
                for (int lx = 0; lx < 16; lx++) {
                    int top = (lx >= 3 && lx <= 12 && lz >= 3 && lz <= 12) ? 60 + hh : 60;
                    b.append(ch.getBlockState(new BlockPos(lx, top, lz)).is(Blocks.GOLD_BLOCK) ? '#' : '.');
                }
                grid.add(b.toString());
            }
            o.add("plateau_3to12_height_" + h, grid);
        }
        write("steep.json", o);
    }

    static void prelim() throws Exception {
        long seed = 0;
        JsonArray rows = new JsonArray();
        RandomState rs = state(seed);
        var pf = rs.surfaceSystem().preliminarySurfaceFunction();
        int[][] chunks = {{0, 0}, {1, 0}, {0, 1}, {7, -5}, {-20, 33}};
        for (int[] c : chunks) {
            ProtoChunk ch = run(seed, c[0], c[1], "plains", stoneTo(250), markIf("{\"type\":\"minecraft:above_preliminary_surface\"}"));
            for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++) {
                int x = c[0] * 16 + lx, z = c[1] * 16 + lz;
                int lowest = 251;
                for (int y = 250; y >= -64 && ch.getBlockState(new BlockPos(x, y, z)).is(Blocks.GOLD_BLOCK); y--) lowest = y;
                JsonArray r = new JsonArray();
                r.add(x); r.add(z); r.add(lowest); r.add(surfaceDepth(seed, x, z));
                r.add(rs.sampleBlockValueUncached(pf, x, 0, z));
                r.add(rs.sampleBlockValueUncached(pf, x, 100, z));
                rows.add(r);
            }
            JsonArray corners = new JsonArray();
            for (int dx = 0; dx <= 16; dx += 16) for (int dz = 0; dz <= 16; dz += 16) {
                JsonArray k = new JsonArray();
                k.add(c[0] * 16 + dx); k.add(c[1] * 16 + dz); k.add(rs.sampleBlockValueUncached(pf, c[0] * 16 + dx, 0, c[1] * 16 + dz));
                corners.add(k);
            }
            rows.add(corners);
        }
        JsonObject o = new JsonObject();
        o.addProperty("columns", "x,z,lowest_marked_y(251=none),surface_depth,preliminary(x,0,z),preliminary(x,100,z); after each chunk: corner values [x,z,prelim]");
        o.add("rows", rows);
        write("above_preliminary_surface.json", o);
    }

    static void biomeCond() throws Exception {
        long seed = 0;
        Holder<Biome> plains = biome("plains"), desert = biome("desert");
        BiomeAt split = (qx, qy, qz) -> qx < 2 ? plains : desert;
        ProtoChunk ch = run(seed, 0, 0, split, stoneTo(70), markIf("{\"type\":\"minecraft:biome\",\"biome_is\":[\"minecraft:desert\"]}"));
        BiomeManager bm = new BiomeManager(split::at, BiomeManager.obfuscateSeed(seed));
        int agreeZoom = 0, agreeQuart = 0, total = 0;
        JsonArray grid = new JsonArray();
        for (int y = 70; y >= 60; y--) {
            StringBuilder b = new StringBuilder();
            for (int lx = 0; lx < 16; lx++) {
                boolean m = ch.getBlockState(new BlockPos(lx, y, 0)).is(Blocks.GOLD_BLOCK);
                b.append(m ? 'D' : 'p');
            }
            grid.add("y" + y + " z0: " + b);
        }
        for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++) for (int y = 40; y <= 70; y++) {
            boolean m = ch.getBlockState(new BlockPos(lx, y, lz)).is(Blocks.GOLD_BLOCK);
            total++;
            if (m == bm.getBiome(new BlockPos(lx, y, lz)).is(Biomes.DESERT)) agreeZoom++;
            if (m == split.at(lx >> 2, y >> 2, lz >> 2).is(Biomes.DESERT)) agreeQuart++;
        }
        // Which biome does the rule see for a whole column: does the marked set vary with y inside a column?
        int colsVarying = 0;
        for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++) {
            boolean first = ch.getBlockState(new BlockPos(lx, 70, lz)).is(Blocks.GOLD_BLOCK);
            for (int y = 40; y <= 69; y++) if (ch.getBlockState(new BlockPos(lx, y, lz)).is(Blocks.GOLD_BLOCK) != first) { colsVarying++; break; }
        }
        JsonObject o = new JsonObject();
        o.addProperty("setup", "biome source: quart x<2 (block x 0..7) plains, else desert; stone y<=70; marked = biome(desert)");
        o.add("rows_y70_to_60_at_z0", grid);
        o.addProperty("positions_y40_70", total);
        o.addProperty("agree_with_BiomeManager_getBiome", agreeZoom);
        o.addProperty("agree_with_plain_quart_lookup", agreeQuart);
        o.addProperty("columns_where_marking_varies_with_y", colsVarying);
        write("biome_condition.json", o);
    }

    static void bands() throws Exception {
        JsonObject o = new JsonObject();
        Map<String, Character> code = new LinkedHashMap<>();
        for (long seed : new long[]{0, 1}) {
            ProtoChunk ch = run(seed, 0, 0, "badlands", stoneTo(319), rule("{\"type\":\"minecraft:bandlands\"}"));
            JsonObject so = new JsonObject();
            int[][] cols = {{0, 0}, {15, 0}, {0, 15}, {8, 8}, {3, 11}};
            for (int[] c : cols) {
                StringBuilder b = new StringBuilder();
                for (int y = -64; y < 320; y++) {
                    String id = id(ch.getBlockState(new BlockPos(c[0], y, c[1])));
                    code.computeIfAbsent(id, k -> (char) ('A' + code.size()));
                    b.append(code.get(id));
                }
                JsonObject co = new JsonObject();
                co.addProperty("y_from_-64", b.toString());
                co.addProperty("clay_bands_offset(x,0,z)", noise(seed, "clay_bands_offset", c[0], 0, c[1]));
                so.add(c[0] + "," + c[1], co);
            }
            o.add("seed" + seed, so);
        }
        JsonObject legend = new JsonObject();
        code.forEach((k, v) -> legend.addProperty(String.valueOf(v), k));
        o.add("legend", legend);
        write("bands.json", o);
    }

    static void extra() throws Exception {
        JsonObject o = new JsonObject();
        long seed = 0;
        // (a) Columns with the lowest surface noise and the secondary noise extremes, from a coarse scan.
        int[] lowChunk = null, secLo = null, secHi = null;
        double lo = 9, sLo = 9, sHi = -9;
        for (int x = -8192; x < 8192; x += 8) for (int z = -8192; z < 8192; z += 8) {
            double v = noise(seed, "surface", x, 0, z), s = noise(seed, "surface_secondary", x, 0, z);
            if (v < lo) { lo = v; lowChunk = new int[]{Math.floorDiv(x, 16), Math.floorDiv(z, 16)}; }
            if (s < sLo) { sLo = s; secLo = new int[]{Math.floorDiv(x, 16), Math.floorDiv(z, 16)}; }
            if (s > sHi) { sHi = s; secHi = new int[]{Math.floorDiv(x, 16), Math.floorDiv(z, 16)}; }
        }
        JsonObject scan = new JsonObject();
        scan.addProperty("surface_min", lo); scan.addProperty("surface_secondary_min", sLo); scan.addProperty("surface_secondary_max", sHi);
        o.add("scan_16384_step8", scan);
        JsonArray rows = new JsonArray();
        String[] conds = {"\"minecraft:under_floor\"", "{\"type\":\"minecraft:hole\"}", "\"minecraft:deep_under_floor\"", "\"minecraft:very_deep_under_floor\"",
            "{\"type\":\"minecraft:y_above\",\"anchor\":{\"absolute\":70},\"surface_depth_multiplier\":1,\"add_stone_depth\":false}"};
        for (int[] ch : new int[][]{lowChunk, secLo, secHi}) {
            ProtoChunk[] cs = new ProtoChunk[conds.length];
            for (int k = 0; k < conds.length; k++) cs[k] = run(seed, ch[0], ch[1], "plains", stoneTo(80), markIf(conds[k]));
            for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++) {
                int x = ch[0] * 16 + lx, z = ch[1] * 16 + lz;
                JsonArray r = new JsonArray();
                r.add(x); r.add(z); r.add(countDown(cs[0], x, z, 80));
                r.add(cs[1].getBlockState(new BlockPos(x, 80, z)).is(Blocks.GOLD_BLOCK) ? 1 : 0);
                r.add(countDown(cs[2], x, z, 80)); r.add(countDown(cs[3], x, z, 80));
                int lowest = 81;
                for (int y = 80; y >= -64 && cs[4].getBlockState(new BlockPos(x, y, z)).is(Blocks.GOLD_BLOCK); y--) lowest = y;
                r.add(lowest);
                r.add(noise(seed, "surface", x, 0, z)); r.add(noise(seed, "surface_secondary", x, 0, z));
                rows.add(r);
            }
        }
        o.addProperty("depth_columns", "x,z,n_under_floor,hole,n_deep(6),n_very_deep(30),lowest y_above(70,m1),surface,surface_secondary");
        o.add("depth_rows", rows);
        // (b) Two water layers; (f) lava instead of water.
        BlockState LAVA = Blocks.LAVA.defaultBlockState();
        Map<String, Fill> fills = new LinkedHashMap<>();
        fills.put("stone<=59,water60-69,stone70-79,water80-89,air", (x, y, z) -> y <= 59 ? STONE : y <= 69 ? WATER : y <= 79 ? STONE : y <= 89 ? WATER : AIR);
        fills.put("stone<=60,lava61-70,air", (x, y, z) -> y <= 60 ? STONE : y <= 70 ? LAVA : AIR);
        String[][] wc = {{"water(off0,m0,noStone)", "{\"type\":\"minecraft:water\",\"offset\":0,\"surface_depth_multiplier\":0,\"add_stone_depth\":false}"},
            {"water(off-25,m0,noStone)", "{\"type\":\"minecraft:water\",\"offset\":-25,\"surface_depth_multiplier\":0,\"add_stone_depth\":false}"},
            {"water(off-11,m0,addStone)", "{\"type\":\"minecraft:water\",\"offset\":-11,\"surface_depth_multiplier\":0,\"add_stone_depth\":true}"},
            {"on_floor", "\"minecraft:on_floor\""}, {"floor_off12", "{\"type\":\"minecraft:stone_depth\",\"offset\":12,\"add_surface_depth\":false,\"secondary_depth_range\":0,\"surface_type\":\"floor\"}"},
            {"on_ceiling", "\"minecraft:on_ceiling\""}};
        for (var fe : fills.entrySet()) {
            JsonObject fo = new JsonObject();
            for (String[] c : wc) fo.addProperty(c[0], ranges(golds(run(seed, 0, 0, "plains", fe.getValue(), markIf(c[1])), 0, 5)));
            o.add(fe.getKey(), fo);
        }
        // (c) Steep under water: slope rising toward +z by 3 from 30, water up to 62.
        Fill wet = (x, y, z) -> { int lz = Math.floorMod(z, 16); int top = 30 + 3 * lz; return y <= top ? STONE : y <= 62 ? WATER : AIR; };
        ProtoChunk sw = run(seed, 0, 0, "plains", wet, markIf("{\"type\":\"minecraft:steep\"}"));
        StringBuilder b = new StringBuilder();
        for (int lz = 0; lz < 16; lz++) b.append(sw.getBlockState(new BlockPos(5, 30 + 3 * lz, lz)).is(Blocks.GOLD_BLOCK) ? '#' : '.');
        o.addProperty("steep_underwater_rise+z_by3_lz0to15", b.toString());
        write("extra.json", o);
    }

    // Code-side extensions seen in buildSurface: frozen ocean icebergs and eroded badlands pillars.
    static void extensions() throws Exception {
        long seed = 0;
        JsonObject o = new JsonObject();
        String[] keys = {"iceberg_pillar", "iceberg_pillar_roof", "iceberg_surface", "badlands_pillar", "badlands_pillar_roof", "badlands_surface"};
        Object[][] cases = {{"frozen_ocean", "sea_floor40_water41to62", layers(new int[][]{{-64, 40, 1}, {41, 62, 2}}), 62},
            {"deep_frozen_ocean", "sea_floor40_water41to62", layers(new int[][]{{-64, 40, 1}, {41, 62, 2}}), 62},
            {"frozen_ocean", "land66", stoneTo(66), 66},
            {"eroded_badlands", "land66", stoneTo(66), 66}, {"eroded_badlands", "land90", stoneTo(90), 90},
            {"eroded_badlands", "sea_floor50_water51to62", layers(new int[][]{{-64, 50, 1}, {51, 62, 2}}), 62}};
        for (Object[] c : cases) {
            JsonArray rows = new JsonArray();
            Map<String, Integer> hist = new TreeMap<>();
            for (int cx = 0; cx < 4; cx++) for (int cz = 0; cz < 2; cz++) {
                ProtoChunk ch = run(seed, cx, cz, (String) c[0], (Fill) c[2], overworld());
                for (int lx = 0; lx < 16; lx++) for (int lz = 0; lz < 16; lz++) {
                    int x = cx * 16 + lx, z = cz * 16 + lz, top = (Integer) c[3];
                    int newTop = -65;
                    for (int y = 319; y >= -64; y--) if (!ch.getBlockState(new BlockPos(x, y, z)).isAir()) { newTop = y; break; }
                    for (int y = newTop; y > top - 30 && y >= -64; y--) {
                        BlockState s = ch.getBlockState(new BlockPos(x, y, z));
                        BlockState orig = ((Fill) c[2]).at(x, y, z);
                        if (y > top || (orig.is(Blocks.WATER) && !s.is(Blocks.WATER))) hist.merge(id(s), 1, Integer::sum);
                    }
                    JsonArray r = new JsonArray();
                    r.add(x); r.add(z); r.add(newTop); r.add(column(ch, x, z, Math.min(319, newTop), top - 25));
                    for (String k : keys) r.add(noise(seed, k, x, 0, z));
                    rows.add(r);
                }
            }
            JsonObject co = new JsonObject();
            co.addProperty("columns", "x,z,new_top,column_from_new_top_to_top-25," + String.join(",", keys));
            JsonObject h = new JsonObject(); hist.forEach(h::addProperty);
            co.add("blocks_above_top_or_replacing_water", h);
            co.add("rows", rows);
            o.add(c[0] + "_" + c[1], co);
        }
        write("extensions.json", o);
    }

    // The full overworld rule on hand-made profiles, per biome: the top blocks of 16 columns, plus their inputs.
    static void golden() throws Exception {
        long seed = 0;
        String[] biomes = {"plains", "forest", "desert", "badlands", "wooded_badlands", "eroded_badlands", "swamp", "mangrove_swamp",
            "beach", "snowy_beach", "stony_shore", "river", "frozen_river", "ocean", "deep_ocean", "warm_ocean", "lukewarm_ocean", "cold_ocean",
            "frozen_ocean", "deep_frozen_ocean", "frozen_peaks", "jagged_peaks", "snowy_slopes", "grove", "stony_peaks", "windswept_hills",
            "windswept_gravelly_hills", "windswept_savanna", "old_growth_pine_taiga", "old_growth_spruce_taiga", "ice_spikes", "mushroom_fields",
            "dappled_forest", "dripstone_caves", "sulfur_caves", "snowy_plains", "pale_garden"};
        Map<String, Fill> profiles = new LinkedHashMap<>();
        profiles.put("land70", stoneTo(70));
        profiles.put("shore62", stoneTo(62));
        profiles.put("sea_floor50_water51to62", layers(new int[][]{{-64, 50, 1}, {51, 62, 2}}));
        profiles.put("high140", stoneTo(140));
        profiles.put("high300", stoneTo(300));
        Map<String, Integer> tops = Map.of("land70", 70, "shore62", 62, "sea_floor50_water51to62", 62, "high140", 140, "high300", 300);
        MaterialRule ow = overworld();
        JsonObject o = new JsonObject();
        JsonObject inputs = new JsonObject();
        String[] keys = {"surface", "surface_secondary", "surface_swamp", "packed_ice", "ice", "powder_snow", "calcite", "gravel", "small_patch", "clay_bands_offset"};
        for (int lx = 0; lx < 16; lx++) {
            JsonObject in = new JsonObject();
            in.addProperty("surface_depth", surfaceDepth(seed, lx, 0));
            for (String k : keys) in.addProperty(k, noise(seed, k, lx, 0, 0));
            inputs.add("x" + lx + "_z0", in);
        }
        o.add("inputs_seed0", inputs);
        for (String b : biomes) {
            JsonObject bo = new JsonObject();
            for (var pe : profiles.entrySet()) {
                ProtoChunk ch = run(seed, 0, 0, b, pe.getValue(), ow);
                int top = tops.get(pe.getKey());
                JsonObject po = new JsonObject();
                for (int lx = 0; lx < 16; lx++) po.addProperty("x" + lx, column(ch, lx, 0, Math.min(319, top + 40), top - 40));
                bo.add(pe.getKey(), po);
            }
            o.add(b, bo);
        }
        write("golden_columns.json", o);
    }
}
