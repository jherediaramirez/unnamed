#pragma once

#include "CoreMinimal.h"
#include "Fluids/VoxelFluidRules.h"
#include "Fluids/VoxelFluidScheduler.h"

// An in-memory world for the fluid rules' tests, with no game wiring: superflat stone up to FloorTop and air above
// (scout M's probe worlds), the cells the tests set, a game tick and the scheduler. Edits behave as 26.3's /setblock:
// the change wakes the fluids around it at the current tick.
class UNNAMEDVOXEL_API FVoxelFluidTestWorld
{
public:
    explicit FVoxelFluidTestWorld(int32 InFloorTop = 63, const FVoxelFluidOptions& InOptions = FVoxelFluidOptions());

    FVoxelFluidCell Get(FIntVector Cell) const;
    // Sets a cell with no wake-ups (the rules' writes call it through VoxelFluidRules::Apply).
    void Set(FIntVector Cell, const FVoxelFluidCell& Value);
    // An edit at the current tick: placed lava meets water at once, and the fluids around act after their delay.
    void Place(FIntVector Cell, const FVoxelFluidCell& Value);
    // Advances one game tick and runs the acts due.
    void Step();

    uint64 GetTick() const { return Tick; }
    const FVoxelFluidOptions& GetOptions() const { return Options; }
    const FVoxelFluidScheduler& GetScheduler() const { return Scheduler; }
    int64 GetActCount() const { return ActCount; }
    int64 GetWriteCount() const { return WriteCount; }

private:
    FVoxelFluidOptions Options;
    int32 FloorTop = 63;
    TMap<FIntVector, FVoxelFluidCell> Cells;
    FVoxelFluidScheduler Scheduler;
    TArray<FVoxelFluidWrite> Scratch;
    uint64 Tick = 0;
    int64 ActCount = 0;
    int64 WriteCount = 0;
};
