#include "Foundation/FoundationAuthority.h"
#include "Algo/AllOf.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Foundation/FoundationBlockFamilies.h"
#include "Foundation/FoundationDeath.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationPlantRules.h"
#include "Foundation/FoundationSimulation.h"
#include "Foundation/FoundationStripping.h"
#include "HAL/IConsoleManager.h"
#include "Sky/VoxelTimeline.h"

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

    // A furnace, smoker or blast furnace (T1 steps 8a, 8b) opens its menu over its block entity, made on first use.
    // foundation.Furnace 0: no use, so the click passes on (a held block places against it), as before 8a (Codex
    // review 18, finding 1).
    bool WouldUseFurnace(const FFoundationAuthority& Authority, const FFoundationPlayer&, const FFoundationHit&)
    {
        return Authority.Options.bFurnace;
    }

    bool UseFurnace(FFoundationAuthority& Authority, FFoundationPlayer& Player, const FFoundationHit& Hit,
        FFoundationResult& Result)
    {
        if (!Authority.Options.bFurnace) return false;
        const FName Block = Authority.World.GetBlock(Authority.World.Get(Hit.Cell).State).Id;
        const EFoundationCookingKind Cooking = FoundationFurnace::GetCookingKind(Block);
        const EFoundationMenuKind Kind = Cooking == EFoundationCookingKind::Smoking ? EFoundationMenuKind::Smoker
            : Cooking == EFoundationCookingKind::Blasting                           ? EFoundationMenuKind::BlastFurnace
                                                                                    : EFoundationMenuKind::Furnace;
        Authority.OpenContainer(Player, Kind, Hit.Cell, Block, Result);
        if (Result.IsCommitted())
            Authority.BlockEntities.FindOrAdd(Hit.Cell, EFoundationBlockEntityKind::Furnace, Block);
        return true;
    }

    // A barrel (T1 step 9) opens its menu over its block entity, made on first use; its open state follows the
    // viewers (OpenContainer, UpdateBarrelOpen). foundation.Barrel 0: no use, so the click passes on.
    bool WouldUseBarrel(const FFoundationAuthority& Authority, const FFoundationPlayer&, const FFoundationHit&)
    {
        return Authority.Options.bBarrel;
    }

    bool UseBarrel(FFoundationAuthority& Authority, FFoundationPlayer& Player, const FFoundationHit& Hit,
        FFoundationResult& Result)
    {
        if (!Authority.Options.bBarrel) return false;
        const FName Block = Authority.World.GetBlock(Authority.World.Get(Hit.Cell).State).Id;
        Authority.OpenContainer(Player, EFoundationMenuKind::Barrel, Hit.Cell, Block, Result);
        if (Result.IsCommitted())
            Authority.BlockEntities.FindOrAdd(Hit.Cell, EFoundationBlockEntityKind::Barrel, Block);
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
    const FFoundationPlayer* Leaving = FindPlayer(Id);
    const TOptional<FIntVector> Barrel =
        Leaving && Leaving->OpenMenu.IsOpen() && Leaving->OpenMenu.Kind == EFoundationMenuKind::Barrel
        ? TOptional<FIntVector>(Leaving->OpenMenu.Cell)
        : NullOpt;
    Players.RemoveAll([Id](const FFoundationPlayer& Player) { return Player.Id == Id; });
    if (Barrel) UpdateBarrelOpen(*Barrel);
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

FFoundationContainerRules FFoundationAuthority::MakeContainerRules(FName Block) const
{
    FFoundationContainerRules Out;
    Out.GetMaxStack = [this](int32 Item) { return Rules.Items.IsValidIndex(Item) ? Rules.Items[Item].MaxStack : 64; };
    Out.GetEquipment = [this](int32 Item)
    { return Rules.Items.IsValidIndex(Item) ? Rules.Items[Item].Equipment : EFoundationEquipmentSlot::None; };
    Out.bCrafting = Options.bCrafting;
    // A furnace's fuel slot and shift-click routing (T1 step 8a), by what the open block cooks (8b,
    // survival.r3.shift_click: an oak log goes to a furnace's input, to a smoker's or blast furnace's fuel).
    Out.IsFuel = [this](int32 Item) { return GetCooking().IsFuel(Item); };
    Out.IsCookable = [this, Cooking = FoundationFurnace::GetCookingKind(Block)](int32 Item)
    { return GetCooking().Find(Cooking, Item) != INDEX_NONE; };
    // The fuel slot's bucket, by the name the screen's drag preview uses too.
    Out.Bucket = Rules.FindItem(FFoundationMenuLayout::FuelBucketItem);
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
    if (!UsesBlockEntity(Player.OpenMenu.Kind)) return Player.OpenMenu.Grid;
    const FFoundationBlockEntity* Entity = BlockEntities.Find(Player.OpenMenu.Cell);
    return Entity ? Entity->Items : TArray<FFoundationItemInstance>();
}

TArray<FFoundationItemInstance> FFoundationAuthority::GetOpenMenu(const FFoundationPlayer& Player) const
{
    TArray<FFoundationItemInstance> Slots;
    if (!Player.OpenMenu.IsOpen()) return Slots;
    FFoundationInventory Copy = Player.Inventory;
    TArray<FFoundationItemInstance> Grid = GetMenuGrid(Player);
    const FFoundationContainerRules ClickRules = MakeContainerRules(Player.OpenMenu.Block);
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
        {TEXT("furnace"), {FBlockUse(&UseFurnace), FBlockWouldUse(&WouldUseFurnace)}},
        {TEXT("smoker"), {FBlockUse(&UseFurnace), FBlockWouldUse(&WouldUseFurnace)}},
        {TEXT("blast_furnace"), {FBlockUse(&UseFurnace), FBlockWouldUse(&WouldUseFurnace)}},
        {TEXT("barrel"), {FBlockUse(&UseBarrel), FBlockWouldUse(&WouldUseBarrel)}}};
    if (const FBlockUseEntry* Use = BlockUses.Find(BlockId)) return Use;
    if (const FBlockUseEntry* Use = BuiltIn.Find(BlockId)) return Use;
    // Doors, trapdoors and fence gates (survival B3a, foundation.Blocks.Interact), by family; iron has none.
    static const FBlockUseEntry FamilyUse{FBlockUse(&FoundationBlockFamilies::Use), nullptr};
    const FFoundationBlockType* Block = World.FindBlock(BlockId);
    return FoundationBlockFamilies::IsInteractOn() && Block && FoundationBlockFamilies::IsUsable(*Block) ? &FamilyUse
                                                                                                         : nullptr;
}

void FFoundationAuthority::CommitBlockUse(const FFoundationPlayer& Player, const FIntVector& Cell,
    TConstArrayView<TPair<FIntVector, FFoundationBlockState>> Writes, FName Sound, FFoundationResult& Result,
    float Volume, float Pitch)
{
    if (!Algo::AllOf(
            Writes, [this](const TPair<FIntVector, FFoundationBlockState>& Write) { return World.IsKnown(Write.Key); }))
    {
        Reject(Result, EFoundationReason::OutsideBounds);
        return;
    }
    TArray<FFoundationBlockState, TInlineAllocator<2>> Before;
    for (const TPair<FIntVector, FFoundationBlockState>& Write : Writes)
        Before.Add(World.Get(Write.Key).State);
    World.SetMany(Writes);
    for (int32 Index = 0; Index < Writes.Num(); ++Index)
        SettlePlants(Writes[Index].Key, Before[Index], Player, false);
    Result.Outcome = EFoundationOutcome::Committed;
    Result.bInteracted = true;
    Result.Cell = Cell;
    Result.State = World.Get(Cell).State;
    Result.Interaction = Sound;
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::BlockUsed;
    Event.Player = Player.Id;
    Event.Cell = Cell;
    Event.State = Result.State;
    Event.Interaction = Sound;
    Event.Amount = Volume;
    Event.Pitch = Pitch;
    Event.Step = Player.Facts.Step;
    AddEvent(Event);
}

void FFoundationAuthority::ScheduleBlockTick(
    const FIntVector& Cell, FFoundationBlockState Expect, int32 Delay, EFoundationBlockTick Kind)
{
    // A cell keeps one tick per kind, the newest: a button broken and replaced, then pressed, is released by its own
    // press, not the old one's (B3b review).
    BlockTicks.RemoveAll([&Cell, Kind](const FBlockTick& Tick) { return Tick.Cell == Cell && Tick.Kind == Kind; });
    BlockTicks.Add({Cell, Expect, FMath::Max(Delay, 1), Kind});
}

