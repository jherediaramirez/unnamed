#include "Voxel/VoxelFlight.h"
#include "Camera/CameraComponent.h"
#include "ConvexVolume.h"
#include "DynamicRHI.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/PlatformMemory.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"
#include "Voxel/VoxelChunkComponent.h"
#include "Voxel/VoxelConsole.h"
#include "Voxel/VoxelFlyCamera.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelWorldSubsystem.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

namespace
{
    constexpr double FlightBlockCm = UFoundationProfile::BlockCm;
    constexpr double FlightColumnCm = VoxelConstants::ColumnSize * FlightBlockCm;
    // Above the stand-in terrain's peaks (about 165), looking down ahead.
    constexpr double FlightCruiseBlocks = 180;
    constexpr double FlightPitch = -20;

    double GetFlightMb(SIZE_T Bytes)
    {
        return double(Bytes) / (1024. * 1024.);
    }

    // In the frustum: holes, columns in view never on screen closer than ViewRadius - 1.5 columns (as a gap; where the
    // edge fog started before far LOD), and late columns past that. Far LOD adds coarse columns, in view and still
    // showing their far mesh, and far holes, far columns with nothing on screen closer than where the fog starts now
    // (D - 1.5).
    void CountFlightHoles(const FVoxelColumnPipeline& Pipeline, const FConvexVolume& Frustum, const FVector& OriginCm,
        TArray<FIntPoint>& Holes, int32& Late, int32& Coarse, int32& FarHoles)
    {
        const FVoxelStreamingRings& Rings = Pipeline.GetRings();
        const double Clear = Rings.ViewRadius - 1.5;
        const int32 Drawn = Rings.GetDrawnRadius();
        const double FarClear = Drawn - 1.5;
        const FVector Extent(FlightColumnCm / 2, FlightColumnCm / 2, VoxelConstants::Height * FlightBlockCm / 2);
        const double MiddleZ = (VoxelConstants::MinZ + VoxelConstants::Height / 2.) * FlightBlockCm;
        Late = Coarse = FarHoles = 0;
        for (int32 DY = -Drawn - 1; DY <= Drawn + 1; ++DY)
            for (int32 DX = -Drawn - 1; DX <= Drawn + 1; ++DX)
            {
                const FIntPoint Column = Rings.Center + FIntPoint(DX, DY);
                if (!Rings.IsDrawn(Column)) continue;
                const FVoxelColumnStatus Status = Pipeline.GetStatus(Column);
                const bool bInView = Rings.IsInView(Column);
                if (Status.bOnScreen && !(bInView && Status.Shown == EVoxelMeshKind::Far)) continue;
                const FVector Center =
                    OriginCm + FVector((Column.X + .5) * FlightColumnCm, (Column.Y + .5) * FlightColumnCm, MiddleZ);
                if (!Frustum.IntersectBox(Center, Extent)) continue;
                if (!bInView)
                {
                    FarHoles += Rings.GetGapSquared(Column) <= FarClear * FarClear;
                    continue;
                }
                if (Status.bOnScreen)
                {
                    ++Coarse;
                    continue;
                }
                if (Rings.GetGapSquared(Column) <= Clear * Clear)
                    Holes.Add(Column);
                else
                    ++Late;
            }
    }
}

FVoxelFlight::FVoxelFlight(const FString& InFolder, double InEndSeconds) : Folder(InFolder), EndSeconds(InEndSeconds)
{
    // Circle legs get their duration from the circle's size when the flight starts.
    Legs = {
        {TEXT("hover"), 0, 2, false, TEXT("flight-start")},
        {TEXT("east 20"), 20, 8, false, nullptr},
        {TEXT("east 40"), 40, 8, false, nullptr},
        {TEXT("east 80"), 80, 8, false, TEXT("flight-80")},
        {TEXT("east 160"), 160, 6, false, TEXT("flight-160")},
        {TEXT("circle 1"), 80, 0, true, nullptr},
        {TEXT("circle 2"), 80, 0, true, TEXT("flight-circle")},
        {TEXT("circle 3"), 80, 0, true, nullptr},
    };
    Totals.SetNum(Legs.Num());
}

