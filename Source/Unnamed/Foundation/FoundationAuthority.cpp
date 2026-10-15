#include "Foundation/FoundationAuthority.h"
#include "Algo/AllOf.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationPlantRules.h"
#include "Foundation/FoundationSimulation.h"
#include "Foundation/FoundationStripping.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarLootAllBlocks(TEXT("foundation.Loot.AllBlocks"), 1,
        TEXT("1: a survival break rolls the block's 26.3 loot table, for every block that has one (loot step 2a). 0: "
             "only the 18 ores roll (items S5b) and every other block drops itself."));
    TAutoConsoleVariable<int32> CVarItemUse(TEXT("foundation.Items.Use"), 1,
        TEXT(
            "1: hold use to eat food (step 10: apple, bread, cooked beef and the reference's other foods, 1.6 s each). "
            "0: food passes, as before. Axe stripping (step 14) and buckets (step 17) hang off this switch."));
    TAutoConsoleVariable<int32> CVarBlockUse(TEXT("foundation.Blocks.Use"), 1,
        TEXT("1: right-clicking a block with a use (the crafting table, survival step 7) uses it; sneaking with an "
             "item still places. 0: right-click always places, as before."));

    // The built-in block uses (FFoundationAuthority::RegisterBlockUse overrides them by id).
    bool UseCraftingTable(FFoundationAuthority& Authority, FFoundationPlayer& Player, const FFoundationHit& Hit,
        FFoundationResult& Result)
    {
        Authority.OpenContainer(Player, EFoundationMenuKind::Crafting, Hit.Cell, TEXT("crafting_table"), Result);
        return true;
    }

    // A furnace (T1 step 8a) opens its menu over its block entity, made on first use.
    bool UseFurnace(FFoundationAuthority& Authority, FFoundationPlayer& Player, const FFoundationHit& Hit,
        FFoundationResult& Result)
    {
        Authority.OpenContainer(Player, EFoundationMenuKind::Furnace, Hit.Cell, TEXT("furnace"), Result);
        if (Result.IsCommitted())
            Authority.BlockEntities.FindOrAdd(Hit.Cell, EFoundationBlockEntityKind::Furnace, TEXT("furnace"));
        return true;
    }

    // The movement solver's contact tolerance (FoundationSimulation.cpp): touching is contact, only
    // positive-volume overlap obstructs. No extra clearance margin.
    constexpr double ContactCm = 1e-6;

    bool Overlaps(const FBox& A, const FBox& B)
    {
        for (int32 Axis = 0; Axis < 3; ++Axis)
            if (!(A.Max[Axis] > B.Min[Axis] + ContactCm && A.Min[Axis] < B.Max[Axis] - ContactCm)) return false;
        return true;
    }

    FFoundationVitals MakeTargetHealth(float MaxHealth)
    {
        FFoundationVitals Health;
        Health.bNaturalRegeneration = false;
        Health.MaxHealth = Health.Health = MaxHealth;
        return Health;
    }

    // foundation.Inventory.Full onto a player's inventory. A change ends a drag in progress: its stored slots may not
    // exist under the other layout.
    void SetFullInventory(FFoundationInventory& Inventory, bool bFull)
    {
        if (Inventory.bFull == bFull) return;
        Inventory.bFull = bFull;
        if (Inventory.QuickCraft.Status != 0 || !Inventory.QuickCraft.Slots.IsEmpty())
        {
            Inventory.QuickCraft = {};
            ++Inventory.Revision;
        }
    }
}

const TCHAR* LexToString(EFoundationReason Reason)
{
    static const TCHAR* Names[] = {TEXT("none"), TEXT("unknown player"), TEXT("stale session"),
        TEXT("expired sequence"), TEXT("invalid slot"), TEXT("dead"), TEXT("item changed"), TEXT("no item"),
        TEXT("no target"), TEXT("target mismatch"), TEXT("out of reach"), TEXT("stale revision"),
        TEXT("not a placement anchor"), TEXT("outside build bounds"), TEXT("destination occupied"),
        TEXT("cannot survive there"), TEXT("no supply"), TEXT("obstructed by lab geometry"),
        TEXT("obstructed by an entity"), TEXT("obstructed by the player"), TEXT("one per step"), TEXT("unknown block"),
        TEXT("immutable lab geometry"), TEXT("unbreakable"), TEXT("no mining session"), TEXT("target changed"),
        TEXT("inventory full"), TEXT("target down"), TEXT("target damage cooldown"),
        TEXT("drop deferred: no item entities"), TEXT("released"), TEXT("expired"), TEXT("canceled"),
        TEXT("cannot break blocks in creative"), TEXT("damage out of range"), TEXT("not hungry"),
        TEXT("no item in use"), TEXT("item use switched off")};
    static_assert(UE_ARRAY_COUNT(Names) == int32(EFoundationReason::Count), "reason names");
    return Names[FMath::Min(int32(Reason), int32(EFoundationReason::Count) - 1)];
}

const TCHAR* LexToString(EFoundationOutcome Outcome)
{
    static const TCHAR* Names[] = {TEXT("pass"), TEXT("committed"), TEXT("rejected"), TEXT("canceled")};
    return Names[int32(Outcome)];
}

const TCHAR* LexToString(EFoundationCommandType Type)
{
    static const TCHAR* Names[] = {TEXT("select"), TEXT("swap"), TEXT("drop"), TEXT("use-on"), TEXT("use-entity"),
        TEXT("mine-start"), TEXT("mine-continue"), TEXT("mine-abort"), TEXT("attack"), TEXT("whiff"),
        TEXT("creative-slot"), TEXT("container-click"), TEXT("container-close"), TEXT("use-item-start"),
        TEXT("use-item-continue"), TEXT("use-item-release")};
    static_assert(UE_ARRAY_COUNT(Names) == int32(EFoundationCommandType::UseItemRelease) + 1, "command names");
    return Names[int32(Type)];
}

bool FoundationBlockUse::IsOn()
{
    return CVarBlockUse.GetValueOnAnyThread() != 0;
}

bool FoundationItemUse::IsOn()
{
    return CVarItemUse.GetValueOnAnyThread() != 0;
}

bool FoundationItemUse::IsEffectTick(int32 Ticks, int32 Duration)
{
    return Ticks < Duration && Ticks > FMath::FloorToInt32(Duration * EffectsDelayFraction) &&
        (Duration - Ticks) % EffectsEveryTicks == 0;
}

FBox FFoundationPlayerFacts::GetBody() const
{
    return FFoundationSimulation::Bounds(FeetCm, HeightCm, WidthCm);
}

int32 FFoundationRules::FindItem(FName Id) const
{
    return Id.IsNone()
        ? INDEX_NONE
        : Items.IndexOfByPredicate([Id](const FFoundationItemDefinition& Item) { return Item.Id == Id; });
}

const FFoundationItemDefinition& FFoundationRules::GetItem(int32 Index) const
{
    return Items.IsValidIndex(Index) ? Items[Index] : Hand;
}

bool FFoundationRules::CanDestroyInCreative(const FFoundationItemDefinition& Item) const
{
    return Item.bCanDestroyBlocksInCreative && !Item.Tags.HasAnyExact(CreativeNoBreakTags);
}

int32 FFoundationAuthority::AddPlayer()
{
    FFoundationPlayer& Player = Players.AddDefaulted_GetRef();
    Player.Id = NextPlayerId++;
    Player.Inventory = MakeLoadout();
    return Player.Id;
}

void FFoundationAuthority::RemovePlayer(int32 Id)
{
    Players.RemoveAll([Id](const FFoundationPlayer& Player) { return Player.Id == Id; });
}

FFoundationPlayer* FFoundationAuthority::FindPlayer(int32 Id)
{
    return Players.FindByPredicate([Id](const FFoundationPlayer& Player) { return Player.Id == Id; });
}

const FFoundationPlayer* FFoundationAuthority::FindPlayer(int32 Id) const
{
    return Players.FindByPredicate([Id](const FFoundationPlayer& Player) { return Player.Id == Id; });
}

void FFoundationAuthority::SetFacts(int32 Id, const FFoundationPlayerFacts& Facts)
{
    if (FFoundationPlayer* Player = FindPlayer(Id))
    {
        Player->Facts = Facts;
        Player->bHasFacts = true;
    }
}

FFoundationTrainingTarget* FFoundationAuthority::FindTarget(int32 Id)
{
    return Targets.FindByPredicate([Id](const FFoundationTrainingTarget& Target) { return Target.Id == Id; });
}

const FFoundationItemDefinition& FFoundationAuthority::GetHandItem(
    const FFoundationPlayer& Player, EFoundationHand Hand) const
{
    const FFoundationItemInstance& Stack = Player.Inventory.GetHand(Hand);
    return Stack.IsEmpty() ? Rules.Hand : Rules.GetItem(Stack.Item);
}

FFoundationContainerRules FFoundationAuthority::MakeContainerRules() const
{
    FFoundationContainerRules Out;
    Out.GetMaxStack = [this](int32 Item) { return Rules.Items.IsValidIndex(Item) ? Rules.Items[Item].MaxStack : 64; };
    Out.GetEquipment = [this](int32 Item)
    { return Rules.Items.IsValidIndex(Item) ? Rules.Items[Item].Equipment : EFoundationEquipmentSlot::None; };
    Out.bCrafting = Options.bCrafting;
    // A furnace's fuel slot and shift-click routing (T1 step 8a).
    Out.IsFuel = [this](int32 Item) { return GetCooking().IsFuel(Item); };
    Out.IsCookable = [this](int32 Item)
    { return GetCooking().Find(EFoundationCookingKind::Smelting, Item) != INDEX_NONE; };
    Out.Bucket = Rules.FindItem(TEXT("bucket"));
    if (Options.bCrafting)
        Out.Craft = [this](TConstArrayView<int32> Grid, int32 Width, int32 Height)
        {
            // foundation.Blocks.Families 0: a B1 family block's item crafts nothing, as before B1.
            const FFoundationRecipeResult Result = Rules.Recipes.Craft(Grid, Width, Height);
            const bool bHidden = !Result.IsEmpty() && Rules.Items.IsValidIndex(Result.Item) &&
                FoundationBlockFamilies::IsHidden(World.GetRegistry().FindBlock(Rules.Items[Result.Item].PlacesBlock));
            return bHidden ? FFoundationRecipeResult() : Result;
        };
    return Out;
}

TArray<FFoundationItemInstance> FFoundationAuthority::GetPlayerMenu(const FFoundationPlayer& Player) const
{
    // A menu over a copy: building one reads the grid's result and changes nothing.
    FFoundationInventory Copy = Player.Inventory;
    const FFoundationContainerRules ClickRules = MakeContainerRules();
    const FFoundationContainerMenu Menu(Copy, ClickRules, false);
    TArray<FFoundationItemInstance> Slots;
    Slots.SetNum(FFoundationContainerMenu::PlayerMenuSlots);
    for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
        if (const FFoundationItemInstance* Stack = Menu.GetMenuStack(Slot)) Slots[Slot] = *Stack;
    return Slots;
}

TArray<FFoundationItemInstance> FFoundationAuthority::GetMenuGrid(const FFoundationPlayer& Player) const
{
    if (Player.OpenMenu.Kind != EFoundationMenuKind::Furnace) return Player.OpenMenu.Grid;
    const FFoundationBlockEntity* Entity = BlockEntities.Find(Player.OpenMenu.Cell);
    return Entity ? Entity->Items : TArray<FFoundationItemInstance>();
}

TArray<FFoundationItemInstance> FFoundationAuthority::GetOpenMenu(const FFoundationPlayer& Player) const
{
    TArray<FFoundationItemInstance> Slots;
    if (!Player.OpenMenu.IsOpen()) return Slots;
    FFoundationInventory Copy = Player.Inventory;
    TArray<FFoundationItemInstance> Grid = GetMenuGrid(Player);
    const FFoundationContainerRules ClickRules = MakeContainerRules();
    const FFoundationContainerMenu Menu(Copy, ClickRules, false, Player.OpenMenu.Kind, Grid);
    Slots.SetNum(Menu.GetLayout().Slots);
    for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
        if (const FFoundationItemInstance* Stack = Menu.GetMenuStack(Slot)) Slots[Slot] = *Stack;
    return Slots;
}

void FFoundationAuthority::RegisterBlockUse(FName BlockId, FBlockUse Use, FBlockWouldUse WouldUse)
{
    BlockUses.Add(BlockId, {MoveTemp(Use), MoveTemp(WouldUse)});
}

const FFoundationAuthority::FBlockUseEntry* FFoundationAuthority::FindBlockUse(FName BlockId) const
{
    static const TMap<FName, FBlockUseEntry> BuiltIn{{TEXT("crafting_table"), {FBlockUse(&UseCraftingTable), nullptr}},
        {TEXT("furnace"), {FBlockUse(&UseFurnace), nullptr}}};
    if (const FBlockUseEntry* Use = BlockUses.Find(BlockId)) return Use;
    return BuiltIn.Find(BlockId);
}

bool FFoundationAuthority::HasBlockUse(const FIntVector& Cell) const
{
    return World.IsKnown(Cell) && FindBlockUse(World.GetBlock(World.Get(Cell).State).Id) != nullptr;
}

bool FFoundationAuthority::WouldUseBlock(const FFoundationPlayer& Player, const FFoundationHit& Hit) const
{
    if (!World.IsKnown(Hit.Cell)) return false;
    const FBlockUseEntry* Found = FindBlockUse(World.GetBlock(World.Get(Hit.Cell).State).Id);
    return Found && (!Found->WouldUse || Found->WouldUse(*this, Player, Hit));
}

const FFoundationCooking& FFoundationAuthority::GetCooking() const
{
    // Resolved again when the item list changes (tests build their own rules).
    if (!Cooking.IsBuilt() || CookingItems != Rules.Items.Num())
    {
        Cooking.Build([this](FName Item) { return Rules.FindItem(Item); });
        CookingItems = Rules.Items.Num();
    }
    return Cooking;
}

