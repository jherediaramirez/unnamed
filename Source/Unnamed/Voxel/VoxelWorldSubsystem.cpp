#include "Voxel/VoxelWorldSubsystem.h"
#include "Async/Fundamental/Scheduler.h"
#include "Biome/VoxelBiomeSource.h"
#include "Camera/PlayerCameraManager.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Engine/World.h"
#include "Terrain/VoxelTerrain.h"
#include "Foundation/FoundationProfile.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Voxel/VoxelChunkComponent.h"
#include "Voxel/VoxelConsole.h"
#include "Voxel/VoxelLights.h"
#include "Voxel/VoxelFluidSystem.h"
#include "Voxel/VoxelWorld.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"
#include "VoxelStats.h"

namespace
{
    TAutoConsoleVariable<int32> CVarModels(TEXT("voxel.Models"), 1,
        TEXT("1 draws model states' baked quads (plants, multiface, custom models); 0 leaves them out (A/B). A change ")
            TEXT("remeshes every loaded column."));
    TAutoConsoleVariable<int32> CVarModelShadeUnturned(TEXT("voxel.Debug.ModelShadeUnturned"), 0,
        TEXT("Look call: 1 shades model quads whose shade direction differs from their facing as up, as an override ")
            TEXT(
                "left unturned by the blockstate rotation would (ceiling glow lichen). Remeshes every loaded column."));
}

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
DECLARE_DWORD_COUNTER_STAT(TEXT("Far columns wanted"), STAT_VoxelFarWanted, STATGROUP_Voxel);
DECLARE_DWORD_COUNTER_STAT(TEXT("Far meshes on screen"), STAT_VoxelFarOnScreen, STATGROUP_Voxel);
DECLARE_DWORD_COUNTER_STAT(TEXT("Far quads on screen"), STAT_VoxelFarQuads, STATGROUP_Voxel);
DECLARE_DWORD_COUNTER_STAT(TEXT("Far meshes applied this frame"), STAT_VoxelFarApplied, STATGROUP_Voxel);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Far apply this frame (ms)"), STAT_VoxelFarApplyMs, STATGROUP_Voxel);
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
    TConstArrayView<UMaterialInstanceDynamic*> FarLayerMaterials, const FVector& InitialFocus)
{
    Stop();
    Owner = &InOwner;
    Materials.Reset();
    for (UMaterialInterface* Material : LayerMaterials)
        Materials.Add(Material);
    FarFadeMaterials.Reset();
    for (UMaterialInstanceDynamic* Material : FarLayerMaterials)
        FarFadeMaterials.Add(Material);
    bFarLookStale = true;
    FVoxelColumnPipeline::FSettings Settings;
    Settings.MeshOptions.bGreedy = VoxelConsole::CVarGreedyMeshing.GetValueOnGameThread();
    Settings.MeshOptions.bAmbientOcclusion = VoxelConsole::CVarAmbientOcclusion.GetValueOnGameThread();
    Settings.MeshOptions.TintBlend = VoxelConsole::GetTintBlend();
    Settings.MeshOptions.bModels = CVarModels.GetValueOnGameThread() != 0;
    Settings.MeshOptions.bModelShadeUnturned = CVarModelShadeUnturned.GetValueOnGameThread() != 0;
    Settings.MeshOptions.OreHost = VoxelConsole::GetOreHost();
    Settings.MeshOptions.OreGlint = VoxelConsole::GetOreGlint();
    Settings.MeshOptions.SlopedTops = FVoxelFluidSystem::GetSlopedTops();
    Settings.MeshOptions.bOpenCorners = FVoxelFluidSystem::GetOpenCorners();
    Settings.MaxTasksInFlight = VoxelConsole::GetMaxTasksInFlight();
    Settings.bEdgeWalls = VoxelConsole::CVarEdgeWalls.GetValueOnGameThread();
    bDecoration = VoxelConsole::CVarDecoration.GetValueOnGameThread();
    bFreezeLast = VoxelConsole::CVarDecorationFreezeLast.GetValueOnGameThread();
    bPlants = VoxelConsole::CVarDecorationPlants.GetValueOnGameThread();
    bTrees = VoxelConsole::CVarDecorationTrees.GetValueOnGameThread();
    // The terrain read voxel.OreVeins when it was made (VoxelTerrainFactory), this frame.
    bOreVeins = VoxelConsole::CVarOreVeins.GetValueOnGameThread();
    UE_LOG(LogUnnamedVoxel, Display, TEXT("voxel.OreVeins: %s"), bOreVeins ? TEXT("on") : TEXT("off"));
    if (bDecoration && Terrain->GetFeatures() && Terrain->GetBiomeSource())
    {
        // Streaming decorates (phase 6, S3): Generate, Decorate and Finalize before a column meshes.
        const TSharedRef<FVoxelDecorator> Decorator = MakeShared<FVoxelDecorator>();
        FVoxelDecorationOptions Options;
        Options.bFreezeLast = bFreezeLast;
        Options.bPlants = bPlants;
        Options.bTrees = bTrees;
        FString Error;
        if (Decorator->Initialize(*Terrain->GetFeatures(), *Table, Terrain->GetBiomeSource()->GetTable(),
                Terrain->GetSeed(), Options, &Error))
        {
            Settings.Decorator = Decorator;
            UE_LOG(LogUnnamedVoxel, Display, TEXT("voxel.Decoration: %s; freeze pass %s; plants %s; trees %s"),
                *Decorator->Describe(), bFreezeLast ? TEXT("at Finalize") : TEXT("inside the snapshot (S2's order)"),
                bPlants ? TEXT("on") : TEXT("off"), bPlants && bTrees ? TEXT("on") : TEXT("off"));
        }
        else
            UE_LOG(LogUnnamedVoxel, Warning, TEXT("voxel.Decoration: no decoration for this terrain (%s)"), *Error);
    }
    else if (bDecoration)
        UE_LOG(LogUnnamedVoxel, Display, TEXT("voxel.Decoration: this terrain has no decoration data or biomes"));
    Pipeline = MakeUnique<FVoxelColumnPipeline>(MoveTemp(Table), MoveTemp(Terrain), Settings);
    // Mesh jobs scan their column's light sources for the owner's light pool (FVoxelColumnMesh::Lights).
    Pipeline->SetLightTable(InOwner.GetLightTable());
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
    LogFarApplied = 0;
    LogFarApplyMs = 0;
    NearViewSeconds = 0;
    // The nearest columns start now, while the map finishes loading, rather than on the first frame.
    Pipeline->SetFocus(MakeRings(LastFocus), 0);
    Pipeline->Update();
}