namespace
{
    // The chunk components of a world (water and sky W3c's leak check): columns, see-through parts, the parts holding
    // geometry, and those of them hidden (a part with geometry on a pooled or released column would be a leak, though a
    // column waiting in the pending releases is hidden on purpose).
    struct FFlightChunkCount
    {
        int32 Columns = 0, Parts = 0, PartsMeshed = 0, PartsMeshedHidden = 0;
    };
    FFlightChunkCount CountFlightChunks(const UWorld* World)
    {
        FFlightChunkCount Count;
        for (TObjectIterator<UVoxelChunkComponent> It; It; ++It)
        {
            if (It->GetWorld() != World || It->IsTemplate() || It->IsBeingDestroyed()) continue;
            if (!It->IsSeeThroughPart())
            {
                ++Count.Columns;
                continue;
            }
            ++Count.Parts;
            if (!It->GetSectionMeshCount()) continue;
            ++Count.PartsMeshed;
            Count.PartsMeshedHidden += !It->IsVisible();
        }
        return Count;
    }
    FFlightChunkCount FlightChunksAtStart;
}

void FVoxelFlight::Start(AVoxelWorld& World)
{
    bStarted = true;
    FlightChunksAtStart = CountFlightChunks(World.GetWorld());
    AVoxelFlyCamera* Fly = AVoxelFlyCamera::Find(World.GetWorld());
    if (!Fly) Fly = AVoxelFlyCamera::Toggle(World.GetWorld(), 0);
    if (!Fly) return;
    Fly->bScripted = true;
    Camera = Fly;
    const FVector Spawn = World.GetSafeSpawn(*GetDefault<UFoundationProfile>());
    const double SpawnBlocks = (Spawn.Z - World.OriginCm.Z) / FlightBlockCm;
    StartCm =
        FVector(Spawn.X, Spawn.Y, World.OriginCm.Z + FMath::Max(FlightCruiseBlocks, SpawnBlocks + 30) * FlightBlockCm);
    // Three view radii across: the rings sweep over ground they left a lap ago.
    const UVoxelWorldSubsystem* Streaming = UVoxelWorldSubsystem::Get(World.GetWorld());
    const int32 Radius = Streaming && Streaming->GetPipeline() ? Streaming->GetPipeline()->GetRings().ViewRadius : 12;
    CircleRadiusCm = 1.5 * Radius * FlightColumnCm;
    for (FLeg& Leg : Legs)
        if (Leg.bCircle) Leg.Seconds = 2. * UE_DOUBLE_PI * CircleRadiusCm / (Leg.BlocksPerSecond * FlightBlockCm);
    Rows = TEXT("time_s,x,y,z,speed,leg,center_x,center_y,generated,needed,in_view,in_view_on_screen,jobs,ready,")
        TEXT("applied,tick_ms,plan_ms,jobs_ms,apply_ms,holes,late,column_mb,cache_mb,process_mb,frame_ms,gpu_ms,")
            TEXT("coarse,far_holes,far_ms,focus_ms,vis_ms,walk_ms,walked,release_ms,pending_ms,pending_releases\n");
}

