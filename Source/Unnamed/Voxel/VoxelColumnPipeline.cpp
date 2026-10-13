#include "Voxel/VoxelColumnPipeline.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelWriteLog.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelTerrain.h"
#include "Voxel/VoxelChunkComponent.h"
#include "VoxelConstants.h"

namespace
{
    constexpr uint32 PipelineAllSections = (1u << VoxelConstants::SectionCount) - 1;
    constexpr uint8 PipelineAllNeighbors = 0xFF;
    // The eight neighbours in FVoxelNeighborhood's row order, skipping the center: bit i of a neighbour mask.
    const FIntPoint PipelineNeighborOffsets[8] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};

    // The offset of the column a bucket index (FVoxelWriteLog::GetBucket) names, from the source.
    FIntPoint GetPipelineBucketOffset(int32 Bucket)
    {
        return FIntPoint(Bucket % 3 - 1, Bucket / 3 - 1);
    }

    // The bucket a source at Source aimed at Target, one of its 3 x 3.
    int32 GetPipelineBucket(FIntPoint Source, FIntPoint Target)
    {
        return FVoxelWriteLog::GetBucket(Target.X - Source.X, Target.Y - Source.Y);
    }

    int64 GetPipelineDistanceSquared(FIntPoint A, FIntPoint B)
    {
        const int64 DX = int64(A.X) - B.X, DY = int64(A.Y) - B.Y;
        return DX * DX + DY * DY;
    }

    bool IsPipelineNear(FIntPoint Column, FIntPoint Center, int32 Radius)
    {
        return FMath::Abs(int64(Column.X) - Center.X) <= Radius && FMath::Abs(int64(Column.Y) - Center.Y) <= Radius;
    }

    // The sections a mesh's geometry spans (positions' world Z), for the component's bounds.
    uint32 GetPipelineSectionMask(const FVoxelMeshData& Data)
    {
        float Low = TNumericLimits<float>::Max(), High = TNumericLimits<float>::Lowest();
        for (const FVoxelMeshLayer& Layer : Data.Layers)
            for (const FVector3f& Position : Layer.Positions)
            {
                Low = FMath::Min(Low, Position.Z);
                High = FMath::Max(High, Position.Z);
            }
        if (Low > High) return 0;
        const int32 First =
            FMath::Clamp(FMath::FloorToInt32((Low - VoxelConstants::MinZ) / VoxelConstants::SectionSize), 0,
                VoxelConstants::SectionCount - 1);
        const int32 Last =
            FMath::Clamp(FMath::CeilToInt32((High - VoxelConstants::MinZ) / VoxelConstants::SectionSize) - 1, First,
                VoxelConstants::SectionCount - 1);
        uint32 Mask = 0;
        for (int32 Section = First; Section <= Last; ++Section)
            Mask |= 1u << Section;
        return Mask;
    }

    void ReplayPipelineEdits(
        FVoxelColumn& Column, TConstArrayView<TPair<int32, uint16>> Replay, const FVoxelBlockTable& Table)
    {
        // One heightmap pass for the lot: a flooded column holds many edits (water and sky W2a).
        Column.SetStates(Replay, Table);
    }
}

struct FVoxelColumnPipeline::FJobResult
{
    TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe> Column;
    TUniquePtr<FVoxelWriteLog> Log;
    TUniquePtr<FVoxelColumnMesh> Mesh;
    double Ms = 0;
    // The column unloaded before the job started, so it did nothing.
    bool bCancelled = false;
};

struct FVoxelColumnPipeline::FJob
{
    EJob Type = EJob::Generate;
    FIntPoint Column = FIntPoint::ZeroValue;
    uint64 Serial = 0;
    uint32 MeshVersion = 0;
    uint8 Neighbors = 0;
    UE::Tasks::TTask<TSharedPtr<FJobResult>> Task;
    // Manual jobs (tests): the work until it runs, then its result.
    TUniqueFunction<TSharedPtr<FJobResult>()> Work;
    TSharedPtr<FJobResult> Result;

    bool IsCompleted() const { return Work ? false : Result.IsValid() || Task.IsCompleted(); }
    void Run()
    {
        if (!Work) return;
        Result = Work();
        Work = TUniqueFunction<TSharedPtr<FJobResult>()>();
    }
    TSharedPtr<FJobResult> GetResult() { return Result.IsValid() ? Result : Task.GetResult(); }
};

FVoxelColumnPipeline::FVoxelColumnPipeline(
    TSharedRef<const FVoxelBlockTable> InTable, TSharedRef<const IVoxelTerrain> InTerrain, const FSettings& InSettings)
    : Table(MoveTemp(InTable)),
      Terrain(MoveTemp(InTerrain)),
      FarClasses(MakeShared<const FVoxelFarClasses>(*Table)),
      Settings(InSettings)
{
    Settings.MaxTasksInFlight = FMath::Max(1, Settings.MaxTasksInFlight);
}

FVoxelColumnPipeline::~FVoxelColumnPipeline()
{
    WaitForJobs();
}

void FVoxelColumnPipeline::SetMaxTasksInFlight(int32 MaxTasks)
{
    MaxTasks = FMath::Max(1, MaxTasks);
    if (MaxTasks > Settings.MaxTasksInFlight) bWalk = true;
    Settings.MaxTasksInFlight = MaxTasks;
}

void FVoxelColumnPipeline::SetEdgeWalls(bool bEdgeWalls)
{
    if (Settings.bEdgeWalls == bEdgeWalls) return;
    Settings.bEdgeWalls = bEdgeWalls;
    // The next SetFocus plans again and remeshes the columns whose edges changed.
    bPlanned = false;
}

void FVoxelColumnPipeline::SetTintBlend(int32 TintBlend)
{
    if (Settings.MeshOptions.TintBlend == TintBlend) return;
    Settings.MeshOptions.TintBlend = TintBlend;
    // Every column's colours change. Jobs already running are stale: their meshes show until their columns mesh again.
    for (TPair<FIntPoint, FRecord>& Pair : Records)
        if (Pair.Value.Data) MarkDirty(Pair.Value, PipelineAllSections);
    Regress();
}

void FVoxelColumnPipeline::SetModelOptions(bool bModels, bool bShadeUnturned)
{
    FVoxelMeshOptions& Options = Settings.MeshOptions;
    if (Options.bModels == bModels && Options.bModelShadeUnturned == bShadeUnturned) return;
    Options.bModels = bModels;
    Options.bModelShadeUnturned = bShadeUnturned;
    for (TPair<FIntPoint, FRecord>& Pair : Records)
        if (Pair.Value.Data) MarkDirty(Pair.Value, PipelineAllSections);
    Regress();
}

void FVoxelColumnPipeline::SetFarOptions(const VoxelFarMesher::FOptions& FarOptions)
{
    if (Settings.FarOptions.CanopyLayer == FarOptions.CanopyLayer) return;
    Settings.FarOptions = FarOptions;
    for (TPair<FIntPoint, FRecord>& Pair : Records)
        if (Pair.Value.Data && (Pair.Value.MeshedKind == EVoxelMeshKind::Far || Rings.IsFar(Pair.Key)))
            MarkDirty(Pair.Value, PipelineAllSections);
    Regress();
}

void FVoxelColumnPipeline::SetFarInnerFirst(bool bInnerFirst)
{
    if (Settings.bFarInnerFirst == bInnerFirst) return;
    Settings.bFarInnerFirst = bInnerFirst;
    bReadySorted = false;
}

void FVoxelColumnPipeline::SetOreLooks(int32 OreHost, int32 OreGlint)
{
    FVoxelMeshOptions& Options = Settings.MeshOptions;
    if (Options.OreHost == OreHost && Options.OreGlint == OreGlint) return;
    Options.OreHost = OreHost;
    Options.OreGlint = OreGlint;
    for (TPair<FIntPoint, FRecord>& Pair : Records)
        if (Pair.Value.Data) MarkDirty(Pair.Value, PipelineAllSections);
    Regress();
}

void FVoxelColumnPipeline::SetSlopedTops(int32 SlopedTops, bool bOpenCorners)
{
    FVoxelMeshOptions& Options = Settings.MeshOptions;
    if (Options.SlopedTops == SlopedTops && Options.bOpenCorners == bOpenCorners) return;
    Options.SlopedTops = SlopedTops;
    Options.bOpenCorners = bOpenCorners;
    for (TPair<FIntPoint, FRecord>& Pair : Records)
        if (Pair.Value.Data) MarkDirty(Pair.Value, PipelineAllSections);
    Regress();
}

