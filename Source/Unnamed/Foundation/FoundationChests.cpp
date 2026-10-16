#include "Foundation/FoundationChests.h"

#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationPlantRules.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarChest(TEXT("foundation.Chest"), 1,
        TEXT("1: a chest or trapped chest opens its 27-slot menu (54 over a double chest), plays its open and close "
             "sounds, and a new chest pairs with a single one beside it (T1 step 18-chest). 0: as before: no menu (a "
             "right-click places against it) and new chests stay single."));

    const FName TypeName(TEXT("type")), FacingName(TEXT("facing"));
    const FName TypeValues[] = {TEXT("single"), TEXT("left"), TEXT("right")};

    // Seen from above: north, east, south, west.
    EFoundationFace ChestClockWise(EFoundationFace Face)
    {
        switch (Face)
        {
        case EFoundationFace::North:
            return EFoundationFace::East;
        case EFoundationFace::East:
            return EFoundationFace::South;
        case EFoundationFace::South:
            return EFoundationFace::West;
        default:
            return EFoundationFace::North;
        }
    }

    bool IsChestSide(EFoundationFace Face)
    {
        return Face == EFoundationFace::North || Face == EFoundationFace::South || Face == EFoundationFace::West ||
            Face == EFoundationFace::East;
    }

    EFoundationFace GetChestFacing(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        const FName Value = Registry.GetValue(State, FacingName);
        for (const EFoundationFace Face :
            {EFoundationFace::North, EFoundationFace::South, EFoundationFace::West, EFoundationFace::East})
            if (Value == FoundationGrid::FaceName(Face)) return Face;
        return EFoundationFace::None;
    }

    FFoundationBlockState WithType(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, FoundationChests::EType Type)
    {
        return Registry.WithValue(State, TypeName, TypeValues[int32(Type)]);
    }

    // A single chest of Block's id facing Facing at Cell (unknown cells are none).
    bool IsSingleMatch(const FFoundationBlockWorld& World, const FIntVector& Cell, FName Block, EFoundationFace Facing)
    {
        if (!World.IsKnown(Cell)) return false;
        const FFoundationBlockState State = World.Get(Cell).State;
        return World.GetBlock(State).Id == Block &&
            FoundationChests::GetType(World.GetRegistry(), State) == FoundationChests::EType::Single &&
            GetChestFacing(World.GetRegistry(), State) == Facing;
    }
}

bool FoundationChests::IsOn()
{
    return CVarChest.GetValueOnAnyThread() != 0;
}

bool FoundationChests::IsChest(FName Block)
{
    static const FName Chest(TEXT("chest")), Trapped(TEXT("trapped_chest"));
    return Block == Chest || Block == Trapped;
}