void FFoundationAuthority::StepBlockTicks()
{
    // Due ticks leave the list before they run, so a tick that schedules another (a button pressed again) keeps it.
    TArray<FBlockTick, TInlineAllocator<8>> Due;
    for (int32 Index = 0; Index < BlockTicks.Num();)
        if (--BlockTicks[Index].Remaining <= 0)
        {
            Due.Add(BlockTicks[Index]);
            BlockTicks.RemoveAt(Index);
        }
        else
            ++Index;
    for (const FBlockTick& Tick : Due)
    {
        // A cell nobody knows drops its tick (edits are not saved, so the cell comes back as generated).
        if (!World.IsKnown(Tick.Cell) || World.Get(Tick.Cell).State != Tick.Expect) continue;
        switch (Tick.Kind)
        {
        case EFoundationBlockTick::ButtonRelease:
            FoundationBlockFamilies::ReleaseButton(*this, Tick.Cell);
            break;
        }
    }
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
    // A barrel this menu replaces loses its viewer (T1 step 9).
    const TOptional<FIntVector> LeftBarrel =
        Player.OpenMenu.IsOpen() && Player.OpenMenu.Kind == EFoundationMenuKind::Barrel
        ? TOptional<FIntVector>(Player.OpenMenu.Cell)
        : NullOpt;
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
    if (LeftBarrel && *LeftBarrel != Cell) UpdateBarrelOpen(*LeftBarrel);
    if (Kind == EFoundationMenuKind::Barrel) UpdateBarrelOpen(Cell);
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

void FFoundationAuthority::ResetInventory(int32 PlayerId)
{
    if (FFoundationPlayer* Player = FindPlayer(PlayerId)) Player->Inventory = MakeLoadout();
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
        Player.BlockingDisabledItem = INDEX_NONE;
        Player.BlockingDisabledUntilStep = 0;
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
    BlockTicks.Empty();
    Mobs.Empty();
    MobSpawnColumns.Empty();
    MobColumnTops.Empty();
    MobEmitters.Empty();
    MobCycleStep = 0;
    PathStart = 0;
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
    StepBlockTicks();
    StepItemEntities();
    StepMobs();
    StepMobSpawns();
    StepExperienceOrbs();
    StepOpenMenus();
    StepFurnaces();
}

bool FFoundationAuthority::IsMenuValid(const FFoundationPlayer& Player) const
{
    const FFoundationOpenMenu& Open = Player.OpenMenu;
    if (!Open.IsOpen() || !World.IsKnown(Open.Cell) || World.GetBlock(World.Get(Open.Cell).State).Id != Open.Block)
        return false;
    if (UsesBlockEntity(Open.Kind) && !BlockEntities.Find(Open.Cell)) return false;
    // The squared distance from the eye to the block's box strictly below (interaction range + 4 blocks)^2: 8.5 blocks
    // at the normal 4.5 (vanilla's stillValid; survival2.r23.container_rule).
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
    if (bThrown && !Left.IsEmpty() && Options.bItemEntities && Player.Facts.bAlive)
        for (const FFoundationItemInstance& Stack : Left)
            bThrown &= Throw(Player, Stack, Stack.Count);
    if (!Left.IsEmpty() && !Options.bItemEntities) Reject(Result, EFoundationReason::DropDeferred);
    // A dead player throws nothing, as Drop's CheckHand refuses (Codex review 11, finding 2); the close is retried each
    // step and goes through after the respawn.
    else if (!Left.IsEmpty() && !Player.Facts.bAlive)
        Reject(Result, EFoundationReason::Dead);
    else if (!bThrown)
        Reject(Result, EFoundationReason::NoSupply);
    else
    {
        ++Trial.Revision;
        Player.Inventory = MoveTemp(Trial);
        const FFoundationOpenMenu Closed = MoveTemp(Player.OpenMenu);
        Player.OpenMenu = {};
        Result.Outcome = EFoundationOutcome::Committed;
        if (Closed.IsOpen() && Closed.Kind == EFoundationMenuKind::Barrel) UpdateBarrelOpen(Closed.Cell);
    }
}

void FFoundationAuthority::UpdateBarrelOpen(const FIntVector& Cell)
{
    static const FName BarrelId(TEXT("barrel")), Open(TEXT("open")), Facing(TEXT("facing")), True(TEXT("true")),
        False(TEXT("false"));
    if (!World.IsKnown(Cell)) return;
    const FFoundationBlockState State = World.Get(Cell).State;
    if (World.GetBlock(State).Id != BarrelId) return;
    const bool bViewed = Players.ContainsByPredicate(
        [&Cell](const FFoundationPlayer& Player)
        {
            return Player.OpenMenu.IsOpen() && Player.OpenMenu.Kind == EFoundationMenuKind::Barrel &&
                Player.OpenMenu.Cell == Cell;
        });
    const FFoundationBlockState Want = World.GetRegistry().WithValue(State, Open, bViewed ? True : False);
    if (Want == State) return;
    World.Set(Cell, Want);
    // The sound plays from the centre of the face the barrel looks toward (R16's barrel rows: facing north, z .0 where
    // the chest's is .5), volume 0.5, pitch 0.9 + 0.1U.
    const FName FacingValue = World.GetRegistry().GetValue(State, Facing);
    FVector Normal = FVector::ZeroVector;
    for (int32 Face = 0; Face < 6; ++Face)
        if (FacingValue == FoundationGrid::FaceName(EFoundationFace(Face)))
            Normal = FoundationGrid::FaceNormal(EFoundationFace(Face));
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::BlockUsed;
    Event.Cell = Cell;
    Event.State = Want;
    Event.Interaction = bViewed ? FName(TEXT("block.barrel.open")) : FName(TEXT("block.barrel.close"));
    Event.Amount = FoundationBarrel::SoundVolume;
    Event.LocationCm =
        FoundationGrid::CellBox(Cell, World.OriginCm).GetCenter() + Normal * (UFoundationProfile::BlockCm * .5);
    AddEvent(Event);
}

void FFoundationAuthority::StepFurnaces()
{
    const FFoundationFurnaceRules FurnaceRules = MakeFurnaceRules();
    TArray<FIntVector, TInlineAllocator<4>> Gone;
    TArray<TPair<FIntVector, FFoundationBlockState>, TInlineAllocator<4>> Relit;
    for (auto& [Cell, Entity] : BlockEntities.GetAll())
    {
        // Only loaded columns tick (vanilla ticks loaded chunks only); the entity waits for its column.
        if (!World.IsKnown(Cell)) continue;
        // A furnace or barrel replaced without a break (a command, a reset of the cell) leaves nothing behind, with the
        // switch on or off, so one placed there later starts empty.
        if (World.GetBlock(World.Get(Cell).State).Id != Entity.Block)
        {
            Gone.Add(Cell);
            continue;
        }
        if (Entity.Kind != EFoundationBlockEntityKind::Furnace || !Options.bFurnace) continue;
        FoundationFurnace::Tick(Entity, FurnaceRules, BlockEntities, FoundationFurnace::GetCookingKind(Entity.Block));
        // The block's lit state follows the fuel on the same tick (survival.r3.furnace_ticks: lit at tick 1, unlit at
        // fuel + 1; its light comes with the state). Same block id, so the entity stays; a plain state change, as an
        // axe's strip is, with no place event. Written after the loop, so no world hook runs mid-iteration. Switched
        // off mid-burn, a block keeps its last lit state, as its entity keeps its timers.
        const FFoundationBlockState State = World.Get(Cell).State;
        static const FName Lit(TEXT("lit")), True(TEXT("true")), False(TEXT("false"));
        const FFoundationBlockState Want = World.GetRegistry().WithValue(State, Lit, Entity.IsLit() ? True : False);
        if (Want != State) Relit.Add({Cell, Want});
    }
    for (const FIntVector& Cell : Gone)
        BlockEntities.Remove(Cell);
    for (const TPair<FIntVector, FFoundationBlockState>& Change : Relit)
        World.Set(Change.Key, Change.Value);
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

int32 FFoundationAuthority::RemoveItemEntities()
{
    const int32 Count = ItemEntities.Num();
    ItemEntities.Reset();
    return Count;
}

int32 FFoundationAuthority::RemoveExperienceOrbs()
{
    const int32 Count = ExperienceOrbs.Num();
    ExperienceOrbs.Reset();
    return Count;
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

TArray<FFoundationMobHit> FFoundationAuthority::TakeMobHits(int32 PlayerId)
{
    FFoundationPlayer* Player = FindPlayer(PlayerId);
    return Player ? MoveTemp(Player->MobHits) : TArray<FFoundationMobHit>();
}

namespace
{
    // FFoundationInventory::Armor's order, vanilla's slots 36-39: feet, legs, chest, head.
    constexpr EFoundationEquipmentSlot ArmorOrder[] = {EFoundationEquipmentSlot::Feet, EFoundationEquipmentSlot::Legs,
        EFoundationEquipmentSlot::Chest, EFoundationEquipmentSlot::Head};

    int32 GetArmorIndex(EFoundationEquipmentSlot Slot)
    {
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(ArmorOrder); ++Index)
            if (ArmorOrder[Index] == Slot) return Index;
        return INDEX_NONE;
    }
}

void FFoundationAuthority::GetWornArmor(int32 PlayerId, int32& OutArmor, float& OutToughness) const
{
    OutArmor = 0;
    OutToughness = 0;
    const FFoundationPlayer* Player = FindPlayer(PlayerId);
    if (!Player || !Options.bFullInventory) return;
    const TArray<FFoundationItemInstance>& Armor = Player->Inventory.Armor;
    for (int32 Index = 0; Index < FMath::Min(Armor.Num(), int32(UE_ARRAY_COUNT(ArmorOrder))); ++Index)
        if (!Armor[Index].IsEmpty() && Rules.Items.IsValidIndex(Armor[Index].Item))
        {
            // An attribute modifier counts only in its own slot (a piece set elsewhere by a creative command adds
            // none).
            const FFoundationItemDefinition& Item = Rules.Items[Armor[Index].Item];
            if (Item.Equipment != ArmorOrder[Index]) continue;
            OutArmor += Item.Armor;
            OutToughness += Item.ArmorToughness;
        }
}

int32 FFoundationAuthority::WearArmor(int32 PlayerId, int32 Amount, EFoundationPlayerMode Mode, int32 FireAmount)
{
    FFoundationPlayer* Player = FindPlayer(PlayerId);
    if (!Player || Amount <= 0 || !Options.bToolWear || !Options.bFullInventory ||
        Mode == EFoundationPlayerMode::Creative)
        return 0;
    int32 Broken = 0;
    FFoundationInventory& Inventory = Player->Inventory;
    for (int32 Index = 0; Index < FMath::Min(Inventory.Armor.Num(), int32(UE_ARRAY_COUNT(ArmorOrder))); ++Index)
    {
        FFoundationItemInstance Stack = Inventory.Armor[Index];
        if (Stack.IsEmpty() || !Rules.Items.IsValidIndex(Stack.Item)) continue;
        const FFoundationItemDefinition& Item = Rules.Items[Stack.Item];
        const int32 Wear = Item.bFireResistant ? Amount - FMath::Clamp(FireAmount, 0, Amount) : Amount;
        if (Item.Equipment != ArmorOrder[Index] || Item.MaxDurability <= 0 || Wear <= 0) continue;
        Stack.Damage += Wear;
        const bool bBroke = Stack.Damage >= Item.MaxDurability;
        Inventory.SetSlot(FFoundationInventory::FirstArmor + Index, bBroke ? FFoundationItemInstance{} : Stack);
        if (!bBroke) continue;
        ++Broken;
        FFoundationEvent Event;
        Event.Kind = EFoundationEventKind::ToolBroke;
        Event.Player = Player->Id;
        Event.Item = Stack.Item;
        Event.Step = Player->Facts.Step;
        AddEvent(Event);
    }
    return Broken;
}

bool FFoundationAuthority::UseEquip(
    FFoundationPlayer& Player, const FFoundationCommand& Command, EFoundationPlayerMode Mode, FFoundationResult& Result)
{
    // Vanilla's equippable swap (SURVIVAL-RUN2-FINDINGS, R19, survival2.r19.equip): the piece goes on and what was worn
    // comes back to the hand; in creative an empty slot gets a copy and the hand keeps the piece.
    const FFoundationItemInstance Held = Player.Inventory.GetHand(Command.Hand);
    // A spectator wears nothing; one piece at a time, as the armor slots take (a lab stack of several does not go on).
    if (Held.IsEmpty() || Held.Count != 1 || !Options.bFullInventory || !Options.bItemUse ||
        Mode == EFoundationPlayerMode::Spectator)
        return false;
    const FFoundationItemDefinition& Item = Rules.GetItem(Held.Item);
    const int32 Index = GetArmorIndex(Item.Equipment);
    if (Index == INDEX_NONE || !Item.bSwappable || !Player.Inventory.Armor.IsValidIndex(Index)) return false;
    const FFoundationItemInstance Worn = Player.Inventory.Armor[Index];
    if (Mode == EFoundationPlayerMode::Creative && Worn.IsEmpty())
    {
        FFoundationItemInstance Copy = Player.Inventory.MakeStack(Held.Item, 1);
        Copy.Damage = Held.Damage;
        Player.Inventory.SetSlot(FFoundationInventory::FirstArmor + Index, Copy);
        Result.Outcome = EFoundationOutcome::Committed;
        return true;
    }
    Player.Inventory.SetSlot(
        Command.Hand == EFoundationHand::Off ? FFoundationInventory::OffhandSlot : Player.Inventory.Selected, Worn);
    Player.Inventory.SetSlot(FFoundationInventory::FirstArmor + Index, Held);
    Result.Outcome = EFoundationOutcome::Committed;
    return true;
}

void FFoundationAuthority::AddEquipEvents(
    const FFoundationPlayer& Player, TConstArrayView<FFoundationItemInstance> Before)
{
    // Vanilla plays a piece's equip_sound when its slot takes a different item (survival.r5.play_calls: PLAYERS,
    // volume 1, pitch 1); the view plays it from this event.
    const TArray<FFoundationItemInstance>& Armor = Player.Inventory.Armor;
    for (int32 Index = 0; Index < FMath::Min(Armor.Num(), Before.Num()); ++Index)
    {
        const FFoundationItemInstance& Now = Armor[Index];
        if (Now.IsEmpty() || (!Before[Index].IsEmpty() && Before[Index].InstanceId == Now.InstanceId)) continue;
        FFoundationEvent Event;
        Event.Kind = EFoundationEventKind::Equipped;
        Event.Player = Player.Id;
        Event.Item = Now.Item;
        Event.LocationCm = Player.Facts.FeetCm;
        Event.Step = Player.Facts.Step;
        AddEvent(Event);
    }
}

void FFoundationAuthority::SetRespawnPoint(int32 PlayerId, const FIntVector& Cell)
{
    if (FFoundationPlayer* Player = FindPlayer(PlayerId)) Player->RespawnCell = Cell;
}

void FFoundationAuthority::ClearRespawnPoint(int32 PlayerId)
{
    if (FFoundationPlayer* Player = FindPlayer(PlayerId)) Player->RespawnCell.Reset();
}

TOptional<FIntVector> FFoundationAuthority::GetRespawnPoint(int32 PlayerId) const
{
    const FFoundationPlayer* Player = FindPlayer(PlayerId);
    return Player ? Player->RespawnCell : TOptional<FIntVector>();
}

void FFoundationAuthority::DropInventoryOnDeath(int32 PlayerId, const FVector& FeetLocalCm, int32 RandomSeed)
{
    using namespace FoundationDeath;
    FFoundationPlayer* Player = FindPlayer(PlayerId);
    // Without item entities (foundation.Items.Entities 0) nothing could despawn or be picked up: keep the inventory.
    if (!Player || !Options.bItemEntities || Options.bInfiniteSupply || Player->Inventory.bInfiniteSupply) return;
    FFoundationInventory& Inventory = Player->Inventory;
    // Before the first facts the eye is the profile's standing height.
    const double EyeCm = Player->bHasFacts ? Player->Facts.EyeCm.Z - Player->Facts.FeetCm.Z : DefaultEyeCm;
    const FVector From = FeetLocalCm + FVector(0, 0, EyeCm - DropBelowEyeCm);
    FRandomStream Random(RandomSeed);
    const auto Drop = [&](FFoundationItemInstance& Stack)
    {
        if (Stack.IsEmpty()) return;
        // [items.drop.random_scatter] f = 0.5 x U, angle = 2 pi x U; vanilla's x = -sin(angle) x f, z = cos(angle) x f
        // on its horizontal plane, Unreal's X and Y here (Z up).
        const double Speed = Random.FRand() * DropScatterCm, Angle = Random.FRand() * 2 * UE_DOUBLE_PI;
        const FVector Velocity(-FMath::Sin(Angle) * Speed, FMath::Cos(Angle) * Speed, DropUpCm);
        SpawnItem(Stack.Item, Stack.Count, From, Velocity, DropPickupDelay, Stack.Damage);
        Stack = {};
    };
    for (FFoundationItemInstance& Stack : Inventory.Hotbar)
        Drop(Stack);
    for (FFoundationItemInstance& Stack : Inventory.Main)
        Drop(Stack);
    for (FFoundationItemInstance& Stack : Inventory.Armor)
        Drop(Stack);
    Drop(Inventory.Offhand);
    Drop(Inventory.Carried);
    for (FFoundationItemInstance& Stack : Inventory.Craft)
        Drop(Stack);
    // An open crafting table's grid drops too; closing the screen afterwards then finds it empty.
    for (FFoundationItemInstance& Stack : Player->OpenMenu.Grid)
        Drop(Stack);
    ++Inventory.Revision;
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
        ++Orb.Ticks;
        const bool bDespawn = Orb.Age >= DespawnAge;
        OrbsDespawned += bDespawn ? 1 : 0;
        if (Orb.LocalCm.Z < VoidCm || bDespawn)
            ExperienceOrbs.RemoveAt(Index);
        else
            ++Index;
    }
    // SURVIVAL-RUN2-FINDINGS, R23 (survival2.r23.xp_rules, xp_merge): newly awarded XP (a new orb, in the step it
    // spawned) searches a 1 x 1 x 1 block box around its position and goes into the orb it finds, which stays where it
    // is with age 0; an orb's own scan (on its steps 1, 21, 41, ...: tickCount % 20 == 1) searches its box grown 0.5
    // blocks and keeps the scanner, with the lower age. Both take only same-value orbs CanMerge allows (Codex review 8
    // #1). Scan removes what merges and says whether the seeker itself went.
    const auto Scan = [this, &Origin](int32& Index, bool bReward)
    {
        // The reward searches at the moment the XP was awarded: the new orb's position and the other orbs' before this
        // step's moves (review 19a2; Codex review 31, finding 3).
        const FVector SeekerCm =
            Origin + (bReward ? ExperienceOrbs[Index].PreviousLocalCm : ExperienceOrbs[Index].LocalCm);
        const FBox Reach = bReward ? FBox::BuildAABB(SeekerCm, FVector(RewardSearchCm * .5)) : GetMergeBox(SeekerCm);
        for (int32 Other = ExperienceOrbs.Num() - 1; Other >= 0; --Other)
        {
            const FFoundationExperienceOrb& Seeking = ExperienceOrbs[Index];
            const FFoundationExperienceOrb& Found = ExperienceOrbs[Other];
            if (Other == Index || Found.bFrozen || Found.Value != Seeking.Value ||
                !Overlaps(Reach, GetBox(Origin + (bReward ? Found.PreviousLocalCm : Found.LocalCm))) ||
                // Only the reward's search rolls; a scan needs ids a multiple of MergeIdPeriod apart.
                !(bReward ? CanMerge(Seeking, Found) : (Seeking.Id - Found.Id) % MergeIdPeriod == 0))
                continue;
            const int32 Gone = bReward ? Index : Other;
            FFoundationExperienceOrb& Into = ExperienceOrbs[bReward ? Other : Index];
            Into.Count += ExperienceOrbs[Gone].Count;
            Into.Age = bReward ? 0 : FMath::Min(Into.Age, ExperienceOrbs[Gone].Age);
            ++OrbsMerged;
            ExperienceOrbs.RemoveAt(Gone);
            if (bReward)
            {
                --Index;
                return true;
            }
            if (Other < Index) --Index;
        }
        return false;
    };
    // The rewards' searches come first (vanilla's happens when the XP is awarded, before the orb ticks); a new orb
    // that found none is an orb like any other and scans on its first step.
    for (int32 Index = 0; Index < ExperienceOrbs.Num(); ++Index)
        if (ExperienceOrbs[Index].bNew && !ExperienceOrbs[Index].bFrozen) Scan(Index, true);
    for (int32 Index = 0; Index < ExperienceOrbs.Num(); ++Index)
        if (!ExperienceOrbs[Index].bFrozen && ExperienceOrbs[Index].Ticks % MergeTicks == 1) Scan(Index, false);
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

int32 FFoundationAuthority::SpawnMob(EFoundationMobType Type, const FVector& LocalCm, float Yaw)
{
    using namespace FoundationMobs;
    if (!Options.bMobs || Type >= EFoundationMobType::Count) return 0;
    const FFoundationMobKind& Kind = GetKind(Type);
    FFoundationMob& Mob = Mobs.AddDefaulted_GetRef();
    Mob.Id = NextMobId++;
    Mob.Type = Type;
    Mob.LocalCm = Mob.PreviousLocalCm = LocalCm;
    Mob.Yaw = Mob.PreviousYaw = Mob.HeadYaw = Mob.PreviousHeadYaw = Yaw;
    Mob.Health.MaxHealth = Mob.Health.Health = Kind.MaxHealth;
    Mob.Health.Armor = FMath::RoundToInt32(Kind.Armor);
    Mob.Health.ArmorToughness = Kind.Toughness;
    Mob.Health.bNaturalRegeneration = false;
    // Deterministic per mob: the rules' loot seed and the id.
    Mob.Random.Initialize(int32(HashCombine(GetTypeHash(Mob.Id), GetTypeHash(Rules.LootSeed))));
    Mob.AmbientTicks = AmbientMinTicks + Mob.Random.RandHelper(AmbientRandomTicks);
    if (Type == EFoundationMobType::Chicken) Mob.EggTicks = EggMinTicks + Mob.Random.RandHelper(EggRandomTicks);
    ++MobsSpawned;
    return Mob.Id;
}

void FFoundationAuthority::AddMobSound(const FFoundationMob& Mob, const TCHAR* Sound, float Volume)
{
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::MobSound;
    Event.EntityId = Mob.Id;
    Event.Interaction =
        FName(FString::Printf(TEXT("entity.%s.%s"), *FoundationMobs::GetKind(Mob.Type).Id.ToString(), Sound));
    Event.LocationCm = World.OriginCm + Mob.LocalCm;
    Event.Amount = Volume;
    AddEvent(Event);
}

FFoundationMob* FFoundationAuthority::FindMob(int32 Id)
{
    return Mobs.FindByPredicate([Id](const FFoundationMob& Mob) { return Mob.Id == Id; });
}

int32 FFoundationAuthority::ClearMobs()
{
    const int32 Count = Mobs.Num();
    Mobs.Empty();
    return Count;
}

FFoundationPathCell FFoundationAuthority::GetPathCell(const FIntVector& Cell)
{
    // A cell the block world does not know (outside the bounds, a column not loaded) has the collision the mobs move
    // against there (a fixture, or an ungenerated column's solid) and no fluid.
    FFoundationPathCell Out;
    Out.bAvoid = World.IsKnown(Cell) && World.GetBlock(World.Get(Cell).State).IsFluid();
    // The boxes that start in the cell (a fence's reaches up into the next one); the pitcher crop's starts 1/16 below.
    const double Block = UFoundationProfile::BlockCm;
    const FVector Bottom = World.OriginCm + FVector(Cell) * Block;
    const FBox Region(Bottom + FVector(1), Bottom + FVector(Block - 1));
    TArray<FBox>& Boxes = PathBoxes;
    Boxes.Reset();
    if (GatherCollision)
        GatherCollision(Region, Boxes);
    else
    {
        World.GatherCollision(Region, Boxes);
        for (const FFoundationStatic& Static : Statics)
            if (Static.Box.Intersect(Region)) Boxes.Add(Static.Box);
    }
    for (const FBox& Box : Boxes)
        if (Box.Min.Z >= Bottom.Z - Block / 8) Out.Solid = FMath::Max(Out.Solid, float((Box.Max.Z - Bottom.Z) / Block));
    return Out;
}

bool FFoundationAuthority::CanGraze(const FFoundationMob& Mob) const
{
    const FIntVector Feet(FoundationGrid::FloorToCell(Mob.LocalCm.X), FoundationGrid::FloorToCell(Mob.LocalCm.Y),
        FoundationGrid::FloorToCell(Mob.LocalCm.Z + 1));
    return IsSheepFood(Feet) ||
        (World.IsKnown(Feet - FIntVector(0, 0, 1)) &&
            World.GetBlock(World.Get(Feet - FIntVector(0, 0, 1)).State).Id == TEXT("grass_block"));
}

bool FFoundationAuthority::IsSheepFood(const FIntVector& Cell) const
{
    // Codex review 23 #2: the block tag #minecraft:edible_for_sheep (short_grass, fern, short_dry_grass,
    // tall_dry_grass; Reference/minecraft-blocks-26.3/research/block_tags.csv).
    const FGameplayTag Edible = FGameplayTag::RequestGameplayTag(TEXT("Block.EdibleForSheep"), false);
    return Edible.IsValid() && World.IsKnown(Cell) && World.GetBlock(World.Get(Cell).State).Tags.HasTag(Edible);
}

void FFoundationAuthority::EatGrass(const FFoundationMob& Mob)
{
    // [survival.r10.ai_defaults] EatBlockGoal with mobGriefing on (the default): a plant tagged edible_for_sheep where
    // it stands breaks without drops, else the grass block below turns to dirt. The break's particles and sound wait
    // (a silent hook).
    const FIntVector Feet(FoundationGrid::FloorToCell(Mob.LocalCm.X), FoundationGrid::FloorToCell(Mob.LocalCm.Y),
        FoundationGrid::FloorToCell(Mob.LocalCm.Z + 1));
    const FIntVector Below = Feet - FIntVector(0, 0, 1);
    const FFoundationBlockState Here = World.IsKnown(Feet) ? World.Get(Feet).State : FFoundationBlockState();
    if (IsSheepFood(Feet))
        World.Set(Feet, World.GetRegistry().GetRemainder(Here));
    else if (const FFoundationBlockType* Dirt = World.FindBlock(TEXT("dirt"));
        Dirt && World.IsKnown(Below) && World.GetBlock(World.Get(Below).State).Id == TEXT("grass_block"))
        World.Set(Below, Dirt->GetDefaultState());
    else
        return;
    ++MobGrazes;
}

void FFoundationAuthority::StepMobs()
{
    using namespace FoundationMobs;
    if (Mobs.IsEmpty() || !Options.bMobs) return;
    const FVector& Origin = World.OriginCm;
    const double VoidCm = double(World.BoundsMin.Z - VoidDepthCells) * UFoundationProfile::BlockCm;
    TArray<FFoundationMobPlayer, TInlineAllocator<4>> Near;
    // [mobs.despawn] the nearest player of any state counts for despawning.
    TArray<FVector, TInlineAllocator<4>> PlayerFeet;
    for (const FFoundationPlayer& Player : Players)
        if (Player.bHasFacts) PlayerFeet.Add(Player.Facts.FeetCm);
    const bool bPeaceful = IsPeaceful();
    for (const FFoundationPlayer& Player : Players)
        if (Player.bHasFacts && Player.Facts.bAlive)
        {
            const auto ItemId = [this, &Player](EFoundationHand Hand)
            {
                const FFoundationItemInstance& Stack = Player.Inventory.GetHand(Hand);
                return Stack.IsEmpty() || !Rules.Items.IsValidIndex(Stack.Item) ? FName() : Rules.Items[Stack.Item].Id;
            };
            FFoundationMobPlayer& Seen = Near.Add_GetRef({Player.Id, Player.Facts.EyeCm, Player.Facts.FeetCm,
                ItemId(EFoundationHand::Main), ItemId(EFoundationHand::Off)});
            Seen.WidthCm = Player.Facts.WidthCm;
            Seen.HeightCm = Player.Facts.HeightCm;
            Seen.bTargetable = Player.Facts.Mode == EFoundationPlayerMode::Survival ||
                Player.Facts.Mode == EFoundationPlayerMode::Adventure;
        }
    // Hits are taken by the player's owner after each step; ones nobody took are dropped.
    for (FFoundationPlayer& Player : Players)
        Player.MobHits.Reset();
    // The cells A* reads this step, shared by its searches (at most PathsPerStep).
    TMap<FIntVector, FFoundationPathCell> PathCells;
    const auto Query = [this, &PathCells](const FIntVector& Cell)
    {
        if (const FFoundationPathCell* Known = PathCells.Find(Cell)) return *Known;
        return PathCells.Add(Cell, GetPathCell(Cell));
    };
    int32 Paths = 0, LastPlanned = PathStart;
    TArray<FBox> Boxes;
    for (int32 Index = 0; Index < Mobs.Num();)
    {
        FFoundationMob& Mob = Mobs[Index];
        const FFoundationMobKind& Kind = GetKind(Mob.Type);
        const FIntVector Cell(FoundationGrid::FloorToCell(Mob.LocalCm.X), FoundationGrid::FloorToCell(Mob.LocalCm.Y),
            FoundationGrid::FloorToCell(Mob.LocalCm.Z));
        // O16: on an unloaded column nothing runs, timers included.
        Mob.bFrozen = !World.GetStorage().IsLoaded(Cell);
        if (Mob.bFrozen)
        {
            Mob.PreviousLocalCm = Mob.LocalCm;
            Mob.PreviousYaw = Mob.Yaw;
            Mob.PreviousHeadYaw = Mob.HeadYaw;
            Mob.PreviousHeadPitch = Mob.HeadPitch;
            Mob.PreviousWalkPosition = Mob.WalkPosition;
            Mob.PreviousWalkSpeed = Mob.WalkSpeed;
            ++Index;
            continue;
        }
        // Monsters go far from every player, idle beyond the near distance on a roll, and all of them on Peaceful.
        if (IsMonster(Mob.Type) && Mob.IsAlive())
        {
            double Nearest = MAX_dbl;
            for (const FVector& Feet : PlayerFeet)
                Nearest = FMath::Min(Nearest, FVector::Dist(Feet, Origin + Mob.LocalCm));
            if (bPeaceful || (!PlayerFeet.IsEmpty() && ShouldDespawn(Mob, Nearest)))
            {
                Mobs.RemoveAt(Index);
                ++MobsDespawned;
                continue;
            }
        }
        ++Mob.Age;
        ++Mob.NoActionTicks;
        Mob.PreviousYaw = Mob.Yaw;
        Mob.Health.StepDamageCooldown();
        if (Mob.HurtTime > 0) --Mob.HurtTime;
        if (!Mob.IsAlive() && ++Mob.DeathTime >= RemoveAtDeathTime)
        {
            Mobs.RemoveAt(Index);
            continue;
        }
        if (IsMonster(Mob.Type)) StepMonster(Mob, Near);
        Think(Mob, Origin, Near, Mob.Type == EFoundationMobType::Sheep && Mob.GrazeTicks == 0 && CanGraze(Mob));
        if (Mob.bEatNow) EatGrass(Mob);
        StepSunBurn(Mob);
        // The budget goes first to the mobs after the one that planned last when it ran out (PathStart; 0: none
        // waited), then, after the loop, to the others.
        if (Mob.bNeedPath && Paths < PathsPerStep && Mob.Id > PathStart)
        {
            PlanPath(Mob, Query);
            ++Paths;
            ++MobPaths;
            LastPlanned = Mob.Id;
        }
        bool bJump = false;
        const FVector2D Input = Steer(Mob, Origin, bJump);
        GatherEntityCollision(GetStepRegion(Mob, Origin), GetBox(Mob.Type, Origin + Mob.LocalCm), Boxes);
        Move(Mob, Origin, Boxes, Input, bJump);
        StepMobHazards(Mob);
        Animate(Mob);
        if (Mob.IsAlive())
        {
            if (--Mob.AmbientTicks <= 0)
            {
                Mob.AmbientTicks = AmbientMinTicks + Mob.Random.RandHelper(AmbientRandomTicks);
                AddMobSound(Mob, TEXT("ambient"), Kind.Volume);
            }
            // [survival.r10.idle_and_sun] a living adult chicken lays an egg when its countdown runs out. Provisional:
            // the egg leaves its feet like a mob drop.
            if (Mob.Type == EFoundationMobType::Chicken && --Mob.EggTicks <= 0)
            {
                Mob.EggTicks = EggMinTicks + Mob.Random.RandHelper(EggRandomTicks);
                if (const int32 Egg = Rules.FindItem(TEXT("egg")); Egg != INDEX_NONE)
                {
                    // One draw per statement: argument order is unspecified.
                    const double Side = FoundationItemEntities::SpawnSidewaysCm;
                    const double X = Mob.Random.FRandRange(-Side, Side);
                    const double Y = Mob.Random.FRandRange(-Side, Side);
                    const FVector Velocity(X, Y, FoundationItemEntities::SpawnUpCm);
                    SpawnItem(Egg, 1, Mob.LocalCm, Velocity, FoundationItemEntities::BlockDropPickupDelay);
                    AddMobSound(Mob, TEXT("egg"), Kind.Volume);
                    ++MobEggs;
                }
            }
            if (Mob.bOnGround)
            {
                Mob.StepDistanceCm += FVector2D(Mob.LocalCm - Mob.PreviousLocalCm).Size();
                if (Mob.StepDistanceCm >= StepSoundCm)
                {
                    Mob.StepDistanceCm -= StepSoundCm;
                    AddMobSound(Mob, TEXT("step"), StepVolume);
                }
            }
        }
        if (Mob.LocalCm.Z < VoidCm)
            Mobs.RemoveAt(Index);
        else
            ++Index;
    }
    bool bWaiting = false;
    for (FFoundationMob& Mob : Mobs)
    {
        if (!Mob.bNeedPath || Mob.bFrozen || !Mob.IsAlive()) continue;
        if (Paths < PathsPerStep && Mob.Id <= PathStart)
        {
            PlanPath(Mob, Query);
            ++Paths;
            ++MobPaths;
            LastPlanned = Mob.Id;
        }
        else
            bWaiting = true;
    }
    PathStart = bWaiting ? LastPlanned : 0;
}

bool FFoundationAuthority::IsMobInFluid(const FBox& Body, bool bLava) const
{
    // As the generated world's GetFluid: a fluid's surface box (amount / 9 of the cell, full under the same fluid)
    // overlapping the body shrunk a hair.
    const FBox Inner = Body.ExpandBy(-.1);
    const FIntVector Low = FoundationGrid::CellAt(Inner.Min, World.OriginCm),
                     High = FoundationGrid::CellAt(Inner.Max, World.OriginCm);
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    const double Block = UFoundationProfile::BlockCm;
    for (int32 Z = Low.Z; Z <= High.Z; ++Z)
        for (int32 Y = Low.Y; Y <= High.Y; ++Y)
            for (int32 X = Low.X; X <= High.X; ++X)
            {
                const FIntVector Cell(X, Y, Z);
                if (!World.IsKnown(Cell)) continue;
                const FFoundationFluid Fluid = Registry.GetFluid(World.Get(Cell).State);
                if (!(bLava ? Fluid.bLava : Fluid.bWater)) continue;
                const FIntVector Up = Cell + FIntVector(0, 0, 1);
                const FFoundationFluid Above =
                    World.IsKnown(Up) ? Registry.GetFluid(World.Get(Up).State) : FFoundationFluid();
                const bool bSameAbove = bLava ? Above.bLava : Above.bWater;
                const FVector Bottom = World.OriginCm + FVector(Cell) * Block;
                const FBox Surface(Bottom, Bottom + FVector(Block, Block, Fluid.GetHeight(bSameAbove) * Block));
                if (Surface.Intersect(Inner)) return true;
            }
    return false;
}

void FFoundationAuthority::StepMobHazards(FFoundationMob& Mob)
{
    using namespace FoundationMobs;
    if (!Mob.IsAlive()) return;
    // [fall.damage_thresholds V:324, hurt.lava_contact V:289] the player's rules: landing damage past 3 blocks, lava
    // 4 per tick (through the damage cooldown) and 15 s of fire, burning 1 every 20 ticks, water puts it out. Lava and
    // fire only with Options.bFire (foundation.Fire), as the pawn. The chicken takes no fall damage (Codex T6 C).
    const FBox Body = GetBox(Mob.Type, World.OriginCm + Mob.LocalCm);
    FFoundationVitalsStep Facts;
    Facts.DisplacementCm = Mob.LocalCm - Mob.PreviousLocalCm;
    Facts.bGrounded = Mob.bOnGround;
    Facts.bBodyInWater = IsMobInFluid(Body, false);
    Facts.bBodyInLava = Options.bFire && IsMobInFluid(Body, true);
    const float Before = Mob.Health.Health;
    Mob.Health.StepHazards(Facts, Mob.Type == EFoundationMobType::Chicken);
    if (Mob.Health.Health >= Before) return;
    // Provisional: a hazard's hit shows the hurt overlay and plays the hurt or death sound like a player's, without
    // knockback or panic.
    Mob.HurtTime = HurtTicks;
    AddMobSound(Mob, Mob.IsAlive() ? TEXT("hurt") : TEXT("death"), GetKind(Mob.Type).Volume);
    if (!Mob.IsAlive()) DropMobLoot(Mob);
}

void FFoundationAuthority::StepMonster(FFoundationMob& Mob, TConstArrayView<FFoundationMobPlayer> Near)
{
    using namespace FoundationMobs;
    // The spider's targeting reads the light proxy's brightness at its eyes.
    float Brightness = 0;
    if (Mob.Type == EFoundationMobType::Spider)
    {
        const FVector Eye = Mob.LocalCm + FVector(0, 0, GetKind(Mob.Type).EyeCm);
        Brightness =
            GetBrightness(GetLightProxy(FIntVector(FoundationGrid::FloorToCell(Eye.X),
                                            FoundationGrid::FloorToCell(Eye.Y), FoundationGrid::FloorToCell(Eye.Z)),
                nullptr, nullptr, true));
    }
    ThinkHostile(
        Mob, World.OriginCm, Near,
        [this](const FVector& From, const FVector& To) { return HasMobLineOfSight(From, To); }, Brightness);
    if (!Mob.bAttackNow) return;
    // [mobs.base_attributes] the attack_damage attribute; the player's owner applies difficulty, armor and the
    // knockback (FFoundationVitals::Hurt).
    const FFoundationMobKind& Kind = GetKind(Mob.Type);
    if (FFoundationPlayer* Player = FindPlayer(Mob.Target))
    {
        Player->MobHits.Add({Mob.Id, Kind.AttackDamage, Kind.Name, World.OriginCm + Mob.LocalCm});
        ++MobMeleeHits;
    }
}

void FFoundationAuthority::StepSunBurn(FFoundationMob& Mob)
{
    using namespace FoundationMobs;
    // [survival.r10.idle_and_sun S 5457] the zombie (no helmet yet) catches fire for 8 s in daylight; with
    // foundation.Fire, as lava's fire. The light at its eyes is the proxy: the open sky's (VoxelSky::OpenSkyLight of
    // the day's sky_light_level) where it sees the sky; elsewhere nothing reaches 0.5 without block light (20c2).
    if (Mob.Type != EFoundationMobType::Zombie || !Mob.IsAlive() || !Options.bFire) return;
    float SkyLevel = SkyLightProxy;
    bool bBurns = false;
    if (GetSkyFacts) GetSkyFacts(SkyLevel, bBurns);
    if (!bBurns) return;
    const float Brightness = GetBrightness(VoxelSky::OpenSkyLight(SkyLevel));
    if (Brightness <= .5f || !RollSunBurn(Mob, Brightness)) return;
    const FVector Eye = Mob.LocalCm + FVector(0, 0, GetKind(Mob.Type).EyeCm);
    const FIntVector EyeCell(
        FoundationGrid::FloorToCell(Eye.X), FoundationGrid::FloorToCell(Eye.Y), FoundationGrid::FloorToCell(Eye.Z));
    if (!IsSkyVisible(EyeCell) || IsMobInFluid(GetBox(Mob.Type, World.OriginCm + Mob.LocalCm), false)) return;
    Mob.Health.FireTicks = FMath::Max(Mob.Health.FireTicks, SunBurnTicks);
    ++MobSunBurns;
}

void FFoundationAuthority::GatherMobBoxes(const FBox& Region, TArray<FBox>& Out)
{
    if (GatherCollision)
        GatherCollision(Region, Out);
    else
    {
        World.GatherCollision(Region, Out);
        for (const FFoundationStatic& Static : Statics)
            if (Static.Box.Intersect(Region)) Out.Add(Static.Box);
    }
}

bool FFoundationAuthority::HasMobLineOfSight(const FVector& FromCm, const FVector& ToCm)
{
    // The cells the segment crosses, in order (a grid walk), each tested against the boxes in it.
    const double Block = UFoundationProfile::BlockCm;
    const FVector From = (FromCm - World.OriginCm) / Block, To = (ToCm - World.OriginCm) / Block;
    FIntVector Cell(FMath::FloorToInt32(From.X), FMath::FloorToInt32(From.Y), FMath::FloorToInt32(From.Z));
    const FIntVector End(FMath::FloorToInt32(To.X), FMath::FloorToInt32(To.Y), FMath::FloorToInt32(To.Z));
    int32 Step[3];
    double Next[3], Delta[3];
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const double D = To[Axis] - From[Axis];
        Step[Axis] = D > 0 ? 1 : D < 0 ? -1 : 0;
        Delta[Axis] = Step[Axis] ? 1 / FMath::Abs(D) : MAX_dbl;
        Next[Axis] = Step[Axis] > 0 ? (Cell[Axis] + 1 - From[Axis]) / D
            : Step[Axis] < 0        ? (Cell[Axis] - From[Axis]) / D
                                    : MAX_dbl;
    }
    TArray<FBox>& Boxes = PathBoxes;
    for (int32 Guard = 0; Guard < 1024; ++Guard)
    {
        const FVector Bottom = World.OriginCm + FVector(Cell) * Block;
        Boxes.Reset();
        GatherMobBoxes(FBox(Bottom, Bottom + FVector(Block)), Boxes);
        for (const FBox& Box : Boxes)
            if (FMath::LineBoxIntersection(Box, FromCm, ToCm, ToCm - FromCm)) return false;
        if (Cell == End) return true;
        const int32 Axis = Next[0] <= Next[1] ? (Next[0] <= Next[2] ? 0 : 2) : (Next[1] <= Next[2] ? 1 : 2);
        if (Next[Axis] > 1) return true;
        Cell[Axis] += Step[Axis];
        Next[Axis] += Delta[Axis];
    }
    return true;
}

bool FFoundationAuthority::IsSkyVisible(const FIntVector& Cell)
{
    // The storage's motion-blocking heightmap when it keeps one (20c2), else a scan up to the world's top.
    if (const int32 Top = World.GetStorage().GetTopZ(Cell.X, Cell.Y, true); Top != MIN_int32) return Cell.Z > Top;
    for (FIntVector Above = Cell; Above.Z <= World.BoundsMax.Z; ++Above.Z)
    {
        if (!World.IsKnown(Above) || World.Get(Above).State.IsAir()) continue;
        const FFoundationPathCell Path = GetPathCell(Above);
        if (Path.bAvoid || Path.Solid > 0) return false;
    }
    return true;
}

void FFoundationAuthority::StepMobSpawns()
{
    using namespace FoundationMobs;
    ++MobCycleStep;
    if (!Options.bMobs || !Options.bMobSpawn || !GetBiomeId || Rules.BiomeSpawns.IsEmpty()) return;
    if (MobCycleStep % CreatureCycleTicks == 0) RunCreatureCycle();
    EmitterRebuilds = 0;
    if (MobCycleStep % MonsterCycleTicks == 0 && !IsPeaceful())
    {
        const double Start = FPlatformTime::Seconds();
        RunSpawnCycle(EFoundationMobCategory::Monster);
        const double Ms = (FPlatformTime::Seconds() - Start) * 1000;
        MonsterCycleMsTotal += Ms;
        MonsterCycleMsMax = FMath::Max(MonsterCycleMsMax, Ms);
    }
    for (const FFoundationPlayer& Player : Players)
    {
        if (!Player.bHasFacts) continue;
        const FIntVector Feet = FoundationGrid::CellAt(Player.Facts.FeetCm, World.OriginCm);
        const FIntPoint Center(FoundationGrid::FloorDivide(Feet.X, SpawnColumnCells),
            FoundationGrid::FloorDivide(Feet.Y, SpawnColumnCells));
        // Nearest rings first; at most SpawnColumnsPerStep new columns a step (the rest wait for the next).
        int32 Rolled = 0;
        for (int32 Ring = 0; Ring <= SpawnRingColumns && Rolled < SpawnColumnsPerStep; ++Ring)
            for (int32 DY = -Ring; DY <= Ring && Rolled < SpawnColumnsPerStep; ++DY)
                for (int32 DX = -Ring; DX <= Ring && Rolled < SpawnColumnsPerStep; ++DX)
                {
                    if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != Ring) continue;
                    const FIntPoint Column = Center + FIntPoint(DX, DY);
                    const FIntVector Corner(Column.X * SpawnColumnCells, Column.Y * SpawnColumnCells, 0);
                    if (MobSpawnColumns.Contains(Column) || !World.GetStorage().IsLoaded(Corner)) continue;
                    MobSpawnColumns.Add(Column);
                    ++MobColumnsSeen;
                    ++Rolled;
                    SpawnPack(Column);
                }
    }
}