void FVoxelColumnPipeline::WaitForJobs()
{
    for (FJob& Job : Jobs)
        if (Job.Work)
            Job.Run();
        else if (!Job.Result)
            Job.Task.Wait();
}

int32 FVoxelColumnPipeline::GetPendingJobCount() const
{
    int32 Count = 0;
    for (const FJob& Job : Jobs)
        Count += bool(Job.Work);
    return Count;
}

bool FVoxelColumnPipeline::RunJob(int32 Index)
{
    if (!Jobs.IsValidIndex(Index) || !Jobs[Index].Work) return false;
    Jobs[Index].Run();
    return true;
}

FIntPoint FVoxelColumnPipeline::GetJobColumn(int32 Index) const
{
    return Jobs.IsValidIndex(Index) ? Jobs[Index].Column : FIntPoint::ZeroValue;
}

bool FVoxelColumnPipeline::IsJobPending(int32 Index) const
{
    return Jobs.IsValidIndex(Index) && bool(Jobs[Index].Work);
}

FVoxelColumnPipeline::FRecord& FVoxelColumnPipeline::AddRecord(FIntPoint Column)
{
    FRecord& Record = Records.Add(Column);
    Record.Serial = ++NextSerial;
    Record.Cancel = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
    return Record;
}

void FVoxelColumnPipeline::SetFocus(const FVoxelStreamingRings& InRings, int32 NewViewSector)
{
    // With decoration each stage needs the 3 x 3 of the one before it: three rings past the view.
    const FVoxelStreamingRings NewRings(InRings.Center, InRings.ViewRadius, InRings.UnloadMargin,
        IsDecorating() ? FVoxelStreamingRings::MaxStages : 1, InRings.FarRadius);
    if (bPlanned && NewRings == Rings && NewViewSector == ViewSector) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_Plan);
    const bool bCenterMoved = !bPlanned || NewRings.Center != Rings.Center;
    // Far LOD step 1b: a move of the center or the sector alone keeps the rings' shape, so the plan shifts a cached
    // order and visits only the columns that crossed a border, with the same result as the full path.
    const bool bShift = bPlanned && !Settings.bFullReplan && NewRings.ViewRadius == Rings.ViewRadius &&
        NewRings.FarRadius == Rings.FarRadius && NewRings.Stages == Rings.Stages &&
        NewRings.UnloadMargin == Rings.UnloadMargin;
    const FVoxelStreamingRings OldRings = Rings;
    const int32 OldSector = ViewSector, OldFirstIncomplete = FirstIncomplete;
    Rings = NewRings;
    ViewSector = NewViewSector;
    ViewAhead = VoxelStreaming::GetViewSectorDirection(ViewSector);
    bPlanned = true;
    if (Settings.bFullReplan)
    {
        VoxelStreaming::GetNeededColumns(Rings, ViewSector, Needed);
        NeededRing.SetNumUninitialized(Needed.Num());
        for (int32 Index = 0; Index < Needed.Num(); ++Index)
            NeededRing[Index] = uint8(Rings.GetRing(Needed[Index]));
    }
    else
    {
        const FVoxelNeededOrder& Order = GetOrder(Rings, ViewSector);
        Order.GetColumns(Rings.Center, Needed);
        NeededRing = Order.OffsetRings;
    }
    if (!bShift)
    {
        for (const FIntPoint Column : Needed)
            if (!Records.Contains(Column)) AddRecord(Column);
        TArray<FIntPoint> Present, Unloads;
        Records.GenerateKeyArray(Present);
        VoxelStreaming::GetUnloads(Rings, Present, Unloads);
        for (const FIntPoint Column : Unloads)
            Unload(Column);
    }
    else
    {
        // Every column the old rings needed has its record, and every record lies within the old unload distance: only
        // newly needed columns get one, and only the band the move left beyond the unload distance unloads (in
        // GetUnloads' order).
        ++ShiftedPlans;
        const FVoxelNeededOrder& OldOrder = GetOrder(OldRings, OldSector);
        for (const FIntPoint Column : Needed)
            if (OldOrder.FindIndex(Column - OldRings.Center) == INDEX_NONE && !Records.Contains(Column))
                AddRecord(Column);
        if (bCenterMoved)
            VoxelStreaming::ForEachLeft(
                OldRings.Center, Rings.Center, Rings.GetUnloadDistance(), [this](FIntPoint Column) { Unload(Column); });
    }
    // Far LOD: a waiting mesh of the kind its drawn column no longer wants goes, as CollectMesh drops a late one, while
    // the column shows something; the wanted kind meshes again. MeshedKind goes to None, not the dropped kind: should
    // the column want that kind again before its next mesh (a quick move back), it must not count as meshed while it
    // shows the other kind.
    for (TArray<FIntPoint>* Queue : {&ReadyColumns, &ReadyFarColumns})
        for (int32 Index = Queue->Num() - 1; Index >= 0; --Index)
        {
            const FIntPoint Column = (*Queue)[Index];
            FRecord& Record = Records.FindChecked(Column);
            const EVoxelMeshKind Kind = Record.Ready->bFar ? EVoxelMeshKind::Far : EVoxelMeshKind::Full;
            if (!Record.bOnScreen || !Rings.IsDrawn(Column) || Kind == GetWantedKind(Column)) continue;
            Record.Ready.Reset();
            Record.MeshedKind = EVoxelMeshKind::None;
            Queue->RemoveAtSwap(Index);
            ++Dropped;
        }
    // Caches live only near the center, where the player can edit; one column of slack avoids churn at the border.
    const int32 CacheReach = Settings.MeshCacheRadius + 1;
    const auto DropFarCache = [this, CacheReach](FIntPoint Column, FRecord& Record)
    {
        if (Record.Cache.IsEmpty() || IsPipelineNear(Column, Rings.Center, CacheReach)) return;
        Record.Cache.Reset();
        Record.CacheBytes = 0;
    };
    if (bCenterMoved && !bShift)
        for (TPair<FIntPoint, FRecord>& Pair : Records)
            DropFarCache(Pair.Key, Pair.Value);
    else if (bCenterMoved)
        // Every cache lies within CacheReach of the old center: CollectMesh keeps one only near the center, and every
        // move drops those beyond it.
        for (int32 DY = -CacheReach; DY <= CacheReach; ++DY)
            for (int32 DX = -CacheReach; DX <= CacheReach; ++DX)
            {
                const FIntPoint Column = OldRings.Center + FIntPoint(DX, DY);
                if (FRecord* Record = Records.Find(Column)) DropFarCache(Column, *Record);
            }
    // With edge walls, a column whose neighbours moved in or out of view meshes again.
    if (Settings.bEdgeWalls || bEdgeWallsUsed)
        for (int32 Index = 0; Index < Needed.Num(); ++Index)
        {
            FRecord& Record = Records.FindChecked(Needed[Index]);
            if (NeededRing[Index] != 0 || !Record.Data || (Record.Dirty && !Record.bMeshing)) continue;
            const uint8 Current = Record.bMeshing ? Record.MeshingNeighbors : Record.MeshedNeighbors;
            if (Current != GetMeshNeighbors(Needed[Index])) MarkDirty(Record, PipelineAllSections);
        }
    if (bShift && !Settings.bEdgeWalls && !bEdgeWallsUsed)
        CarryProgress(OldRings, OldSector, OldFirstIncomplete);
    else
        Regress();
    bReadySorted = false;
}

const FVoxelNeededOrder& FVoxelColumnPipeline::GetOrder(const FVoxelStreamingRings& ForRings, int32 Sector)
{
    const int32 Slot = ((Sector % VoxelStreaming::ViewSectorCount) + VoxelStreaming::ViewSectorCount) %
        VoxelStreaming::ViewSectorCount;
    FVoxelNeededOrder& Order = Orders[Slot];
    if (!Order.Matches(ForRings, Slot)) Order = FVoxelNeededOrder(ForRings, Slot);
    return Order;
}

