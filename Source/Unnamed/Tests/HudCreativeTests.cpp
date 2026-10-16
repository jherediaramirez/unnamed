#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Hud/HudCreative.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags CreativeTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // The model over the project's items: every block item and the 26.3 items of DA_Items.
    bool MakeModel(FHudCreativeModel& Model, FFoundationRules& Rules)
    {
        const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
        if (!Registry->GetAssets()) return false;
        UFoundationInteractionSettings::Get().BuildRules(Rules);
        TMap<FName, FHudCreativeItem> Items;
        for (const FFoundationItemDefinition& Item : Rules.Items)
        {
            FHudCreativeItem& Entry = Items.Add(Item.Id);
            Entry.Name = Item.DisplayName;
            Entry.MaxStack = Item.MaxStack;
            if (Item.PlacesBlock == TEXT("oak_log") || Item.PlacesBlock == TEXT("birch_log"))
                Entry.Tags.Add(TEXT("logs"));
        }
        Model.Build(Registry->GetAssets()->CreativeTabs, Items);
        return true;
    }

    int32 FindTab(const FHudCreativeModel& Model, const TCHAR* Id)
    {
        return Model.GetTabs().IndexOfByPredicate([Id](const FHudCreativeModel::FTab& Tab) { return Tab.Id == Id; });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudCreativeTabsTest, "Unnamed.Hud.Creative.Tabs", CreativeTestFlags)