FVoxelStreamingRings UVoxelWorldSubsystem::MakeRings(const FVector& Focus) const
{
    const FVector Columns = (Focus - Owner->OriginCm) / StreamingColumnCm;
    return FVoxelStreamingRings(FIntPoint(FMath::FloorToInt32(Columns.X), FMath::FloorToInt32(Columns.Y)),
        FMath::Clamp(VoxelConsole::CVarViewRadius.GetValueOnGameThread(), 1, 64),
        VoxelConsole::CVarUnloadMargin.GetValueOnGameThread(), 1,
        FMath::Clamp(VoxelConsole::CVarFarRadius.GetValueOnGameThread(), 0, 64));
}

void UVoxelWorldSubsystem::Stop()
{
    // Waits for the jobs in flight; their results go with it.
    Pipeline.Reset();
    // No column is on screen any more, so none gives light.
    if (AVoxelWorld* World = Owner.Get())
        if (FVoxelLights* Lights = World->GetLights()) Lights->Reset();
    for (const TPair<FIntPoint, TObjectPtr<UVoxelChunkComponent>>& Chunk : Chunks)
        if (Chunk.Value) Chunk.Value->DestroyComponent();
    for (const TObjectPtr<UVoxelChunkComponent>& Chunk : Pool)
        if (Chunk) Chunk->DestroyComponent();
    Chunks.Reset();
    Pool.Reset();
    PendingReleases.Reset();
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

    // Decoration or the ore veins switched on or off, or the freeze moved (captures pass them with -ExecCmds, after the
    // world started), rebuilds the world.
    if ((VoxelConsole::CVarDecoration.GetValueOnGameThread() != bDecoration ||
            VoxelConsole::CVarDecorationFreezeLast.GetValueOnGameThread() != bFreezeLast ||
            VoxelConsole::CVarDecorationPlants.GetValueOnGameThread() != bPlants ||
            VoxelConsole::CVarDecorationTrees.GetValueOnGameThread() != bTrees ||
            VoxelConsole::CVarOreVeins.GetValueOnGameThread() != bOreVeins) &&
        Owner.IsValid())
    {
        Owner->Regenerate(Owner->GetSeed());
        return;
    }
    FVoxelStreamingFrame Frame;
    Pipeline->SetMaxTasksInFlight(VoxelConsole::GetMaxTasksInFlight());
    Pipeline->SetEdgeWalls(VoxelConsole::CVarEdgeWalls.GetValueOnGameThread());
    Pipeline->SetTintBlend(VoxelConsole::GetTintBlend());
    Pipeline->SetModelOptions(
        CVarModels.GetValueOnGameThread() != 0, CVarModelShadeUnturned.GetValueOnGameThread() != 0);
    Pipeline->SetOreLooks(VoxelConsole::GetOreHost(), VoxelConsole::GetOreGlint());
    Pipeline->SetSlopedTops(FVoxelFluidSystem::GetSlopedTops(), FVoxelFluidSystem::GetOpenCorners());
    VoxelFarMesher::FOptions FarOptions;
    FarOptions.CanopyLayer =
        VoxelConsole::CVarFarCanopyLayer.GetValueOnGameThread() == 1 ? EVoxelLayer::Opaque : EVoxelLayer::Cutout;
    Pipeline->SetFarOptions(FarOptions);
    Pipeline->SetFarInnerFirst(VoxelConsole::CVarFarInnerFirst.GetValueOnGameThread());
    UpdateFarLooks();
    const FVoxelStreamingRings Before = Pipeline->GetRings();
    const double FocusStart = FPlatformTime::Seconds();
    Pipeline->SetFocus(MakeRings(LastFocus), VoxelStreaming::GetViewSector(Yaw));
    const double FocusEnd = FPlatformTime::Seconds();
    if (!(Pipeline->GetRings() == Before)) UpdateVisibility(Before);
    double Mark = FPlatformTime::Seconds();
    Frame.FocusMs = (FocusEnd - FocusStart) * 1000.;
    Frame.VisibilityMs = (Mark - FocusEnd) * 1000.;
    Frame.PlanMs = (Mark - Start) * 1000.;
    Pipeline->Update();
    Frame.WalkMs = Pipeline->GetLastWalk().Ms;
    Frame.Walked = Pipeline->GetLastWalk().Visited;
    const double ReleaseStart = FPlatformTime::Seconds();
    TArray<FIntPoint> Unloaded;
    Pipeline->TakeUnloaded(Unloaded);
    // Off screen, an unloaded column gives no light, as before. Its component is hidden already (beyond the drawn
    // radius) and is released in the time the budget leaves.
    FVoxelLights* Lights = Owner.IsValid() ? Owner->GetLights() : nullptr;
    for (const FIntPoint Column : Unloaded)
    {
        if (Lights) Lights->ClearColumn(Column);
        if (Chunks.Contains(Column)) PendingReleases.Add(Column);
    }
    const double JobsEnd = FPlatformTime::Seconds();
    Frame.JobsMs = (JobsEnd - Mark) * 1000.;
    Frame.ReleaseMs = (JobsEnd - ReleaseStart) * 1000.;
    const double BudgetMs = VoxelConsole::CVarApplyBudgetMs.GetValueOnGameThread();
    ApplyReady(BudgetMs, Start);
    const double ApplyEnd = FPlatformTime::Seconds();
    Frame.ApplyMs = (ApplyEnd - JobsEnd) * 1000.;
    ReleasePending(BudgetMs, Start);
    Mark = FPlatformTime::Seconds();
    Frame.PendingMs = (Mark - ApplyEnd) * 1000.;
    Frame.Applied = LastFrame.Applied;
    Frame.FarApplied = LastFrame.FarApplied;
    Frame.FarApplyMs = LastFrame.FarApplyMs;
    Frame.TickMs = (Mark - Start) * 1000.;
    LastFrame = Frame;
    if (Frame.TickMs > WorstFrame.TickMs) WorstFrame = Frame;
    if (Frame.TickMs > LogWorstFrame.TickMs) LogWorstFrame = Frame;
    if (!NearViewSeconds && Pipeline->IsNearViewOnScreen()) NearViewSeconds = FPlatformTime::Seconds();
    if (FirstViewReport.IsEmpty() && Pipeline->IsViewOnScreen()) ReportFirstView();
    UpdateStats();
}

