#include "Voxel/VoxelColumnPipeline.h"
#include "Blocks/VoxelBlockTable.h"
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

    int64 GetPipelineDistanceSquared(FIntPoint A, FIntPoint B)
    {
        const int64 DX = int64(A.X) - B.X, DY = int64(A.Y) - B.Y;
        return DX * DX + DY * DY;
    }

    bool IsPipelineNear(FIntPoint Column, FIntPoint Center, int32 Radius)
    {
        return FMath::Abs(int64(Column.X) - Center.X) <= Radius && FMath::Abs(int64(Column.Y) - Center.Y) <= Radius;
    }
}

struct FVoxelColumnPipeline::FJobResult
{
    TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe> Column;
    TUniquePtr<FVoxelColumnMesh> Mesh;
    double Ms = 0;
};

struct FVoxelColumnPipeline::FJob
{
    bool bMesh = false;
    FIntPoint Column = FIntPoint::ZeroValue;
    uint64 Serial = 0;
    uint32 MeshVersion = 0;
    uint8 Neighbors = 0;
    UE::Tasks::TTask<TSharedPtr<FJobResult>> Task;
};

FVoxelColumnPipeline::FVoxelColumnPipeline(
    TSharedRef<const FVoxelBlockTable> InTable, TSharedRef<const IVoxelTerrain> InTerrain, const FSettings& InSettings)
    : Table(MoveTemp(InTable)), Terrain(MoveTemp(InTerrain)), Settings(InSettings)
{
    Settings.MaxTasksInFlight = FMath::Max(1, Settings.MaxTasksInFlight);
}

FVoxelColumnPipeline::~FVoxelColumnPipeline()
{
    WaitForJobs();
}

void FVoxelColumnPipeline::SetMaxTasksInFlight(int32 MaxTasks)
{
    Settings.MaxTasksInFlight = FMath::Max(1, MaxTasks);
}

void FVoxelColumnPipeline::SetEdgeWalls(bool bEdgeWalls)
{
    if (Settings.bEdgeWalls == bEdgeWalls) return;
    Settings.bEdgeWalls = bEdgeWalls;
    // The next SetFocus plans again and remeshes the columns whose edges changed.
    bPlanned = false;
}

void FVoxelColumnPipeline::WaitForJobs()
{
    for (FJob& Job : Jobs)
        Job.Task.Wait();
}

FVoxelColumnPipeline::FRecord& FVoxelColumnPipeline::AddRecord(FIntPoint Column)
{
    FRecord& Record = Records.Add(Column);
    Record.Serial = ++NextSerial;
    return Record;
}

void FVoxelColumnPipeline::SetFocus(const FVoxelStreamingRings& NewRings, int32 NewViewSector)
{
    if (bPlanned && NewRings == Rings && NewViewSector == ViewSector) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_Plan);
    const bool bCenterMoved = !bPlanned || NewRings.Center != Rings.Center;
    Rings = NewRings;
    ViewSector = NewViewSector;
    bPlanned = true;
    VoxelStreaming::GetNeededColumns(Rings, ViewSector, Needed);
    NeededInView.Init(false, Needed.Num());
    for (int32 Index = 0; Index < Needed.Num(); ++Index)
    {
        NeededInView[Index] = Rings.IsInView(Needed[Index]);
        if (!Records.Contains(Needed[Index])) AddRecord(Needed[Index]);
    }
    TArray<FIntPoint> Present, Unloads;
    Records.GenerateKeyArray(Present);
    VoxelStreaming::GetUnloads(Rings, Present, Unloads);
    for (const FIntPoint Column : Unloads)
        Unload(Column);
    // Caches live only near the center, where the player can edit; one column of slack avoids churn at the border.
    if (bCenterMoved)
        for (TPair<FIntPoint, FRecord>& Pair : Records)
            if (!Pair.Value.Cache.IsEmpty() && !IsPipelineNear(Pair.Key, Rings.Center, Settings.MeshCacheRadius + 1))
            {
                Pair.Value.Cache.Reset();
                Pair.Value.CacheBytes = 0;
            }
    // With edge walls, a column whose neighbours moved in or out of view meshes again.
    if (Settings.bEdgeWalls || bEdgeWallsUsed)
        for (int32 Index = 0; Index < Needed.Num(); ++Index)
        {
            FRecord& Record = Records.FindChecked(Needed[Index]);
            if (!NeededInView[Index] || !Record.Data || (Record.Dirty && !Record.bMeshing)) continue;
            const uint8 Current = Record.bMeshing ? Record.MeshingNeighbors : Record.MeshedNeighbors;
            if (Current != GetMeshNeighbors(Needed[Index])) MarkDirty(Record, PipelineAllSections);
        }
    FirstIncomplete = 0;
    bReadySorted = false;
}

