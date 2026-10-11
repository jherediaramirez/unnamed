#pragma once

#include "CoreMinimal.h"
#include "Core/RealtimeMeshDataStream.h"
#include "HAL/ThreadSafeBool.h"
#include "MeshCardBuild.h"
#include "Meshing/VoxelFarMesher.h"
#include "Meshing/VoxelMesher.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Tasks/Task.h"
#include "Voxel/VoxelLightSources.h"

class FVoxelBlockTable;
class FVoxelColumn;
class FVoxelDecorator;
class IVoxelTerrain;
struct FVoxelWriteBucket;

// Which mesh a column has: none yet, its full mesh (in view) or its far mesh (far LOD, phase 9 S1).
enum class EVoxelMeshKind : uint8
{
    None,
    Full,
    Far
};

// A column's finished mesh on its way to the screen: the stream set and Lumen cards UVoxelChunkComponent::SetMesh
// takes, built on a worker from the column's section meshes, or from its surface summary for a far mesh (no cards).
struct FVoxelColumnMesh
{
    bool bFar = false;
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
    // The column's light sources (VoxelLightSources::GatherSources), scanned by every mesh job while the pipeline has
    // a light table, whether the light pool is live or not; empty without a table.
    TArray<FVoxelLightSource> Lights;
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
    // Its final blocks are loaded: decorated and finished (or, without decoration, generated), edits replayed.
    bool bGenerated = false;
    bool bGenerating = false;
    bool bMeshing = false;
    // A finished mesh waits to be applied.
    bool bReady = false;
    // The latest finished mesh was taken for the screen.
    bool bOnScreen = false;
    // The kind of mesh on screen, and of the latest finished one.
    EVoxelMeshKind Shown = EVoxelMeshKind::None;
    EVoxelMeshKind Meshed = EVoxelMeshKind::None;
    // Sections waiting to be meshed.
    uint32 DirtySections = 0;
    bool bCached = false;
    // Decoration's stages: the snapshot is held, the column's Decorate ran since it loaded (some of its buckets may
    // have been consumed since), and the jobs in flight. HeldBuckets has a bit per bucket still held
    // (FVoxelWriteLog::GetBucket of the target's offset).
    bool bSnapshot = false;
    bool bDecorated = false;
    bool bDecorating = false;
    bool bFinalizing = false;
    uint16 HeldBuckets = 0;
};

struct FVoxelPipelineStats
{
    // Columns with a record: loaded at some stage, or needed and waiting.
    int32 Columns = 0;
    // Columns with final blocks.
    int32 Generated = 0;
    int32 Needed = 0;
    // Needed columns per ring (FVoxelStreamingRings::GetRing): in view, final, decorated, generated.
    int32 NeededPerRing[FVoxelStreamingRings::MaxStages + 1] = {};
    int32 InView = 0;
    // Columns in view whose full mesh is on screen, current and taken.
    int32 InViewOnScreen = 0;
    // Far LOD: far columns wanted (drawn, beyond the view), far meshes on screen and their quads, far jobs in flight
    // and far meshes waiting; Coarse counts columns in view still showing their far mesh.
    int32 FarWanted = 0;
    int32 FarOnScreen = 0;
    int64 FarQuads = 0;
    int32 FarMeshing = 0;
    int32 FarReady = 0;
    int32 Coarse = 0;
    int32 Generating = 0;
    int32 Meshing = 0;
    // Finished meshes waiting to be taken, full and far.
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
    // Results dropped because their column was unloaded while the job ran.
    int64 Dropped = 0;
    // Mesh results whose column changed while the job ran: shown, and the column meshed again (water and sky W2a).
    int64 Stale = 0;
    FVoxelStageTimes Generate;
    FVoxelStageTimes Mesh;
    FVoxelStageTimes Streams;
    // Far mesh jobs: summaries, mesh and stream set together.
    FVoxelStageTimes FarMesh;

