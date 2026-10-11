#pragma once

#include "CoreMinimal.h"

class ACameraActor;
class AVoxelWorld;
class FVoxelColumnPipeline;
class IConsoleVariable;

// The flowing water captures (water and sky W2a and W3b: -VoxelShotFluids=breach|flood|scenes with -VoxelShot=<folder>,
// Scripts/CaptureVoxel.ps1 -Fluids). It runs once the first view is on screen, before the -VoxelShot shots, with
// voxel.Fluids on (put back at the end); every write goes through the authority's block world, so the fluids wake up
// as from the player's edits. The edited cells are not put back.
// - breach (the plan's "dig into a sea from a cave; the cave floods"): the sea column nearest the streaming center, in
//   the fluid simulation ring, whose water sources reach the top water block (62) over a solid seabed. A sealed 7 x 7 x
//   4 air room is carved under it (its 9 x 9 x 6 shell and the shaft's walls solid full blocks without fluid, or the
//   next column is tried), with a one-cell shaft up to the seabed block and four glowstones in the ceiling. A shot of
//   the dry room from a ceiling corner, then the seabed block is mined (set to air) and shots follow at 3, 8 and 20 s.
//   Writes breach.csv (each second: the room's water sources, flowing and falling cells, and the fluid stats),
//   breach.txt and breach-*.png.
// - flood (the plan's frame-time trace of a 1,000-cell flood): the flattest dry ground near the center in the ring
//   (the least mean height difference from its middle) takes a 5 x 5 grid of water sources 10 cells apart, each in the
//   air cell over a solid top block at most 2 cells off its grid point. For 30 s, flood.csv gets a row per frame
//   (frame ms, the fluids' ms, acts, ticks behind and queued acts; every second the water cells in the flood area),
//   flood.txt the means, p95s and maxima, the frames over voxel.Fluids.BudgetMs and the tick the flood settled at, and
//   flood-end.png a view from above at the end.
// - scenes (W3b, the look shots beside vanilla's): the vanilla client probe's water and lava scenes
//   (C:\dev\sandbox\vanilla-probes\facts.md, "W3 water and lava scenes") on its Superflat ground (grass top at y -61;
//   -World with a Superflat world), built by its commands in its order with vanilla (x, y, z) at cells (x, z, y), and
//   shot from its 20 poses (the /tp feet + 1.62, yaw + 90, pitch negated) at the probe's measured 77 degree vertical
//   field of view (FluidSceneVerticalFov): the water poses from FluidSceneWaterSeconds after the build, the lava poses
//   from FluidSceneLavaSeconds, as the probe's frames were taken. voxel.Fluids stays as set (a control run sets it to
//   0). Writes scenes.txt (the reach checks the probe made over RCON, each scene's ground as levels, the mesh jobs
//   while it ran) and scenes-<pose>.png.
class FVoxelFluidShots
{
public:
    enum class EScenario : uint8
    {
        Breach,
        Flood,
        Scenes
    };

    // Null, with a warning, when Name is not breach, flood or scenes.
    static TSharedPtr<FVoxelFluidShots> Make(const FString& InFolder, const FString& Name);
    FVoxelFluidShots(const FString& InFolder, EScenario InScenario);
    ~FVoxelFluidShots();
    // Runs one frame; false once the scenario has finished, written its files and put voxel.Fluids back.
    bool Tick(AVoxelWorld& World);
    // Started and not finished: Tick then runs every frame, also those whose fluid writes have columns remeshing (the
    // world not ready), so the traces miss no frame.
    bool IsRunning() const { return Stage != EStage::Enable && Stage != EStage::Done; }

private:
    enum class EStage : uint8
    {
        // voxel.Fluids switched on; the fluids see the ring from the next frame's Advance.
        Enable,
        Setup,
        // Breach: the carved room remeshes and the light settles; then the dry shot.
        Settle,
        Run,
        // The last screenshot is written.
        Drain,
        Done
    };
    // Water cells in a box, by kind.
    struct FCount
    {
        int32 Sources = 0;
        int32 Flowing = 0;
        int32 Falling = 0;
        int32 Total() const { return Sources + Flowing + Falling; }
    };