void FVoxelColumnPipeline::Unload(FIntPoint Column)
{
    const FRecord* Record = Records.Find(Column);
    if (!Record) return;
    if (Record->Ready) ReadyColumns.RemoveSingle(Column);
    if (Record->bOnScreen) UnloadedOnScreen.Add(Column);
    Records.Remove(Column);
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
        if (!Job.Task.IsCompleted())
        {
            ++Index;
            continue;
        }
        const TSharedPtr<FJobResult> Result = Job.Task.GetResult();
        FRecord* Record = Records.Find(Job.Column);
        const bool bCurrent = Record && Record->Serial == Job.Serial;
        if (!Job.bMesh)
        {
            GenerateTimes.Add(Result->Ms);
            if (bCurrent)
            {
                Record->bGenerating = false;
                Record->Data = Result->Column;
                Record->DataBytes = Result->Column->GetAllocatedSize();
                MarkDirty(*Record, PipelineAllSections);
            }
            else
                ++Dropped;
        }
        else
        {
            MeshTimes.Add(Result->Mesh->MeshMs);
            StreamsTimes.Add(Result->Mesh->StreamsMs);
            if (bCurrent) Record->bMeshing = false;
            if (bCurrent && Record->MeshVersion == Job.MeshVersion)
            {
                // Nothing became dirty since the job started, so it meshed every dirty section.
                Record->Dirty = 0;
                Record->MeshedNeighbors = Job.Neighbors;
                if (IsPipelineNear(Job.Column, Rings.Center, Settings.MeshCacheRadius))
                {
                    Record->Cache = Result->Mesh->Sections;
                    Record->CacheBytes = Result->Mesh->SectionBytes;
                }
                else
                {
                    Record->Cache.Reset();
                    Record->CacheBytes = 0;
                }
                if (!Record->Ready)
                {
                    ReadyColumns.Add(Job.Column);
                    bReadySorted = false;
                }
                Record->Ready = MoveTemp(Result->Mesh);
            }
            else
                ++Dropped;
        }
        Jobs.RemoveAtSwap(Index);
    }
}

bool FVoxelColumnPipeline::IsComplete(int32 NeededIndex) const
{
    const FRecord& Record = Records.FindChecked(Needed[NeededIndex]);
    return Record.Data && (!NeededInView[NeededIndex] || (!Record.Dirty && !Record.bMeshing));
}

void FVoxelColumnPipeline::LaunchJobs()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_Launch);
    while (FirstIncomplete < Needed.Num() && IsComplete(FirstIncomplete))
        ++FirstIncomplete;
    // Nearest first: a column's mesh as soon as its neighbours exist, otherwise its blocks.
    for (int32 Index = FirstIncomplete; Index < Needed.Num() && Jobs.Num() < Settings.MaxTasksInFlight; ++Index)
    {
        const FIntPoint Column = Needed[Index];
        FRecord& Record = Records.FindChecked(Column);
        if (!Record.Data)
        {
            if (!Record.bGenerating) LaunchGenerate(Column, Record);
            continue;
        }
        if (!NeededInView[Index] || !Record.Dirty || Record.bMeshing) continue;
        const uint8 Neighbors = GetMeshNeighbors(Column);
        if (AreGenerated(Column, Neighbors)) LaunchMesh(Column, Record, Neighbors);
    }
}

uint8 FVoxelColumnPipeline::GetMeshNeighbors(FIntPoint Column) const
{
    if (!Settings.bEdgeWalls) return PipelineAllNeighbors;
    uint8 Mask = 0;
    for (int32 Index = 0; Index < 8; ++Index)
        if (Rings.IsInView(Column + PipelineNeighborOffsets[Index])) Mask |= uint8(1u << Index);
    return Mask;
}

