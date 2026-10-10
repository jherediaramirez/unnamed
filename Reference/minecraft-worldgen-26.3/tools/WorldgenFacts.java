import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.mojang.datafixers.util.Pair;
import com.mojang.serialization.Codec;
import com.mojang.serialization.JsonOps;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Arrays;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.TreeMap;
import java.util.stream.Stream;
import net.minecraft.SharedConstants;
import net.minecraft.commands.Commands;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.core.LayeredRegistryAccess;
import net.minecraft.core.RegistryAccess;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.Identifier;
import net.minecraft.server.Bootstrap;
import net.minecraft.server.RegistryLayer;
import net.minecraft.server.WorldLoader;
import net.minecraft.server.packs.repository.PackRepository;
import net.minecraft.server.packs.repository.ServerPacksSource;
import net.minecraft.server.permissions.PermissionSet;
import net.minecraft.util.BoundedFloatFunction;
import net.minecraft.util.CubicSpline;
import net.minecraft.util.Interval;
import net.minecraft.util.RandomSource;
import net.minecraft.util.Util;
import net.minecraft.util.valueproviders.FloatProvider;
import net.minecraft.util.valueproviders.FloatProviders;
import net.minecraft.util.valueproviders.IntProvider;
import net.minecraft.util.valueproviders.IntProviders;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.level.ChunkPos;
import net.minecraft.world.level.LevelHeightAccessor;
import net.minecraft.world.level.WorldDataConfiguration;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.biome.BiomeManager;
import net.minecraft.world.level.biome.BiomeResolver;
import net.minecraft.world.level.biome.Climate;
import net.minecraft.world.level.biome.FeatureSorter;
import net.minecraft.world.level.biome.MultiNoiseBiomeSourceParameterList;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.ChunkGenerator;
import net.minecraft.world.level.chunk.PalettedContainerFactory;
import net.minecraft.world.level.chunk.ProtoChunk;
import net.minecraft.world.level.chunk.UpgradeData;
import net.minecraft.world.level.levelgen.Aquifer;
import net.minecraft.world.level.levelgen.Beardifier;
import net.minecraft.world.level.levelgen.GenerationStep;
import net.minecraft.world.level.levelgen.Heightmap;
import net.minecraft.world.level.levelgen.NoiseChunk;
import net.minecraft.world.level.levelgen.NoiseGeneratorSettings;
import net.minecraft.world.level.levelgen.RandomState;
import net.minecraft.world.level.levelgen.WorldGenerationContext;
import net.minecraft.world.level.levelgen.blending.Blender;
import net.minecraft.world.level.levelgen.densityfunction.DensityFunction;
import net.minecraft.world.level.levelgen.densityfunction.DensitySamplerSet;
import net.minecraft.world.level.levelgen.densityfunction.DensityVolume;
import net.minecraft.world.level.levelgen.heightproviders.HeightProvider;
import net.minecraft.world.level.levelgen.material.rule.MaterialRule;
import net.minecraft.world.level.levelgen.material.rule.OreVeinRule;
import net.minecraft.world.level.levelgen.placement.PlacedFeature;
import net.minecraft.world.level.levelgen.presets.WorldPresets;
import net.minecraft.world.level.levelgen.synth.Noise;
import net.minecraft.world.level.levelgen.synth.NormalNoise;

// Dumps Mojang's own terrain-spline values into facts/spline_samples.json by running the 26.3 jar's CubicSpline: each
// terrain spline in the reference's density functions is decoded with the game's codec and sampled at seeded
// coordinates, a quarter of them exactly on a point's location. The coordinates are plain inputs here, so the values
// test spline parsing and evaluation alone. It also dumps the value spread of every top-level noise into
// facts/noise_samples.json: each noise is decoded with the game's codec, created from fixed seeds and sampled at seeded
// positions spread over many periods of its lowest octave. The overworld biome parameter list, which the jar builds
// in code (its JSON only names the preset), goes to facts/biome_parameters.json, and seeded climate points with the
// biome the jar picks for each to facts/biome_lookup_samples.json. Then it loads the game's registries as a dedicated
// server does and writes three more: every distinct int, float and height provider in the reference's decoration data,
// decoded with the game's codecs and sampled 10^6 times (facts/provider_histograms.json); the per-step feature order
// the jar's FeatureSorter builds over the overworld's biomes (facts/feature_order.json); and the overworld material
// rule run by the jar's own surface system on stone-filled chunks over the ore vein heights, with the vein density
// values at each recorded position (facts/ore_vein_samples.json). Numbers only: no game code is copied.
//
// usage (JDK 25, classpath = server-26.3.jar plus its libraries):
//   javac -cp <classpath> -d <classes> WorldgenFacts.java
//   java -cp <classes>;<classpath> WorldgenFacts <Reference/minecraft-worldgen-26.3>
public class WorldgenFacts {
    static final String[] FUNCTIONS = {"overworld/offset", "overworld/factor", "overworld/jaggedness",
        "overworld_amplified/offset", "overworld_amplified/factor", "overworld_amplified/jaggedness",
        "overworld_large_biomes/offset", "overworld_large_biomes/factor", "overworld_large_biomes/jaggedness"};
    static final int SAMPLES = 512;