void FVoxelColumnPipeline::CarryProgress(
    const FVoxelStreamingRings& OldRings, int32 OldSector, int32 OldFirstIncomplete)
{
    // Every column before OldFirstIncomplete had reached its ring's stage. SetFocus changed no record's stage, so a
    // column that keeps its ring (and, in view or far, its wanted mesh) is still complete: the walk skips the longest
    // run of those at the head of the new order. Complete columns launch nothing, so the jobs are the full path's.
    const FVoxelNeededOrder& OldOrder = GetOrder(OldRings, OldSector);
    int32 Carried = 0;
    for (; Carried < Needed.Num(); ++Carried)
    {
        const FIntPoint Column = Needed[Carried];
        const int32 OldIndex = OldOrder.FindIndex(Column - OldRings.Center);
        if (OldIndex == INDEX_NONE || OldIndex >= OldFirstIncomplete ||
            OldOrder.OffsetRings[OldIndex] != NeededRing[Carried])
            break;
        if (NeededRing[Carried] == 0 && OldRings.IsInView(Column) != Rings.IsInView(Column)) break;
    }
    FirstIncomplete = Carried;
    CarriedColumns += Carried;
    bWalk = bRegressed = true;
}

void FVoxelColumnPipeline::Unload(FIntPoint Column)
{
    const FRecord* Record = Records.Find(Column);
    if (!Record) return;
    // Its jobs that have not started skip their work; the others' results are dropped when they arrive.
    *Record->Cancel = true;
    if (Record->Ready) (Record->Ready->bFar ? ReadyFarColumns : ReadyColumns).RemoveSingle(Column);
    if (Record->bOnScreen) UnloadedOnScreen.Add(Column);
    Records.Remove(Column);
}

void FVoxelColumnPipeline::Regress()
{
    FirstIncomplete = 0;
    bWalk = bRegressed = true;
}

void FVoxelColumnPipeline::Update()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_Update);
    Collect();
    if (bPlanned) LaunchJobs();
}

void FVoxelColumnPipeline::Collect()
{
    for (int32 Index = 0; Index < Jobs.Num();)
    {
        FJob& Job = Jobs[Index];
        if (!Job.IsCompleted())
        {
            ++Index;
            continue;
        }
        const TSharedPtr<FJobResult> Result = Job.GetResult();
        FRecord* Record = Records.Find(Job.Column);
        const bool bCurrent = Record && Record->Serial == Job.Serial;
        if (Result->bCancelled)
            ++Cancelled;
        else
            switch (Job.Type)
            {
            case EJob::Generate:
                GenerateTimes.Add(Result->Ms);
                break;
            case EJob::Decorate:
                DecorateTimes.Add(Result->Ms);
                break;
            case EJob::Finalize:
                FinalizeTimes.Add(Result->Ms);
                break;
            case EJob::Mesh:
                MeshTimes.Add(Result->Mesh->MeshMs);
                StreamsTimes.Add(Result->Mesh->StreamsMs);
                break;
            case EJob::FarMesh:
                FarMeshTimes.Add(Result->Mesh->MeshMs + Result->Mesh->StreamsMs);
                break;
            }
        if (!bCurrent)
            ++Dropped;
        else
            switch (Job.Type)
            {
            case EJob::Generate:
                CollectGenerate(Job, *Result, *Record);
                break;
            case EJob::Decorate:
                CollectDecorate(Job, *Result, *Record);
                break;
            case EJob::Finalize:
                CollectFinalize(Job, *Result, *Record);
                break;
            case EJob::Mesh:
            case EJob::FarMesh:
                CollectMesh(Job, *Result, *Record);
                break;
            }
        if (Job.Type == EJob::FarMesh) --FarJobs;
        Jobs.RemoveAtSwap(Index);
        bWalk = true;
    }
}

void FVoxelColumnPipeline::CollectGenerate(const FJob& Job, FJobResult& Result, FRecord& Record)
{
    Record.bGenerating = false;
    Record.bHadSnapshot = true;
    if (!IsDecorating())
    {
        Record.Data = Result.Column;
        Record.DataBytes = Result.Column->GetAllocatedSize();
        MarkDirty(Record, PipelineAllSections);
        return;
    }
    Record.Snapshot = Result.Column;
    Record.SnapshotBytes = Result.Column->GetAllocatedSize();
}

void FVoxelColumnPipeline::CollectDecorate(const FJob& Job, FJobResult& Result, FRecord& Record)
{
    Record.bDecorating = false;
    Record.bDecorated = true;
    for (int32 Bucket = 0; Bucket < 9; ++Bucket)
    {
        const FIntPoint Target = Job.Column + GetPipelineBucketOffset(Bucket);
        FVoxelWriteBucket& Writes = Result.Log->Buckets[Bucket];
        FRecord* TargetRecord = Records.Find(Target);
        if (TargetRecord && TargetRecord->Data)
        {
            // Never applied to a final column: its blocks already hold these writes, and the player's edits since.
            if (Settings.Mutation == EVoxelPipelineMutation::ApplyToFinal && !Writes.IsEmpty())
            {
                const FVoxelWriteBucket* Incoming[9] = {};
                Incoming[GetPipelineBucket(Target, Job.Column)] = &Writes;
                TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe> Applied =
                    MakeShared<FVoxelColumn, ESPMode::ThreadSafe>(Target);
                Settings.Decorator->Finalize(*TargetRecord->Data, Incoming, *Applied);
                TargetRecord->Data = Applied;
                TargetRecord->DataBytes = Applied->GetAllocatedSize();
                MarkDirty(*TargetRecord, PipelineAllSections);
                ++BucketsAppliedToFinal;
            }
            else
                ++BucketsDropped;
            continue;
        }
        FreeBucket(Record, Bucket);
        if (!Writes.IsEmpty())
        {
            Record.Buckets[Bucket] = MakeShared<FVoxelWriteBucket, ESPMode::ThreadSafe>(MoveTemp(Writes));
            Record.BucketBytes[Bucket] = Record.Buckets[Bucket]->GetAllocatedSize();
        }
        Record.HeldBuckets |= uint16(1u << Bucket);
    }
    // The final columns of its 3 x 3 may need their snapshots no longer.
    for (int32 Bucket = 0; Bucket < 9; ++Bucket)
        TryFreeSnapshot(Job.Column + GetPipelineBucketOffset(Bucket));
}

void FVoxelColumnPipeline::CollectFinalize(const FJob& Job, FJobResult& Result, FRecord& Record)
{
    Record.bFinalizing = false;
    Record.bHadSnapshot = true;
    Record.Data = Result.Column;
    Record.DataBytes = Result.Column->GetAllocatedSize();
    MarkDirty(Record, PipelineAllSections);
    // Every bucket aimed at it has been applied: free them.
    for (int32 Bucket = 0; Bucket < 9; ++Bucket)
    {
        const FIntPoint Source = Job.Column + GetPipelineBucketOffset(Bucket);
        if (FRecord* SourceRecord = Records.Find(Source))
            FreeBucket(*SourceRecord, GetPipelineBucket(Source, Job.Column));
    }
    for (int32 Bucket = 0; Bucket < 9; ++Bucket)
        TryFreeSnapshot(Job.Column + GetPipelineBucketOffset(Bucket));
}

void FVoxelColumnPipeline::CollectMesh(const FJob& Job, FJobResult& Result, FRecord& Record)
{
    Record.bMeshing = false;
    const bool bFar = Job.Type == EJob::FarMesh;
    // Far LOD: a drawn column that changed kinds back while the job ran keeps what it shows; the mesh of the kind it
    // no longer wants is thrown away (a column showing nothing takes it, rather than stay empty).
    if (Record.bOnScreen && Rings.IsDrawn(Job.Column) &&
        (bFar ? EVoxelMeshKind::Far : EVoxelMeshKind::Full) != GetWantedKind(Job.Column))
    {
        ++Dropped;
        return;
    }
    // Marked dirty again while the job ran (water and sky W2a): its mesh is still the newest there is, so it goes to
    // the screen, or a column edited every few frames (flowing water) would show none. The column stays dirty and
    // meshes again. Otherwise nothing became dirty since the job started, so it meshed every dirty section.
    if (Record.MeshVersion != Job.MeshVersion)
        ++Stale;
    else
        Record.Dirty = 0;
    Record.MeshedNeighbors = Job.Neighbors;
    Record.MeshedKind = bFar ? EVoxelMeshKind::Far : EVoxelMeshKind::Full;
    // A far mesh drops the section cache: it took the dirty bits the cache would need.
    if (!bFar && IsPipelineNear(Job.Column, Rings.Center, Settings.MeshCacheRadius))
    {
        Record.Cache = Result.Mesh->Sections;
        Record.CacheBytes = Result.Mesh->SectionBytes;
    }
    else
    {
        Record.Cache.Reset();
        Record.CacheBytes = 0;
    }
    // A waiting mesh of the other kind gives its place in the queues to this one.
    if (Record.Ready && Record.Ready->bFar != bFar)
    {
        (Record.Ready->bFar ? ReadyFarColumns : ReadyColumns).RemoveSingle(Job.Column);
        Record.Ready.Reset();
    }
    if (!Record.Ready)
    {
        (bFar ? ReadyFarColumns : ReadyColumns).Add(Job.Column);
        bReadySorted = false;
    }
    Record.Ready = MoveTemp(Result.Mesh);
}

