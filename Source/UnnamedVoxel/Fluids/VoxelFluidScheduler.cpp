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

bool FVoxelFluidScheduler::PeekDue(uint64& OutDue) const
{
    if (Heap.IsEmpty()) return false;
    OutDue = Heap.HeapTop().Due;
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
    Parked.Reset();
    ParkedCount = 0;
    NextOrder = 0;
}

void FVoxelFluidScheduler::Park(FIntPoint Column, const FAct& Act, uint64 Tick, bool bAsleep)
{
    if (Act.Fluid == EVoxelFluid::None) return;
    // Taken from the queue, it is pending again until it is re-queued.
    Pending[int32(Act.Fluid) - 1].Add(Act.Cell);
    Parked.FindOrAdd(Column).Add(FParked{Act.Due > Tick ? Act.Due - Tick : 0, Act.Order, Act.Cell, Act.Fluid, bAsleep});
    ++ParkedCount;
}

int32 FVoxelFluidScheduler::ParkAsleep(uint64 Tick, TFunctionRef<bool(FIntPoint Column)> IsAsleep)
{
    int32 Count = 0;
    for (int32 Index = Heap.Num() - 1; Index >= 0; --Index)
    {
        const FAct& Act = Heap[Index];
        const FIntPoint Column = GetColumn(Act.Cell);
        if (!IsAsleep(Column)) continue;
        Parked.FindOrAdd(Column).Add(
            FParked{Act.Due > Tick ? Act.Due - Tick : 0, Act.Order, Act.Cell, Act.Fluid, true});
        ++ParkedCount;
        ++Count;
        Heap.RemoveAtSwap(Index, EAllowShrinking::No);
    }
    if (Count) Heap.Heapify(FFluidActBefore());
    return Count;
}

int32 FVoxelFluidScheduler::Unpark(FIntPoint Column, uint64 Tick, bool bInRing)
{
    return Unpark(Tick, [Column, bInRing](FIntPoint Other) { return Other != Column ? 0 : bInRing ? 2 : 1; });
}

int32 FVoxelFluidScheduler::Unpark(uint64 Tick, TFunctionRef<int32(FIntPoint Column)> Release, uint64 MinDue)
{
    TArray<FParked> Leaving;
    for (auto It = Parked.CreateIterator(); It; ++It)
    {
        const int32 Level = Release(It.Key());
        if (Level <= 0) continue;
        TArray<FParked>& List = It.Value();
        for (int32 Index = 0; Index < List.Num();)
            if (Level >= 2 || !List[Index].bAsleep)
            {
                Leaving.Add(List[Index]);
                List.RemoveAt(Index, EAllowShrinking::No);
            }
            else
                ++Index;
        if (List.IsEmpty()) It.RemoveCurrent();
    }
    // New orders follow the first ones, so acts due in one tick keep their first-in, first-out order.
    Leaving.Sort([](const FParked& A, const FParked& B) { return A.Order < B.Order; });
    for (const FParked& Act : Leaving)
        Heap.HeapPush(
            FAct{FMath::Max(Tick + Act.Remaining, MinDue), NextOrder++, Act.Cell, Act.Fluid}, FFluidActBefore());
    ParkedCount -= Leaving.Num();
    return Leaving.Num();
}

void FVoxelFluidScheduler::GetParkedColumns(TArray<FIntPoint>& Out) const
{
    Parked.GetKeys(Out);
}