FoundationChests::EType FoundationChests::GetType(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
{
    const FName Value = Registry.GetValue(State, TypeName);
    return Value == TypeValues[1] ? EType::Left : Value == TypeValues[2] ? EType::Right : EType::Single;
}

EFoundationFace FoundationChests::GetPartnerSide(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
{
    if (!IsChest(Registry.GetBlock(State).Id)) return EFoundationFace::None;
    const EType Type = GetType(Registry, State);
    const EFoundationFace Facing = GetChestFacing(Registry, State);
    if (Type == EType::Single || Facing == EFoundationFace::None) return EFoundationFace::None;
    const EFoundationFace ClockWise = ChestClockWise(Facing);
    return Type == EType::Left ? ClockWise : FoundationBlockPlacement::GetOpposite(ClockWise);
}

bool FoundationChests::GetPartner(const FFoundationBlockWorld& World, const FIntVector& Cell, FIntVector& OutPartner)
{
    if (!World.IsKnown(Cell)) return false;
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    const FFoundationBlockState State = World.Get(Cell).State;
    const EFoundationFace Side = GetPartnerSide(Registry, State);
    if (Side == EFoundationFace::None) return false;
    const FIntVector Partner = Cell + FoundationGrid::FaceOffset(Side);
    if (!World.IsKnown(Partner)) return false;
    const FFoundationBlockState Other = World.Get(Partner).State;
    if (World.GetBlock(Other).Id != World.GetBlock(State).Id ||
        GetChestFacing(Registry, Other) != GetChestFacing(Registry, State) ||
        GetPartnerSide(Registry, Other) != FoundationBlockPlacement::GetOpposite(Side))
        return false;
    OutPartner = Partner;
    return true;
}

bool FoundationChests::GetHalves(
    const FFoundationBlockWorld& World, const FIntVector& Cell, FIntVector& OutRight, FIntVector& OutLeft)
{
    FIntVector Partner;
    if (!GetPartner(World, Cell, Partner)) return false;
    const bool bRight = GetType(World.GetRegistry(), World.Get(Cell).State) == EType::Right;
    OutRight = bRight ? Cell : Partner;
    OutLeft = bRight ? Partner : Cell;
    return true;
}

FFoundationBlockState FoundationChests::GetStateForPlacement(
    const FFoundationBlockWorld& World, const FFoundationBlockType& Block, const FFoundationPlacementContext& Context)
{
    using FoundationBlockPlacement::GetOpposite;
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    FFoundationBlockState State =
        WithType(Registry, FoundationPlantRules::GetStateForPlacement(World, Block, Context), EType::Single);
    const EFoundationFace Facing = GetChestFacing(Registry, State);
    if (Facing == EFoundationFace::None) return State;
    if (Context.bSecondaryUse)
    {
        // Sneak-clicking a chest's side tries to join that chest: the new half takes its facing when the clicked face
        // is across it (blocks.chest_neighbor_pairing; minecraft.wiki "Chest"). The chest is the destination's
        // neighbour back across the clicked face (the clicked cell, unless a replaceable block there is replaced).
        if (!IsChestSide(Context.ClickedFace)) return State;
        const FIntVector Beside = Context.Destination + FoundationGrid::FaceOffset(GetOpposite(Context.ClickedFace));
        if (!World.IsKnown(Beside)) return State;
        const EFoundationFace ClickedFacing = GetChestFacing(Registry, World.Get(Beside).State);
        if (ClickedFacing == EFoundationFace::None || int32(ClickedFacing) / 2 == int32(Context.ClickedFace) / 2 ||
            !IsSingleMatch(World, Beside, Block.Id, ClickedFacing))
            return State;
        State = Registry.WithValue(State, FacingName, FoundationGrid::FaceName(ClickedFacing));
        // From the new half, the clicked chest is back across the clicked face.
        return WithType(Registry, State,
            GetOpposite(Context.ClickedFace) == ChestClockWise(ClickedFacing) ? EType::Left : EType::Right);
    }
    const EFoundationFace ClockWise = ChestClockWise(Facing);
    if (IsSingleMatch(World, Context.Destination + FoundationGrid::FaceOffset(ClockWise), Block.Id, Facing))
        return WithType(Registry, State, EType::Left);
    if (IsSingleMatch(
            World, Context.Destination + FoundationGrid::FaceOffset(GetOpposite(ClockWise)), Block.Id, Facing))
        return WithType(Registry, State, EType::Right);
    return State;
}

FFoundationBlockState FoundationChests::UpdateShape(const FFoundationBlockRegistry& Registry,
    FFoundationBlockState State, EFoundationFace Toward, FFoundationBlockState Neighbour)
{
    if (!IsChestSide(Toward)) return State;
    const FName Block = Registry.GetBlock(State).Id;
    const EFoundationFace Facing = GetChestFacing(Registry, State);
    const bool bPointsHere = Registry.GetBlock(Neighbour).Id == Block &&
        GetChestFacing(Registry, Neighbour) == Facing &&
        GetPartnerSide(Registry, Neighbour) == FoundationBlockPlacement::GetOpposite(Toward);
    const EType Type = GetType(Registry, State);
    if (Type == EType::Single)
        return bPointsHere
            ? WithType(Registry, State, GetType(Registry, Neighbour) == EType::Left ? EType::Right : EType::Left)
            : State;
    return GetPartnerSide(Registry, State) == Toward && !bPointsHere ? WithType(Registry, State, EType::Single) : State;
}

bool FoundationChests::IsBlockedAbove(const FFoundationBlockWorld& World, const FIntVector& Cell)
{
    const FIntVector Above = Cell + FoundationGrid::FaceOffset(EFoundationFace::Up);
    if (!World.IsKnown(Above)) return false;
    const FFoundationBlockState State = World.Get(Above).State;
    static const FName NonConductors[] = {
        TEXT("glowstone"), TEXT("redstone_block"), TEXT("observer"), TEXT("soul_sand"), TEXT("mud")};
    const FFoundationBlockType& Block = World.GetBlock(State);
    for (const FName& Id : NonConductors)
        if (Block.Id == Id) return false;
    return World.GetRegistry().GetShape(State) == EFoundationBlockShape::Full &&
        Block.Layer == EFoundationRenderLayer::Opaque;
}
