#pragma once

#include "CoreMinimal.h"
#include "Blocks/VoxelBlockTable.h"

// Pending fluid acts by game tick (26.3's scheduled fluid ticks): a min-heap on (due tick, order scheduled), so acts
// due in one tick run in the order they were scheduled (FIFO; scout M 4.3, "same-tick order"). A cell holds at most one
// pending act per fluid; a second request while one is pending is dropped (scout M 6, "Scheduler").
//
// Streaming (water and sky W2a): an act can be parked on a column with the delay it had left, and re-queued from there
// when the game says the column is back. A parked act still counts as pending for its cell.
class UNNAMEDVOXEL_API FVoxelFluidScheduler
{
public:
    struct FAct
    {
        uint64 Due = 0;
        uint64 Order = 0;
        FIntVector Cell = FIntVector::ZeroValue;
        EVoxelFluid Fluid = EVoxelFluid::None;
    };

    // False when Cell already has an act pending for Fluid (that one stays, at its own tick).
    bool Schedule(FIntVector Cell, EVoxelFluid Fluid, uint64 Due);
    // Takes the next act due at or before Tick; false when none is.
    bool PopDue(uint64 Tick, FAct& OutAct);
    // The earliest due tick in the queue; false when it is empty.
    bool PeekDue(uint64& OutDue) const;
    bool IsPending(FIntVector Cell, EVoxelFluid Fluid) const;
    // Acts in the queue (parked ones aside).
    int32 Num() const { return Heap.Num(); }
    void Reset();

    // Parks an act taken from the queue on Column with the delay it had left at Tick (none when it was due). An asleep
    // act waits for Column to be back in the simulation ring (its own column left it); the others wait for Column to be
    // known (they read a cell there before it was).
    void Park(FIntPoint Column, const FAct& Act, uint64 Tick, bool bAsleep);
    // Parks asleep, on its own column, every queued act whose column IsAsleep says left the ring. Returns how many.
    int32 ParkAsleep(uint64 Tick, TFunctionRef<bool(FIntPoint Column)> IsAsleep);
    // Re-queues the acts parked on Column at Tick plus the delay each had left, in the order they were first scheduled:
    // all of them when the column is in the ring, else only those waiting for it to be known. Returns how many.
    int32 Unpark(FIntPoint Column, uint64 Tick, bool bInRing);
    // Unpark for every parked column at once, so acts from different columns keep their first order too. Release says
    // what each column lets go: 2 all (back in the ring), 1 those waiting for it to be known, 0 none. No act is due
    // before MinDue.
    int32 Unpark(uint64 Tick, TFunctionRef<int32(FIntPoint Column)> Release, uint64 MinDue = 0);
    void GetParkedColumns(TArray<FIntPoint>& Out) const;
    int32 GetParkedCount() const { return ParkedCount; }

    // A cell's column: world X and Y divided by 16, rounded down.
    static FIntPoint GetColumn(FIntVector Cell) { return FIntPoint(Cell.X >> 4, Cell.Y >> 4); }

private:
    struct FParked
    {
        uint64 Remaining = 0;
        uint64 Order = 0;
        FIntVector Cell = FIntVector::ZeroValue;
        EVoxelFluid Fluid = EVoxelFluid::None;
        bool bAsleep = false;
    };

    TArray<FAct> Heap;
    // Cells with an act pending, queued or parked, per fluid (water, lava).
    TSet<FIntVector> Pending[2];
    TMap<FIntPoint, TArray<FParked>> Parked;
    int32 ParkedCount = 0;
    uint64 NextOrder = 0;
};
