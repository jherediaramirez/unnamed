#pragma once

#include "CoreMinimal.h"
#include "Blocks/VoxelBlockTable.h"
#include "Fluids/VoxelFluidScheduler.h"

// Flowing water and lava (26.3), as scout M measured them on the server jar (Docs/Records/
// 2026-10-10-WaterSky-Scout-M-Fluids.md, sections 4 and 5; the tests replay its oracles). The rules are written in our
// own words from those measurements: Q8 of the water and sky plan, where the probe and the wiki disagree, the probe
// wins.

// The blocks the rules name: what makes basalt, and what lava meeting water makes. Every other block is described by
// its properties alone (FVoxelFluidCell).
enum class EVoxelFluidBlock : uint8
{
    None,
    SoulSoil,
    BlueIce,
    Obsidian,
    Cobblestone,
    Stone,
    Basalt
};

// A cell as the rules see it. The tests' world fills it directly; the game (W2a) will fill it from the block table.
struct FVoxelFluidCell
{
    EVoxelFluid Fluid = EVoxelFluid::None;
    // 0 a source, 1-7 flowing (amount 8 - level), 8-15 falling (amount 8).
    uint8 Level = 0;
    // One bit per EVoxelFace: the faces the block's collision covers whole (a bottom slab: Down; a full block: all).
    // Fluid never crosses a face that either cell covers.
    uint8 Walls = 0;
    // Blocks motion: a floor for a new source, and never entered by fluid.
    bool bSolid = false;
    // Waterloggable (slabs): holds only a water source; flowing fluid never enters it (scout M 4.4).
    bool bWaterloggable = false;
    // In washed_away_by_fluids (plants, torches): the rules enter it as an open cell; the flag tells the game that the
    // block the fluid replaces pops (scout M 4.4).
    bool bWashedAway = false;
    // Not known yet (an unloaded column, W2a): an act that reads it waits.
    bool bUnknown = false;
    EVoxelFluidBlock Block = EVoxelFluidBlock::None;

    bool operator==(const FVoxelFluidCell& Other) const = default;

    bool IsSource(EVoxelFluid Of) const { return Fluid == Of && Level == 0; }
    // A source and falling fluid hold 8; flowing fluid 8 - level.
    int32 GetAmount() const { return Fluid == EVoxelFluid::None ? 0 : Level == 0 || Level >= 8 ? 8 : 8 - Level; }

    static FVoxelFluidCell MakeFluid(EVoxelFluid InFluid, uint8 InLevel)
    {
        FVoxelFluidCell Cell;
        Cell.Fluid = InFluid;
        Cell.Level = InLevel;
        return Cell;
    }

    // A full block that blocks motion.
    static FVoxelFluidCell MakeSolid(EVoxelFluidBlock InBlock = EVoxelFluidBlock::None)
    {
        FVoxelFluidCell Cell;
        Cell.Walls = 0x3F;
        Cell.bSolid = true;
        Cell.Block = InBlock;
        return Cell;
    }
};

// One change: Cell becomes After if it still holds Before. An act computes all its writes from the world as it was
// before them (with its own cell's new state); the check keeps a write off a cell that an earlier one changed.
struct FVoxelFluidWrite
{
    FIntVector Cell = FIntVector::ZeroValue;
    FVoxelFluidCell Before;
    FVoxelFluidCell After;
};

// Test-only changes that must break the oracles named beside them (scout M section 5, "Mutations that must fail";
// Unnamed.Voxel.Fluid.Mutations runs each one).
enum class EVoxelFluidMutation : uint8
{
    None,
    // Water acts every 4 or 6 ticks: w_plane's timing.
    WaterDelay4,
    WaterDelay6,
    // Water loses 2 per cell: w_plane and w_channel reach 3.
    WaterDropOff2,
    // Water finds drops up to 4 or 6 cells away: w_hole5 becomes a diamond, w_hole6 a line.
    WaterSearch4,
    WaterSearch6,
    // Lava finds drops up to 4 cells away: l_hole4 becomes a line.
    LavaSearch4,
    // Lava loses 1 per cell: l_plane0 reaches 7.
    LavaDropOff1,
    // A new source needs no floor: w_infinite_air's middle becomes one.
    NoFloorCheck,
    // A source above counts with one beside: w_above_beside2's middle becomes a source.
    SourceAboveCounts,
    // A bottom slab is not a floor: w_infinite_slab's middle stays flowing.
    SlabNotFloor,
    // A target takes the spreading cell's amount less the drop-off, not its own re-derived state: w_infinite's middle
    // is level 1 at t=5.
    TargetFromSpreader,
    // Flowing fluid over a drop also spreads sideways: w_cliff grows wider on top.
    FallingSpreads,
    // Lava also meets water below it: lw_lava_over_water's (2, 64) becomes cobblestone.
    ReactBelow,
    // Flowing water waterlogs slabs: w_slab_wall's slabs get wet.
    FlowingWaterlogs,
    // Placed lava doesn't meet water at once: lw_place_beside's lava stays.
    NoPlacedReaction,
    Count
};