void UVoxelWorldSubsystem::UpdateVisibility(const FVoxelStreamingRings& Before)
{
    const FVoxelStreamingRings& Rings = Pipeline->GetRings();
    const int32 Drawn = Rings.GetDrawnRadius();
    // A chunk waiting for release stays hidden, even if its column is drawn again: its old mesh is not the new
    // record's.
    const auto IsShown = [this, &Rings](FIntPoint Column)
    { return Rings.IsDrawn(Column) && !PendingReleases.IsWaiting(Column); };
    if (Before.GetDrawnRadius() != Drawn)
    {
        for (const TPair<FIntPoint, TObjectPtr<UVoxelChunkComponent>>& Chunk : Chunks)
            if (Chunk.Value) Chunk.Value->SetVisibility(IsShown(Chunk.Key));
        return;
    }
    // Every chunk's visibility matched Before (AcquireChunk sets it, and so does every move): only the columns drawn by
    // one rings and not the other change.
    const auto Refresh = [this, &IsShown](FIntPoint Column)
    {
        if (const TObjectPtr<UVoxelChunkComponent>* Chunk = Chunks.Find(Column); Chunk && *Chunk)
            (*Chunk)->SetVisibility(IsShown(Column));
    };
    if (Before.Center == Rings.Center) return;
    VoxelStreaming::ForEachLeft(Before.Center, Rings.Center, Drawn, Refresh);
    VoxelStreaming::ForEachLeft(Rings.Center, Before.Center, Drawn, Refresh);
}

