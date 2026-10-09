#include "Generation/VoxelArea.h"
#include "Async/Fundamental/Scheduler.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelTerrain.h"
#include "VoxelConstants.h"

namespace
{
    FString DescribeTimes(const TCHAR* Name, TConstArrayView<double> Times, int32 Count)
    {
        double Sum = 0, Worst = 0;
        for (const double Time : Times)
        {
            Sum += Time;
            Worst = FMath::Max(Worst, Time);
        }
        return FString::Printf(TEXT("%s: %d, mean %.3f ms, worst %.3f ms, total %.1f ms"), Name, Count,
            Count ? Sum / Count : 0., Worst, Sum);
    }
}

void FVoxelArea::Build(const FVoxelBlockTable& Table, const IVoxelTerrain& Terrain, FIntPoint InCenter, int32 InRadius,
    const FVoxelMeshOptions& Options, bool bParallel, FVoxelArea& Out)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelArea_Build);
    Out = FVoxelArea();
    Out.Center = InCenter;
    Out.Radius = InRadius;
    Out.Workers = bParallel ? FMath::Max(1, int32(LowLevelTasks::FScheduler::Get().GetNumWorkers())) : 1;
    const int32 Side = 2 * InRadius + 1, ColumnCount = Side * Side;
    const EParallelForFlags Flags = bParallel ? EParallelForFlags::Unbalanced : EParallelForFlags::ForceSingleThread;

    // Columns: each is a pure function of the terrain's settings and its position.
    TArray<TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe>> Built;
    Built.SetNum(ColumnCount);
    Out.GenerateMs.SetNumZeroed(ColumnCount);
    double Start = FPlatformTime::Seconds();
    ParallelFor(
        TEXT("VoxelArea.Generate"), ColumnCount, 1,
        [&](int32 Index)
        {
            const FIntPoint Position = InCenter + FIntPoint(Index % Side - InRadius, Index / Side - InRadius);
            const double ColumnStart = FPlatformTime::Seconds();
            TArray<uint16> Dense;
            TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe> Column = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>();
            Terrain.Generate(Position, *Column, Dense);
            Built[Index] = Column;
            Out.GenerateMs[Index] = (FPlatformTime::Seconds() - ColumnStart) * 1000.;
        },
        Flags);
    Out.GenerateWallMs = (FPlatformTime::Seconds() - Start) * 1000.;
    for (const TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe>& Column : Built)
        Out.Columns.Add(Column);

    // Sections: each reads its 3 x 3 neighbourhood, so all columns must exist first.
    const int32 SectionCount = ColumnCount * VoxelConstants::SectionCount;
    TArray<FVoxelMeshData> Meshes;
    Meshes.SetNum(SectionCount);
    Out.GatherMs.SetNumZeroed(SectionCount);
    Out.MeshMs.SetNumZeroed(SectionCount);
    Start = FPlatformTime::Seconds();
    ParallelFor(
        TEXT("VoxelArea.Mesh"), SectionCount, 4,
        [&](int32 Job)
        {
            const int32 ColumnIndex = Job / VoxelConstants::SectionCount, Section = Job % VoxelConstants::SectionCount;
            const FIntPoint Position = Out.Columns[ColumnIndex]->GetPosition();
            FVoxelNeighborhood Neighborhood;
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                {
                    const int32 Neighbor = Out.GetColumnIndex(Position + FIntPoint(DX, DY));
                    Neighborhood.Set(DX, DY, Neighbor == INDEX_NONE ? nullptr : Out.Columns[Neighbor].Get());
                }
            const double GatherStart = FPlatformTime::Seconds();
            TUniquePtr<FVoxelPaddedSection> Padded = MakeUnique<FVoxelPaddedSection>();
            VoxelMesher::GatherPadded(Neighborhood, Section, *Padded);
            const double MeshStart = FPlatformTime::Seconds();
            VoxelMesher::MeshSection(Table, *Padded, Section, Options, Meshes[Job], Terrain.GetTints(), &Neighborhood);
            Out.GatherMs[Job] = (MeshStart - GatherStart) * 1000.;
            Out.MeshMs[Job] = (FPlatformTime::Seconds() - MeshStart) * 1000.;
        },
        Flags);
    Out.MeshWallMs = (FPlatformTime::Seconds() - Start) * 1000.;
    for (int32 Job = 0; Job < SectionCount; ++Job)
        if (!Meshes[Job].IsEmpty())
            Out.Meshes.Add(
                {Job / VoxelConstants::SectionCount, Job % VoxelConstants::SectionCount, MoveTemp(Meshes[Job])});
}

int32 FVoxelArea::GetColumnIndex(FIntPoint Column) const
{
    const int32 Side = 2 * Radius + 1;
    const FIntPoint Local = Column - Center + FIntPoint(Radius, Radius);
    if (Local.X < 0 || Local.Y < 0 || Local.X >= Side || Local.Y >= Side) return INDEX_NONE;
    return Local.Y * Side + Local.X;
}

int64 FVoxelArea::GetQuadCount() const
{
    int64 Quads = 0;
    for (const FSectionMesh& Section : Meshes)
        Quads += Section.Mesh.GetQuadCount();
    return Quads;
}

SIZE_T FVoxelArea::GetColumnBytes() const
{
    SIZE_T Bytes = 0;
    for (const TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>& Column : Columns)
        Bytes += Column->GetAllocatedSize();
    return Bytes;
}

FString FVoxelArea::Describe() const
{
    // Meshed sections alone show what a visible section costs; the all-sections line includes the quick exits.
    TArray<double> Meshed;
    for (const FSectionMesh& Section : Meshes)
        Meshed.Add(MeshMs[Section.Column * VoxelConstants::SectionCount + Section.Section]);
    const int32 Side = 2 * Radius + 1;
    FString Out = FString::Printf(TEXT("Area %d x %d columns around (%d, %d), %d worker%s\n"), Side, Side, Center.X,
        Center.Y, Workers, Workers == 1 ? TEXT("") : TEXT("s"));
    Out += TEXT("  ") + DescribeTimes(TEXT("Generate columns"), GenerateMs, GenerateMs.Num()) +
        FString::Printf(TEXT(" (wall %.1f ms)\n"), GenerateWallMs);
    Out += TEXT("  ") + DescribeTimes(TEXT("Gather sections"), GatherMs, GatherMs.Num()) + TEXT("\n");
    Out += TEXT("  ") + DescribeTimes(TEXT("Mesh sections, all"), MeshMs, MeshMs.Num()) +
        FString::Printf(TEXT(" (wall %.1f ms)\n"), MeshWallMs);
    Out += TEXT("  ") + DescribeTimes(TEXT("Mesh sections with geometry"), Meshed, Meshed.Num()) + TEXT("\n");
    const SIZE_T Bytes = GetColumnBytes();
    Out += FString::Printf(TEXT("  Quads %lld (%.0f per meshed section); columns %.1f KB each (%.1f MB total)"),
        GetQuadCount(), Meshes.Num() ? double(GetQuadCount()) / Meshes.Num() : 0.,
        Columns.Num() ? double(Bytes) / Columns.Num() / 1024. : 0., double(Bytes) / (1024. * 1024.));
    return Out;
}
