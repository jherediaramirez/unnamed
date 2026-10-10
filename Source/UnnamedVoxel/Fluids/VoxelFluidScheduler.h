#pragma once

#include "CoreMinimal.h"
#include "Blocks/VoxelBlockTable.h"

// Pending fluid acts by game tick (26.3's scheduled fluid ticks): a min-heap on (due tick, order scheduled), so acts
// due in one tick run in the order they were scheduled (FIFO; scout M 4.3, "same-tick order"). A cell holds at most one
// pending act per fluid; a second request while one is pending is dropped (scout M 6, "Scheduler").
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
    bool IsPending(FIntVector Cell, EVoxelFluid Fluid) const;
    int32 Num() const { return Heap.Num(); }
    void Reset();

private:
    TArray<FAct> Heap;
    // Cells with an act pending, per fluid (water, lava).
    TSet<FIntVector> Pending[2];
    uint64 NextOrder = 0;
};
