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
import java.util.Random;
import net.minecraft.util.BoundedFloatFunction;
import net.minecraft.util.CubicSpline;
import net.minecraft.util.Interval;

// Dumps Mojang's own terrain-spline values into facts/spline_samples.json by running the 26.3 jar's CubicSpline: each
// terrain spline in the reference's density functions is decoded with the game's codec and sampled at seeded
// coordinates, a quarter of them exactly on a point's location. The coordinates are plain inputs here, so the values
// test spline parsing and evaluation alone. Numbers only: no game code is copied.
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
