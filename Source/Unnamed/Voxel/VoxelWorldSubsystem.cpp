#include "Voxel/VoxelWorldSubsystem.h"
#include "Async/Fundamental/Scheduler.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Foundation/FoundationProfile.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Voxel/VoxelChunkComponent.h"
#include "Voxel/VoxelConsole.h"
#include "Voxel/VoxelWorld.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"
#include "VoxelStats.h"

DECLARE_CYCLE_STAT(TEXT("Streaming tick"), STAT_VoxelStreamingTick, STATGROUP_Voxel);
DECLARE_CYCLE_STAT(TEXT("Apply columns"), STAT_VoxelApplyColumns, STATGROUP_Voxel);
DECLARE_DWORD_COUNTER_STAT(TEXT("Columns generated"), STAT_VoxelColumnsGenerated, STATGROUP_Voxel);
DECLARE_DWORD_COUNTER_STAT(TEXT("Columns needed"), STAT_VoxelColumnsNeeded, STATGROUP_Voxel);
DECLARE_DWORD_COUNTER_STAT(TEXT("Columns in view"), STAT_VoxelColumnsInView, STATGROUP_Voxel);
DECLARE_DWORD_COUNTER_STAT(TEXT("Columns in view on screen"), STAT_VoxelColumnsOnScreen, STATGROUP_Voxel);
DECLARE_DWORD_COUNTER_STAT(TEXT("Jobs in flight"), STAT_VoxelJobsInFlight, STATGROUP_Voxel);
DECLARE_DWORD_COUNTER_STAT(TEXT("Meshes waiting to apply"), STAT_VoxelMeshesWaiting, STATGROUP_Voxel);
DECLARE_DWORD_COUNTER_STAT(TEXT("Columns applied this frame"), STAT_VoxelColumnsApplied, STATGROUP_Voxel);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Chunk work this frame (ms)"), STAT_VoxelChunkWorkMs, STATGROUP_Voxel);
DECLARE_MEMORY_STAT(TEXT("Column data"), STAT_VoxelColumnMemory, STATGROUP_Voxel);
DECLARE_MEMORY_STAT(TEXT("Section mesh cache"), STAT_VoxelCacheMemory, STATGROUP_Voxel);

namespace
{
    constexpr double StreamingColumnCm = VoxelConstants::ColumnSize * UFoundationProfile::BlockCm;

    FString DescribeStreamingStage(const TCHAR* Name, const FVoxelStageTimes& Times)
    {
        return FString::Printf(TEXT("  %s: %lld, mean %.3f ms, worst %.3f ms, total %.1f ms\n"), Name, Times.Count,
            Times.GetMeanMs(), Times.WorstMs, Times.TotalMs);
    }
}

bool UVoxelWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVoxelWorldSubsystem::Deinitialize()
{
    Stop();
    Super::Deinitialize();
}

TStatId UVoxelWorldSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UVoxelWorldSubsystem, STATGROUP_Tickables);
}

UVoxelWorldSubsystem* UVoxelWorldSubsystem::Get(const UWorld* World)
{
    return World ? World->GetSubsystem<UVoxelWorldSubsystem>() : nullptr;
}

void UVoxelWorldSubsystem::Start(AVoxelWorld& InOwner, TSharedRef<const FVoxelBlockTable> Table,
    TSharedRef<const IVoxelTerrain> Terrain, TConstArrayView<UMaterialInterface*> LayerMaterials,
    const FVector& InitialFocus)
{
    Stop();
    Owner = &InOwner;
    Materials.Reset();
    for (UMaterialInterface* Material : LayerMaterials)
        Materials.Add(Material);
    FVoxelColumnPipeline::FSettings Settings;
    Settings.MeshOptions.bGreedy = VoxelConsole::CVarGreedyMeshing.GetValueOnGameThread();
    Settings.MeshOptions.bAmbientOcclusion = VoxelConsole::CVarAmbientOcclusion.GetValueOnGameThread();
    Settings.MaxTasksInFlight = VoxelConsole::GetMaxTasksInFlight();
    Settings.bEdgeWalls = VoxelConsole::CVarEdgeWalls.GetValueOnGameThread();
    Pipeline = MakeUnique<FVoxelColumnPipeline>(MoveTemp(Table), MoveTemp(Terrain), Settings);
    LastFocus = InitialFocus;
    StartSeconds = LogSeconds = FPlatformTime::Seconds();
    FirstTickSeconds = 0;
    FirstViewFrames = 0;
    LastFrame = WorstFrame = LogWorstFrame = FVoxelStreamingFrame();
    FirstApplyMs = FirstWorstApplyMs = 0;
    FirstApplied = FirstApplyFrames = 0;
    FirstViewReport.Reset();
    LogApplied = 0;
    LogApplyMs = 0;
    // The nearest columns start now, while the map finishes loading, rather than on the first frame.
    Pipeline->SetFocus(MakeRings(LastFocus), 0);
    Pipeline->Update();
}