bool FVoxelColumnPipeline::AreGenerated(FIntPoint Column, uint8 Neighbors) const
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

void FVoxelColumnPipeline::LaunchGenerate(FIntPoint Column, FRecord& Record)
{
    Record.bGenerating = true;
    // The column's edits, replayed over the terrain: they survive unloading.
    TArray<TPair<int32, uint16>> Replay;
    if (const TMap<int32, FEdit>* ColumnEdits = Edits.Find(Column))
        for (const TPair<int32, FEdit>& Edit : *ColumnEdits)
            Replay.Add({Edit.Key, Edit.Value.State});
    FJob& Job = Jobs.AddDefaulted_GetRef();
    Job.Column = Column;
    Job.Serial = Record.Serial;
    Job.Task = UE::Tasks::Launch(
        UE_SOURCE_LOCATION,
        [Terrain = Terrain, Table = Table, Column, Replay = MoveTemp(Replay)]
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(VoxelPipeline_Generate);
            const double Start = FPlatformTime::Seconds();
            // The dense column is scratch, reused by every job on this worker.
            thread_local TArray<uint16> Dense;
            TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe> Built = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>(Column);
            Terrain->Generate(Column, *Built, Dense);
            for (const TPair<int32, uint16>& Edit : Replay)
                Built->SetState(Edit.Key % VoxelConstants::ColumnSize,
                    Edit.Key / VoxelConstants::ColumnSize % VoxelConstants::ColumnSize,
                    Edit.Key / FVoxelColumn::Area + VoxelConstants::MinZ, Edit.Value, *Table);
            TSharedPtr<FJobResult> Result = MakeShared<FJobResult>();
            Result->Column = Built;
            Result->Ms = (FPlatformTime::Seconds() - Start) * 1000.;
            return Result;
        },
        UE::Tasks::ETaskPriority::BackgroundHigh);
}

void FVoxelColumnPipeline::LaunchMesh(FIntPoint Column, FRecord& Record, uint8 Neighbors)
{
    Record.bMeshing = true;
    Record.MeshingNeighbors = Neighbors;
    // Without a cache, or with one built from other neighbours (edge walls), every section meshes.
    const bool bUseCache = !Record.Cache.IsEmpty() && Record.MeshedNeighbors == Neighbors;
    const uint32 Sections = bUseCache ? Record.Dirty : PipelineAllSections;
    if (Neighbors != PipelineAllNeighbors) bEdgeWallsUsed = true;
    FJob& Job = Jobs.AddDefaulted_GetRef();
    Job.bMesh = true;
    Job.Column = Column;
    Job.Serial = Record.Serial;
    Job.MeshVersion = Record.MeshVersion;
    Job.Neighbors = Neighbors;
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
    Job.Task = UE::Tasks::Launch(
        UE_SOURCE_LOCATION,
        [Table = Table, Options = Settings.MeshOptions, Around, Cached = MoveTemp(Cached), Sections]
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
                    VoxelMesher::MeshSection(*Table, *Padded, Section, Options, *Data);
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
            const double StreamsStart = FPlatformTime::Seconds();
            Mesh.MeshMs = (StreamsStart - Start) * 1000.;
            UVoxelChunkComponent::BuildStreams(Present, Mesh.Streams);
            UVoxelChunkComponent::BuildCards(Present, Mesh.Cards);
            Mesh.LayerMask = UVoxelChunkComponent::GetLayerMask(Present);
            Mesh.StreamsMs = (FPlatformTime::Seconds() - StreamsStart) * 1000.;
            return Result;
        },
        UE::Tasks::ETaskPriority::BackgroundHigh);
}

void FVoxelColumnPipeline::SortReady()
{
    if (bReadySorted) return;
    // Farthest first, so the nearest pops off the end.
    ReadyColumns.Sort(
        [Center = Rings.Center](FIntPoint A, FIntPoint B)
        {
            const int64 DistanceA = GetPipelineDistanceSquared(A, Center),
                        DistanceB = GetPipelineDistanceSquared(B, Center);
            if (DistanceA != DistanceB) return DistanceA > DistanceB;
            return A.Y != B.Y ? A.Y > B.Y : A.X > B.X;
        });
    bReadySorted = true;
}

