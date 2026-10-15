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
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.context.UseOnContext;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.BlockStateProperties;
import net.minecraft.world.level.block.state.properties.Property;
import net.minecraft.world.level.block.state.properties.SlabType;
import net.minecraft.world.level.storage.LevelStorageSource;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.Vec3;

// Black-box measurement of vanilla 26.3 button/lever placement and press timing through the public API only.
// Usage: java -cp <server jar + libraries> SwitchPlacementProbe <work dir> <output json>
public class SwitchPlacementProbe {
    static final int SETUP_FLAGS = Block.UPDATE_CLIENTS | Block.UPDATE_KNOWN_SHAPE;
    static final float[] YAWS = {0f, 90f, 180f, 270f, 45f};
    static final float[] PITCHES = {-60f, 0f, 60f};
    static final String[] BLOCKS = {"stone_button", "oak_button", "lever"};
    static final Direction[] DIRS = Direction.values();

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
    static String q(String s) { return s == null ? "null" : "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\""; }
    static String num(float f) { return f == (int) f ? Integer.toString((int) f) : Float.toString(f); }

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
        try {
            server.submit(() -> {
                try {
                    String s = runProbe(server);
                    Files.createDirectories(out.getParent());
                    Files.write(out, s.getBytes(StandardCharsets.UTF_8));
                    return s;
                }
                catch (Exception e) { e.printStackTrace(); throw new RuntimeException(e); }
            }).get(30, TimeUnit.MINUTES);
        } finally {
            server.halt(false);
        }
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

    static ServerLevel level;
    static ServerPlayer player;
    static BlockPos P;
    static final List<String> anomalies = new ArrayList<>();

    static void clearArea() {
        for (int dx = -4; dx <= 4; dx++) for (int dy = -4; dy <= 4; dy++) for (int dz = -4; dz <= 4; dz++)
            level.setBlock(P.offset(dx, dy, dz), Blocks.AIR.defaultBlockState(), SETUP_FLAGS);
    }
    static void setb(BlockPos p, BlockState s) { level.setBlock(p, s, SETUP_FLAGS); }

    static void look(float yaw, float pitch) {
        player.snapTo(P.getX() + 0.5, P.getY() - 3, P.getZ() + 8.5, yaw, pitch);
        player.setYRot(yaw); player.setXRot(pitch); player.setYHeadRot(yaw); player.setYBodyRot(yaw);
    }
    static Item item(String b) { return BuiltInRegistries.ITEM.getValue(net.minecraft.resources.Identifier.withDefaultNamespace(b)); }
    static Block block(String b) { return BuiltInRegistries.BLOCK.getValue(net.minecraft.resources.Identifier.withDefaultNamespace(b)); }

    /** Click face `face` of block `clicked` at its centre; returns [state string at clicked.relative(face) or null, result]. */
    static String[] click(String b, BlockPos clicked, Direction face) {
        BlockPos dest = clicked.relative(face);
        player.setItemInHand(InteractionHand.MAIN_HAND, new ItemStack(item(b)));
        Vec3 hv = new Vec3(clicked.getX() + 0.5 + 0.5 * face.getStepX(), clicked.getY() + 0.5 + 0.5 * face.getStepY(), clicked.getZ() + 0.5 + 0.5 * face.getStepZ());
        BlockHitResult hit = new BlockHitResult(hv, face, clicked, false);
        InteractionResult res = player.getItemInHand(InteractionHand.MAIN_HAND).useOn(new UseOnContext(player, InteractionHand.MAIN_HAND, hit));
        BlockState st = level.getBlockState(dest);
        boolean placed = st.getBlock() == block(b);
        if (placed != res.consumesAction()) anomalies.add("placed=" + placed + " result=" + res + " " + b + " at " + dest);
        return new String[]{placed ? stateString(st) : null, String.valueOf(res)};
    }

