#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlocks.h"
#include "Fluids/VoxelFluidRules.h"
#include "Fluids/VoxelFluidScheduler.h"
#include "Streaming/VoxelStreamingPlanner.h"

class FFoundationBlockRegistry;
struct FGameplayTag;
class FVoxelBlockTable;
class FVoxelColumn;
class FVoxelColumnPipeline;

// Flowing water and lava in a voxel world (water and sky W2a): W1's rules (VoxelFluidRules) on the streamed columns,
// run on the world clock's game ticks. AVoxelWorld owns one; the Playground has none (plan Q13).
// - Wake-up: the block world's OnSet hook. Every change it commits (an edit, a plant pop) wakes the cell and its six
//   neighbours as VoxelFluidRules::Apply does, and placed lava meets water at once.
// - Ticks: its own tick, which equals the game tick while it runs. Under voxel.Fluids 0 it stands still and ignores
//   edits (the control). When a frame's acts pass voxel.Fluids.BudgetMs it falls behind and catches up later: the
//   same world, later.
// - Writes: an act's writes land in an overlay that later reads see, then go to the block world's SetMany at the end
//   of the frame: stamped edits that replay after Finalize and survive unloading, one column copy per column.
// - Streaming: acts run in the simulation ring only, the view less one column (plan Q6; voxel.Fluids.SimRadius can
//   narrow it), so the cells an act reads (at most 6 away) are in view. Pending acts of a column that leaves the ring
//   are parked on it with the delay they had left; an act that reads a column not final yet is parked on that column.
//   Both are re-queued when their column is back (in the ring and final, or final).
// Game thread only.
class UNNAMED_API FVoxelFluidSystem
{
public:
    struct FSettings
    {
        // voxel.Fluids: off (the default until W3 meshes flowing fluid) stops the fluid ticks and ignores edits.
        bool bEnabled = false;
        // voxel.Fluids.BudgetMs: game-thread time a frame's acts may take (plan Q10); 0 or less, no limit.
        double BudgetMs = 0.5;
        // voxel.Fluids.SimRadius: acts run at most this many columns from the center on both axes; negative, the
        // whole ring.
        int32 SimRadius = -1;

        static FSettings Read();
    };

    struct FStats
    {
        // The fluid tick, and the game ticks it is behind.
        uint64 Tick = 0;
        uint64 Behind = 0;
        int32 Queued = 0;
        int32 Parked = 0;
        int64 Acts = 0;
        // Acts that read a column not final yet, and pending acts put to sleep outside the ring.
        int64 Waits = 0;
        int64 Sleeps = 0;
        int64 Writes = 0;
        // Writes the IsKnown guard kept off a cell that was not known when they were flushed.
        int64 Lost = 0;
        // The last frame's game-thread time and acts, and the worst frame's time.
        double LastMs = 0;
        int32 LastActs = 0;
        double WorstMs = 0;
        // Frames whose fluid time passed the budget, and frames that stopped at it with acts left.
        int64 OverBudgetFrames = 0;
        int64 CutFrames = 0;
    };

    FVoxelFluidSystem() = default;
    ~FVoxelFluidSystem();
    FVoxelFluidSystem(const FVoxelFluidSystem&) = delete;
    FVoxelFluidSystem& operator=(const FVoxelFluidSystem&) = delete;

    // Runs on World, whose storage is GetPipeline's columns (FVoxelBlockStorage), from the game tick Tick, and installs
    // World's OnSet hook. False, with nothing installed, when Table lacks a fluid level or a block the rules write.
    bool Start(FFoundationBlockWorld& InWorld, TSharedRef<const FVoxelBlockTable> InTable,
        TFunction<FVoxelColumnPipeline*()> InGetPipeline, const FVoxelFluidOptions& InOptions, uint64 InTick);
    // Removes the hook and forgets the pending acts.
    void Stop();
    // Forgets the pending acts (a new seed, or the edits dropped); the tick goes on.
    void Reset();
    // One frame in which Ticks game ticks passed: runs the acts due within the budget, then writes what they changed.
    void Advance(int32 Ticks, const FSettings& InSettings);

