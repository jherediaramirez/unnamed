import java.io.PrintWriter;
import java.nio.file.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.data.registries.VanillaRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.flag.FeatureFlags;
import net.minecraft.world.item.*;
import net.minecraft.world.level.block.Block;

// Dumps 26.3 creative tab contents and per-block hardness facts by running the game's own registries.
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
        System.out.println("FACTS DUMP OK");
        System.exit(0);
    }
}