void FVoxelColumnPipeline::FreeBucket(FRecord& Record, int32 Bucket)
{
    Record.Buckets[Bucket].Reset();
    Record.BucketBytes[Bucket] = 0;
    Record.HeldBuckets &= uint16(~(1u << Bucket));
}

void FVoxelColumnPipeline::TryFreeSnapshot(FIntPoint Column)
{
    // Kept while a stage may read it: until the column is final and it and its 8 neighbours have decorated. Its
    // neighbours are its sources, so at rest every final column has freed its snapshot; one regenerated for a
    // neighbour that reloaded stays until that neighbour decorates.
    FRecord* Record = Records.Find(Column);
    if (!Record || !Record->Snapshot || !Record->Data || !Record->bDecorated ||
        Settings.Mutation == EVoxelPipelineMutation::KeepSnapshots)
        return;
    for (const FIntPoint Offset : PipelineNeighborOffsets)
    {
        const FRecord* Neighbor = Records.Find(Column + Offset);
        if (!Neighbor || !Neighbor->bDecorated) return;
    }
    Record->Snapshot.Reset();
    Record->SnapshotBytes = 0;
}

void FVoxelColumnPipeline::EvictSnapshot(FIntPoint Column)
{
    if (FRecord* Record = Records.Find(Column); Record && Record->Snapshot)
    {
        Record->Snapshot.Reset();
        Record->SnapshotBytes = 0;
        Regress();
    }
}

void FVoxelColumnPipeline::EvictBuckets(FIntPoint Column)
{
    if (FRecord* Record = Records.Find(Column); Record && Record->HeldBuckets)
    {
        for (int32 Bucket = 0; Bucket < 9; ++Bucket)
            FreeBucket(*Record, Bucket);
        Regress();
    }
}

bool FVoxelColumnPipeline::IsComplete(int32 NeededIndex) const
{
    const FRecord& Record = Records.FindChecked(Needed[NeededIndex]);
    switch (IsDecorating() ? NeededRing[NeededIndex] : FMath::Min<int32>(NeededRing[NeededIndex], 1))
    {
    case 0:
        return Record.Data && !Record.Dirty && !Record.bMeshing &&
            Record.MeshedKind == GetWantedKind(Needed[NeededIndex]);
    case 1:
        return Record.Data.IsValid();
    case 2:
        return Record.bDecorated;
    default:
        return Record.Snapshot.IsValid() || Record.Data.IsValid();
    }
}

void FVoxelColumnPipeline::LaunchJobs()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_Launch);
    LastWalk = FWalk();
    // Nothing finished and nothing moved since a walk that had room to spare: nothing new can be ready.
    if (!bWalk) return;
    const double WalkStart = FPlatformTime::Seconds();
    const int32 WalkFrom = FirstIncomplete;
    bRegressed = false;
    while (FirstIncomplete < Needed.Num() && IsComplete(FirstIncomplete))
        ++FirstIncomplete;
    // Nearest first: each column's stage as soon as its inputs exist, otherwise what they miss.
    int32 Index = FirstIncomplete;
    for (; Index < Needed.Num() && HasRoom(); ++Index)
    {
        const FIntPoint Column = Needed[Index];
        if (!IsDecorating())
        {
            FRecord& Record = Records.FindChecked(Column);
            if (!Record.Data)
            {
                if (!Record.bGenerating) LaunchGenerate(Column, Record);
            }
            else if (NeededRing[Index] == 0)
                TryMesh(Column, Record);
            continue;
        }
        switch (NeededRing[Index])
        {
        case 0:
            if (RequireFinal(Column)) TryMesh(Column, Records.FindChecked(Column));
            break;
        case 1:
            RequireFinal(Column);
            break;
        case 2:
            RequireDecorated(Column);
            break;
        default:
            // A final column (one the rings moved away from) needs no snapshot of its own.
            if (!Records.FindChecked(Column).Data) RequireSnapshot(Column);
            break;
        }
    }
    // A walk stopped for room, or one that lost a stage behind it, walks again next time; otherwise the next result or
    // plan starts one.
    bWalk = !HasRoom() || bRegressed;
    LastWalk.Visited = Index - WalkFrom;
    LastWalk.Ms = (FPlatformTime::Seconds() - WalkStart) * 1000.;
}

bool FVoxelColumnPipeline::RequireSnapshot(FIntPoint Column)
{
    FRecord* Record = Records.Find(Column);
    if (!Record) return false;
    if (Record->Snapshot) return true;
    if (!Record->bGenerating && HasRoom()) LaunchGenerate(Column, *Record);
    return false;
}

bool FVoxelColumnPipeline::RequireDecorated(FIntPoint Column)
{
    FRecord* Record = Records.Find(Column);
    if (!Record) return false;
    if (Record->bDecorated) return true;
    if (Record->bDecorating) return false;
    // Every missing snapshot of the 3 x 3 starts now, not one per walk.
    bool bReady = true;
    for (int32 Bucket = 0; Bucket < 9; ++Bucket)
        bReady &= RequireSnapshot(Column + GetPipelineBucketOffset(Bucket));
    if (bReady && HasRoom()) LaunchDecorate(Column, *Record);
    return false;
}

bool FVoxelColumnPipeline::RequireBucket(FIntPoint Source, FIntPoint Target)
{
    FRecord* Record = Records.Find(Source);
    if (!Record) return false;
    if (Record->HeldBuckets & (1u << GetPipelineBucket(Source, Target))) return true;
    if (Record->bDecorated)
    {
        // Its bucket for Target is gone (applied to Target before it reloaded or lost its edits, dropped while Target
        // was final, or evicted): Decorate is pure, so it runs again.
        if (Settings.Mutation == EVoxelPipelineMutation::MissingBucketAsEmpty) return true;
        Record->bDecorated = false;
        ++Redecorated;
        Regress();
    }
    RequireDecorated(Source);
    return false;
}

bool FVoxelColumnPipeline::RequireFinal(FIntPoint Column)
{
    FRecord* Record = Records.Find(Column);
    if (!Record) return false;
    if (Record->Data) return true;
    if (Record->bFinalizing) return false;
    bool bReady = RequireSnapshot(Column);
    for (int32 Bucket = 0; Bucket < 9; ++Bucket)
        bReady &= RequireBucket(Column + GetPipelineBucketOffset(Bucket), Column);
    // RequireBucket may have added records' jobs, never records, so Record still stands.
    if (bReady && HasRoom()) LaunchFinalize(Column, *Record);
    return false;
}

void FVoxelColumnPipeline::TryMesh(FIntPoint Column, FRecord& Record)
{
    // A drawn column meshes when its sections changed or it wants the other kind of mesh (far LOD).
    const EVoxelMeshKind Wanted = GetWantedKind(Column);
    if (Record.bMeshing || (!Record.Dirty && Record.MeshedKind == Wanted)) return;
    if (Wanted == EVoxelMeshKind::Far && !HasFarRoom()) return;
    const uint8 Neighbors = GetMeshNeighbors(Column);
    if (IsDecorating())
    {
        // The neighbours' Finalizes first: the view waits on them.
        bool bReady = true;
        for (int32 Index = 0; Index < 8; ++Index)
            if (Neighbors & (1u << Index)) bReady &= RequireFinal(Column + PipelineNeighborOffsets[Index]);
        if (!bReady || !HasRoom()) return;
    }
    if (!AreFinal(Column, Neighbors)) return;
    if (Wanted == EVoxelMeshKind::Far)
        LaunchFarMesh(Column, Record, Neighbors);
    else
        LaunchMesh(Column, Record, Neighbors);
}

uint8 FVoxelColumnPipeline::GetMeshNeighbors(FIntPoint Column) const
{
    if (!Settings.bEdgeWalls) return PipelineAllNeighbors;
    uint8 Mask = 0;
    for (int32 Index = 0; Index < 8; ++Index)
        if (Rings.IsDrawn(Column + PipelineNeighborOffsets[Index])) Mask |= uint8(1u << Index);
    return Mask;
}