FFoundationFurnaceRules FFoundationAuthority::MakeFurnaceRules() const
{
    FFoundationFurnaceRules Out;
    Out.Cooking = &GetCooking();
    Out.GetMaxStack = [this](int32 Item) { return Rules.Items.IsValidIndex(Item) ? Rules.Items[Item].MaxStack : 64; };
    Out.Bucket = Rules.FindItem(TEXT("bucket"));
    Out.WaterBucket = Rules.FindItem(TEXT("water_bucket"));
    Out.LavaBucket = Rules.FindItem(TEXT("lava_bucket"));
    Out.WetSponge = Rules.FindItem(TEXT("wet_sponge"));
    return Out;
}

void FFoundationAuthority::OpenContainer(
    FFoundationPlayer& Player, EFoundationMenuKind Kind, const FIntVector& Cell, FName Block, FFoundationResult& Result)
{
    // A crafting grid a refused close left behind stays with its menu: another kind of menu cannot open over it.
    if (Player.OpenMenu.IsOpen() && Player.OpenMenu.Kind != Kind &&
        Player.OpenMenu.Grid.ContainsByPredicate([](const FFoundationItemInstance& Stack) { return !Stack.IsEmpty(); }))
    {
        Reject(Result, EFoundationReason::DropDeferred);
        return;
    }
    // vanilla's containerId counts 1-100 and wraps (minecraft.wiki "Java Edition protocol"); 0 is the inventory.
    Player.LastContainerId = Player.LastContainerId % 100 + 1;
    const FFoundationMenuLayout& Layout = FFoundationMenuLayout::Get(Kind);
    // A grid a refused close left behind (its throw is T2's, DropDeferred) carries over rather than vanish.
    TArray<FFoundationItemInstance> Kept =
        Player.OpenMenu.Kind == Kind ? MoveTemp(Player.OpenMenu.Grid) : TArray<FFoundationItemInstance>();
    Player.OpenMenu = {};
    Player.OpenMenu.Grid = MoveTemp(Kept);
    Player.OpenMenu.ContainerId = Player.LastContainerId;
    Player.OpenMenu.Kind = Kind;
    Player.OpenMenu.Cell = Cell;
    Player.OpenMenu.Block = Block;
    Player.OpenMenu.Grid.SetNum(Layout.CraftWidth * Layout.CraftHeight);
    Result.Outcome = EFoundationOutcome::Committed;
    Result.Cell = Cell;
    Result.OpenedContainer = Player.OpenMenu.ContainerId;
    Result.OpenedMenu = Kind;
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::ContainerOpened;
    Event.Player = Player.Id;
    Event.Cell = Cell;
    Event.EntityId = Player.OpenMenu.ContainerId;
    Event.Interaction = Block;
    Event.Step = Player.Facts.Step;
    AddEvent(Event);
}

FFoundationInventory FFoundationAuthority::MakeLoadout()
{
    FFoundationInventory Inventory;
    Inventory.bInfiniteSupply = Options.bInfiniteSupply;
    for (int32 Slot = 0; Slot < FMath::Min(Rules.Loadout.Num(), FFoundationInventory::HotbarSlots); ++Slot)
    {
        const int32 Item = Rules.FindItem(Rules.Loadout[Slot]);
        if (Item == INDEX_NONE) continue;
        const FFoundationItemDefinition& Definition = Rules.Items[Item];
        const int32 Count = !Definition.CanPlace() ? 1
            : Options.bInfiniteSupply              ? Definition.MaxStack
                                                   : FMath::Min(Options.FiniteBlockCount, Definition.MaxStack);
        if (Count > 0) Inventory.SetSlot(Slot, Inventory.MakeStack(Item, Count));
    }
    Inventory.Select(0);
    return Inventory;
}

void FFoundationAuthority::Reset()
{
    ++SessionId;
    ++Resets;
    bResetRequested = false;
    World.Clear();
    for (const auto& [Cell, State] : Seed)
        if (World.GetRegistry().IsKnown(State) && World.IsKnown(Cell)) World.Set(Cell, State);
    for (FFoundationPlayer& Player : Players)
    {
        Player.Inventory = MakeLoadout();
        Player.Mining = {};
        Player.Use = {};
        Player.LastPlacementStep = MIN_int64;
        Player.Results.Empty();
        Player.ResultOrder.Empty();
        Player.HighestEvicted = 0;
        Player.ExperienceDelay = Player.PendingExperience = 0;
        Player.OpenMenu = {};
    }
    BlockEntities.Clear();
    for (FFoundationTrainingTarget& Target : Targets)
    {
        Target.Health = MakeTargetHealth(Rules.TargetMaxHealth);
        Target.Hits = 0;
        Target.DamageTaken = 0;
    }
    ItemEntities.Empty();
    ExperienceOrbs.Empty();
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::Reset;
    AddEvent(Event);
}

void FFoundationAuthority::Step()
{
    for (FFoundationTrainingTarget& Target : Targets)
        Target.Health.StepDamageCooldown();
    // A use nobody continued last step has been let go (its owner stopped stepping it).
    for (FFoundationPlayer& Player : Players)
        if (Player.Use.bActive && Player.Facts.Step - Player.Use.LastStep > 1) CancelUse(Player);
    StepItemEntities();
    StepExperienceOrbs();
    StepOpenMenus();
    StepFurnaces();
}

bool FFoundationAuthority::IsMenuValid(const FFoundationPlayer& Player) const
{
    const FFoundationOpenMenu& Open = Player.OpenMenu;
    if (!Open.IsOpen() || !World.IsKnown(Open.Cell) || World.GetBlock(World.Get(Open.Cell).State).Id != Open.Block)
        return false;
    if (Open.Kind == EFoundationMenuKind::Furnace && !BlockEntities.Find(Open.Cell)) return false;
    // Within the interaction range plus 4 blocks of the eye (vanilla's stillValid; provisional, minecraft.wiki).
    if (!Player.bHasFacts) return true;
    const double ReachCm = Rules.BlockReachCm + ContainerReachExtraCm;
    return FoundationGrid::CellBox(Open.Cell, World.OriginCm).ComputeSquaredDistanceToPoint(Player.Facts.EyeCm) <
        ReachCm * ReachCm;
}

void FFoundationAuthority::StepOpenMenus()
{
    for (FFoundationPlayer& Player : Players)
        if (Player.OpenMenu.IsOpen() && !IsMenuValid(Player))
        {
            // As the player's own close (a refused one, foundation.Items.Entities 0 with leftovers, tries each step).
            FFoundationResult Ignored;
            CloseMenu(Player, Ignored);
        }
}

void FFoundationAuthority::CloseMenu(FFoundationPlayer& Player, FFoundationResult& Result)
{
    // The cursor stack, then the open container's grid (the crafting table's 3x3 belongs to its screen, survival step
    // 7; a furnace's slots stay in its block entity), then the 2x2's stacks in slot order (survival step 6), go back
    // into the inventory; what does not fit goes as Q's throw (items fixes), or with foundation.Items.Entities 0 the
    // close is refused and the container stays open. Each stack keeps its damage and identity
    // (FFoundationInventory::Return).
    FFoundationInventory Trial = Player.Inventory;
    TArray<FFoundationItemInstance, TInlineAllocator<1 + 9 + FFoundationInventory::CraftSlots>> Returns{Trial.Carried};
    Returns.Append(Player.OpenMenu.Grid);
    Returns.Append(Trial.Craft);
    Trial.Carried = {};
    for (FFoundationItemInstance& Stack : Trial.Craft)
        Stack = {};
    Trial.QuickCraft = {};
    TArray<FFoundationItemInstance, TInlineAllocator<1 + 9 + FFoundationInventory::CraftSlots>> Left;
    for (const FFoundationItemInstance& Stack : Returns)
        if (!Stack.IsEmpty())
            if (FFoundationItemInstance Rest = Trial.Return(Stack, Rules.Items[Stack.Item].MaxStack); !Rest.IsEmpty())
                Left.Add(Rest);
    // Every leftover must be throwable before any is thrown: a refused close (retried each step by StepOpenMenus) must
    // not leave thrown copies behind.
    bool bThrown = Algo::AllOf(Left, [this](const FFoundationItemInstance& Stack)
        { return Rules.Items.IsValidIndex(Stack.Item) && Stack.Count > 0; });
    if (bThrown && !Left.IsEmpty() && Options.bItemEntities)
        for (const FFoundationItemInstance& Stack : Left)
            bThrown &= Throw(Player, Stack, Stack.Count);
    if (!Left.IsEmpty() && !Options.bItemEntities)
        Reject(Result, EFoundationReason::DropDeferred);
    else if (!bThrown)
        Reject(Result, EFoundationReason::NoSupply);
    else
    {
        ++Trial.Revision;
        Player.Inventory = MoveTemp(Trial);
        Player.OpenMenu = {};
        Result.Outcome = EFoundationOutcome::Committed;
    }
}

void FFoundationAuthority::StepFurnaces()
{
    const FFoundationFurnaceRules FurnaceRules = MakeFurnaceRules();
    TArray<FIntVector, TInlineAllocator<4>> Gone;
    for (auto& [Cell, Entity] : BlockEntities.GetAll())
    {
        if (Entity.Kind != EFoundationBlockEntityKind::Furnace) continue;
        // Only loaded columns tick (vanilla ticks loaded chunks only); the entity waits for its column.
        if (!World.IsKnown(Cell)) continue;
        // A furnace replaced without a break (a command, a reset of the cell) leaves nothing behind, with the switch on
        // or off, so a furnace placed there later starts empty.
        if (World.GetBlock(World.Get(Cell).State).Id != Entity.Block)
        {
            Gone.Add(Cell);
            continue;
        }
        if (Options.bFurnace) FoundationFurnace::Tick(Entity, FurnaceRules, BlockEntities);
    }
    for (const FIntVector& Cell : Gone)
        BlockEntities.Remove(Cell);
}

void FFoundationAuthority::PayFurnaceExperience(
    FFoundationBlockEntity& Furnace, const FVector& LocalCm, int32 RandomSeed)
{
    if (Furnace.RecipesUsed.IsEmpty()) return;
    // Step 12's switch: off, no orbs (the XP is spent all the same, as an ore's is).
    if (Options.bExperienceOrbs)
    {
        FRandomStream Random(RandomSeed);
        if (const int32 Total = FoundationFurnace::RollExperience(Furnace.RecipesUsed, GetCooking(), Random))
            SpawnExperience(LocalCm, Total, Random.GetCurrentSeed());
    }
    Furnace.RecipesUsed.Empty();
}

void FFoundationAuthority::SpawnContents(
    const FIntVector& Cell, TConstArrayView<FFoundationItemInstance> Items, int32 RandomSeed)
{
    using namespace FoundationItemEntities;
    FRandomStream Random(RandomSeed);
    const FVector Center = (FVector(Cell) + FVector(.5)) * UFoundationProfile::BlockCm - FVector(0, 0, BoxCm * .5);
    for (const FFoundationItemInstance& Stack : Items)
    {
        if (Stack.IsEmpty() || !Rules.Items.IsValidIndex(Stack.Item)) continue;
        const FVector Spread(Random.FRandRange(-SpawnSpreadCm, SpawnSpreadCm),
            Random.FRandRange(-SpawnSpreadCm, SpawnSpreadCm), Random.FRandRange(-SpawnSpreadCm, SpawnSpreadCm));
        const FVector Velocity(Random.FRandRange(-SpawnSidewaysCm, SpawnSidewaysCm),
            Random.FRandRange(-SpawnSidewaysCm, SpawnSidewaysCm), SpawnUpCm);
        SpawnItem(Stack.Item, Stack.Count, Center + Spread, Velocity, BlockDropPickupDelay, Stack.Damage);
    }
}

int32 FFoundationAuthority::SpawnItem(
    int32 Item, int32 Count, const FVector& LocalCm, const FVector& VelocityCm, int32 PickupDelay, int32 Damage)
{
    if (!Rules.Items.IsValidIndex(Item) || Count <= 0) return 0;
    FFoundationItemEntity& Entity = ItemEntities.AddDefaulted_GetRef();
    Entity.Id = NextItemEntityId++;
    Entity.Item = Item;
    Entity.Count = Count;
    Entity.Damage = Damage;
    Entity.LocalCm = Entity.PreviousLocalCm = LocalCm;
    Entity.VelocityCm = VelocityCm;
    Entity.PickupDelay = PickupDelay;
    // The look's phase from the id: deterministic, and different for neighbours.
    Entity.BobOffset = float(FRandomStream(int32(GetTypeHash(Entity.Id))).FRandRange(0, 2 * UE_PI));
    ++ItemsSpawned;
    return Entity.Id;
}

void FFoundationAuthority::SpawnDrops(
    const FIntVector& Cell, TConstArrayView<FFoundationLootDrop> Drops, int32 RandomSeed)
{
    using namespace FoundationItemEntities;
    FRandomStream Random(RandomSeed);
    const FVector Center = (FVector(Cell) + FVector(.5)) * UFoundationProfile::BlockCm - FVector(0, 0, BoxCm * .5);
    for (const FFoundationLootDrop& Drop : Drops)
    {
        if (!Rules.Items.IsValidIndex(Drop.Item)) continue;
        const int32 MaxStack = FMath::Max(1, Rules.Items[Drop.Item].MaxStack);
        for (int32 Left = Drop.Count; Left > 0; Left -= MaxStack)
        {
            const FVector Spread(Random.FRandRange(-SpawnSpreadCm, SpawnSpreadCm),
                Random.FRandRange(-SpawnSpreadCm, SpawnSpreadCm), Random.FRandRange(-SpawnSpreadCm, SpawnSpreadCm));
            const FVector Velocity(Random.FRandRange(-SpawnSidewaysCm, SpawnSidewaysCm),
                Random.FRandRange(-SpawnSidewaysCm, SpawnSidewaysCm), SpawnUpCm);
            SpawnItem(Drop.Item, FMath::Min(Left, MaxStack), Center + Spread, Velocity, BlockDropPickupDelay);
        }
    }
}