int32 FFoundationAuthority::SpawnPack(const FIntPoint& Column)
{
    using namespace FoundationMobs;
    // Deterministic from the world seed and the column alone (never the order columns load in).
    FRandomStream Random(FoundationLoot::MakeSeed(MobSpawnSeed ^ 0x6d6f6273, FIntVector(Column.X, Column.Y, 0), 20));
    const int32 MinX = Column.X * SpawnColumnCells, MinY = Column.Y * SpawnColumnCells;
    const int32 X = MinX + Random.RandHelper(SpawnColumnCells);
    const int32 Y = MinY + Random.RandHelper(SpawnColumnCells);
    const float Roll = Random.GetFraction();
    // The biome above the top block of the pack's column.
    FIntVector Top(X, Y, World.BoundsMax.Z);
    while (Top.Z > World.BoundsMin.Z && World.IsKnown(Top) && World.Get(Top).State.IsAir())
        --Top.Z;
    const FFoundationBiomeSpawns* Biome = Rules.BiomeSpawns.Find(GetBiomeId(Top + FIntVector(0, 0, 1)));
    if (!Biome || Roll >= Biome->Chance) return 0;
    const FFoundationMobSpawn* Entry = PickSpawn(*Biome, Random);
    if (!Entry) return 0;
    const int32 Count = RollPackSize(*Entry, Random);
    const double Block = UFoundationProfile::BlockCm;
    int32 Spawned = 0;
    for (int32 Member = 0; Member < Count; ++Member)
    {
        const int32 OffsetX = RollPackOffset(Random);
        const int32 OffsetY = RollPackOffset(Random);
        const float Yaw = Random.FRandRange(0, 360);
        const int32 MemberX = FMath::Clamp(X + OffsetX, MinX, MinX + SpawnColumnCells - 1);
        const int32 MemberY = FMath::Clamp(Y + OffsetY, MinY, MinY + SpawnColumnCells - 1);
        FIntVector Feet;
        if (!FindPackCell(Entry->Type, MemberX, MemberY, Feet)) continue;
        if (SpawnMob(Entry->Type, FVector(Feet.X + .5, Feet.Y + .5, Feet.Z) * Block, Yaw)) ++Spawned;
    }
    if (Spawned > 0) ++MobPacks;
    MobsPackSpawned += Spawned;
    return Spawned;
}