bool FHudCreativeTabsTest::RunTest(const FString&)
{
    FHudCreativeModel Model;
    FFoundationRules Rules;
    if (!TestTrue(TEXT("block registry asset"), MakeModel(Model, Rules))) return false;
    // Empty category tabs hide (shouldDisplay); hotbar, search and inventory always show. DA_Items's 146 items fill
    // Tools & Utilities, Combat, Food & Drinks, Ingredients and Natural Blocks, so every tab shows here (spawn eggs:
    // the spawners).
    FHudCreativeModel Bare;
    Bare.Build(FFoundationBlockRegistry::GetDefault()->GetAssets()->CreativeTabs, {});
    TestEqual(TEXT("with no items, category tabs hide"), FindTab(Bare, TEXT("tools_and_utilities")), int32(INDEX_NONE));
    TestNotEqual(TEXT("with no items, search still shows"), FindTab(Bare, TEXT("search")), int32(INDEX_NONE));
    for (const TCHAR* Id :
        {TEXT("building_blocks"), TEXT("colored_blocks"), TEXT("natural_blocks"), TEXT("functional_blocks"),
            TEXT("redstone_blocks"), TEXT("hotbar"), TEXT("search"), TEXT("tools_and_utilities"), TEXT("combat"),
            TEXT("food_and_drinks"), TEXT("ingredients"), TEXT("spawn_eggs"), TEXT("op_blocks"), TEXT("inventory")})
        TestNotEqual(FString(TEXT("tab shown: ")) + Id, FindTab(Model, Id), int32(INDEX_NONE));
    if (FindTab(Model, TEXT("tools_and_utilities")) != INDEX_NONE && FindTab(Model, TEXT("combat")) != INDEX_NONE)
    {
        Model.SelectTab(FindTab(Model, TEXT("tools_and_utilities")));
        const TArray<FName>& Tools = Model.GetItems();
        TestTrue(TEXT("tools: 28 tools and 4 buckets (the milk bucket since survival 20a), wooden shovel first"),
            Tools.Num() == 32 && Tools[0] == TEXT("wooden_shovel") && Tools[1] == TEXT("wooden_pickaxe") &&
                Tools[4] == TEXT("stone_shovel"));
        Model.SelectTab(FindTab(Model, TEXT("combat")));
        // 26.3's combat tab keeps the snowball (index 68) after TNT (66), then the egg (69, survival 20a). After the
        // axes sits the shield (index 23, T2 19b), then the 29 armor pieces (T2 19a2): leather, copper, chainmail,
        // iron, golden, diamond and netherite sets, then the turtle helmet; after the egg the bow (73) and the arrow
        // (78, T2 19c) (56 held items, 54 before 19c, 24 before 19a2; creative_tabs.json "combat" filtered to DA_Items and DA_Blocks).
        TestTrue(TEXT("combat: swords, spears, axes, the shield, armor, TNT, the snowball, the egg, the bow, the arrow"),
            Model.GetItems().Num() == 56 && Model.GetItems()[0] == TEXT("wooden_sword") &&
                Model.GetItems()[3] == TEXT("iron_sword") && Model.GetItems()[7] == TEXT("wooden_spear") &&
                Model.GetItems()[20] == TEXT("netherite_axe") && Model.GetItems()[21] == TEXT("shield") &&
                Model.GetItems()[22] == TEXT("leather_helmet") &&
                Model.GetItems()[26] == TEXT("copper_helmet") && Model.GetItems()[30] == TEXT("chainmail_helmet") &&
                Model.GetItems()[49] == TEXT("netherite_boots") && Model.GetItems()[50] == TEXT("turtle_helmet") &&
                Model.GetItems()[51] == TEXT("tnt") && Model.GetItems()[52] == TEXT("snowball") &&
                Model.GetItems()[53] == TEXT("egg") && Model.GetItems()[54] == TEXT("bow") &&
                Model.GetItems().Last() == TEXT("arrow"));
    }
    // 26.3 order within a tab, skipping items that do not exist here. With B1's families the whole oak family is here:
    // the first 13 of building_blocks' display list in Reference/minecraft-blocks-26.3/facts/creative_tabs.json.
    Model.SelectTab(FindTab(Model, TEXT("building_blocks")));
    const TArray<FName>& Items = Model.GetItems();
    const TArray<FName> OakFamily = {TEXT("oak_log"), TEXT("oak_wood"), TEXT("stripped_oak_log"),
        TEXT("stripped_oak_wood"), TEXT("oak_planks"), TEXT("oak_stairs"), TEXT("oak_slab"), TEXT("oak_fence"),
        TEXT("oak_fence_gate"), TEXT("oak_door"), TEXT("oak_trapdoor"), TEXT("oak_pressure_plate"), TEXT("oak_button")};
    TestTrue(TEXT("building blocks start with the oak family"),
        Items.Num() > OakFamily.Num() && TArray<FName>(Items.GetData(), OakFamily.Num()) == OakFamily &&
            Items[OakFamily.Num()] == TEXT("spruce_log"));
    // Layout: tab and slot positions from the 26.3 screen.
    const FHudCreativeModel::FTab& Search = Model.GetTabs()[FindTab(Model, TEXT("search"))];
    const FHudCreativeModel::FTab& Inventory = Model.GetTabs()[FindTab(Model, TEXT("inventory"))];
    TestEqual(TEXT("search tab top right"), FHudCreativeModel::GetTabPosition(Search), FIntPoint(169, -28));
    TestEqual(TEXT("inventory tab bottom right"), FHudCreativeModel::GetTabPosition(Inventory), FIntPoint(169, 132));
    TestEqual(TEXT("building tab top left"),
        FHudCreativeModel::GetTabPosition(Model.GetTabs()[FindTab(Model, TEXT("building_blocks"))]), FIntPoint(0, -28));
    TestTrue(TEXT("grid slot 0 at (9, 18), hovered from one pixel out"),
        Model.HitTest({9, 18}).Kind == EHudCreativeHit::Grid && Model.HitTest({8, 17}).Index == 0);
    TestTrue(TEXT("hotbar slot 3"),
        Model.HitTest({9 + 54 + 4, 115}).Kind == EHudCreativeHit::Hotbar &&
            Model.HitTest({9 + 54 + 4, 115}).Index == 3);
    TestEqual(TEXT("scroller"), Model.HitTest({176, 40}).Kind, EHudCreativeHit::Scroller);
    TestEqual(TEXT("outside"), Model.HitTest({-5, 50}).Kind, EHudCreativeHit::Outside);
    TestEqual(TEXT("a tab"), Model.HitTest({5, -20}).Kind, EHudCreativeHit::Tab);
    // Scrolling: one row per notch over the rows past the first five.
    TestTrue(TEXT("building blocks scroll"), Model.CanScroll());
    Model.ScrollRows(1);
    TestEqual(TEXT("one row down"), Model.GetFirstRow(), 1);
    TestEqual(TEXT("grid shows the second row"), Model.GetGridItem(0), Items[9]);
    // Search: names and ids contain the text; "#" matches tags.
    Model.SelectTab(FindTab(Model, TEXT("search")));
    Model.SetSearch(TEXT("Stained Glass"));
    // The 16 stained glass blocks and, with B1, their 16 panes.
    TestTrue(TEXT("16 stained glass and 16 panes"),
        Model.GetItems().Num() == 32 && Model.GetItems().Contains(TEXT("red_stained_glass")) &&
            Model.GetItems().Contains(TEXT("red_stained_glass_pane")));
    Model.SetSearch(TEXT("#logs"));
    TestTrue(
        TEXT("tag search"), Model.GetItems().Contains(TEXT("oak_log")) && !Model.GetItems().Contains(TEXT("stone")));
    Model.SetSearch(TEXT(""));
    TestTrue(TEXT("empty search lists every item once"), Model.GetItems().Num() >= 520);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudCreativeClicksTest, "Unnamed.Hud.Creative.Clicks", CreativeTestFlags)