void FFoundationAuthority::StepItemEntities()
{
    using namespace FoundationItemEntities;
    if (ItemEntities.IsEmpty()) return;
    const FVector& Origin = World.OriginCm;
    const double VoidCm = double(World.BoundsMin.Z - VoidDepthCells) * UFoundationProfile::BlockCm;
    TArray<FBox> Boxes;
    for (int32 Index = 0; Index < ItemEntities.Num();)
    {
        FFoundationItemEntity& Entity = ItemEntities[Index];
        const FIntVector Cell(FoundationGrid::FloorToCell(Entity.LocalCm.X),
            FoundationGrid::FloorToCell(Entity.LocalCm.Y), FoundationGrid::FloorToCell(Entity.LocalCm.Z));
        // An item in a column that is not loaded stays as it is (vanilla's entities do not tick in an unloaded chunk).
        Entity.bFrozen = !World.GetStorage().IsLoaded(Cell);
        if (Entity.bFrozen)
        {
            Entity.PreviousLocalCm = Entity.LocalCm;
            ++Index;
            continue;
        }
        GatherEntityCollision(GetStepRegion(Entity, Origin), GetBox(Origin + Entity.LocalCm), Boxes);
        Move(Entity, Origin, Boxes);
        ++Entity.Age;
        if (Entity.PickupDelay > 0) --Entity.PickupDelay;
        // Items S8b: an item despawns at DespawnAge (its age does not count while frozen).
        const bool bDespawn = Entity.Age >= DespawnAge && Options.bItemEntities;
        ItemsDespawned += bDespawn ? 1 : 0;
        if (Entity.LocalCm.Z < VoidCm || bDespawn)
            ItemEntities.RemoveAt(Index);
        else
            ++Index;
    }
    if (Options.bItemEntities) MergeItemEntities();
    for (FFoundationPlayer& Player : Players)
    {
        if (!Player.bHasFacts || !Player.Facts.bAlive) continue;
        SetFullInventory(Player.Inventory, Options.bFullInventory);
        const FBox Reach = GetPickupBox(Player.Facts.GetBody());
        for (int32 Index = 0; Index < ItemEntities.Num();)
        {
            FFoundationItemEntity& Entity = ItemEntities[Index];
            const FVector Location = Origin + Entity.LocalCm;
            const int32 Taken = !Entity.bFrozen && Entity.PickupDelay <= 0 && Overlaps(Reach, GetBox(Location))
                ? Player.Inventory.Add(
                      Entity.Item, Entity.Count, Rules.Items[Entity.Item].MaxStack, false, Entity.Damage)
                : 0;
            if (Taken <= 0)
            {
                ++Index;
                continue;
            }
            // Only what fits transfers; the rest stays in the world.
            Entity.Count -= Taken;
            ++ItemsPickedUp;
            FFoundationEvent Event;
            Event.Kind = EFoundationEventKind::PickedUp;
            Event.Player = Player.Id;
            Event.EntityId = Entity.Id;
            Event.Item = Entity.Item;
            Event.Count = Taken;
            Event.LocationCm = Location;
            Event.bWholeStack = Entity.Count <= 0;
            Event.Step = Player.Facts.Step;
            AddEvent(Event);
            if (Entity.Count <= 0)
                ItemEntities.RemoveAt(Index);
            else
                ++Index;
        }
    }
}

void FFoundationAuthority::Drop(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result)
{
    // Items fixes (Codex review 4, #5): a dead player or a changed hand is refused first, as every hand command.
    if (!CheckHand(Player, Command, Result)) return;
    // foundation.Items.Entities 0: refused, as before items S8b.
    if (!Options.bItemEntities)
    {
        Reject(Result, EFoundationReason::DropDeferred);
        return;
    }
    FFoundationItemInstance& Stack = Player.Inventory.GetHand(EFoundationHand::Main);
    if (Stack.IsEmpty())
    {
        Reject(Result, EFoundationReason::NoSupply);
        return;
    }
    // Survival and creative alike take the thrown items from the stack (vanilla's creative Q does too).
    const int32 Count = Command.bWholeStack ? Stack.Count : 1;
    if (!Throw(Player, Stack, Count))
    {
        Reject(Result, EFoundationReason::NoSupply);
        return;
    }
    if (Count >= Stack.Count)
    {
        Stack = {};
        ++Player.Inventory.Revision;
    }
    else
    {
        Stack.Count -= Count;
        Player.Inventory.Touch(Stack);
    }
    Result.Outcome = EFoundationOutcome::Committed;
}

bool FFoundationAuthority::Throw(const FFoundationPlayer& Player, const FFoundationItemInstance& Stack, int32 Count)
{
    using namespace FoundationItemEntities;
    const FFoundationPlayerFacts Facts = Player.bHasFacts ? Player.Facts : FFoundationPlayerFacts();
    FRandomStream Random(int32(HashCombine(GetTypeHash(Player.Id), GetTypeHash(NextItemEntityId))));
    const double Angle = Random.FRandRange(0, 2 * UE_PI), Spread = Random.FRand() * ThrowSpreadCm;
    const FVector Velocity = Facts.Aim.GetSafeNormal() * ThrowSpeedCm +
        FVector(FMath::Cos(Angle) * Spread, FMath::Sin(Angle) * Spread,
            ThrowUpCm + (Random.FRand() - Random.FRand()) * ThrowSpreadUpCm);
    if (!SpawnItem(Stack.Item, Count, Facts.EyeCm - FVector(0, 0, ThrowBelowEyeCm) - World.OriginCm, Velocity,
            ThrowPickupDelay, Stack.Damage))
        return false;
    ++ItemsThrown;
    return true;
}

void FFoundationAuthority::GatherEntityCollision(const FBox& Region, const FBox& Body, TArray<FBox>& Boxes)
{
    const auto Gather = [this, &Boxes](const FBox& Where)
    {
        Boxes.Reset();
        if (GatherCollision)
            GatherCollision(Where, Boxes);
        else
        {
            World.GatherCollision(Where, Boxes);
            for (const FFoundationStatic& Static : Statics)
                if (Static.Box.Intersect(Where)) Boxes.Add(Static.Box);
        }
    };
    Gather(Region);
    // Inside a block: the neighbours of the centre's cell are at most two blocks from the centre.
    if (FoundationItemEntities::IsInside(Body, Boxes)) Gather(Region + Body.ExpandBy(2 * UFoundationProfile::BlockCm));
}

void FFoundationAuthority::MergeItemEntities()
{
    using namespace FoundationItemEntities;
    const FVector& Origin = World.OriginCm;
    for (int32 Index = 0; Index < ItemEntities.Num(); ++Index)
    {
        // [items.physics] every MovingMergeTicks of the item's own age when its integer block cell changed this step,
        // else every MergeTicks.
        const FFoundationItemEntity& Seeker = ItemEntities[Index];
        const auto CellOf = [](const FVector& At)
        {
            return FIntVector(FoundationGrid::FloorToCell(At.X), FoundationGrid::FloorToCell(At.Y),
                FoundationGrid::FloorToCell(At.Z));
        };
        const bool bMovedCell = CellOf(Seeker.PreviousLocalCm) != CellOf(Seeker.LocalCm);
        if (Seeker.bFrozen || Seeker.Age % (bMovedCell ? MovingMergeTicks : MergeTicks) != 0) continue;
        const int32 MaxStack = FMath::Max(1, Rules.Items[Seeker.Item].MaxStack);
        const FBox Reach = GetMergeBox(Origin + Seeker.LocalCm);
        for (int32 Other = 0; Other < ItemEntities.Num() && ItemEntities[Index].Count < MaxStack; ++Other)
        {
            FFoundationItemEntity& A = ItemEntities[Index];
            FFoundationItemEntity& B = ItemEntities[Other];
            // Items fixes (Codex review 4, #2; wiki "Item (entity)", Merging): only when the sum fits one stack.
            if (Other == Index || B.bFrozen || B.Item != A.Item || B.Damage != A.Damage ||
                A.Count + B.Count > MaxStack || !Overlaps(Reach, GetBox(Origin + B.LocalCm)))
                continue;
            // The smaller stack moves into the larger, and on a tie the seeker into the other (Codex review 4, #3;
            // the wiki); provisional (P3): the result keeps the longer pickup delay and the younger age.
            FFoundationItemEntity& Into = B.Count >= A.Count ? B : A;
            FFoundationItemEntity& From = B.Count >= A.Count ? A : B;
            const int32 Moved = FMath::Min(From.Count, MaxStack - Into.Count);
            Into.Count += Moved;
            From.Count -= Moved;
            Into.PickupDelay = FMath::Max(Into.PickupDelay, From.PickupDelay);
            Into.Age = FMath::Min(Into.Age, From.Age);
            ++ItemsMerged;
            if (From.Count > 0) continue;
            const bool bSeekerGone = &From == &A;
            ItemEntities.RemoveAt(bSeekerGone ? Index : Other);
            if (bSeekerGone)
            {
                --Index;
                break;
            }
            if (Other < Index) --Index;
            --Other;
        }
    }
}

int32 FFoundationAuthority::SpawnExperience(const FVector& LocalCm, int32 Total, int32 RandomSeed)
{
    using namespace FoundationItemEntities;
    FRandomStream Random(RandomSeed);
    // LocalCm is the orbs' centre: each stands half its box lower, with the block drop's spread and speed.
    const FVector Center = LocalCm - FVector(0, 0, FoundationExperienceOrbs::BoxCm * .5);
    int32 Spawned = 0;
    for (int32 Left = Total; Left > 0; ++Spawned)
    {
        FFoundationExperienceOrb& Orb = ExperienceOrbs.AddDefaulted_GetRef();
        Orb.Id = NextOrbId++;
        Orb.bNew = true;
        Orb.Value = FoundationExperienceOrbs::GetSplitValue(Left);
        Left -= Orb.Value;
        Orb.LocalCm = Orb.PreviousLocalCm = Center +
            FVector(Random.FRandRange(-SpawnSpreadCm, SpawnSpreadCm), Random.FRandRange(-SpawnSpreadCm, SpawnSpreadCm),
                Random.FRandRange(-SpawnSpreadCm, SpawnSpreadCm));
        Orb.VelocityCm = FVector(Random.FRandRange(-SpawnSidewaysCm, SpawnSidewaysCm),
            Random.FRandRange(-SpawnSidewaysCm, SpawnSidewaysCm), SpawnUpCm);
    }
    OrbsSpawned += Spawned;
    return Spawned;
}

int32 FFoundationAuthority::TakeExperience(int32 PlayerId)
{
    FFoundationPlayer* Player = FindPlayer(PlayerId);
    if (!Player) return 0;
    const int32 Points = Player->PendingExperience;
    Player->PendingExperience = 0;
    return Points;
}

void FFoundationAuthority::StepExperienceOrbs()
{
    using namespace FoundationExperienceOrbs;
    for (FFoundationPlayer& Player : Players)
        if (Player.ExperienceDelay > 0) --Player.ExperienceDelay;
    if (ExperienceOrbs.IsEmpty()) return;
    const FVector& Origin = World.OriginCm;
    const double VoidCm =
        double(World.BoundsMin.Z - FoundationItemEntities::VoidDepthCells) * UFoundationProfile::BlockCm;
    TArray<FBox> Boxes;
    for (int32 Index = 0; Index < ExperienceOrbs.Num();)
    {
        FFoundationExperienceOrb& Orb = ExperienceOrbs[Index];
        const FIntVector Cell(FoundationGrid::FloorToCell(Orb.LocalCm.X), FoundationGrid::FloorToCell(Orb.LocalCm.Y),
            FoundationGrid::FloorToCell(Orb.LocalCm.Z));
        Orb.bFrozen = !World.GetStorage().IsLoaded(Cell);
        if (Orb.bFrozen)
        {
            Orb.PreviousLocalCm = Orb.LocalCm;
            ++Index;
            continue;
        }
        // [xp.attraction] the nearest live player whose position is within FollowCm; the pull aims at its position plus
        // half its eye height.
        const FVector Position = Origin + Orb.LocalCm;
        const FFoundationPlayer* Target = nullptr;
        double Nearest = FollowCm * FollowCm;
        for (const FFoundationPlayer& Player : Players)
        {
            const double Distance =
                Player.bHasFacts && Player.Facts.bAlive ? FVector::DistSquared(Player.Facts.FeetCm, Position) : Nearest;
            if (Distance < Nearest)
            {
                Nearest = Distance;
                Target = &Player;
            }
        }
        const FVector Push = Target
            ? GetAttraction(
                  Position, Target->Facts.FeetCm + FVector(0, 0, (Target->Facts.EyeCm.Z - Target->Facts.FeetCm.Z) * .5))
            : FVector::ZeroVector;
        GatherEntityCollision(GetStepRegion(Orb, Origin, Push), GetBox(Origin + Orb.LocalCm), Boxes);
        Move(Orb, Origin, Boxes, Push);
        ++Orb.Age;
        const bool bDespawn = Orb.Age >= DespawnAge;
        OrbsDespawned += bDespawn ? 1 : 0;
        if (Orb.LocalCm.Z < VoidCm || bDespawn)
            ExperienceOrbs.RemoveAt(Index);
        else
            ++Index;
    }
    // Provisional (wiki): every MergeTicks of its age, and in the step it spawned, an orb takes in same-value orbs
    // inside its merge box that CanMerge allows (Codex review 8 #1).
    for (int32 Index = 0; Index < ExperienceOrbs.Num(); ++Index)
    {
        const FFoundationExperienceOrb& Seeker = ExperienceOrbs[Index];
        if (Seeker.bFrozen || (Seeker.Age % MergeTicks != 0 && !Seeker.bNew)) continue;
        const FBox Reach = GetMergeBox(Origin + ExperienceOrbs[Index].LocalCm);
        for (int32 Other = ExperienceOrbs.Num() - 1; Other >= 0; --Other)
        {
            FFoundationExperienceOrb& Into = ExperienceOrbs[Index];
            const FFoundationExperienceOrb& From = ExperienceOrbs[Other];
            if (Other == Index || From.bFrozen || From.Value != Into.Value ||
                !Overlaps(Reach, GetBox(Origin + From.LocalCm)) || !CanMerge(Into, From))
                continue;
            Into.Count += From.Count;
            Into.Age = FMath::Min(Into.Age, From.Age);
            ++OrbsMerged;
            ExperienceOrbs.RemoveAt(Other);
            if (Other < Index) --Index;
        }
    }
    // A new orb has had its scan (a frozen one keeps the flag until it steps).
    for (FFoundationExperienceOrb& Orb : ExperienceOrbs)
        Orb.bNew &= Orb.bFrozen;
    // A live player takes one orb (one point-value of its count) every PickupEveryTicks, oldest first.
    for (FFoundationPlayer& Player : Players)
    {
        if (!Player.bHasFacts || !Player.Facts.bAlive || Player.ExperienceDelay > 0) continue;
        const FBox Reach = FoundationItemEntities::GetPickupBox(Player.Facts.GetBody());
        const int32 Index = ExperienceOrbs.IndexOfByPredicate([&](const FFoundationExperienceOrb& Orb)
            { return !Orb.bFrozen && Overlaps(Reach, GetBox(Origin + Orb.LocalCm)); });
        if (Index == INDEX_NONE) continue;
        FFoundationExperienceOrb& Orb = ExperienceOrbs[Index];
        Player.ExperienceDelay = PickupEveryTicks;
        Player.PendingExperience += Orb.Value;
        ++OrbsPickedUp;
        FFoundationEvent Event;
        Event.Kind = EFoundationEventKind::ExperiencePickedUp;
        Event.Player = Player.Id;
        Event.EntityId = Orb.Id;
        Event.Count = Orb.Value;
        Event.LocationCm = Origin + Orb.LocalCm;
        Event.bWholeStack = --Orb.Count <= 0;
        Event.Step = Player.Facts.Step;
        AddEvent(Event);
        if (Event.bWholeStack) ExperienceOrbs.RemoveAt(Index);
    }
}