bool FVoxelColumnPipeline::AreFinal(FIntPoint Column, uint8 Neighbors) const
{
    for (int32 Index = 0; Index < 8; ++Index)
        if ((Neighbors & (1u << Index)) && !FindColumn(Column + PipelineNeighborOffsets[Index])) return false;
    return true;
}

void FVoxelColumnPipeline::MarkDirty(FRecord& Record, uint32 Sections)
{
    Record.Dirty |= Sections;
    ++Record.MeshVersion;
}

FVoxelColumnPipeline::FJob& FVoxelColumnPipeline::AddJob(EJob Type, FIntPoint Column, const FRecord& Record)
{
    FJob& Job = Jobs.AddDefaulted_GetRef();
    Job.Type = Type;
    Job.Column = Column;
    Job.Serial = Record.Serial;
    return Job;
}

void FVoxelColumnPipeline::StartJob(FJob& Job, TUniqueFunction<TSharedPtr<FJobResult>()>&& Work)
{
    if (Settings.bManualJobs)
        Job.Work = MoveTemp(Work);
    else
        Job.Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(Work), UE::Tasks::ETaskPriority::BackgroundHigh);
}

TArray<TPair<int32, uint16>> FVoxelColumnPipeline::GetReplay(FIntPoint Column) const
{
    TArray<TPair<int32, uint16>> Replay;
    if (const TMap<int32, FEdit>* ColumnEdits = Edits.Find(Column))
        for (const TPair<int32, FEdit>& Edit : *ColumnEdits)
            Replay.Add({Edit.Key, Edit.Value.State});
    return Replay;
}

void FVoxelColumnPipeline::LaunchGenerate(FIntPoint Column, FRecord& Record)
{
    Record.bGenerating = true;
    if (IsDecorating() && Record.bHadSnapshot) ++Regenerated;
    // Without decoration the column's edits are replayed over the terrain here: they survive unloading. With it, the
    // snapshot stays pure and Finalize replays them.
    TArray<TPair<int32, uint16>> Replay;
    if (!IsDecorating()) Replay = GetReplay(Column);
    FJob& Job = AddJob(EJob::Generate, Column, Record);
    StartJob(Job,
        [Terrain = Terrain, Table = Table, Decorator = Settings.Decorator, Cancel = Record.Cancel, Column,
            Replay = MoveTemp(Replay)]() -> TSharedPtr<FJobResult>
        {
            TSharedPtr<FJobResult> Result = MakeShared<FJobResult>();
            if (*Cancel)
            {
                Result->bCancelled = true;
                return Result;
            }
            TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_Generate);
            const double Start = FPlatformTime::Seconds();
            // The dense column is scratch, reused by every job on this worker.
            thread_local TArray<uint16> Dense;
            TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe> Built = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>(Column);
            if (Decorator)
                Decorator->GenerateSnapshot(*Terrain, Column, *Built, Dense);
            else
                Terrain->Generate(Column, *Built, Dense);
            ReplayPipelineEdits(*Built, Replay, *Table);
            Result->Column = Built;
            Result->Ms = (FPlatformTime::Seconds() - Start) * 1000.;
            return Result;
        });
}

void FVoxelColumnPipeline::LaunchDecorate(FIntPoint Column, FRecord& Record)
{
    Record.bDecorating = true;
    // The 3 x 3 snapshots, by bucket index. Shared pointers keep them alive should they be freed meanwhile.
    TStaticArray<TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>, 9> Around;
    for (int32 Bucket = 0; Bucket < 9; ++Bucket)
        Around[Bucket] = Records.FindChecked(Column + GetPipelineBucketOffset(Bucket)).Snapshot;
    FJob& Job = AddJob(EJob::Decorate, Column, Record);
    StartJob(Job,
        [Decorator = Settings.Decorator, Cancel = Record.Cancel, Column, Around]() -> TSharedPtr<FJobResult>
        {
            TSharedPtr<FJobResult> Result = MakeShared<FJobResult>();
            if (*Cancel)
            {
                Result->bCancelled = true;
                return Result;
            }
            TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_Decorate);
            const double Start = FPlatformTime::Seconds();
            const FVoxelColumn* Snapshots[9];
            for (int32 Bucket = 0; Bucket < 9; ++Bucket)
                Snapshots[Bucket] = Around[Bucket].Get();
            Result->Log = MakeUnique<FVoxelWriteLog>();
            Decorator->Decorate(Column, Snapshots, *Result->Log);
            Result->Ms = (FPlatformTime::Seconds() - Start) * 1000.;
            return Result;
        });
}

void FVoxelColumnPipeline::LaunchFinalize(FIntPoint Column, FRecord& Record)
{
    Record.bFinalizing = true;
    // The bucket each source of the 3 x 3 aimed at this column, by the source's bucket index from here (null: empty).
    TStaticArray<TSharedPtr<const FVoxelWriteBucket, ESPMode::ThreadSafe>, 9> Incoming;
    for (int32 Bucket = 0; Bucket < 9; ++Bucket)
    {
        const FIntPoint Source = Column + GetPipelineBucketOffset(Bucket);
        Incoming[Bucket] = Records.FindChecked(Source).Buckets[GetPipelineBucket(Source, Column)];
    }
    FJob& Job = AddJob(EJob::Finalize, Column, Record);
    StartJob(Job,
        [Terrain = Terrain, Table = Table, Decorator = Settings.Decorator, Cancel = Record.Cancel,
            Snapshot = Record.Snapshot, Incoming, Replay = GetReplay(Column)]() -> TSharedPtr<FJobResult>
        {
            TSharedPtr<FJobResult> Result = MakeShared<FJobResult>();
            if (*Cancel)
            {
                Result->bCancelled = true;
                return Result;
            }
            TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_Finalize);
            const double Start = FPlatformTime::Seconds();
            const FVoxelWriteBucket* Buckets[9];
            for (int32 Bucket = 0; Bucket < 9; ++Bucket)
                Buckets[Bucket] = Incoming[Bucket].Get();
            // The merge, the freeze pass, then the player's edits (plan 3.1).
            TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe> Final =
                MakeShared<FVoxelColumn, ESPMode::ThreadSafe>(Snapshot->GetPosition());
            Decorator->Finalize(*Snapshot, Buckets, *Final);
            Decorator->FinishColumn(*Terrain, *Final);
            ReplayPipelineEdits(*Final, Replay, *Table);
            Result->Column = Final;
            Result->Ms = (FPlatformTime::Seconds() - Start) * 1000.;
            return Result;
        });
}

