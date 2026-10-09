import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.mojang.serialization.JsonOps;
import java.awt.image.BufferedImage;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;
import java.util.function.Function;
import javax.imageio.ImageIO;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.RegistryOps;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.Bootstrap;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.DryFoliageColor;
import net.minecraft.world.level.FoliageColor;
import net.minecraft.world.level.GrassColor;
import net.minecraft.world.level.LightLayer;
import net.minecraft.world.level.WorldGenLevel;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.biome.BiomeSpecialEffects;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.BlockStateProperties;
import net.minecraft.world.level.levelgen.Heightmap;
import net.minecraft.world.level.levelgen.feature.SnowAndFreezeFeature;
import net.minecraft.world.level.material.Fluids;

// Black-box probe of 26.3 climate behaviour: per-block temperature (found by bisecting a biome's base temperature
// against the jar's own coldEnoughToSnow), the frozen modifier, the freeze and snow checks and the snow-and-freeze
// feature on a small map-backed level, and the grass/foliage colormap lookups. Records numbers and outcomes only.
// usage: java -cp <classes>;<classpath> ClimateProbe <outDir> <colormapDir>
public class ClimateProbe {
    static HolderLookup.Provider lookup;
    static RegistryOps<JsonElement> ops;
    static JsonObject plainsJson;
    static Path out;

    static Holder<Biome> biome(String id) {
        return lookup.lookupOrThrow(Registries.BIOME).getOrThrow(ResourceKey.create(Registries.BIOME, Identifier.withDefaultNamespace(id)));
    }
    static Biome custom(float t, String modifier) {
        JsonObject j = plainsJson.deepCopy();
        j.addProperty("temperature", t);
        j.addProperty("temperature_modifier", modifier);
        return Biome.DIRECT_CODEC.parse(ops, j).getOrThrow();
    }
    // Smallest base temperature T (float) at which the position is no longer cold enough to snow; adj = T - 0.15.
    static float flip(BlockPos p, int seaLevel, String modifier) {
        double lo = -3, hi = 3; // cold at lo, not cold at hi
        for (int i = 0; i < 60; i++) {
            double mid = (lo + hi) / 2;
            if (custom((float) mid, modifier).coldEnoughToSnow(p, seaLevel)) lo = mid; else hi = mid;
            if ((float) lo == (float) hi) break;
        }
        float f = (float) hi;
        while (custom(Math.nextDown(f), modifier).coldEnoughToSnow(p, seaLevel) == false) f = Math.nextDown(f);
        while (custom(f, modifier).coldEnoughToSnow(p, seaLevel)) f = Math.nextUp(f);
        return f;
    }
    static void write(String name, JsonElement e) throws Exception {
        Files.writeString(out.resolve(name), e.toString(), StandardCharsets.UTF_8);
        System.out.println("WROTE " + name);
    }
    static int[] pixels(Path png) throws Exception {
        BufferedImage img = ImageIO.read(png.toFile());
        return img.getRGB(0, 0, img.getWidth(), img.getHeight(), null, 0, img.getWidth());
    }

    public static void main(String[] args) throws Exception {
        out = Path.of(args[0]);
        Path colormaps = Path.of(args[1]);
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
        lookup = layers.compositeAccess();
        ops = lookup.createSerializationContext(JsonOps.INSTANCE);
        plainsJson = Biome.DIRECT_CODEC.encodeStart(ops, biome("plains").value()).getOrThrow().getAsJsonObject();
        for (int i = 2; i < args.length; i++) {
            long t = System.nanoTime();
            switch (args[i]) {
                case "temp" -> temperature();
                case "frozen" -> frozen();
                case "freeze" -> freeze();
                case "colors" -> colors(colormaps);
                case "attrs" -> attrs();
                default -> throw new IllegalArgumentException(args[i]);
            }
            System.out.printf("%s %.1fs%n", args[i], (System.nanoTime() - t) / 1e9);
        }
    }