FFoundationHit FFoundationAuthority::Resolve(const FFoundationPlayerFacts& Facts, bool bFluidSources) const
{
    TArray<FFoundationEntityShape, TInlineAllocator<4>> Shapes;
    for (const FFoundationTrainingTarget& Target : Targets)
        Shapes.Add({Target.Id, Target.Box, Target.Label, true, !Target.Health.IsDead()});
    FFoundationRay Ray;
    Ray.Eye = Facts.EyeCm;
    Ray.Direction = Facts.Aim;
    Ray.BlockReachCm = Rules.BlockReachCm;
    Ray.EntityReachCm = Rules.EntityReachCm;
    Ray.bFluidSources = bFluidSources;
    return FoundationTargeting::Resolve(Ray, World, Statics, Shapes);
}

bool FFoundationAuthority::GetDestination(
    const FFoundationHit& Hit, FIntVector& OutDestination, EFoundationReason& OutReason) const
{
    OutReason = EFoundationReason::None;
    if (Hit.Kind == EFoundationHitKind::Block)
    {
        // The clicked cell when it is replaceable, otherwise the cell beyond the clicked face.
        const bool bReplaceable = FoundationBlockPlacement::IsReplaceable(World, World.Get(Hit.Cell).State);
        OutDestination = bReplaceable ? Hit.Cell : Hit.Cell + FoundationGrid::FaceOffset(Hit.Face);
        return true;
    }
    if (Hit.Kind == EFoundationHitKind::Static)
    {
        // Lab geometry is never replaced; only designated anchors seed a placement beside them.
        if (!Statics.IsValidIndex(Hit.StaticIndex) || !Statics[Hit.StaticIndex].bAnchor)
        {
            OutReason = EFoundationReason::NotAnchor;
            return false;
        }
        OutDestination = Hit.Cell + FoundationGrid::FaceOffset(Hit.Face);
        return true;
    }
    OutReason = EFoundationReason::NoTarget;
    return false;
}

bool FFoundationAuthority::GetDestination(const FFoundationPlayer& Player, EFoundationHand Hand,
    const FFoundationHit& Hit, FIntVector& OutDestination, EFoundationReason& OutReason) const
{
    const FFoundationItemInstance& Stack = Player.Inventory.GetHand(Hand);
    const FFoundationItemDefinition* Item = Stack.IsEmpty() ? nullptr : &Rules.GetItem(Stack.Item);
    // A standing-and-wall item resolves its destination with its standing block (the wall one is chosen there).
    const FFoundationBlockType* Placing = Item ? World.FindBlock(Item->PlacesBlock) : nullptr;
    if (!Placing) return GetDestination(Hit, OutDestination, OutReason);
    FFoundationPlacementContext Context;
    bool bAccepted;
    const bool bResolved = MakePlacementContext(Player, Hit, *Placing, Context, bAccepted, OutReason);
    OutDestination = Context.Destination;
    return bResolved;
}

bool FFoundationAuthority::MakePlacementContext(const FFoundationPlayer& Player, const FFoundationHit& Hit,
    const FFoundationBlockType& Placing, FFoundationPlacementContext& OutContext, bool& bOutAccepted,
    EFoundationReason& OutReason) const
{
    OutReason = EFoundationReason::None;
    bOutAccepted = false;
    OutContext.ClickedCell = OutContext.Destination = Hit.Cell;
    if (Hit.Kind == EFoundationHitKind::Static &&
        (!Statics.IsValidIndex(Hit.StaticIndex) || !Statics[Hit.StaticIndex].bAnchor))
        OutReason = EFoundationReason::NotAnchor;
    else if (!Hit.IsBlockLike())
        OutReason = EFoundationReason::NoTarget;
    if (OutReason != EFoundationReason::None) return false;
    OutContext.ClickedFace = Hit.Face;
    OutContext.ClickPointCm = Hit.Point;
    FoundationBlockPlacement::GetYawPitch(Player.Facts.Aim, OutContext.Yaw, OutContext.Pitch);
    bOutAccepted =
        FoundationBlockPlacement::ResolveDestination(World, Placing, Hit.Kind == EFoundationHitKind::Block, OutContext);
    return true;
}

EFoundationReason FFoundationAuthority::CheckPlacement(const FFoundationPlayer& Player, EFoundationHand Hand,
    const FFoundationHit& Hit, FIntVector& OutDestination, FFoundationBlockState* OutState) const
{
    OutDestination = Hit.Cell;
    const FFoundationItemInstance& Stack = Player.Inventory.GetHand(Hand);
    if (Stack.IsEmpty()) return EFoundationReason::NoItem;
    const FFoundationItemDefinition& Item = Rules.GetItem(Stack.Item);
    const FFoundationBlockType* Placing = World.FindBlock(Item.PlacesBlock);
    const FFoundationBlockType* Wall = Item.PlacesWallBlock.IsNone() ? nullptr : World.FindBlock(Item.PlacesWallBlock);
    if (!Item.CanPlace() || !Placing || (!Item.PlacesWallBlock.IsNone() && !Wall))
        return EFoundationReason::UnknownBlock;
    // foundation.Blocks.Families 0: a B1 family block is refused as when it was not imported.
    if (FoundationBlockFamilies::IsHidden(Placing)) return EFoundationReason::UnknownBlock;
    if (!Hit.IsBlockLike()) return EFoundationReason::NoTarget;
    if (!Hit.bInRange) return EFoundationReason::OutOfRange;
    FFoundationPlacementContext Context;
    bool bAccepted;
    EFoundationReason Reason;
    const bool bResolved = MakePlacementContext(Player, Hit, *Placing, Context, bAccepted, Reason);
    OutDestination = Context.Destination;
    if (!bResolved) return Reason;
    if (!World.IsKnown(OutDestination)) return EFoundationReason::OutsideBounds;
    if (!bAccepted) return EFoundationReason::DestinationOccupied;
    // A standing-and-wall item places by look among the faces that hold it, or not at all: the torch by its support
    // table, a coral fan by the plants' (CheckPlantPlacement).
    FFoundationBlockState Placed;
    const auto Holds = [this, Placing, &Context](FFoundationBlockState State)
    {
        return Placing->bTorchRule ? FoundationBlockPlacement::IsTorchSupported(World, State, Context.Destination)
                                   : CheckPlantPlacement(Context.Destination, State) == EFoundationReason::None;
    };
    if (!Wall)
        Placed = FoundationBlockPlacement::GetStateForPlacement(World, *Placing, Context);
    else if (!FoundationBlockPlacement::GetStandingOrWallState(World, *Placing, *Wall, Context, Holds, Placed))
        return EFoundationReason::CannotSurvive;
    if (OutState) *OutState = Placed;
    if (Placing->bNeedsSupportBelow)
    {
        const FIntVector Below = OutDestination - FIntVector(0, 0, 1);
        const FBox BelowBox = FoundationGrid::CellBox(Below, World.OriginCm);
        FBox Ignored;
        const bool bSolidBelow = World.GetCollisionBox(Below, World.Get(Below).State, Ignored) ||
            Statics.ContainsByPredicate(
                [&BelowBox](const FFoundationStatic& Static) { return Static.Box.IsInside(BelowBox.GetCenter()); });
        if (!bSolidBelow) return EFoundationReason::CannotSurvive;
    }
    if (const EFoundationReason Plant = CheckPlantPlacement(OutDestination, Placed); Plant != EFoundationReason::None)
        return Plant;
    if (!Player.Inventory.bInfiniteSupply && Stack.Count < 1) return EFoundationReason::NoSupply;
    // Obstruction by the placed state's collision shape (a bottom slab fills only the lower half). The player's
    // own body is checked last, so ObstructedBySelf means nothing else is in the way.
    FBox Shape;
    if (!World.GetCollisionBox(OutDestination, Placed, Shape)) return EFoundationReason::None;
    for (const FFoundationStatic& Static : Statics)
        if (Overlaps(Shape, Static.Box)) return EFoundationReason::ObstructedByStatic;
    for (const FFoundationTrainingTarget& Target : Targets)
        if (Overlaps(Shape, Target.Box)) return EFoundationReason::ObstructedByEntity;
    for (const FFoundationPlayer& Other : Players)
        if (Other.Id != Player.Id && Other.bHasFacts && Overlaps(Shape, Other.Facts.GetBody()))
            return EFoundationReason::ObstructedByEntity;
    if (Player.bHasFacts && Overlaps(Shape, Player.Facts.GetBody())) return EFoundationReason::ObstructedBySelf;
    return EFoundationReason::None;
}

double FFoundationAuthority::GetMiningDelta(const FFoundationPlayer& Player, const FFoundationBlockType& Block) const
{
    if (Block.Hardness < 0) return 0;
    if (Block.Hardness == 0) return 1;
    // 26.3 order: tool speed, efficiency above 1, haste/fatigue/break speed, eyes underwater, airborne.
    const FFoundationItemDefinition& Tool = GetHandItem(Player, EFoundationHand::Main);
    double Speed = Tool.GetMiningSpeed(Block.Tags);
    if (Speed > 1) Speed += Rules.MiningEfficiency;
    Speed *= Rules.HasteMultiplier * Rules.FatigueMultiplier * Rules.BlockBreakSpeed;
    if (Player.Facts.bEyesInWater) Speed *= Rules.SubmergedMiningMultiplier;
    if (!Player.Facts.bGrounded) Speed *= Rules.AirborneMiningMultiplier;
    return Speed / Block.Hardness / (CanHarvest(Tool, Block) ? Rules.EligibleDivisor : Rules.IneligibleDivisor);
}

bool FFoundationAuthority::CanHarvest(const FFoundationItemDefinition& Tool, const FFoundationBlockType& Block)
{
    return !Block.bRequiresCorrectToolForDrops || Tool.IsCorrectForDrops(Block.Tags);
}

float FFoundationAuthority::GetBlockHardness(const FIntVector& Cell) const
{
    return World.GetBlock(World.Get(Cell).State).Hardness;
}

void FFoundationAuthority::AddEvent(FFoundationEvent Event)
{
    Event.Serial = ++EventSerial;
    Events.Add(Event);
    if (Events.Num() > 128) Events.RemoveAt(0, Events.Num() - 128);
}

FFoundationResult& FFoundationAuthority::Reject(FFoundationResult& Result, EFoundationReason Reason)
{
    Result.Outcome = EFoundationOutcome::Rejected;
    Result.Reason = Reason;
    return Result;
}

void FFoundationAuthority::Remember(FFoundationPlayer& Player, const FFoundationResult& Result)
{
    Player.Results.Add(Result.Sequence, Result);
    Player.ResultOrder.Add(Result.Sequence);
    while (Player.ResultOrder.Num() > FMath::Max(1, Rules.ResultCacheSize))
    {
        const uint32 Oldest = Player.ResultOrder[0];
        Player.ResultOrder.RemoveAt(0);
        Player.Results.Remove(Oldest);
        Player.HighestEvicted = FMath::Max(Player.HighestEvicted, Oldest);
    }
}

bool FFoundationAuthority::CheckHand(
    const FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result)
{
    if (!Player.Facts.bAlive)
    {
        Reject(Result, EFoundationReason::Dead);
        return false;
    }
    if (Player.Inventory.GetHand(Command.Hand).GetRef() != Command.Item)
    {
        Reject(Result, EFoundationReason::ItemChanged);
        return false;
    }
    return true;
}