void FVoxelColumnPipeline::LaunchMesh(FIntPoint Column, FRecord& Record, uint8 Neighbors)
{
    Record.bMeshing = true;
    Record.MeshingNeighbors = Neighbors;
    // Without a cache, or with one built from other neighbours (edge walls), every section meshes.
    const bool bUseCache = !Record.Cache.IsEmpty() && Record.MeshedNeighbors == Neighbors;
    const uint32 Sections = bUseCache ? Record.Dirty : PipelineAllSections;
    if (Neighbors != PipelineAllNeighbors) bEdgeWallsUsed = true;
    // The 3 x 3 columns row by row. Shared pointers keep them alive should they unload meanwhile.
    TStaticArray<TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>, 9> Around;
    Around[4] = Record.Data;
    for (int32 Index = 0; Index < 8; ++Index)
        if (Neighbors & (1u << Index))
        {
            const FIntPoint Offset = PipelineNeighborOffsets[Index];
            Around[(Offset.Y + 1) * 3 + Offset.X + 1] = Records.FindChecked(Column + Offset).Data;
        }
    TArray<TSharedPtr<const FVoxelMeshData, ESPMode::ThreadSafe>> Cached;
    if (bUseCache) Cached = Record.Cache;
    FJob& Job = AddJob(EJob::Mesh, Column, Record);
    Job.MeshVersion = Record.MeshVersion;
    Job.Neighbors = Neighbors;
    StartJob(Job,
        [Table = Table, Terrain = Terrain, LightTable = LightTable, Options = Settings.MeshOptions, Around,
            Cached = MoveTemp(Cached), Sections, Column]() -> TSharedPtr<FJobResult>
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_Mesh);
            const double Start = FPlatformTime::Seconds();
            TSharedPtr<FJobResult> Result = MakeShared<FJobResult>();
            Result->Mesh = MakeUnique<FVoxelColumnMesh>();
            FVoxelColumnMesh& Mesh = *Result->Mesh;
            FVoxelNeighborhood Neighborhood;
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                    Neighborhood.Set(DX, DY, Around[(DY + 1) * 3 + DX + 1].Get());
            const FVoxelColumn& Center = *Around[4];
            // The padded copy is scratch, reused by every job on this worker.
            thread_local TUniquePtr<FVoxelPaddedSection> Padded;
            if (!Padded) Padded = MakeUnique<FVoxelPaddedSection>();
            Mesh.Sections.SetNum(VoxelConstants::SectionCount);
            TArray<const FVoxelMeshData*, TInlineAllocator<VoxelConstants::SectionCount>> Present;
            for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
            {
                if (!(Sections & (1u << Section))) Mesh.Sections[Section] = Cached[Section];
                // An all-air section has no faces of its own; its neighbours draw the faces toward it.
                else if (!Center.GetSection(Section).IsEmpty())
                {
                    VoxelMesher::GatherPadded(Neighborhood, Section, *Padded);
                    TSharedPtr<FVoxelMeshData, ESPMode::ThreadSafe> Data =
                        MakeShared<FVoxelMeshData, ESPMode::ThreadSafe>();
                    VoxelMesher::MeshSection(
                        *Table, *Padded, Section, Options, *Data, Terrain->GetTints(), &Neighborhood);
                    ++Mesh.MeshedSections;
                    if (!Data->IsEmpty()) Mesh.Sections[Section] = Data;
                }
                if (const FVoxelMeshData* Data = Mesh.Sections[Section].Get())
                {
                    Present.Add(Data);
                    Mesh.SectionMask |= 1u << Section;
                    Mesh.QuadCount += Data->GetQuadCount();
                    for (const FVoxelMeshLayer& Layer : Data->Layers)
                        Mesh.SectionBytes += Layer.GetAllocatedSize();
                }
            }
            // The whole column every time, cached sections or not: the light pool replaces the column's sources.
            if (LightTable) VoxelLightSources::GatherSources(Center, Column, Neighborhood, *LightTable, Mesh.Lights);
            const double StreamsStart = FPlatformTime::Seconds();
            Mesh.MeshMs = (StreamsStart - Start) * 1000.;
            UVoxelChunkComponent::BuildStreams(Present, Mesh.Streams);
            UVoxelChunkComponent::BuildCards(Present, Mesh.Cards);
            Mesh.LayerMask = UVoxelChunkComponent::GetLayerMask(Present);
            Mesh.StreamsMs = (FPlatformTime::Seconds() - StreamsStart) * 1000.;
            return Result;
        });
}

void FVoxelColumnPipeline::LaunchFarMesh(FIntPoint Column, FRecord& Record, uint8 Neighbors)
{
    Record.bMeshing = true;
    Record.MeshingNeighbors = Neighbors;
    if (Neighbors != PipelineAllNeighbors) bEdgeWallsUsed = true;
    // The 3 x 3 columns row by row, as LaunchMesh takes them: the tints read all of them, the walls the 4 edge ones.
    TStaticArray<TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>, 9> Around;
    Around[4] = Record.Data;
    for (int32 Index = 0; Index < 8; ++Index)
        if (Neighbors & (1u << Index))
        {
            const FIntPoint Offset = PipelineNeighborOffsets[Index];
            Around[(Offset.Y + 1) * 3 + Offset.X + 1] = Records.FindChecked(Column + Offset).Data;
        }
    FJob& Job = AddJob(EJob::FarMesh, Column, Record);
    Job.MeshVersion = Record.MeshVersion;
    Job.Neighbors = Neighbors;
    ++FarJobs;
    StartJob(Job,
        [Table = Table, Terrain = Terrain, FarClasses = FarClasses, Options = Settings.MeshOptions,
            FarOptions = Settings.FarOptions, Around]() -> TSharedPtr<FJobResult>
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_FarMesh);
            const double MeshStart = FPlatformTime::Seconds();
            TSharedPtr<FJobResult> Result = MakeShared<FJobResult>();
            Result->Mesh = MakeUnique<FVoxelColumnMesh>();
            FVoxelColumnMesh& Mesh = *Result->Mesh;
            Mesh.bFar = true;
            FVoxelNeighborhood Neighborhood;
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                    Neighborhood.Set(DX, DY, Around[(DY + 1) * 3 + DX + 1].Get());
            // The center with its tints, then the edge neighbours West, East, North and South (EVoxelFace order).
            TArray<FVoxelFarSummary> Summaries;
            Summaries.SetNum(5);
            VoxelFarMesher::Summarize(
                *FarClasses, *Table, *Around[4], Summaries[0], Terrain->GetTints(), &Neighborhood, Options);
            constexpr int32 EdgeIndices[4] = {3, 5, 1, 7};
            const FVoxelFarSummary* Edges[4] = {};
            for (int32 Face = 0; Face < 4; ++Face)
                if (const FVoxelColumn* Edge = Around[EdgeIndices[Face]].Get())
                {
                    VoxelFarMesher::Summarize(*FarClasses, *Table, *Edge, Summaries[Face + 1]);
                    Edges[Face] = &Summaries[Face + 1];
                }
            FVoxelMeshData Data;
            VoxelFarMesher::Mesh(*Table, Summaries[0], Edges, FarOptions, Data);
            Mesh.QuadCount = Data.GetQuadCount();
            const double StreamsStart = FPlatformTime::Seconds();
            Mesh.MeshMs = (StreamsStart - MeshStart) * 1000.;
            if (!Data.IsEmpty())
            {
                const FVoxelMeshData* Present[1] = {&Data};
                UVoxelChunkComponent::BuildStreams(Present, Mesh.Streams);
                Mesh.LayerMask = UVoxelChunkComponent::GetLayerMask(Present);
                Mesh.SectionMask = GetPipelineSectionMask(Data);
            }
            Mesh.StreamsMs = (FPlatformTime::Seconds() - StreamsStart) * 1000.;
            return Result;
        });
}

void FVoxelColumnPipeline::SortReady()
{
    if (bReadySorted) return;
    // Farthest first, so the nearest pops off the end.
    const auto ByPriority = [this](FIntPoint A, FIntPoint B)
    {
        const int64 DistanceA = GetReadyPriority(A), DistanceB = GetReadyPriority(B);
        if (DistanceA != DistanceB) return DistanceA > DistanceB;
        return A.Y != B.Y ? A.Y > B.Y : A.X > B.X;
    };
    ReadyColumns.Sort(ByPriority);
    if (!Settings.bFarInnerFirst)
        ReadyFarColumns.Sort(ByPriority);
    else
    {
        // The urgent far meshes pop first, nearest first; each is looked up once per sort.
        TArray<TPair<bool, FIntPoint>> Keyed;
        Keyed.Reserve(ReadyFarColumns.Num());
        for (const FIntPoint Column : ReadyFarColumns)
            Keyed.Emplace(IsUrgentFar(Column), Column);
        Keyed.Sort([&ByPriority](const TPair<bool, FIntPoint>& A, const TPair<bool, FIntPoint>& B)
            { return A.Key != B.Key ? !A.Key : ByPriority(A.Value, B.Value); });
        for (int32 Index = 0; Index < Keyed.Num(); ++Index)
            ReadyFarColumns[Index] = Keyed[Index].Value;
    }
    bReadySorted = true;
}

int64 FVoxelColumnPipeline::GetReadyPriority(FIntPoint Column) const
{
    // A column behind the view sector counts as twice as far, so the columns the camera faces go first while it turns:
    // the holes a player sees are those (S9b).
    const int64 Distance = GetPipelineDistanceSquared(Column, Rings.Center);
    const FIntPoint Offset = Column - Rings.Center;
    return int64(Offset.X) * ViewAhead.X + int64(Offset.Y) * ViewAhead.Y < 0 ? 4 * Distance : Distance;
}

bool FVoxelColumnPipeline::IsUrgentFar(FIntPoint Column) const
{
    const int64 Band = int64(Rings.ViewRadius) + 2;
    return Settings.bFarInnerFirst && Rings.GetGapSquared(Column) <= Band * Band &&
        Records.FindChecked(Column).ShownKind == EVoxelMeshKind::None;
}

