#pragma once

#include "CoreMinimal.h"
#include "Core/RealtimeMeshDataStream.h"
#include "MeshCardBuild.h"
#include "Meshing/VoxelMesher.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Tasks/Task.h"

class FVoxelBlockTable;
class FVoxelColumn;
class IVoxelTerrain;

// A column's finished mesh on its way to the screen: the stream set and Lumen cards UVoxelChunkComponent::SetMesh
// takes, built on a worker from the column's section meshes.
struct FVoxelColumnMesh
{
    RealtimeMesh::FRealtimeMeshStreamSet Streams;
    FMeshCardsBuildData Cards;
    // A bit per layer with triangles and per section with geometry.
    uint32 LayerMask = 0;
    uint32 SectionMask = 0;
    int32 QuadCount = 0;
    // Sections this job meshed; the others came from the column's cache.
    int32 MeshedSections = 0;
    // Every section's mesh, null where it has no geometry: the column's cache near the camera.
    TArray<TSharedPtr<const FVoxelMeshData, ESPMode::ThreadSafe>> Sections;
    SIZE_T SectionBytes = 0;
    // Worker time gathering and meshing the sections, then building the stream set and the cards.
    double MeshMs = 0;
    double StreamsMs = 0;
};

// One stage's timings: how many, their total and the worst, in milliseconds.
struct FVoxelStageTimes
{
    int64 Count = 0;
    double TotalMs = 0;
    double WorstMs = 0;

    void Add(double Ms)
    {
        ++Count;
        TotalMs += Ms;
        WorstMs = FMath::Max(WorstMs, Ms);
    }
    double GetMeanMs() const { return Count ? TotalMs / Count : 0.; }
};

// What one column is doing, for tests and diagnostics.
struct FVoxelColumnStatus
{
    bool bPresent = false;
    bool bGenerated = false;
    bool bGenerating = false;
    bool bMeshing = false;
    // A finished mesh waits to be applied.
    bool bReady = false;
    // The latest finished mesh was taken for the screen.
    bool bOnScreen = false;
    // Sections waiting to be meshed.
    uint32 DirtySections = 0;
    bool bCached = false;
};

struct FVoxelPipelineStats
{
    // Columns with a record: generated, or needed and waiting.
    int32 Columns = 0;
    int32 Generated = 0;
    int32 Needed = 0;
    int32 InView = 0;
    int32 InViewOnScreen = 0;
    int32 Generating = 0;
    int32 Meshing = 0;
    int32 Ready = 0;
    // Columns whose mesh is on screen, and their quads and Lumen cards.
    int32 OnScreen = 0;
    int64 ScreenQuads = 0;
    int32 ScreenCards = 0;
    int32 MostCards = 0;
    SIZE_T ColumnBytes = 0;
    SIZE_T CacheBytes = 0;
    // Edited cells, kept per column whether loaded or not.
    int32 Edits = 0;
    // Results dropped because their column was unloaded or changed while the job ran.
    int64 Dropped = 0;
    FVoxelStageTimes Generate;
    FVoxelStageTimes Mesh;
    FVoxelStageTimes Streams;
};

// The voxel world's streaming pipeline (phase 3): a record per column around the camera, and the UE::Tasks jobs that
// generate and mesh them. Plain C++ with no UObjects, so tests drive it without a world; UVoxelWorldSubsystem runs it
// every frame and hands the finished meshes to chunk components.
//
// - Rings: SetFocus moves them. The plan is rebuilt only when the rings or the view sector change. Columns beyond the
//   unload distance are dropped at once; their jobs' results are thrown away when they arrive.
// - Jobs: walking the needed columns nearest first, a column in view whose 3 x 3 neighbours are generated gets a mesh
//   job, and a needed column without blocks gets a generation job, up to MaxTasksInFlight together.
// - A mesh job gathers and meshes the column's dirty sections (all of them the first time, and whenever the column has
//   no cache), then builds its stream set and Lumen cards, so the game thread only hands them over.
// - Stale results are dropped: those of an unloaded column (its record's serial changed) and those whose column was
//   marked dirty again while the job ran. The column stays dirty and is meshed again.
// - Edits (SetCell) copy the column and change the copy, so jobs keep reading the column they started with. They mark
//   exactly the sections whose padded copy holds the cell dirty, and they are kept per column and replayed after
//   generation, so they survive unloading: for the session, until phase 10 saves them.
//
// Game thread only. Jobs read shared, immutable columns and write only their own results.
class FVoxelColumnPipeline
{
public:
    struct FSettings
    {
        FVoxelMeshOptions MeshOptions;
        // Generation and mesh jobs in flight together; at least 1.
        int32 MaxTasksInFlight = 1;
        // Columns within this many columns of the center on both axes keep their section meshes, so an edit there
        // remeshes only the sections it touched.
        int32 MeshCacheRadius = 2;
        // Debug: columns at the edge of the view mesh against air, showing the cut through the world (captures).
        bool bEdgeWalls = false;
    };

    // A finished mesh taken for the screen.
    struct FReady
    {
        FIntPoint Column = FIntPoint::ZeroValue;
        TUniquePtr<FVoxelColumnMesh> Mesh;
    };

    FVoxelColumnPipeline(TSharedRef<const FVoxelBlockTable> InTable, TSharedRef<const IVoxelTerrain> InTerrain,
        const FSettings& InSettings);
    // Waits for the jobs in flight.
    ~FVoxelColumnPipeline();
    FVoxelColumnPipeline(const FVoxelColumnPipeline&) = delete;
    FVoxelColumnPipeline& operator=(const FVoxelColumnPipeline&) = delete;

    // Changes picked up by the next Update; changing the edge walls remeshes the columns at the edge, and changing the
    // tint blend remeshes every column.
    void SetMaxTasksInFlight(int32 MaxTasks);
    void SetEdgeWalls(bool bEdgeWalls);
    void SetTintBlend(int32 TintBlend);