void UVoxelWorldSubsystem::ReleasePending(double BudgetMs, double TickStartSeconds)
{
    FIntPoint Column;
    for (int32 Released = 0; (!Released || (FPlatformTime::Seconds() - TickStartSeconds) * 1000. < 0.85 * BudgetMs) &&
        PendingReleases.Pop(Column);)
        Released += ReleaseChunk(Column) ? 1 : 0;
}

UMaterialInterface* UVoxelWorldSubsystem::GetFarTintMaterial()
{
    if (!VoxelConsole::CVarFarTint.GetValueOnGameThread()) return nullptr;
    if (!FarTintMaterial)
        if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(
                nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
        {
            FarTintMaterial = UMaterialInstanceDynamic::Create(Base, this);
            FarTintMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.f, 1.f));
        }
    return FarTintMaterial;
}

void UVoxelWorldSubsystem::UpdateFarLooks()
{
    const bool bShadows = VoxelConsole::CVarFarShadows.GetValueOnGameThread();
    const bool bTint = VoxelConsole::CVarFarTint.GetValueOnGameThread();
    const FVoxelStreamingRings& Rings = Pipeline->GetRings();
    const bool bFade =
        VoxelEdgeFog::UsesHaze(VoxelConsole::CVarFogHaze.GetValueOnGameThread(), Rings.ViewRadius, Rings.FarRadius);
    // Shots from outside the world (the edge fog hidden) see every far mesh whole.
    const AVoxelWorld* World = Owner.Get();
    const VoxelEdgeFog::FFade Fade = World && !World->IsEdgeFogVisible()
        ? VoxelEdgeFog::GetNoFade()
        : VoxelEdgeFog::GetFarFade(Rings.GetDrawnRadius());
    if (bFarLookStale || !(Fade == FarFade))
    {
        FarFade = Fade;
        for (UMaterialInstanceDynamic* Material : FarFadeMaterials)
            if (Material)
            {
                Material->SetScalarParameterValue(TEXT("FarFadeStart"), float(Fade.StartCm));
                Material->SetScalarParameterValue(TEXT("FarFadeEnd"), float(Fade.EndCm));
            }
    }
    if (!bFarLookStale && bShadows == bFarShadows && bTint == bFarTint && bFade == bFarFade) return;
    bFarLookStale = false;
    bFarShadows = bShadows;
    bFarTint = bTint;
    bFarFade = bFade;
    FarLook.Reset();
    if (UMaterialInterface* Tint = GetFarTintMaterial())
        FarLook.Init(Tint, int32(EVoxelLayer::Count));
    else if (bFarFade)
        for (UMaterialInstanceDynamic* Material : FarFadeMaterials)
            FarLook.Add(Material);
    for (const TPair<FIntPoint, TObjectPtr<UVoxelChunkComponent>>& Chunk : Chunks)
        if (Chunk.Value && Chunk.Value->IsFarLook()) Chunk.Value->SetFarLook(true, bFarShadows, FarLook);
}