void FFoundationAuthority::RunCreatureCycle()
{
    RunSpawnCycle(EFoundationMobCategory::Creature);
}

bool FFoundationAuthority::IsPeaceful() const
{
    for (const FFoundationPlayer& Player : Players)
        if (Player.bHasFacts && Player.Facts.Difficulty == EFoundationDifficulty::Peaceful) return true;
    return false;
}

void FFoundationAuthority::RunSpawnCycle(EFoundationMobCategory Category)
{
    using namespace FoundationMobs;
    const bool bMonster = Category == EFoundationMobCategory::Monster;
    const double Block = UFoundationProfile::BlockCm;
    TArray<FVector, TInlineAllocator<4>> PlayerCells;
    TArray<FIntPoint, TInlineAllocator<4>> Centers;
    for (const FFoundationPlayer& Player : Players)
        if (Player.bHasFacts)
        {
            PlayerCells.Add((Player.Facts.FeetCm - World.OriginCm) / Block);
            const FIntVector Feet = FoundationGrid::CellAt(Player.Facts.FeetCm, World.OriginCm);
            Centers.Emplace(FoundationGrid::FloorDivide(Feet.X, SpawnColumnCells),
                FoundationGrid::FloorDivide(Feet.Y, SpawnColumnCells));
        }
    // The eligible columns: loaded, within SpawnRadiusColumns of a live player.
    TArray<FIntPoint> Columns;
    TSet<FIntPoint> Eligible;
    for (const FIntPoint& Center : Centers)
        for (int32 DY = -SpawnRadiusColumns; DY <= SpawnRadiusColumns; ++DY)
            for (int32 DX = -SpawnRadiusColumns; DX <= SpawnRadiusColumns; ++DX)
            {
                const FIntPoint Column = Center + FIntPoint(DX, DY);
                if (Eligible.Contains(Column) ||
                    !World.GetStorage().IsLoaded(
                        FIntVector(Column.X * SpawnColumnCells, Column.Y * SpawnColumnCells, 0)))
                    continue;
                Eligible.Add(Column);
                Columns.Add(Column);
            }
    // The category's mobs counted (Codex 30 #1, #2): every live one not frozen (its column loaded) for the global
    // cap, those in each player's square for its local cap.
    const auto ColumnOf = [](const FFoundationMob& Mob)
    {
        return FIntPoint(FoundationGrid::FloorDivide(FoundationGrid::FloorToCell(Mob.LocalCm.X), SpawnColumnCells),
            FoundationGrid::FloorDivide(FoundationGrid::FloorToCell(Mob.LocalCm.Y), SpawnColumnCells));
    };
    const auto InSquare = [](const FIntPoint& A, const FIntPoint& B)
    { return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y)) <= SpawnRadiusColumns; };
    const int32 Cap = GetCategoryCap(Category, Columns.Num());
    const int32 LocalCap = GetBaseCap(Category);
    int32 Count = 0;
    TArray<int32, TInlineAllocator<4>> Local;
    Local.Init(0, Centers.Num());
    for (const FFoundationMob& Mob : Mobs)
        if (Mob.IsAlive() && !Mob.bFrozen && GetCategory(Mob.Type) == Category)
        {
            const FIntPoint Column = ColumnOf(Mob);
            ++Count;
            for (int32 Index = 0; Index < Centers.Num(); ++Index)
                Local[Index] += InSquare(Column, Centers[Index]) ? 1 : 0;
        }
    (bMonster ? MonsterCycleCount : MobCycleCount) = Count;
    (bMonster ? MonsterCycleCap : MobCycleCap) = Cap;
    if (!bMonster) MobCycleEligible = Columns.Num();
    if (Count >= Cap) return;
    ++(bMonster ? MonsterCycles : MobCycles);
    // The columns in a shuffled order, deterministic from the seed and the cycle. Monsters: one shuffle a pass, each
    // step its next MonsterColumnsPerStep (provisional, cost).
    const int64 Cycle = MobCycleStep / (bMonster ? MonsterCycleTicks : CreatureCycleTicks);
    const uint32 Salt = bMonster ? 0x6d6f6e73 : 0x6379636c;
    const int32 Passes = bMonster ? FMath::Max(1, FMath::DivideAndRoundUp(Columns.Num(), MonsterColumnsPerStep)) : 1;
    FRandomStream Order(FoundationLoot::MakeSeed(MobSpawnSeed ^ Salt, FIntVector(0, 0, int32(Cycle / Passes)), 21));
    for (int32 Index = Columns.Num() - 1; Index > 0; --Index)
        Columns.Swap(Index, Order.RandRange(0, Index));
    const int32 First = bMonster ? int32(Cycle % Passes) * MonsterColumnsPerStep : 0;
    const int32 Last = bMonster ? FMath::Min(First + MonsterColumnsPerStep, Columns.Num()) : Columns.Num();
    for (int32 Next = First; Next < Last; ++Next)
    {
        const FIntPoint& Column = Columns[Next];
        if (Count >= Cap) break;
        // A column spawns while a player whose square holds it is under its local cap.
        bool bUnder = false;
        for (int32 Index = 0; Index < Centers.Num() && !bUnder; ++Index)
            bUnder = InSquare(Column, Centers[Index]) && Local[Index] < LocalCap;
        if (!bUnder) continue;
        const int32 Spawned = SpawnCycleColumn(Column, PlayerCells, Cycle, Category);
        Count += Spawned;
        for (int32 Index = 0; Index < Centers.Num(); ++Index)
            Local[Index] += InSquare(Column, Centers[Index]) ? Spawned : 0;
    }
}

