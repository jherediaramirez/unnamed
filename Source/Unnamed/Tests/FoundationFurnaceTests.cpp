#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationContainer.h"
#include "Foundation/FoundationContainers.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "HAL/IConsoleManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags FurnaceTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Block storage whose columns can be marked unloaded (a streamed world), as the item entity tests build it.
    class FFurnaceColumnStorage final : public IFoundationBlockStorage
    {
    public:
        TSet<FIntPoint> Unloaded;

        virtual FFoundationCell Get(const FIntVector& Cell) const override { return Inner.Get(Cell); }
        virtual void Set(const FIntVector& Cell, const FFoundationCell& Value) override { Inner.Set(Cell, Value); }
        virtual void Empty() override { Inner.Empty(); }
        virtual int32 GetSolidCount() const override { return Inner.GetSolidCount(); }
        virtual void ForEachSolid(const FIntVector& Min, const FIntVector& Max,
            TFunctionRef<void(const FIntVector&, const FFoundationCell&)> Visit) const override
        {
            Inner.ForEachSolid(Min, Max, Visit);
        }
        virtual SIZE_T GetAllocatedSize() const override { return Inner.GetAllocatedSize(); }
        virtual bool IsLoaded(const FIntVector& Cell) const override
        {
            return !Unloaded.Contains(
                FIntPoint(FoundationGrid::FloorDivide(Cell.X, 16), FoundationGrid::FloorDivide(Cell.Y, 16)));
        }

    private:
        FFoundationSectionStorage Inner;
    };

    // T1 step 8a's lab: the project's rules and blocks, the full inventory, block use and furnaces on, an empty
    // inventory, and the player's eye looking at a furnace.
    struct FFurnaceLab
    {
        using FEntity = FFoundationBlockEntity;
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        FFurnaceColumnStorage* Storage = nullptr;
        int32 Player = 0;
        int64 StepIndex = 0;
        FVector Eye = FVector(50, 50, 150);
        FIntVector Aimed = FIntVector(2, 0, 1);

        FFurnaceLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            TUniquePtr<FFurnaceColumnStorage> Columns = MakeUnique<FFurnaceColumnStorage>();
            Storage = Columns.Get();
            A.World.SetStorage(MoveTemp(Columns));
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            A.Options.bFullInventory = true;
            A.Options.bBlockUse = true;
            A.Options.bFurnace = true;
            Player = A.AddPlayer();
            A.Reset();
            I.PlayerId = Player;
            I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(TEXT("VanillaReference")), .05);
            I.Resync(A);
            for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
                Inv().SetSlot(Slot, {});
            Inv().Select(0);
            A.World.Set(Aimed, A.World.FindBlock(TEXT("furnace"))->GetDefaultState());
            Steps(1);
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }
        FFoundationInventory& Inv() { return P().Inventory; }
        int32 Item(const TCHAR* Id) const { return A.Rules.FindItem(Id); }
        FFoundationItemInstance Stack(const TCHAR* Id, int32 Count) { return Inv().MakeStack(Item(Id), Count); }

        // Count reference steps, aiming at the furnace.
        void Steps(int32 Count)
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                FFoundationPlayerFacts Facts;
                Facts.FeetCm = Eye - FVector(0, 0, 162);
                Facts.EyeCm = Eye;
                Facts.Aim = (FoundationGrid::CellBox(Aimed, A.World.OriginCm).GetCenter() - Eye).GetSafeNormal();
                Facts.Step = ++StepIndex;
                A.SetFacts(Player, Facts);
                A.Step();
            }
        }

        FFoundationCommand Make(EFoundationCommandType Type)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = I.NextSequence++;
            Command.Type = Type;
            Command.Hand = EFoundationHand::Main;
            Command.Item = Inv().GetHand(EFoundationHand::Main).GetRef();
            Command.Target = A.Resolve(P().Facts);
            return Command;
        }

        FFoundationResult Use()
        {
            FFoundationCommand Command = Make(EFoundationCommandType::UseOn);
            FIntVector Destination;
            EFoundationReason Reason;
            if (A.GetDestination(P(), EFoundationHand::Main, Command.Target, Destination, Reason))
                Command.Destination = Destination;
            return A.Execute(Player, Command, Vitals);
        }

        // Opens the furnace; the container id.
        int32 Open() { return Use().OpenedContainer; }

        FFoundationResult Click(int32 Container, int32 Slot, int32 Button, EFoundationClickType Type)
        {
            FFoundationCommand Command = Make(EFoundationCommandType::ContainerClick);
            Command.ContainerId = Container;
            Command.Slot = Slot;
            Command.Button = Button;
            Command.ClickType = Type;
            return A.Execute(Player, Command, Vitals);
        }

        FFoundationResult Close() { return A.Execute(Player, Make(EFoundationCommandType::ContainerClose), Vitals); }

        // The furnace's entity (made as its use does when there is none yet).
        FEntity& F() { return A.BlockEntities.FindOrAdd(Aimed, EFoundationBlockEntityKind::Furnace, TEXT("furnace")); }
        // Fills the furnace's slots directly (nullptr leaves one empty).
        void Load(const TCHAR* Input, int32 InputCount, const TCHAR* Fuel, int32 FuelCount)
        {
            F().Items[FEntity::FurnaceInput] = Input ? Stack(Input, InputCount) : FFoundationItemInstance();
            F().Items[FEntity::FurnaceFuel] = Fuel ? Stack(Fuel, FuelCount) : FFoundationItemInstance();
        }

        bool Holds(const FFoundationItemInstance& Slot, const TCHAR* Id, int32 Count) const
        {
            return Count == 0 ? Slot.IsEmpty() : !Slot.IsEmpty() && Slot.Item == Item(Id) && Slot.Count == Count;
        }

        // Mines the furnace with the bare hand until it breaks; whether it did.
        bool Break()
        {
            FFoundationResult R = A.Execute(Player, Make(EFoundationCommandType::MineStart), Vitals);
            for (int32 Step = 0; Step < 4000 && !R.bBroken && R.IsCommitted(); ++Step)
            {
                Steps(1);
                R = A.Execute(Player, Make(EFoundationCommandType::MineContinue), Vitals);
            }
            return R.bBroken;
        }

        // The points in orbs, plus those already taken but not claimed.
        int32 OrbTotal() const
        {
            int32 Total = A.FindPlayer(Player)->PendingExperience;
            for (const FFoundationExperienceOrb& Orb : A.GetExperienceOrbs())
                Total += Orb.Value * Orb.Count;
            return Total;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationFurnaceDataTest, "Unnamed.Foundation.Furnace.Data", FurnaceTestFlags)