    // Decoration's stages (zero without a decorator).
    // Buckets: those held with writes (an empty bucket is held as a flag alone).
    int32 Snapshots = 0, Decorated = 0, Buckets = 0;
    int32 Decorating = 0, Finalizing = 0;
    SIZE_T SnapshotBytes = 0, BucketBytes = 0;
    FVoxelStageTimes Decorate;
    FVoxelStageTimes Finalize;
    // Recompute-on-miss (scout C, recommendation 1): snapshots generated again for columns that had decorated or
    // finalized before (their snapshot was freed), and Decorates run again because a Finalize missed a bucket.
    int64 Regenerated = 0;
    int64 Redecorated = 0;
    // Buckets aimed at a column that was already final, dropped and never applied (edits stay as the player left them).
    int64 BucketsDropped = 0;
    // Mutation check only: buckets applied to a final column. Zero otherwise.
    int64 BucketsAppliedToFinal = 0;
    // Jobs whose column unloaded before they started, skipped without running.
    int64 Cancelled = 0;
};

// Test-only changes that must break the pipeline checks named beside them (S3's targeted mutations).
enum class EVoxelPipelineMutation : uint8
{
    None,
    // A bucket aimed at a column that is already final is applied to it: Pipeline.ReturnTrip fails (edits undone).
    ApplyToFinal,
    // A Finalize missing a source's bucket treats it as empty instead of running the source's Decorate again:
    // Pipeline.EvictionFuzz fails (final columns differ from the oracle).
    MissingBucketAsEmpty,
    // Snapshots are never freed: Pipeline.Stages fails (final columns keep their snapshots at rest).
    KeepSnapshots
};

// The voxel world's streaming pipeline (phase 3; decoration's stages since phase 6, S3): a record per column around
// the camera, and the UE::Tasks jobs that build and mesh them. Plain C++ with no UObjects, so tests drive it without a
// world; UVoxelWorldSubsystem runs it every frame and hands the finished meshes to chunk components.
//
// - Rings: SetFocus moves them. The plan is rebuilt only when the rings or the view sector change. Columns beyond the
//   unload distance are dropped at once; their jobs skip their work if they have not started, and their results are
//   thrown away when they arrive.
// - Stages: without a decorator, a Generate job gives a column its final blocks. With one (plan 3.1), Generate gives
//   the snapshot, Decorate (its 3 x 3 snapshots) a write log in 9 buckets, one per target column, and Finalize (its
//   snapshot and the 9 buckets aimed at it) the final column: the merge, the freeze pass, then the edits. A column
//   wants the stage its ring asks for (FVoxelStreamingRings::GetRing: mesh in view, final, decorated, generated).
// - Jobs: walking the needed columns nearest first, each column's wanted stage is required; whatever it misses is
//   launched as soon as its own inputs exist, up to MaxTasksInFlight together. A column in view whose 3 x 3 is final
//   gets a mesh job.
// - Recompute-on-miss (scout C, recommendation 1): snapshots and buckets are caches of pure functions. A Decorate
//   without a neighbour's snapshot generates it again; a Finalize without a source's bucket runs that source's Decorate
//   again (at worst 9 Decorates and 25 Generates). A bucket aimed at a column that is already final is dropped, never
//   applied: applying it again would undo the player's edits.
// - Lifetimes: a bucket is freed when its target finalizes; a final column's snapshot when its 8 neighbours have
//   decorated. Everything else goes when its column unloads.
// - A mesh job gathers and meshes the column's dirty sections (all of them the first time, and whenever the column has
//   no cache), then builds its stream set and Lumen cards, so the game thread only hands them over.
// - Stale results: those of an unloaded column (its record's serial changed) are dropped. A mesh whose column was
//   marked dirty again while the job ran is still shown (water and sky W2a), and the column stays dirty and is meshed
//   again.
// - Edits (SetCell) copy the final column and change the copy, so jobs keep reading the column they started with. They
//   mark exactly the sections whose padded copy holds the cell dirty, and they are kept per column and replayed after
//   generation (with decoration, after Finalize), so they survive unloading: for the session, until phase 10 saves
//   them.
//
// Game thread only. Jobs read shared, immutable columns and buckets and write only their own results.
class FVoxelColumnPipeline
{
public:
    struct FSettings
    {
        FVoxelMeshOptions MeshOptions;
        // Generation, decoration and mesh jobs in flight together; at least 1.
        int32 MaxTasksInFlight = 1;
        // Columns within this many columns of the center on both axes keep their section meshes, so an edit there
        // remeshes only the sections it touched.
        int32 MeshCacheRadius = 2;
        // Debug: columns at the edge of the view mesh against air, showing the cut through the world (captures).
        bool bEdgeWalls = false;
        // voxel.Decoration: columns are decorated before they mesh, with three rings past the view instead of one.
        TSharedPtr<const FVoxelDecorator> Decorator;
        // Far meshes' options (voxel.Far.CanopyLayer).
        VoxelFarMesher::FOptions FarOptions;
        // Tests: jobs are queued, not launched; RunJob runs one on the calling thread, in any order.
        bool bManualJobs = false;
        EVoxelPipelineMutation Mutation = EVoxelPipelineMutation::None;
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
    // Model quads on or off (voxel.Models) and the look-call shading switch (voxel.Debug.ModelShadeUnturned); a change
    // remeshes every column.
    void SetModelOptions(bool bModels, bool bShadeUnturned);
    // Far meshes' options; a change remeshes every far column.
    void SetFarOptions(const VoxelFarMesher::FOptions& FarOptions);
    // The render-only ore looks (voxel.Look.OreHost, voxel.Look.OreGlint); a change remeshes every column.
    void SetOreLooks(int32 OreHost, int32 OreGlint);
    // Fluid surfaces (voxel.Fluids.SlopedTops and OpenCorners, FVoxelMeshOptions); a change remeshes every column.
    void SetSlopedTops(int32 SlopedTops, bool bOpenCorners);
    // The light table mesh jobs scan their column's light sources with (FVoxelColumnMesh::Lights); null scans none.
    // Jobs launched from now on use it: a column picks a new table up when it next meshes, nothing remeshes for it.
    void SetLightTable(TSharedPtr<const FVoxelLightTable> InLightTable) { LightTable = MoveTemp(InLightTable); }
    bool IsDecorating() const { return Settings.Decorator.IsValid(); }
    const FVoxelDecorator* GetDecorator() const { return Settings.Decorator.Get(); }

