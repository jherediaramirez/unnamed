// Black-box facts extractor (survival 20a): writes facts/entity_models.json, deterministic (sorted layers, indent 1).
// Usage: javac -cp <client jar + libraries> -d <classes> EntityModelFacts.java; java -cp <classes;client jar + libraries> EntityModelFacts <out.json>
// Black-box probe: calls only the 26.3 client jar's public API
// (LayerDefinitions.createRoots, LayerDefinition.bakeRoot, ModelPart public fields/getChild/visit,
// ModelPart$Cube public fields, Polygon/Vertex record accessors) and prints the result as JSON.
import net.minecraft.client.model.geom.*;
import net.minecraft.client.model.geom.builders.LayerDefinition;
import com.mojang.blaze3d.vertex.PoseStack;
import java.util.*;

public class EntityModelFacts {
    static final List<String> WANT = List.of("cow","pig","sheep","sheep_wool","sheep_wool_undercoat","chicken","zombie","skeleton","creeper","spider");
    // Survival T2 19c: models written after WANT's sorted layers (each model's layers sorted), so no earlier layer moves.
    // Append only.
    static final List<String> APPEND = List.of("arrow");
    static String f(float v){ return Float.toString(v); }
    public static void main(String[] a) {
        Map<ModelLayerLocation, LayerDefinition> roots = LayerDefinitions.createRoots();
        StringBuilder sb = new StringBuilder("{\n");
        boolean first = true;
        TreeMap<String, ModelLayerLocation> keys = new TreeMap<>();
        for (ModelLayerLocation l : roots.keySet()) keys.put(l.model().getPath() + "#" + l.layer(), l);
        List<Map.Entry<String, ModelLayerLocation>> order = new ArrayList<>();
        for (var e : keys.entrySet()) if (WANT.contains(e.getValue().model().getPath())) order.add(e);
        for (String m : APPEND) for (var e : keys.entrySet()) if (e.getValue().model().getPath().equals(m)) order.add(e);
        for (var e : order) {
            ModelLayerLocation l = e.getValue();
            if (l.layer().contains("armor") || l.layer().contains("baby")) continue;
            ModelPart root = roots.get(l).bakeRoot();
            if (!first) sb.append(",\n"); first = false;
            sb.append("\"").append(e.getKey()).append("\": {\"parts\": [");
            List<String> parts = new ArrayList<>();
            walk(root, "", parts);
            sb.append(String.join(",", parts)).append("], \"cubes\": [");
            List<String> cubes = new ArrayList<>();
            root.visit(new PoseStack(), (pose, path, idx, cube) -> {
                StringBuilder c = new StringBuilder();
                c.append("{\"path\":\"").append(path).append("\",\"i\":").append(idx)
                 .append(",\"min\":[").append(f(cube.minX)).append(",").append(f(cube.minY)).append(",").append(f(cube.minZ))
                 .append("],\"max\":[").append(f(cube.maxX)).append(",").append(f(cube.maxY)).append(",").append(f(cube.maxZ)).append("],\"faces\":[");
                List<String> fs = new ArrayList<>();
                for (ModelPart.Polygon p : cube.polygons) {
                    StringBuilder ps = new StringBuilder("{\"n\":[" + f(p.normal().x()) + "," + f(p.normal().y()) + "," + f(p.normal().z()) + "],\"v\":[");
                    List<String> vs = new ArrayList<>();
                    for (ModelPart.Vertex v : p.vertices()) vs.add("[" + f(v.x()) + "," + f(v.y()) + "," + f(v.z()) + "," + f(v.u()) + "," + f(v.v()) + "]");
                    fs.add(ps.append(String.join(",", vs)).append("]}").toString());
                }
                c.append(String.join(",", fs)).append("]}");
                cubes.add(c.toString());
            });
            sb.append(String.join(",\n", cubes)).append("]}");
        }
        sb.append("\n}\n");
        try { java.nio.file.Files.writeString(java.nio.file.Path.of(a[0]), pretty(sb.toString().strip()) + "\n"); } catch (java.io.IOException e) { throw new RuntimeException(e); }
        System.err.println("layers total " + roots.size());
    }
    /** Re-indents compact JSON with one space per level; number and string tokens are kept verbatim. */
    static String pretty(String j) {
        StringBuilder o = new StringBuilder(); int d = 0; boolean str = false;
        for (int i = 0; i < j.length(); i++) {
            char c = j.charAt(i);
            if (str) { o.append(c); if (c == '\\') o.append(j.charAt(++i)); else if (c == '"') str = false; continue; }
            if (c == '\n' || c == ' ') continue;
            switch (c) {
                case '"' -> { str = true; o.append(c); }
                case '{', '[' -> { if (j.charAt(i + 1) == (c == '{' ? '}' : ']')) { o.append(c).append(j.charAt(++i)); } else { o.append(c).append('\n').append(" ".repeat(++d)); } }
                case '}', ']' -> o.append('\n').append(" ".repeat(--d)).append(c);
                case ',' -> o.append(',').append('\n').append(" ".repeat(d));
                case ':' -> o.append(": ");
                default -> o.append(c);
            }
        }
        return o.toString();
    }
    static void walk(ModelPart p, String path, List<String> out) {
        PartPose pose = p.getInitialPose();
        out.add("{\"path\":\"" + path + "\",\"pivot\":[" + f(p.x) + "," + f(p.y) + "," + f(p.z) + "],\"rot\":[" + f(p.xRot) + "," + f(p.yRot) + "," + f(p.zRot) + "],\"scale\":[" + f(p.xScale) + "," + f(p.yScale) + "," + f(p.zScale) + "]}");
        // children: probe names via createPartLookup is name->part; use getAllParts order is unnamed, so test known names
        for (String n : NAMES) if (p.hasChild(n)) walk(p.getChild(n), path.isEmpty() ? n : path + "/" + n, out);
    }
    static final String[] NAMES = {"head","hat","body","right_arm","left_arm","right_leg","left_leg","right_hind_leg","left_hind_leg","right_front_leg","left_front_leg","horns","right_horn","left_horn","snout","beak","red_thing","right_wing","left_wing","body0","body1","right_middle_hind_leg","left_middle_hind_leg","right_middle_front_leg","left_middle_front_leg","jacket","waist","left_sleeve","right_sleeve","left_pants","right_pants",
        // Survival T2 19c: the arrow's parts (appended; no earlier model has them).
        "back","cross_1","cross_2"};
}
