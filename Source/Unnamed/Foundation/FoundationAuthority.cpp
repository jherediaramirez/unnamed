#include "Foundation/FoundationAuthority.h"
#include "Algo/AllOf.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Foundation/FoundationPlantRules.h"
#include "Foundation/FoundationSimulation.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarLootAllBlocks(TEXT("foundation.Loot.AllBlocks"), 1,
        TEXT("1: a survival break rolls the block's 26.3 loot table, for every block that has one (loot step 2a). 0: "
             "only the 18 ores roll (items S5b) and every other block drops itself."));

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
        TEXT("cannot break blocks in creative"), TEXT("damage out of range")};
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
        TEXT("creative-slot")};
    return Names[int32(Type)];
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
        Player.LastPlacementStep = MIN_int64;
        Player.Results.Empty();
        Player.ResultOrder.Empty();
        Player.HighestEvicted = 0;
    }
    for (FFoundationTrainingTarget& Target : Targets)
    {
        Target.Health = MakeTargetHealth(Rules.TargetMaxHealth);
        Target.Hits = 0;
        Target.DamageTaken = 0;
    }
    FFoundationEvent Event;
    Event.Kind = EFoundationEventKind::Reset;
    AddEvent(Event);
}

void FFoundationAuthority::Step()
{
    for (FFoundationTrainingTarget& Target : Targets)
        Target.Health.StepDamageCooldown();
}

FFoundationHit FFoundationAuthority::Resolve(const FFoundationPlayerFacts& Facts) const
{
    TArray<FFoundationEntityShape, TInlineAllocator<4>> Shapes;
    for (const FFoundationTrainingTarget& Target : Targets)
        Shapes.Add({Target.Id, Target.Box, Target.Label, true, !Target.Health.IsDead()});
    FFoundationRay Ray;
    Ray.Eye = Facts.EyeCm;
    Ray.Direction = Facts.Aim;
    Ray.BlockReachCm = Rules.BlockReachCm;
    Ray.EntityReachCm = Rules.EntityReachCm;
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

    switch (Command.Type)
    {
    case EFoundationCommandType::SelectSlot:
        if (Command.Slot < 0 || Command.Slot >= FFoundationInventory::HotbarSlots)
            Reject(Result, EFoundationReason::InvalidSlot);
        else
        {
            Player->Inventory.Select(Command.Slot);
            Result.Outcome = EFoundationOutcome::Committed;
        }
        break;
    case EFoundationCommandType::SwapHands:
        Player->Inventory.SwapHands();
        Result.Outcome = EFoundationOutcome::Committed;
        break;
    case EFoundationCommandType::Drop:
        Reject(Result, EFoundationReason::DropDeferred);
        break;
    case EFoundationCommandType::UseOn:
        UseOn(*Player, Command, Result);
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
        else if (Command.Slot < 0 || Command.Slot > FFoundationInventory::HotbarSlots)
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
            if (Command.Slot == FFoundationInventory::HotbarSlots)
            {
                Inventory.Offhand = Stack;
                ++Inventory.Revision;
            }
            else
                Inventory.SetSlot(Command.Slot, Stack);
            Result.Outcome = EFoundationOutcome::Committed;
        }
        break;
    }
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
    FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result)
{
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
    // 2. The hand item's place capability; without one the use passes to the next hand.
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
    // The finite fixture returns the drops straight to the inventory (a lab stand-in for item entities).
    // Without room for all of them the break is refused and the block stays.
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
    FFoundationInventory Trial = Player.Inventory;
    if (!Algo::AllOf(Drops, [&](const FFoundationLootDrop& Drop)
            { return Trial.Add(Drop.Item, Drop.Count, Rules.Items[Drop.Item].MaxStack) == Drop.Count; }))
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
    // A block holding water leaves its source (W2b).
    World.Set(Cell, World.GetRegistry().GetRemainder(State));
    ++Breaks;
    Result.bBroken = true;
    Result.Cell = Cell;
    Result.State = State;
    for (const FFoundationLootDrop& Drop : Drops)
        Result.bItemReturned |= Player.Inventory.Add(Drop.Item, Drop.Count, Rules.Items[Drop.Item].MaxStack) > 0;
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
    SettlePlants(Cell, Before, Nobody);
}