    // Moves the rings (their Stages are the pipeline's: 3 with a decorator, 1 without). A new plan is made only when
    // the rings or the view sector change.
    void SetFocus(const FVoxelStreamingRings& Rings, int32 ViewSector);
    // Collects finished jobs and launches new ones.
    void Update();
    // Tests: blocks until every job in flight has finished (manual jobs run in order); the next Update collects them.
    void WaitForJobs();
    // Tests with manual jobs: the jobs not run yet, and running one of them (an index below GetJobCount) here.
    int32 GetPendingJobCount() const;
    bool RunJob(int32 Index);
    // Tests: the column the job at Index (below GetJobCount) works on, and whether it has not run yet.
    FIntPoint GetJobColumn(int32 Index) const;
    bool IsJobPending(int32 Index) const;
    // Tests: forget a column's snapshot or its buckets now, whatever the lifetimes say; a stage that needs them
    // recomputes them. Running jobs keep what they read.
    void EvictSnapshot(FIntPoint Column);
    void EvictBuckets(FIntPoint Column);

    // The finished mesh nearest the center, without taking it: full meshes first, far meshes only when no full mesh
    // waits.
    const FVoxelColumnMesh* PeekReady(FIntPoint* OutColumn = nullptr);
    // Takes the mesh PeekReady shows: its column counts as on screen from now on, so the caller applies it.
    bool PopReady(FReady& Out);
    // Columns unloaded since the last call whose meshes had been taken for the screen.
    void TakeUnloaded(TArray<FIntPoint>& Out);

    const FVoxelStreamingRings& GetRings() const { return Rings; }
    // A column's final blocks, or null when they are not loaded.
    const FVoxelColumn* FindColumn(FIntPoint Column) const;
    bool IsGenerated(FIntPoint Column) const { return FindColumn(Column) != nullptr; }
    void ForEachColumn(TFunctionRef<void(FIntPoint, const FVoxelColumn&)> Visit) const;
    // Tests: a column's snapshot, or null when it is not held.
    const FVoxelColumn* FindSnapshot(FIntPoint Column) const;