struct FVoxelFluidOptions
{
    // 26.3's game rules water_source_conversion (default true) and lava_source_conversion (default false).
    bool bWaterSourceConversion = true;
    bool bLavaSourceConversion = false;
    EVoxelFluidMutation Mutation = EVoxelFluidMutation::None;
};

// One fluid's numbers in the overworld.
struct FVoxelFluidParams
{
    // Game ticks from a change to the act it wakes; a falling fluid drops one cell per delay.
    int32 Delay = 5;
    // The amount lost per cell sideways.
    int32 DropOff = 1;
    // How far a drop is found, in cells from the spreading cell (the cell beside it is 1).
    int32 Search = 5;
    // Two sources beside a cell with a floor below make it a source (the game rule).
    bool bConvert = true;
};

namespace VoxelFluidRules
{
    // Water: every 5 ticks, levels 1-7 (reach 7), drops found up to 5 cells away [P w_plane, w_channel, w_hole1-6].
    // Overworld lava: every 30 ticks, levels 2, 4, 6 (reach 3), drops up to 3 cells away [P l_plane0-3, l_hole2-4; the
    // wiki's 4 is wrong, Q8]. No random delays [P l_plane0-3, l_drain0-3, l_onto_lower0-3: four copies identical].
    inline FVoxelFluidParams GetParams(EVoxelFluid Fluid, const FVoxelFluidOptions& Options)
    {
        const EVoxelFluidMutation Mutation = Options.Mutation;
        FVoxelFluidParams Params;
        if (Fluid == EVoxelFluid::Lava)
        {
            Params.Delay = 30;
            Params.DropOff = Mutation == EVoxelFluidMutation::LavaDropOff1 ? 1 : 2;
            Params.Search = Mutation == EVoxelFluidMutation::LavaSearch4 ? 4 : 3;
            Params.bConvert = Options.bLavaSourceConversion;
            return Params;
        }
        Params.Delay = Mutation == EVoxelFluidMutation::WaterDelay4 ? 4
            : Mutation == EVoxelFluidMutation::WaterDelay6          ? 6
                                                                    : 5;
        Params.DropOff = Mutation == EVoxelFluidMutation::WaterDropOff2 ? 2 : 1;
        Params.Search = Mutation == EVoxelFluidMutation::WaterSearch4 ? 4
            : Mutation == EVoxelFluidMutation::WaterSearch6           ? 6
                                                                      : 5;
        Params.bConvert = Options.bWaterSourceConversion;
        return Params;
    }

    // The neighbour across a face, in EVoxelFace order (West, East, North, South, Down, Up); faces 0-3 are horizontal.
    inline FIntVector GetFaceOffset(int32 Face)
    {
        constexpr int32 X[6] = {-1, 1, 0, 0, 0, 0}, Y[6] = {0, 0, -1, 1, 0, 0}, Z[6] = {0, 0, 0, 0, -1, 1};
        return FIntVector(X[Face], Y[Face], Z[Face]);
    }

    constexpr int32 Down = int32(EVoxelFace::Down);
    constexpr int32 Up = int32(EVoxelFace::Up);

    // The order a change tells its neighbours: 26.3's block update order, west, east, down, up, north, south [W Block
    // update]. It only orders acts due in the same tick.
    constexpr int32 UpdateOrder[6] = {
        int32(EVoxelFace::West), int32(EVoxelFace::East), Down, Up, int32(EVoxelFace::North), int32(EVoxelFace::South)};