    // Moves the rings. A new plan is made only when the rings or the view sector change.
    void SetFocus(const FVoxelStreamingRings& Rings, int32 ViewSector);
    // Collects finished jobs and launches new ones.
    void Update();
    // Tests: blocks until every job in flight has finished; the next Update collects them.
    void WaitForJobs();

    // The finished mesh nearest the center, without taking it.
    const FVoxelColumnMesh* PeekReady(FIntPoint* OutColumn = nullptr);
    // Takes the finished mesh nearest the center: its column counts as on screen from now on, so the caller applies it.
    bool PopReady(FReady& Out);
    // Columns unloaded since the last call whose meshes had been taken for the screen.
    void TakeUnloaded(TArray<FIntPoint>& Out);

    const FVoxelStreamingRings& GetRings() const { return Rings; }
    // A generated column, or null when it is not loaded.
    const FVoxelColumn* FindColumn(FIntPoint Column) const;
    bool IsGenerated(FIntPoint Column) const { return FindColumn(Column) != nullptr; }
    void ForEachColumn(TFunctionRef<void(FIntPoint, const FVoxelColumn&)> Visit) const;

    // A world cell's state and the stamp of its last edit (0: never edited). False when its column is not generated.
    bool GetCell(const FIntVector& Cell, uint16& OutState, int64& OutStamp) const;
    // Sets a cell of a generated column, keeping Stamp with the edit (the block world's revision). False, changing
    // nothing, when the column is not generated or the cell lies outside the world's height.
    bool SetCell(const FIntVector& Cell, uint16 State, int64 Stamp);
    // Forgets every edit; loaded columns that had one are generated again.
    void DropEdits();
    // Every non-air cell of the generated columns inside an inclusive range, with its edit stamp.
    void ForEachNonAir(
        const FIntVector& Min, const FIntVector& Max, TFunctionRef<void(const FIntVector&, uint16, int64)> Visit) const;
    int64 CountNonAir() const;
    SIZE_T GetEditBytes() const;
    FVoxelColumnStatus GetStatus(FIntPoint Column) const;
    int32 GetJobCount() const { return Jobs.Num(); }
    // Every needed column is generated, every column in view is meshed and every finished mesh has been taken.
    bool IsViewOnScreen() const;
    // Nothing in flight and nothing left to launch or take.
    bool IsIdle() const;
    FVoxelPipelineStats GetStats() const;

private:
    struct FRecord
    {
        // New every time the column loads, so a job's result can tell whether its column is still the one it started
        // for.
        uint64 Serial = 0;
        TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe> Data;
        SIZE_T DataBytes = 0;
        // Sections to mesh: every section until the first mesh, then those edits touch.
        uint32 Dirty = 0;
        // Rises whenever sections become dirty; a mesh job started at an older version is stale.
        uint32 MeshVersion = 0;
        // The neighbours the latest accepted mesh read, and those the job in flight reads (edge walls leave some out).
        uint8 MeshedNeighbors = 0;
        uint8 MeshingNeighbors = 0;
        bool bGenerating = false;
        bool bMeshing = false;
        bool bOnScreen = false;
        TUniquePtr<FVoxelColumnMesh> Ready;
        // Section meshes kept near the center, so an edit remeshes only what it touched; empty elsewhere.
        TArray<TSharedPtr<const FVoxelMeshData, ESPMode::ThreadSafe>> Cache;
        SIZE_T CacheBytes = 0;
        int32 ScreenQuads = 0;
        int32 ScreenCards = 0;
    };
    struct FJob;
    struct FJobResult;
    struct FEdit
    {
        uint16 State = 0;
        int64 Stamp = 0;
    };

    FRecord& AddRecord(FIntPoint Column);
    void Unload(FIntPoint Column);
    void Collect();
    void LaunchJobs();
    void LaunchGenerate(FIntPoint Column, FRecord& Record);
    void LaunchMesh(FIntPoint Column, FRecord& Record, uint8 Neighbors);
    // The neighbours a column's mesh reads, a bit per neighbour in FVoxelNeighborhood order (skipping the center):
    // all eight, or with edge walls only those in view.
    uint8 GetMeshNeighbors(FIntPoint Column) const;
    bool AreGenerated(FIntPoint Column, uint8 Neighbors) const;
    bool IsComplete(int32 NeededIndex) const;
    void MarkDirty(FRecord& Record, uint32 Sections);
    void SortReady();

    TSharedRef<const FVoxelBlockTable> Table;
    TSharedRef<const IVoxelTerrain> Terrain;
    FSettings Settings;

    FVoxelStreamingRings Rings;
    int32 ViewSector = 0;
    bool bPlanned = false;
    // The needed columns in priority order, and whether each is in view.
    TArray<FIntPoint> Needed;
    TBitArray<> NeededInView;
    // Every needed column before this index is generated and, if in view, meshed.
    int32 FirstIncomplete = 0;

    TMap<FIntPoint, FRecord> Records;
    // Per column, by dense index (FVoxelColumn::GetDenseIndex), every cell edited this session.
    TMap<FIntPoint, TMap<int32, FEdit>> Edits;
    uint64 NextSerial = 0;
    TArray<FJob> Jobs;
    // Columns with a finished mesh, nearest the center first once sorted.
    TArray<FIntPoint> ReadyColumns;
    bool bReadySorted = true;
    TArray<FIntPoint> UnloadedOnScreen;
    // Some column was meshed against air at the edge, so later plans check which edges moved.
    bool bEdgeWallsUsed = false;

    int64 Dropped = 0;
    FVoxelStageTimes GenerateTimes, MeshTimes, StreamsTimes;
};