    // A world cell's state and the stamp of its last edit (0: never edited). False when its column is not final.
    bool GetCell(const FIntVector& Cell, uint16& OutState, int64& OutStamp) const;
    // Sets a cell of a final column, keeping Stamp with the edit (the block world's revision). False, changing
    // nothing, when the column is not final or the cell lies outside the world's height.
    bool SetCell(const FIntVector& Cell, uint16 State, int64 Stamp);
    // One cell for SetCells.
    struct FCellWrite
    {
        FIntVector Cell = FIntVector::ZeroValue;
        uint16 State = 0;
        int64 Stamp = 0;
    };
    // SetCell for many cells of one column at once (a fluid tick's writes, water and sky W2a): one copy of the column,
    // one heightmap pass, and the union of the sections they touch marked dirty once per column. The same column, edits
    // and dirty sections as SetCell for each in turn. Cells outside Column or the world's height are skipped; false,
    // changing nothing, when the column is not final.
    bool SetCells(FIntPoint Column, TConstArrayView<FCellWrite> Cells);
    // Forgets every edit; loaded columns that had one are built again.
    void DropEdits();
    // Every non-air cell of the final columns inside an inclusive range, with its edit stamp.
    void ForEachNonAir(
        const FIntVector& Min, const FIntVector& Max, TFunctionRef<void(const FIntVector&, uint16, int64)> Visit) const;
    int64 CountNonAir() const;
    SIZE_T GetEditBytes() const;
    FVoxelColumnStatus GetStatus(FIntPoint Column) const;
    int32 GetJobCount() const { return Jobs.Num(); }
    // Every needed column has reached its ring's stage, every column in view is meshed and every finished mesh has been
    // taken.
    bool IsViewOnScreen() const;
    // Every column in view has its current full mesh on screen (the far ring may still be loading).
    bool IsNearViewOnScreen() const;
    // Nothing in flight and nothing left to launch or take.
    bool IsIdle() const;
    FVoxelPipelineStats GetStats() const;

private:
    struct FRecord
    {
        // New every time the column loads, so a job's result can tell whether its column is still the one it started
        // for.
        uint64 Serial = 0;
        // Set when the column unloads: its jobs that have not started skip their work.
        TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> Cancel;
        // The final column: decorated, finished and edited (without decoration, generated and edited).
        TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe> Data;
        SIZE_T DataBytes = 0;
        // Decoration's stages: the snapshot, and the buckets this column's Decorate aimed at each column of its 3 x 3
        // (null for an empty bucket), with a bit per bucket held.
        TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe> Snapshot;
        SIZE_T SnapshotBytes = 0;
        TSharedPtr<const FVoxelWriteBucket, ESPMode::ThreadSafe> Buckets[9];
        SIZE_T BucketBytes[9] = {};
        uint16 HeldBuckets = 0;
        bool bDecorated = false;
        bool bDecorating = false;
        bool bFinalizing = false;
        // The column has had a snapshot or final blocks since it loaded, so a new Generate is a regeneration.
        bool bHadSnapshot = false;
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
        // The kind of the latest accepted mesh, and of the mesh on screen: a column whose wanted kind changes keeps
        // showing what it shows until the other kind's mesh is taken.
        EVoxelMeshKind MeshedKind = EVoxelMeshKind::None;
        EVoxelMeshKind ShownKind = EVoxelMeshKind::None;
        TUniquePtr<FVoxelColumnMesh> Ready;
        // Section meshes kept near the center, so an edit remeshes only what it touched; empty elsewhere.
        TArray<TSharedPtr<const FVoxelMeshData, ESPMode::ThreadSafe>> Cache;
        SIZE_T CacheBytes = 0;
        int32 ScreenQuads = 0;
        int32 ScreenCards = 0;
    };
    enum class EJob : uint8
    {
        Generate,
        Decorate,
        Finalize,
        Mesh,
        FarMesh
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
    void CollectGenerate(const FJob& Job, FJobResult& Result, FRecord& Record);
    void CollectDecorate(const FJob& Job, FJobResult& Result, FRecord& Record);
    void CollectFinalize(const FJob& Job, FJobResult& Result, FRecord& Record);
    void CollectMesh(const FJob& Job, FJobResult& Result, FRecord& Record);
    void LaunchJobs();
    bool HasRoom() const { return Jobs.Num() < Settings.MaxTasksInFlight; }
    // Far mesh jobs take at most a third of the slots (at least one).
    bool HasFarRoom() const { return FarJobs < FMath::Max(1, Settings.MaxTasksInFlight / 3); }
    // A drawn column's wanted mesh: full in view, far beyond it.
    EVoxelMeshKind GetWantedKind(FIntPoint Column) const
    {
        return Rings.IsInView(Column) ? EVoxelMeshKind::Full : EVoxelMeshKind::Far;
    }
    // Each returns whether the column has the stage, launching what it misses (room permitting) otherwise.
    bool RequireSnapshot(FIntPoint Column);
    bool RequireDecorated(FIntPoint Column);
    bool RequireBucket(FIntPoint Source, FIntPoint Target);
    bool RequireFinal(FIntPoint Column);
    void TryMesh(FIntPoint Column, FRecord& Record);
    FJob& AddJob(EJob Type, FIntPoint Column, const FRecord& Record);
    void StartJob(FJob& Job, TUniqueFunction<TSharedPtr<FJobResult>()>&& Work);
    void LaunchGenerate(FIntPoint Column, FRecord& Record);
    void LaunchDecorate(FIntPoint Column, FRecord& Record);
    void LaunchFinalize(FIntPoint Column, FRecord& Record);
    void LaunchMesh(FIntPoint Column, FRecord& Record, uint8 Neighbors);
    void LaunchFarMesh(FIntPoint Column, FRecord& Record, uint8 Neighbors);
    // The column's edits as (dense index, state), replayed over its final blocks.
    TArray<TPair<int32, uint16>> GetReplay(FIntPoint Column) const;
    // Frees a final column's snapshot once its 8 neighbours have decorated.
    void TryFreeSnapshot(FIntPoint Column);
    void FreeBucket(FRecord& Record, int32 Bucket);
    // A stage was lost (a miss, an eviction, an edit dropped): the walk starts again from the first needed column.
    void Regress();
    // The neighbours a column's mesh reads, a bit per neighbour in FVoxelNeighborhood order (skipping the center):
    // all eight, or with edge walls only those drawn.
    uint8 GetMeshNeighbors(FIntPoint Column) const;
    bool AreFinal(FIntPoint Column, uint8 Neighbors) const;
    bool IsComplete(int32 NeededIndex) const;
    void MarkDirty(FRecord& Record, uint32 Sections);
    void SortReady();