EVoxelApplyStep GetVoxelApplyStep(
    double SpentMs, double EstimateMs, double BudgetMs, int32 Applied, bool bFar, bool bWaited)
{
    // Stop before the budget, at 85% of it: the rest covers one apply above its estimate and the bookkeeping after the
    // last.
    if (SpentMs + EstimateMs <= 0.85 * BudgetMs) return EVoxelApplyStep::Apply;
    // Far meshes go only in the room near ones leave, except one turned down as a frame's first apply: after one
    // frame's wait it goes first, whatever its estimate (an estimate above 85% of the budget would otherwise hold it,
    // and the near meshes behind it, forever).
    if (bFar) return Applied == 0 && bWaited ? EVoxelApplyStep::Apply : EVoxelApplyStep::SkipFar;
    if (Applied > 0) return EVoxelApplyStep::Stop;
    // A frame's first near column goes anyway, so none waits forever, however big; but one that fits the budget alone
    // and that the frame's planning and jobs leave no room for waits one frame, once, and goes first when it heads the
    // queue again.
    if (SpentMs + EstimateMs > BudgetMs && EstimateMs <= BudgetMs && !bWaited) return EVoxelApplyStep::Wait;
    return EVoxelApplyStep::Apply;
}

void UVoxelWorldSubsystem::ApplyReady(double BudgetMs, double TickStartSeconds)
{
    SCOPE_CYCLE_COUNTER(STAT_VoxelApplyColumns);
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelWorldSubsystem_Apply);
    int32 Applied = 0, FarApplied = 0;
    double FrameApplyMs = 0, FarApplyMs = 0;
    FIntPoint Head;
    // Set once a far mesh is turned down: the rest of the frame takes near meshes only, so a far mesh heading the
    // queue (an urgent one goes before near meshes) never holds them back.
    bool bNearOnly = false;
    while (const FVoxelColumnMesh* Next = Pipeline->PeekReady(&Head, bNearOnly))
    {
        const double SpentMs = (FPlatformTime::Seconds() - TickStartSeconds) * 1000.;
        // Far meshes (no cards, one stream set) keep their own fit, so neither kind skews the other's estimates.
        FApplyCost& Cost = Next->bFar ? FarApplyCost : ApplyCost;
        const double Estimate = Cost.Estimate(Next->QuadCount);
        const TOptional<FIntPoint>& Waited = Next->bFar ? WaitedFarColumn : WaitedColumn;
        const EVoxelApplyStep Step =
            GetVoxelApplyStep(SpentMs, Estimate, BudgetMs, Applied, Next->bFar, Waited && *Waited == Head);
        if (Step == EVoxelApplyStep::Stop) break;
        if (Step == EVoxelApplyStep::Wait)
        {
            WaitedColumn = Head;
            break;
        }
        if (Step == EVoxelApplyStep::SkipFar)
        {
            if (Applied == 0) WaitedFarColumn = Head;
            bNearOnly = true;
            continue;
        }
        const double ApplyStart = FPlatformTime::Seconds();
        FVoxelColumnPipeline::FReady Ready;
        Pipeline->PopReady(Ready, bNearOnly);
        const int32 Quads = Ready.Mesh->QuadCount;
        const uint32 Layers = Ready.Mesh->LayerMask, Sections = Ready.Mesh->SectionMask;
        const bool bReused = Chunks.Contains(Ready.Column), bPooled = !bReused && !Pool.IsEmpty();
        const bool bFar = Ready.Mesh->bFar;
        if (Ready.Mesh->LayerMask)
        {
            UVoxelChunkComponent* Chunk = AcquireChunk(Ready.Column);
            Chunk->SetFarLook(bFar, bFarShadows, FarLook);
            Chunk->SetMesh(MoveTemp(Ready.Mesh->Streams), MoveTemp(Ready.Mesh->Cards), Ready.Mesh->LayerMask,
                Ready.Mesh->SectionMask);
        }
        else
            ReleaseChunk(Ready.Column);
        // The column's light sources go to the light pool with its mesh (none when the mesh is empty).
        if (AVoxelWorld* World = Owner.Get())
            if (FVoxelLights* Lights = World->GetLights())
                Lights->SetColumn(Ready.Column, MoveTemp(Ready.Mesh->Lights));
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
        Cost.Add(Quads, Ms);
        FrameApplyMs += Ms;
        ++Applied;
        if (bFar)
        {
            ++FarApplied;
            FarApplyMs += Ms;
        }
    }
    LastFrame.Applied = Applied;
    LastFrame.FarApplied = FarApplied;
    LastFrame.FarApplyMs = FarApplyMs;
    LogFarApplied += FarApplied;
    LogFarApplyMs += FarApplyMs;
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
    // A column that unloaded and came back before its release keeps its component, hidden while it waited, and no
    // longer waits; the caller's mesh replaces the old one.
    const bool bWaited = PendingReleases.IsWaiting(Column);
    PendingReleases.Remove(Column);
    if (TObjectPtr<UVoxelChunkComponent>* Found = Chunks.Find(Column))
    {
        if (bWaited && *Found) (*Found)->SetVisibility(Pipeline->GetRings().IsDrawn(Column));
        return *Found;
    }
    // With the pool empty, the oldest waiting column gives up its component before a new one is made.
    FIntPoint Oldest;
    while (Pool.IsEmpty() && PendingReleases.Pop(Oldest))
        ReleaseChunk(Oldest);
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
    Chunk->SetVisibility(Pipeline->GetRings().IsDrawn(Column));
    Chunks.Add(Column, Chunk);
    return Chunk;
}