int32 FFoundationAuthority::SpawnCycleColumn(
    const FIntPoint& Column, TConstArrayView<FVector> PlayerCells, int64 Cycle, EFoundationMobCategory Category)
{
    using namespace FoundationMobs;
    const bool bMonster = Category == EFoundationMobCategory::Monster;
    // Deterministic from the world seed, the column and the cycle.
    FRandomStream Random(FoundationLoot::MakeSeed(
        MobSpawnSeed ^ (bMonster ? 0x6d6f6e73 : 0x6379636c), FIntVector(Column.X, Column.Y, int32(Cycle)), 22));
    const int32 StartX = Column.X * SpawnColumnCells + Random.RandHelper(SpawnColumnCells);
    const int32 StartY = Column.Y * SpawnColumnCells + Random.RandHelper(SpawnColumnCells);
    const double Block = UFoundationProfile::BlockCm;
    // Monsters: the round's height, from the world's bottom to one above the start column's top block; a full solid
    // there ends the column.
    int32 StartZ = 0;
    if (bMonster)
    {
        const FMobColumnTops Tops = GetColumnTops(StartX, StartY);
        if (Tops.NonAir < World.BoundsMin.Z) return 0;
        StartZ = Random.RandRange(World.BoundsMin.Z, FMath::Min(Tops.NonAir + 1, World.BoundsMax.Z));
        const FIntVector Start(StartX, StartY, StartZ);
        if (!World.IsKnown(Start) || GetPathCell(Start).Solid >= 1) return 0;
    }
    int32 Spawned = 0;
    for (int32 Round = 0; Round < GroupRounds; ++Round)
    {
        const FFoundationMobSpawn* Entry = nullptr;
        int32 Group = 0, InGroup = 0;
        const int32 Attempts = FMath::CeilToInt32(Random.GetFraction() * MaxRoundAttempts);
        for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
        {
            const int32 X = StartX + RollPackOffset(Random);
            const int32 Y = StartY + RollPackOffset(Random);
            const float Yaw = Random.FRandRange(0, 360);
            // Beyond MaxPlayerDistanceCells of every player across the ground: no column scan.
            bool bInReach = false;
            for (const FVector& Player : PlayerCells)
                bInReach |= FVector2D(X + .5 - Player.X, Y + .5 - Player.Y).Size() <= MaxPlayerDistanceCells;
            if (!bInReach) continue;
            // Creatures: the top block of the column; the distance test and the biome stand above it. Monsters: the
            // round's height.
            FIntVector Top(X, Y, bMonster ? StartZ - 1 : World.BoundsMax.Z);
            if (!bMonster)
            {
                while (Top.Z > World.BoundsMin.Z && World.IsKnown(Top) && World.Get(Top).State.IsAir())
                    --Top.Z;
                if (!World.IsKnown(Top) || World.Get(Top).State.IsAir()) continue;
            }
            const FVector At(X + .5, Y + .5, Top.Z + 1);
            double Nearest = TNumericLimits<double>::Max();
            for (const FVector& Player : PlayerCells)
                Nearest = FMath::Min(Nearest, FVector::Dist(At, Player));
            if (Nearest < MinPlayerDistanceCells || Nearest > MaxPlayerDistanceCells) continue;
            // The round's species: by weight from the biome where it first finds a place.
            if (!Entry)
            {
                const FFoundationBiomeSpawns* Biome = Rules.BiomeSpawns.Find(GetBiomeId(Top + FIntVector(0, 0, 1)));
                Entry = Biome ? PickSpawn(bMonster ? Biome->Monsters : Biome->Creatures, Random) : nullptr;
                if (!Entry) break;
                Group = RollPackSize(*Entry, Random);
            }
            // A mob not built yet (SM 20c2): the round spawns nothing.
            if (Entry->bBlank) break;
            FIntVector Feet = Top + FIntVector(0, 0, 1);
            if (bMonster ? !FindMonsterCell(Entry->Type, Feet, Random) : !FindPackCell(Entry->Type, X, Y, Feet))
                continue;
            if (!SpawnMob(Entry->Type, FVector(Feet.X + .5, Feet.Y + .5, Feet.Z) * Block, Yaw)) continue;
            ++(bMonster ? MonstersCycleSpawned : MobsCycleSpawned);
            if (++Spawned >= ClusterMax) return Spawned;
            if (++InGroup >= Group) break;
        }
    }
    return Spawned;
}