    static void temperature() throws Exception {
        JsonObject o = new JsonObject();
        JsonArray rows = new JsonArray();
        int[][] xz = {{0, 0}, {100, -37}, {-2000, 513}, {12345, 6789}, {7, 7}, {-64, 64}, {30000, -30000}, {511, 1024}};
        int[] ys = {-64, 0, 62, 63, 79, 80, 81, 82, 83, 90, 100, 120, 150, 200, 250, 319, 400};
        for (int[] p : xz) for (int y : ys) {
            JsonArray r = new JsonArray();
            float f63 = flip(new BlockPos(p[0], y, p[1]), 63, "none");
            r.add(p[0]); r.add(y); r.add(p[1]); r.add(f63 - 0.15f);
            r.add(flip(new BlockPos(p[0], y, p[1]), 0, "none") - 0.15f);
            rows.add(r);
        }
        o.addProperty("columns", "x,y,z,adj_sea63,adj_sea0  (adj = flip T - 0.15: the amount the height rule subtracts; T is the base temperature at which coldEnoughToSnow turns false)");
        o.add("rows", rows);
        // Smoothness of the noise term: a row of x at y=200, z=0.
        JsonArray line = new JsonArray();
        for (int x = 0; x <= 64; x += 2) line.add(flip(new BlockPos(x, 200, 0), 63, "none") - 0.15f);
        o.add("adj_y200_z0_x0to64_step2", line);
        // y-independence of the noise term inside one column, and the same column under a different seed: the API has no seed.
        write("temperature.json", o);
    }

    static void frozen() throws Exception {
        JsonObject o = new JsonObject();
        JsonArray rows = new JsonArray();
        Random rnd = new Random(263);
        int hits = 0;
        for (int i = 0; i < 4000; i++) {
            int x = rnd.nextInt(20000) - 10000, z = rnd.nextInt(20000) - 10000, y = rnd.nextInt(200) - 64;
            float base = new float[]{0.0f, 0.1f, 0.2f, 0.5f}[i % 4];
            float m = Biome.TemperatureModifier.FROZEN.modifyTemperature(new BlockPos(x, y, z), base);
            float m2 = Biome.TemperatureModifier.FROZEN.modifyTemperature(new BlockPos(x, y + 37, z), base);
            if (m != base) hits++;
            JsonArray r = new JsonArray();
            r.add(x); r.add(y); r.add(z); r.add(base); r.add(m); r.add(m2 == m ? 1 : 0);
            r.add(Biome.FROZEN_TEMPERATURE_NOISE.get(x * 0.05, z * 0.05));
            r.add(Biome.BIOME_INFO_NOISE.get(x * 0.2, z * 0.2));
            r.add(Biome.BIOME_INFO_NOISE.get(x * 0.09, z * 0.09));
            rows.add(r);
        }
        o.addProperty("columns", "x,y,z,base,FROZEN.modify(pos,base),same_at_y+37,FROZEN_TEMPERATURE_NOISE.get(x*0.05,z*0.05),BIOME_INFO_NOISE.get(x*0.2,z*0.2),BIOME_INFO_NOISE.get(x*0.09,z*0.09)");
        o.addProperty("changed", hits);
        o.addProperty("NONE_modify_0.5_at_0,64,0", Biome.TemperatureModifier.NONE.modifyTemperature(new BlockPos(0, 64, 0), 0.5f));
        o.addProperty("FROZEN_TEMPERATURE_NOISE_range", Biome.FROZEN_TEMPERATURE_NOISE.range().toString());
        o.addProperty("BIOME_INFO_NOISE_range", Biome.BIOME_INFO_NOISE.range().toString());
        o.add("rows", rows);
        // The real frozen_ocean biome: snow checks at sea level across positions, with the modifier.
        Biome fo = biome("frozen_ocean").value();
        int cold = 0, melt = 0;
        for (int i = 0; i < 4000; i++) {
            int x = rnd.nextInt(20000) - 10000, z = rnd.nextInt(20000) - 10000;
            if (fo.coldEnoughToSnow(new BlockPos(x, 63, z), 63)) cold++;
            if (fo.shouldMeltFrozenOceanIcebergSlightly(new BlockPos(x, 63, z), 63)) melt++;
        }
        o.addProperty("frozen_ocean_y63_cold_enough_to_snow_of_4000", cold);
        o.addProperty("frozen_ocean_y63_should_melt_iceberg_slightly_of_4000", melt);
        write("frozen_modifier.json", o);
    }