FFoundationResult FFoundationAuthority::Execute(
    int32 PlayerId, const FFoundationCommand& Command, FFoundationVitals& Vitals)
{
    FFoundationResult Result;
    Result.Sequence = Command.Sequence;
    Result.Type = Command.Type;
    Result.Hand = Command.Hand;
    Result.WorldRevision = World.Revision;
    FFoundationPlayer* Player = FindPlayer(PlayerId);
    if (!Player) return Reject(Result, EFoundationReason::UnknownPlayer);
    // Commands of an earlier session (before a reset) can never act.
    if (Command.SessionId != SessionId) return Reject(Result, EFoundationReason::StaleSession);
    if (const FFoundationResult* Stored = Player->Results.Find(Command.Sequence))
    {
        ++Duplicates;
        FFoundationResult Copy = *Stored;
        Copy.bDuplicate = true;
        return Copy;
    }
    // Evicted or never valid: its effect cannot be known, so it must not apply now.
    if (Command.Sequence == 0 || Command.Sequence <= Player->HighestEvicted)
        return Reject(Result, EFoundationReason::ExpiredSequence);
    SetFullInventory(Player->Inventory, Options.bFullInventory);

    switch (Command.Type)
    {
    case EFoundationCommandType::SelectSlot:
        if (Command.Slot < 0 || Command.Slot >= FFoundationInventory::HotbarSlots)
            Reject(Result, EFoundationReason::InvalidSlot);
        else
        {
            // A slot change ends a use in progress (a re-select of the same slot keeps it).
            if (Command.Slot != Player->Inventory.Selected) CancelUse(*Player);
            Player->Inventory.Select(Command.Slot);
            Result.Outcome = EFoundationOutcome::Committed;
        }
        break;
    case EFoundationCommandType::SwapHands:
        CancelUse(*Player);
        Player->Inventory.SwapHands();
        Result.Outcome = EFoundationOutcome::Committed;
        break;
    case EFoundationCommandType::Drop:
        Drop(*Player, Command, Result);
        break;
    case EFoundationCommandType::UseOn:
        UseOn(*Player, Command, Result, Vitals.Mode);
        break;
    case EFoundationCommandType::UseEntity:
        UseEntity(*Player, Command, Result);
        break;
    case EFoundationCommandType::MineStart:
        MineStart(*Player, Command, Vitals, Result);
        break;
    case EFoundationCommandType::MineContinue:
        MineContinue(*Player, Command, Vitals, Result);
        break;
    case EFoundationCommandType::MineAbort:
        if (!Player->Mining.bActive)
            Reject(Result, EFoundationReason::NoMiningSession);
        else
        {
            // An abandoned break restarts attack recovery, as the reference's stop does.
            Result.Cell = Player->Mining.Cell;
            Player->Mining = {};
            Vitals.ResetAttackStrength();
            Result.bChargeReset = true;
            Result.Outcome = EFoundationOutcome::Canceled;
            Result.Reason = EFoundationReason::Released;
        }
        break;
    case EFoundationCommandType::Attack:
        Attack(*Player, Command, Vitals, Result);
        break;
    case EFoundationCommandType::Whiff:
        Whiff(*Player, Command, Vitals, Result);
        break;
    case EFoundationCommandType::SetCreativeSlot:
    {
        const int32 Item = Rules.FindItem(Command.CreativeItem);
        const bool bClear = Command.CreativeItem.IsNone() || Command.CreativeCount <= 0;
        if (!Options.bCreativeInventory)
            Reject(Result, EFoundationReason::InvalidSlot);
        else if (!Player->Inventory.GetSlot(Command.Slot))
            Reject(Result, EFoundationReason::InvalidSlot);
        else if (!bClear && (Item == INDEX_NONE || Command.CreativeCount > Rules.Items[Item].MaxStack))
            Reject(Result, EFoundationReason::NoItem);
        // Damage runs from 0 to the max damage, the durability bar's range (facts/durability_bar.json).
        else if (!bClear && (Command.CreativeDamage < 0 || Command.CreativeDamage > Rules.Items[Item].MaxDurability))
            Reject(Result, EFoundationReason::InvalidDamage);
        else
        {
            FFoundationInventory& Inventory = Player->Inventory;
            FFoundationItemInstance Stack =
                bClear ? FFoundationItemInstance{} : Inventory.MakeStack(Item, Command.CreativeCount);
            if (!bClear) Stack.Damage = Command.CreativeDamage;
            Inventory.SetSlot(Command.Slot, Stack);
            Result.Outcome = EFoundationOutcome::Committed;
        }
        break;
    }
    case EFoundationCommandType::ContainerClick:
    {
        // A container click or close ends an item use in progress, as a slot change does.
        CancelUse(*Player);
        // The click runs on a copy. What it throws (a click outside the window, Q over a slot) goes as Q's throw (items
        // fixes); with foundation.Items.Entities 0 such a click is refused and nothing changes.
        FFoundationInventory Trial = Player->Inventory;
        const FFoundationContainerRules ClickRules = MakeContainerRules();
        const bool bCreative = Vitals.Mode == EFoundationPlayerMode::Creative;
        // Container 0 is the player's own menu; the open container's id is its menu (survival step 7) while it is
        // valid (its block, within reach; Step closes it otherwise, Codex review 12 finding 1); any other id is stale.
        const FFoundationOpenMenu& Open = Player->OpenMenu;
        const bool bOpenMenu = Open.IsOpen() && Command.ContainerId == Open.ContainerId;
        const bool bStillValid = bOpenMenu && IsMenuValid(*Player);
        const bool bFurnace = bOpenMenu && Open.Kind == EFoundationMenuKind::Furnace;
        TArray<FFoundationItemInstance> TrialGrid =
            bOpenMenu ? GetMenuGrid(*Player) : TArray<FFoundationItemInstance>();
        const TArray<FFoundationItemInstance> GridBefore = TrialGrid;
        FFoundationClickResult Click{false};
        // A spectator moves nothing (Codex review 12, finding 6).
        if (Vitals.Mode == EFoundationPlayerMode::Spectator)
            Click.bValid = false;
        else if (Command.ContainerId == FFoundationContainerMenu::PlayerContainer)
            Click = FFoundationContainerMenu(Trial, ClickRules, bCreative)
                        .Click(Command.Slot, Command.Button, Command.ClickType);
        else if (bStillValid)
            Click = FFoundationContainerMenu(Trial, ClickRules, bCreative, Open.Kind, TrialGrid)
                        .Click(Command.Slot, Command.Button, Command.ClickType);
        if (!Click.bValid)
            Reject(Result, EFoundationReason::InvalidSlot);
        else if (!Click.Thrown.IsEmpty() && !Options.bItemEntities)
            Reject(Result, EFoundationReason::DropDeferred);
        else if (!Click.Thrown.IsEmpty() && !Throw(*Player, Click.Thrown, Click.Thrown.Count))
            Reject(Result, EFoundationReason::NoSupply);
        else
        {
            Player->Inventory = MoveTemp(Trial);
            if (FFoundationBlockEntity* Furnace = bFurnace ? BlockEntities.Find(Open.Cell) : nullptr)
            {
                using Entity = FFoundationBlockEntity;
                const FFoundationItemInstance &InputBefore = GridBefore[Entity::FurnaceInput],
                                              &ResultBefore = GridBefore[Entity::FurnaceResult];
                Furnace->Items = MoveTemp(TrialGrid);
                // Another item put in the input starts over at its cook time (vanilla's setItem for slot 0); taking
                // from the input or adding to the same item keeps the progress.
                const FFoundationItemInstance& Input = Furnace->Items[Entity::FurnaceInput];
                if (!Input.IsEmpty() && (InputBefore.IsEmpty() || Input.Item != InputBefore.Item))
                    FoundationFurnace::OnInputChanged(*Furnace, GetCooking(), EFoundationCookingKind::Smelting);
                // Taking any of the output pays the XP of everything cooked since (FurnaceResultSlot; the value
                // report's fractional XP), as orbs at the player.
                const FFoundationItemInstance& Output = Furnace->Items[Entity::FurnaceResult];
                const bool bTaken = !ResultBefore.IsEmpty() &&
                    (Output.IsEmpty() || Output.Item != ResultBefore.Item || Output.Count < ResultBefore.Count);
                if (bTaken)
                    PayFurnaceExperience(*Furnace, Player->Facts.FeetCm - World.OriginCm,
                        int32(HashCombine(GetTypeHash(Open.Cell), GetTypeHash(Command.Sequence))));
            }
            else if (bOpenMenu)
                Player->OpenMenu.Grid = MoveTemp(TrialGrid);
            Result.Outcome = EFoundationOutcome::Committed;
        }
        break;
    }
    case EFoundationCommandType::ContainerClose:
        CancelUse(*Player);
        CloseMenu(*Player, Result);
        break;
    case EFoundationCommandType::UseItemStart:
        UseItemStart(*Player, Command, Vitals, Result);
        break;
    case EFoundationCommandType::UseItemContinue:
        UseItemContinue(*Player, Command, Vitals, Result);
        break;
    case EFoundationCommandType::UseItemRelease:
        Result.UseTicks = Player->Use.Ticks;
        Result.UseDuration = Player->Use.Duration;
        if (!CancelUse(*Player))
            Reject(Result, EFoundationReason::NotUsing);
        else
        {
            Result.Outcome = EFoundationOutcome::Canceled;
            Result.Reason = EFoundationReason::Released;
        }
        break;
    }
    if (Result.Outcome == EFoundationOutcome::Rejected) ++Rejections;
    Result.WorldRevision = World.Revision;
    Remember(*Player, Result);
    if (Result.bInteracted && OnInteraction) OnInteraction(Result.Interaction);
    if (bResetRequested)
    {
        // The interaction finished first; the reset then starts a new session for everyone.
        Reset();
        Result.bReset = true;
    }
    return Result;
}

void FFoundationAuthority::UseOn(
    FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result, EFoundationPlayerMode Mode)
{
    const bool bCreative = Mode == EFoundationPlayerMode::Creative;
    if (!CheckHand(Player, Command, Result)) return;
    const FFoundationHit Hit = Resolve(Player.Facts);
    if (!Hit.IsBlockLike() || !Hit.SameTarget(Command.Target))
    {
        Reject(Result, EFoundationReason::TargetMismatch);
        return;
    }
    if (!Hit.bInRange)
    {
        Reject(Result, EFoundationReason::OutOfRange);
        return;
    }
    if (Hit.Revision != Command.Target.Revision)
    {
        Reject(Result, EFoundationReason::StaleRevision);
        return;
    }
    // 1. The target's own use interaction wins, unless a crouching player holds something.
    const bool bHolding = !Player.Inventory.GetHand(EFoundationHand::Main).IsEmpty() ||
        !Player.Inventory.GetHand(EFoundationHand::Off).IsEmpty();
    const bool bBypass = Player.Facts.bSecondaryUse && bHolding;
    if (!bBypass && Command.Hand == EFoundationHand::Main && Hit.Kind == EFoundationHitKind::Static &&
        !Statics[Hit.StaticIndex].Interaction.IsNone())
    {
        Result.Outcome = EFoundationOutcome::Committed;
        Result.bInteracted = true;
        Result.Cell = Hit.Cell;
        Result.Interaction = Statics[Hit.StaticIndex].Interaction;
        // The reset is the authority's own; the lab reacts to other interactions through the event.
        if (Result.Interaction == TEXT("lab.reset")) bResetRequested = true;
        FFoundationEvent Event;
        Event.Kind = EFoundationEventKind::Interacted;
        Event.Player = Player.Id;
        Event.Cell = Hit.Cell;
        Event.Interaction = Result.Interaction;
        Event.Step = Player.Facts.Step;
        AddEvent(Event);
        return;
    }
    // A spectator uses and places nothing (Codex review 12, finding 6; vanilla's spectators cannot edit the world).
    if (Mode == EFoundationPlayerMode::Spectator)
    {
        Result.Outcome = EFoundationOutcome::Pass;
        return;
    }
    // 1b. A block's own use (survival step 7, foundation.Blocks.Use; RegisterBlockUse), on the same terms.
    if (!bBypass && Options.bBlockUse && Command.Hand == EFoundationHand::Main &&
        Hit.Kind == EFoundationHitKind::Block && World.IsKnown(Hit.Cell))
        if (const FBlockUseEntry* Found = FindBlockUse(World.GetBlock(World.Get(Hit.Cell).State).Id))
        {
            // A copy: a handler may register another (the map may grow under it).
            const FBlockUse Use = Found->Use;
            if (Use(*this, Player, Hit, Result)) return;
        }
    // 2. The hand item's use on the block (foundation.Items.Use): an axe strips.
    if (StripOn(Player, Command, Hit, Result, Mode)) return;
    // 3. The hand item's place capability; without one the use passes to the next hand.
    const FFoundationItemInstance& Stack = Player.Inventory.GetHand(Command.Hand);
    if (Stack.IsEmpty() || !Rules.GetItem(Stack.Item).CanPlace())
    {
        Result.Outcome = EFoundationOutcome::Pass;
        return;
    }
    FIntVector Destination = FIntVector::ZeroValue;
    FFoundationBlockState Placed;
    const EFoundationReason Reason = CheckPlacement(Player, Command.Hand, Hit, Destination, &Placed);
    Result.Cell = Destination;
    if (Reason == EFoundationReason::None && Destination != Command.Destination)
    {
        Reject(Result, EFoundationReason::TargetMismatch);
        return;
    }
    if (Reason == EFoundationReason::None && World.Get(Destination).Revision != Command.DestinationRevision)
    {
        Reject(Result, EFoundationReason::StaleRevision);
        return;
    }
    if (Reason == EFoundationReason::None && Player.LastPlacementStep == Player.Facts.Step)
    {
        Reject(Result, EFoundationReason::StepLimit);
        return;
    }
    if (Reason != EFoundationReason::None)
    {
        // An explicit failure stops here: it never falls through to the other hand.
        Reject(Result, Reason);
        if (Reason != EFoundationReason::ObstructedBySelf)
        {
            FFoundationEvent Event;
            Event.Kind = EFoundationEventKind::Rejected;
            Event.Player = Player.Id;
            Event.Cell = Destination;
            Event.Reason = Reason;
            Event.Step = Player.Facts.Step;
            AddEvent(Event);
        }
        return;
    }
    const FFoundationBlockState Replaced = World.Get(Destination).State;
    World.Set(Destination, Placed);
    if (!Player.Inventory.bInfiniteSupply)
    {
        Player.Inventory.ConsumeOne(Command.Hand);
        Result.bItemConsumed = true;
    }
    Player.LastPlacementStep = Player.Facts.Step;
    ++Placements;
    Result.Outcome = EFoundationOutcome::Committed;
    Result.bPlaced = true;
    Result.State = Placed;
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::Placed;
    Event.Player = Player.Id;
    Event.Cell = Destination;
    Event.State = Placed;
    Event.Step = Player.Facts.Step;
    AddEvent(Event);
    SettlePlants(Destination, Replaced, Player);
}