bool FFoundationAuthority::FindPackCell(EFoundationMobType Type, int32 X, int32 Y, FIntVector& OutFeet)
{
    using namespace FoundationMobs;
    // The top motion-blocking block: the highest with a collision box or a fluid (plants and air are skipped).
    FIntVector Cell(X, Y, World.BoundsMax.Z);
    for (; Cell.Z >= World.BoundsMin.Z; --Cell.Z)
    {
        if (!World.IsKnown(Cell)) return false;
        if (World.Get(Cell).State.IsAir()) continue;
        const FFoundationPathCell Path = GetPathCell(Cell);
        if (Path.bAvoid || Path.Solid > 0) break;
    }
    if (Cell.Z < World.BoundsMin.Z || World.GetBlock(World.Get(Cell).State).Id != TEXT("grass_block")) return false;
    // Above the motion-blocking top the sky light proxy is 15 (scout Q1 (a)).
    if (SkyLightProxy < PassiveMinLight) return false;
    OutFeet = Cell + FIntVector(0, 0, 1);
    return HasMobRoom(Type, OutFeet);
}

bool FFoundationAuthority::HasMobRoom(EFoundationMobType Type, const FIntVector& Feet)
{
    using namespace FoundationMobs;
    // Room for the box: no collision and no fluid in it (shrunk a hair off the floor).
    const double Block = UFoundationProfile::BlockCm;
    const FBox Body = GetBox(Type, World.OriginCm + FVector(Feet.X + .5, Feet.Y + .5, Feet.Z) * Block).ExpandBy(-.1);
    TArray<FBox>& Boxes = PathBoxes;
    Boxes.Reset();
    if (GatherCollision)
        GatherCollision(Body, Boxes);
    else
        World.GatherCollision(Body, Boxes);
    for (const FBox& Box : Boxes)
        if (Box.Intersect(Body)) return false;
    return !IsMobInFluid(Body, false) && !IsMobInFluid(Body, true);
}

bool FFoundationAuthority::FindMonsterCell(EFoundationMobType Type, const FIntVector& Feet, FRandomStream& Random)
{
    using namespace FoundationMobs;
    const FIntVector Below = Feet - FIntVector(0, 0, 1);
    if (!World.IsKnown(Feet) || !World.IsKnown(Below)) return false;
    // The floor: a full block top, not leaves (provisional: vanilla's per-block spawn rule is code).
    const FFoundationPathCell Floor = GetPathCell(Below);
    if (Floor.bAvoid || !FMath::IsNearlyEqual(Floor.Solid, 1.f, 1e-3f)) return false;
    static const FGameplayTag Leaves = FGameplayTag::RequestGameplayTag(TEXT("Block.Leaves"), false);
    if (Leaves.IsValid() && World.GetBlock(World.Get(Below).State).Tags.HasTag(Leaves)) return false;
    if (!HasMobRoom(Type, Feet)) return false;
    // The sky draw first: the block light is the costly part.
    const bool bSky = IsSkyVisible(Feet);
    if (!RollSkyLight(Random, bSky ? FullSkyLight : 0)) return false;
    const int32 BlockLight = GetBlockLightProxy(Feet, true);
    if (BlockLight < 0) return false;
    float SkyLevel = SkyLightProxy;
    bool bBurns = false;
    if (GetSkyFacts) GetSkyFacts(SkyLevel, bBurns);
    return RollMonsterLight(Random, BlockLight, FMath::Max(bSky ? VoxelSky::OpenSkyLight(SkyLevel) : 0, BlockLight));
}

FFoundationAuthority::FMobColumnTops FFoundationAuthority::GetColumnTops(int32 X, int32 Y)
{
    using namespace FoundationMobs;
    // The storage's heightmaps when it keeps them (the voxel world: no cache needed), else a cached scan down.
    if (const int32 Surface = World.GetStorage().GetTopZ(X, Y, false); Surface != MIN_int32)
    {
        FMobColumnTops Tops;
        Tops.NonAir = FMath::Clamp(Surface, World.BoundsMin.Z - 1, World.BoundsMax.Z);
        Tops.Blocking = FMath::Clamp(World.GetStorage().GetTopZ(X, Y, true), World.BoundsMin.Z - 1, World.BoundsMax.Z);
        return Tops;
    }
    FMobColumnTops& Tops = MobColumnTops.FindOrAdd(FIntPoint(X, Y));
    if (Tops.Step >= 0 && MobCycleStep - Tops.Step < LightCacheTicks) return Tops;
    Tops.Step = MobCycleStep;
    Tops.NonAir = Tops.Blocking = World.BoundsMin.Z - 1;
    for (FIntVector Cell(X, Y, World.BoundsMax.Z); Cell.Z >= World.BoundsMin.Z; --Cell.Z)
    {
        if (!World.IsKnown(Cell))
        {
            Tops.NonAir = Tops.Blocking = World.BoundsMin.Z - 1;
            break;
        }
        if (World.Get(Cell).State.IsAir()) continue;
        if (Tops.NonAir < World.BoundsMin.Z) Tops.NonAir = Cell.Z;
        const FFoundationPathCell Path = GetPathCell(Cell);
        if (Path.bAvoid || Path.Solid > 0)
        {
            Tops.Blocking = Cell.Z;
            break;
        }
    }
    return Tops;
}

const FFoundationAuthority::FMobEmitters* FFoundationAuthority::GetSectionEmitters(
    const FIntVector& Section, bool bBudgeted)
{
    using namespace FoundationMobs;
    if (FMobEmitters* Known = MobEmitters.Find(Section);
        Known && Known->Step >= 0 && MobCycleStep - Known->Step < LightCacheTicks)
        return Known;
    if (bBudgeted && EmitterRebuilds >= EmitterRebuildsPerStep) return nullptr;
    FMobEmitters& Entry = MobEmitters.FindOrAdd(Section);
    Entry.Step = MobCycleStep;
    Entry.Emitters.Reset();
    const FIntVector Min = Section * SpawnColumnCells;
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    // A section whose palette holds no emitting state has none (no scan, no budget).
    if (!World.GetStorage().MayHold(
            Min, [&Registry](FFoundationBlockState State) { return Registry.GetLightEmission(State) > 0; }))
        return &Entry;
    ++EmitterRebuilds;
    World.GetStorage().ForEachSolid(Min, Min + FIntVector(SpawnColumnCells - 1),
        [&Entry, &Registry](const FIntVector& Cell, const FFoundationCell& Value)
        {
            if (const int32 Level = Registry.GetLightEmission(Value.State); Level > 0)
                Entry.Emitters.Emplace(Cell, Level);
        });
    return &Entry;
}