    // Fluid can cross from A to its neighbour B through A's Face.
    inline bool IsOpen(const FVoxelFluidCell& A, int32 Face, const FVoxelFluidCell& B)
    {
        return ((A.Walls >> Face) & 1) == 0 && ((B.Walls >> VoxelFaces::GetOpposite(Face)) & 1) == 0;
    }

    // A cell flowing fluid could fill: not solid, not waterloggable (any fluid in it aside).
    inline bool CanHold(const FVoxelFluidCell& Cell)
    {
        return !Cell.bSolid && !Cell.bWaterloggable;
    }

    // A cell the drop search walks into from A: open, able to hold fluid, and not a source of this fluid.
    inline bool IsPassable(const FVoxelFluidCell& A, int32 Face, const FVoxelFluidCell& B, EVoxelFluid Fluid)
    {
        return IsOpen(A, Face, B) && CanHold(B) && !B.IsSource(Fluid);
    }

    // A drop below Top: open downward, and the cell below holds this fluid or could hold fluid.
    inline bool IsDrop(const FVoxelFluidCell& Top, const FVoxelFluidCell& Bottom, EVoxelFluid Fluid)
    {
        return IsOpen(Top, Down, Bottom) && (Bottom.Fluid == Fluid || CanHold(Bottom));
    }

    // A cell spreading fluid writes into: holding no fluid and able to (air, or a washed-away plant). A cell already
    // holding the fluid re-derives its own state when it acts [P w_infinite, w_merge].
    inline bool CanEnter(const FVoxelFluidCell& Cell, EVoxelFluid Fluid, const FVoxelFluidOptions& Options)
    {
        if (Cell.Fluid != EVoxelFluid::None || Cell.bSolid && !Cell.bWaterloggable) return false;
        return !Cell.bWaterloggable ||
            Options.Mutation == EVoxelFluidMutation::FlowingWaterlogs && Fluid == EVoxelFluid::Water;
    }

    // The state Cell takes from its neighbours (scout M 4.3, "Re-deriving"): a source with two same-fluid sources
    // beside it and a floor below (a block that blocks motion, bottom slabs included, or a source of the fluid; not
    // falling fluid) [P w_infinite, w_infinite_slab, w_infinite_air]; falling with the fluid above it [P
    // w_above_beside2: a source above and one beside make falling fluid, not a source; the wiki says otherwise, Q8];
    // else the most fluid beside it less the drop-off; nothing below 1. The fluid in Cell itself is not read.
    template <typename TGet>
    FVoxelFluidCell Derive(const TGet& Get, FIntVector Cell, EVoxelFluid Fluid, const FVoxelFluidParams& Params,
        const FVoxelFluidOptions& Options)
    {
        const EVoxelFluidMutation Mutation = Options.Mutation;
        const FVoxelFluidCell Self = Get(Cell);
        int32 Sources = 0, Most = 0;
        for (int32 Face = 0; Face < 4; ++Face)
        {
            const FVoxelFluidCell Side = Get(Cell + GetFaceOffset(Face));
            if (Side.Fluid != Fluid || !IsOpen(Self, Face, Side)) continue;
            Sources += Side.Level == 0;
            Most = FMath::Max(Most, Side.GetAmount());
        }
        const FVoxelFluidCell Above = Get(Cell + GetFaceOffset(Up));
        const bool bAbove = Above.Fluid == Fluid && IsOpen(Self, Up, Above);
        if (Mutation == EVoxelFluidMutation::SourceAboveCounts && bAbove && Above.Level == 0) ++Sources;
        if (Params.bConvert && Sources >= 2)
        {
            const FVoxelFluidCell Below = Get(Cell + GetFaceOffset(Down));
            const bool bFloor = Mutation == EVoxelFluidMutation::NoFloorCheck ||
                Below.bSolid && !(Mutation == EVoxelFluidMutation::SlabNotFloor && Below.bWaterloggable) ||
                Below.IsSource(Fluid);
            if (bFloor) return FVoxelFluidCell::MakeFluid(Fluid, 0);
        }
        if (bAbove) return FVoxelFluidCell::MakeFluid(Fluid, 8);
        const int32 Amount = Most - Params.DropOff;
        return Amount > 0 ? FVoxelFluidCell::MakeFluid(Fluid, uint8(8 - Amount)) : FVoxelFluidCell();
    }