FVoxelStreamingRings UVoxelWorldSubsystem::MakeRings(const FVector& Focus) const
{
    const FVector Columns = (Focus - Owner->OriginCm) / StreamingColumnCm;
    return FVoxelStreamingRings(FIntPoint(FMath::FloorToInt32(Columns.X), FMath::FloorToInt32(Columns.Y)),
        FMath::Clamp(VoxelConsole::CVarViewRadius.GetValueOnGameThread(), 1, 64),
        VoxelConsole::CVarUnloadMargin.GetValueOnGameThread());
}

void UVoxelWorldSubsystem::Stop()
{
    // Waits for the jobs in flight; their results go with it.
    Pipeline.Reset();
    for (const TPair<FIntPoint, TObjectPtr<UVoxelChunkComponent>>& Chunk : Chunks)
        if (Chunk.Value) Chunk.Value->DestroyComponent();
    for (const TObjectPtr<UVoxelChunkComponent>& Chunk : Pool)
        if (Chunk) Chunk->DestroyComponent();
    Chunks.Reset();
    Pool.Reset();
}

int32 UVoxelWorldSubsystem::GetSectionMeshCount() const
{
    int32 Count = 0;
    for (const TPair<FIntPoint, TObjectPtr<UVoxelChunkComponent>>& Chunk : Chunks)
        if (Chunk.Value) Count += Chunk.Value->GetSectionMeshCount();
    return Count;
}

void UVoxelWorldSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    const AVoxelWorld* World = Owner.Get();
    if (!Pipeline || !World) return;
    SCOPE_CYCLE_COUNTER(STAT_VoxelStreamingTick);
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelWorldSubsystem_Tick);
    const double Start = FPlatformTime::Seconds();

    // The rings follow the camera, wherever the player looks from, unless a capture pins them.
    double Yaw = 0;
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    if (Controller && Controller->PlayerCameraManager)
    {
        LastFocus = Controller->PlayerCameraManager->GetCameraLocation();
        Yaw = Controller->PlayerCameraManager->GetCameraRotation().Yaw;
    }
    if (FocusOverride) LastFocus = *FocusOverride;
    if (!FirstTickSeconds) FirstTickSeconds = Start;
    if (FirstViewReport.IsEmpty()) ++FirstViewFrames;

    FVoxelStreamingFrame Frame;
    Pipeline->SetMaxTasksInFlight(VoxelConsole::GetMaxTasksInFlight());
    Pipeline->SetEdgeWalls(VoxelConsole::CVarEdgeWalls.GetValueOnGameThread());
    const FVoxelStreamingRings Before = Pipeline->GetRings();
    Pipeline->SetFocus(MakeRings(LastFocus), VoxelStreaming::GetViewSector(Yaw));
    if (!(Pipeline->GetRings() == Before)) UpdateVisibility();
    double Mark = FPlatformTime::Seconds();
    Frame.PlanMs = (Mark - Start) * 1000.;
    Pipeline->Update();
    TArray<FIntPoint> Unloaded;
    Pipeline->TakeUnloaded(Unloaded);
    for (const FIntPoint Column : Unloaded)
        ReleaseChunk(Column);
    const double JobsEnd = FPlatformTime::Seconds();
    Frame.JobsMs = (JobsEnd - Mark) * 1000.;
    ApplyReady(VoxelConsole::CVarApplyBudgetMs.GetValueOnGameThread(), Start);
    Mark = FPlatformTime::Seconds();
    Frame.ApplyMs = (Mark - JobsEnd) * 1000.;
    Frame.Applied = LastFrame.Applied;
    Frame.TickMs = (Mark - Start) * 1000.;
    LastFrame = Frame;
    if (Frame.TickMs > WorstFrame.TickMs) WorstFrame = Frame;
    if (Frame.TickMs > LogWorstFrame.TickMs) LogWorstFrame = Frame;
    if (FirstViewReport.IsEmpty() && Pipeline->IsViewOnScreen()) ReportFirstView();
    UpdateStats();
}

