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
import net.minecraft.world.item.context.UseOnContext;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.BlockStateProperties;
import net.minecraft.world.level.block.state.properties.DoorHingeSide;
import net.minecraft.world.level.block.state.properties.DoubleBlockHalf;
import net.minecraft.world.level.block.state.properties.Property;
import net.minecraft.world.level.block.state.properties.SlabType;
import net.minecraft.world.level.storage.LevelStorageSource;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.Vec3;

// Black-box measurement of vanilla 26.3 door hinge placement through the public API only (same boot as TorchPlacementProbe):
// ItemStack(oak_door).useOn on the top face of a stone floor with a connection-less ServerPlayer.
// Usage: java -cp <server jar + libraries> DoorHingeProbe <work dir> <output json>
public class DoorHingeProbe {
    static final int SETUP_FLAGS = Block.UPDATE_CLIENTS | Block.UPDATE_KNOWN_SHAPE;
    static final Direction[] FACINGS = {Direction.NORTH, Direction.EAST, Direction.SOUTH, Direction.WEST};
    static final double[] CLICKS = {0.25, 0.75};

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
    static final List<String> cases = new ArrayList<>();
    static final Map<String, Integer> counts = new LinkedHashMap<>();

    static void clearArea() {
        for (int dx = -3; dx <= 3; dx++) for (int dy = -2; dy <= 3; dy++) for (int dz = -3; dz <= 3; dz++)
            level.setBlock(P.offset(dx, dy, dz), Blocks.AIR.defaultBlockState(), SETUP_FLAGS);
        for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++)
            level.setBlock(P.offset(dx, -1, dz), Blocks.STONE.defaultBlockState(), SETUP_FLAGS);
    }

    /** A neighbour cell state: label is what goes in JSON (block id for solids, full state string for doors). */
    record Cell(String label, BlockState state) {}

    static Cell solid(String label, BlockState s) { return new Cell(label, s); }
    static Cell door(Block b, Direction f, DoorHingeSide hinge, DoubleBlockHalf half) {
        BlockState s = b.defaultBlockState().setValue(BlockStateProperties.HORIZONTAL_FACING, f)
            .setValue(BlockStateProperties.DOUBLE_BLOCK_HALF, half).setValue(BlockStateProperties.DOOR_HINGE, hinge)
            .setValue(BlockStateProperties.OPEN, false).setValue(BlockStateProperties.POWERED, false);
        return new Cell(stateString(s), s);
    }
    static Cell AIR;

    static void setChecked(BlockPos p, Cell c, String what) {
        level.setBlock(p, c.state, SETUP_FLAGS);
        BlockState rb = level.getBlockState(p);
        if (rb != c.state) anomalies.add("readback mismatch " + what + " at " + p + ": set " + stateString(c.state) + " got " + stateString(rb));
    }

    /** A door option on one side: lower cell and upper cell contents. */
    record Side(String name, Cell lower, Cell upper) {}

    static void run(String part, Direction f, double c, Side L, Side R) {
        clearArea();
        Direction left = f.getCounterClockWise(), right = f.getClockWise();
        BlockPos lp = P.relative(left), rp = P.relative(right);
        setChecked(lp, L.lower, "left_lower");
        setChecked(lp.above(), L.upper, "left_upper");
        setChecked(rp, R.lower, "right_lower");
        setChecked(rp.above(), R.upper, "right_upper");
        float yaw = f.toYRot();
        double px = P.getX() + 0.5 - 2 * f.getStepX(), pz = P.getZ() + 0.5 - 2 * f.getStepZ();
        player.snapTo(px, P.getY(), pz, yaw, 45f);
        player.setYRot(yaw); player.setXRot(45f); player.setYHeadRot(yaw); player.setYBodyRot(yaw);
        player.setItemInHand(InteractionHand.MAIN_HAND, new ItemStack(Items.OAK_DOOR));
        double hx = P.getX() + 0.5, hz = P.getZ() + 0.5;
        if (f.getAxis() == Direction.Axis.X) hz = P.getZ() + c; else hx = P.getX() + c;
        BlockPos floor = P.below();
        BlockHitResult hit = new BlockHitResult(new Vec3(hx, P.getY(), hz), Direction.UP, floor, false);
        InteractionResult res = player.getItemInHand(InteractionHand.MAIN_HAND).useOn(new UseOnContext(player, InteractionHand.MAIN_HAND, hit));
        String lower = stateString(level.getBlockState(P)), upper = stateString(level.getBlockState(P.above()));
        boolean placed = lower.startsWith("minecraft:oak_door");
        if (placed != res.consumesAction()) anomalies.add(part + " " + f + " c=" + c + " placed=" + placed + " result=" + res);
        if (!placed) counts.merge("failed_" + part, 1, Integer::sum);
        counts.merge("n_" + part, 1, Integer::sum);
        cases.add("{\"part\":" + q(part) + ",\"facing\":" + q(f.getName()) + ",\"click\":" + c
            + ",\"left_lower\":" + q(L.lower.label) + ",\"left_upper\":" + q(L.upper.label)
            + ",\"right_lower\":" + q(R.lower.label) + ",\"right_upper\":" + q(R.upper.label)
            + ",\"result\":" + q(String.valueOf(res)) + ",\"lower\":" + q(lower) + ",\"upper\":" + q(upper) + "}");
    }

    static String runProbe(MinecraftServer server) {
        level = server.overworld();
        AIR = solid("minecraft:air", Blocks.AIR.defaultBlockState());
        P = new BlockPos(96, 150, 96);
        player = new ServerPlayer(server, level, new GameProfile(new UUID(0L, 0x646f6f72L), "probe"), ClientInformation.createDefault());

        List<Cell> solids = List.of(
            AIR,
            solid("minecraft:stone", Blocks.STONE.defaultBlockState()),
            solid("minecraft:glass", Blocks.GLASS.defaultBlockState()),
            solid("minecraft:oak_leaves", Blocks.OAK_LEAVES.defaultBlockState()),
            solid("minecraft:oak_slab[type=bottom]", Blocks.OAK_SLAB.defaultBlockState().setValue(BlockStateProperties.SLAB_TYPE, SlabType.BOTTOM)),
            solid("minecraft:oak_fence", Blocks.OAK_FENCE.defaultBlockState()));

        for (Direction f : FACINGS) for (double c : CLICKS)
            for (Cell ll : solids) for (Cell lu : solids) for (Cell rl : solids) for (Cell ru : solids)
                run("A", f, c, new Side("", ll, lu), new Side("", rl, ru));

        for (Direction f : FACINGS) {
            Side[] opts = doorOptions(f);
            for (double c : CLICKS) for (Side l : opts) for (Side r : opts) run("B", f, c, l, r);
        }
        Cell stone = solids.get(1);
        Side stoneSide = new Side("stone", stone, stone);
        for (Direction f : FACINGS) {
            Side[] opts = doorOptions(f);
            for (double c : CLICKS) for (int i = 1; i < opts.length; i++) {
                run("C", f, c, opts[i], stoneSide);
                run("C", f, c, stoneSide, opts[i]);
            }
        }
        Cell stoneD = stone;
        double[] clicksD = {0.25, 0.5, 0.75};
        for (int sideIdx = 0; sideIdx < 2; sideIdx++) for (Direction f : FACINGS) for (double c : clicksD) {
            Side dside = new Side("door_lower_over_stone", door(Blocks.OAK_DOOR, f, DoorHingeSide.LEFT, DoubleBlockHalf.LOWER), stoneD);
            Side[] others = {new Side("air_air", AIR, AIR), new Side("stone_air", stoneD, AIR), new Side("air_stone", AIR, stoneD), new Side("stone_stone", stoneD, stoneD)};
            for (Side o : others) {
                if (sideIdx == 0) run("D", f, c, dside, o); else run("D", f, c, o, dside);
            }
        }
        Side airSide = new Side("air_air", AIR, AIR);
        for (Direction f : FACINGS) run("E", f, 0.5, airSide, airSide);
        for (double c : new double[]{0.4999, 0.5001}) for (Direction f : FACINGS) run("E", f, c, airSide, airSide);
        clearArea();

        StringBuilder j = new StringBuilder();
        j.append("{\"version\":").append(q(SharedConstants.getCurrentVersion().name())).append(",\"probe\":\"DoorHingeProbe\",\n");
        j.append("\"conventions\":").append(q("oak_door item used via ItemStack.useOn(UseOnContext(fake ServerPlayer, MAIN_HAND, BlockHitResult(hit, UP, floor pos, false))) on the top face of a stone floor at y=P.y-1, P=(96,150,96); lower half goes in P, upper in P+up. Player stands 2 blocks behind P opposite to facing, yaw = facing (0=south,90=west,180=north,270=east), pitch 45 (down). click = lateral hit coordinate offset within P's block: x for north/south facing, z for east/west facing; the other coordinate is +0.5. left = Direction.getCounterClockWise() of facing (facing south -> left=east +X; north -> left=west -X; east -> left=north -Z; west -> left=south +Z), right = clockwise. left_lower=P+left, left_upper=P+left+up, right_lower=P+right, right_upper=P+right+up. Solid cells are block ids (slab = type bottom), door cells are full state strings (as set, read back and verified; setBlock flags UPDATE_CLIENTS|UPDATE_KNOWN_SHAPE, no neighbour updates). Part A: 6^4 solids per facing/click; Part B: 8x8 door options per side (none, oak f hinge=left, oak f hinge=right, spruce f left, oak opposite(f) left, iron f left, oak lower only, oak upper only); Part C: each non-none door option on one side with stone (both cells) on the other side, both orders. Part D: door LOWER half only (facing = placed facing, hinge=left, closed) in the lower cell with stone above it on one side (sides in order left then right), other side air/air, stone/air, air/stone, stone/stone (lower/upper); clicks 0.25,0.5,0.75. Part E: all four neighbour cells air, clicks 0.5 then 0.4999 then 0.5001.")).append(",\n");
        j.append("\"counts\":{");
        boolean first = true;
        for (var e : counts.entrySet()) { if (!first) j.append(','); first = false; j.append(q(e.getKey())).append(':').append(e.getValue()); }
        j.append("},\n\"anomalies\":[");
        for (int i = 0; i < anomalies.size(); i++) j.append(i > 0 ? "," : "").append("\n").append(q(anomalies.get(i)));
        j.append("],\n\"cases\":[\n");
        for (int i = 0; i < cases.size(); i++) j.append(cases.get(i)).append(i + 1 < cases.size() ? ",\n" : "\n");
        j.append("]}\n");
        return j.toString();
    }

    static Side[] doorOptions(Direction f) {
        Side none = new Side("none", AIR, AIR);
        Side oakL = new Side("oak_f_left", door(Blocks.OAK_DOOR, f, DoorHingeSide.LEFT, DoubleBlockHalf.LOWER), door(Blocks.OAK_DOOR, f, DoorHingeSide.LEFT, DoubleBlockHalf.UPPER));
        Side oakR = new Side("oak_f_right", door(Blocks.OAK_DOOR, f, DoorHingeSide.RIGHT, DoubleBlockHalf.LOWER), door(Blocks.OAK_DOOR, f, DoorHingeSide.RIGHT, DoubleBlockHalf.UPPER));
        Side spr = new Side("spruce_f_left", door(Blocks.SPRUCE_DOOR, f, DoorHingeSide.LEFT, DoubleBlockHalf.LOWER), door(Blocks.SPRUCE_DOOR, f, DoorHingeSide.LEFT, DoubleBlockHalf.UPPER));
        Direction o = f.getOpposite();
        Side opp = new Side("oak_opp_left", door(Blocks.OAK_DOOR, o, DoorHingeSide.LEFT, DoubleBlockHalf.LOWER), door(Blocks.OAK_DOOR, o, DoorHingeSide.LEFT, DoubleBlockHalf.UPPER));
        Side iron = new Side("iron_f_left", door(Blocks.IRON_DOOR, f, DoorHingeSide.LEFT, DoubleBlockHalf.LOWER), door(Blocks.IRON_DOOR, f, DoorHingeSide.LEFT, DoubleBlockHalf.UPPER));
        Side lowOnly = new Side("oak_lower_only", door(Blocks.OAK_DOOR, f, DoorHingeSide.LEFT, DoubleBlockHalf.LOWER), AIR);
        Side upOnly = new Side("oak_upper_only", AIR, door(Blocks.OAK_DOOR, f, DoorHingeSide.LEFT, DoubleBlockHalf.UPPER));
        return new Side[]{none, oakL, oakR, spr, opp, iron, lowOnly, upOnly};
    }
}