    // A spline coordinate that reads its value from the sample's inputs.
    record Coordinate(String name, int index) implements BoundedFloatFunction<float[]> {
        public float apply(float[] inputs) { return inputs[index]; }
        public Interval range() { return Interval.INFINITE; }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        StringBuilder out = new StringBuilder("{\"generated_by\":\"tools/WorldgenFacts.java\",\"samples_per_spline\":")
            .append(SAMPLES).append(",\"splines\":[");
        for (int f = 0; f < FUNCTIONS.length; f++) {
            Path file = root.resolve("data/minecraft/worldgen/density_function/" + FUNCTIONS[f] + ".json");
            List<JsonObject> found = new ArrayList<>();
            findSplines(JsonParser.parseString(Files.readString(file, StandardCharsets.UTF_8)), found);
            if (found.size() != 1) throw new IllegalStateException(file + ": expected one spline, found " + found.size());
            JsonObject spline = found.get(0).getAsJsonObject("spline");
            List<String> names = new ArrayList<>();
            List<List<Float>> locations = new ArrayList<>();
            collect(spline, names, locations);
            Codec<Coordinate> coordinates = Codec.STRING.xmap(n -> new Coordinate(n, names.indexOf(n)), Coordinate::name);
            CubicSpline<Coordinate> decoded = CubicSpline.codec(coordinates).parse(JsonOps.INSTANCE, spline).getOrThrow();

            out.append(f == 0 ? "\n" : ",\n").append("{\"function\":\"minecraft:").append(FUNCTIONS[f]).append("\",\"coordinates\":[");
            for (int c = 0; c < names.size(); c++) out.append(c == 0 ? "\"" : ",\"").append(names.get(c)).append('"');
            out.append("],\"samples\":[");
            Random random = new Random(263_000L + f);
            float[] inputs = new float[names.size()];
            for (int s = 0; s < SAMPLES; s++) {
                for (int c = 0; c < inputs.length; c++) {
                    List<Float> at = locations.get(c);
                    inputs[c] = random.nextInt(4) == 0 ? at.get(random.nextInt(at.size())) : -1.5f + 3f * random.nextFloat();
                }
                float value = CubicSpline.sample(decoded, inputs);
                out.append(s == 0 ? "\n[" : ",\n[");
                for (float input : inputs) out.append(input).append(',');
                out.append(value).append(']');
            }
            out.append("]}");
        }
        out.append("\n]}\n");
        Path target = root.resolve("facts/spline_samples.json");
        Files.createDirectories(target.getParent());
        Files.writeString(target, out.toString(), StandardCharsets.UTF_8);
        System.out.println("WROTE " + FUNCTIONS.length + " splines x " + SAMPLES + " samples: " + target);
        writeNoiseSpread(root);
        writeBiomes(root);
        RegistryAccess access = loadRegistries();
        writeProviderHistograms(root, access);
        writeFeatureOrder(root, access);
        writeOreVeins(root, access);
        // The loader's worker threads would keep the JVM alive.
        System.exit(0);
    }