    // A map-backed level for the freeze checks and the snow-and-freeze feature. Unknown abstract methods throw.
    static final class World implements InvocationHandler {
        final Map<BlockPos, BlockState> blocks = new HashMap<>();
        Function<BlockPos, Holder<Biome>> biomeAt;
        int blockLight = 0;
        final Set<String> calls = new TreeSet<>();
        BlockState get(BlockPos p) { return blocks.getOrDefault(p.immutable(), Blocks.AIR.defaultBlockState()); }
        int height(Heightmap.Types t, int x, int z) {
            for (int y = 319; y >= -64; y--) if (t.isOpaque().test(get(new BlockPos(x, y, z)))) return y + 1;
            return -64;
        }
        public Object invoke(Object proxy, Method m, Object[] a) throws Throwable {
            String n = m.getName();
            calls.add(n);
            switch (n) {
                case "getBlockState": return get((BlockPos) a[0]);
                case "getFluidState": return get((BlockPos) a[0]).getFluidState();
                case "isStateAtPosition": return ((java.util.function.Predicate<BlockState>) a[1]).test(get((BlockPos) a[0]));
                case "isFluidAtPosition": return ((java.util.function.Predicate<net.minecraft.world.level.material.FluidState>) a[1]).test(get((BlockPos) a[0]).getFluidState());
                case "getBrightness": return a[0] == LightLayer.BLOCK ? blockLight : 15;
                case "getMinY": return -64;
                case "getHeight": if (a == null || a.length == 0) return 384; calls.add("getHeight:" + a[0]); return height((Heightmap.Types) a[0], (Integer) a[1], (Integer) a[2]);
                case "getHeightmapPos": { calls.add("getHeightmapPos:" + a[0]); BlockPos p = (BlockPos) a[1]; return new BlockPos(p.getX(), height((Heightmap.Types) a[0], p.getX(), p.getZ()), p.getZ()); }
                case "getBiome": calls.add("getBiome@y" + ((BlockPos) a[0]).getY()); return biomeAt.apply((BlockPos) a[0]);
                case "getSeaLevel": return 63;
                case "setBlock": blocks.put(((BlockPos) a[0]).immutable(), (BlockState) a[1]); return true;
                case "isClientSide": return false;
                case "hashCode": return System.identityHashCode(proxy);
                case "equals": return proxy == a[0];
                case "toString": return "ProbeWorld";
            }
            if (m.isDefault()) return InvocationHandler.invokeDefault(proxy, m, a);
            throw new UnsupportedOperationException(n);
        }
    }
    static World world() { return new World(); }
    static WorldGenLevel level(World w) {
        return (WorldGenLevel) Proxy.newProxyInstance(ClimateProbe.class.getClassLoader(), new Class<?>[]{WorldGenLevel.class}, w);
    }
    static String id(BlockState s) {
        String n = BuiltInRegistries.BLOCK.getKey(s.getBlock()).getPath();
        if (s.hasProperty(BlockStateProperties.SNOWY) && s.getValue(BlockStateProperties.SNOWY)) n += "[snowy]";
        if (s.hasProperty(BlockStateProperties.LAYERS)) n += "[layers=" + s.getValue(BlockStateProperties.LAYERS) + "]";
        if (s.hasProperty(BlockStateProperties.LEVEL)) n += "[level=" + s.getValue(BlockStateProperties.LEVEL) + "]";
        return n;
    }