    TSharedRef<const FVoxelBlockTable> Table;
    TSharedRef<const IVoxelTerrain> Terrain;
    TSharedRef<const FVoxelFarClasses> FarClasses;
    FSettings Settings;
    TSharedPtr<const FVoxelLightTable> LightTable;

    FVoxelStreamingRings Rings;
    int32 ViewSector = 0;
    bool bPlanned = false;
    // The needed columns in priority order, and the ring of each.
    TArray<FIntPoint> Needed;
    TArray<uint8> NeededRing;
    // Every needed column before this index has reached its ring's stage.
    int32 FirstIncomplete = 0;
    // Something changed since the last walk that ended with room to spare, so the next Update walks again.
    bool bWalk = true;
    // Set by Regress, so a walk that lost a stage behind its position walks again.
    bool bRegressed = false;

    TMap<FIntPoint, FRecord> Records;
    // Per column, by dense index (FVoxelColumn::GetDenseIndex), every cell edited this session.
    TMap<FIntPoint, TMap<int32, FEdit>> Edits;
    uint64 NextSerial = 0;
    TArray<FJob> Jobs;
    // Columns with a finished full mesh, and with a finished far mesh, nearest the center first once sorted.
    TArray<FIntPoint> ReadyColumns;
    TArray<FIntPoint> ReadyFarColumns;
    int32 FarJobs = 0;
    bool bReadySorted = true;
    TArray<FIntPoint> UnloadedOnScreen;
    // Some column was meshed against air at the edge, so later plans check which edges moved.
    bool bEdgeWallsUsed = false;

    int64 Dropped = 0, Stale = 0;
    int64 Regenerated = 0, Redecorated = 0, BucketsDropped = 0, BucketsAppliedToFinal = 0, Cancelled = 0;
    FVoxelStageTimes GenerateTimes, DecorateTimes, FinalizeTimes, MeshTimes, StreamsTimes, FarMeshTimes;
};