    // The block world's hook: a change wakes the fluid around it.
    void OnSet(const FIntVector& Cell, FFoundationBlockState Before, FFoundationBlockState After);
    // After a flush, every cell it changed with the state the fluid replaced, and whether that block was washed away
    // (water and sky W2b): AVoxelWorld's authority pops it with no drop (plan Q7) and settles the plants around it.
    // Changes made here wake the fluid through the hook, as edits do.
    TFunction<void(const FIntVector& Cell, FFoundationBlockState Before, bool bWashedAway)> OnFlushed;

    // The rules' world (VoxelFluidRules' TWorld). Get reads the overlay, then the final columns (unknown in a column
    // that is not final); Set writes the overlay.
    FVoxelFluidCell Get(FIntVector Cell) const;
    void Set(FIntVector Cell, const FVoxelFluidCell& Value);
    // A state as the rules see it, and the state a rules' cell is written as.
    const FVoxelFluidCell& GetCell(uint16 State) const;
    uint16 GetState(const FVoxelFluidCell& Cell) const;

    // Within the simulation ring of the rings last seen.
    bool IsInRing(FIntPoint Column) const;
    bool IsRunning() const { return World != nullptr; }
    const FVoxelFluidOptions& GetOptions() const { return Options; }
    const FVoxelFluidScheduler& GetScheduler() const { return Scheduler; }
    FStats GetStats() const;
    // voxel.Fluids.Info's line.
    FString Describe() const;

private:
    void RunAct(const FVoxelFluidScheduler::FAct& Act);
    // Parks the acts of columns that left the ring, and re-queues those whose column is back.
    void UpdateRing(const FVoxelStreamingRings& Rings);
    // Writes the overlay through the block world.
    void Flush();
    FVoxelFluidCell MakeCell(
        uint16 State, const FFoundationBlockRegistry& Registry, const FGameplayTag& WashedAway) const;

    struct FOverlayCell
    {
        FVoxelFluidCell Cell;
        uint16 State = 0;
    };

    FFoundationBlockWorld* World = nullptr;
    TSharedPtr<const FVoxelBlockTable> Table;
    TFunction<FVoxelColumnPipeline*()> GetPipeline;
    FVoxelFluidOptions Options;
    FSettings Settings;
    FVoxelFluidScheduler Scheduler;
    TArray<FVoxelFluidWrite> Scratch;
    // The rules' view of every state, and the states the rules write.
    TArray<FVoxelFluidCell> Cells;
    uint16 WaterStates[16] = {};
    uint16 LavaStates[16] = {};
    uint16 BlockStates[int32(EVoxelFluidBlock::Basalt) + 1] = {};
    // The tick the acts run at (the last one started), the tick to reach, and whether Tick's acts are not all run.
    uint64 Tick = 0;
    uint64 Target = 0;
    bool bInTick = false;
    // The overlay of writes not flushed yet, in the order they were first made.
    TMap<FIntVector, FOverlayCell> Overlay;
    TArray<FIntVector> OverlayOrder;
    bool bFlushing = false;
    // The ring: the rings last seen and voxel.Fluids.SimRadius.
    FVoxelStreamingRings Ring;
    int32 RingSimRadius = -1;
    bool bHasRing = false;
    // The frame's pipeline, and the last column read (reads are cached per column until a flush).
    FVoxelColumnPipeline* Pipeline = nullptr;
    mutable FIntPoint CachedColumn = FIntPoint::ZeroValue;
    mutable const FVoxelColumn* CachedData = nullptr;
    mutable bool bCached = false;
    // The column of the first unknown cell the current act read.
    mutable FIntPoint UnknownColumn = FIntPoint::ZeroValue;
    mutable bool bReadUnknown = false;
    // What an act and a flushed cell cost, measured (the most of recent frames that acted, falling a tenth each, so a
    // costly one counts): the acts stop when the next one and the flush would pass the budget. Negative until first
    // measured.
    double ActCycles = -1;
    double FlushCyclesPerCell = -1;
    FStats Stats;
};