bool FFoundationFurnaceDataTest::RunTest(const FString&)
{
    // The generated cooking data (Scripts/GenerateCookingData.py), resolved against the project's items: the value
    // report's fuel rows (survival.r3.fuels) and recipe rows (survival.r3.recipes).
    FFurnaceLab L;
    const FFoundationCooking& Cooking = L.A.GetCooking();
    AddInfo(FString::Printf(
        TEXT("cooking: %d recipes, %d fuels resolved"), Cooking.GetRecipeCount(), Cooking.GetFuelCount()));
    // Today 106 of the 116 recipes and 234 of the 347 fuels (8a2 appended charcoal, the nuggets, brick, nether
    // brick, the two dyes, dried kelp, resin brick and baked potato; 20a the four meats): the project still lacks
    // rabbit, fish, chorus fruit, armor, blaze rods, boats, signs and banners (DA_Items); more resolve as items are
    // added. 106 and 234 are this test's own AddInfo at 8a2 (C:\dev\sandbox\inv8a2\items-tests-1.log): floors, so new
    // items only raise them.
    TestTrue(TEXT("recipes resolve"), Cooking.GetRecipeCount() >= 106);
    TestTrue(TEXT("fuels resolve"), Cooking.GetFuelCount() >= 234);
    const struct
    {
        const TCHAR* Item;
        int32 Ticks;
    } Fuels[] = {{TEXT("coal"), 1600}, {TEXT("coal_block"), 16000}, {TEXT("lava_bucket"), 20000},
        {TEXT("dried_kelp_block"), 4001}, {TEXT("oak_log"), 300}, {TEXT("oak_planks"), 300}, {TEXT("oak_slab"), 150},
        {TEXT("stick"), 100}, {TEXT("oak_sapling"), 100}, {TEXT("bamboo"), 50}, {TEXT("charcoal"), 1600},
        {TEXT("cobblestone"), 0}, {TEXT("bucket"), 0}};
    for (const auto& [Item, Ticks] : Fuels)
        TestEqual(FString::Printf(TEXT("%s burns"), Item), Cooking.GetBurnTicks(L.Item(Item)), Ticks);
    const struct
    {
        const TCHAR* Input;
        const TCHAR* Result;
        float Xp;
    } Recipes[] = {{TEXT("raw_iron"), TEXT("iron_ingot"), .7f}, {TEXT("raw_gold"), TEXT("gold_ingot"), 1.f},
        {TEXT("cobblestone"), TEXT("stone"), .1f}, {TEXT("sand"), TEXT("glass"), .1f},
        {TEXT("raw_copper"), TEXT("copper_ingot"), .7f}, {TEXT("stone"), TEXT("smooth_stone"), .1f},
        {TEXT("oak_log"), TEXT("charcoal"), .15f}, {TEXT("clay_ball"), TEXT("brick"), .3f},
        {TEXT("potato"), TEXT("baked_potato"), .35f}};
    for (const auto& [Input, Result, Xp] : Recipes)
    {
        const FFoundationCookingRecipe* Recipe =
            Cooking.Get(Cooking.Find(EFoundationCookingKind::Smelting, L.Item(Input)));
        if (!TestNotNull(FString::Printf(TEXT("%s smelts"), Input), Recipe)) continue;
        TestEqual(FString::Printf(TEXT("%s makes %s"), Input, Result), Recipe->Result, L.Item(Result));
        TestEqual(FString::Printf(TEXT("%s: 200 ticks"), Input), Recipe->CookTicks, 200);
        TestEqual(FString::Printf(TEXT("%s: xp"), Input), Recipe->Experience, Xp);
    }
    TestEqual(TEXT("a bucket does not smelt"), Cooking.Find(EFoundationCookingKind::Smelting, L.Item(TEXT("bucket"))),
        int32(INDEX_NONE));

    // The furnace's menu (value-swaps.md, "FurnaceScreen"): 0 input, 1 fuel, 2 result, 3-29 main, 30-38 hotbar.
    const FFoundationMenuLayout& Furnace = FFoundationMenuLayout::Get(EFoundationMenuKind::Furnace);
    TestEqual(TEXT("furnace slots"), Furnace.Slots, 39);
    TestTrue(TEXT("container 0-2"), Furnace.IsContainer(0) && Furnace.IsContainer(2) && !Furnace.IsContainer(3));
    TestTrue(TEXT("no crafting result"), !Furnace.HasCraftResult() && !Furnace.IsCraft(0) && !Furnace.IsCraft(1));
    TestTrue(TEXT("main 3-29, hotbar 30-38"),
        Furnace.IsMain(3) && Furnace.IsMain(29) && Furnace.IsHotbar(30) && Furnace.IsHotbar(38) && !Furnace.IsMain(2));
    TestEqual(TEXT("hotbar 0 is furnace 30"), Furnace.ToMenuSlot(0), 30);
    TestEqual(TEXT("main 9 is furnace 3"), Furnace.ToMenuSlot(FFoundationInventory::FirstMain), 3);
    TestEqual(TEXT("no armor slot"), Furnace.ToMenuSlot(FFoundationInventory::FirstArmor), int32(INDEX_NONE));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationFurnaceTickTest, "Unnamed.Foundation.Furnace.Tick", FurnaceTestFlags)