    static void freeze() throws Exception {
        JsonObject o = new JsonObject();
        Holder<Biome> cold = Holder.direct(custom(-0.5f, "none")), warm = Holder.direct(custom(0.8f, "none"));
        // 1. shouldFreeze / shouldSnow at y=64 for the block at pos and the block below it.
        String[] atPos = {"water", "water_level1", "ice", "air", "stone", "snow", "snow_layers7", "lava"};
        String[] below = {"stone", "grass_block", "dirt", "sand", "gravel", "ice", "packed_ice", "blue_ice", "snow_block", "snow", "snow_layers7",
            "snow_layers8", "powder_snow", "water", "air", "oak_leaves", "spruce_leaves", "glass", "oak_slab", "oak_slab_top", "mud", "soul_sand",
            "honey_block", "barrier", "podzol", "mycelium", "dirt_path", "farmland", "lava", "oak_fence", "torch"};
        JsonObject f = new JsonObject();
        for (Holder<Biome> b : List.of(cold, warm)) for (int light : new int[]{0, 9, 10}) for (String s : atPos) {
            World w = world(); w.biomeAt = p -> b; w.blockLight = light;
            BlockPos p = new BlockPos(0, 64, 0);
            w.blocks.put(p, state(s)); w.blocks.put(p.below(), state("stone"));
            for (BlockPos n : new BlockPos[]{p.north(), p.south(), p.east(), p.west()}) w.blocks.put(n, state("water"));
            WorldGenLevel l = level(w);
            String key = (b == cold ? "cold" : "warm") + "_light" + light + "_" + s;
            f.addProperty(key + "_freeze", b.value().shouldFreeze(l, p, false));
            f.addProperty(key + "_freezeEdge(all4water)", b.value().shouldFreeze(l, p, true));
            w.blocks.put(p.east(), state("stone"));
            f.addProperty(key + "_freezeEdge(eastStone)", b.value().shouldFreeze(l, p, true));
            f.addProperty(key + "_snow(onStone)", b.value().shouldSnow(l, p));
        }
        o.add("should_freeze_and_snow_y64", f);
        JsonObject sn = new JsonObject();
        for (String s : below) {
            World w = world(); w.biomeAt = p -> cold;
            BlockPos p = new BlockPos(0, 64, 0);
            w.blocks.put(p.below(), state(s));
            if (s.equals("air")) w.blocks.put(p.below(2), state("stone"));
            sn.addProperty(s, cold.value().shouldSnow(level(w), p));
        }
        o.add("should_snow_air_at_64_cold_by_block_below", sn);
        JsonObject ys = new JsonObject();
        for (int y : new int[]{-65, -64, 318, 319, 320}) {
            World w = world(); w.biomeAt = p -> cold;
            BlockPos p = new BlockPos(0, y, 0);
            w.blocks.put(p, state("water")); w.blocks.put(p.below(), state("stone"));
            ys.addProperty("freeze_y" + y, cold.value().shouldFreeze(level(w), p, false));
            w.blocks.put(p, state("air"));
            ys.addProperty("snow_y" + y, cold.value().shouldSnow(level(w), p));
        }
        o.add("build_height_limits", ys);

        // 2. The snow-and-freeze feature on scenes: each column of chunk 0 gets one scene (lx = scene index).
        String[][] scenes = {
            {"grass_top70", "stone<=69,grass_block@70"}, {"water_surface62", "stone<=55,water56-62"}, {"leaves_on_log", "stone<=65,oak_log66-69,oak_leaves@70"},
            {"sand_top70", "stone<=69,sand@70"}, {"snow_block_top", "stone<=69,snow_block@70"}, {"ice_top", "stone<=69,ice@70"},
            {"packed_ice_top", "stone<=69,packed_ice@70"}, {"powder_snow_top", "stone<=69,powder_snow@70"}, {"podzol_top", "stone<=69,podzol@70"},
            {"mycelium_top", "stone<=69,mycelium@70"}, {"water_1deep_on_grass", "stone<=60,grass_block@61,water@62"},
            {"flowing_water_top", "stone<=61,water_level1@62"}, {"slab_top", "stone<=69,oak_slab@70"}, {"glass_top", "stone<=69,glass@70"},
            {"dirt_top", "stone<=69,dirt@70"}, {"lava_surface", "stone<=61,lava@62"}};
        for (String bname : new String[]{"cold", "warm", "split_y71", "split_y63"}) {
            World w = world();
            w.biomeAt = switch (bname) { case "cold" -> p -> cold; case "warm" -> p -> warm; case "split_y63" -> p -> p.getY() >= 63 ? cold : warm; default -> p -> p.getY() >= 71 ? cold : warm; };
            for (int i = 0; i < scenes.length; i++) for (int lz = 0; lz < 16; lz++) fill(w, i, lz, scenes[i][1]);
            Map<BlockPos, BlockState> before = new HashMap<>(w.blocks);
            new SnowAndFreezeFeature().place(level(w), null, RandomSource.create(0), new BlockPos(0, 0, 0));
            JsonObject so = new JsonObject();
            for (int i = 0; i < scenes.length; i++) {
                List<String> changes = new ArrayList<>();
                for (int y = -64; y < 320; y++) {
                    BlockPos p = new BlockPos(i, y, 3);
                    BlockState a = before.getOrDefault(p, Blocks.AIR.defaultBlockState()), c = w.get(p);
                    if (a != c) changes.add(y + ":" + id(a) + "->" + id(c));
                }
                so.addProperty(scenes[i][0], String.join(" ", changes));
            }
            int touched = 0;
            for (var e : w.blocks.entrySet()) if (before.get(e.getKey()) != e.getValue() && (e.getKey().getX() < 0 || e.getKey().getX() > 15 || e.getKey().getZ() < 0 || e.getKey().getZ() > 15)) touched++;
            so.addProperty("changes_outside_chunk0", touched);
            o.add("feature_" + bname, so);
            o.addProperty("level_methods_called_" + bname, String.join(",", w.calls));
        }
        // 3. The snow line on a mountain: a cold-ish biome (T=0.3) with columns topped at y=100..300 in steps.
        Holder<Biome> taiga = Holder.direct(custom(0.3f, "none"));
        World w = world(); w.biomeAt = p -> taiga;
        int[] tops = new int[16];
        for (int lx = 0; lx < 16; lx++) { tops[lx] = 180 + lx * 4; for (int lz = 0; lz < 16; lz++) fill(w, lx, lz, "stone<=" + (tops[lx] - 1) + ",grass_block@" + tops[lx]); }
        new SnowAndFreezeFeature().place(level(w), null, RandomSource.create(0), new BlockPos(0, 0, 0));
        JsonObject line = new JsonObject();
        for (int lx = 0; lx < 16; lx++) {
            StringBuilder b = new StringBuilder();
            for (int lz = 0; lz < 16; lz++) b.append(w.get(new BlockPos(lx, tops[lx] + 1, lz)).is(Blocks.SNOW) ? '*' : '.');
            line.addProperty("top" + tops[lx], b.toString());
        }
        o.add("snow_line_T0.3_rows_lz0to15", line);
        write("freeze.json", o);
    }
    static BlockState state(String s) {
        return switch (s) {
            case "water_level1" -> Blocks.WATER.defaultBlockState().setValue(BlockStateProperties.LEVEL, 1);
            case "snow_layers7" -> Blocks.SNOW.defaultBlockState().setValue(BlockStateProperties.LAYERS, 7);
            case "snow_layers8" -> Blocks.SNOW.defaultBlockState().setValue(BlockStateProperties.LAYERS, 8);
            case "oak_slab_top" -> Blocks.OAK_SLAB.defaultBlockState().setValue(BlockStateProperties.SLAB_TYPE, net.minecraft.world.level.block.state.properties.SlabType.TOP);
            default -> BuiltInRegistries.BLOCK.getValue(Identifier.withDefaultNamespace(s)).defaultBlockState();
        };
    }
    static void fill(World w, int lx, int lz, String spec) { // "stone<=69,grass_block@70,water56-62"
        for (String part : spec.split(",")) {
            if (part.contains("<=")) { String[] k = part.split("<="); for (int y = 40; y <= Integer.parseInt(k[1]); y++) w.blocks.put(new BlockPos(lx, y, lz), state(k[0])); }
            else if (part.contains("@")) { String[] k = part.split("@"); w.blocks.put(new BlockPos(lx, Integer.parseInt(k[1]), lz), state(k[0])); }
            else { int d = part.lastIndexOf('-', part.length() - 1); String name = part.replaceAll("[0-9-]+$", ""); String[] r = part.substring(name.length()).split("-");
                for (int y = Integer.parseInt(r[0]); y <= Integer.parseInt(r[1]); y++) w.blocks.put(new BlockPos(lx, y, lz), state(name)); }
        }
    }