bool FVoxelFlight::Tick(AVoxelWorld& World, float DeltaSeconds)
{
    // A hitch moves the camera at most 50 ms of flight, as a frame of the flight's own screenshots can take 400 ms;
    // flight.csv still records the frame's real length.
    if (!bStarted)
        Start(World);
    else
        Elapsed += FMath::Min(DeltaSeconds, .05f);
    AVoxelFlyCamera* Fly = Camera.Get();
    const UVoxelWorldSubsystem* Streaming = UVoxelWorldSubsystem::Get(World.GetWorld());
    if (!Fly || !Streaming || !Streaming->GetPipeline())
    {
        UE_LOG(LogUnnamedVoxel, Error, TEXT("Flight: no fly camera or streaming; giving up."));
        Finish(World);
        return false;
    }

    // The leg flying now, where it began and where the camera is along it. Straight legs fly east; the circle starts
    // heading east from the end of the last one and turns south.
    int32 LegIndex = INDEX_NONE;
    double LegStart = 0, CircleStart = 0;
    FVector LegStartCm = StartCm;
    for (int32 Index = 0; Index < Legs.Num(); ++Index)
    {
        if (Legs[Index].bCircle && (Index == 0 || !Legs[Index - 1].bCircle)) CircleStart = LegStart;
        if (Elapsed < LegStart + Legs[Index].Seconds)
        {
            LegIndex = Index;
            break;
        }
        if (!Legs[Index].bCircle) LegStartCm.X += Legs[Index].BlocksPerSecond * FlightBlockCm * Legs[Index].Seconds;
        LegStart += Legs[Index].Seconds;
    }
    if (LegIndex == INDEX_NONE || (EndSeconds > 0 && Elapsed >= EndSeconds))
    {
        Finish(World);
        return false;
    }
    const FLeg& Leg = Legs[LegIndex];
    FVector Location;
    double Yaw = 0;
    if (!Leg.bCircle)
        Location = LegStartCm + FVector(Leg.BlocksPerSecond * FlightBlockCm * (Elapsed - LegStart), 0, 0);
    else
    {
        const double Angle =
            -UE_DOUBLE_HALF_PI + Leg.BlocksPerSecond * FlightBlockCm / CircleRadiusCm * (Elapsed - CircleStart);
        const FVector CircleCenter = LegStartCm + FVector(0, CircleRadiusCm, 0);
        Location = CircleCenter + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * CircleRadiusCm;
        Yaw = FMath::RadiansToDegrees(Angle) + 90.;
    }
    Fly->SetActorLocationAndRotation(Location, FRotator(FlightPitch, Yaw, 0));

    // What the frame shows from there.
    FMinimalViewInfo View;
    Fly->GetCamera()->GetCameraView(DeltaSeconds, View);
    FMatrix ViewMatrix, ProjectionMatrix, ViewProjection;
    UGameplayStatics::GetViewProjectionMatrix(View, ViewMatrix, ProjectionMatrix, ViewProjection);
    FConvexVolume Frustum;
    GetViewFrustumBounds(Frustum, ViewProjection, false);
    const FVoxelColumnPipeline& Pipeline = *Streaming->GetPipeline();
    const FVoxelPipelineStats Stats = Pipeline.GetStats();
    const FVoxelStreamingFrame& Frame = Streaming->GetLastFrame();
    TArray<FIntPoint> HoleColumns;
    int32 Late = 0, Coarse = 0, FarHoles = 0;
    CountFlightHoles(Pipeline, Frustum, World.OriginCm, HoleColumns, Late, Coarse, FarHoles);
    const int32 Holes = HoleColumns.Num();
    // The first frames with holes, column by column, for diagnosis.
    if ((Holes || Late) && HoleFramesLogged < 12)
    {
        ++HoleFramesLogged;
        FString Columns;
        for (const FIntPoint Column : HoleColumns)
        {
            const FVoxelColumnStatus Status = Pipeline.GetStatus(Column);
            Columns += FString::Printf(TEXT(" (%d,%d) gap2 %lld%s%s%s%s%s dirty %x;"), Column.X, Column.Y,
                Pipeline.GetRings().GetGapSquared(Column), Status.bPresent ? TEXT("") : TEXT(" absent"),
                Status.bGenerated ? TEXT(" generated") : TEXT(""), Status.bGenerating ? TEXT(" generating") : TEXT(""),
                Status.bMeshing ? TEXT(" meshing") : TEXT(""), Status.bReady ? TEXT(" ready") : TEXT(""),
                Status.DirtySections);
        }
        UE_LOG(LogUnnamedVoxel, Display, TEXT("Flight at %.3f s, center (%d, %d), %d jobs: %d late, holes:%s"), Elapsed,
            Pipeline.GetRings().Center.X, Pipeline.GetRings().Center.Y, Pipeline.GetJobCount(), Late, *Columns);
    }
    const double ProcessMb = GetFlightMb(FPlatformMemory::GetStats().UsedPhysical);
    const double ColumnMb = GetFlightMb(Stats.ColumnBytes), CacheMb = GetFlightMb(Stats.CacheBytes);
    const FVector Blocks = (Location - World.OriginCm) / FlightBlockCm;
    Rows +=
        FString::Printf(TEXT("%.4f,%.1f,%.1f,%.1f,%.0f,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%d,%d,%.2f,")
                            TEXT("%.2f,%.1f,%.2f,%.2f,%d,%d,%.3f,%.3f,%.3f,%.3f,%d,%.3f,%.3f,%d\n"),
            Elapsed, Blocks.X, Blocks.Y, Blocks.Z, Leg.BlocksPerSecond, Leg.Name, Pipeline.GetRings().Center.X,
            Pipeline.GetRings().Center.Y, Stats.Generated, Stats.Needed, Stats.InView, Stats.InViewOnScreen,
            Pipeline.GetJobCount(), Stats.Ready, Frame.Applied, Frame.TickMs, Frame.PlanMs, Frame.JobsMs, Frame.ApplyMs,
            Holes, Late, ColumnMb, CacheMb, ProcessMb, DeltaSeconds * 1000.,
            FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()), Coarse, FarHoles, Frame.FarApplyMs, Frame.FocusMs,
            Frame.VisibilityMs, Frame.WalkMs, Frame.Walked, Frame.ReleaseMs, Frame.PendingMs,
            Streaming->GetPendingReleaseCount());

    FLegTotals& Total = Totals[LegIndex];
    if (!Total.Frames)
    {
        Total.GeneratedBefore = Stats.Generate.Count;
        Total.ProcessMbFirst = ProcessMb;
        Total.ColumnMbFirst = ColumnMb;
    }
    ++Total.Frames;
    Total.Seconds += DeltaSeconds;
    Total.TickMsSum += Frame.TickMs;
    Total.WorstTickMs = FMath::Max(Total.WorstTickMs, Frame.TickMs);
    Total.FramesOverBudget += Frame.TickMs > VoxelConsole::CVarApplyBudgetMs.GetValueOnGameThread();
    Total.FramesWithHoles += Holes > 0;
    Total.MostHoles = FMath::Max(Total.MostHoles, Holes);
    Total.FramesWithLate += Late > 0;
    Total.MostLate = FMath::Max(Total.MostLate, Late);
    Total.FramesWithCoarse += Coarse > 0;
    Total.MostCoarse = FMath::Max(Total.MostCoarse, Coarse);
    Total.FramesWithFarHoles += FarHoles > 0;
    Total.MostFarHoles = FMath::Max(Total.MostFarHoles, FarHoles);
    Total.FarApplied += Frame.FarApplied;
    Total.FarApplyMs += Frame.FarApplyMs;
    Total.Applied += Frame.Applied;
    Total.GeneratedAfter = Stats.Generate.Count;
    Total.ProcessMbLast = ProcessMb;
    Total.ColumnMbLast = ColumnMb;
    Total.CacheMbLast = CacheMb;
    Total.MostJobs = FMath::Max(Total.MostJobs, Pipeline.GetJobCount());

    if (Leg.Shot && !ShotsTaken.Contains(LegIndex) && Elapsed >= LegStart + Leg.Seconds / 2)
    {
        ShotsTaken.Add(LegIndex);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Folder, FString(Leg.Shot) + TEXT(".png")), false, false);
    }
    return true;
}