bool FFoundationFurnaceTickTest::RunTest(const FString&)
{
    using FEntity = FFoundationBlockEntity;
    // Burn boundaries (survival.r3.furnace_ticks, furnace/coal): lit at tick 1, the first output at tick 200, eight
    // items by tick 1600, the first unlit tick 1601 with nothing left cooking.
    {
        FFurnaceLab L;
        L.Load(TEXT("raw_iron"), 9, TEXT("coal"), 1);
        L.Steps(1);
        TestTrue(TEXT("coal: lit at tick 1"), L.F().IsLit());
        TestEqual(TEXT("coal: the fuel used"), L.F().Items[FEntity::FurnaceFuel].Count, 0);
        TestEqual(TEXT("coal: 1600 fuel ticks"), L.F().LitDuration, 1600);
        TestEqual(TEXT("coal: cooking started"), L.F().CookProgress, 1);
        L.Steps(198);
        TestTrue(TEXT("coal: nothing at tick 199"), L.F().Items[FEntity::FurnaceResult].IsEmpty());
        L.Steps(1);
        TestTrue(
            TEXT("coal: first ingot at tick 200"), L.Holds(L.F().Items[FEntity::FurnaceResult], TEXT("iron_ingot"), 1));
        L.Steps(1400);
        TestTrue(
            TEXT("coal: 8 ingots at tick 1600"), L.Holds(L.F().Items[FEntity::FurnaceResult], TEXT("iron_ingot"), 8));
        TestTrue(TEXT("coal: still lit at tick 1600"), L.F().IsLit());
        L.Steps(1);
        TestFalse(TEXT("coal: unlit at tick 1601"), L.F().IsLit());
        TestEqual(TEXT("coal: nothing cooking at 1601"), L.F().CookProgress, 0);
        TestTrue(TEXT("coal: one raw iron left"), L.Holds(L.F().Items[FEntity::FurnaceInput], TEXT("raw_iron"), 1));
        TestEqual(TEXT("coal: 8 smelts counted"),
            L.F().RecipesUsed.FindRef(
                L.A.GetCooking().Find(EFoundationCookingKind::Smelting, L.Item(TEXT("raw_iron")))),
            8);
    }
    // Cooling (furnace/oak_planks): 300 fuel ticks make 1 item and leave 100; the first unlit tick (301) shows 98, then
    // -2 a tick to 0 (offsets 0-49), clamped there.
    {
        FFurnaceLab L;
        L.Load(TEXT("raw_iron"), 8, TEXT("oak_planks"), 1);
        L.Steps(300);
        TestTrue(TEXT("planks: 1 ingot"), L.Holds(L.F().Items[FEntity::FurnaceResult], TEXT("iron_ingot"), 1));
        TestEqual(TEXT("planks: 100 at tick 300"), L.F().CookProgress, 100);
        TestTrue(TEXT("planks: lit at 300"), L.F().IsLit());
        L.Steps(1);
        TestFalse(TEXT("planks: unlit at 301"), L.F().IsLit());
        TestEqual(TEXT("planks: offset 0 is 98"), L.F().CookProgress, 98);
        L.Steps(1);
        TestEqual(TEXT("planks: offset 1 is 96"), L.F().CookProgress, 96);
        L.Steps(47);
        TestEqual(TEXT("planks: offset 48 is 2"), L.F().CookProgress, 2);
        L.Steps(1);
        TestEqual(TEXT("planks: offset 49 is 0"), L.F().CookProgress, 0);
        L.Steps(5);
        TestEqual(TEXT("planks: clamped at 0"), L.F().CookProgress, 0);
    }
    // A lava bucket burns 20000 ticks and leaves its bucket in the fuel slot.
    {
        FFurnaceLab L;
        L.Load(TEXT("cobblestone"), 1, TEXT("lava_bucket"), 1);
        L.Steps(1);
        TestEqual(TEXT("lava: 20000 ticks"), L.F().LitDuration, 20000);
        TestTrue(TEXT("lava: the bucket left"), L.Holds(L.F().Items[FEntity::FurnaceFuel], TEXT("bucket"), 1));
    }
    // No fuel spent without something to cook; something that does not smelt stays unlit.
    {
        FFurnaceLab L;
        L.Load(nullptr, 0, TEXT("coal"), 3);
        L.Steps(20);
        TestTrue(TEXT("no input: unlit, coal kept"),
            !L.F().IsLit() && L.Holds(L.F().Items[FEntity::FurnaceFuel], TEXT("coal"), 3));
        L.Load(TEXT("stick"), 4, TEXT("coal"), 3);
        L.Steps(20);
        TestTrue(TEXT("stick does not smelt: unlit"), !L.F().IsLit());
    }
    // A full output stops the cooking (a lit furnace resets its progress) and keeps the input.
    {
        FFurnaceLab L;
        L.Load(TEXT("raw_iron"), 4, TEXT("coal"), 1);
        L.F().Items[FEntity::FurnaceResult] = L.Stack(TEXT("iron_ingot"), 64);
        L.Steps(10);
        TestTrue(TEXT("full output: never lit"), !L.F().IsLit());
        TestTrue(TEXT("full output: coal kept"), L.Holds(L.F().Items[FEntity::FurnaceFuel], TEXT("coal"), 1));
    }
    // foundation.Furnace 0: nothing ticks; the switch ships at 1.
    {
        FFurnaceLab L;
        L.A.Options.bFurnace = false;
        L.Load(TEXT("raw_iron"), 1, TEXT("coal"), 1);
        L.Steps(400);
        TestTrue(TEXT("off: unlit, nothing used"),
            !L.F().IsLit() && L.Holds(L.F().Items[FEntity::FurnaceFuel], TEXT("coal"), 1) &&
                L.F().Items[FEntity::FurnaceResult].IsEmpty());
        IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Furnace"));
        TestTrue(TEXT("foundation.Furnace exists and ships at 1"), Switch && Switch->GetInt() == 1);
    }
    // The store is keyed by cell: an unloaded column's furnace waits (no ticks) and keeps its slots and timers.
    {
        FFurnaceLab L;
        L.Load(TEXT("raw_iron"), 4, TEXT("coal"), 1);
        L.Steps(50);
        L.Storage->Unloaded.Add(FIntPoint(0, 0));
        L.Steps(500);
        TestTrue(TEXT("unloaded: kept"), L.A.BlockEntities.Find(L.Aimed) != nullptr);
        TestEqual(TEXT("unloaded: no ticks"), L.F().CookProgress, 50);
        L.Storage->Unloaded.Empty();
        L.Steps(150);
        TestTrue(TEXT("reloaded: ticks on to the first ingot"),
            L.Holds(L.F().Items[FEntity::FurnaceResult], TEXT("iron_ingot"), 1));
    }
    // A furnace replaced without a break leaves no entity behind, the switch off included (review A finding 1): a
    // furnace placed there later starts empty.
    {
        FFurnaceLab L;
        L.A.Options.bFurnace = false;
        L.Load(TEXT("raw_iron"), 4, TEXT("coal"), 1);
        L.A.World.Set(L.Aimed, L.A.World.FindBlock(TEXT("stone"))->GetDefaultState());
        L.Steps(1);
        TestNull(TEXT("replaced: entity dropped"), L.A.BlockEntities.Find(L.Aimed));
    }
    // A reset clears the store.
    {
        FFurnaceLab L;
        L.Load(TEXT("raw_iron"), 4, TEXT("coal"), 1);
        L.A.Reset();
        TestEqual(TEXT("reset: store empty"), L.A.BlockEntities.Num(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationFurnaceMenuTest, "Unnamed.Foundation.Furnace.Menu", FurnaceTestFlags)

bool FFoundationFurnaceMenuTest::RunTest(const FString&)
{
    using FEntity = FFoundationBlockEntity;
    // Right-click opens the furnace's menu over its block entity; it ticks with the menu open or closed.
    {
        FFurnaceLab L;
        L.A.BlockEntities.Clear();
        const FFoundationResult Result = L.Use();
        TestTrue(TEXT("opens"), Result.IsCommitted() && Result.OpenedMenu == EFoundationMenuKind::Furnace);
        TestNotNull(TEXT("its entity made"), L.A.BlockEntities.Find(L.Aimed));
        TestEqual(TEXT("39 slots"), L.A.GetOpenMenu(L.P()).Num(), 39);
        TestEqual(TEXT("no grid of its own"), L.P().OpenMenu.Grid.Num(), 0);
    }
    // Clicks: the input takes anything, the fuel slot only fuel (and one empty bucket), the result never takes.
    {
        FFurnaceLab L;
        const int32 Id = L.Open();
        L.Inv().SetSlot(0, L.Stack(TEXT("cobblestone"), 10));
        L.Inv().SetSlot(1, L.Stack(TEXT("coal"), 5));
        L.Inv().SetSlot(2, L.Stack(TEXT("bucket"), 3));
        L.Click(Id, 30, 0, EFoundationClickType::Pickup);
        TestTrue(TEXT("cobblestone refused by the fuel slot"),
            L.Click(Id, 1, 0, EFoundationClickType::Pickup).IsCommitted() &&
                L.F().Items[FEntity::FurnaceFuel].IsEmpty());
        TestTrue(TEXT("cobblestone refused by the result slot"),
            L.Click(Id, 2, 0, EFoundationClickType::Pickup).IsCommitted() &&
                L.F().Items[FEntity::FurnaceResult].IsEmpty());
        L.Click(Id, 0, 0, EFoundationClickType::Pickup);
        TestTrue(
            TEXT("cobblestone into the input"), L.Holds(L.F().Items[FEntity::FurnaceInput], TEXT("cobblestone"), 10));
        TestEqual(TEXT("input set: its cook time"), L.F().CookTotal, 200);
        L.Click(Id, 32, 0, EFoundationClickType::Pickup);
        L.Click(Id, 1, 0, EFoundationClickType::Pickup);
        TestTrue(TEXT("one bucket in the fuel slot"), L.Holds(L.F().Items[FEntity::FurnaceFuel], TEXT("bucket"), 1));
        TestTrue(TEXT("two buckets left on the cursor"), L.Holds(L.Inv().Carried, TEXT("bucket"), 2));
        // Closing returns the cursor; the furnace keeps its slots.
        TestTrue(TEXT("close"), L.Close().IsCommitted());
        TestTrue(
            TEXT("slots stay in the furnace"), L.Holds(L.F().Items[FEntity::FurnaceInput], TEXT("cobblestone"), 10));
        TestTrue(
            TEXT("reopened: same slots"), L.Open() > 0 && L.Holds(L.A.GetOpenMenu(L.P())[0], TEXT("cobblestone"), 10));
    }
    // Shift-click routing (survival.r3.shift_click, furnace rows, SURVIVAL-FINDINGS F 3210-3218): 8 items from main 9
    // (furnace slot 3). A log smelts (into charcoal, 8a2) as well as burns: the input wins.
    {
        const struct
        {
            const TCHAR* Item;
            int32 Slot;
        } Routes[] = {{TEXT("coal"), 1}, {TEXT("oak_log"), 0}, {TEXT("oak_planks"), 1}, {TEXT("raw_iron"), 0},
            {TEXT("cobblestone"), 0}, {TEXT("sand"), 0}, {TEXT("beef"), 0}, {TEXT("bucket"), 30}};
        for (const auto& [ItemId, Slot] : Routes)
        {
            FFurnaceLab L;
            const int32 Id = L.Open();
            L.Inv().SetSlot(FFoundationInventory::FirstMain, L.Stack(ItemId, 8));
            L.Click(Id, 3, 0, EFoundationClickType::QuickMove);
            const TArray<FFoundationItemInstance> Menu = L.A.GetOpenMenu(L.P());
            TestTrue(FString::Printf(TEXT("%s x8 to slot %d"), ItemId, Slot), L.Holds(Menu[Slot], ItemId, 8));
            TestTrue(FString::Printf(TEXT("%s left main 9"), ItemId), Menu[3].IsEmpty());
        }
    }
    // A log smelts into charcoal over coal (8a2's items: survival.r3.recipes, 200 ticks).
    {
        FFurnaceLab L;
        L.Load(TEXT("oak_log"), 1, TEXT("coal"), 1);
        FoundationFurnace::OnInputChanged(L.F(), L.A.GetCooking(), EFoundationCookingKind::Smelting);
        L.Steps(205);
        TestTrue(TEXT("a log smelted into one charcoal"),
            L.Holds(L.F().Items[FEntity::FurnaceResult], TEXT("charcoal"), 1) &&
                L.F().Items[FEntity::FurnaceInput].IsEmpty());
    }
    // Putting another item in the input starts over; adding more of the same keeps the progress.
    {
        FFurnaceLab L;
        L.Load(TEXT("raw_iron"), 2, TEXT("coal"), 1);
        const int32 Id = L.Open();
        L.Steps(50);
        L.Inv().SetSlot(0, L.Stack(TEXT("raw_iron"), 3));
        L.Click(Id, 30, 0, EFoundationClickType::Pickup);
        L.Click(Id, 0, 0, EFoundationClickType::Pickup);
        TestEqual(TEXT("same item added: progress kept"), L.F().CookProgress, 50);
        L.Inv().SetSlot(1, L.Stack(TEXT("sand"), 1));
        L.Click(Id, 31, 0, EFoundationClickType::Pickup);
        L.Click(Id, 0, 0, EFoundationClickType::Pickup);
        TestTrue(TEXT("sand swapped in"), L.Holds(L.F().Items[FEntity::FurnaceInput], TEXT("sand"), 1));
        TestEqual(TEXT("another item: progress 0"), L.F().CookProgress, 0);
    }
    // Taking any of the output pays the XP of every item cooked since, as orbs (fractional XP, survival.r3.xp_rule):
    // 10 iron ingots at 0.7 is a whole 7.
    {
        FFurnaceLab L;
        L.A.Options.bExperienceOrbs = true;
        const int32 Iron = L.A.GetCooking().Find(EFoundationCookingKind::Smelting, L.Item(TEXT("raw_iron")));
        L.F().Items[FEntity::FurnaceResult] = L.Stack(TEXT("iron_ingot"), 10);
        L.F().RecipesUsed.Add(Iron, 10);
        const int32 Id = L.Open();
        TestTrue(TEXT("take one (right-click half: 5)"), L.Click(Id, 2, 1, EFoundationClickType::Pickup).IsCommitted());
        TestTrue(TEXT("5 ingots on the cursor"), L.Holds(L.Inv().Carried, TEXT("iron_ingot"), 5));
        TestEqual(TEXT("7 XP in orbs"), L.OrbTotal(), 7);
        TestEqual(TEXT("stored XP spent"), L.F().RecipesUsed.Num(), 0);
        L.Click(Id, 2, 0, EFoundationClickType::Pickup);
        TestEqual(TEXT("nothing more owed"), L.OrbTotal(), 7);
    }
    // A furnace broken while open closes its menu; it drops its slots and pays its stored XP (provisional, wiki).
    {
        FFurnaceLab L;
        L.A.Options.bItemEntities = true;
        L.A.Options.bExperienceOrbs = true;
        const int32 Iron = L.A.GetCooking().Find(EFoundationCookingKind::Smelting, L.Item(TEXT("raw_iron")));
        L.Load(TEXT("raw_iron"), 5, TEXT("oak_log"), 2);
        L.F().Items[FEntity::FurnaceResult] = L.Stack(TEXT("iron_ingot"), 10);
        L.F().RecipesUsed.Add(Iron, 10);
        L.A.Options.bFurnace = false;
        L.Open();
        TestTrue(TEXT("broken"), L.Break());
        L.Steps(1);
        TestFalse(TEXT("broken: menu closed"), L.P().OpenMenu.IsOpen());
        TestNull(TEXT("broken: entity gone"), L.A.BlockEntities.Find(L.Aimed));
        TMap<int32, int32> Dropped;
        for (const FFoundationItemEntity& Entity : L.A.GetItemEntities())
            Dropped.FindOrAdd(Entity.Item) += Entity.Count;
        TestEqual(TEXT("raw iron dropped"), Dropped.FindRef(L.Item(TEXT("raw_iron"))), 5);
        TestEqual(TEXT("oak logs dropped"), Dropped.FindRef(L.Item(TEXT("oak_log"))), 2);
        TestEqual(TEXT("ingots dropped"), Dropped.FindRef(L.Item(TEXT("iron_ingot"))), 10);
        TestEqual(TEXT("stored XP paid"), L.OrbTotal(), 7);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationFurnaceExperienceTest, "Unnamed.Foundation.Furnace.Experience", FurnaceTestFlags)

bool FFoundationFurnaceExperienceTest::RunTest(const FString&)
{
    // survival.r3.xp_rule: floor(items x xp) + Bernoulli(fraction); a whole total never rolls. The value report's
    // trials at xp 0.7: 1 item 0 or 1, 64 items 44 or 45 (mean 44.7 over 200), 10 items always 7.
    FFurnaceLab L;
    const FFoundationCooking& Cooking = L.A.GetCooking();
    const int32 Iron = Cooking.Find(EFoundationCookingKind::Smelting, L.Item(TEXT("raw_iron")));
    const int32 Glass = Cooking.Find(EFoundationCookingKind::Smelting, L.Item(TEXT("sand")));
    auto Trials = [&](TMap<int32, int32> Used, int32& OutMin, int32& OutMax, double& OutMean)
    {
        FRandomStream Random(1234);
        OutMin = MAX_int32;
        OutMax = MIN_int32;
        double Sum = 0;
        for (int32 Trial = 0; Trial < 2000; ++Trial)
        {
            const int32 Xp = FoundationFurnace::RollExperience(Used, Cooking, Random);
            OutMin = FMath::Min(OutMin, Xp);
            OutMax = FMath::Max(OutMax, Xp);
            Sum += Xp;
        }
        OutMean = Sum / 2000;
    };
    int32 Min = 0, Max = 0;
    double Mean = 0;
    Trials({{Iron, 1}}, Min, Max, Mean);
    TestTrue(TEXT("1 x 0.7: 0 or 1, mean near 0.7"), Min == 0 && Max == 1 && FMath::Abs(Mean - .7) < .05);
    Trials({{Iron, 64}}, Min, Max, Mean);
    TestTrue(TEXT("64 x 0.7: 44 or 45, mean near 44.8"), Min == 44 && Max == 45 && FMath::Abs(Mean - 44.8) < .05);
    Trials({{Iron, 10}}, Min, Max, Mean);
    TestTrue(TEXT("10 x 0.7: always 7"), Min == 7 && Max == 7);
    Trials({{Glass, 10}}, Min, Max, Mean);
    TestTrue(TEXT("10 x 0.1: always 1 (float, no roll)"), Min == 1 && Max == 1);
    Trials({{Iron, 1}, {Glass, 10}}, Min, Max, Mean);
    TestTrue(TEXT("per recipe, summed: 1 or 2"), Min == 1 && Max == 2);
    return true;
}

#endif