void UVoxelWorldSubsystem::UpdateVisibility()
{
    const FVoxelStreamingRings& Rings = Pipeline->GetRings();
    for (const TPair<FIntPoint, TObjectPtr<UVoxelChunkComponent>>& Chunk : Chunks)
        if (Chunk.Value) Chunk.Value->SetVisibility(Rings.IsInView(Chunk.Key));
}

void UVoxelWorldSubsystem::ApplyReady(double BudgetMs, double TickStartSeconds)
{
    SCOPE_CYCLE_COUNTER(STAT_VoxelApplyColumns);
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelWorldSubsystem_Apply);
    int32 Applied = 0;
    double FrameApplyMs = 0;
    while (const FVoxelColumnMesh* Next = Pipeline->PeekReady())
    {
        const double SpentMs = (FPlatformTime::Seconds() - TickStartSeconds) * 1000.;
        const double Estimate = ApplyCost.Estimate(Next->QuadCount);
        // Stop before the budget, at 85% of it: the rest covers one apply above its estimate and the bookkeeping
        // after the last. A frame's first column always goes, so no column waits forever, however big.
        if (Applied > 0 && SpentMs + Estimate > 0.85 * BudgetMs) break;
        const double ApplyStart = FPlatformTime::Seconds();
        FVoxelColumnPipeline::FReady Ready;
        Pipeline->PopReady(Ready);
        const int32 Quads = Ready.Mesh->QuadCount;
        const uint32 Layers = Ready.Mesh->LayerMask, Sections = Ready.Mesh->SectionMask;
        const bool bReused = Chunks.Contains(Ready.Column), bPooled = !bReused && !Pool.IsEmpty();
        if (Ready.Mesh->LayerMask)
            AcquireChunk(Ready.Column)
                ->SetMesh(MoveTemp(Ready.Mesh->Streams), MoveTemp(Ready.Mesh->Cards), Ready.Mesh->LayerMask,
                    Ready.Mesh->SectionMask);
        else
            ReleaseChunk(Ready.Column);
        // Freeing what the mesh leaves behind is part of applying it.
        Ready.Mesh.Reset();
        const double Ms = (FPlatformTime::Seconds() - ApplyStart) * 1000.;
        if (Ms > .5)
            UE_LOG(LogUnnamedVoxel, Display,
                TEXT("Slow apply: column (%d, %d) took %.2f ms (estimate %.2f): %d quads, layers %x, sections %x, %s"),
                Ready.Column.X, Ready.Column.Y, Ms, Estimate, Quads, Layers, Sections,
                bReused       ? TEXT("its own component")
                    : bPooled ? TEXT("a pooled component")
                              : TEXT("a new component"));
        ApplyCost.Add(Quads, Ms);
        FrameApplyMs += Ms;
        ++Applied;
    }
    LastFrame.Applied = Applied;
    LogApplied += Applied;
    LogApplyMs += FrameApplyMs;
    if (FirstViewReport.IsEmpty() && Applied)
    {
        FirstApplied += Applied;
        FirstApplyMs += FrameApplyMs;
        FirstWorstApplyMs = FMath::Max(FirstWorstApplyMs, FrameApplyMs);
        ++FirstApplyFrames;
    }
}

void UVoxelWorldSubsystem::FApplyCost::Add(int32 QuadCount, double Milliseconds)
{
    // One stall (the thread descheduled, another app busy) must not raise every estimate after it.
    Milliseconds = FMath::Min(Milliseconds, 4 * Estimate(QuadCount) + 1);
    // Older applies fade, so the line follows what the machine does now.
    constexpr double Keep = 0.98;
    const double Q = QuadCount;
    Weight = Weight * Keep + 1;
    Quads = Quads * Keep + Q;
    QuadsSquared = QuadsSquared * Keep + Q * Q;
    Ms = Ms * Keep + Milliseconds;
    QuadsMs = QuadsMs * Keep + Q * Milliseconds;
}

