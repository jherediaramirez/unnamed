import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
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
import net.minecraft.world.level.levelgen.synth.Noise;
import net.minecraft.world.level.levelgen.synth.NormalNoise;

// Dumps Mojang's own terrain-spline values into facts/spline_samples.json by running the 26.3 jar's CubicSpline: each
// terrain spline in the reference's density functions is decoded with the game's codec and sampled at seeded
// coordinates, a quarter of them exactly on a point's location. The coordinates are plain inputs here, so the values
// test spline parsing and evaluation alone. It also dumps the value spread of every top-level noise into
// facts/noise_samples.json: each noise is decoded with the game's codec, created from fixed seeds and sampled at seeded
// positions spread over many periods of its lowest octave. Numbers only: no game code is copied.
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
