import java.io.PrintWriter;
import java.nio.file.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.data.registries.VanillaRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.flag.FeatureFlags;
import net.minecraft.world.item.*;
import net.minecraft.world.level.block.Block;

// Dumps 26.3 creative tab contents and per-block hardness facts by running the game's own registries. With a
// second argument (a file of block ids, one per line), also dumps per-state shapes and behaviour for those blocks
// (block_state_facts.json) and the faces each of their states holds a torch on (block_support.json), measured
// through public state methods only, and block_sounds.csv, each of those blocks' sound type.
public class FactsDump {
    static String q(String s) { return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\""; }

    public static void main(String[] args) throws Exception {
        Path out = Path.of(args[0]);
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        // Load the built-in vanilla data pack the way a dedicated server starts, so tags and components are bound.
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
        net.minecraft.core.HolderLookup.Provider lookup = layers.compositeAccess();
        // Bind item default components the way the server does after loading registries.
        // Tabs are built one by one: the painting tab needs data-pack tags, which a bare bootstrap lacks.
        var params = new CreativeModeTab.ItemDisplayParameters(FeatureFlags.DEFAULT_FLAGS, true, lookup);
        java.util.List<CreativeModeTab> ordered = new java.util.ArrayList<>(CreativeModeTabs.allTabs());
        ordered.sort(java.util.Comparator.comparing(t -> t.getType() == CreativeModeTab.Type.SEARCH));
        for (CreativeModeTab tab : ordered) {
            try { tab.buildContents(params); }
            catch (RuntimeException e) { System.out.println("TAB FAILED " + BuiltInRegistries.CREATIVE_MODE_TAB.getKey(tab) + ": " + e); }
        }
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve("creative_tabs.json")))) {
            w.println("[");
            boolean firstTab = true;
            for (CreativeModeTab tab : CreativeModeTabs.allTabs()) {
                if (!firstTab) w.println(",");
                firstTab = false;
                var key = BuiltInRegistries.CREATIVE_MODE_TAB.getKey(tab);
                w.print("{\"id\":" + q(String.valueOf(key)) + ",\"title\":" + q(tab.getDisplayName().getString()) + ",\"row\":"
                    + q(tab.row().name()) + ",\"column\":" + tab.column() + ",\"type\":" + q(tab.getType().name())
                    + ",\"aligned_right\":" + tab.isAlignedRight() + ",\"show_title\":" + tab.showTitle() + ",\"can_scroll\":"
                    + tab.canScroll() + ",\"should_display\":" + tab.shouldDisplay() + ",\"icon\":"
                    + q(String.valueOf(BuiltInRegistries.ITEM.getKey(tab.getIconItem().getItem()))));
                for (String list : new String[] {"display", "search"}) {
                    var stacks = list.equals("display") ? tab.getDisplayItems() : tab.getSearchTabDisplayItems();
                    w.print(",\"" + list + "\":[");
                    boolean first = true;
                    for (ItemStack s : stacks) {
                        if (!first) w.print(",");
                        first = false;
                        String id = String.valueOf(BuiltInRegistries.ITEM.getKey(s.getItem()));
                        String patch = s.getComponentsPatch().isEmpty() ? "" : s.getComponentsPatch().toString();
                        w.print(patch.isEmpty() ? q(id) : "{\"id\":" + q(id) + ",\"name\":" + q(s.getHoverName().getString()) + ",\"patch\":" + q(patch) + "}");
                    }
                    w.print("]");
                }
                w.print("}");
            }
            w.println("\n]");
        }
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve("block_facts.csv")))) {
            w.println("protocol_id,id,destroy_time,explosion_resistance,requires_correct_tool_for_drops,light_emission,item,item_max_stack,item_name");
            for (Block b : BuiltInRegistries.BLOCK) {
                var state = b.defaultBlockState();
                Item item = b.asItem();
                String itemId = item == Items.AIR ? "" : String.valueOf(BuiltInRegistries.ITEM.getKey(item));
                w.println(BuiltInRegistries.BLOCK.getId(b) + "," + BuiltInRegistries.BLOCK.getKey(b).getPath() + "," + b.defaultDestroyTime() + ","
                    + b.getExplosionResistance() + "," + state.requiresCorrectToolForDrops() + "," + state.getLightEmission() + ","
                    + itemId + "," + (itemId.isEmpty() ? "" : new ItemStack(item).getMaxStackSize()) + ","
                    + (itemId.isEmpty() ? "" : q(new ItemStack(item).getHoverName().getString())));
            }
        }
        if (args.length > 1) {
            dumpStates(out, Path.of(args[1]));
            dumpSupport(out, Path.of(args[1]));
            dumpSounds(out, Path.of(args[1]));
        }
        System.out.println("FACTS DUMP OK");
        System.exit(0);
    }

    static String sound(net.minecraft.sounds.SoundEvent e) {
        if (!e.location().getNamespace().equals("minecraft")) throw new IllegalStateException("sound outside minecraft: " + e.location());
        return e.location().getPath();
    }

    // Per imported block: the default state's SoundType (BlockStateBase.getSoundType): volume, pitch and the five sound
    // event ids as paths in the minecraft namespace (e.g. block.stone.break). Every state of the block must report the
    // same sound type; the check fails the dump otherwise.
    static void dumpSounds(Path out, Path idsFile) throws Exception {
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve("block_sounds.csv")))) {
            w.println("id,volume,pitch,break,hit,place,step,fall");
            for (String id : Files.readAllLines(idsFile)) {
                if (id.isBlank()) continue;
                Block b = BuiltInRegistries.BLOCK.getValue(net.minecraft.resources.Identifier.withDefaultNamespace(id.trim()));
                var type = b.defaultBlockState().getSoundType();
                for (var s : b.getStateDefinition().getPossibleStates())
                    if (s.getSoundType() != type)
                        throw new IllegalStateException(id.trim() + ": states differ in sound type");
                w.println(id.trim() + "," + type.getVolume() + "," + type.getPitch() + "," + sound(type.getBreakSound()) + ","
                    + sound(type.getHitSound()) + "," + sound(type.getPlaceSound()) + "," + sound(type.getStepSound()) + ","
                    + sound(type.getFallSound()));
            }
        }
        System.out.println("SOUND TYPES UNIFORM per block");
    }

    static String boxes(net.minecraft.world.phys.shapes.VoxelShape shape, net.minecraft.world.phys.Vec3 shift) {
        StringBuilder b = new StringBuilder("[");
        for (var box : shape.toAabbs()) {
            var m = box.move(shift.reverse());
            if (b.length() > 1) b.append(",");
            b.append("[").append(m.minX).append(",").append(m.minY).append(",").append(m.minZ).append(",")
                .append(m.maxX).append(",").append(m.maxY).append(",").append(m.maxZ).append("]");
        }
        return b.append("]").toString();
    }

    // Per state: outline and collision boxes (in block units, with the position offset taken out when the shape moves
    // with it), light, replaceable, fluid and the faces whose occlusion shape is a full square. Per block: the offset
    // kind and range, sampled black-box from getOffset over 4,096 columns, and whether the shapes move with it.
    static void dumpStates(Path out, Path idsFile) throws Exception {
        var getter = net.minecraft.world.level.EmptyBlockGetter.INSTANCE;
        var dirs = new net.minecraft.core.Direction[] {net.minecraft.core.Direction.WEST, net.minecraft.core.Direction.EAST,
            net.minecraft.core.Direction.NORTH, net.minecraft.core.Direction.SOUTH, net.minecraft.core.Direction.DOWN,
            net.minecraft.core.Direction.UP};
        var origin = net.minecraft.core.BlockPos.ZERO;
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve("block_state_facts.json")))) {
            w.println("{\"face_order\":[\"west\",\"east\",\"north\",\"south\",\"down\",\"up\"],\"blocks\":[");
            boolean firstBlock = true;
            for (String id : Files.readAllLines(idsFile)) {
                if (id.isBlank()) continue;
                Block b = BuiltInRegistries.BLOCK.getValue(net.minecraft.resources.Identifier.withDefaultNamespace(id.trim()));
                if (!firstBlock) w.println(",");
                firstBlock = false;
                var def = b.defaultBlockState();
                String offset = "null";
                if (def.hasOffsetFunction()) {
                    double maxXZ = 0, minY = 0, maxY = 0;
                    boolean yIndependent = true, shapeMoves = false, collisionMoves = false;
                    for (int x = 0; x < 64; x++)
                        for (int z = 0; z < 64; z++) {
                            var p = new net.minecraft.core.BlockPos(x * 7 - 200, 0, z * 13 - 400);
                            var o = def.getOffset(p);
                            maxXZ = Math.max(maxXZ, Math.max(Math.abs(o.x), Math.abs(o.z)));
                            minY = Math.min(minY, o.y);
                            maxY = Math.max(maxY, o.y);
                            if (!o.equals(def.getOffset(p.above(37)))) yIndependent = false;
                        }
                    for (var s : b.getStateDefinition().getPossibleStates())
                        for (int x = 1; x < 9; x++) {
                            var p = new net.minecraft.core.BlockPos(x * 11, 0, x * 5);
                            if (!boxes(s.getShape(getter, p), net.minecraft.world.phys.Vec3.ZERO).equals(boxes(s.getShape(getter, origin), net.minecraft.world.phys.Vec3.ZERO))) shapeMoves = true;
                            if (!boxes(s.getCollisionShape(getter, p), net.minecraft.world.phys.Vec3.ZERO).equals(boxes(s.getCollisionShape(getter, origin), net.minecraft.world.phys.Vec3.ZERO))) collisionMoves = true;
                        }
                    offset = "{\"kind\":" + q(minY < 0 || maxY > 0 ? "xyz" : "xz") + ",\"max_xz\":" + maxXZ + ",\"min_y\":" + minY
                        + ",\"max_y\":" + maxY + ",\"y_independent\":" + yIndependent + ",\"outline_moves\":" + shapeMoves
                        + ",\"collision_moves\":" + collisionMoves + "}";
                }
                w.print("{\"id\":" + q(id.trim()) + ",\"offset\":" + offset + ",\"states\":[");
                boolean firstState = true;
                for (var s : b.getStateDefinition().getPossibleStates()) {
                    var shift = s.hasOffsetFunction() ? s.getOffset(origin) : net.minecraft.world.phys.Vec3.ZERO;
                    var outline = s.getShape(getter, origin);
                    var collision = s.getCollisionShape(getter, origin);
                    // Moving shapes are stored unshifted: compare with the shape at another column, shifted back.
                    var p = new net.minecraft.core.BlockPos(77, 0, 35);
                    var shiftP = s.hasOffsetFunction() ? s.getOffset(p) : net.minecraft.world.phys.Vec3.ZERO;
                    if (s.hasOffsetFunction() && (shiftP.x == shift.x || shiftP.z == shift.z))
                        throw new IllegalStateException(id + ": pick another probe column, the offsets coincide");
                    boolean outlineMoves = !boxes(s.getShape(getter, p), net.minecraft.world.phys.Vec3.ZERO).equals(boxes(outline, net.minecraft.world.phys.Vec3.ZERO));
                    boolean collisionMoves = !boxes(s.getCollisionShape(getter, p), net.minecraft.world.phys.Vec3.ZERO).equals(boxes(collision, net.minecraft.world.phys.Vec3.ZERO));
                    String outlineText = boxes(outline, outlineMoves ? shift : net.minecraft.world.phys.Vec3.ZERO);
                    String collisionText = boxes(collision, collisionMoves ? shift : net.minecraft.world.phys.Vec3.ZERO);
                    if (outlineMoves && !outlineText.equals(boxes(s.getShape(getter, p), shiftP)))
                        throw new IllegalStateException(id + ": outline moves by more than the offset");
                    if (collisionMoves && !collisionText.equals(boxes(s.getCollisionShape(getter, p), shiftP)))
                        throw new IllegalStateException(id + ": collision moves by more than the offset");
                    int occludes = 0;
                    if (s.canOcclude())
                        for (int i = 0; i < 6; i++)
                            if (Block.isFaceFull(s.getFaceOcclusionShape(dirs[i]), dirs[i])) occludes |= 1 << i;
                    var fluid = s.getFluidState();
                    String fluidText = fluid.isEmpty() ? "null" : "{\"fluid\":" + q(String.valueOf(BuiltInRegistries.FLUID.getKey(fluid.getType())))
                        + ",\"amount\":" + fluid.getAmount() + ",\"source\":" + fluid.isSource() + "}";
                    if (!firstState) w.print(",");
                    firstState = false;
                    w.print("\n {\"id\":" + Block.getId(s) + ",\"outline\":" + outlineText + ",\"collision\":" + collisionText
                        + ",\"light\":" + s.getLightEmission() + ",\"replaceable\":" + s.canBeReplaced() + ",\"occludes\":" + occludes
                        + ",\"fluid\":" + fluidText + "}");
                }
                w.print("]}");
            }
            w.println("\n]}");
        }
    }

    // A world holding one state at one position and air everywhere else, as a proxy of the public LevelReader
    // interface: block and fluid lookups answer from that state, the interface's default methods run as declared,
    // and any other method fails the dump instead of guessing.
    static net.minecraft.world.level.LevelReader worldWith(net.minecraft.core.BlockPos at,
            net.minecraft.world.level.block.state.BlockState state) {
        var air = net.minecraft.world.level.block.Blocks.AIR.defaultBlockState();
        var type = net.minecraft.world.level.LevelReader.class;
        return (net.minecraft.world.level.LevelReader) java.lang.reflect.Proxy.newProxyInstance(type.getClassLoader(),
            new Class<?>[] {type}, (proxy, method, a) -> switch (method.getName()) {
                case "getBlockState" -> at.equals(a[0]) ? state : air;
                case "getFluidState" -> (at.equals(a[0]) ? state : air).getFluidState();
                case "getBlockEntity" -> null;
                case "isClientSide" -> false;
                case "getMinY" -> -64;
                case "getHeight" -> {
                    if (a == null || a.length == 0) yield 384;
                    throw new UnsupportedOperationException(method.toString());
                }
                case "hashCode" -> System.identityHashCode(proxy);
                case "equals" -> proxy == a[0];
                case "toString" -> "support probe world";
                default -> {
                    if (method.isDefault()) yield java.lang.reflect.InvocationHandler.invokeDefault(proxy, method, a);
                    throw new UnsupportedOperationException(method.toString());
                }
            });
    }

    // Per state: a bit per face (face_order) when a torch attached to that face survives, from canSurvive on the
    // torch's own state in a world holding only the supporting state. Up: a floor torch on top. The four sides: a wall
    // torch against that side, facing away from it. Down: no torch hangs, so always clear. Per standing-and-wall item
    // of an imported block: its floor and wall blocks, the blocks BlockItem.registerBlocks maps to it.
    static void dumpSupport(Path out, Path idsFile) throws Exception {
        var dirs = new net.minecraft.core.Direction[] {net.minecraft.core.Direction.WEST, net.minecraft.core.Direction.EAST,
            net.minecraft.core.Direction.NORTH, net.minecraft.core.Direction.SOUTH, net.minecraft.core.Direction.DOWN,
            net.minecraft.core.Direction.UP};
        var at = new net.minecraft.core.BlockPos(0, 64, 0);
        var torch = net.minecraft.world.level.block.Blocks.TORCH.defaultBlockState();
        var wall = net.minecraft.world.level.block.Blocks.WALL_TORCH.defaultBlockState();
        var ids = Files.readAllLines(idsFile).stream().filter(s -> !s.isBlank()).map(String::trim).toList();
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve("block_support.json")))) {
            w.print("{\"face_order\":[\"west\",\"east\",\"north\",\"south\",\"down\",\"up\"],\"probes\":{\"west\":"
                + "\"wall_torch[facing=west]\",\"east\":\"wall_torch[facing=east]\",\"north\":\"wall_torch[facing=north]\","
                + "\"south\":\"wall_torch[facing=south]\",\"down\":null,\"up\":\"torch\"},\"items\":[");
            boolean firstItem = true;
            for (String id : ids) {
                Block b = BuiltInRegistries.BLOCK.getValue(net.minecraft.resources.Identifier.withDefaultNamespace(id));
                if (!(b.asItem() instanceof StandingAndWallBlockItem item) || item.getBlock() != b) continue;
                var map = new java.util.LinkedHashMap<Block, Item>();
                item.registerBlocks(map, item);
                var others = map.keySet().stream().filter(k -> k != b).toList();
                if (others.size() != 1) throw new IllegalStateException(id + ": expected one wall block, got " + others);
                if (!firstItem) w.print(",");
                firstItem = false;
                w.print("\n {\"item\":" + q(String.valueOf(BuiltInRegistries.ITEM.getKey(item))) + ",\"standing\":" + q(id)
                    + ",\"wall\":" + q(BuiltInRegistries.BLOCK.getKey(others.get(0)).getPath()) + "}");
            }
            w.print("],\"blocks\":[");
            boolean firstBlock = true;
            for (String id : ids) {
                Block b = BuiltInRegistries.BLOCK.getValue(net.minecraft.resources.Identifier.withDefaultNamespace(id));
                StringBuilder masks = new StringBuilder();
                int previous = -1;
                for (var s : b.getStateDefinition().getPossibleStates()) {
                    if (Block.getId(s) != previous + 1 && previous >= 0)
                        throw new IllegalStateException(id + ": state ids are not consecutive");
                    previous = Block.getId(s);
                    var level = worldWith(at, s);
                    int mask = 0;
                    for (int i = 0; i < 6; i++) {
                        if (dirs[i] == net.minecraft.core.Direction.DOWN) continue;
                        var probe = dirs[i] == net.minecraft.core.Direction.UP ? torch
                            : wall.setValue(net.minecraft.world.level.block.WallTorchBlock.FACING, dirs[i]);
                        if (probe.canSurvive(level, at.relative(dirs[i]))) mask |= 1 << i;
                    }
                    masks.append(masks.length() > 0 ? "," : "").append(mask);
                }
                if (!firstBlock) w.print(",");
                firstBlock = false;
                w.print("\n {\"id\":" + q(id) + ",\"first_state\":" + Block.getId(b.getStateDefinition().getPossibleStates().get(0))
                    + ",\"support\":[" + masks + "]}");
            }
            w.println("\n]}");
        }
    }
}