bool UVoxelWorldSubsystem::ReleaseChunk(FIntPoint Column)
{
    PendingReleases.Remove(Column);
    // Off screen, the column gives no light.
    if (AVoxelWorld* World = Owner.Get())
        if (FVoxelLights* Lights = World->GetLights()) Lights->ClearColumn(Column);
    TObjectPtr<UVoxelChunkComponent> Chunk;
    if (!Chunks.RemoveAndCopyValue(Column, Chunk) || !Chunk) return false;
    // Empty and hidden until a column needs it again.
    Chunk->SetMesh(RealtimeMesh::FRealtimeMeshStreamSet(), FMeshCardsBuildData(), 0, 0);
    Chunk->SetVisibility(false);
    Pool.Add(Chunk);
    return true;
}

void UVoxelWorldSubsystem::ReportFirstView()
{
    const FVoxelPipelineStats Stats = Pipeline->GetStats();
    const FVoxelStreamingRings& Rings = Pipeline->GetRings();
    FString Report = FString::Printf(TEXT("Streaming: view radius %d (%d columns in view, %d generated), unload beyond "
                                          "%d, %u workers, %d jobs in flight\n"),
        Rings.ViewRadius, Stats.InView, Stats.Needed, Rings.GetUnloadDistance(),
        LowLevelTasks::FScheduler::Get().GetNumWorkers(), VoxelConsole::GetMaxTasksInFlight());
    Report += FString::Printf(TEXT("  Rings: %d in view, %d final, %d decorated, %d generated (decoration %s)\n"),
        Stats.NeededPerRing[0], Stats.NeededPerRing[1], Stats.NeededPerRing[2], Stats.NeededPerRing[3],
        Pipeline->IsDecorating() ? (bFreezeLast ? TEXT("on") : TEXT("on, freeze inside the snapshot")) : TEXT("off"));
    Report += DescribeStreamingStage(TEXT("Generate columns"), Stats.Generate);
    if (Pipeline->IsDecorating())
    {
        Report += DescribeStreamingStage(TEXT("Decorate columns"), Stats.Decorate);
        Report += DescribeStreamingStage(TEXT("Finalize columns"), Stats.Finalize);
        Report += FString::Printf(TEXT("  Worker time to view: %.0f ms (Generate %.0f, Decorate %.0f, Finalize %.0f, "
                                       "Mesh %.0f, streams %.0f)\n"),
            Stats.Generate.TotalMs + Stats.Decorate.TotalMs + Stats.Finalize.TotalMs + Stats.Mesh.TotalMs +
                Stats.Streams.TotalMs,
            Stats.Generate.TotalMs, Stats.Decorate.TotalMs, Stats.Finalize.TotalMs, Stats.Mesh.TotalMs,
            Stats.Streams.TotalMs);
        Report += FString::Printf(TEXT("  Held: %d snapshots (%.1f MB), %d buckets (%.1f MB)\n"), Stats.Snapshots,
            double(Stats.SnapshotBytes) / (1024. * 1024.), Stats.Buckets, double(Stats.BucketBytes) / (1024. * 1024.));
    }
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
    // Far LOD: when the near view was on screen, and the far ring (without one, the near view is the whole view).
    if (Rings.GetDrawnRadius() > Rings.ViewRadius)
    {
        Report += FString::Printf(TEXT("\n  Start to near view on screen: %.0f ms"),
            ((NearViewSeconds ? NearViewSeconds : FPlatformTime::Seconds()) - StartSeconds) * 1000.);
        Report += FString::Printf(TEXT("\n  Far ring: radius %d, %d far columns, %d far meshes on screen, %lld quads "
                                       "(%.0f per far column)\n"),
            Rings.FarRadius, Stats.FarWanted, Stats.FarOnScreen, Stats.FarQuads,
            Stats.FarOnScreen ? double(Stats.FarQuads) / Stats.FarOnScreen : 0.);
        Report += DescribeStreamingStage(TEXT("Far meshes (summaries, mesh and streams)"), Stats.FarMesh);
        Report.RemoveFromEnd(TEXT("\n"));
    }
    TArray<FString> Lines;
    Report.ParseIntoArrayLines(Lines);
    for (const FString& Line : Lines)
        UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Line);
    FirstViewReport = MoveTemp(Report);
}