void FVoxelFlight::Finish(AVoxelWorld& World)
{
    const double Budget = VoxelConsole::CVarApplyBudgetMs.GetValueOnGameThread();
    FString Summary = World.GetBuildReport() + TEXT("\n");
    int32 Frames = 0, FramesWithHoles = 0, MostHoles = 0, FramesOverBudget = 0, FramesWithLate = 0, MostLate = 0;
    int32 FramesWithCoarse = 0, MostCoarse = 0, FramesWithFarHoles = 0, MostFarHoles = 0, FarApplied = 0;
    double FarApplyMs = 0;
    double WorstTickMs = 0;
    const TCHAR* WorstLeg = TEXT("none");
    for (int32 Index = 0; Index < Legs.Num(); ++Index)
    {
        const FLegTotals& Total = Totals[Index];
        Frames += Total.Frames;
        FramesWithHoles += Total.FramesWithHoles;
        MostHoles = FMath::Max(MostHoles, Total.MostHoles);
        FramesWithLate += Total.FramesWithLate;
        MostLate = FMath::Max(MostLate, Total.MostLate);
        FramesWithCoarse += Total.FramesWithCoarse;
        MostCoarse = FMath::Max(MostCoarse, Total.MostCoarse);
        FramesWithFarHoles += Total.FramesWithFarHoles;
        MostFarHoles = FMath::Max(MostFarHoles, Total.MostFarHoles);
        FarApplied += Total.FarApplied;
        FarApplyMs += Total.FarApplyMs;
        FramesOverBudget += Total.FramesOverBudget;
        if (Total.WorstTickMs > WorstTickMs)
        {
            WorstTickMs = Total.WorstTickMs;
            WorstLeg = Legs[Index].Name;
        }
    }
    Summary += FString::Printf(TEXT("Flight: %d frames over %.1f s at %.0f blocks altitude; circle radius %.0f blocks; "
                                    "chunk work budget %.1f ms\n"),
        Frames, Elapsed, StartCm.Z / FlightBlockCm - World.OriginCm.Z / FlightBlockCm, CircleRadiusCm / FlightBlockCm,
        Budget);
    for (int32 Index = 0; Index < Legs.Num(); ++Index)
    {
        const FLeg& Leg = Legs[Index];
        const FLegTotals& Total = Totals[Index];
        Summary += FString::Printf(
            TEXT("  %s: %.1f s at %.0f blocks/s, %d frames; chunk work mean %.2f ms, worst %.2f ms, %d over budget; "
                 "holes in %d frames (most %d), late columns in %d (most %d); applied %d, generated %lld columns, jobs "
                 "at most %d; process "
                 "%.0f -> %.0f MB, columns %.2f -> %.2f MB, cache %.2f MB\n"),
            Leg.Name, Total.Seconds, Leg.BlocksPerSecond, Total.Frames,
            Total.Frames ? Total.TickMsSum / Total.Frames : 0., Total.WorstTickMs, Total.FramesOverBudget,
            Total.FramesWithHoles, Total.MostHoles, Total.FramesWithLate, Total.MostLate, Total.Applied,
            Total.GeneratedAfter - Total.GeneratedBefore, Total.MostJobs, Total.ProcessMbFirst, Total.ProcessMbLast,
            Total.ColumnMbFirst, Total.ColumnMbLast, Total.CacheMbLast);
    }
    Summary += FramesWithHoles
        ? FString::Printf(TEXT("Holes: %d frames with holes, at most %d columns\n"), FramesWithHoles, MostHoles)
        : FString(TEXT("Holes: none\n"));
    Summary += FString::Printf(
        TEXT("Late columns in the fog's fade: %d frames, at most %d columns\n"), FramesWithLate, MostLate);
    // Far LOD (phase 9, S1).
    Summary += FString::Printf(
        TEXT("Coarse columns (in view, still far): %d frames, at most %d columns\n"), FramesWithCoarse, MostCoarse);
    Summary += FramesWithFarHoles
        ? FString::Printf(TEXT("Far holes: %d frames, at most %d columns\n"), FramesWithFarHoles, MostFarHoles)
        : FString(TEXT("Far holes: none\n"));
    Summary += FString::Printf(TEXT("Far applies: %d in %.1f ms (%.3f ms per frame)\n"), FarApplied, FarApplyMs,
        Frames ? FarApplyMs / Frames : 0.);
    // Far LOD step 1b: unloaded columns whose components wait to be released.
    if (const UVoxelWorldSubsystem* Streaming = UVoxelWorldSubsystem::Get(World.GetWorld()))
        Summary += FString::Printf(TEXT("Pending releases: peak %d, %d at the end\n"),
            Streaming->GetPendingReleasePeak(), Streaming->GetPendingReleaseCount());
    const FFlightChunkCount Chunks = CountFlightChunks(World.GetWorld());
    Summary += FString::Printf(TEXT("Chunk components: start %d columns, %d see-through parts (%d with geometry, %d "
                                    "of them hidden); end %d, %d (%d, %d)\n"),
        FlightChunksAtStart.Columns, FlightChunksAtStart.Parts, FlightChunksAtStart.PartsMeshed,
        FlightChunksAtStart.PartsMeshedHidden, Chunks.Columns, Chunks.Parts, Chunks.PartsMeshed,
        Chunks.PartsMeshedHidden);
    Summary += FString::Printf(TEXT("Chunk work: worst %.2f ms (%s), %d of %d frames over the %.1f ms budget\n"),
        WorstTickMs, WorstLeg, FramesOverBudget, Frames, Budget);
    // The first lap fills the circle; laps 2 and 3 fly over ground the rings already left once.
    const int32 LastLap = Legs.Num() - 1;
    Summary += FString::Printf(TEXT("Memory over laps 2 and 3: process %.0f -> %.0f MB, columns %.2f -> %.2f MB"),
        Totals[LastLap - 1].ProcessMbFirst, Totals[LastLap].ProcessMbLast, Totals[LastLap - 1].ColumnMbFirst,
        Totals[LastLap].ColumnMbLast);
    FFileHelper::SaveStringToFile(Rows, *FPaths::Combine(Folder, TEXT("flight.csv")));
    FFileHelper::SaveStringToFile(Summary + TEXT("\n"), *FPaths::Combine(Folder, TEXT("flight.txt")));
    TArray<FString> Lines;
    Summary.ParseIntoArrayLines(Lines);
    for (const FString& Line : Lines)
        UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Line);
}
