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
// through public state methods only, and block_sounds.csv, each of those blocks' sound type. Survival B1 added
// block_use_sounds.csv (door, trapdoor, gate, button, plate and lever use events) and family_survival.json (the
// survival probes over every imported block); plant_survival.json keeps the frozen base list (third argument).
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
        registryAccess = layers.compositeAccess();
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
            // Third argument: the frozen survival base list (tools/survival_base_ids.txt), the subjects and candidates
            // of plant_survival.json, so that file does not change when blocks are added.
            if (args.length < 3) throw new IllegalArgumentException("usage: FactsDump <out> <ids> <survival_base_ids>");
            dumpStates(out, Path.of(args[1]));
            dumpSupport(out, Path.of(args[1]));
            dumpSounds(out, Path.of(args[1]));
            dumpUseSounds(out, Path.of(args[1]));
            dumpSurvival(out, Path.of(args[2]));
            dumpFamilySurvival(out, Path.of(args[1]));
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

    // The BlockSetType (doors, trapdoors, buttons, pressure plates) or WoodType (fence gates) a block is made of,
    // found by name among the public values(): the block id without its family suffix, without the waxed and
    // oxidation prefixes (the four copper stages share one set), heavy/light weighted plates being iron and gold.
    // Doors expose their type publicly (DoorBlock.type()), which checks the rule; in 26.3 TrapDoorBlock, ButtonBlock,
    // BasePressurePlateBlock/PressurePlateBlock/WeightedPressurePlateBlock and FenceGateBlock have no public type
    // accessor (javap -public), so every other block is checked by its sound type against the set's. Either check
    // failing throws.
    static String material(String id, String suffix) {
        String m = id.substring(0, id.length() - suffix.length());
        for (String p : new String[] {"waxed_", "exposed_", "weathered_", "oxidized_"}) if (m.startsWith(p)) m = m.substring(p.length());
        if (m.equals("heavy_weighted")) m = "iron";
        if (m.equals("light_weighted")) m = "gold";
        return m;
    }

    static net.minecraft.world.level.block.state.properties.BlockSetType setType(String id, String suffix) {
        String m = material(id, suffix);
        var found = net.minecraft.world.level.block.state.properties.BlockSetType.values()
            .filter(t -> t.name().equals(m) || t.name().equals("minecraft:" + m)).toList();
        if (found.size() != 1) throw new IllegalStateException(id + ": block set type " + m + " matches " + found.size());
        return found.get(0);
    }

    // Per imported door, trapdoor, fence gate, button, pressure plate and lever: the events it plays when used, as
    // paths in the minecraft namespace, from the public BlockSetType / WoodType accessors (the lever: its public
    // SoundEvents constant). open/close for doors, trapdoors and gates; click_on/click_off for buttons, plates, lever.
    static void dumpUseSounds(Path out, Path idsFile) throws Exception {
        int rows = 0, doorChecks = 0, soundChecks = 0;
        var soundMismatch = new java.util.ArrayList<String>();
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve("block_use_sounds.csv")))) {
            w.println("id,kind,set_type,open,close,click_on,click_off");
            for (String line : Files.readAllLines(idsFile)) {
                if (line.isBlank()) continue;
                String id = line.trim();
                Block b = BuiltInRegistries.BLOCK.getValue(net.minecraft.resources.Identifier.withDefaultNamespace(id));
                String kind, type, open = "", close = "", on = "", off = "";
                net.minecraft.world.level.block.SoundType expected = null;
                if (b instanceof net.minecraft.world.level.block.DoorBlock door) {
                    var t = setType(id, "_door");
                    if (door.type() != t) throw new IllegalStateException(id + ": DoorBlock.type() is " + door.type().name() + ", not " + t.name());
                    doorChecks++;
                    kind = "door"; type = t.name(); open = sound(t.doorOpen()); close = sound(t.doorClose()); expected = t.soundType();
                } else if (b instanceof net.minecraft.world.level.block.TrapDoorBlock) {
                    var t = setType(id, "_trapdoor");
                    kind = "trapdoor"; type = t.name(); open = sound(t.trapdoorOpen()); close = sound(t.trapdoorClose()); expected = t.soundType();
                } else if (b instanceof net.minecraft.world.level.block.ButtonBlock) {
                    var t = setType(id, "_button");
                    kind = "button"; type = t.name(); on = sound(t.buttonClickOn()); off = sound(t.buttonClickOff()); expected = t.soundType();
                } else if (b instanceof net.minecraft.world.level.block.BasePressurePlateBlock) {
                    var t = setType(id, "_pressure_plate");
                    kind = "pressure_plate"; type = t.name(); on = sound(t.pressurePlateClickOn()); off = sound(t.pressurePlateClickOff()); expected = t.soundType();
                } else if (b instanceof net.minecraft.world.level.block.FenceGateBlock) {
                    String m = material(id, "_fence_gate");
                    var found = net.minecraft.world.level.block.state.properties.WoodType.values()
                        .filter(t -> t.name().equals(m) || t.name().equals("minecraft:" + m)).toList();
                    if (found.size() != 1) throw new IllegalStateException(id + ": wood type " + m + " matches " + found.size());
                    var t = found.get(0);
                    kind = "fence_gate"; type = t.name(); open = sound(t.fenceGateOpen()); close = sound(t.fenceGateClose()); expected = t.soundType();
                } else if (b instanceof net.minecraft.world.level.block.LeverBlock) {
                    kind = "lever"; type = ""; on = sound(net.minecraft.sounds.SoundEvents.LEVER_CLICK); off = on;
                } else continue;
                if (expected != null) {
                    soundChecks++;
                    if (b.defaultBlockState().getSoundType() != expected) soundMismatch.add(id);
                }
                w.println(id + "," + kind + "," + type + "," + open + "," + close + "," + on + "," + off);
                rows++;
            }
        }
        System.out.println("USE SOUNDS: rows " + rows + ", door types checked " + doorChecks + ", sound type checks " + soundChecks
            + ", sound type differs from the set's: " + soundMismatch);
        // The by-name match is only trusted when the block's sound type is its set's; any difference fails the dump.
        if (!soundMismatch.isEmpty()) throw new IllegalStateException("sound type differs from the matched set's: " + soundMismatch);
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

    // The survival probe's world: a mutable map of positions to states (everything unset is air) behind a proxy of
    // the public LevelReader interface. Light answers are the field below; any method not listed fails the dump.
    static final java.util.Map<net.minecraft.core.BlockPos, net.minecraft.world.level.block.state.BlockState> survivalWorld = new java.util.HashMap<>();
    static int survivalLight = 15;
    static Object registryAccess;

    static net.minecraft.world.level.LevelReader survivalLevel() {
        var air = net.minecraft.world.level.block.Blocks.AIR.defaultBlockState();
        var type = net.minecraft.world.level.LevelReader.class;
        return (net.minecraft.world.level.LevelReader) java.lang.reflect.Proxy.newProxyInstance(type.getClassLoader(),
            new Class<?>[] {type}, (proxy, method, a) -> switch (method.getName()) {
                case "getBlockState" -> survivalWorld.getOrDefault(a[0], air);
                case "getFluidState" -> survivalWorld.getOrDefault(a[0], air).getFluidState();
                case "getBlockEntity" -> null;
                case "isClientSide" -> false;
                case "getMinY" -> -64;
                case "getHeight" -> {
                    if (a == null || a.length == 0) yield 384;
                    throw new UnsupportedOperationException(method.toString());
                }
                case "getSeaLevel" -> 63;
                case "getSkyDarken" -> 0;
                case "getRawBrightness", "getMaxLocalRawBrightness", "getBrightness" -> survivalLight;
                case "canSeeSky" -> survivalLight >= 15;
                case "enabledFeatures" -> FeatureFlags.DEFAULT_FLAGS;
                case "registryAccess" -> registryAccess;
                case "hashCode" -> System.identityHashCode(proxy);
                case "equals" -> proxy == a[0];
                case "toString" -> "survival probe world";
                default -> {
                    if (method.isDefault()) yield java.lang.reflect.InvocationHandler.invokeDefault(proxy, method, a);
                    throw new UnsupportedOperationException(method.toString());
                }
            });
    }

    // Plant survival matrix (plant_survival.json). Subjects: every state of every block of the frozen base list
    // (tools/survival_base_ids.txt), in its order. Candidates: air, then the same states. For a subject S at the origin
    // cell and a direction D, the set of candidates X for which S.canSurvive is true with X at the neighbour in
    // direction D and air elsewhere (light 15). Phase A2: below only, water sources around the support cell. Phase A3:
    // below only, light 0. Identical sets share a signature id, numbered in order of first appearance (0 = the empty set).
    static void dumpSurvival(Path out, Path baseIdsFile) throws Exception {
        var ids = Files.readAllLines(baseIdsFile).stream().filter(s -> !s.isBlank()).map(String::trim).toList();
        survivalMatrix(out.resolve("plant_survival.json"), ids, false, "PLANT SURVIVAL", "Per subject state S (every state of every imported block) and per probe, the set of candidate states X (air plus every state of every imported block) for which ");
    }

    // family_survival.json: the same probes over every imported block (the full ids list), subjects narrowed to the
    // rule-bearing states (ruleBearing below), candidates air plus every imported state.
    static void dumpFamilySurvival(Path out, Path idsFile) throws Exception {
        var ids = Files.readAllLines(idsFile).stream().filter(s -> !s.isBlank()).map(String::trim).toList();
        survivalMatrix(out.resolve("family_survival.json"), ids, true, "FAMILY SURVIVAL", "Per subject state S (every rule-bearing state of every imported block: a state for which "
            + "S.canSurvive is false in a world holding only S, or holding only S and a stone block at one of its six neighbours; "
            + "every other state's row is null, and blocks with no rule-bearing state are absent) and per probe, the set of candidate "
            + "states X (air plus every state of every imported block) for which ");
    }

    // A state with a survival rule: it fails alone (air on all sides), or next to a full stone cube on some side
    // (farmland and dirt path under a solid block).
    static boolean ruleBearing(net.minecraft.world.level.LevelReader level, net.minecraft.core.BlockPos at,
            net.minecraft.world.level.block.state.BlockState s) {
        var stone = net.minecraft.world.level.block.Blocks.STONE.defaultBlockState();
        survivalWorld.clear();
        survivalLight = 15;
        survivalWorld.put(at, s);
        boolean rule = !s.canSurvive(level, at);
        for (var d : net.minecraft.core.Direction.values()) {
            if (rule) break;
            survivalWorld.put(at.relative(d), stone);
            rule = !s.canSurvive(level, at);
            survivalWorld.remove(at.relative(d));
        }
        survivalWorld.clear();
        return rule;
    }

    static void survivalMatrix(Path file, java.util.List<String> ids, boolean ruleOnly, String label, String notePrefix) throws Exception {
        long started = System.nanoTime();
        var cands = new java.util.ArrayList<net.minecraft.world.level.block.state.BlockState>();
        var blockOrder = new java.util.ArrayList<String>();
        var blockRanges = new java.util.ArrayList<int[]>();
        var blocks = new java.util.ArrayList<Block>();
        blockOrder.add("air");
        blocks.add(net.minecraft.world.level.block.Blocks.AIR);
        for (String id : ids) {
            Block b = BuiltInRegistries.BLOCK.getValue(net.minecraft.resources.Identifier.withDefaultNamespace(id));
            if (b == net.minecraft.world.level.block.Blocks.AIR) throw new IllegalStateException("unknown block " + id);
            blockOrder.add(id);
            blocks.add(b);
        }
        for (Block b : blocks) {
            int start = cands.size();
            cands.addAll(b.getStateDefinition().getPossibleStates());
            blockRanges.add(new int[] {start, cands.size()});
        }
        var dirs = new net.minecraft.core.Direction[] {net.minecraft.core.Direction.WEST, net.minecraft.core.Direction.EAST,
            net.minecraft.core.Direction.NORTH, net.minecraft.core.Direction.SOUTH, net.minecraft.core.Direction.DOWN,
            net.minecraft.core.Direction.UP};
        var level = survivalLevel();
        var P = new net.minecraft.core.BlockPos(0, 64, 0);
        var water = net.minecraft.world.level.block.Blocks.WATER.defaultBlockState();
        var sigs = new java.util.LinkedHashMap<java.util.BitSet, Integer>();
        var sigList = new java.util.ArrayList<java.util.BitSet>();
        sigs.put(new java.util.BitSet(), 0);
        sigList.add(new java.util.BitSet());
        int subjectCount = 0;
        StringBuilder subjects = new StringBuilder();
        for (int bi = 1; bi < blocks.size(); bi++) {
            int[] range = blockRanges.get(bi);
            StringBuilder rows = new StringBuilder();
            boolean anyRule = false;
            for (int si = range[0]; si < range[1]; si++) {
                var s = cands.get(si);
                if (ruleOnly && !ruleBearing(level, P, s)) {
                    rows.append(rows.length() > 0 ? "," : "").append("null");
                    continue;
                }
                anyRule = true;
                subjectCount++;
                int[] row = new int[8];
                for (int phase = 0; phase < 8; phase++) {
                    var d = phase < 6 ? dirs[phase] : net.minecraft.core.Direction.DOWN;
                    var n = P.relative(d);
                    survivalWorld.clear();
                    survivalWorld.put(P, s);
                    if (phase == 6) for (var h : net.minecraft.core.Direction.Plane.HORIZONTAL) survivalWorld.put(n.relative(h), water);
                    survivalLight = phase == 7 ? 0 : 15;
                    var set = new java.util.BitSet(cands.size());
                    for (int ci = 0; ci < cands.size(); ci++) {
                        survivalWorld.put(n, cands.get(ci));
                        if (s.canSurvive(level, P)) set.set(ci);
                    }
                    Integer sig = sigs.get(set);
                    if (sig == null) { sig = sigList.size(); sigs.put(set, sig); sigList.add(set); }
                    row[phase] = sig;
                }
                survivalWorld.clear();
                survivalLight = 15;
                rows.append(rows.length() > 0 ? "," : "").append("[");
                for (int i = 0; i < 8; i++) rows.append(i > 0 ? "," : "").append(row[i]);
                rows.append("]");
            }
            if (ruleOnly && bi % 100 == 0)
                System.out.println(label + ": block " + bi + " of " + (blocks.size() - 1) + ", subjects " + subjectCount + ", seconds "
                    + (System.nanoTime() - started) / 1_000_000_000.0);
            if (!ruleOnly || anyRule)
                subjects.append(subjects.length() > 0 ? ",\n " : "").append(q(blockOrder.get(bi))).append(":[").append(rows).append("]");
        }
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(file))) {
            w.print("{\"note\":" + q(notePrefix
                + "S.canSurvive(level, P) is true in a world holding only S at P and X at the probed neighbour cell, air elsewhere, "
                + "block light and sky light 15. Rows are [west, east, north, south, down, up, down_water, down_dark]: the first six "
                + "put X at P + that direction; down_water puts X below with water sources at the four horizontal neighbours of that "
                + "support cell (sugar cane's rule); down_dark puts X below with light 0 (the mushrooms' rule). Each entry is a "
                + "signature id into signatures; identical sets share one, numbered by first appearance, 0 = no candidate. A "
                + "signature maps a block id to \"all\" (every state of the block passes) or to the ascending state indices within "
                + "that block (its state order in block_states.json, 0-based); blocks with no passing state are absent; air is "
                + "the block id \"air\". Subjects are listed by block id in ids order, one entry per state in state order.")
                + ",\"face_order\":[\"west\",\"east\",\"north\",\"south\",\"down\",\"up\"],\"light\":15,\"signatures\":[");
            for (int i = 0; i < sigList.size(); i++) {
                var set = sigList.get(i);
                StringBuilder line = new StringBuilder();
                for (int bi = 0; bi < blocks.size(); bi++) {
                    int[] range = blockRanges.get(bi);
                    int count = set.get(range[0], range[1]).cardinality();
                    if (count == 0) continue;
                    line.append(line.length() > 0 ? "," : "").append(q(blockOrder.get(bi))).append(":");
                    if (count == range[1] - range[0]) line.append("\"all\"");
                    else {
                        line.append("[");
                        boolean first = true;
                        for (int ci = set.nextSetBit(range[0]); ci >= 0 && ci < range[1]; ci = set.nextSetBit(ci + 1)) {
                            line.append(first ? "" : ",").append(ci - range[0]);
                            first = false;
                        }
                        line.append("]");
                    }
                }
                w.print((i > 0 ? ",\n " : "\n ") + "{" + line + "}");
            }
            w.print("\n],\"subjects\":{\n " + subjects + "\n}}\n");
        }
        System.out.println(label + ": candidates " + cands.size() + ", subjects " + subjectCount + ", signatures " + sigList.size()
            + ", seconds " + (System.nanoTime() - started) / 1_000_000_000.0);
    }
}