    static void attrs() throws Exception {
        JsonObject o = new JsonObject();
        for (var f : net.minecraft.world.attribute.EnvironmentAttributes.class.getFields()) {
            if (!java.lang.reflect.Modifier.isStatic(f.getModifiers())) continue;
            String n = f.getName();
            if (!(n.contains("FOG") || n.contains("SKY_COLOR") || n.contains("WATER"))) continue;
            Object v = f.get(null);
            if (v instanceof net.minecraft.world.attribute.EnvironmentAttribute<?> a)
                o.addProperty(n, String.valueOf(a.defaultValue()) + " interpolated=" + a.isSpatiallyInterpolated() + " positional=" + a.isPositional());
        }
        write("environment_attribute_defaults.json", o);
        System.out.println(o);
    }
    static String safe(java.util.function.IntSupplier f) {
        try { return String.format("#%06X", f.getAsInt() & 0xFFFFFF); } catch (Throwable t) { return "throws " + t.getClass().getSimpleName(); }
    }
    static void colors(Path dir) throws Exception {
        JsonObject o = new JsonObject();
        int[] grass = pixels(dir.resolve("grass.png")), foliage = pixels(dir.resolve("foliage.png"));
        GrassColor.init(grass);
        FoliageColor.init(foliage);
        o.addProperty("grass_png_pixels", grass.length);
        o.addProperty("GrassColor.getDefaultColor", String.format("#%06X", GrassColor.getDefaultColor() & 0xFFFFFF));
        JsonObject k = new JsonObject();
        for (var fl : FoliageColor.class.getFields()) k.addProperty("FoliageColor." + fl.getName(), String.format("#%06X", fl.getInt(null) & 0xFFFFFF));
        for (var fl : DryFoliageColor.class.getFields()) k.addProperty("DryFoliageColor." + fl.getName(), String.format("#%06X", fl.getInt(null) & 0xFFFFFF));
        o.add("constants", k);
        // Lookup grid incl. out-of-range inputs.
        double[] ts = {-0.5, 0, 0.05, 0.25, 0.5, 0.7, 0.8, 0.95, 1.0, 1.5, 2.0}, ds = {-0.2, 0, 0.3, 0.4, 0.5, 0.8, 0.9, 1.0, 1.5};
        JsonArray g = new JsonArray();
        for (double t : ts) for (double d : ds) {
            JsonArray r = new JsonArray();
            r.add(t); r.add(d); r.add(safe(() -> GrassColor.get(t, d))); r.add(safe(() -> FoliageColor.get(t, d)));
            g.add(r);
        }
        o.addProperty("grid_columns", "temperature,downfall,GrassColor.get,FoliageColor.get");
        o.add("grid", g);
        // Exhaustive check of the lookup hypothesis on a fine grid (inside [0,1]).
        int okG = 0, okF = 0, n = 0;
        for (int i = 0; i <= 200; i++) for (int j = 0; j <= 200; j++) {
            double t = i / 200.0, d = j / 200.0;
            double dd = d * t;
            int px = (int) ((1.0 - t) * 255.0), py = (int) ((1.0 - dd) * 255.0);
            int idx = py << 8 | px;
            n++;
            if ((GrassColor.get(t, d) & 0xFFFFFF) == (grass[idx] & 0xFFFFFF)) okG++;
            if ((FoliageColor.get(t, d) & 0xFFFFFF) == (foliage[idx] & 0xFFFFFF)) okF++;
        }
        o.addProperty("hypothesis_index=(int)((1-dd*t... )) checks", n);
        o.addProperty("grass_matches", okG); o.addProperty("foliage_matches", okF);
        // Modifiers.
        Random rnd = new Random(7);
        int swampOk = 0, dfOk = 0, swampLow = 0;
        Set<String> swampOut = new TreeSet<>();
        for (int i = 0; i < 20000; i++) {
            double x = rnd.nextInt(40000) - 20000, z = rnd.nextInt(40000) - 20000;
            int c = rnd.nextInt(0x1000000);
            int s = BiomeSpecialEffects.GrassColorModifier.SWAMP.modifyColor(x, z, c) & 0xFFFFFF;
            swampOut.add(String.format("#%06X", s));
            boolean low = Biome.BIOME_INFO_NOISE.get(x * 0.0225, z * 0.0225) < -0.1;
            if (low) swampLow++;
            if (s == (low ? 0x4C763C : 0x6A7039)) swampOk++;
            int dfo = BiomeSpecialEffects.GrassColorModifier.DARK_FOREST.modifyColor(x, z, c) & 0xFFFFFF;
            if (dfo == (((c & 0xFEFEFE) + 0x28340A) >> 1)) dfOk++;
        }
        o.addProperty("swamp_outputs", String.join(",", swampOut));
        o.addProperty("swamp_hypothesis_matches_of_20000", swampOk);
        o.addProperty("swamp_low_noise_share", swampLow / 20000.0);
        o.addProperty("dark_forest_hypothesis_matches_of_20000", dfOk);
        o.addProperty("NONE_modify_keeps_color", BiomeSpecialEffects.GrassColorModifier.NONE.modifyColor(1, 2, 0x123456) == 0x123456);
        // Per biome: water, grass at three positions, foliage, dry foliage, versus the plain colormap lookup.
        JsonObject per = new JsonObject();
        for (var e : lookup.lookupOrThrow(Registries.BIOME).listElements().sorted(Comparator.comparing(h -> h.key().identifier().toString())).toList()) {
            Biome b = e.value();
            JsonObject bo = new JsonObject();
            bo.addProperty("water", String.format("#%06X", b.getWaterColor() & 0xFFFFFF));
            Set<String> gs = new TreeSet<>();
            for (int[] p : new int[][]{{0, 0}, {1000, 1000}, {-5000, 300}, {123, -4567}, {8000, 8000}}) gs.add(String.format("#%06X", b.getGrassColor(p[0], p[1]) & 0xFFFFFF));
            bo.addProperty("grass", String.join(",", gs));
            bo.addProperty("foliage", String.format("#%06X", b.getFoliageColor() & 0xFFFFFF));
            try { bo.addProperty("dry_foliage", String.format("#%06X", b.getDryFoliageColor() & 0xFFFFFF)); } catch (Throwable t) { bo.addProperty("dry_foliage", "error: " + t.getClass().getSimpleName()); }
            float temp = b.getBaseTemperature();
            bo.addProperty("base_temperature", temp);
            per.add(e.key().identifier().getPath(), bo);
        }
        o.add("per_biome", per);
        write("colors.json", o);
    }
}