bool FFoundationAuthority::StripOn(FFoundationPlayer& Player, const FFoundationCommand& Command,
    const FFoundationHit& Hit, FFoundationResult& Result, EFoundationPlayerMode Mode)
{
    const FFoundationItemInstance& Stack = Player.Inventory.GetHand(Command.Hand);
    // A plain axe in Adventure passes and the block stays (SURVIVAL-FINDINGS, R1, survival.r1.modes); items with
    // Adventure permission components are not modelled.
    const bool bCreative = Mode == EFoundationPlayerMode::Creative;
    if (!Options.bItemUse || Mode == EFoundationPlayerMode::Adventure || Stack.IsEmpty() ||
        Hit.Kind != EFoundationHitKind::Block)
        return false;
    const int32 AxeItem = Stack.Item;
    const FFoundationItemDefinition& Axe = Rules.GetItem(AxeItem);
    const FGameplayTag AxesTag = FoundationStripping::GetAxesTag();
    if (!AxesTag.IsValid() || !Axe.Tags.HasTagExact(AxesTag)) return false;
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    const FFoundationBlockState State = World.Get(Hit.Cell).State;
    // The stripped block keeps the properties (the axis): the saved form with the other id.
    FFoundationSavedState Saved = Registry.Save(State);
    Saved.Id = FoundationStripping::FindStripped(Saved.Id);
    FFoundationBlockState Stripped;
    if (Saved.Id.IsNone() || !Registry.Load(Saved, Stripped)) return false;
    World.Set(Hit.Cell, Stripped);
    ++Strips;
    Result.Outcome = EFoundationOutcome::Committed;
    Result.bStripped = true;
    Result.Cell = Hit.Cell;
    Result.State = Stripped;
    // One point of wear (wiki "Axe"), when tool wear is on; creative tools never wear.
    if (!bCreative && Options.bToolWear && Axe.MaxDurability > 0)
    {
        Result.bToolWorn = true;
        Result.bToolBroke = Player.Inventory.Wear(Command.Hand, FoundationStripping::WearPerStrip, Axe.MaxDurability);
    }
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::Stripped;
    Event.Player = Player.Id;
    Event.Cell = Hit.Cell;
    Event.State = Stripped;
    Event.Item = AxeItem;
    Event.Step = Player.Facts.Step;
    AddEvent(Event);
    if (Result.bToolBroke)
    {
        Event.Kind = EFoundationEventKind::ToolBroke;
        AddEvent(Event);
    }
    return true;
}

bool FFoundationAuthority::UseBucket(
    FFoundationPlayer& Player, const FFoundationCommand& Command, EFoundationPlayerMode Mode, FFoundationResult& Result)
{
    static const FName BucketId(TEXT("bucket")), WaterBucketId(TEXT("water_bucket")), LavaBucketId(TEXT("lava_bucket"));
    static const FName Waterlogged(TEXT("waterlogged")), True(TEXT("true")), False(TEXT("false"));
    FFoundationInventory& Inventory = Player.Inventory;
    const FFoundationItemInstance Stack = Inventory.GetHand(Command.Hand);
    if (!Options.bItemUse || Stack.IsEmpty()) return false;
    const FName Id = Rules.GetItem(Stack.Item).Id;
    if (!FoundationBucket::IsBucket(Id)) return false;
    const bool bEmpty = Id == BucketId, bWater = Id == WaterBucketId;
    Result.Outcome = EFoundationOutcome::Pass;
    // Adventure: a bucket changes no block (minecraft.wiki "Adventure mode"; provisional, the reports say nothing).
    if (Mode == EFoundationPlayerMode::Adventure) return true;
    // An empty bucket's ray stops at fluid sources; a full one's sees blocks only (minecraft.wiki "Bucket").
    const FFoundationHit Hit = Resolve(Player.Facts, bEmpty);
    if (Hit.Kind != EFoundationHitKind::Block || !Hit.bInRange) return true;
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    // Creative keeps the used bucket (minecraft.wiki "Bucket"; provisional): a fill adds the filled bucket only when
    // the inventory holds none, a pour changes nothing. Survival swaps a single bucket in place; from a stack, one
    // goes and the new bucket is added, or dropped at the player when nothing takes it.
    const bool bCreative = Mode == EFoundationPlayerMode::Creative;
    const int32 HandSlot =
        Command.Hand == EFoundationHand::Main ? Inventory.Selected : FFoundationInventory::OffhandSlot;
    auto Swap = [&](int32 NewItem, const FIntVector& Cell)
    {
        const int32 MaxStack = Rules.GetItem(NewItem).MaxStack;
        if (bCreative)
        {
            if (Stack.Item == NewItem) return;
            for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
                if (const FFoundationItemInstance* Held = Inventory.GetSlot(Slot); Held && Held->Item == NewItem)
                    return;
            Inventory.Add(NewItem, 1, MaxStack);
            return;
        }
        if (Stack.Count == 1)
        {
            Inventory.SetSlot(HandSlot, Inventory.MakeStack(NewItem, 1));
            return;
        }
        Inventory.ConsumeOne(Command.Hand);
        if (Inventory.Add(NewItem, 1, MaxStack) > 0) return;
        const FIntVector Feet = FoundationGrid::CellAt(Player.Facts.FeetCm, World.OriginCm);
        const FFoundationLootDrop Drop{NewItem, 1};
        SpawnDrops(Feet, MakeArrayView(&Drop, 1), FoundationLoot::MakeSeed(Rules.LootSeed, Cell, BucketUses));
    };
    FFoundationEvent Event;
    Event.Player = Player.Id;
    Event.Step = Player.Facts.Step;
    if (bEmpty)
    {
        // A water or lava source becomes air; a waterlogged block gives up its water.
        const FFoundationBlockState Before = World.Get(Hit.Cell).State;
        const FFoundationFluid Fluid = Registry.GetFluid(Before);
        FFoundationBlockState After;
        FName FilledId;
        if (Registry.GetBlock(Before).IsFluid() && Fluid.bSource)
            FilledId = Fluid.bLava ? LavaBucketId : WaterBucketId;
        else if (Registry.GetValue(Before, Waterlogged) == True)
        {
            After = Registry.WithValue(Before, Waterlogged, False);
            FilledId = WaterBucketId;
        }
        const int32 Filled = FilledId.IsNone() ? INDEX_NONE : Rules.FindItem(FilledId);
        if (Filled == INDEX_NONE) return true;
        // A stack's new bucket needs a slot, or the item entities to drop it; otherwise nothing changes.
        if (!bCreative && Stack.Count > 1 && !Options.bItemEntities &&
            Inventory.GetSpaceFor(Filled, Rules.GetItem(Filled).MaxStack) < 1)
        {
            Reject(Result, EFoundationReason::InventoryFull);
            return true;
        }
        World.Set(Hit.Cell, After);
        Swap(Filled, Hit.Cell);
        ++BucketUses;
        Result.Outcome = EFoundationOutcome::Committed;
        Result.bFilled = true;
        Result.Cell = Hit.Cell;
        Result.State = After;
        Event.Kind = EFoundationEventKind::Filled;
        Event.Cell = Hit.Cell;
        Event.State = After;
        Event.Item = Filled;
        Event.LocationCm = Player.Facts.FeetCm;
        AddEvent(Event);
        SettlePlants(Hit.Cell, Before, Player);
        return true;
    }
    // Pouring: water fills a waterloggable block (the clicked one, else the one beside it); otherwise the source goes
    // where a block would be placed (the clicked block when replaceable, else beside it) if that cell is air, a fluid
    // or replaceable. A replaced plant goes without a drop (a known issue). Lava never waterlogs. Pouring into the same
    // source empties the bucket and leaves the cell as it was.
    const FFoundationBlockType* FluidBlock = Registry.FindBlock(bWater ? TEXT("water") : TEXT("lava"));
    const int32 Emptied = Rules.FindItem(BucketId);
    if (!FluidBlock || Emptied == INDEX_NONE) return true;
    auto TakesWater = [&](FFoundationBlockState State)
    { return bWater && Registry.GetValue(State, Waterlogged) == False; };
    FIntVector Cell = Hit.Cell;
    FFoundationBlockState Before = World.Get(Cell).State;
    if (!TakesWater(Before))
    {
        if (!FoundationBlockPlacement::IsReplaceable(World, Before)) Cell += FoundationGrid::FaceOffset(Hit.Face);
        if (!World.IsKnown(Cell)) return true;
        Before = World.Get(Cell).State;
    }
    FFoundationBlockState After;
    if (TakesWater(Before))
        After = Registry.WithValue(Before, Waterlogged, True);
    else if (Before.IsAir() || Registry.GetBlock(Before).IsFluid() ||
        FoundationBlockPlacement::IsReplaceable(World, Before))
        After = FluidBlock->GetDefaultState();
    else
        return true;
    if (After != Before) World.Set(Cell, After);
    const int32 Poured = Stack.Item;
    if (!bCreative) Swap(Emptied, Cell);
    ++BucketUses;
    Result.Outcome = EFoundationOutcome::Committed;
    Result.bEmptied = true;
    Result.Cell = Cell;
    Result.State = After;
    Event.Kind = EFoundationEventKind::Emptied;
    Event.Cell = Cell;
    Event.State = After;
    Event.Item = Poured;
    AddEvent(Event);
    if (After != Before) SettlePlants(Cell, Before, Player);
    return true;
}

void FFoundationAuthority::UseItemStart(FFoundationPlayer& Player, const FFoundationCommand& Command,
    const FFoundationVitals& Vitals, FFoundationResult& Result)
{
    if (!CheckHand(Player, Command, Result)) return;
    if (UseBucket(Player, Command, Vitals.Mode, Result)) return;
    const FFoundationItemInstance& Stack = Player.Inventory.GetHand(Command.Hand);
    const FFoundationConsumable Food =
        Stack.IsEmpty() ? FFoundationConsumable{} : Rules.GetItem(Stack.Item).GetConsumable();
    // Without the switch or a consumable the use passes to the next hand, as before.
    if (!Options.bItemUse || !Food.IsValid())
    {
        Result.Outcome = EFoundationOutcome::Pass;
        return;
    }
    // Full hunger refuses (wiki "Food"); the refusal stops the other hand too. Invulnerable modes may always eat
    // (provisional: the wiki's creative note, unverified).
    if (Vitals.Food >= 20 && !Vitals.IsInvulnerable())
    {
        Reject(Result, EFoundationReason::NotHungry);
        return;
    }
    CancelUse(Player);
    Player.Use.bActive = true;
    Player.Use.Hand = Command.Hand;
    Player.Use.InstanceId = Stack.InstanceId;
    Player.Use.Item = Stack.Item;
    Player.Use.Ticks = 0;
    Player.Use.Duration = Food.ConsumeTicks;
    Player.Use.LastStep = Player.Facts.Step;
    Result.Outcome = EFoundationOutcome::Committed;
    Result.UseDuration = Food.ConsumeTicks;
}

void FFoundationAuthority::UseItemContinue(
    FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationVitals& Vitals, FFoundationResult& Result)
{
    FFoundationItemUse& Use = Player.Use;
    if (!Use.bActive || Use.Hand != Command.Hand)
    {
        Reject(Result, EFoundationReason::NotUsing);
        return;
    }
    Result.UseDuration = Use.Duration;
    Result.UseTicks = Use.Ticks;
    // foundation.Items.Use turned 0 mid-use: the use ends, nothing eaten (Codex review 1, finding 1).
    if (!Options.bItemUse)
    {
        CancelUse(Player);
        Result.Outcome = EFoundationOutcome::Canceled;
        Result.Reason = EFoundationReason::ItemUseOff;
        return;
    }
    // The same stack by instance: its count may change (a pickup); another stack or an empty hand ends the use.
    const FFoundationItemInstance& Stack = Player.Inventory.GetHand(Use.Hand);
    const EFoundationReason Cancel = !Player.Facts.bAlive       ? EFoundationReason::Dead
        : Stack.IsEmpty() || Stack.InstanceId != Use.InstanceId ? EFoundationReason::ItemChanged
                                                                : EFoundationReason::None;
    if (Cancel != EFoundationReason::None)
    {
        CancelUse(Player);
        Result.Outcome = EFoundationOutcome::Canceled;
        Result.Reason = Cancel;
        return;
    }
    // One tick per reference step.
    if (Use.LastStep == Player.Facts.Step)
    {
        Reject(Result, EFoundationReason::StepLimit);
        return;
    }
    Use.LastStep = Player.Facts.Step;
    Result.UseTicks = ++Use.Ticks;
    Result.Outcome = EFoundationOutcome::Committed;
    FFoundationEvent Event;
    Event.Player = Player.Id;
    Event.Item = Use.Item;
    Event.LocationCm = Player.Facts.EyeCm;
    Event.Step = Player.Facts.Step;
    if (Use.Ticks < Use.Duration)
    {
        if (FoundationItemUse::IsEffectTick(Use.Ticks, Use.Duration))
        {
            Event.Kind = EFoundationEventKind::Eating;
            AddEvent(Event);
        }
        return;
    }
    // Finished: the food (Eat caps saturation at the new hunger), then one item from the stack.
    const FFoundationConsumable Food = Rules.GetItem(Use.Item).GetConsumable();
    Vitals.Eat(Food.Nutrition, Food.Saturation);
    if (!Player.Inventory.bInfiniteSupply)
    {
        Player.Inventory.ConsumeOne(Use.Hand);
        Result.bItemConsumed = true;
    }
    Use = {};
    ++ItemsEaten;
    Result.bAte = true;
    Event.Kind = EFoundationEventKind::Ate;
    Event.Amount = float(Food.Nutrition);
    AddEvent(Event);
}

bool FFoundationAuthority::CancelUse(FFoundationPlayer& Player)
{
    const bool bWasActive = Player.Use.bActive;
    Player.Use = {};
    return bWasActive;
}

void FFoundationAuthority::UseEntity(
    FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result)
{
    if (!CheckHand(Player, Command, Result)) return;
    const FFoundationHit Hit = Resolve(Player.Facts);
    if (Hit.Kind != EFoundationHitKind::Entity || !Hit.SameTarget(Command.Target))
        Reject(Result, EFoundationReason::TargetMismatch);
    else if (!Hit.bInRange)
        Reject(Result, EFoundationReason::OutOfRange);
    else
        // Training targets have no use interaction; the caller tries the item and the other hand.
        Result.Outcome = EFoundationOutcome::Pass;
}