double UVoxelWorldSubsystem::FApplyCost::Estimate(int32 QuadCount) const
{
    // A generous guess until a few applies are measured.
    if (Weight < 8) return 0.1 + 0.001 * QuadCount;
    const double Determinant = Weight * QuadsSquared - Quads * Quads;
    const double PerQuad =
        Determinant > 1e-6 * Weight * QuadsSquared ? FMath::Max(0., (Weight * QuadsMs - Quads * Ms) / Determinant) : 0.;
    const double Fixed = FMath::Max(0., (Ms - PerQuad * Quads) / Weight);
    // The line is a mean; the margin covers most applies above it.
    return 1.5 * (Fixed + PerQuad * QuadCount);
}

UVoxelChunkComponent* UVoxelWorldSubsystem::AcquireChunk(FIntPoint Column)
{
    if (TObjectPtr<UVoxelChunkComponent>* Found = Chunks.Find(Column)) return *Found;
    AVoxelWorld* World = Owner.Get();
    UVoxelChunkComponent* Chunk = Pool.IsEmpty() ? nullptr : Pool.Pop(EAllowShrinking::No).Get();
    if (!Chunk)
    {
        Chunk = NewObject<UVoxelChunkComponent>(World);
        Chunk->SetupAttachment(World->GetRootComponent());
        Chunk->RegisterComponent();
        TArray<UMaterialInterface*> LayerMaterials;
        for (UMaterialInterface* Material : Materials)
            LayerMaterials.Add(Material);
        Chunk->Setup(LayerMaterials);
    }
    Chunk->SetWorldLocation(World->OriginCm + FVector(Column.X, Column.Y, 0) * StreamingColumnCm);
    Chunk->SetVisibility(Pipeline->GetRings().IsInView(Column));
    Chunks.Add(Column, Chunk);
    return Chunk;
}

void UVoxelWorldSubsystem::ReleaseChunk(FIntPoint Column)
{
    TObjectPtr<UVoxelChunkComponent> Chunk;
    if (!Chunks.RemoveAndCopyValue(Column, Chunk) || !Chunk) return;
    // Empty and hidden until a column needs it again.
    Chunk->SetMesh(RealtimeMesh::FRealtimeMeshStreamSet(), FMeshCardsBuildData(), 0, 0);
    Chunk->SetVisibility(false);
    Pool.Add(Chunk);
}

void UVoxelWorldSubsystem::ReportFirstView()
{
    const FVoxelPipelineStats Stats = Pipeline->GetStats();
    const FVoxelStreamingRings& Rings = Pipeline->GetRings();
    FString Report = FString::Printf(TEXT("Streaming: view radius %d (%d columns in view, %d generated), unload beyond "
                                          "%d, %u workers, %d jobs in flight\n"),
        Rings.ViewRadius, Stats.InView, Stats.Needed, Rings.GetUnloadDistance(),
        LowLevelTasks::FScheduler::Get().GetNumWorkers(), VoxelConsole::GetMaxTasksInFlight());
    Report += DescribeStreamingStage(TEXT("Generate columns"), Stats.Generate);
    Report += DescribeStreamingStage(TEXT("Mesh columns"), Stats.Mesh);
    Report += DescribeStreamingStage(TEXT("Stream sets and Lumen cards"), Stats.Streams);
    Report += FString::Printf(
        TEXT("  Quads %lld (%.0f per column on screen); columns %.1f KB each (%.1f MB total); mesh cache %.1f MB\n"),
        Stats.ScreenQuads, Stats.OnScreen ? double(Stats.ScreenQuads) / Stats.OnScreen : 0.,
        Stats.Generated ? double(Stats.ColumnBytes) / Stats.Generated / 1024. : 0.,
        double(Stats.ColumnBytes) / (1024. * 1024.), double(Stats.CacheBytes) / (1024. * 1024.));
    Report += FString::Printf(TEXT("  Lumen cards: %d (%.1f per column, most %d of %d)\n"), Stats.ScreenCards,
        Stats.OnScreen ? double(Stats.ScreenCards) / Stats.OnScreen : 0., Stats.MostCards,
        UVoxelChunkComponent::MaxCards);
    Report +=
        FString::Printf(TEXT("  Apply: %d columns in %.2f ms over %d frames, worst frame %.2f ms; worst chunk work "
                             "%.2f ms (budget %.1f ms)\n"),
            FirstApplied, FirstApplyMs, FirstApplyFrames, FirstWorstApplyMs, WorstFrame.TickMs,
            VoxelConsole::CVarApplyBudgetMs.GetValueOnGameThread());
    Report += FString::Printf(
        TEXT("  Start to view on screen: %.0f ms (first frame to view: %.0f ms over %d frames); %d chunk components "
             "hold %d sections; %lld results dropped"),
        (FPlatformTime::Seconds() - StartSeconds) * 1000., (FPlatformTime::Seconds() - FirstTickSeconds) * 1000.,
        FirstViewFrames, Chunks.Num(), GetSectionMeshCount(), Stats.Dropped);
    TArray<FString> Lines;
    Report.ParseIntoArrayLines(Lines);
    for (const FString& Line : Lines)
        UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Line);
    FirstViewReport = MoveTemp(Report);
}