FString UVoxelWorldSubsystem::DescribeStages() const
{
    if (!Pipeline) return TEXT("Streaming: no voxel world is running");
    const FVoxelPipelineStats Stats = Pipeline->GetStats();
    const FVoxelStreamingRings& Rings = Pipeline->GetRings();
    FString Out = FString::Printf(TEXT("Streaming: center (%d, %d), decoration %s; rings %d in view, %d final, %d "
                                       "decorated, %d generated; unload beyond %d\n"),
        Rings.Center.X, Rings.Center.Y,
        Pipeline->IsDecorating() ? (bFreezeLast ? TEXT("on") : TEXT("on (freeze inside the snapshot)")) : TEXT("off"),
        Stats.NeededPerRing[0], Stats.NeededPerRing[1], Stats.NeededPerRing[2], Stats.NeededPerRing[3],
        Rings.GetUnloadDistance());
    Out += FString::Printf(TEXT("  Records %d: %d final (%.1f MB), %d snapshots (%.1f MB), %d decorated with %d "
                                "buckets held (%.1f MB); jobs %d (generating %d, decorating %d, finalizing %d, "
                                "meshing %d)\n"),
        Stats.Columns, Stats.Generated, double(Stats.ColumnBytes) / (1024. * 1024.), Stats.Snapshots,
        double(Stats.SnapshotBytes) / (1024. * 1024.), Stats.Decorated, Stats.Buckets,
        double(Stats.BucketBytes) / (1024. * 1024.), Pipeline->GetJobCount(), Stats.Generating, Stats.Decorating,
        Stats.Finalizing, Stats.Meshing);
    Out += DescribeStreamingStage(TEXT("Generate"), Stats.Generate);
    Out += DescribeStreamingStage(TEXT("Decorate"), Stats.Decorate);
    Out += DescribeStreamingStage(TEXT("Finalize"), Stats.Finalize);
    Out += DescribeStreamingStage(TEXT("Mesh"), Stats.Mesh);
    if (Rings.GetDrawnRadius() > Rings.ViewRadius)
    {
        Out += FString::Printf(TEXT("  Far ring: radius %d (drawn %d), %d far columns wanted, %d far meshes on screen, "
                                    "%lld far quads, %d in view still coarse, %d far jobs, %d far meshes waiting\n"),
            Rings.FarRadius, Rings.GetDrawnRadius(), Stats.FarWanted, Stats.FarOnScreen, Stats.FarQuads, Stats.Coarse,
            Stats.FarMeshing, Stats.FarReady);
        Out += DescribeStreamingStage(TEXT("Far mesh"), Stats.FarMesh);
    }
    Out += FString::Printf(TEXT("  Recompute-on-miss: %lld snapshots regenerated, %lld Decorates run again; %lld "
                                "buckets aimed at final columns dropped; %lld results dropped, %lld jobs cancelled"),
        Stats.Regenerated, Stats.Redecorated, Stats.BucketsDropped, Stats.Dropped, Stats.Cancelled);
    return Out;
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
    SET_DWORD_STAT(STAT_VoxelFarWanted, Stats.FarWanted);
    SET_DWORD_STAT(STAT_VoxelFarOnScreen, Stats.FarOnScreen);
    SET_DWORD_STAT(STAT_VoxelFarQuads, uint32(Stats.FarQuads));
    SET_DWORD_STAT(STAT_VoxelFarApplied, LastFrame.FarApplied);
    SET_FLOAT_STAT(STAT_VoxelFarApplyMs, LastFrame.FarApplyMs);
    if (!bLog) return;
    // Only while something streams, so a still world keeps the log quiet.
    // Decoration's stages go at the end, so readers of the earlier fields (the flight) keep working.
    const FString Stages = Pipeline->IsDecorating()
        ? FString::Printf(TEXT("; stages: %d snapshots %.1f MB, %d decorated, %d buckets %.1f MB, regenerated %lld, "
                               "redecorated %lld, buckets dropped %lld, cancelled %lld"),
              Stats.Snapshots, double(Stats.SnapshotBytes) / (1024. * 1024.), Stats.Decorated, Stats.Buckets,
              double(Stats.BucketBytes) / (1024. * 1024.), Stats.Regenerated, Stats.Redecorated, Stats.BucketsDropped,
              Stats.Cancelled)
        : FString();
    // Far LOD's counters go last, and only with a far ring.
    const FString Far = Stats.FarWanted || Stats.FarOnScreen
        ? FString::Printf(TEXT("; far: %d of %d on screen, %lld quads, %d coarse, %d jobs, %d waiting, applied %d in "
                               "%.2f ms"),
              Stats.FarOnScreen, Stats.FarWanted, Stats.FarQuads, Stats.Coarse, Stats.FarMeshing, Stats.FarReady,
              LogFarApplied, LogFarApplyMs)
        : FString();
    if (LogApplied || Pipeline->GetJobCount() || Stats.Ready)
        UE_LOG(LogUnnamedVoxel, Display,
            TEXT("Streaming: center (%d, %d), %d of %d in view on screen, %d generated (%d needed), %d jobs, %d "
                 "waiting; applied %d in %.2f ms, worst frame %.2f ms (plan %.2f, jobs %.2f, apply %.2f; budget %.1f); "
                 "columns %.1f MB, cache %.1f MB, "
                 "%lld dropped%s%s"),
            Pipeline->GetRings().Center.X, Pipeline->GetRings().Center.Y, Stats.InViewOnScreen, Stats.InView,
            Stats.Generated, Stats.Needed, Pipeline->GetJobCount(), Stats.Ready, LogApplied, LogApplyMs,
            LogWorstFrame.TickMs, LogWorstFrame.PlanMs, LogWorstFrame.JobsMs, LogWorstFrame.ApplyMs,
            VoxelConsole::CVarApplyBudgetMs.GetValueOnGameThread(), double(Stats.ColumnBytes) / (1024. * 1024.),
            double(Stats.CacheBytes) / (1024. * 1024.), Stats.Dropped, *Stages, *Far);
    LogSeconds = Now;
    LogApplied = 0;
    LogApplyMs = 0;
    LogFarApplied = 0;
    LogFarApplyMs = 0;
    LogWorstFrame = FVoxelStreamingFrame();
}