void FFoundationAuthority::MineStart(FFoundationPlayer& Player, const FFoundationCommand& Command,
    const FFoundationVitals& Vitals, FFoundationResult& Result)
{
    if (Command.Hand != EFoundationHand::Main)
    {
        Reject(Result, EFoundationReason::ItemChanged);
        return;
    }
    if (!CheckHand(Player, Command, Result)) return;
    const FFoundationHit Hit = Resolve(Player.Facts);
    Result.Cell = Hit.Cell;
    if (!Hit.IsBlockLike() || !Hit.SameTarget(Command.Target))
    {
        Reject(Result, EFoundationReason::TargetMismatch);
        return;
    }
    if (!Hit.bInRange)
    {
        Reject(Result, EFoundationReason::OutOfRange);
        return;
    }
    if (Hit.Kind == EFoundationHitKind::Static)
    {
        Player.Mining = {};
        Reject(Result, EFoundationReason::ImmutableGeometry);
        return;
    }
    if (Hit.Revision != Command.Target.Revision)
    {
        Reject(Result, EFoundationReason::StaleRevision);
        return;
    }
    // A new target replaces any session elsewhere; like the reference's abort, that alone restarts nothing.
    Player.Mining = {};
    // Creative (wiki "Creative"): every block breaks at once, bedrock included, without drops or wear; an item that
    // breaks no blocks in creative (swords, spears) breaks nothing.
    if (Vitals.Mode == EFoundationPlayerMode::Creative)
    {
        if (!Rules.CanDestroyInCreative(GetHandItem(Player, EFoundationHand::Main)))
        {
            Reject(Result, EFoundationReason::CreativeCannotBreak);
            return;
        }
        Result.bInstant = true;
        if (BreakBlock(Player, Hit.Cell, Result, true)) Result.Outcome = EFoundationOutcome::Committed;
        return;
    }
    const FFoundationBlockType& Block = World.GetBlock(World.Get(Hit.Cell).State);
    if (Block.Hardness < 0)
    {
        Reject(Result, EFoundationReason::Unbreakable);
        return;
    }
    // A step that completes the break at once (d = 1 within the tolerance: an iron hoe on leaves, whose float hardness
    // leaves d a hair under 1) takes the instant path.
    const double Delta = GetMiningDelta(Player, Block);
    if (Block.Hardness == 0 || Rules.CompletesBreak(Delta))
    {
        // The instant path: no accumulation and no post-break delay.
        Result.bInstant = true;
        if (BreakBlock(Player, Hit.Cell, Result)) Result.Outcome = EFoundationOutcome::Committed;
        return;
    }
    Player.Mining.bActive = true;
    Player.Mining.Cell = Hit.Cell;
    Player.Mining.Revision = Hit.Revision;
    Player.Mining.Face = Hit.Face;
    Player.Mining.Item = Command.Item;
    Result.Outcome = EFoundationOutcome::Committed;
}

void FFoundationAuthority::MineContinue(FFoundationPlayer& Player, const FFoundationCommand& Command,
    const FFoundationVitals& Vitals, FFoundationResult& Result)
{
    FFoundationMining& Mining = Player.Mining;
    Result.Cell = Mining.Cell;
    if (!Mining.bActive)
    {
        Reject(Result, EFoundationReason::NoMiningSession);
        return;
    }
    if (Command.Target.Cell != Mining.Cell)
    {
        Reject(Result, EFoundationReason::TargetMismatch);
        return;
    }
    auto End = [&](EFoundationReason Reason)
    {
        Mining = {};
        Result.Outcome = EFoundationOutcome::Canceled;
        Result.Reason = Reason;
    };
    if (!Player.Facts.bAlive) return End(EFoundationReason::Dead);
    // Same cell, same block revision and the same main-hand stack; only the face may change.
    if (Player.Inventory.GetHand(EFoundationHand::Main).GetRef() != Mining.Item || Command.Item != Mining.Item)
        return End(EFoundationReason::ItemChanged);
    const FFoundationHit Hit = Resolve(Player.Facts);
    if (Hit.Kind != EFoundationHitKind::Block || Hit.Cell != Mining.Cell || !Hit.bInRange)
        return End(EFoundationReason::TargetChanged);
    if (Hit.Revision != Mining.Revision) return End(EFoundationReason::StaleRevision);
    if (Mining.LastStep == Player.Facts.Step)
    {
        Reject(Result, EFoundationReason::StepLimit);
        return;
    }
    Mining.Face = Hit.Face;
    Mining.LastStep = Player.Facts.Step;
    ++Mining.Steps;
    Mining.Progress += GetMiningDelta(Player, World.GetBlock(World.Get(Hit.Cell).State));
    Result.Progress = Mining.Progress;
    Result.MiningSteps = Mining.Steps;
    if (!Rules.CompletesBreak(Mining.Progress))
    {
        Result.Outcome = EFoundationOutcome::Committed;
        return;
    }
    const FIntVector Cell = Mining.Cell;
    Mining = {};
    // A break begun in survival that ends after a switch to creative ends as a creative break.
    if (BreakBlock(Player, Cell, Result, Vitals.Mode == EFoundationPlayerMode::Creative))
        Result.Outcome = EFoundationOutcome::Committed;
}

bool FFoundationAuthority::BreakBlock(
    FFoundationPlayer& Player, const FIntVector& Cell, FFoundationResult& Result, bool bCreative)
{
    const FFoundationBlockState State = World.Get(Cell).State;
    const FFoundationBlockType& Definition = World.GetBlock(State);
    const FFoundationItemDefinition& Tool = GetHandItem(Player, EFoundationHand::Main);
    const bool bEligible = !bCreative && CanHarvest(Tool, Definition);
    // The finite fixture drops the loot into the world as item entities (Options.bItemEntities), or returns it straight
    // to the inventory (the lab's stand-in before items S8a); then, without room for all of it, the break is refused
    // and the block stays.
    TArray<FFoundationLootDrop> Drops;
    if (!Player.Inventory.bInfiniteSupply && bEligible)
    {
        const FFoundationLootTable* Table = Rules.LootTables.Find(Definition.Id);
        // foundation.Loot.AllBlocks 0: only items S5b's ore tables roll.
        if (Table && Table->Index >= FoundationLoot::LegacyTables && CVarLootAllBlocks.GetValueOnAnyThread() == 0)
            Table = nullptr;
        if (Table)
        {
            // A refused break rolls the same drops again: the break count moves only when a block breaks.
            FRandomStream Random(FoundationLoot::MakeSeed(Rules.LootSeed, Cell, Breaks));
            FFoundationLootContext Context{Tool, Definition.Id, World.GetRegistry().GetProperties(State)};
            // Before the break: a double plant's other half is still in place (location_check).
            Context.GetBlockAt = [this, &Cell](
                                     const FIntVector& Offset, FName& Block, TArray<TPair<FName, FName>>& Props)
            {
                if (!World.IsKnown(Cell + Offset)) return false;
                const FFoundationBlockState At = World.Get(Cell + Offset).State;
                Block = World.GetBlock(At).Id;
                Props = World.GetRegistry().GetProperties(At);
                return true;
            };
            FoundationLoot::Roll(*Table, Rules.LootPredicates, Context, Random, Drops);
        }
        else if (const int32 Drop = Rules.FindItem(Definition.DropItem); Drop != INDEX_NONE)
        {
            // Provisional loot rule for blocks without a table, "drops itself": a double slab is two slabs.
            const int32 DropCount =
                Definition.DropCount * (World.GetRegistry().GetValue(State, TEXT("type")) == TEXT("double") ? 2 : 1);
            if (DropCount > 0) Drops.Add({Drop, DropCount});
        }
    }
    // A container's slots come out with it, in any mode (T1 step 8a; vanilla's Containers.dropContents).
    FFoundationBlockEntity* Entity = BlockEntities.Find(Cell);
    if (Entity && Entity->Block != Definition.Id) Entity = nullptr;
    TArray<FFoundationItemInstance> Contents;
    if (Entity)
        for (const FFoundationItemInstance& Stack : Entity->Items)
            if (!Stack.IsEmpty()) Contents.Add(Stack);
    FFoundationInventory Trial = Player.Inventory;
    if (!Options.bItemEntities &&
        (!Algo::AllOf(Drops, [&](const FFoundationLootDrop& Drop)
             { return Trial.Add(Drop.Item, Drop.Count, Rules.Items[Drop.Item].MaxStack) == Drop.Count; }) ||
            !Algo::AllOf(Contents, [&](const FFoundationItemInstance& Stack)
                { return Trial.Return(Stack, Rules.Items[Stack.Item].MaxStack).IsEmpty(); })))
    {
        Reject(Result, EFoundationReason::InventoryFull);
        FFoundationEvent Event;
        Event.Kind = EFoundationEventKind::Rejected;
        Event.Player = Player.Id;
        Event.Cell = Cell;
        Event.Reason = EFoundationReason::InventoryFull;
        Event.Step = Player.Facts.Step;
        AddEvent(Event);
        return false;
    }
    // The drops' spread and speed: seeded like the roll, from another stream.
    const int32 DropSeed = FoundationLoot::MakeSeed(~Rules.LootSeed, Cell, Breaks);
    // A block holding water leaves its source (W2b).
    World.Set(Cell, World.GetRegistry().GetRemainder(State));
    ++Breaks;
    Result.bBroken = true;
    Result.Cell = Cell;
    Result.State = State;
    // Step 12: an ore pays experience orbs on the same terms as its loot (a survival break that can harvest it).
    int32 MinXp = 0, MaxXp = 0;
    if (Options.bExperienceOrbs && !Player.Inventory.bInfiniteSupply && bEligible &&
        FoundationExperienceOrbs::GetBlockExperience(Definition.Id, MinXp, MaxXp))
    {
        FRandomStream Random(int32(HashCombine(uint32(DropSeed), 0x58500u)));
        SpawnExperience((FVector(Cell) + FVector(.5)) * UFoundationProfile::BlockCm, Random.RandRange(MinXp, MaxXp),
            Random.GetCurrentSeed());
    }
    if (Options.bItemEntities)
        SpawnDrops(Cell, Drops, DropSeed);
    else
        for (const FFoundationLootDrop& Drop : Drops)
            Result.bItemReturned |= Player.Inventory.Add(Drop.Item, Drop.Count, Rules.Items[Drop.Item].MaxStack) > 0;
    if (Entity)
    {
        // The slots, then the XP stored for what it cooked (provisional: minecraft.wiki "Furnace"), at the block.
        if (Options.bItemEntities)
            SpawnContents(Cell, Contents, int32(HashCombine(uint32(DropSeed), 0x1C0u)));
        else
            for (const FFoundationItemInstance& Stack : Contents)
                Result.bItemReturned |=
                    Player.Inventory.Return(Stack, Rules.Items[Stack.Item].MaxStack).Count < Stack.Count;
        PayFurnaceExperience(*Entity, (FVector(Cell) + FVector(.5)) * UFoundationProfile::BlockCm,
            int32(HashCombine(uint32(DropSeed), 0x58501u)));
        BlockEntities.Remove(Cell);
    }
    Result.Drops = MoveTemp(Drops);
    // Positive-hardness blocks wear the tool once, when tool wear is on; creative tools never wear.
    if (!bCreative && Options.bToolWear && Definition.Hardness > 0 && Tool.DurabilityPerBlock > 0 &&
        !Player.Inventory.GetHand(EFoundationHand::Main).IsEmpty())
    {
        Result.bToolWorn = true;
        Result.bToolBroke = Player.Inventory.Wear(EFoundationHand::Main, Tool.DurabilityPerBlock, Tool.MaxDurability);
    }
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::Broken;
    Event.Player = Player.Id;
    Event.Cell = Cell;
    Event.State = State;
    Event.Step = Player.Facts.Step;
    AddEvent(Event);
    if (Result.bToolBroke)
    {
        Event.Kind = EFoundationEventKind::ToolBroke;
        AddEvent(Event);
    }
    SettlePlants(Cell, State, Player);
    return true;
}

EFoundationReason FFoundationAuthority::CheckPlantPlacement(
    const FIntVector& Destination, FFoundationBlockState Placed) const
{
    if (!Options.bPlantRules) return EFoundationReason::None;
    if (FoundationPlantRules::HasNoFace(World.GetRegistry(), Placed)) return EFoundationReason::CannotSurvive;
    // A double plant's upper half needs a cell that takes it, as the lower half does (inferred).
    FIntVector Upper;
    FFoundationBlockState UpperState;
    const bool bTwoHalves =
        FoundationPlantRules::GetOtherHalf(World, Destination, Placed, Upper, UpperState) && Upper.Z > Destination.Z;
    if (bTwoHalves)
    {
        if (!World.IsKnown(Upper)) return EFoundationReason::OutsideBounds;
        if (!FoundationBlockPlacement::IsReplaceable(World, World.Get(Upper).State))
            return EFoundationReason::DestinationOccupied;
    }
    if (!PlantSurvival) return EFoundationReason::None;
    // The placed state must survive where it goes, read as the cell is now (the water seagrass goes into), and a
    // double plant's upper half on its lower half. A rule that reads an unknown cell refuses.
    bool bUnknown = false;
    auto Read = [this, &bUnknown](const FIntVector& At) -> uint16
    {
        if (World.IsKnown(At)) return World.Get(At).State.Id;
        bUnknown = true;
        return 0;
    };
    if (!PlantSurvival->Survives(Placed.Id, Destination.X, Destination.Y, Destination.Z,
            [&Read](int32 X, int32 Y, int32 Z) { return Read({X, Y, Z}); }))
        return EFoundationReason::CannotSurvive;
    if (bTwoHalves &&
        !PlantSurvival->Survives(UpperState.Id, Upper.X, Upper.Y, Upper.Z,
            [&Read, &Destination, Placed](int32 X, int32 Y, int32 Z)
            { return FIntVector(X, Y, Z) == Destination ? Placed.Id : Read({X, Y, Z}); }))
        return EFoundationReason::CannotSurvive;
    return bUnknown ? EFoundationReason::CannotSurvive : EFoundationReason::None;
}

bool FFoundationAuthority::CanPlantSurvive(const FIntVector& Cell, FFoundationBlockState State) const
{
    // Torches (T3): the support table, as at placement; an unknown support cell keeps the torch.
    if (World.GetBlock(State).bTorchRule)
    {
        FIntVector Support;
        return FoundationBlockPlacement::IsTorchSupported(World, State, Cell, &Support) || !World.IsKnown(Support);
    }
    if (!PlantSurvival) return true;
    bool bUnknown = false;
    const bool bSurvives = PlantSurvival->Survives(State.Id, Cell.X, Cell.Y, Cell.Z,
        [this, &bUnknown](int32 X, int32 Y, int32 Z) -> uint16
        {
            const FIntVector At(X, Y, Z);
            if (World.IsKnown(At)) return World.Get(At).State.Id;
            bUnknown = true;
            return 0;
        });
    return bSurvives || bUnknown;
}