    // How many cells past Start, the cell beside the spreading one (From), the nearest drop lies: a breadth-first walk
    // through passable cells, at most MaxDepth deep, never back through From. MAX_int32 when none is in reach.
    template <typename TGet>
    int32 FindDrop(const TGet& Get, FIntVector From, FIntVector Start, const FVoxelFluidCell& StartCell,
        EVoxelFluid Fluid, int32 MaxDepth)
    {
        struct FNode
        {
            FIntVector At;
            FVoxelFluidCell Cell;
            int32 Depth;
        };
        // Cells reached so far, by their offset from Start (MaxDepth is at most 5: the search mutations).
        constexpr int32 Reach = 6, Width = 2 * Reach + 1;
        check(MaxDepth < Reach);
        bool Reached[Width * Width] = {};
        const auto IsReached = [&Reached, Start](FIntVector At) -> bool&
        { return Reached[(At.X - Start.X + Reach) * Width + At.Y - Start.Y + Reach]; };
        IsReached(Start) = true;
        IsReached(From) = true;
        TArray<FNode, TInlineAllocator<64>> Queue;
        Queue.Add({Start, StartCell, 0});
        for (int32 Head = 0; Head < Queue.Num(); ++Head)
        {
            const FNode Node = Queue[Head];
            if (Node.Depth >= MaxDepth) continue;
            for (int32 Face = 0; Face < 4; ++Face)
            {
                const FIntVector At = Node.At + GetFaceOffset(Face);
                bool& bReached = IsReached(At);
                if (bReached) continue;
                const FVoxelFluidCell Next = Get(At);
                // Not marked when closed: walls are per face, so another way in may be open.
                if (!IsPassable(Node.Cell, Face, Next, Fluid)) continue;
                bReached = true;
                if (IsDrop(Next, Get(At + GetFaceOffset(Down)), Fluid)) return Node.Depth + 1;
                Queue.Add({At, Next, Node.Depth + 1});
            }
        }
        return MAX_int32;
    }

    // The sides a cell spreads to (scout M 4.3, "Drop search"): of the passable cells beside it, those whose nearest
    // drop is nearest (a drop right below one counts 0); all of them when no drop is in reach [P w_hole1-6,
    // w_hole_two, w_hole_tie, w_hole_diag, l_hole2-4]. One bit per horizontal EVoxelFace.
    template <typename TGet>
    uint8 FindSpreadSides(const TGet& Get, FIntVector Cell, const FVoxelFluidCell& Self, EVoxelFluid Fluid,
        const FVoxelFluidParams& Params, const FVoxelFluidOptions& Options)
    {
        int32 Nearest = MAX_int32;
        uint8 Sides = 0;
        for (int32 Face = 0; Face < 4; ++Face)
        {
            const FIntVector At = Cell + GetFaceOffset(Face);
            const FVoxelFluidCell Side = Get(At);
            // FlowingWaterlogs only: a dry slab beside water is a way to spread.
            const bool bWaterlogs = Options.Mutation == EVoxelFluidMutation::FlowingWaterlogs &&
                Fluid == EVoxelFluid::Water && Side.bWaterloggable && Side.Fluid == EVoxelFluid::None &&
                IsOpen(Self, Face, Side);
            if (!IsPassable(Self, Face, Side, Fluid) && !bWaterlogs) continue;
            const int32 Distance = bWaterlogs ? MAX_int32
                : IsDrop(Side, Get(At + GetFaceOffset(Down)), Fluid)
                ? 0
                : FindDrop(Get, Cell, At, Side, Fluid, Params.Search - 1);
            if (Distance < Nearest)
            {
                Nearest = Distance;
                Sides = 0;
            }
            if (Distance == Nearest) Sides |= uint8(1 << Face);
        }
        return Sides;
    }

