#include "Fluids/VoxelFluidScheduler.h"

namespace
{
    // The heap's order: the earliest due tick first, then the earliest scheduled.
    struct FFluidActBefore
    {
        bool operator()(const FVoxelFluidScheduler::FAct& A, const FVoxelFluidScheduler::FAct& B) const
        {
            return A.Due != B.Due ? A.Due < B.Due : A.Order < B.Order;
        }
    };
}

bool FVoxelFluidScheduler::Schedule(FIntVector Cell, EVoxelFluid Fluid, uint64 Due)
{
    if (Fluid == EVoxelFluid::None) return false;
    bool bAlready = false;
    Pending[int32(Fluid) - 1].Add(Cell, &bAlready);
    if (bAlready) return false;
    Heap.HeapPush(FAct{Due, NextOrder++, Cell, Fluid}, FFluidActBefore());
    return true;
}

bool FVoxelFluidScheduler::PopDue(uint64 Tick, FAct& OutAct)
{
    if (Heap.IsEmpty() || Heap.HeapTop().Due > Tick) return false;
    Heap.HeapPop(OutAct, FFluidActBefore(), EAllowShrinking::No);
    Pending[int32(OutAct.Fluid) - 1].Remove(OutAct.Cell);
    return true;
}

bool FVoxelFluidScheduler::IsPending(FIntVector Cell, EVoxelFluid Fluid) const
{
    return Fluid != EVoxelFluid::None && Pending[int32(Fluid) - 1].Contains(Cell);
}

void FVoxelFluidScheduler::Reset()
{
    Heap.Reset();
    Pending[0].Reset();
    Pending[1].Reset();
    NextOrder = 0;
}