FFoundationBlockState FFoundationAuthority::TrimPlantFaces(const FIntVector& Cell, FFoundationBlockState State) const
{
    if (!PlantSurvival) return State;
    const FVoxelSurvivalRule& Rule = PlantSurvival->GetRule(State.Id);
    if (Rule.Kind != EVoxelSurvival::Multiface) return State;
    // A neighbour change trims only the faces it no longer holds (scout H, 4.2); an unknown neighbour keeps its face.
    FFoundationBlockState Trimmed = State;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        if (!(Rule.Faces >> Face & 1)) continue;
        bool bUnknown = false;
        const bool bHeld = PlantSurvival->HoldsFace(State.Id, Face, Cell.X, Cell.Y, Cell.Z,
            [this, &bUnknown](int32 X, int32 Y, int32 Z) -> uint16
            {
                const FIntVector At(X, Y, Z);
                if (World.IsKnown(At)) return World.Get(At).State.Id;
                bUnknown = true;
                return 0;
            });
        if (!bHeld && !bUnknown)
            Trimmed =
                World.GetRegistry().WithValue(Trimmed, FoundationGrid::FaceName(EFoundationFace(Face)), TEXT("false"));
    }
    return Trimmed;
}

void FFoundationAuthority::SettlePlants(
    const FIntVector& Cell, FFoundationBlockState Before, const FFoundationPlayer& Player)
{
    if (!Options.bPlantRules) return;
    // Pops drop nothing until item entities exist (scout H, Q4).
    TArray<FIntVector, TInlineAllocator<8>> Changed = {Cell};
    auto Pop = [this, &Player, &Changed](const FIntVector& At)
    {
        FFoundationEvent Event;
        Event.Kind = EFoundationEventKind::Popped;
        Event.Player = Player.Id;
        Event.Cell = At;
        Event.State = World.Get(At).State;
        Event.Step = Player.Facts.Step;
        World.Set(At, World.GetRegistry().GetRemainder(Event.State));
        Changed.Add(At);
        ++Pops;
        AddEvent(Event);
    };
    // A double plant is one unit: the half that was removed or replaced takes its other half along.
    auto PopOtherHalf = [this, &Pop](const FIntVector& At, FFoundationBlockState Old)
    {
        FIntVector Other;
        FFoundationBlockState Ignored;
        if (FoundationPlantRules::GetOtherHalf(World, At, Old, Other, Ignored) && World.IsKnown(Other) &&
            FoundationPlantRules::IsOtherHalf(World.GetRegistry(), Old, World.Get(Other).State))
            Pop(Other);
    };
    PopOtherHalf(Cell, Before);
    // A placed lower half writes its upper half (CheckPlantPlacement found the cell free).
    FIntVector Upper;
    FFoundationBlockState UpperState;
    if (FoundationPlantRules::GetOtherHalf(World, Cell, World.Get(Cell).State, Upper, UpperState) && Upper.Z > Cell.Z &&
        World.IsKnown(Upper))
    {
        const FFoundationBlockState UpperBefore = World.Get(Upper).State;
        World.Set(Upper, UpperState);
        Changed.AddUnique(Upper);
        PopOtherHalf(Upper, UpperBefore);
    }
    // Settles one cell: a vine, glow lichen or sculk vein loses the faces no longer held while one is left; any other
    // state that no longer survives pops, with its other half. Whether the cell changed.
    auto Settle = [this, &Changed, &Pop, &PopOtherHalf](const FIntVector& At, FFoundationBlockState State)
    {
        if (State.IsAir()) return false;
        const FFoundationBlockState Trimmed = TrimPlantFaces(At, State);
        if (Trimmed.Id != State.Id && !FoundationPlantRules::HasNoFace(World.GetRegistry(), Trimmed))
        {
            World.Set(At, Trimmed);
            Changed.Add(At);
            return true;
        }
        if (CanPlantSurvive(At, State)) return false;
        Pop(At);
        PopOtherHalf(At, State);
        return true;
    };
    // A changed segment of a stack, and its stack (FoundationPlantRules::GetStack).
    TArray<TPair<FIntVector, FName>, TInlineAllocator<8>> Stacks;
    auto Visit = [this, &Changed, &Settle, &Stacks](const FIntVector& At)
    {
        if (Changed.Contains(At) || !World.IsKnown(At)) return;
        const FFoundationBlockState State = World.Get(At).State;
        const FName Stack = FoundationPlantRules::GetStack(World.GetBlock(State).Id);
        if (Settle(At, State) && !Stack.IsNone()) Stacks.Emplace(At, Stack);
    };
    // One level deep: the neighbours of the edited unit are checked, the neighbours of a pop are not...
    const int32 Edited = Changed.Num();
    for (int32 Index = 0; Index < Edited; ++Index)
        for (int32 Face = 0; Face < 6; ++Face)
            Visit(Changed[Index] + FoundationGrid::FaceOffset(EFoundationFace(Face)));
    // ...except along a stack, which settles to its end (Jesus's answer to scout H's Q1): from each changed segment the
    // cells of the same stack above and below, while they change. The column bounds it.
    for (int32 Index = 0; Index < Stacks.Num(); ++Index)
    {
        const TPair<FIntVector, FName> Segment = Stacks[Index];
        for (const int32 Step : {-1, 1})
        {
            const FIntVector Next = Segment.Key + FIntVector(0, 0, Step);
            if (World.IsKnown(Next) &&
                FoundationPlantRules::GetStack(World.GetBlock(World.Get(Next).State).Id) == Segment.Value)
                Visit(Next);
        }
    }
}

void FFoundationAuthority::Attack(
    FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationVitals& Vitals, FFoundationResult& Result)
{
    if (Command.Hand != EFoundationHand::Main)
    {
        Reject(Result, EFoundationReason::ItemChanged);
        return;
    }
    if (!CheckHand(Player, Command, Result)) return;
    const FFoundationHit Hit = Resolve(Player.Facts);
    if (Hit.Kind != EFoundationHitKind::Entity || !Hit.SameTarget(Command.Target))
    {
        Reject(Result, EFoundationReason::TargetMismatch);
        return;
    }
    if (!Hit.bInRange)
    {
        Reject(Result, EFoundationReason::OutOfRange);
        return;
    }
    FFoundationTrainingTarget* Target = FindTarget(Hit.EntityId);
    if (!Target || Target->Health.IsDead())
    {
        Reject(Result, EFoundationReason::TargetDead);
        return;
    }
    // Charge from the player's own recovery clock at the attack's half-tick sample; damage and the
    // critical are decided here from authoritative facts, never taken from the caller.
    const FFoundationItemDefinition& Weapon = GetHandItem(Player, EFoundationHand::Main);
    const float Strength = Vitals.GetAttackStrength(Rules.AttackSampleTicks);
    float Damage = Weapon.AttackDamage * (.2f + Strength * Strength * .8f);
    const FFoundationPlayerFacts& Facts = Player.Facts;
    // Past the 84.8% gate an attack is special (wiki "Damage", "Knockback", "Sweep attack"): sprinting makes it a
    // sprint-knockback attack, falling a critical, and a sword on the ground, barely moving, a sweep.
    const bool bStrong = Strength > Rules.CriticalStrength;
    const bool bSprintKnockback = bStrong && Facts.bSprinting;
    const bool bCritical = bStrong && Facts.FallDistanceCm > 0 && !Facts.bGrounded && !Facts.bClimbing &&
        !Facts.bBodyInWater && !Facts.bMobilityRestricted && !Facts.bPassenger && !Facts.bSprinting;
    const bool bSweep = bStrong && !bCritical && !bSprintKnockback && Facts.bGrounded &&
        Facts.StepMovedCm <= Rules.SweepMaxMoveCm && Weapon.Tags.HasAny(Rules.SweepItemTags);
    if (bCritical) Damage *= Rules.CriticalMultiplier;
    const float Before = Target->Health.Health;
    // A hit landing inside the target's hurt window knocks nothing back (wiki "Knockback").
    const bool bFreshHit = !Target->Health.IsInHurtWindow();
    const bool bAccepted = Target->Health.Hurt(EFoundationDamage::PlayerAttack, Damage);
    // Every issued attack restarts recovery, hit or not.
    Vitals.ResetAttackStrength();
    Result.Outcome = EFoundationOutcome::Committed;
    Result.bChargeReset = true;
    Result.EntityId = Target->Id;
    Result.Strength = Strength;
    Result.Damage = Damage;
    Result.bCritical = bCritical;
    Result.TargetHealth = Target->Health.Health;
    if (!bAccepted)
    {
        Result.Reason = EFoundationReason::TargetCooldown;
        return;
    }
    Result.DamageApplied = Before - Target->Health.Health;
    Result.bTargetDied = Target->Health.IsDead();
    Result.bKnockback = bFreshHit;
    Result.bSprintKnockback = bSprintKnockback;
    ++Target->Hits;
    Target->DamageTaken += Result.DamageApplied;
    ++Hits;
    Vitals.AddExhaustion(Rules.HitExhaustion);
    // Accepted hits wear the weapon once, sweep or not, when tool wear is on; creative tools never wear (wiki
    // "Creative"). The wiki does not say whether a sweep's extra hits wear it.
    if (Options.bToolWear && Vitals.Mode != EFoundationPlayerMode::Creative && Weapon.DurabilityPerHit > 0 &&
        !Player.Inventory.GetHand(EFoundationHand::Main).IsEmpty())
    {
        Result.bToolWorn = true;
        Result.bToolBroke = Player.Inventory.Wear(EFoundationHand::Main, Weapon.DurabilityPerHit, Weapon.MaxDurability);
    }
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::Hit;
    Event.Player = Player.Id;
    Event.EntityId = Target->Id;
    Event.Amount = Result.DamageApplied;
    Event.bCritical = bCritical;
    Event.bKnockback = Result.bKnockback;
    Event.bSprintKnockback = bSprintKnockback;
    Event.Step = Facts.Step;
    AddEvent(Event);
    if (Result.bTargetDied)
    {
        Event.Kind = EFoundationEventKind::TargetDown;
        AddEvent(Event);
    }
    // A sweep follows a landed hit only: the primary's box grown, and positions (the box's bottom centre) within reach
    // of the player's feet.
    if (bSweep)
    {
        Result.bSweep = true;
        const FBox Reach = Target->Box.ExpandBy(Rules.SweepGrowCm);
        // 1 + ratio x the base melee damage, which the cooldown scales (wiki "Sweep attack", "Damage"); never a
        // critical.
        const float SweepDamage = Rules.SweepDamage + Rules.SweepingDamageRatio * Damage;
        for (FFoundationTrainingTarget& Other : Targets)
        {
            const FVector Position(Other.Box.GetCenter().X, Other.Box.GetCenter().Y, Other.Box.Min.Z);
            if (&Other == Target || Other.Health.IsDead() || !Overlaps(Reach, Other.Box) ||
                FVector::DistSquared(Facts.FeetCm, Position) >= FMath::Square(Rules.SweepReachCm))
                continue;
            const float OtherBefore = Other.Health.Health;
            const bool bOtherFresh = !Other.Health.IsInHurtWindow();
            if (!Other.Health.Hurt(EFoundationDamage::PlayerAttack, SweepDamage)) continue;
            FFoundationEvent Swept;
            Swept.Kind = EFoundationEventKind::Hit;
            Swept.Player = Player.Id;
            Swept.EntityId = Other.Id;
            Swept.Amount = OtherBefore - Other.Health.Health;
            Swept.bKnockback = bOtherFresh;
            Swept.bSweep = true;
            Swept.Step = Facts.Step;
            ++Other.Hits;
            Other.DamageTaken += Swept.Amount;
            ++Hits;
            ++Result.SweptTargets;
            AddEvent(Swept);
            if (Other.Health.IsDead())
            {
                Swept.Kind = EFoundationEventKind::TargetDown;
                AddEvent(Swept);
            }
        }
    }
    if (Result.bToolBroke)
    {
        Event.Kind = EFoundationEventKind::ToolBroke;
        AddEvent(Event);
    }
}

void FFoundationAuthority::Whiff(
    FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationVitals& Vitals, FFoundationResult& Result)
{
    if (!CheckHand(Player, Command, Result)) return;
    const FFoundationHit Hit = Resolve(Player.Facts);
    if (Hit.Kind != EFoundationHitKind::None && Hit.bInRange)
    {
        Reject(Result, EFoundationReason::TargetMismatch);
        return;
    }
    Vitals.ResetAttackStrength();
    Result.Outcome = EFoundationOutcome::Committed;
    Result.bChargeReset = true;
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::Whiff;
    Event.Player = Player.Id;
    Event.Step = Player.Facts.Step;
    AddEvent(Event);
}

void FFoundationAuthority::SettleAfterFluid(const FIntVector& Cell, FFoundationBlockState Before, bool bWashedAway)
{
    // No player made the change: player 0, step 0.
    static const FFoundationPlayer Nobody;
    if (bWashedAway)
    {
        // The fluid already holds the cell; the block pops with no drop, as every pop until item entities exist (plan
        // Q7).
        FFoundationEvent Event;
        Event.Kind = EFoundationEventKind::Popped;
        Event.Player = Nobody.Id;
        Event.Cell = Cell;
        Event.State = Before;
        ++Pops;
        AddEvent(Event);
    }
    // Lava meeting water hardened the cell (step 17, foundation.Items.Use): block.lava.extinguish plays there.
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    const FFoundationFluid Was = Registry.GetFluid(Before);
    const FFoundationBlockState Now = World.Get(Cell).State;
    const FName NowId = Registry.GetBlock(Now).Id;
    if (Options.bItemUse && (Was.bWater || Was.bLava) &&
        (NowId == TEXT("stone") || NowId == TEXT("obsidian") || NowId == TEXT("cobblestone") ||
            NowId == TEXT("basalt")))
    {
        FFoundationEvent Event;
        Event.Kind = EFoundationEventKind::Extinguished;
        Event.Player = Nobody.Id;
        Event.Cell = Cell;
        Event.State = Now;
        AddEvent(Event);
    }
    SettlePlants(Cell, Before, Nobody);
}