    static String runProbe(MinecraftServer server) {
        level = server.overworld();
        P = new BlockPos(96, 150, 96);
        player = new ServerPlayer(server, level, new GameProfile(new UUID(0L, 0x73776974L), "probe"), ClientInformation.createDefault());
        StringBuilder A = new StringBuilder(), B = new StringBuilder();
        int na = 0, nb = 0;
        BlockState stone = Blocks.STONE.defaultBlockState();

        // Part A
        for (String b : BLOCKS) for (Direction f : DIRS) for (float yaw : YAWS) for (float pitch : PITCHES) {
            clearArea(); setb(P, stone); look(yaw, pitch);
            String[] r = click(b, P, f);
            A.append(na++ > 0 ? ",\n" : "").append('[').append(q(b)).append(',').append(num(yaw)).append(',').append(num(pitch)).append(',').append(q(f.getName())).append(',').append(q(r[0])).append(']');
        }
        // Part B: D = P; clicked block C = D + d (non-sturdy toward D), click face = d.opposite; stone at D + s (s != d) or none
        BlockState fence = Blocks.OAK_FENCE.defaultBlockState();
        BlockState slab = Blocks.OAK_SLAB.defaultBlockState().setValue(BlockStateProperties.SLAB_TYPE, SlabType.BOTTOM);
        for (String b : BLOCKS) for (int cType = 0; cType < 2; cType++) for (Direction d : DIRS) {
            if (cType == 1 && d.getAxis() == Direction.Axis.Y) continue;
            List<List<Direction>> ss = new ArrayList<>();
            ss.add(List.of());
            List<Direction> others = new ArrayList<>();
            for (Direction s : DIRS) if (s != d) others.add(s);
            for (Direction s : others) ss.add(List.of(s));
            for (int i1 = 0; i1 < others.size(); i1++) for (int i2 = i1 + 1; i2 < others.size(); i2++) ss.add(List.of(others.get(i1), others.get(i2)));
            ss.add(others);
            for (List<Direction> s : ss) for (float yaw : YAWS) for (float pitch : PITCHES) {
                clearArea(); look(yaw, pitch);
                BlockPos C = P.relative(d);
                setb(C, cType == 0 ? fence : slab);
                for (Direction sd : s) setb(P.relative(sd), stone);
                String[] r = click(b, C, d.getOpposite());
                B.append(nb++ > 0 ? ",\n" : "").append('[').append(q(b)).append(',').append(q(cType == 0 ? "oak_fence" : "oak_slab[type=bottom]")).append(',')
                    .append(q(d.getName())).append(',').append(q(s.isEmpty() ? null : String.join("+", s.stream().map(Direction::getName).toList()))).append(',')
                    .append(num(yaw)).append(',').append(num(pitch)).append(',').append(q(r[0])).append(']');
            }
        }

        // Part C
        for (int cx = 4; cx <= 7; cx++) for (int cz = 4; cz <= 7; cz++) level.setChunkForced(cx, cz, true);
        long[] chunkKeys = {net.minecraft.world.level.ChunkPos.pack(6, 5), net.minecraft.world.level.ChunkPos.pack(6, 6)};
        int[] spins = {0};
        server.managedBlock(() -> { if (level.isPositionTickingWithEntitiesLoaded(chunkKeys[0]) && level.isPositionTickingWithEntitiesLoaded(chunkKeys[1])) return true; if (++spins[0] > 3000) return true; level.tick(() -> true); try { Thread.sleep(2); } catch (InterruptedException e) { } return false; });
        if (!(level.isPositionTickingWithEntitiesLoaded(chunkKeys[0]) && level.isPositionTickingWithEntitiesLoaded(chunkKeys[1]))) anomalies.add("chunks not ticking with entities loaded");
        StringBuilder C = new StringBuilder();
        C.append("\"gameTimeAdvancesOnePerTick\":").append(timeCheck()).append(",\n");
        C.append("\"runs\":[\n");
        int nc = 0;
        for (String b : new String[]{"stone_button", "oak_button"}) {
            int[][] schedules = {{0}, {0, 10}, {0, 0}, {0, 1}, {0, 19}, {0, 20}, {0, 25}};
            for (int[] sch : schedules) {
                C.append(nc++ > 0 ? ",\n" : "").append(pressRun(b, sch));
            }
        }
        C.append(",\n").append(pressRun("lever", new int[]{0, 5}));
        C.append("\n]");
        clearArea();

        StringBuilder j = new StringBuilder();
        j.append("{\"version\":").append(q(SharedConstants.getCurrentVersion().name())).append(",\"probe\":\"SwitchPlacementProbe\",\n");
        j.append("\"conventions\":").append(q("Items stone_button/oak_button/lever used via ItemStack.useOn(UseOnContext(connection-less ServerPlayer, MAIN_HAND, BlockHitResult(centre of clicked face, face, clicked pos, false))). P=(96,150,96). Player yaw/pitch as Minecraft (yaw 0=south,90=west,180=north,270=east; pitch -60 looks up, +60 down). A rows: [block, yaw, pitch, clicked_face of stone at P, state at P+face or null]. B rows: [block, clicked_block (placed at D+d), d (clicked block is on side d of D, click face = opposite of d, destination D=P), stone cells at D+s, several joined with + (null=none, 5 names = all neighbours except d), yaw, pitch, state at D or null]. C: stone at P, switch placed on its north face (wall) at P.north by useOn; sched = tick indices (after N ServerLevel.tick calls) at which BlockState.useWithoutItem is called; ticks[i] = powered after i ServerLevel.tick calls (0..40), -1 = block gone; uses = [tick, InteractionResult].")).append(",\n");
        j.append("\"counts\":{\"A\":").append(na).append(",\"B\":").append(nb).append("},\n");
        j.append("\"anomalies\":[");
        for (int i = 0; i < anomalies.size(); i++) j.append(i > 0 ? "," : "").append("\n").append(q(anomalies.get(i)));
        j.append("],\n\"parts\":{\"A\":[\n").append(A).append("\n],\n\"B\":[\n").append(B).append("\n],\n\"C\":{").append(C).append("}}}\n");
        return j.toString();
    }