    // The game's registries, loaded as a dedicated server loads them: the vanilla pack, then the static registry tags.
    static RegistryAccess loadRegistries() throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        PackRepository repository = ServerPacksSource.createVanillaTrustedRepository();
        repository.reload();
        WorldLoader.InitConfig init = new WorldLoader.InitConfig(
            new WorldLoader.PackConfig(repository, WorldDataConfiguration.DEFAULT, false, true),
            Commands.CommandSelection.DEDICATED, PermissionSet.ALL_PERMISSIONS);
        LayeredRegistryAccess<RegistryLayer> layers = WorldLoader.load(init,
            context -> new WorldLoader.DataLoadOutput<Boolean>(Boolean.TRUE, context.datapackDimensions()),
            (WorldLoader.ResultFactory<Boolean, LayeredRegistryAccess<RegistryLayer>>) (resources, reloadable, registries, cookie) -> {
                reloadable.updateComponentsAndStaticRegistryTags();
                return registries;
            }, Util.backgroundExecutor(), Runnable::run).get();
        return layers.compositeAccess();
    }

    static final String[] DECORATION = {"placed_feature", "feature", "block_state_provider", "carver"};
    // The keys that hold providers, as the reader's schema types them (VoxelFeatureReader.cpp).
    static final Set<String> FLOAT_KEYS = Set.of("density", "distance_factor", "floor_level", "height_scale",
        "horizontal_radius_factor", "horizontal_radius_multiplier", "room_vertical_radius_multiplier", "stalactite_bluntness",
        "stalagmite_bluntness", "thickness", "vertical_radius_multiplier", "vertical_rotation", "wetness", "wind_speed");
    static final Set<String> INT_KEYS = Set.of("bend_length", "branch_amount", "branch_count", "branch_end_offset_from_top",
        "branch_horizontal_length", "column_radius", "count", "crown_height", "depth", "extra_branch_length",
        "extra_branch_steps", "foliage_height", "log_length", "offset", "outer_wall_distance", "radius",
        "speleothem_block_layer_thickness", "trunk_height", "trunk_offset_y", "values", "x", "xz_radius", "xz_size",
        "y_size", "z");
    static final int PROVIDER_SAMPLES = 1_000_000, FLOAT_BINS = 64;

    record Provider(String kind, JsonElement json, List<String> uses) {}

    static String providerKind(String key, String parentType) {
        if (FLOAT_KEYS.contains(key)) return "float";
        if (key.equals("height")) return parentType.equals("minecraft:height_range") ? "height" : "int";
        if (key.equals("y")) return parentType.equals("minecraft:offset") ? "int" : "height";
        return INT_KEYS.contains(key) ? "int" : null;
    }

    // Providers in first-seen order, each with the places that use it. A provider is an object with a "type" at a key
    // that holds one; "height" is a height provider only in height_range, "y" an int provider only in offset. Bare
    // numbers are constants and need no histogram.
    static void findProviders(JsonElement json, String parentType, String path, Map<String, Provider> found) {
        if (json.isJsonArray()) {
            JsonArray array = json.getAsJsonArray();
            for (int i = 0; i < array.size(); i++) findProviders(array.get(i), parentType, path + "[" + i + "]", found);
            return;
        }
        if (!json.isJsonObject()) return;
        JsonElement type = json.getAsJsonObject().get("type");
        String own = type != null && type.isJsonPrimitive() ? type.getAsString() : parentType;
        for (var entry : json.getAsJsonObject().entrySet()) {
            String kind = providerKind(entry.getKey(), own), at = path + "." + entry.getKey();
            JsonElement value = entry.getValue();
            if (kind != null && value.isJsonObject() && value.getAsJsonObject().has("type"))
                found.computeIfAbsent(kind + " " + value, k -> new Provider(kind, value, new ArrayList<>())).uses().add(at);
            else
                findProviders(value, own, at, found);
        }
    }

    // Per provider: its JSON, where it is used, the spread of 10^6 samples, and ints (heights included) as counts per
    // value or floats as FLOAT_BINS equal bins over the provider's own range.
    static void writeProviderHistograms(Path root, RegistryAccess access) throws Exception {
        Map<String, Provider> found = new LinkedHashMap<>();
        for (String folder : DECORATION) {
            Path dir = root.resolve("data/minecraft/worldgen/" + folder);
            List<Path> files;
            try (Stream<Path> walk = Files.walk(dir)) { files = walk.filter(p -> p.toString().endsWith(".json")).sorted().toList(); }
            for (Path file : files) {
                String id = folder + "/" + dir.relativize(file).toString().replace('\\', '/').replace(".json", "");
                findProviders(JsonParser.parseString(Files.readString(file, StandardCharsets.UTF_8)), "", id, found);
            }
        }
        WorldGenerationContext context =
            new WorldGenerationContext(WorldPresets.getNormalOverworld(access).generator(), LevelHeightAccessor.create(-64, 384));
        StringBuilder out = new StringBuilder("{\"generated_by\":\"tools/WorldgenFacts.java\",\"samples\":")
            .append(PROVIDER_SAMPLES).append(",\"float_bins\":").append(FLOAT_BINS).append(",\"percentiles\":")
            .append(Arrays.toString(PERCENTILES)).append(",\"height_context\":{\"min_y\":").append(context.getMinGenY())
            .append(",\"height\":").append(context.getGenDepth()).append("},\"providers\":[");
        int index = 0;
        int[] kinds = new int[3];
        for (Provider provider : found.values()) {
            RandomSource random = RandomSource.create(263_100L + index);
            double[] samples = new double[PROVIDER_SAMPLES];
            out.append(index == 0 ? "\n" : ",\n").append("{\"kind\":\"").append(provider.kind()).append("\",\"provider\":")
                .append(provider.json()).append(",\"uses\":[");
            for (int u = 0; u < provider.uses().size(); u++) out.append(u == 0 ? "\"" : ",\"").append(provider.uses().get(u)).append('"');
            out.append("],");
            switch (provider.kind()) {
                case "int" -> {
                    IntProvider decoded = IntProviders.CODEC.parse(JsonOps.INSTANCE, provider.json()).getOrThrow();
                    for (int s = 0; s < samples.length; s++) samples[s] = decoded.sample(random);
                    out.append("\"range\":[").append(decoded.minInclusive()).append(',').append(decoded.maxInclusive()).append("],");
                    kinds[0]++;
                }
                case "height" -> {
                    HeightProvider decoded = HeightProvider.CODEC.parse(JsonOps.INSTANCE, provider.json()).getOrThrow();
                    for (int s = 0; s < samples.length; s++) samples[s] = decoded.sample(random, context);
                    kinds[2]++;
                }
                default -> {
                    FloatProvider decoded = FloatProviders.CODEC.parse(JsonOps.INSTANCE, provider.json()).getOrThrow();
                    for (int s = 0; s < samples.length; s++) samples[s] = decoded.sample(random);
                    float min = decoded.min(), max = decoded.max();
                    int[] bins = new int[FLOAT_BINS];
                    int outside = 0;
                    for (double v : samples) {
                        if (v < min || v > max) outside++;
                        else bins[Math.min(FLOAT_BINS - 1, (int) ((v - min) / (max - min) * FLOAT_BINS))]++;
                    }
                    out.append("\"range\":[").append(min).append(',').append(max).append("],\"bins\":")
                        .append(Arrays.toString(bins).replace(" ", "")).append(",\"outside\":").append(outside).append(',');
                    kinds[1]++;
                }
            }
            if (!provider.kind().equals("float")) {
                Map<Integer, Integer> counts = new TreeMap<>();
                for (double v : samples) counts.merge((int) v, 1, Integer::sum);
                out.append("\"counts\":[");
                boolean first = true;
                for (var count : counts.entrySet()) {
                    out.append(first ? "[" : ",[").append(count.getKey()).append(',').append(count.getValue()).append(']');
                    first = false;
                }
                out.append("],");
            }
            out.append("\"spread\":");
            spread(samples, out);
            out.append('}');
            index++;
        }
        out.append("\n]}\n");
        Path target = root.resolve("facts/provider_histograms.json");
        Files.writeString(target, out.toString(), StandardCharsets.UTF_8);
        System.out.println("WROTE " + index + " providers (" + kinds[0] + " int, " + kinds[1] + " float, " + kinds[2]
            + " height) x " + PROVIDER_SAMPLES + " samples: " + target);
    }

    // Vanilla's per-step order: FeatureSorter.buildFeaturesPerStep over the normal preset's overworld biome source, in
    // the order possibleBiomes() gives them, with each biome's lists as the overworld generator reads them.
    static void writeFeatureOrder(Path root, RegistryAccess access) throws Exception {
        ChunkGenerator generator = WorldPresets.getNormalOverworld(access).generator();
        List<Holder<Biome>> biomes = List.copyOf(generator.getBiomeSource().possibleBiomes());
        List<FeatureSorter.StepFeatureData> steps =
            FeatureSorter.buildFeaturesPerStep(biomes, biome -> generator.getBiomeGenerationSettings(biome).features(), true);
        Map<PlacedFeature, String> ids = new IdentityHashMap<>();
        access.lookupOrThrow(Registries.PLACED_FEATURE).listElements()
            .forEach(holder -> ids.put(holder.value(), holder.key().identifier().toString()));
        StringBuilder out = new StringBuilder("{\"generated_by\":\"tools/WorldgenFacts.java\",\"preset\":\"minecraft:normal\",")
            .append("\"biomes\":[");
        for (int b = 0; b < biomes.size(); b++)
            out.append(b == 0 ? "\n\"" : ",\n\"").append(biomes.get(b).unwrapKey().orElseThrow().identifier()).append('"');
        out.append("],\"steps\":[");
        int total = 0;
        for (int s = 0; s < steps.size(); s++) {
            out.append(s == 0 ? "\n{\"step\":\"" : ",\n{\"step\":\"").append(GenerationStep.Decoration.values()[s].getSerializedName())
                .append("\",\"features\":[");
            List<PlacedFeature> features = steps.get(s).features();
            for (int f = 0; f < features.size(); f++) {
                String id = ids.get(features.get(f));
                if (id == null) throw new IllegalStateException("step " + s + ": a placed feature without a registry id");
                out.append(f == 0 ? "\"" : ",\"").append(id).append('"');
            }
            out.append("]}");
            total += features.size();
        }
        out.append("\n]}\n");
        Path target = root.resolve("facts/feature_order.json");
        Files.writeString(target, out.toString(), StandardCharsets.UTF_8);
        System.out.println("WROTE " + steps.size() + " steps, " + total + " placed features over " + biomes.size() + " biomes: " + target);
    }

    static final long[] VEIN_SEEDS = {0L, 263L, -4_000_000_007L};
    static final String[] VEIN_FUNCTIONS = {"toggle", "mask", "copper_density", "iron_density", "richness", "gap"};
    static final int VEIN_MIN_Y = -64, VEIN_MAX_Y = 56, VEIN_SCAN = 32, VEIN_SAMPLE_ONE_IN = 64;
    static final LevelHeightAccessor OVERWORLD_HEIGHT = LevelHeightAccessor.create(-64, 384);

    static <T> T registered(RegistryAccess access, ResourceKey<? extends net.minecraft.core.Registry<T>> registry, String id) {
        return access.lookupOrThrow(registry).getOrThrow(ResourceKey.create(registry, Identifier.withDefaultNamespace(id))).value();
    }

    // The overworld material rule (bedrock floor, the two ore veins, surface, underground) run by the jar's surface
    // system on chunks of stone from VEIN_MIN_Y to VEIN_MAX_Y, plains everywhere. Per seed, the scan picks the first
    // chunk (row-major over VEIN_SCAN^2 from the origin) whose centre has toggle >= 0.5 at Y 25 (copper's side) and, of
    // the others, the first with toggle <= -0.5 at Y -34 (iron's). Recorded: block counts per Y, every vein block and one in
    // VEIN_SAMPLE_ONE_IN of the other positions with the six vein densities as the chunk's own samplers give them, and
    // how often a vein block sits where the type's density is above 0.
    static void writeOreVeins(Path root, RegistryAccess access) throws Exception {
        Holder<NoiseGeneratorSettings> settings =
            access.lookupOrThrow(Registries.NOISE_SETTINGS).getOrThrow(NoiseGeneratorSettings.OVERWORLD);
        ChunkGenerator generator = WorldPresets.getNormalOverworld(access).generator();
        WorldGenerationContext context = new WorldGenerationContext(generator, OVERWORLD_HEIGHT);
        MaterialRule rule = registered(access, Registries.MATERIAL_RULE, "overworld");
        OreVeinRule[] veins = {(OreVeinRule) registered(access, Registries.MATERIAL_RULE, "overworld/copper_ore_vein"),
            (OreVeinRule) registered(access, Registries.MATERIAL_RULE, "overworld/iron_ore_vein")};
        OreVeinRule.VeinType[] types = {OreVeinRule.VeinType.COPPER, OreVeinRule.VeinType.IRON};
        DensityFunction[] functions = new DensityFunction[VEIN_FUNCTIONS.length];
        for (int f = 0; f < functions.length; f++)
            functions[f] = registered(access, Registries.DENSITY_FUNCTION, "overworld/ore_vein/" + VEIN_FUNCTIONS[f]);
        Holder<Biome> plains = access.lookupOrThrow(Registries.BIOME)
            .getOrThrow(ResourceKey.create(Registries.BIOME, Identifier.withDefaultNamespace("plains")));
        BlockState stone = Blocks.STONE.defaultBlockState(), water = Blocks.WATER.defaultBlockState();

        StringBuilder out = new StringBuilder("{\"generated_by\":\"tools/WorldgenFacts.java\",\"rule\":\"minecraft:overworld\",")
            .append("\"fill\":\"minecraft:stone\",\"biome\":\"minecraft:plains\",\"y_range\":[").append(VEIN_MIN_Y).append(',')
            .append(VEIN_MAX_Y).append("],\"constants\":{\"VEININESS_THRESHOLD\":").append(OreVeinRule.VEININESS_THRESHOLD)
            .append(",\"EDGE_ROUNDOFF_BEGIN\":").append(OreVeinRule.EDGE_ROUNDOFF_BEGIN).append(",\"MAX_EDGE_ROUNDOFF\":")
            .append(OreVeinRule.MAX_EDGE_ROUNDOFF).append(",\"VEIN_SOLIDNESS\":").append(OreVeinRule.VEIN_SOLIDNESS)
            .append(",\"MIN_RICHNESS\":").append(OreVeinRule.MIN_RICHNESS).append(",\"MAX_RICHNESS\":").append(OreVeinRule.MAX_RICHNESS)
            .append(",\"MAX_RICHNESS_THRESHOLD\":").append(OreVeinRule.MAX_RICHNESS_THRESHOLD)
            .append(",\"SKIP_ORE_IF_GAP_NOISE_IS_BELOW\":").append(OreVeinRule.SKIP_ORE_IF_GAP_NOISE_IS_BELOW).append("},\"veins\":[");
        List<Set<BlockState>> veinBlocks = new ArrayList<>();
        for (int v = 0; v < veins.length; v++) {
            veinBlocks.add(Set.of(veins[v].oreBlock(), veins[v].rawOreBlock(), veins[v].fillerBlock()));
            out.append(v == 0 ? "\n{\"type\":\"" : ",\n{\"type\":\"").append(types[v].name().toLowerCase()).append("\",\"min_y\":")
                .append(types[v].minY).append(",\"max_y\":").append(types[v].maxY).append(",\"ore\":\"")
                .append(name(veins[v].oreBlock())).append("\",\"raw_ore\":\"").append(name(veins[v].rawOreBlock()))
                .append("\",\"filler\":\"").append(name(veins[v].fillerBlock())).append("\",\"raw_ore_chance\":")
                .append(veins[v].rawOreChance()).append('}');
        }
        out.append("],\"densities\":[");
        for (int f = 0; f < functions.length; f++)
            out.append(f == 0 ? "\"" : ",\"").append("minecraft:overworld/ore_vein/").append(VEIN_FUNCTIONS[f]).append('"');
        out.append("],\"columns\":[\"x\",\"y\",\"z\",\"block\"");
        for (String function : VEIN_FUNCTIONS) out.append(",\"").append(function).append('"');
        out.append("],\"agreement_columns\":[\"vein block, density > 0\",\"no vein block, density > 0\",")
            .append("\"vein block, density <= 0\"],\"seeds\":[");

        List<String> blocks = new ArrayList<>(List.of(name(stone)));
        float[] uncachedDifference = new float[functions.length];
        for (int s = 0; s < VEIN_SEEDS.length; s++) {
            long seed = VEIN_SEEDS[s];
            RandomState state = RandomState.create(access.lookupOrThrow(Registries.NOISE), seed, settings.value());
            int[][] chunks = new int[2][];
            for (int i = 0; i < VEIN_SCAN * VEIN_SCAN && (chunks[0] == null || chunks[1] == null); i++) {
                int cx = i % VEIN_SCAN, cz = i / VEIN_SCAN;
                if (chunks[0] == null && state.sampleBlockValueUncached(functions[0], cx * 16 + 8, 25, cz * 16 + 8) >= 0.5f)
                    chunks[0] = new int[] {cx, cz};
                else if (chunks[1] == null && state.sampleBlockValueUncached(functions[0], cx * 16 + 8, -34, cz * 16 + 8) <= -0.5f)
                    chunks[1] = new int[] {cx, cz};
            }
            if (chunks[0] == null || chunks[1] == null) throw new IllegalStateException("seed " + seed + ": no vein chunks found");
            Map<String, int[]> counts = new TreeMap<>();
            int[][] agreement = new int[2][3];
            StringBuilder samples = new StringBuilder();
            Random pick = new Random(263_200L + s);
            for (int[] at : chunks) {
                ProtoChunk chunk = new ProtoChunk(new ChunkPos(at[0], at[1]), UpgradeData.EMPTY, OVERWORLD_HEIGHT,
                    PalettedContainerFactory.create(access), null);
                BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
                for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) for (int y = VEIN_MIN_Y; y <= VEIN_MAX_Y; y++)
                    chunk.setBlockState(pos.set(at[0] * 16 + x, y, at[1] * 16 + z), stone, 0);
                Heightmap.primeHeightmaps(chunk, EnumSet.of(Heightmap.Types.WORLD_SURFACE_WG, Heightmap.Types.OCEAN_FLOOR_WG));
                BiomeResolver resolver = (qx, qy, qz) -> plains;
                chunk.fillBiomesFromNoise(resolver);
                BiomeManager manager = new BiomeManager(resolver, BiomeManager.obfuscateSeed(seed));
                DensityVolume volume = new DensityVolume(16, 384, 16, at[0] * 16, -64, at[1] * 16);
                try (NoiseChunk noise = new NoiseChunk(state, Beardifier.EMPTY, settings.value(),
                        (x, y, z) -> new Aquifer.FluidStatus(63, water), Blender.empty(), volume)) {
                    state.surfaceSystem().buildSurface(state, manager, context, chunk, noise, rule, Set.of(plains));
                    DensitySamplerSet samplers = noise.cachingSamplers();
                    float[] values = new float[functions.length];
                    for (int y = VEIN_MIN_Y; y <= VEIN_MAX_Y; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
                        int bx = at[0] * 16 + x, bz = at[1] * 16 + z;
                        BlockState block = chunk.getBlockState(pos.set(bx, y, bz));
                        String blockName = name(block);
                        counts.computeIfAbsent(blockName, k -> new int[VEIN_MAX_Y - VEIN_MIN_Y + 1])[y - VEIN_MIN_Y]++;
                        boolean vein = veinBlocks.get(0).contains(block) || veinBlocks.get(1).contains(block);
                        boolean sampled = pick.nextInt(VEIN_SAMPLE_ONE_IN) == 0;
                        for (int f = 0; f < functions.length; f++) values[f] = samplers.sampleValue(functions[f], bx, y, bz);
                        for (int v = 0; v < 2; v++) {
                            boolean dense = values[2 + v] > 0, placed = veinBlocks.get(v).contains(block);
                            if (dense || placed) agreement[v][placed ? (dense ? 0 : 2) : 1]++;
                        }
                        if (!vein && !sampled) continue;
                        if (!blocks.contains(blockName)) blocks.add(blockName);
                        samples.append(samples.isEmpty() ? "\n[" : ",\n[").append(bx).append(',').append(y).append(',').append(bz)
                            .append(',').append(blocks.indexOf(blockName));
                        for (int f = 0; f < functions.length; f++) {
                            samples.append(',').append(values[f]);
                            uncachedDifference[f] = Math.max(uncachedDifference[f],
                                Math.abs(values[f] - state.sampleBlockValueUncached(functions[f], bx, y, bz)));
                        }
                        samples.append(']');
                    }
                }
            }
            out.append(s == 0 ? "\n{\"seed\":" : ",\n{\"seed\":").append(seed).append(",\"chunks\":[[").append(chunks[0][0]).append(',')
                .append(chunks[0][1]).append("],[").append(chunks[1][0]).append(',').append(chunks[1][1]).append("]],\"agreement\":{");
            for (int v = 0; v < 2; v++)
                out.append(v == 0 ? "\"" : ",\"").append(types[v].name().toLowerCase()).append("\":")
                    .append(Arrays.toString(agreement[v]).replace(" ", ""));
            out.append("},\"counts_by_y\":{");
            boolean first = true;
            for (var count : counts.entrySet()) {
                out.append(first ? "\n\"" : ",\n\"").append(count.getKey()).append("\":").append(Arrays.toString(count.getValue()).replace(" ", ""));
                first = false;
            }
            out.append("},\"samples\":[").append(samples).append("]}");
            System.out.println("seed " + seed + ": chunks " + Arrays.toString(chunks[0]) + " " + Arrays.toString(chunks[1])
                + ", copper agreement " + Arrays.toString(agreement[0]) + ", iron agreement " + Arrays.toString(agreement[1]));
        }
        out.append("\n],\"blocks\":[");
        for (int b = 0; b < blocks.size(); b++) out.append(b == 0 ? "\"" : ",\"").append(blocks.get(b)).append('"');
        out.append("],\"uncached_max_difference\":").append(Arrays.toString(uncachedDifference).replace(" ", "")).append("}\n");
        Path target = root.resolve("facts/ore_vein_samples.json");
        Files.writeString(target, out.toString(), StandardCharsets.UTF_8);
        System.out.println("WROTE ore vein samples for " + VEIN_SEEDS.length + " seeds: " + target);
    }

    static String name(BlockState state) {
        return BuiltInRegistries.BLOCK.getKey(state.getBlock()).toString();
    }

    static final int LOOKUP_UNIFORM = 4096, LOOKUP_EDGES = 1024;

    static Climate.Parameter parameter(Climate.ParameterPoint point, int axis) {
        return switch (axis) {
            case 0 -> point.temperature();
            case 1 -> point.humidity();
            case 2 -> point.continentalness();
            case 3 -> point.erosion();
            case 4 -> point.depth();
            default -> point.weirdness();
        };
    }

    // The points in list order (the order decides ties), each as the biome's index in "biomes", then min and max of
    // the six climate parameters and the offset, quantized as the game stores them. The samples are target points in
    // the same units with the biome the jar's indexed search picks and the one its brute-force scan picks (the first
    // best point in list order). Half sit at depth 0 (the surface); the edge samples put one to three parameters on a
    // random point's bounds, give or take one unit, where inclusive and exclusive bounds part.
    static void writeBiomes(Path root) throws Exception {
        Climate.ParameterList<ResourceKey<Biome>> list =
            MultiNoiseBiomeSourceParameterList.knownPresets().get(MultiNoiseBiomeSourceParameterList.Preset.OVERWORLD);
        List<Pair<Climate.ParameterPoint, ResourceKey<Biome>>> values = list.values();
        List<String> biomes = values.stream().map(p -> p.getSecond().identifier().toString()).distinct().sorted().toList();
        StringBuilder out = new StringBuilder("{\"generated_by\":\"tools/WorldgenFacts.java\",")
            .append("\"preset\":\"minecraft:overworld\",\"quantization\":10000,")
            .append("\"columns\":[\"biome\",\"temperature\",\"humidity\",\"continentalness\",\"erosion\",\"depth\",")
            .append("\"weirdness\",\"offset\"],\"biomes\":[");
        for (int b = 0; b < biomes.size(); b++) out.append(b == 0 ? "\n\"" : ",\n\"").append(biomes.get(b)).append('"');
        out.append("],\"points\":[");
        for (int i = 0; i < values.size(); i++) {
            Climate.ParameterPoint point = values.get(i).getFirst();
            out.append(i == 0 ? "\n[" : ",\n[").append(biomes.indexOf(values.get(i).getSecond().identifier().toString()));
            for (int axis = 0; axis < 6; axis++)
                out.append(',').append(parameter(point, axis).min()).append(',').append(parameter(point, axis).max());
            out.append(',').append(point.offset()).append(']');
        }
        out.append("\n]}\n");
        Path target = root.resolve("facts/biome_parameters.json");
        Files.writeString(target, out.toString(), StandardCharsets.UTF_8);
        System.out.println("WROTE " + values.size() + " biome points over " + biomes.size() + " biomes: " + target);

        out = new StringBuilder("{\"generated_by\":\"tools/WorldgenFacts.java\",\"preset\":\"minecraft:overworld\",")
            .append("\"columns\":[\"temperature\",\"humidity\",\"continentalness\",\"erosion\",\"depth\",\"weirdness\",")
            .append("\"brute_force\",\"indexed\"],\"biomes\":\"the biomes list of biome_parameters.json\",\"samples\":[");
        Random random = new Random(263_005L);
        int disagree = 0;
        for (int s = 0; s < LOOKUP_UNIFORM + LOOKUP_EDGES; s++) {
            long[] t = new long[6];
            for (int axis = 0; axis < 6; axis++) t[axis] = random.nextInt(30001) - 15000;
            t[4] = random.nextBoolean() ? 0 : random.nextInt(30001) - 10000;
            if (s >= LOOKUP_UNIFORM) {
                Climate.ParameterPoint point = values.get(random.nextInt(values.size())).getFirst();
                for (int k = 1 + random.nextInt(3); k > 0; k--) {
                    int axis = random.nextInt(6);
                    Climate.Parameter range = parameter(point, axis);
                    t[axis] = (random.nextBoolean() ? range.min() : range.max()) + random.nextInt(3) - 1;
                }
            }
            Climate.TargetPoint point = new Climate.TargetPoint(t[0], t[1], t[2], t[3], t[4], t[5]);
            int brute = biomes.indexOf(list.findValueBruteForce(point).identifier().toString());
            int indexed = biomes.indexOf(list.findValue(point).identifier().toString());
            if (brute != indexed) disagree++;
            out.append(s == 0 ? "\n[" : ",\n[");
            for (long v : t) out.append(v).append(',');
            out.append(brute).append(',').append(indexed).append(']');
        }
        out.append("\n]}\n");
        target = root.resolve("facts/biome_lookup_samples.json");
        Files.writeString(target, out.toString(), StandardCharsets.UTF_8);
        System.out.println("WROTE " + (LOOKUP_UNIFORM + LOOKUP_EDGES) + " lookup samples (" + disagree
            + " where the indexed search and brute force differ): " + target);
    }

    static final long[] NOISE_SEEDS = {0L, 263L, -4_000_000_007L, 1_234_567_890_123L};
    static final int NOISE_SAMPLES_PER_SEED = 16384;
    static final double[] PERCENTILES = {0.001, 0.01, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99, 0.999};

    // Per noise: its declared range, and the spread of get(x, 0, z) and get(x, y, z) in the noise's own input units.
    static void writeNoiseSpread(Path root) throws Exception {
        Path folder = root.resolve("data/minecraft/worldgen/noise");
        List<Path> files;
        try (Stream<Path> list = Files.list(folder)) { files = list.filter(p -> p.toString().endsWith(".json")).sorted().toList(); }
        StringBuilder out = new StringBuilder("{\"generated_by\":\"tools/WorldgenFacts.java\",\"samples_per_plane\":")
            .append(NOISE_SEEDS.length * NOISE_SAMPLES_PER_SEED).append(",\"percentiles\":").append(Arrays.toString(PERCENTILES))
            .append(",\"noises\":[");
        for (int f = 0; f < files.size(); f++) {
            String name = files.get(f).getFileName().toString().replace(".json", "");
            JsonObject json = JsonParser.parseString(Files.readString(files.get(f), StandardCharsets.UTF_8)).getAsJsonObject();
            NormalNoise parameters = NormalNoise.DIRECT_CODEC.parse(JsonOps.INSTANCE, json).getOrThrow();
            // Positions span 64 periods of the lowest octave.
            double extent = 64 * Math.pow(2, -json.get("base_octave").getAsInt());
            double[] plane = new double[NOISE_SEEDS.length * NOISE_SAMPLES_PER_SEED];
            double[] volume = new double[plane.length];
            Interval range = null;
            for (int s = 0; s < NOISE_SEEDS.length; s++) {
                Noise noise = parameters.create(RandomSource.create(NOISE_SEEDS[s]));
                range = noise.range();
                Random random = new Random(26_300L + 31L * f + s);
                for (int i = 0; i < NOISE_SAMPLES_PER_SEED; i++) {
                    double x = (random.nextDouble() * 2 - 1) * extent, y = (random.nextDouble() * 2 - 1) * extent;
                    double z = (random.nextDouble() * 2 - 1) * extent;
                    plane[s * NOISE_SAMPLES_PER_SEED + i] = noise.get(x, 0, z);
                    volume[s * NOISE_SAMPLES_PER_SEED + i] = noise.get(x, y, z);
                }
            }
            out.append(f == 0 ? "\n" : ",\n").append("{\"noise\":\"minecraft:").append(name).append("\",\"range\":[")
                .append(range.min()).append(',').append(range.max()).append("],\"xz\":");
            spread(plane, out);
            out.append(",\"xyz\":");
            spread(volume, out);
            out.append('}');
        }
        out.append("\n]}\n");
        Path target = root.resolve("facts/noise_samples.json");
        Files.writeString(target, out.toString(), StandardCharsets.UTF_8);
        System.out.println("WROTE " + files.size() + " noises: " + target);
    }

    // Mean, standard deviation, and the percentiles in order.
    static void spread(double[] values, StringBuilder out) {
        double sum = 0, squares = 0;
        for (double v : values) { sum += v; squares += v * v; }
        double mean = sum / values.length;
        out.append("{\"mean\":").append((float) mean).append(",\"std\":")
            .append((float) Math.sqrt(Math.max(0, squares / values.length - mean * mean))).append(",\"values\":[");
        double[] sorted = values.clone();
        Arrays.sort(sorted);
        for (int p = 0; p < PERCENTILES.length; p++)
            out.append(p == 0 ? "" : ",").append((float) sorted[(int) Math.floor(PERCENTILES[p] * (sorted.length - 1))]);
        out.append("]}");
    }

    // Spline density functions anywhere in a file; nested splines live inside their "spline" object and are not listed.
    static void findSplines(JsonElement json, List<JsonObject> found) {
        if (json.isJsonObject()) {
            JsonObject object = json.getAsJsonObject();
            JsonElement type = object.get("type");
            if (type != null && type.isJsonPrimitive() && type.getAsString().equals("minecraft:spline")) {
                found.add(object);
                return;
            }
            for (var entry : object.entrySet()) findSplines(entry.getValue(), found);
        } else if (json.isJsonArray()) {
            for (JsonElement element : json.getAsJsonArray()) findSplines(element, found);
        }
    }

    // Coordinates in first-use order, depth first, with every point location seen on each.
    static void collect(JsonObject spline, List<String> names, List<List<Float>> locations) {
        String name = spline.get("coordinate").getAsString();
        if (!names.contains(name)) {
            names.add(name);
            locations.add(new ArrayList<>());
        }
        List<Float> at = locations.get(names.indexOf(name));
        for (JsonElement element : spline.getAsJsonArray("points")) {
            JsonObject point = element.getAsJsonObject();
            at.add(point.get("location").getAsFloat());
            if (point.get("value").isJsonObject()) collect(point.getAsJsonObject("value"), names, locations);
        }
    }
}
