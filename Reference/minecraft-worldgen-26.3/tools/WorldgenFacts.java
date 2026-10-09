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
import java.util.List;
import java.util.Arrays;
import java.util.Random;
import java.util.stream.Stream;
import net.minecraft.util.BoundedFloatFunction;
import net.minecraft.util.CubicSpline;
import net.minecraft.util.Interval;
import net.minecraft.util.RandomSource;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.biome.Climate;
import net.minecraft.world.level.biome.MultiNoiseBiomeSourceParameterList;
import net.minecraft.world.level.levelgen.synth.Noise;
import net.minecraft.world.level.levelgen.synth.NormalNoise;

// Dumps Mojang's own terrain-spline values into facts/spline_samples.json by running the 26.3 jar's CubicSpline: each
// terrain spline in the reference's density functions is decoded with the game's codec and sampled at seeded
// coordinates, a quarter of them exactly on a point's location. The coordinates are plain inputs here, so the values
// test spline parsing and evaluation alone. It also dumps the value spread of every top-level noise into
// facts/noise_samples.json: each noise is decoded with the game's codec, created from fixed seeds and sampled at seeded
// positions spread over many periods of its lowest octave. The overworld biome parameter list, which the jar builds
// in code (its JSON only names the preset), goes to facts/biome_parameters.json, and seeded climate points with the
// biome the jar picks for each to facts/biome_lookup_samples.json. Numbers only: no game code is copied.
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
            .append((float) Math.sqrt(squares / values.length - mean * mean)).append(",\"values\":[");
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