    // One act of the fluid in Cell (26.3's scheduled fluid tick; scout M 4.3): a flowing or falling cell first
    // re-derives its state. Then the fluid goes down if the cell below takes it, and does nothing more this act [P
    // w_falling_lands: a source in the air falls at t=5 and spreads at t=10]; lava going down into water turns the
    // water to stone and stays [P lw_stone, lw_lava_over_water]. Otherwise it spreads sideways toward the nearest drop
    // if it has amount to spare, unless it is flowing over a drop [P w_cliff: the edge cells don't spread past the
    // edge]. A target takes its own re-derived state [P w_infinite, w_merge]. Appends the writes in order, the cell's
    // own change first. Returns false, with nothing appended, when a cell it reads is not known (the act waits).
    // TReader: FVoxelFluidCell Get(FIntVector) const.
    template <typename TReader>
    bool Act(const TReader& Reader, FIntVector Cell, EVoxelFluid Fluid, const FVoxelFluidOptions& Options,
        TArray<FVoxelFluidWrite>& OutWrites)
    {
        const FVoxelFluidCell Self = Reader.Get(Cell);
        if (Self.bUnknown) return false;
        if (Self.Fluid != Fluid) return true;
        const EVoxelFluidMutation Mutation = Options.Mutation;
        const FVoxelFluidParams Params = GetParams(Fluid, Options);
        const int32 First = OutWrites.Num();
        FVoxelFluidCell Now = Self;
        bool bUnknown = false;
        // Reads see the cell's own new state.
        const auto Get = [&](FIntVector At)
        {
            if (At == Cell) return Now;
            const FVoxelFluidCell Read = Reader.Get(At);
            bUnknown |= Read.bUnknown;
            return Read;
        };
        const auto Finish = [&]
        {
            if (bUnknown) OutWrites.SetNum(First);
            return !bUnknown;
        };

        if (Now.Level != 0)
        {
            const FVoxelFluidCell Derived = Derive(Get, Cell, Fluid, Params, Options);
            if (Derived != Now)
            {
                OutWrites.Add({Cell, Self, Derived});
                Now = Derived;
            }
            if (Now.Fluid != Fluid) return Finish();
        }

        const auto SpreadSideways = [&]
        {
            if (Now.GetAmount() - Params.DropOff <= 0) return;
            const uint8 Sides = FindSpreadSides(Get, Cell, Now, Fluid, Params, Options);
            for (int32 Face = 0; Face < 4; ++Face)
            {
                if ((Sides >> Face & 1) == 0) continue;
                const FIntVector At = Cell + GetFaceOffset(Face);
                const FVoxelFluidCell Target = Get(At);
                if (!CanEnter(Target, Fluid, Options)) continue;
                FVoxelFluidCell After = Derive(Get, At, Fluid, Params, Options);
                if (Mutation == EVoxelFluidMutation::TargetFromSpreader)
                    After = FVoxelFluidCell::MakeFluid(Fluid, uint8(8 - (Now.GetAmount() - Params.DropOff)));
                if (Target.bWaterloggable)
                {
                    // FlowingWaterlogs only: the slab holds a water source.
                    After = Target;
                    After.Fluid = EVoxelFluid::Water;
                    After.Level = 0;
                }
                OutWrites.Add({At, Target, After});
            }
        };

        const FIntVector BelowCell = Cell + GetFaceOffset(Down);
        const FVoxelFluidCell Below = Get(BelowCell);
        if (IsOpen(Now, Down, Below))
        {
            if (Fluid == EVoxelFluid::Lava && Below.Fluid == EVoxelFluid::Water)
            {
                OutWrites.Add({BelowCell, Below, FVoxelFluidCell::MakeSolid(EVoxelFluidBlock::Stone)});
                return Finish();
            }
            // Slabs are never entered from above, even under FlowingWaterlogs (a sideways mutation).
            if (CanEnter(Below, Fluid, Options) && !Below.bWaterloggable)
            {
                OutWrites.Add({BelowCell, Below, Derive(Get, BelowCell, Fluid, Params, Options)});
                if (Mutation == EVoxelFluidMutation::FallingSpreads && Now.Level != 0) SpreadSideways();
                return Finish();
            }
        }
        if (Now.Level == 0 || !IsDrop(Now, Below, Fluid) || Mutation == EVoxelFluidMutation::FallingSpreads)
            SpreadSideways();
        return Finish();
    }