void UVoxelWorldSubsystem::UpdateStats()
{
    const double Now = FPlatformTime::Seconds();
    const bool bLog = Now - LogSeconds >= 1.;
#if STATS
    const bool bStats = FThreadStats::IsCollectingData();
#else
    const bool bStats = false;
#endif
    if (!bStats && !bLog) return;
    const FVoxelPipelineStats Stats = Pipeline->GetStats();
    SET_DWORD_STAT(STAT_VoxelColumnsGenerated, Stats.Generated);
    SET_DWORD_STAT(STAT_VoxelColumnsNeeded, Stats.Needed);
    SET_DWORD_STAT(STAT_VoxelColumnsInView, Stats.InView);
    SET_DWORD_STAT(STAT_VoxelColumnsOnScreen, Stats.InViewOnScreen);
    SET_DWORD_STAT(STAT_VoxelJobsInFlight, Pipeline->GetJobCount());
    SET_DWORD_STAT(STAT_VoxelMeshesWaiting, Stats.Ready);
    SET_DWORD_STAT(STAT_VoxelColumnsApplied, LastFrame.Applied);
    SET_FLOAT_STAT(STAT_VoxelChunkWorkMs, LastFrame.TickMs);
    SET_MEMORY_STAT(STAT_VoxelColumnMemory, Stats.ColumnBytes);
    SET_MEMORY_STAT(STAT_VoxelCacheMemory, Stats.CacheBytes);
    if (!bLog) return;
    // Only while something streams, so a still world keeps the log quiet.
    if (LogApplied || Pipeline->GetJobCount() || Stats.Ready)
        UE_LOG(LogUnnamedVoxel, Display,
            TEXT("Streaming: center (%d, %d), %d of %d in view on screen, %d generated (%d needed), %d jobs, %d "
                 "waiting; applied %d in %.2f ms, worst frame %.2f ms (plan %.2f, jobs %.2f, apply %.2f; budget %.1f); "
                 "columns %.1f MB, cache %.1f MB, "
                 "%lld dropped"),
            Pipeline->GetRings().Center.X, Pipeline->GetRings().Center.Y, Stats.InViewOnScreen, Stats.InView,
            Stats.Generated, Stats.Needed, Pipeline->GetJobCount(), Stats.Ready, LogApplied, LogApplyMs,
            LogWorstFrame.TickMs, LogWorstFrame.PlanMs, LogWorstFrame.JobsMs, LogWorstFrame.ApplyMs,
            VoxelConsole::CVarApplyBudgetMs.GetValueOnGameThread(), double(Stats.ColumnBytes) / (1024. * 1024.),
            double(Stats.CacheBytes) / (1024. * 1024.), Stats.Dropped);
    LogSeconds = Now;
    LogApplied = 0;
    LogApplyMs = 0;
    LogWorstFrame = FVoxelStreamingFrame();
}