TArray<FIntPoint>& FVoxelColumnPipeline::GetReadyQueue(bool bNearOnly)
{
    if (bNearOnly) return ReadyColumns;
    // Far meshes wait while any full mesh does, except (bFarInnerFirst) an urgent one: it fills a column at the view's
    // edge before a full mesh refines a column that shows something, and goes first if nearer than a full mesh that
    // fills one.
    if (ReadyColumns.IsEmpty()) return ReadyFarColumns;
    if (ReadyFarColumns.IsEmpty() || !IsUrgentFar(ReadyFarColumns.Last())) return ReadyColumns;
    const FIntPoint Near = ReadyColumns.Last(), Far = ReadyFarColumns.Last();
    const bool bNearFills = Records.FindChecked(Near).ShownKind == EVoxelMeshKind::None;
    return !bNearFills || GetReadyPriority(Far) < GetReadyPriority(Near) ? ReadyFarColumns : ReadyColumns;
}

const FVoxelColumnMesh* FVoxelColumnPipeline::PeekReady(FIntPoint* OutColumn, bool bNearOnly)
{
    if (ReadyColumns.IsEmpty() && (bNearOnly || ReadyFarColumns.IsEmpty())) return nullptr;
    SortReady();
    const TArray<FIntPoint>& Queue = GetReadyQueue(bNearOnly);
    if (OutColumn) *OutColumn = Queue.Last();
    return Records.FindChecked(Queue.Last()).Ready.Get();
}

bool FVoxelColumnPipeline::PopReady(FReady& Out, bool bNearOnly)
{
    if (ReadyColumns.IsEmpty() && (bNearOnly || ReadyFarColumns.IsEmpty())) return false;
    SortReady();
    Out.Column = GetReadyQueue(bNearOnly).Pop(EAllowShrinking::No);
    FRecord& Record = Records.FindChecked(Out.Column);
    Out.Mesh = MoveTemp(Record.Ready);
    Record.bOnScreen = true;
    Record.ShownKind = Out.Mesh->bFar ? EVoxelMeshKind::Far : EVoxelMeshKind::Full;
    Record.ScreenQuads = Out.Mesh->QuadCount;
    Record.ScreenCards = Out.Mesh->Cards.CardBuildData.Num();
    return true;
}

void FVoxelColumnPipeline::TakeUnloaded(TArray<FIntPoint>& Out)
{
    Out = MoveTemp(UnloadedOnScreen);
    UnloadedOnScreen.Reset();
}

const FVoxelColumn* FVoxelColumnPipeline::FindColumn(FIntPoint Column) const
{
    const FRecord* Record = Records.Find(Column);
    return Record ? Record->Data.Get() : nullptr;
}

const FVoxelColumn* FVoxelColumnPipeline::FindSnapshot(FIntPoint Column) const
{
    const FRecord* Record = Records.Find(Column);
    return Record ? Record->Snapshot.Get() : nullptr;
}

bool FVoxelColumnPipeline::GetCell(const FIntVector& Cell, uint16& OutState, int64& OutStamp) const
{
    const FIntPoint Column(Cell.X >> 4, Cell.Y >> 4);
    const FVoxelColumn* Data = FindColumn(Column);
    if (!Data) return false;
    OutState = Data->GetState(Cell.X & 15, Cell.Y & 15, Cell.Z);
    OutStamp = 0;
    if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return true;
    if (const TMap<int32, FEdit>* ColumnEdits = Edits.Find(Column))
        if (const FEdit* Edit = ColumnEdits->Find(FVoxelColumn::GetDenseIndex(Cell.X & 15, Cell.Y & 15, Cell.Z)))
            OutStamp = Edit->Stamp;
    return true;
}