const FVoxelColumnMesh* FVoxelColumnPipeline::PeekReady(FIntPoint* OutColumn)
{
    if (ReadyColumns.IsEmpty()) return nullptr;
    SortReady();
    if (OutColumn) *OutColumn = ReadyColumns.Last();
    return Records.FindChecked(ReadyColumns.Last()).Ready.Get();
}

bool FVoxelColumnPipeline::PopReady(FReady& Out)
{
    if (ReadyColumns.IsEmpty()) return false;
    SortReady();
    Out.Column = ReadyColumns.Pop(EAllowShrinking::No);
    FRecord& Record = Records.FindChecked(Out.Column);
    Out.Mesh = MoveTemp(Record.Ready);
    Record.bOnScreen = true;
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
    // Exactly the sections whose padded copy holds the cell; neighbours not generated yet read it when they mesh.
    TArray<FVoxelSectionKey, TInlineAllocator<8>> Touched;
    VoxelMesher::GetSectionsTouching(Cell, Touched);
    for (const FVoxelSectionKey& Key : Touched)
        if (FRecord* Neighbor = Records.Find(Key.Column); Neighbor && Neighbor->Data)
            MarkDirty(*Neighbor, 1u << Key.Section);
    FirstIncomplete = 0;
    return true;
}

void FVoxelColumnPipeline::DropEdits()
{
    for (const TPair<FIntPoint, TMap<int32, FEdit>>& Column : Edits)
    {
        FRecord* Record = Records.Find(Column.Key);
        if (!Record || (!Record->Data && !Record->bGenerating)) continue;
        // Generated again from the terrain alone; whatever is in flight belongs to the edited column.
        Record->Serial = ++NextSerial;
        Record->Data.Reset();
        Record->DataBytes = 0;
        Record->bGenerating = Record->bMeshing = false;
        Record->Cache.Reset();
        Record->CacheBytes = 0;
        if (Record->Ready)
        {
            Record->Ready.Reset();
            ReadyColumns.RemoveSingle(Column.Key);
        }
        // Its neighbours' borders read the edits too.
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
                if (FRecord* Neighbor = Records.Find(Column.Key + FIntPoint(DX, DY)); Neighbor && Neighbor->Data)
                    MarkDirty(*Neighbor, PipelineAllSections);
    }
    Edits.Reset();
    FirstIncomplete = 0;
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
    Status.DirtySections = Record->Dirty;
    Status.bCached = !Record->Cache.IsEmpty();
    return Status;
}

bool FVoxelColumnPipeline::IsViewOnScreen() const
{
    return bPlanned && ReadyColumns.IsEmpty() && FirstIncomplete >= Needed.Num();
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
    Stats.Ready = ReadyColumns.Num();
    Stats.Dropped = Dropped;
    for (const TPair<FIntPoint, TMap<int32, FEdit>>& Column : Edits)
        Stats.Edits += Column.Value.Num();
    Stats.Generate = GenerateTimes;
    Stats.Mesh = MeshTimes;
    Stats.Streams = StreamsTimes;
    for (const TPair<FIntPoint, FRecord>& Pair : Records)
    {
        const FRecord& Record = Pair.Value;
        Stats.Generated += Record.Data.IsValid();
        Stats.Generating += Record.bGenerating;
        Stats.Meshing += Record.bMeshing;
        Stats.ColumnBytes += Record.DataBytes;
        Stats.CacheBytes += Record.CacheBytes;
        if (Record.bOnScreen)
        {
            ++Stats.OnScreen;
            Stats.ScreenQuads += Record.ScreenQuads;
            Stats.ScreenCards += Record.ScreenCards;
            Stats.MostCards = FMath::Max(Stats.MostCards, Record.ScreenCards);
        }
        if (!Rings.IsInView(Pair.Key)) continue;
        ++Stats.InView;
        Stats.InViewOnScreen += Record.bOnScreen && !Record.Dirty && !Record.Ready;
    }
    return Stats;
}
