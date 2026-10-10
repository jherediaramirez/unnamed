#include "Fluids/VoxelFluidTestWorld.h"

FVoxelFluidTestWorld::FVoxelFluidTestWorld(int32 InFloorTop, const FVoxelFluidOptions& InOptions)
    : Options(InOptions), FloorTop(InFloorTop)
{
}

FVoxelFluidCell FVoxelFluidTestWorld::Get(FIntVector Cell) const
{
    if (const FVoxelFluidCell* Found = Cells.Find(Cell)) return *Found;
    return Cell.Z <= FloorTop ? FVoxelFluidCell::MakeSolid() : FVoxelFluidCell();
}

void FVoxelFluidTestWorld::Set(FIntVector Cell, const FVoxelFluidCell& Value)
{
    Cells.Add(Cell, Value);
    ++WriteCount;
}

void FVoxelFluidTestWorld::Place(FIntVector Cell, const FVoxelFluidCell& Value)
{
    VoxelFluidRules::Apply(*this, Scheduler, Tick, FVoxelFluidWrite{Cell, Get(Cell), Value}, Options);
}

void FVoxelFluidTestWorld::Step()
{
    ++Tick;
    ActCount += VoxelFluidRules::RunTick(*this, Scheduler, Tick, Options, Scratch);
}