bool FHudCreativeClicksTest::RunTest(const FString&)
{
    FHudCreativeModel Model;
    FFoundationRules Rules;
    if (!TestTrue(TEXT("block registry asset"), MakeModel(Model, Rules))) return false;
    Model.SelectTab(FindTab(Model, TEXT("building_blocks")));
    TArray<FHudCreativeStack> Inventory;
    Inventory.SetNum(FFoundationInventory::Slots);
    const FHudCreativeHit Log{EHudCreativeHit::Grid, 0}, Wood{EHudCreativeHit::Grid, 1};
    // The picker (slotClicked): one item, +1, shift fills, right takes one away, elsewhere deletes.
    Model.Click(Log, 0, false, Inventory);
    TestEqual(TEXT("left click picks one"), Model.Carried, FHudCreativeStack{TEXT("oak_log"), 1});
    Model.Click(Log, 0, false, Inventory);
    TestEqual(TEXT("again adds one"), Model.Carried.Count, 2);
    Model.Click(Log, 0, true, Inventory);
    TestEqual(TEXT("shift fills the stack"), Model.Carried.Count, 64);
    Model.Click(Log, 1, false, Inventory);
    TestEqual(TEXT("right takes one away"), Model.Carried.Count, 63);
    Model.Click(Wood, 0, false, Inventory);
    TestTrue(TEXT("left on another item deletes the carried stack"), Model.Carried.IsEmpty());
    Model.Click(Wood, 2, false, Inventory);
    TestEqual(TEXT("middle click: a full stack"), Model.Carried, FHudCreativeStack{TEXT("oak_wood"), 64});
    // Hotbar slots are ordinary container slots.
    TArray<FHudCreativeWrite> Writes = Model.Click({EHudCreativeHit::Hotbar, 2}, 1, false, Inventory);
    TestTrue(TEXT("right places one"),
        Writes.Num() == 1 && Writes[0].Slot == 2 && Writes[0].Stack.Count == 1 && Model.Carried.Count == 63);
    Inventory[2] = Writes[0].Stack;
    Writes = Model.Click({EHudCreativeHit::Hotbar, 2}, 0, false, Inventory);
    TestTrue(TEXT("left merges the rest"), Writes.Num() == 1 && Writes[0].Stack.Count == 64 && Model.Carried.IsEmpty());
    Inventory[2] = Writes[0].Stack;
    Writes = Model.Click({EHudCreativeHit::Hotbar, 2}, 0, true, Inventory);
    TestTrue(TEXT("shift-click in the picker tabs deletes"), Writes.Num() == 1 && Writes[0].Stack.IsEmpty());
    Inventory[2] = {};
    // Number keys over the picker put a full stack in that hotbar slot.
    Writes = Model.PressNumber(Log, 4, Inventory);
    TestTrue(TEXT("number key"),
        Writes.Num() == 1 && Writes[0].Slot == 4 && Writes[0].Stack == FHudCreativeStack{TEXT("oak_log"), 64});
    // A worn tool keeps its damage through right-click moves, as through a left-click swap.
    const FHudCreativeStack Pickaxe{TEXT("iron_pickaxe"), 1, 37};
    Inventory[5] = Pickaxe;
    Writes = Model.Click({EHudCreativeHit::Hotbar, 5}, 1, false, Inventory);
    TestTrue(TEXT("right-pick carries the tool with its damage"),
        Writes.Num() == 1 && Writes[0].Stack.IsEmpty() && Model.Carried == Pickaxe);
    Inventory[5] = {};
    Writes = Model.Click({EHudCreativeHit::Hotbar, 5}, 1, false, Inventory);
    TestTrue(TEXT("right-place writes the damage back"),
        Writes.Num() == 1 && Writes[0].Stack == Pickaxe && Model.Carried.IsEmpty());
    Inventory[5] = Pickaxe;
    Writes = Model.Click({EHudCreativeHit::Hotbar, 5}, 0, false, Inventory);
    TestTrue(TEXT("left-pick carries the tool with its damage"), Writes.Num() == 1 && Model.Carried == Pickaxe);
    Inventory[5] = {};
    Writes = Model.Click({EHudCreativeHit::Hotbar, 6}, 1, false, Inventory);
    TestTrue(TEXT("then right-place into an empty slot writes the damage"),
        Writes.Num() == 1 && Writes[0].Slot == 6 && Writes[0].Stack == Pickaxe && Model.Carried.IsEmpty());
    Inventory[6] = {};
    // A stackable item still splits in half.
    Inventory[5] = {TEXT("dirt"), 64};
    Writes = Model.Click({EHudCreativeHit::Hotbar, 5}, 1, false, Inventory);
    TestTrue(TEXT("right-pick takes half a stack"),
        Writes.Num() == 1 && Writes[0].Stack == FHudCreativeStack{TEXT("dirt"), 32} &&
            Model.Carried == FHudCreativeStack{TEXT("dirt"), 32});
    Model.Carried = {};
    Inventory[5] = {};
    // The survival tab: the bin deletes the carried stack; shift empties everything.
    Model.SelectTab(FindTab(Model, TEXT("inventory")));
    Model.Carried = {TEXT("stone"), 5};
    Inventory[0] = {TEXT("stone"), 64};
    Inventory[FFoundationInventory::OffhandSlot] = {TEXT("glass"), 3};
    Model.Click({EHudCreativeHit::Destroy, 0}, 0, false, Inventory);
    TestTrue(TEXT("bin"), Model.Carried.IsEmpty());
    Writes = Model.Click({EHudCreativeHit::Destroy, 0}, 0, true, Inventory);
    TestEqual(TEXT("shift on the bin clears the hotbar and offhand"), Writes.Num(), 2);
    TestEqual(TEXT("offhand slot is the survival tab's"), Model.HitTest({36, 21}).Kind, EHudCreativeHit::Offhand);
    // The survival tab's main and armor slots are real with the full inventory: clicks write their Inventory numbers.
    Inventory[0] = {TEXT("stone"), 64};
    Writes = Model.Click({EHudCreativeHit::Hotbar, 0}, 0, true, Inventory);
    TestTrue(TEXT("shift in the survival tab moves hotbar to main"),
        Writes.Num() == 2 && Writes[0].Slot == 0 && Writes[0].Stack.IsEmpty() && Writes[1].Slot == 9 &&
            Writes[1].Stack == FHudCreativeStack{TEXT("stone"), 64});
    Inventory[0] = {};
    Inventory[9] = {TEXT("stone"), 64};
    Writes = Model.Click({EHudCreativeHit::Inventory, 0}, 1, false, Inventory);
    TestTrue(TEXT("right on a main slot takes half"),
        Writes.Num() == 1 && Writes[0].Slot == 9 && Writes[0].Stack.Count == 32 &&
            Model.Carried == FHudCreativeStack{TEXT("stone"), 32});
    Inventory[9] = Writes[0].Stack;
    Writes = Model.Click({EHudCreativeHit::Armor, 0}, 0, false, Inventory);
    TestTrue(TEXT("the head slot refuses stone"), Writes.IsEmpty() && Model.Carried.Count == 32);
    Inventory[3] = {TEXT("dirt"), 5};
    Writes = Model.PressNumber({EHudCreativeHit::Inventory, 0}, 3, Inventory);
    TestTrue(TEXT("a number key over a main slot swaps with the hotbar"),
        Writes.Num() == 2 && Writes[0].Slot == 3 && Writes[0].Stack == FHudCreativeStack{TEXT("stone"), 32} &&
            Writes[1].Slot == 9 && Writes[1].Stack == FHudCreativeStack{TEXT("dirt"), 5});
    Inventory[3] = {};
    // The switch at 0: the main and armor slots stay inert, as before step 5a.
    Model.bFullInventory = false;
    Writes = Model.Click({EHudCreativeHit::Inventory, 0}, 0, false, Inventory);
    TestTrue(TEXT("switch 0: a main slot does nothing"), Writes.IsEmpty() && Model.Carried.Count == 32);
    TestTrue(TEXT("switch 0: no number key over a main or armor slot"),
        Model.PressNumber({EHudCreativeHit::Inventory, 0}, 3, Inventory).IsEmpty() &&
            Model.PressNumber({EHudCreativeHit::Armor, 0}, 3, Inventory).IsEmpty());
    Inventory[2] = {TEXT("dirt"), 5};
    TestTrue(TEXT("switch 0: shift and middle on the hotbar do nothing in the survival tab"),
        Model.Click({EHudCreativeHit::Hotbar, 2}, 0, true, Inventory).IsEmpty() &&
            Model.Click({EHudCreativeHit::Hotbar, 2}, 2, false, Inventory).IsEmpty() && Model.Carried.Count == 32);
    Inventory[2] = {};
    Writes = Model.Click({EHudCreativeHit::Hotbar, 1}, 0, false, Inventory);
    TestTrue(TEXT("switch 0: the hotbar still takes the cursor"),
        Writes.Num() == 1 && Writes[0].Slot == 1 && Model.Carried.IsEmpty());
    Model.bFullInventory = true;
    Model.Carried = {};
    // The armor slots run head (index 0, Inventory 39) to feet (3, Inventory 36) and take only their own items,
    // through the item's Equipment.
    {
        FHudCreativeModel Armor;
        TMap<FName, FHudCreativeItem> ArmorItems;
        ArmorItems.Add(TEXT("stone")).MaxStack = 64;
        FHudCreativeItem& Helmet = ArmorItems.Add(TEXT("test_helmet"));
        Helmet.MaxStack = 1;
        Helmet.Equipment = EFoundationEquipmentSlot::Head;
        FHudCreativeItem& Boots = ArmorItems.Add(TEXT("test_boots"));
        Boots.MaxStack = 1;
        Boots.Equipment = EFoundationEquipmentSlot::Feet;
        Armor.Build(FFoundationBlockRegistry::GetDefault()->GetAssets()->CreativeTabs, ArmorItems);
        Armor.SelectTab(FindTab(Armor, TEXT("inventory")));
        TArray<FHudCreativeStack> Slots;
        Slots.SetNum(FFoundationInventory::Slots);
        Armor.Carried = {TEXT("test_helmet"), 1};
        Writes = Armor.Click({EHudCreativeHit::Armor, 3}, 0, false, Slots);
        TestTrue(TEXT("the feet slot refuses a helmet"), Writes.IsEmpty() && !Armor.Carried.IsEmpty());
        Writes = Armor.Click({EHudCreativeHit::Armor, 0}, 0, false, Slots);
        TestTrue(TEXT("a helmet goes on the head slot, Inventory 39"),
            Writes.Num() == 1 && Writes[0].Slot == 39 && Writes[0].Stack == FHudCreativeStack{TEXT("test_helmet"), 1});
        Armor.Carried = {TEXT("test_boots"), 1};
        Writes = Armor.Click({EHudCreativeHit::Armor, 3}, 0, false, Slots);
        TestTrue(TEXT("boots go on the feet slot, Inventory 36"), Writes.Num() == 1 && Writes[0].Slot == 36);
    }
    // Block icons: the gui display transform shows the top, east (left) and north (right) faces.
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    const TArray<HudBlockIcon::FQuad> Grass = HudBlockIcon::Build(*Registry, TEXT("grass_block"));
    TestEqual(TEXT("grass icon: three faces and two side overlays"), Grass.Num(), 5);
    if (Grass.Num() == 5)
    {
        TestEqual(TEXT("top tinted with the item's grass color #7CBD6B"), Grass[0].Color, FColor(0x7C, 0xBD, 0x6B));
        float Left = 16, Right = 0;
        for (const FVector2f& Corner : Grass[1].Corners)
            Left = FMath::Min(Left, Corner.X);
        for (const FVector2f& Corner : Grass[3].Corners)
            Right = FMath::Max(Right, Corner.X);
        TestTrue(TEXT("east face on the left, north on the right, within the slot"),
            Left > .5f && Left < 2 && Right > 14 && Right < 15.5f);
    }
    const TArray<HudBlockIcon::FQuad> Slab = HudBlockIcon::Build(*Registry, TEXT("oak_slab"));
    float SlabTop = 16;
    for (const FVector2f& Corner : Slab[0].Corners)
        SlabTop = FMath::Min(SlabTop, Corner.Y);
    const TArray<HudBlockIcon::FQuad> Planks = HudBlockIcon::Build(*Registry, TEXT("oak_planks"));
    float PlanksTop = 16;
    for (const FVector2f& Corner : Planks[0].Corners)
        PlanksTop = FMath::Min(PlanksTop, Corner.Y);
    TestTrue(TEXT("a slab icon is the bottom half"), SlabTop > PlanksTop + 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudCreativeAuthorityTest, "Unnamed.Hud.Creative.Authority", CreativeTestFlags)

bool FHudCreativeAuthorityTest::RunTest(const FString&)
{
    // The creative write goes through the authority like every inventory change.
    FFoundationAuthority A;
    UFoundationInteractionSettings::Get().BuildRules(A.Rules);
    A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
    const int32 Player = A.AddPlayer();
    A.Reset();
    FFoundationVitals Vitals;
    uint32 Sequence = 1;
    auto Write = [&](int32 Slot, FName Item, int32 Count)
    {
        FFoundationCommand Command;
        Command.SessionId = A.GetSessionId();
        Command.Sequence = Sequence++;
        Command.Type = EFoundationCommandType::SetCreativeSlot;
        Command.Slot = Slot;
        Command.CreativeItem = Item;
        Command.CreativeCount = Count;
        return A.Execute(Player, Command, Vitals);
    };
    const FFoundationInventory& Inventory = A.FindPlayer(Player)->Inventory;
    TestTrue(TEXT("hotbar write"),
        Write(4, TEXT("oak_slab"), 64).IsCommitted() &&
            A.Rules.GetItem(Inventory.Hotbar[4].Item).Id == TEXT("oak_slab") && Inventory.Hotbar[4].Count == 64);
    TestTrue(TEXT("offhand write"),
        Write(FFoundationInventory::OffhandSlot, TEXT("glass"), 3).IsCommitted() && Inventory.Offhand.Count == 3);
    TestTrue(TEXT("clear"), Write(4, NAME_None, 0).IsCommitted() && Inventory.Hotbar[4].IsEmpty());
    TestEqual(
        TEXT("the lab inventory has no main slots"), Write(9, TEXT("stone"), 1).Reason, EFoundationReason::InvalidSlot);
    TestEqual(
        TEXT("bad slot"), Write(FFoundationInventory::Slots, TEXT("stone"), 1).Reason, EFoundationReason::InvalidSlot);
    // The full inventory: every one of the 41 slots takes a creative write.
    A.Options.bFullInventory = true;
    TestTrue(TEXT("main write"), Write(35, TEXT("dirt"), 7).IsCommitted() && Inventory.Main[26].Count == 7);
    TestTrue(TEXT("armor write (creative writes are not filtered)"),
        Write(39, TEXT("glass"), 1).IsCommitted() && Inventory.Armor[3].Count == 1);
    TestEqual(TEXT("past the offhand"), Write(41, TEXT("stone"), 1).Reason, EFoundationReason::InvalidSlot);
    TestEqual(TEXT("over the stack size"), Write(0, TEXT("stone"), 65).Reason, EFoundationReason::NoItem);
    TestEqual(TEXT("unknown item"), Write(0, TEXT("dummy_block_item"), 1).Reason, EFoundationReason::NoItem);
    A.Options.bCreativeInventory = false;
    TestEqual(TEXT("only while the lab allows it"), Write(0, TEXT("stone"), 1).Reason, EFoundationReason::InvalidSlot);
    return true;
}
#endif