    // Lava in Cell meeting water (scout M 4.5; 26.3 checks it when lava is placed and when a neighbour changes): with
    // water above or beside it, never below [P lw_lava_over_water], a source becomes obsidian [P lw_obsidian,
    // lw_place_beside] and flowing lava cobblestone [P lw_cobble, lw_water_onto_flowing_lava]; over soul soil with
    // blue ice beside it, basalt [P lw_basalt]. Returns false when nothing happens.
    template <typename TReader>
    bool React(const TReader& Reader, FIntVector Cell, const FVoxelFluidOptions& Options, FVoxelFluidWrite& OutWrite)
    {
        const FVoxelFluidCell Self = Reader.Get(Cell);
        if (Self.Fluid != EVoxelFluid::Lava) return false;
        const bool bSoulSoil = Reader.Get(Cell + GetFaceOffset(Down)).Block == EVoxelFluidBlock::SoulSoil;
        constexpr int32 Faces[6] = {Up, int32(EVoxelFace::West), int32(EVoxelFace::East), int32(EVoxelFace::North),
            int32(EVoxelFace::South), Down};
        const int32 Count = Options.Mutation == EVoxelFluidMutation::ReactBelow ? 6 : 5;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const FVoxelFluidCell Side = Reader.Get(Cell + GetFaceOffset(Faces[Index]));
            EVoxelFluidBlock Made = EVoxelFluidBlock::None;
            if (bSoulSoil && Side.Block == EVoxelFluidBlock::BlueIce)
                Made = EVoxelFluidBlock::Basalt;
            else if (Side.Fluid == EVoxelFluid::Water)
                Made = Self.Level == 0 ? EVoxelFluidBlock::Obsidian : EVoxelFluidBlock::Cobblestone;
            if (Made == EVoxelFluidBlock::None) continue;
            OutWrite = {Cell, Self, FVoxelFluidCell::MakeSolid(Made)};
            return true;
        }
        return false;
    }

    // A cell hears of a change (its own or a neighbour's): lava in it meets water at once (unless bReact is false), and
    // fluid in it acts after its delay.
    template <typename TWorld>
    void Wake(TWorld& World, FVoxelFluidScheduler& Scheduler, uint64 Tick, FIntVector Cell,
        const FVoxelFluidOptions& Options, bool bReact);

    // Every change goes through here, as every block change does in 26.3: the cell is set, placed lava meets water at
    // once, then the neighbours hear of it in UpdateOrder. Returns false, changing nothing, when the cell no longer
    // holds Write.Before. TWorld: FVoxelFluidCell Get(FIntVector) const; void Set(FIntVector, const FVoxelFluidCell&).
    template <typename TWorld>
    bool Apply(TWorld& World, FVoxelFluidScheduler& Scheduler, uint64 Tick, const FVoxelFluidWrite& Write,
        const FVoxelFluidOptions& Options)
    {
        if (World.Get(Write.Cell) != Write.Before) return false;
        if (Write.After == Write.Before) return true;
        World.Set(Write.Cell, Write.After);
        Wake(World, Scheduler, Tick, Write.Cell, Options, Options.Mutation != EVoxelFluidMutation::NoPlacedReaction);
        for (const int32 Face : UpdateOrder)
            Wake(World, Scheduler, Tick, Write.Cell + GetFaceOffset(Face), Options, true);
        return true;
    }

    template <typename TWorld>
    void Wake(TWorld& World, FVoxelFluidScheduler& Scheduler, uint64 Tick, FIntVector Cell,
        const FVoxelFluidOptions& Options, bool bReact)
    {
        const FVoxelFluidCell Here = World.Get(Cell);
        if (Here.Fluid == EVoxelFluid::None) return;
        FVoxelFluidWrite Reaction;
        if (bReact && React(World, Cell, Options, Reaction))
        {
            Apply(World, Scheduler, Tick, Reaction, Options);
            return;
        }
        Scheduler.Schedule(Cell, Here.Fluid, Tick + uint64(GetParams(Here.Fluid, Options).Delay));
    }

    // One game tick: every act due runs in the order it was scheduled, and its writes apply at once, so later acts in
    // the tick see them (scout M 4.3, "Same-tick order"). An act that reads an unknown cell is dropped here (W2a parks
    // it). Returns the number of acts run.
    template <typename TWorld>
    int32 RunTick(TWorld& World, FVoxelFluidScheduler& Scheduler, uint64 Tick, const FVoxelFluidOptions& Options,
        TArray<FVoxelFluidWrite>& Scratch)
    {
        int32 Acts = 0;
        FVoxelFluidScheduler::FAct Due;
        while (Scheduler.PopDue(Tick, Due))
        {
            Scratch.Reset();
            if (!Act(World, Due.Cell, Due.Fluid, Options, Scratch)) continue;
            ++Acts;
            for (const FVoxelFluidWrite& Write : Scratch)
                Apply(World, Scheduler, Tick, Write, Options);
        }
        return Acts;
    }
}