bool FVoxelColumnPipeline::SetCell(const FIntVector& Cell, uint16 State, int64 Stamp)
{
    const FIntPoint Column(Cell.X >> 4, Cell.Y >> 4);
    FRecord* Record = Records.Find(Column);
    if (!Record || !Record->Data || Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return false;
    const int32 X = Cell.X & 15, Y = Cell.Y & 15;
    // Copy on write: jobs in flight keep reading the column they started with.
    TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe> Copy = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>(*Record->Data);
    Copy->SetState(X, Y, Cell.Z, State, *Table);
    Record->Data = Copy;
    Record->DataBytes = Copy->GetAllocatedSize();
    Edits.FindOrAdd(Column).Add(FVoxelColumn::GetDenseIndex(X, Y, Cell.Z), {State, Stamp});
    // Exactly the sections whose padded copy holds the cell; neighbours not final yet read it when they mesh.
    TArray<FVoxelSectionKey, TInlineAllocator<8>> Touched;
    VoxelMesher::GetSectionsTouching(Cell, Touched);
    for (const FVoxelSectionKey& Key : Touched)
        if (FRecord* Neighbor = Records.Find(Key.Column); Neighbor && Neighbor->Data)
            MarkDirty(*Neighbor, 1u << Key.Section);
    Regress();
    return true;
}

bool FVoxelColumnPipeline::SetCells(FIntPoint Column, TConstArrayView<FCellWrite> Cells)
{
    FRecord* Record = Records.Find(Column);
    if (!Record || !Record->Data) return false;
    TArray<TPair<int32, uint16>> Dense;
    Dense.Reserve(Cells.Num());
    // The sections to mark, per column of the 3 x 3: the union of what SetCell marks for each cell.
    TArray<TPair<FIntPoint, uint32>, TInlineAllocator<9>> Dirty;
    TArray<FVoxelSectionKey, TInlineAllocator<8>> Touched;
    for (const FCellWrite& Write : Cells)
    {
        if (FIntPoint(Write.Cell.X >> 4, Write.Cell.Y >> 4) != Column || Write.Cell.Z < VoxelConstants::MinZ ||
            Write.Cell.Z > VoxelConstants::MaxZ)
            continue;
        const int32 Index = FVoxelColumn::GetDenseIndex(Write.Cell.X & 15, Write.Cell.Y & 15, Write.Cell.Z);
        Dense.Add({Index, Write.State});
        Edits.FindOrAdd(Column).Add(Index, {Write.State, Write.Stamp});
        Touched.Reset();
        VoxelMesher::GetSectionsTouching(Write.Cell, Touched);
        for (const FVoxelSectionKey& Key : Touched)
        {
            TPair<FIntPoint, uint32>* Found = Dirty.FindByPredicate(
                [&Key](const TPair<FIntPoint, uint32>& Entry) { return Entry.Key == Key.Column; });
            if (!Found) Found = &Dirty.Add_GetRef({Key.Column, 0});
            Found->Value |= 1u << Key.Section;
        }
    }
    if (Dense.IsEmpty()) return true;
    // One copy on write for every cell: jobs in flight keep reading the column they started with.
    TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe> Copy = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>(*Record->Data);
    Copy->SetStates(Dense, *Table);
    Record->Data = Copy;
    Record->DataBytes = Copy->GetAllocatedSize();
    for (const TPair<FIntPoint, uint32>& Entry : Dirty)
        if (FRecord* Neighbor = Records.Find(Entry.Key); Neighbor && Neighbor->Data) MarkDirty(*Neighbor, Entry.Value);
    Regress();
    return true;
}

void FVoxelColumnPipeline::DropEdits()
{
    for (const TPair<FIntPoint, TMap<int32, FEdit>>& Column : Edits)
    {
        FRecord* Record = Records.Find(Column.Key);
        if (!Record || (!Record->Data && !Record->bGenerating && !Record->bFinalizing)) continue;
        // Built again without the edits; whatever is in flight belongs to the edited column. With decoration the
        // snapshot and buckets are pure and stay; the buckets aimed at it were freed when it finalized, so its
        // sources decorate again (recompute-on-miss).
        Record->Serial = ++NextSerial;
        Record->Data.Reset();
        Record->DataBytes = 0;
        Record->bGenerating = Record->bDecorating = Record->bFinalizing = Record->bMeshing = false;
        Record->Cache.Reset();
        Record->CacheBytes = 0;
        if (Record->Ready)
        {
            (Record->Ready->bFar ? ReadyFarColumns : ReadyColumns).RemoveSingle(Column.Key);
            Record->Ready.Reset();
        }
        // Its neighbours' borders read the edits too.
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
                if (FRecord* Neighbor = Records.Find(Column.Key + FIntPoint(DX, DY)); Neighbor && Neighbor->Data)
                    MarkDirty(*Neighbor, PipelineAllSections);
    }
    Edits.Reset();
    Regress();
}

void FVoxelColumnPipeline::ForEachNonAir(
    const FIntVector& Min, const FIntVector& Max, TFunctionRef<void(const FIntVector&, uint16, int64)> Visit) const
{
    const int32 LowZ = FMath::Max(Min.Z, VoxelConstants::MinZ), HighZ = FMath::Min(Max.Z, VoxelConstants::MaxZ);
    if (LowZ > HighZ) return;
    for (const TPair<FIntPoint, FRecord>& Pair : Records)
    {
        if (!Pair.Value.Data) continue;
        const int64 BaseX = int64(Pair.Key.X) * VoxelConstants::ColumnSize,
                    BaseY = int64(Pair.Key.Y) * VoxelConstants::ColumnSize;
        const int64 LowX = FMath::Max<int64>(Min.X - BaseX, 0), HighX = FMath::Min<int64>(Max.X - BaseX, 15);
        const int64 LowY = FMath::Max<int64>(Min.Y - BaseY, 0), HighY = FMath::Min<int64>(Max.Y - BaseY, 15);
        if (LowX > HighX || LowY > HighY) continue;
        const FVoxelColumn& Data = *Pair.Value.Data;
        const TMap<int32, FEdit>* ColumnEdits = Edits.Find(Pair.Key);
        for (int32 Z = LowZ; Z <= HighZ; ++Z)
        {
            // All-air sections are skipped whole.
            if (Data.GetSection((Z - VoxelConstants::MinZ) >> 4).IsEmpty())
            {
                Z |= 15;
                continue;
            }
            for (int32 Y = int32(LowY); Y <= int32(HighY); ++Y)
                for (int32 X = int32(LowX); X <= int32(HighX); ++X)
                {
                    const uint16 State = Data.GetState(X, Y, Z);
                    if (!State) continue;
                    const FEdit* Edit = ColumnEdits ? ColumnEdits->Find(FVoxelColumn::GetDenseIndex(X, Y, Z)) : nullptr;
                    Visit(FIntVector(int32(BaseX + X), int32(BaseY + Y), Z), State, Edit ? Edit->Stamp : 0);
                }
        }
    }
}

int64 FVoxelColumnPipeline::CountNonAir() const
{
    int64 Count = 0;
    for (const TPair<FIntPoint, FRecord>& Pair : Records)
        if (Pair.Value.Data)
            for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
                Count += Pair.Value.Data->GetSection(Section).GetNonAirCount();
    return Count;
}

SIZE_T FVoxelColumnPipeline::GetEditBytes() const
{
    SIZE_T Bytes = Edits.GetAllocatedSize();
    for (const TPair<FIntPoint, TMap<int32, FEdit>>& Column : Edits)
        Bytes += Column.Value.GetAllocatedSize();
    return Bytes;
}

void FVoxelColumnPipeline::ForEachColumn(TFunctionRef<void(FIntPoint, const FVoxelColumn&)> Visit) const
{
    for (const TPair<FIntPoint, FRecord>& Pair : Records)
        if (Pair.Value.Data) Visit(Pair.Key, *Pair.Value.Data);
}

FVoxelColumnStatus FVoxelColumnPipeline::GetStatus(FIntPoint Column) const
{
    FVoxelColumnStatus Status;
    const FRecord* Record = Records.Find(Column);
    if (!Record) return Status;
    Status.bPresent = true;
    Status.bGenerated = Record->Data.IsValid();
    Status.bGenerating = Record->bGenerating;
    Status.bMeshing = Record->bMeshing;
    Status.bReady = Record->Ready.IsValid();
    Status.bOnScreen = Record->bOnScreen;
    Status.Shown = Record->ShownKind;
    Status.Meshed = Record->MeshedKind;
    Status.DirtySections = Record->Dirty;
    Status.bCached = !Record->Cache.IsEmpty();
    Status.bSnapshot = Record->Snapshot.IsValid();
    Status.bDecorated = Record->bDecorated;
    Status.bDecorating = Record->bDecorating;
    Status.bFinalizing = Record->bFinalizing;
    Status.HeldBuckets = Record->HeldBuckets;
    return Status;
}

bool FVoxelColumnPipeline::IsViewOnScreen() const
{
    return bPlanned && ReadyColumns.IsEmpty() && ReadyFarColumns.IsEmpty() && FirstIncomplete >= Needed.Num();
}

bool FVoxelColumnPipeline::IsNearViewOnScreen() const
{
    // Without a far ring the near view is the whole view, as it was before far LOD.
    if (Rings.GetDrawnRadius() == Rings.ViewRadius) return IsViewOnScreen();
    if (!bPlanned || !ReadyColumns.IsEmpty()) return false;
    for (int32 Index = FirstIncomplete; Index < Needed.Num(); ++Index)
    {
        if (NeededRing[Index] != 0 || !Rings.IsInView(Needed[Index])) continue;
        const FRecord& Record = Records.FindChecked(Needed[Index]);
        if (!IsComplete(Index) || Record.ShownKind != EVoxelMeshKind::Full) return false;
    }
    return true;
}

bool FVoxelColumnPipeline::IsIdle() const
{
    return Jobs.IsEmpty() && IsViewOnScreen();
}

FVoxelPipelineStats FVoxelColumnPipeline::GetStats() const
{
    FVoxelPipelineStats Stats;
    Stats.Columns = Records.Num();
    Stats.Needed = Needed.Num();
    for (const uint8 Ring : NeededRing)
        ++Stats.NeededPerRing[FMath::Min<int32>(Ring, FVoxelStreamingRings::MaxStages)];
    Stats.Ready = ReadyColumns.Num() + ReadyFarColumns.Num();
    Stats.FarReady = ReadyFarColumns.Num();
    Stats.FarMeshing = FarJobs;
    Stats.FarMesh = FarMeshTimes;
    Stats.Dropped = Dropped;
    Stats.Stale = Stale;
    for (const TPair<FIntPoint, TMap<int32, FEdit>>& Column : Edits)
        Stats.Edits += Column.Value.Num();
    Stats.Generate = GenerateTimes;
    Stats.Decorate = DecorateTimes;
    Stats.Finalize = FinalizeTimes;
    Stats.Mesh = MeshTimes;
    Stats.Streams = StreamsTimes;
    Stats.Regenerated = Regenerated;
    Stats.Redecorated = Redecorated;
    Stats.BucketsDropped = BucketsDropped;
    Stats.BucketsAppliedToFinal = BucketsAppliedToFinal;
    Stats.Cancelled = Cancelled;
    Stats.ShiftedPlans = ShiftedPlans;
    Stats.CarriedColumns = CarriedColumns;
    for (const TPair<FIntPoint, FRecord>& Pair : Records)
    {
        const FRecord& Record = Pair.Value;
        Stats.Generated += Record.Data.IsValid();
        Stats.Generating += Record.bGenerating;
        Stats.Meshing += Record.bMeshing;
        Stats.ColumnBytes += Record.DataBytes;
        Stats.CacheBytes += Record.CacheBytes;
        Stats.Snapshots += Record.Snapshot.IsValid();
        Stats.SnapshotBytes += Record.SnapshotBytes;
        Stats.Decorated += Record.bDecorated;
        Stats.Decorating += Record.bDecorating;
        Stats.Finalizing += Record.bFinalizing;
        for (const TSharedPtr<const FVoxelWriteBucket, ESPMode::ThreadSafe>& Bucket : Record.Buckets)
            Stats.Buckets += Bucket.IsValid();
        for (const SIZE_T Bytes : Record.BucketBytes)
            Stats.BucketBytes += Bytes;
        if (Record.bOnScreen)
        {
            ++Stats.OnScreen;
            Stats.ScreenQuads += Record.ScreenQuads;
            Stats.ScreenCards += Record.ScreenCards;
            Stats.MostCards = FMath::Max(Stats.MostCards, Record.ScreenCards);
        }
        if (Record.bOnScreen && Record.ShownKind == EVoxelMeshKind::Far && Rings.IsDrawn(Pair.Key))
        {
            ++Stats.FarOnScreen;
            Stats.FarQuads += Record.ScreenQuads;
        }
        if (!Rings.IsInView(Pair.Key))
        {
            Stats.FarWanted += Rings.IsFar(Pair.Key);
            continue;
        }
        ++Stats.InView;
        Stats.InViewOnScreen +=
            Record.bOnScreen && Record.ShownKind == EVoxelMeshKind::Full && !Record.Dirty && !Record.Ready;
        Stats.Coarse += Record.bOnScreen && Record.ShownKind == EVoxelMeshKind::Far;
    }
    return Stats;
}
