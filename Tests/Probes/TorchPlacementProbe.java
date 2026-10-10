import com.mojang.authlib.GameProfile;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.TimeUnit;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.gametest.framework.GameTestServer;
import net.minecraft.server.Bootstrap;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ClientInformation;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.packs.repository.PackRepository;
import net.minecraft.server.packs.repository.ServerPacksSource;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.item.context.BlockPlaceContext;
import net.minecraft.world.item.context.UseOnContext;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.Property;
import net.minecraft.world.level.storage.LevelStorageSource;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.Vec3;

// Black-box measurement of vanilla 26.3 torch placement through the public API only: an in-process GameTestServer
// (selection matching no test), a connection-less ServerPlayer whose look is set per trial, and ItemStack.useOn on a
// test cell high in the air. Usage: java -cp <server jar + libraries> TorchPlacementProbe <work dir> <output json>
public class TorchPlacementProbe {
    static final Direction[] FACES = {Direction.DOWN, Direction.UP, Direction.NORTH, Direction.SOUTH, Direction.WEST, Direction.EAST};
    // Support bits for case A: below, north, south, west, east of D. Case B uses the last four only.
    static final Direction[] SUPPORT_DIRS = {Direction.DOWN, Direction.NORTH, Direction.SOUTH, Direction.WEST, Direction.EAST};
    static final char[] SUPPORT_CHARS = {'B', 'N', 'S', 'W', 'E'};
    static final int YAWS = 24, PITCHES = 13;
    static final int SETUP_FLAGS = Block.UPDATE_CLIENTS | Block.UPDATE_KNOWN_SHAPE;

    static char dirChar(Direction d) {
        switch (d) {
            case DOWN: return 'D'; case UP: return 'U'; case NORTH: return 'N';
            case SOUTH: return 'S'; case WEST: return 'W'; default: return 'E';
        }
    }
    static String order(Direction[] ds) { StringBuilder b = new StringBuilder(); for (Direction d : ds) b.append(dirChar(d)); return b.toString(); }
    static float yawOf(int look) { return 15f * (look / PITCHES); }
    static float pitchOf(int look) { return -90f + 15f * (look % PITCHES); }

    static String stateString(BlockState s) {
        StringBuilder b = new StringBuilder(String.valueOf(BuiltInRegistries.BLOCK.getKey(s.getBlock())));
        List<Property<?>> props = new ArrayList<>(s.getProperties());
        props.sort(Comparator.comparing(Property::getName));
        if (!props.isEmpty()) {
            b.append('[');
            for (int i = 0; i < props.size(); i++) {
                if (i > 0) b.append(',');
                b.append(props.get(i).getName()).append('=').append(valueName(s, props.get(i)));
            }
            b.append(']');
        }
        return b.toString();
    }
    @SuppressWarnings({"unchecked", "rawtypes"})
    static String valueName(BlockState s, Property p) { return p.getName(s.getValue(p)); }

    static String q(String s) { return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\""; }

    public static void main(String[] args) throws Exception {
        Path work = Path.of(args[0]).toAbsolutePath();
        Path out = Path.of(args[1]).toAbsolutePath();
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        Path saves = work.resolve("level");
        deleteTree(saves);
        Files.createDirectories(saves);
        LevelStorageSource source = LevelStorageSource.createDefault(saves);
        LevelStorageSource.LevelStorageAccess access = source.createAccess("probe");
        PackRepository repo = ServerPacksSource.createVanillaTrustedRepository();
        GameTestServer server = MinecraftServer.spin(thread -> GameTestServer.create(thread, access, repo,
            Optional.of("probe_no_such_namespace:probe_no_such_test"), false, 1));
        String json;
        try {
            json = server.submit(() -> {
                try { return runProbe(server); }
                catch (RuntimeException e) { e.printStackTrace(); throw e; }
            }).get(30, TimeUnit.MINUTES);
        } finally {
            server.halt(false);
        }
        Files.createDirectories(out.getParent());
        Files.write(out, json.getBytes(StandardCharsets.UTF_8));
        System.out.println("PROBE DONE " + out);
        System.out.flush();
        Runtime.getRuntime().halt(0);
    }