int32 FFoundationAuthority::GetBlockLightProxy(const FIntVector& Cell, bool bBudgeted)
{
    using namespace FoundationMobs;
    // Bounded caches: dropped whole when they grow past a few loaded worlds' worth.
    if (MobEmitters.Num() > 32768) MobEmitters.Empty();
    if (MobColumnTops.Num() > 262144) MobColumnTops.Empty();
    constexpr int32 Reach = FullSkyLight - 1;
    const auto SectionOf = [](const FIntVector& At)
    {
        return FIntVector(FoundationGrid::FloorDivide(At.X, SpawnColumnCells),
            FoundationGrid::FloorDivide(At.Y, SpawnColumnCells), FoundationGrid::FloorDivide(At.Z, SpawnColumnCells));
    };
    const FIntVector Low = SectionOf(Cell - FIntVector(Reach)), High = SectionOf(Cell + FIntVector(Reach));
    int32 Best = 0;
    for (int32 Z = Low.Z; Z <= High.Z; ++Z)
        for (int32 Y = Low.Y; Y <= High.Y; ++Y)
            for (int32 X = Low.X; X <= High.X; ++X)
            {
                const FMobEmitters* Section = GetSectionEmitters(FIntVector(X, Y, Z), bBudgeted);
                if (!Section) return -1;
                for (const TPair<FIntVector, int32>& Emitter : Section->Emitters)
                {
                    const FIntVector D = Emitter.Key - Cell;
                    Best = FMath::Max(Best, Emitter.Value - (FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z)));
                }
            }
    return Best;
}

int32 FFoundationAuthority::GetLightProxy(const FIntVector& Cell, int32* OutSky, int32* OutBlock, bool bBudgeted)
{
    using namespace FoundationMobs;
    const bool bSky = IsSkyVisible(Cell);
    float SkyLevel = SkyLightProxy;
    bool bBurns = false;
    if (GetSkyFacts) GetSkyFacts(SkyLevel, bBurns);
    // Budgeted: a section left for a later step counts as no block light.
    const int32 BlockLight = FMath::Max(0, GetBlockLightProxy(Cell, bBudgeted));
    if (OutSky) *OutSky = bSky ? FullSkyLight : 0;
    if (OutBlock) *OutBlock = BlockLight;
    return FMath::Max(bSky ? VoxelSky::OpenSkyLight(SkyLevel) : 0, BlockLight);
}

void FFoundationAuthority::DropMobLoot(const FFoundationMob& Mob)
{
    using namespace FoundationItemEntities;
    const FFoundationMobKind& Kind = FoundationMobs::GetKind(Mob.Type);
    ++MobsKilled;
    const FIntVector Cell(FoundationGrid::FloorToCell(Mob.LocalCm.X), FoundationGrid::FloorToCell(Mob.LocalCm.Y),
        FoundationGrid::FloorToCell(Mob.LocalCm.Z));
    FRandomStream Random(FoundationLoot::MakeSeed(~Rules.LootSeed, Cell, Mob.Id));
    // [loot_table/entities/<id>.json] rolled with killed_by_player for a player kill and is_on_fire while it burns
    // (cooked meat); no enchantments yet. Provisional: the drops leave the corpse's feet with the block drops' spread
    // and speed.
    if (const FFoundationLootTable* Table = Rules.EntityLootTables.Find(Kind.Id))
    {
        FFoundationLootContext Context{Rules.Hand};
        Context.bKilledByPlayer = Mob.bKilledByPlayer;
        Context.bOnFire = Mob.Health.IsOnFire();
        TArray<FFoundationLootDrop> Drops;
        FoundationLoot::Roll(*Table, Rules.LootPredicates, Context, Random, Drops);
        for (const FFoundationLootDrop& Drop : Drops)
        {
            if (!Rules.Items.IsValidIndex(Drop.Item)) continue;
            const int32 MaxStack = FMath::Max(1, Rules.Items[Drop.Item].MaxStack);
            for (int32 Left = Drop.Count; Left > 0; Left -= MaxStack)
            {
                const FVector Velocity(Random.FRandRange(-SpawnSidewaysCm, SpawnSidewaysCm),
                    Random.FRandRange(-SpawnSidewaysCm, SpawnSidewaysCm), SpawnUpCm);
                SpawnItem(Drop.Item, FMath::Min(Left, MaxStack), Mob.LocalCm, Velocity, BlockDropPickupDelay);
            }
        }
    }
    // Provisional (wiki "Experience"): a player kill pays the kind's experience, when orbs are on.
    if (Mob.bKilledByPlayer && Options.bExperienceOrbs && Kind.ExperienceMax > 0)
    {
        const int32 Points = Random.RandRange(Kind.ExperienceMin, Kind.ExperienceMax);
        SpawnExperience(Mob.LocalCm + FVector(0, 0, Kind.HeightCm * .5), Points, int32(Random.GetUnsignedInt()));
    }
}

FFoundationHit FFoundationAuthority::Resolve(const FFoundationPlayerFacts& Facts, bool bFluidSources) const
{
    TArray<FFoundationEntityShape, TInlineAllocator<4>> Shapes;
    for (const FFoundationTrainingTarget& Target : Targets)
        Shapes.Add({Target.Id, Target.Box, Target.Label, true, !Target.Health.IsDead()});
    // Mobs (survival T6 20a): a frozen one is not drawn and cannot be hit; a dying one still stops the aim (Attack
    // refuses it).
    if (Options.bMobs)
        for (const FFoundationMob& Mob : Mobs)
            if (!Mob.bFrozen)
                Shapes.Add({Mob.Id, FoundationMobs::GetBox(Mob.Type, World.OriginCm + Mob.LocalCm),
                    FoundationMobs::GetKind(Mob.Type).Id.ToString(), true, Mob.IsAlive()});
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
    const TArray<FFoundationItemInstance> ArmorBefore = Player->Inventory.Armor;

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
        const bool bCreative = Vitals.Mode == EFoundationPlayerMode::Creative;
        // Container 0 is the player's own menu; the open container's id is its menu (survival step 7) while it is
        // valid (its block, within reach; Step closes it otherwise, Codex review 12 finding 1); any other id is stale.
        const FFoundationOpenMenu& Open = Player->OpenMenu;
        const bool bOpenMenu = Open.IsOpen() && Command.ContainerId == Open.ContainerId;
        const FFoundationContainerRules ClickRules = MakeContainerRules(bOpenMenu ? Open.Block : NAME_None);
        const bool bStillValid = bOpenMenu && IsMenuValid(*Player);
        const bool bFurnace = bOpenMenu && IsFurnaceMenu(Open.Kind);
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
        // A dead player throws nothing (Codex review 11, finding 2), as Drop's CheckHand refuses.
        else if (!Click.Thrown.IsEmpty() && !Player->Facts.bAlive)
            Reject(Result, EFoundationReason::Dead);
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
                    FoundationFurnace::OnInputChanged(
                        *Furnace, GetCooking(), FoundationFurnace::GetCookingKind(Furnace->Block));
                // Taking any of the output pays the XP of everything cooked since (FurnaceResultSlot; the value
                // report's fractional XP), as orbs at the player.
                const FFoundationItemInstance& Output = Furnace->Items[Entity::FurnaceResult];
                const bool bTaken = !ResultBefore.IsEmpty() &&
                    (Output.IsEmpty() || Output.Item != ResultBefore.Item || Output.Count < ResultBefore.Count);
                if (bTaken)
                    PayFurnaceExperience(*Furnace, Player->Facts.FeetCm - World.OriginCm,
                        int32(HashCombine(GetTypeHash(Open.Cell), GetTypeHash(Command.Sequence))));
            }
            else if (FFoundationBlockEntity* Barrel = bOpenMenu && Open.Kind == EFoundationMenuKind::Barrel
                    ? BlockEntities.Find(Open.Cell)
                    : nullptr)
                // A barrel's 27 slots live in its block entity (T1 step 9).
                Barrel->Items = MoveTemp(TrialGrid);
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
    // A spectator makes no equip sound (its clicks and uses move nothing).
    if (Vitals.Mode != EFoundationPlayerMode::Spectator) AddEquipEvents(*Player, ArmorBefore);
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
    // 3. The hand item's place capability; without one the use passes to the next hand. A hidden B1 family block
    // (foundation.Blocks.Families 0) counts as none, as before B1 (a sapling in hand passes to the offhand).
    const FFoundationItemInstance& Stack = Player.Inventory.GetHand(Command.Hand);
    if (Stack.IsEmpty() || !Rules.GetItem(Stack.Item).CanPlace() ||
        FoundationBlockFamilies::IsHidden(World.FindBlock(Rules.GetItem(Stack.Item).PlacesBlock)))
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
    // Pouring: water fills a waterloggable block (the clicked one, else the one beside it; never a double slab,
    // minecraft.wiki "Slab"); otherwise the source goes where a block would be placed (the clicked block when
    // replaceable, else beside it) if that cell is air, a fluid, replaceable or washed away by fluids (a torch). A
    // replaced block pops its drop (minecraft.wiki "Water Bucket"), as item entities only. Lava never waterlogs.
    // Pouring into the same source empties the bucket and leaves the cell as it was.
    const FFoundationBlockType* FluidBlock = Registry.FindBlock(bWater ? TEXT("water") : TEXT("lava"));
    const int32 Emptied = Rules.FindItem(BucketId);
    if (!FluidBlock || Emptied == INDEX_NONE) return true;
    static const FName SlabType(TEXT("type")), Double(TEXT("double"));
    auto TakesWater = [&](FFoundationBlockState State)
    {
        return bWater && Registry.GetValue(State, Waterlogged) == False && Registry.GetValue(State, SlabType) != Double;
    };
    const FGameplayTag WashedAway = FGameplayTag::RequestGameplayTag(TEXT("Block.WashedAwayByFluids"), false);
    auto IsReplaced = [&](FFoundationBlockState State)
    {
        return FoundationBlockPlacement::IsReplaceable(World, State) ||
            (WashedAway.IsValid() && Registry.GetBlock(State).Tags.HasTag(WashedAway));
    };
    FIntVector Cell = Hit.Cell;
    FFoundationBlockState Before = World.Get(Cell).State;
    if (!TakesWater(Before))
    {
        if (!IsReplaced(Before)) Cell += FoundationGrid::FaceOffset(Hit.Face);
        if (!World.IsKnown(Cell)) return true;
        Before = World.Get(Cell).State;
    }
    FFoundationBlockState After;
    if (TakesWater(Before))
        After = Registry.WithValue(Before, Waterlogged, True);
    else if (Before.IsAir() || Registry.GetBlock(Before).IsFluid() || IsReplaced(Before))
        After = FluidBlock->GetDefaultState();
    else
        return true;
    TArray<FFoundationLootDrop> Drops;
    if (Options.bItemEntities && !Before.IsAir() && !Registry.GetBlock(Before).IsFluid() && !TakesWater(Before))
        RollBlockDrops(Cell, Before, Rules.Hand, FoundationLoot::MakeSeed(Rules.LootSeed, Cell, BucketUses), Drops);
    if (After != Before) World.Set(Cell, After);
    if (!Drops.IsEmpty()) SpawnDrops(Cell, Drops, FoundationLoot::MakeSeed(~Rules.LootSeed, Cell, BucketUses));
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
    if (UseEquip(Player, Command, Vitals.Mode, Result)) return;
    const FFoundationItemInstance& Stack = Player.Inventory.GetHand(Command.Hand);
    // 26.3's blocks_attacks (the shield, survival step 19b): the use raises it until released; it blocks from its
    // delay on (IsBlocking). Held up as long as the use lasts: 72000 ticks, the bow's and the shield's use duration
    // (minecraft.wiki "Shield"; provisional). An axe-disabled shield passes to the next hand.
    if (Options.bItemUse && !Stack.IsEmpty() && Rules.GetItem(Stack.Item).BlocksAttacks.bValid)
    {
        if (Player.BlockingDisabledItem == Stack.Item && Player.Facts.Step < Player.BlockingDisabledUntilStep)
        {
            Result.Outcome = EFoundationOutcome::Pass;
            return;
        }
        CancelUse(Player);
        Player.Use.bActive = Player.Use.bBlocks = true;
        Player.Use.Hand = Command.Hand;
        Player.Use.InstanceId = Stack.InstanceId;
        Player.Use.Item = Stack.Item;
        Player.Use.Ticks = 0;
        Player.Use.Duration = FoundationItemUse::BlockingUseTicks;
        Player.Use.LastStep = Player.Facts.Step;
        Result.Outcome = EFoundationOutcome::Committed;
        Result.UseDuration = Player.Use.Duration;
        return;
    }
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
    // A raised shield only counts its ticks (IsBlocking reads them).
    if (Use.bBlocks) return;
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

