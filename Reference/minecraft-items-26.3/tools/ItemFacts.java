import com.google.gson.JsonParser;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.chat.Component;
import net.minecraft.network.chat.Style;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.*;

/** Black-box facts for Reference/minecraft-items-26.3: the 26.3 server jar's own ItemStack answers, through its
 * public API only. Writes facts/durability_bar.json (isBarVisible, getBarWidth and getBarColor at every damage
 * value of every max damage among the reference's items) and facts/tooltips.json (getTooltipLines of each item,
 * as styled text runs). Usage: java -cp <out>;<classpath> ItemFacts <reference folder>; run it outside the project,
 * since the game leaves a logs/ folder in the working directory. */
public class ItemFacts {
    static String q(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 0x20 || c > 0x7e) b.append(String.format("\\u%04x", (int) c));
            else b.append(c);
        }
        return b.append('"').toString();
    }

    static String line(Component component) {
        List<String> runs = new ArrayList<>();
        component.visit((Style style, String text) -> {
            if (!text.isEmpty())
                runs.add("[" + q(text) + ", " + (style.getColor() == null ? "null" : q(style.getColor().serialize()))
                    + "]");
            return Optional.empty();
        }, Style.EMPTY);
        return "[" + String.join(", ", runs) + "]";
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        // Load the vanilla data pack the way a dedicated server starts, so item components and tags are bound.
        var repo = net.minecraft.server.packs.repository.ServerPacksSource.createVanillaTrustedRepository();
        repo.reload();
        var packs = new net.minecraft.server.WorldLoader.PackConfig(
            repo, net.minecraft.world.level.WorldDataConfiguration.DEFAULT, false, true);
        var init = new net.minecraft.server.WorldLoader.InitConfig(packs,
            net.minecraft.commands.Commands.CommandSelection.DEDICATED,
            net.minecraft.server.permissions.PermissionSet.ALL_PERMISSIONS);
        net.minecraft.core.LayeredRegistryAccess<net.minecraft.server.RegistryLayer> layers =
            net.minecraft.server.WorldLoader.load(init,
                ctx -> new net.minecraft.server.WorldLoader.DataLoadOutput<Boolean>(Boolean.TRUE,
                    ctx.datapackDimensions()),
                (net.minecraft.server.WorldLoader.ResultFactory<Boolean,
                    net.minecraft.core.LayeredRegistryAccess<net.minecraft.server.RegistryLayer>>)
                    (resources, reloadable, registries, cookie) -> {
                        reloadable.updateComponentsAndStaticRegistryTags();
                        return registries;
                    },
                net.minecraft.util.Util.backgroundExecutor(), Runnable::run).get();
        net.minecraft.core.HolderLookup.Provider lookup = layers.compositeAccess();

        List<String> ids = new ArrayList<>();
        for (var e : JsonParser.parseString(Files.readString(root.resolve("items.json"))).getAsJsonObject()
                 .getAsJsonArray("items"))
            ids.add(e.getAsJsonObject().get("id").getAsString());
        Map<String, Item> items = new HashMap<>();
        for (Item item : BuiltInRegistries.ITEM)
            items.put(BuiltInRegistries.ITEM.getKey(item).getPath(), item);

        // Durability bar: every damage value 0..max of each distinct max damage, on the first item that has it.
        TreeMap<Integer, List<String>> byMax = new TreeMap<>();
        for (String id : ids) {
            int max = new ItemStack(items.get(id)).getMaxDamage();
            if (max > 0) byMax.computeIfAbsent(max, k -> new ArrayList<>()).add(id);
        }
        List<String> rows = new ArrayList<>();
        for (var entry : byMax.entrySet()) {
            int max = entry.getKey();
            StringBuilder visible = new StringBuilder(), width = new StringBuilder(), color = new StringBuilder();
            for (String id : entry.getValue())
                for (int damage = 0; damage <= max; ++damage) {
                    ItemStack stack = new ItemStack(items.get(id));
                    stack.setDamageValue(damage);
                    String v = stack.isBarVisible() ? "1" : "0", w = String.valueOf(stack.getBarWidth()),
                        c = q(String.format("%06x", stack.getBarColor() & 0xffffff));
                    if (id.equals(entry.getValue().get(0))) {
                        visible.append(damage == 0 ? "" : ", ").append(v);
                        width.append(damage == 0 ? "" : ", ").append(w);
                        color.append(damage == 0 ? "" : ", ").append(c);
                    }
                    else {
                        // Every item with this max damage must answer the same: the bar depends on the numbers only.
                        ItemStack first = new ItemStack(items.get(entry.getValue().get(0)));
                        first.setDamageValue(damage);
                        if (first.getBarWidth() != stack.getBarWidth() || first.getBarColor() != stack.getBarColor()
                            || first.isBarVisible() != stack.isBarVisible())
                            throw new IllegalStateException("bar differs between items: " + id);
                    }
                }
            List<String> names = new ArrayList<>();
            for (String id : entry.getValue()) names.add(q(id));
            rows.add("  {\"max_damage\": " + max + ", \"items\": [" + String.join(", ", names) + "],\n   \"visible\": ["
                + visible + "],\n   \"width\": [" + width + "],\n   \"color\": [" + color + "]}");
        }
        Files.writeString(root.resolve("facts/durability_bar.json"),
            "{\n \"source\": \"26.3 server jar, ItemStack.setDamageValue(d) then isBarVisible(), getBarWidth(), "
                + "getBarColor() (RGB), for d = 0..max_damage; tools/ItemFacts.java\",\n \"maxima\": [\n"
                + String.join(",\n", rows) + "\n ]\n}\n",
            StandardCharsets.UTF_8);

        // Tooltips: each item's lines as the creative inventory asks for them (TooltipFlag.NORMAL.asCreative()),
        // without a player. A player adds its base attribute values to the base modifiers; that needs a live
        // level, which a black-box run does not have, so those lines show the modifier amounts here.
        Item.TooltipContext context = Item.TooltipContext.of(lookup);
        List<String> tips = new ArrayList<>();
        for (String id : ids) {
            List<String> lines = new ArrayList<>();
            for (Component c : new ItemStack(items.get(id)).getTooltipLines(
                     context, (Player) null, TooltipFlag.NORMAL.asCreative()))
                lines.add(line(c));
            tips.add("  {\"id\": " + q(id) + ", \"lines\": [" + String.join(", ", lines) + "]}");
        }
        Files.writeString(root.resolve("facts/tooltips.json"),
            "{\n \"source\": \"26.3 server jar, ItemStack.getTooltipLines(Item.TooltipContext.of(registries), null, "
                + "TooltipFlag.NORMAL.asCreative()); each line is its text runs as [text, colour or null]; "
                + "tools/ItemFacts.java\",\n \"items\": [\n" + String.join(",\n", tips) + "\n ]\n}\n",
            StandardCharsets.UTF_8);
        System.out.println("ITEM_FACTS " + byMax.size() + " maxima, " + ids.size() + " tooltips");
        System.exit(0);
    }
}