    static void deleteTree(Path p) throws IOException {
        if (!Files.exists(p)) return;
        try (var s = Files.walk(p)) {
            for (Path x : s.sorted(Comparator.reverseOrder()).toList()) Files.delete(x);
        }
    }

    // ---------------------------------------------------------------- probe (server thread)

    static ServerLevel level;
    static ServerPlayer player;
    static BlockPos D;
    static final List<String> others = new ArrayList<>();
    static final List<String> anomalies = new ArrayList<>();

    static void clearNeighbourhood() {
        for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) for (int dz = -2; dz <= 2; dz++)
            level.setBlock(D.offset(dx, dy, dz), Blocks.AIR.defaultBlockState(), SETUP_FLAGS);
    }
    static void set(BlockPos p, BlockState s) { level.setBlock(p, s, SETUP_FLAGS); }

    static String[] snapshot() {
        String[] r = new String[27];
        int i = 0;
        for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++)
            r[i++] = stateString(level.getBlockState(D.offset(dx, dy, dz)));
        return r;
    }
    static String cellName(int i) {
        int dy = i / 9 - 1, dz = (i / 3) % 3 - 1, dx = i % 3 - 1;
        return "(" + dx + "," + dy + "," + dz + ")";
    }

    static void look(int lookIndex) {
        float yaw = yawOf(lookIndex), pitch = pitchOf(lookIndex);
        double px = D.getX() + 0.5, py = D.getY(), pz = D.getZ() + 3.5;
        player.snapTo(px, py, pz, yaw, pitch);
        player.setYRot(yaw);
        player.setXRot(pitch);
        player.setYHeadRot(yaw);
        player.setYBodyRot(yaw);
    }

    static Vec3 faceCentre(BlockPos clicked, Direction face) {
        return Vec3.atCenterOf(clicked).add(face.getStepX() * 0.5, face.getStepY() * 0.5, face.getStepZ() * 0.5);
    }

    /** One trial; returns the code. setup must already be built. expected = snapshot before the click. */
    static char click(String caseName, Direction face, String cfg, int lookIndex, BlockPos clicked, Map<String, String> orderSink) {
        look(lookIndex);
        player.setItemInHand(InteractionHand.MAIN_HAND, new ItemStack(Items.TORCH));
        BlockHitResult hit = new BlockHitResult(faceCentre(clicked, face), face, clicked, false);
        // Placement context as the item would see it (built before the click, on the same world state).
        BlockPlaceContext ctx = new BlockPlaceContext(player, InteractionHand.MAIN_HAND, player.getItemInHand(InteractionHand.MAIN_HAND), hit);
        String ctxOrder = order(ctx.getNearestLookingDirections()) + (ctx.replacingClickedOnBlock() ? "r" : "p")
            + "@" + (ctx.getClickedPos().equals(D) ? "D" : "X");
        String key = caseName + "|" + dirChar(face) + "|" + lookIndex;
        String prev = orderSink.putIfAbsent(key, ctxOrder);
        if (prev != null && !prev.equals(ctxOrder))
            anomalies.add("order differs by config " + key + " cfg=" + cfg + " " + prev + " vs " + ctxOrder);
        String[] before = snapshot();
        InteractionResult res = player.getItemInHand(InteractionHand.MAIN_HAND).useOn(new UseOnContext(player, InteractionHand.MAIN_HAND, hit));
        String[] after = snapshot();
        int centre = 13;
        for (int i = 0; i < 27; i++) {
            if (i == centre) continue;
            if (!before[i].equals(after[i]))
                anomalies.add(caseName + " face=" + dirChar(face) + " cfg=" + cfg + " look=" + lookIndex + " cell" + cellName(i) + " " + before[i] + " -> " + after[i]);
        }
        String a = after[centre];
        char code;
        if (a.equals(before[centre])) code = '-';
        else if (a.equals("minecraft:torch")) code = 'F';
        else if (a.equals("minecraft:wall_torch[facing=north]")) code = 'n';
        else if (a.equals("minecraft:wall_torch[facing=south]")) code = 's';
        else if (a.equals("minecraft:wall_torch[facing=east]")) code = 'e';
        else if (a.equals("minecraft:wall_torch[facing=west]")) code = 'w';
        else code = '?';
        if (code == '?') others.add(caseName + " face=" + dirChar(face) + " cfg=" + cfg + " look=" + lookIndex + " -> " + a + " result=" + res);
        boolean placed = code != '-';
        boolean ok = res.consumesAction();
        if (placed != ok) anomalies.add(caseName + " face=" + dirChar(face) + " cfg=" + cfg + " look=" + lookIndex + " placed=" + placed + " result=" + res);
        return code;
    }

    static String runProbe(MinecraftServer server) {
        level = server.overworld();
        D = new BlockPos(96, 150, 96);
        player = new ServerPlayer(server, level, new GameProfile(new UUID(0L, 0x7470726f6265L), "probe"), ClientInformation.createDefault());
        BlockState stone = Blocks.STONE.defaultBlockState();
        int looks = YAWS * PITCHES;

        // Look-order table: Direction.orderedByNearest(player) and view vector per look.
        List<String> nearest = new ArrayList<>();
        List<String> views = new ArrayList<>();
        for (int l = 0; l < looks; l++) {
            look(l);
            nearest.add(order(Direction.orderedByNearest(player)));
            Vec3 v = player.getViewVector(1.0f);
            views.add("[" + v.x + "," + v.y + "," + v.z + "]");
        }

        Map<String, String> ctxOrders = new LinkedHashMap<>();
        List<String> results = new ArrayList<>();

        // Case A: solid click on stone at D - offset(face); placement destination D.
        for (Direction face : FACES) {
            BlockPos clicked = D.relative(face.getOpposite());
            for (int mask = 0; mask < 32; mask++) {
                int forced = -1;
                for (int b = 0; b < 5; b++) if (SUPPORT_DIRS[b] == face.getOpposite()) forced = b;
                if (forced >= 0 && (mask & (1 << forced)) == 0) continue;
                StringBuilder cfg = new StringBuilder();
                for (int b = 0; b < 5; b++) cfg.append((mask & (1 << b)) != 0 ? SUPPORT_CHARS[b] : '-');
                StringBuilder codes = new StringBuilder();
                for (int l = 0; l < looks; l++) {
                    clearNeighbourhood();
                    for (int b = 0; b < 5; b++) if ((mask & (1 << b)) != 0) set(D.relative(SUPPORT_DIRS[b]), stone);
                    set(clicked, stone);
                    codes.append(click("A", face, cfg.toString(), l, clicked, ctxOrders));
                }
                results.add("{\"case\":\"A\",\"face\":\"" + dirChar(face) + "\",\"cfg\":\"" + cfg + "\",\"r\":\"" + codes + "\"}");
            }
        }
        // Case B: replaceable click on short grass at D (grass_block below), side supports on/off.
        for (Direction face : FACES) {
            for (int mask = 0; mask < 16; mask++) {
                StringBuilder cfg = new StringBuilder("B");
                for (int b = 1; b < 5; b++) cfg.append((mask & (1 << (b - 1))) != 0 ? SUPPORT_CHARS[b] : '-');
                StringBuilder codes = new StringBuilder();
                for (int l = 0; l < looks; l++) {
                    clearNeighbourhood();
                    set(D.below(), Blocks.GRASS_BLOCK.defaultBlockState());
                    for (int b = 1; b < 5; b++) if ((mask & (1 << (b - 1))) != 0) set(D.relative(SUPPORT_DIRS[b]), stone);
                    set(D, Blocks.SHORT_GRASS.defaultBlockState());
                    codes.append(click("B", face, cfg.toString(), l, D, ctxOrders));
                }
                results.add("{\"case\":\"B\",\"face\":\"" + dirChar(face) + "\",\"cfg\":\"" + cfg + "\",\"r\":\"" + codes + "\"}");
            }
        }
        clearNeighbourhood();

        StringBuilder j = new StringBuilder();
        j.append("{\n");
        j.append("\"game\":").append(q(SharedConstants.getCurrentVersion().name())).append(",\n");
        j.append("\"source\":\"vanilla 26.3 server jar, black-box via public API: GameTestServer level, connection-less ServerPlayer, ItemStack(minecraft:torch).useOn(UseOnContext(player, MAIN_HAND, BlockHitResult(face centre, face, clickedPos, false)))\",\n");
        j.append("\"grid\":{\"yaw\":{\"start\":0,\"step\":15,\"count\":24},\"pitch\":{\"start\":-90,\"step\":15,\"count\":13},")
         .append("\"look_index\":\"yawIndex*13+pitchIndex; yaw=15*yawIndex, pitch=-90+15*pitchIndex\",")
         .append("\"set_by\":\"Entity.snapTo(x,y,z,yaw,pitch) then setYRot(yaw), setXRot(pitch), setYHeadRot(yaw), setYBodyRot(yaw)\",")
         .append("\"convention\":\"vanilla Entity yRot/xRot in degrees. Measured (looks[].view = Entity.getViewVector(1), exact doubles, x=east y=up z=south): yaw 0 -> +z (south), 90 -> -x (west), 180 -> -z (north), 270 -> +x (east); pitch -90 -> +y (up), 0 -> level, +90 -> -y (down); view ~= (-sin(yaw)cos(pitch), -sin(pitch), cos(yaw)cos(pitch))\"},\n");
        j.append("\"dir_letters\":\"D=down U=up N=north(-z) S=south(+z) W=west(-x) E=east(+x)\",\n");
        j.append("\"cells\":\"D=(96,150,96); clicked face F: case A clicks stone at D-offset(F) on face F (placement cell D); case B clicks short_grass at D on face F\",\n");
        j.append("\"cfg_legend\":\"5 chars B,N,S,W,E = stone at D.below, D.north, D.south, D.west, D.east ('-' = air). Case A: the clicked block's own cell is always stone, so only configs with that bit set are listed (face D: all 32, clicked block is above D). Case B: first char 'B' = grass_block below (always), short_grass at D, last 4 = stone on D's sides\",\n");
        j.append("\"code_legend\":{\"-\":\"nothing placed (D unchanged)\",\"F\":\"minecraft:torch at D\",\"n\":\"minecraft:wall_torch[facing=north] at D\",\"s\":\"minecraft:wall_torch[facing=south] at D\",\"e\":\"minecraft:wall_torch[facing=east] at D\",\"w\":\"minecraft:wall_torch[facing=west] at D\",\"?\":\"anything else, listed in others\"},\n");
        j.append("\"r_format\":\"one code per look, 312 chars, in look_index order\",\n");
        j.append("\"order_format\":\"nearest = Direction.orderedByNearest(player); ctx[case+face] = BlockPlaceContext.getNearestLookingDirections() for that click, then 'p' (plain) or 'r' (replacingClickedOnBlock), then '@D' if getClickedPos()==D\",\n");
        j.append("\"looks\":[\n");
        for (int l = 0; l < looks; l++) {
            j.append("{\"i\":").append(l).append(",\"yaw\":").append((int) yawOf(l)).append(",\"pitch\":").append((int) pitchOf(l))
             .append(",\"view\":").append(views.get(l)).append(",\"nearest\":\"").append(nearest.get(l)).append("\",\"ctx\":{");
            boolean first = true;
            for (String c : new String[]{"A", "B"}) for (Direction f : FACES) {
                String o = ctxOrders.get(c + "|" + dirChar(f) + "|" + l);
                if (!first) j.append(',');
                first = false;
                j.append('"').append(c).append(dirChar(f)).append("\":\"").append(o).append('"');
            }
            j.append("}}").append(l + 1 < looks ? ",\n" : "\n");
        }
        j.append("],\n\"results\":[\n");
        for (int i = 0; i < results.size(); i++) j.append(results.get(i)).append(i + 1 < results.size() ? ",\n" : "\n");
        j.append("],\n\"others\":[");
        for (int i = 0; i < others.size(); i++) j.append(i > 0 ? "," : "").append("\n").append(q(others.get(i)));
        j.append("],\n\"anomalies\":[");
        for (int i = 0; i < anomalies.size(); i++) j.append(i > 0 ? "," : "").append("\n").append(q(anomalies.get(i)));
        j.append("]\n}\n");
        return j.toString();
    }
}