bool FFoundationAuthority::IsBlocking(int32 PlayerId) const
{
    const FFoundationPlayer* Player = FindPlayer(PlayerId);
    if (!Player || !Player->Use.bActive || !Player->Use.bBlocks || !Rules.Items.IsValidIndex(Player->Use.Item))
        return false;
    const FFoundationBlocksAttacks& Blocks = Rules.Items[Player->Use.Item].BlocksAttacks;
    const bool bDisabled =
        Player->BlockingDisabledItem == Player->Use.Item && Player->Facts.Step < Player->BlockingDisabledUntilStep;
    return Blocks.bValid && Player->Use.Ticks >= Blocks.DelayTicks && !bDisabled;
}

bool FFoundationAuthority::HurtPlayer(int32 PlayerId, FFoundationVitals& Vitals, const FFoundationAttack& Attack)
{
    FFoundationPlayer* Player = FindPlayer(PlayerId);
    // Only hits with a source can be blocked: a mob's or a player's attack. Every damage type in 26.3's
    // #minecraft:bypasses_shield (data/minecraft/tags/damage_type/bypasses_shield.json: #bypasses_armor, cactus,
    // campfire, in_fire, lava, ...) reaches the player some other way, without a source.
    const bool bBlockable =
        Attack.Type == EFoundationDamage::MobAttack || Attack.Type == EFoundationDamage::PlayerAttack;
    if (!Player || !bBlockable || !IsBlocking(PlayerId) || Vitals.IsDead() || !(Attack.Amount > 0.f))
        return Vitals.Hurt(Attack.Type, Attack.Amount);
    // The default damage reduction's horizontal_blocking_angle (90): the angle between the look and the direction to
    // the source, both horizontal (minecraft.wiki "Data component format/blocks_attacks"; the edge counts,
    // provisional).
    const FFoundationItemDefinition& Shield = Rules.Items[Player->Use.Item];
    const FFoundationBlocksAttacks& Blocks = Shield.BlocksAttacks;
    const FVector2D Look = FVector2D(Player->Facts.Aim).GetSafeNormal();
    const FVector2D ToSource = FVector2D(Attack.SourceCm - Player->Facts.FeetCm).GetSafeNormal();
    const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Look | ToSource, -1.0, 1.0)));
    // Looking straight up or down, or a source at the feet, leaves no horizontal direction: not blocked (provisional).
    if (Look.IsZero() || ToSource.IsZero() || Angle > Blocks.AngleDegrees + 1e-4)
        return Vitals.Hurt(Attack.Type, Attack.Amount);
    const float Blocked = FMath::Clamp(Blocks.Base + Blocks.Factor * Attack.Amount, 0.f, Attack.Amount);
    ++AttacksBlocked;
    FFoundationEvent Event;
    Event.Player = Player->Id;
    Event.Item = Player->Use.Item;
    Event.LocationCm = Player->Facts.FeetCm;
    Event.Step = Player->Facts.Step;
    Event.Kind = EFoundationEventKind::Blocked;
    Event.Interaction = Blocks.BlockSound;
    Event.Amount = Blocked;
    AddEvent(Event);
    // item_damage: from the threshold on, base + factor x damage, floored (the shield's 1 + floor(damage) from 3;
    // minecraft.wiki "Shield", provisional), under lab tool wear and not in creative.
    if (Attack.Amount >= Blocks.ItemDamageThreshold && Options.bToolWear &&
        Vitals.Mode != EFoundationPlayerMode::Creative && Shield.MaxDurability > 0)
    {
        const int32 Wear = FMath::FloorToInt32(Blocks.ItemDamageBase + Blocks.ItemDamageFactor * Attack.Amount);
        if (Wear > 0 && Player->Inventory.Wear(Player->Use.Hand, Wear, Shield.MaxDurability))
        {
            FFoundationEvent Broke = Event;
            Broke.Kind = EFoundationEventKind::ToolBroke;
            Broke.Interaction = NAME_None;
            AddEvent(Broke);
            CancelUse(*Player);
        }
    }
    // The attacker's weapon disables it: round(seconds x disable_cooldown_scale x 20) ticks (the value report's
    // "Axe disable": the axes' 5 s, 100 ticks); the use ends and a new one passes until the cooldown is over.
    if (Player->Use.bActive && Attack.DisableBlockingSeconds > 0.f && Blocks.DisableCooldownScale > 0.f)
    {
        const int32 Ticks = FMath::RoundToInt32(Attack.DisableBlockingSeconds * Blocks.DisableCooldownScale * 20.f);
        Player->BlockingDisabledItem = Player->Use.Item;
        Player->BlockingDisabledUntilStep = Player->Facts.Step + Ticks;
        FFoundationEvent Disabled = Event;
        Disabled.Kind = EFoundationEventKind::BlockingDisabled;
        Disabled.Interaction = Blocks.DisabledSound;
        Disabled.Amount = float(Ticks);
        AddEvent(Disabled);
        CancelUse(*Player);
    }
    const float Rest = Attack.Amount - Blocked;
    return Rest > 0.f && Vitals.Hurt(Attack.Type, Rest);
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

void FFoundationAuthority::RollBlockDrops(const FIntVector& Cell, FFoundationBlockState State,
    const FFoundationItemDefinition& Tool, int32 RandomSeed, TArray<FFoundationLootDrop>& Out) const
{
    const FFoundationBlockType& Definition = World.GetBlock(State);
    const FFoundationLootTable* Table = Rules.LootTables.Find(Definition.Id);
    // foundation.Loot.AllBlocks 0: only items S5b's ore tables roll.
    if (Table && Table->Index >= FoundationLoot::LegacyTables && CVarLootAllBlocks.GetValueOnAnyThread() == 0)
        Table = nullptr;
    if (Table)
    {
        FRandomStream Random(RandomSeed);
        FFoundationLootContext Context{Tool, Definition.Id, World.GetRegistry().GetProperties(State)};
        // Before the block goes: a double plant's other half is still in place (location_check).
        Context.GetBlockAt = [this, &Cell](const FIntVector& Offset, FName& Block, TArray<TPair<FName, FName>>& Props)
        {
            if (!World.IsKnown(Cell + Offset)) return false;
            const FFoundationBlockState At = World.Get(Cell + Offset).State;
            Block = World.GetBlock(At).Id;
            Props = World.GetRegistry().GetProperties(At);
            return true;
        };
        FoundationLoot::Roll(*Table, Rules.LootPredicates, Context, Random, Out);
    }
    else if (const int32 Drop = Rules.FindItem(Definition.DropItem); Drop != INDEX_NONE)
    {
        // Provisional loot rule for blocks without a table, "drops itself": a double slab is two slabs.
        const int32 DropCount =
            Definition.DropCount * (World.GetRegistry().GetValue(State, TEXT("type")) == TEXT("double") ? 2 : 1);
        if (DropCount > 0) Out.Add({Drop, DropCount});
    }
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
    // A refused break rolls the same drops again: the break count moves only when a block breaks.
    if (!Player.Inventory.bInfiniteSupply && bEligible)
        RollBlockDrops(Cell, State, Tool, FoundationLoot::MakeSeed(Rules.LootSeed, Cell, Breaks), Drops);
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
    // A door's lower half needs a sturdy top below it (survival B3a); an unknown cell below refuses.
    if (const EFoundationFamilySurvival Family = FoundationBlockFamilies::GetSurvival(World, Destination, Placed);
        Family == EFoundationFamilySurvival::Fails || Family == EFoundationFamilySurvival::Unknown)
        return EFoundationReason::CannotSurvive;
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
    // Door halves (survival B3a): an unknown support keeps them.
    if (const EFoundationFamilySurvival Family = FoundationBlockFamilies::GetSurvival(World, Cell, State);
        Family != EFoundationFamilySurvival::NoRule)
        return Family != EFoundationFamilySurvival::Fails;
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
    const FIntVector& Cell, FFoundationBlockState Before, const FFoundationPlayer& Player, bool bWriteUpperHalf)
{
    // Stairs, fences, walls, panes and gates beside the edit (survival B2b, foundation.Blocks.Shapes).
    FoundationBlockFamilies::UpdateNeighbourShapes(World, Cell);
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
        // A half that still pairs with it (a door opened by hand, a double plant replaced by its own kind) keeps it.
        if (FoundationPlantRules::GetOtherHalf(World, At, Old, Other, Ignored) && World.IsKnown(Other) &&
            FoundationPlantRules::IsOtherHalf(World.GetRegistry(), Old, World.Get(Other).State) &&
            !FoundationPlantRules::IsOtherHalf(World.GetRegistry(), World.Get(At).State, World.Get(Other).State))
            Pop(Other);
    };
    PopOtherHalf(Cell, Before);
    // A placed lower half writes its upper half (CheckPlantPlacement found the cell free).
    FIntVector Upper;
    FFoundationBlockState UpperState;
    if (bWriteUpperHalf && FoundationPlantRules::GetOtherHalf(World, Cell, World.Get(Cell).State, Upper, UpperState) &&
        Upper.Z > Cell.Z && World.IsKnown(Upper))
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
    // The family shapes beside every other changed cell too: a written upper half, a pop (survival B3a).
    for (int32 Index = 1; Index < Changed.Num(); ++Index)
        FoundationBlockFamilies::UpdateNeighbourShapes(World, Changed[Index]);
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
    // Mobs (survival T6 20a) take the same hit through their own health; sweeps stay with the training targets until
    // 20c.
    FFoundationMob* Mob = Target || !Options.bMobs ? nullptr : FindMob(Hit.EntityId);
    FFoundationVitals* Health = Target ? &Target->Health : Mob ? &Mob->Health : nullptr;
    if (!Health || Health->IsDead())
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
    const bool bSweep = Target && bStrong && !bCritical && !bSprintKnockback && Facts.bGrounded &&
        Facts.StepMovedCm <= Rules.SweepMaxMoveCm && Weapon.Tags.HasAny(Rules.SweepItemTags);
    if (bCritical) Damage *= Rules.CriticalMultiplier;
    const float Before = Health->Health;
    // A hit landing inside the target's hurt window knocks nothing back (wiki "Knockback").
    const bool bFreshHit = !Health->IsInHurtWindow();
    const bool bAccepted = Health->Hurt(EFoundationDamage::PlayerAttack, Damage);
    // Every issued attack restarts recovery, hit or not.
    Vitals.ResetAttackStrength();
    Result.Outcome = EFoundationOutcome::Committed;
    Result.bChargeReset = true;
    Result.EntityId = Hit.EntityId;
    Result.Strength = Strength;
    Result.Damage = Damage;
    Result.bCritical = bCritical;
    Result.TargetHealth = Health->Health;
    if (!bAccepted)
    {
        Result.Reason = EFoundationReason::TargetCooldown;
        return;
    }
    Result.DamageApplied = Before - Health->Health;
    Result.bTargetDied = Health->IsDead();
    Result.bKnockback = bFreshHit;
    Result.bSprintKnockback = bSprintKnockback;
    if (Target)
    {
        ++Target->Hits;
        Target->DamageTaken += Result.DamageApplied;
    }
    if (Mob)
    {
        // [movement.knockback_resistance, extra.motion] the knockback pushes it away from the player's feet; the
        // sprint-knockback bonus waits for 20c. [survival.r5.play_calls] the hurt or the death sound.
        FoundationMobs::OnHurt(*Mob, World.OriginCm, Facts.FeetCm, bFreshHit, true, Player.Id);
        AddMobSound(*Mob, Result.bTargetDied ? TEXT("death") : TEXT("hurt"), FoundationMobs::GetKind(Mob->Type).Volume);
        if (Result.bTargetDied) DropMobLoot(*Mob);
    }
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
    Event.EntityId = Hit.EntityId;
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