    static boolean timeCheck() {
        long t0 = level.getGameTime();
        level.tick(() -> true);
        return level.getGameTime() == t0 + 1;
    }

    static String pressRun(String b, int[] sch) {
        clearArea(); look(0f, 0f);
        setb(P, Blocks.STONE.defaultBlockState());
        String[] r = click(b, P, Direction.NORTH);
        BlockPos bp = P.north();
        BlockState placed = level.getBlockState(bp);
        if (r[0] == null) anomalies.add("C placement failed " + b);
        BlockHitResult hit = new BlockHitResult(Vec3.atCenterOf(bp), Direction.NORTH, bp, false);
        StringBuilder uses = new StringBuilder(), ticks = new StringBuilder(), afterUse = new StringBuilder();
        int next = 0;
        for (int t = 0; t <= 40; t++) {
            while (next < sch.length && sch[next] == t) {
                BlockState cur = level.getBlockState(bp);
                InteractionResult res = cur.useWithoutItem(level, player, hit);
                uses.append(next > 0 ? "," : "").append('[').append(t).append(',').append(q(String.valueOf(res))).append(']');
                afterUse.append(next > 0 ? "," : "").append(level.getBlockState(bp).getValue(BlockStateProperties.POWERED) ? 1 : 0);
                next++;
            }
            BlockState cur = level.getBlockState(bp);
            ticks.append(t > 0 ? "," : "").append(cur.getBlock() == placed.getBlock() ? (cur.getValue(BlockStateProperties.POWERED) ? 1 : 0) : -1);
            if (t < 40) level.tick(() -> true);
        }
        StringBuilder s = new StringBuilder("{\"block\":" + q(b) + ",\"placed\":" + q(r[0]) + ",\"sched\":[");
        for (int i = 0; i < sch.length; i++) s.append(i > 0 ? "," : "").append(sch[i]);
        s.append("],\"uses\":[").append(uses).append("],\"poweredRightAfterUse\":[").append(afterUse).append("],\"ticks\":[").append(ticks).append("]}");
        return s.toString();
    }
}