    // Find the site, write the room (breach) and place the camera; false, with the reason in the report, when no site
    // was found.
    bool SetupBreach(AVoxelWorld& World);
    // Mines the seabed block over the shaft.
    void BreakSeabed(AVoxelWorld& World);
    // A breach.csv row Seconds after the seabed was mined.
    void RecordBreachSecond(AVoxelWorld& World, double Seconds);
    void TickBreach(AVoxelWorld& World);
    bool SetupFlood(AVoxelWorld& World);
    // Counts the water already in the flood box, then places the sources.
    void StartFlood(AVoxelWorld& World);
    void TickFlood(AVoxelWorld& World);
    // Water in the cells of an inclusive box, read from the pipeline's final columns.
    FCount CountWater(AVoxelWorld& World, const FIntVector& Min, const FIntVector& Max) const;
    // Builds the vanilla scenes; false, with the reason, when the ground is not VP1's Superflat or not loaded.
    bool SetupScenes(AVoxelWorld& World);
    void TickScenes(AVoxelWorld& World);
    // The reach checks, the scenes' ground and the mesh jobs, into the report.
    void RecordScenes(AVoxelWorld& World);
    void SetPose(AVoxelWorld& World, const FVector& Blocks, const FRotator& Rotation);
    // The fluid stats as CSV fields (tick, behind, queued, parked, last ms, last acts, acts, writes, worst ms, frames
    // over budget), and the header for them.
    FString DescribeStats(AVoxelWorld& World) const;
    static const TCHAR* StatsHeader();
    void SetView(AVoxelWorld& World, const FVector& Blocks, const FVector& Target);
    // Requests a screenshot to <scenario>-<Name>.png at the end of this frame.
    void Shoot(const FString& Name);
    // The last screenshot's file is not written yet, and it was requested less than FluidShotWriteFrames ago.
    bool IsShotPending() const;
    double GetSeconds(AVoxelWorld& World) const;
    void Line(const FString& Text);
    void Finish(AVoxelWorld& World);

    FString Folder;
    EScenario Scenario = EScenario::Breach;
    EStage Stage = EStage::Enable;
    IConsoleVariable* FluidsVariable = nullptr;
    int32 FluidsAtStart = 0;
    // The shot camera's horizontal field of view.
    float FieldOfView = 90;
    TWeakObjectPtr<ACameraActor> Camera;
    FVector CameraBlocks = FVector::ZeroVector;
    FVector TargetBlocks = FVector::ZeroVector;
    // Frames in the current stage, those since the pipeline went idle (Settle), and the world time and game tick it
    // began at.
    int32 StageFrames = 0;
    int32 IdleFrames = 0;
    double StageSeconds = 0;
    uint64 StageTick = 0;
    // The last screenshot file requested and its frame.
    FString LastShot;
    uint64 LastShotFrame = 0;
    TArray<FString> Report;
    TArray<FString> Csv;

    // Breach: the shaft's column, the seabed block's Z, the room's interior (inclusive) and the shaft's cells.
    FIntPoint Shaft = FIntPoint::ZeroValue;
    int32 Seabed = 0;
    FIntVector RoomMin = FIntVector::ZeroValue;
    FIntVector RoomMax = FIntVector::ZeroValue;
    TArray<FIntVector> ShaftCells;
    // Whole seconds and shots (at 3, 8 and 20 s) done since the seabed was mined.
    int32 BreachSecond = -1;
    int32 BreachShot = 0;

    // Scenes: the next pose and the frames it has been on screen; the mesh jobs (count and total ms) at the build.
    int32 ScenePose = 0;
    int32 PoseFrames = 0;
    int64 MeshJobsAtBuild = 0;
    double MeshMsAtBuild = 0;

    // Flood: the grid's centre, the box the water is counted in, the water there before the sources, and per frame
    // the frame ms and the fluids' ms.
    FIntPoint FloodCenter = FIntPoint::ZeroValue;
    TArray<FIntVector> FloodSources;
    FIntVector FloodMin = FIntVector::ZeroValue;
    FIntVector FloodMax = FIntVector::ZeroValue;
    int32 FloodBaseline = 0;
    TArray<double> FrameMs;
    TArray<double> FluidMs;
    int32 FloodSecond = -1;
    int32 PeakCells = 0;
    int32 LastCells = 0;
    int64 CutAtStart = 0;
    // The first frame with nothing queued and nothing behind: the fluid tick, game ticks and seconds since the sources.
    bool bSettled = false;
    uint64 SettledTick = 0;
    uint64 SettledTicks = 0;
    double SettledSeconds = 0;
};
