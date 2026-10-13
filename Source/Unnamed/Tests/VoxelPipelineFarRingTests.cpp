#if WITH_DEV_AUTOMATION_TESTS
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelSeed.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelWorldSubsystem.h"

// Far LOD step 1 (phase 9 S1): the pipeline's far ring, with manual jobs and small radii. A far column gets a far mesh
// and never a full one; moving swaps kinds without a frame showing nothing; full meshes are taken before far ones; far
// jobs keep to a third of the slots; without a far ring the pipeline runs today's jobs; a mesh waiting when the
// rings move is dropped if its column now wants the other kind; and a quick move back after such a drop still
// converges.
namespace
{
    constexpr int32 FarRingTestMargin = 2;
    // Steps (Update, take, run) a drive takes before it gives up, and errors listed before the rest are only counted.
    constexpr int32 FarRingTestStepCap = 20000;
    constexpr int32 FarRingTestErrorCap = 16;
    // Columns checked: every one within this many columns of (0, 0) on both axes, past every ring used here.
    constexpr int32 FarRingTestScan = 10;
    // Brute counts at Stages 1 (brute_counts.py, far-lod tests-notes.md): view radius 1 with far radius 3 or 4.
    constexpr int32 FarRingTestInView = 21;
    constexpr int32 FarRingTestFar3 = 40;
    constexpr int32 FarRingTestNeeded3 = 101;
    constexpr int32 FarRingTestFar4 = 68;
    constexpr int32 FarRingTestNeeded4 = 137;

    // Lists errors up to a cap, so one broken invariant does not drown the log.
    struct FFarRingTestReport
    {
        explicit FFarRingTestReport(FAutomationTestBase& InTest) : Test(InTest) {}

        FAutomationTestBase& Test;
        int32 Errors = 0;

        void Fail(const FString& Message)
        {
            if (Errors < FarRingTestErrorCap)
                Test.AddError(Message);
            else if (Errors == FarRingTestErrorCap)
                Test.AddError(TEXT("Further errors are not listed."));
            ++Errors;
        }
    };

    struct FFarRingTestWorld
    {
        TSharedRef<const FVoxelBlockTable> BlockTable;
        TSharedRef<const IVoxelTerrain> HeightTerrain;
    };

    TUniquePtr<FVoxelColumnPipeline> FarRingTestMakePipeline(const FFarRingTestWorld& World, int32 MaxTasks)
    {
        FVoxelColumnPipeline::FSettings PipelineSettings;
        PipelineSettings.MaxTasksInFlight = MaxTasks;
        PipelineSettings.bManualJobs = true;
        return MakeUnique<FVoxelColumnPipeline>(World.BlockTable, World.HeightTerrain, PipelineSettings);
    }

    FVoxelStreamingRings FarRingTestRings(FIntPoint Center, int32 View, int32 Far)
    {
        return FVoxelStreamingRings(Center, View, FarRingTestMargin, 1, Far);
    }

    // The mesh a column should show under these rings: full in view, far in the far ring, none past it.
    EVoxelMeshKind FarRingTestWanted(const FVoxelStreamingRings& At, FIntPoint Column)
    {
        if (At.IsInView(Column)) return EVoxelMeshKind::Full;
        return At.IsFar(Column) ? EVoxelMeshKind::Far : EVoxelMeshKind::None;
    }

    EVoxelMeshKind FarRingTestKind(const FVoxelColumnPipeline::FReady& Taken)
    {
        return Taken.Mesh->bFar ? EVoxelMeshKind::Far : EVoxelMeshKind::Full;
    }

    const TCHAR* FarRingTestKindName(EVoxelMeshKind Kind)
    {
        switch (Kind)
        {
        case EVoxelMeshKind::Full:
            return TEXT("full");
        case EVoxelMeshKind::Far:
            return TEXT("far");
        default:
            return TEXT("no");
        }
    }

    // The columns of the scan square that pass Filter, by Y then X.
    TArray<FIntPoint> FarRingTestColumns(TFunctionRef<bool(FIntPoint)> Filter)
    {
        TArray<FIntPoint> Out;
        for (int32 Y = -FarRingTestScan; Y <= FarRingTestScan; ++Y)
            for (int32 X = -FarRingTestScan; X <= FarRingTestScan; ++X)
                if (Filter(FIntPoint(X, Y))) Out.Add(FIntPoint(X, Y));
        return Out;
    }

    TArray<FIntPoint> FarRingTestDrawn(const FVoxelStreamingRings& At)
    {
        return FarRingTestColumns([&At](FIntPoint Column) { return At.IsDrawn(Column); });
    }

    // Which pending jobs a drive step runs: the lowest index (Collect's RemoveAtSwap moves the newest job there, so
    // older jobs wait until the launches stop), the one pending longest (its column's first step pending, ties by
    // index), or every one.
    enum class EFarRingTestJobs : uint8
    {
        Lowest,
        Oldest,
        All
    };

    // Steps the pipeline: Update and Check(false); with bTake, every ready mesh taken, each followed by OnTake and
    // Check(true); then pending jobs run as JobMode says. True once the pipeline is idle (without taking: once no job
    // is left, the finished meshes waiting), or when Stop says so after a step's takes; false when the step cap runs
    // out.
    bool FarRingTestDrive(FVoxelColumnPipeline& Pipeline, EFarRingTestJobs JobMode, bool bTake,
        TFunctionRef<void(bool)> Check, TFunctionRef<void(const FVoxelColumnPipeline::FReady&)> OnTake,
        TFunctionRef<bool()> Stop)
    {
        TMap<FIntPoint, int32> PendingSince;
        for (int32 Step = 0; Step < FarRingTestStepCap; ++Step)
        {
            Pipeline.Update();
            Check(false);
            if (bTake)
            {
                FVoxelColumnPipeline::FReady Taken;
                while (Pipeline.PopReady(Taken))
                {
                    OnTake(Taken);
                    Check(true);
                }
                if (Pipeline.IsIdle()) return true;
            }
            else if (Pipeline.GetJobCount() == 0)
                return true;
            if (Stop()) return true;
            if (JobMode == EFarRingTestJobs::Oldest)
            {
                TMap<FIntPoint, int32> StillPending;
                int32 Oldest = INDEX_NONE, OldestSince = MAX_int32;
                for (int32 Index = 0; Index < Pipeline.GetJobCount(); ++Index)
                {
                    if (!Pipeline.IsJobPending(Index)) continue;
                    const FIntPoint Column = Pipeline.GetJobColumn(Index);
                    const int32* Since = PendingSince.Find(Column);
                    const int32 First = Since ? *Since : Step;
                    StillPending.Add(Column, First);
                    if (First < OldestSince)
                    {
                        Oldest = Index;
                        OldestSince = First;
                    }
                }
                PendingSince = MoveTemp(StillPending);
                // The column's next job (a mesh after its generation) counts from its own launch.
                if (Oldest != INDEX_NONE) PendingSince.Remove(Pipeline.GetJobColumn(Oldest));
                Pipeline.RunJob(Oldest);
                continue;
            }
            for (int32 Index = 0; Index < Pipeline.GetJobCount(); ++Index)
                if (Pipeline.RunJob(Index) && JobMode == EFarRingTestJobs::Lowest) break;
        }
        return false;
    }

    // The edges toward columns in view (EVoxelFace order: West, East, North, South).
    uint8 FarRingTestFullEdges(const FVoxelStreamingRings& At, FIntPoint Column)
    {
        const FIntPoint Steps[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        uint8 Mask = 0;
        for (int32 Face = 0; Face < 4; ++Face)
            if (At.IsInView(Column + Steps[Face])) Mask |= uint8(1u << Face);
        return Mask;
    }

    // Far columns beside one that came into view (stayed far, the neighbour was not in view before).
    TSet<FIntPoint> FarRingTestEdged(const FVoxelStreamingRings& From, const FVoxelStreamingRings& To)
    {
        TSet<FIntPoint> Out;
        for (const FIntPoint Column :
            FarRingTestColumns([&](FIntPoint Candidate) { return From.IsFar(Candidate) && To.IsFar(Candidate); }))
            if (FarRingTestFullEdges(To, Column) & ~FarRingTestFullEdges(From, Column)) Out.Add(Column);
        return Out;
    }

    // At idle (review A3): every far column's mesh read each edge toward a column in view cell by cell.
    void FarRingTestCheckEdges(FFarRingTestReport& Report, const FVoxelColumnPipeline& Pipeline, const FString& Where)
    {
        const FVoxelStreamingRings& At = Pipeline.GetRings();
        for (const FIntPoint Column : FarRingTestColumns([&At](FIntPoint Candidate) { return At.IsFar(Candidate); }))
        {
            const uint8 Wanted = FarRingTestFullEdges(At, Column), Read = Pipeline.GetStatus(Column).CellEdges;
            if (Wanted & ~Read)
                Report.Fail(FString::Printf(TEXT("%s: far column (%d, %d) read edges %x cell by cell, wanted %x"),
                    *Where, Column.X, Column.Y, Read, Wanted));
        }
    }

    // At idle: every drawn column has meshed and shows its wanted kind, the view is on screen and the counts add up.
    void FarRingTestCheckIdle(FFarRingTestReport& Report, const FVoxelColumnPipeline& Pipeline, const FString& Where,
        int32 ExpectFar, int32 ExpectNeeded)
    {
        const FVoxelStreamingRings& At = Pipeline.GetRings();
        int32 InView = 0, Far = 0;
        for (const FIntPoint Column : FarRingTestDrawn(At))
        {
            const EVoxelMeshKind Wanted = FarRingTestWanted(At, Column);
            ++(Wanted == EVoxelMeshKind::Full ? InView : Far);
            const FVoxelColumnStatus Status = Pipeline.GetStatus(Column);
            if (Status.Shown != Wanted || Status.Meshed != Wanted)
                Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) shows a %s mesh and meshed %s, wanted %s"),
                    *Where, Column.X, Column.Y, FarRingTestKindName(Status.Shown), FarRingTestKindName(Status.Meshed),
                    FarRingTestKindName(Wanted)));
        }
        FAutomationTestBase& Test = Report.Test;
        FarRingTestCheckEdges(Report, Pipeline, Where);
        Test.TestEqual(*(Where + TEXT(": columns in view")), InView, FarRingTestInView);
        Test.TestEqual(*(Where + TEXT(": far columns")), Far, ExpectFar);
        Test.TestTrue(*(Where + TEXT(": IsViewOnScreen")), Pipeline.IsViewOnScreen());
        Test.TestTrue(*(Where + TEXT(": IsNearViewOnScreen")), Pipeline.IsNearViewOnScreen());
        const FVoxelPipelineStats PipelineStats = Pipeline.GetStats();
        Test.TestEqual(*(Where + TEXT(": Stats.Needed")), PipelineStats.Needed, ExpectNeeded);
        Test.TestEqual(*(Where + TEXT(": Stats.FarWanted")), PipelineStats.FarWanted, ExpectFar);
        Test.TestEqual(*(Where + TEXT(": Stats.FarOnScreen")), PipelineStats.FarOnScreen, ExpectFar);
        Test.TestEqual(*(Where + TEXT(": Stats.InViewOnScreen")), PipelineStats.InViewOnScreen, FarRingTestInView);
        Test.TestEqual(*(Where + TEXT(": Stats.Coarse")), PipelineStats.Coarse, 0);
        Test.TestEqual(*(Where + TEXT(": Stats.Ready")), PipelineStats.Ready, 0);
        Test.TestEqual(*(Where + TEXT(": Stats.FarMeshing")), PipelineStats.FarMeshing, 0);
    }

    // 1. A far column gets a far mesh and never a full one: V1/F3 streamed from nothing, one job at a time. With the
    // oldest job first, the near view is also on screen before the whole view.
    void FarRingTestFarOnly(
        FFarRingTestReport& Report, const FFarRingTestWorld& World, EFarRingTestJobs JobMode, const FString& Where)
    {
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = FarRingTestMakePipeline(World, 6);
        const FVoxelStreamingRings Home = FarRingTestRings(FIntPoint::ZeroValue, 1, 3);
        const TArray<FIntPoint> Drawn = FarRingTestDrawn(Home);
        int32 Steps = 0, NearStep = INDEX_NONE, ViewStep = INDEX_NONE, FullTaken = 0, FarTaken = 0;
        const auto Check = [&](bool bAfterTake)
        {
            Steps += !bAfterTake;
            const bool bNear = Pipeline->IsNearViewOnScreen();
            if (bNear && NearStep == INDEX_NONE) NearStep = Steps;
            if (ViewStep == INDEX_NONE && Pipeline->IsViewOnScreen()) ViewStep = Steps;
            for (const FIntPoint Column : Drawn)
            {
                const FVoxelColumnStatus Status = Pipeline->GetStatus(Column);
                if (Home.IsFar(Column) &&
                    (Status.Meshed == EVoxelMeshKind::Full || Status.Shown == EVoxelMeshKind::Full))
                    Report.Fail(
                        FString::Printf(TEXT("%s, step %d: far column (%d, %d) meshed %s and shows %s"), *Where, Steps,
                            Column.X, Column.Y, FarRingTestKindName(Status.Meshed), FarRingTestKindName(Status.Shown)));
                else if (bNear && Home.IsInView(Column) && Status.Shown != EVoxelMeshKind::Full)
                    Report.Fail(FString::Printf(
                        TEXT("%s, step %d: IsNearViewOnScreen, but column (%d, %d) in view shows a %s mesh"), *Where,
                        Steps, Column.X, Column.Y, FarRingTestKindName(Status.Shown)));
            }
        };
        const auto OnTake = [&](const FVoxelColumnPipeline::FReady& Taken)
        {
            const EVoxelMeshKind Kind = FarRingTestKind(Taken);
            ++(Kind == EVoxelMeshKind::Far ? FarTaken : FullTaken);
            const EVoxelMeshKind Wanted = FarRingTestWanted(Home, Taken.Column);
            if (Kind != Wanted)
                Report.Fail(FString::Printf(TEXT("%s, step %d: column (%d, %d) took a %s mesh, wanted %s"), *Where,
                    Steps, Taken.Column.X, Taken.Column.Y, FarRingTestKindName(Kind), FarRingTestKindName(Wanted)));
        };
        Pipeline->SetFocus(Home, 0);
        if (!FarRingTestDrive(*Pipeline, JobMode, true, Check, OnTake, [] { return false; }))
        {
            Report.Fail(Where + TEXT(": not idle after the step cap"));
            return;
        }
        FarRingTestCheckIdle(Report, *Pipeline, Where + TEXT(", idle at (0, 0)"), FarRingTestFar3, FarRingTestNeeded3);
        FAutomationTestBase& Test = Report.Test;
        const FVoxelPipelineStats PipelineStats = Pipeline->GetStats();
        Test.TestEqual(*(Where + TEXT(": full meshes taken")), FullTaken, FarRingTestInView);
        Test.TestEqual(*(Where + TEXT(": far meshes taken")), FarTaken, FarRingTestFar3);
        Test.TestEqual(
            *(Where + TEXT(": full mesh jobs (Stats.Mesh.Count)")), PipelineStats.Mesh.Count, int64(FarRingTestInView));
        Test.TestEqual(*(Where + TEXT(": far mesh jobs (Stats.FarMesh.Count)")), PipelineStats.FarMesh.Count,
            int64(FarRingTestFar3));
        Test.TestEqual(*(Where + TEXT(": Stats.Dropped")), PipelineStats.Dropped, int64(0));
        if (JobMode == EFarRingTestJobs::Oldest)
            Test.TestTrue(*FString::Printf(TEXT("%s: near view on screen (step %d) before the whole view (step %d)"),
                              *Where, NearStep, ViewStep),
                NearStep != INDEX_NONE && NearStep < ViewStep);
        Test.AddInfo(FString::Printf(TEXT("%s: idle after %d steps; near view on screen from step %d, whole view from "
                                          "step %d"),
            *Where, Steps, NearStep, ViewStep));
    }

    // 2. One move from idle, one job at a time: every column drawn before and after keeps showing something, and its
    // kind changes only when its own mesh of the new kind is taken; at idle the kinds match the new rings and only the
    // columns that changed kind were meshed (once), and those that stayed far beside one that came into view at most
    // once (when the mesh shown did not read that edge cell by cell: review A3; CheckIdle checks they all do). With
    // bBounce the rings move back once meshes of both kinds are in flight for columns that changed kind (and at least
    // one took its new kind): those results are dropped on arrival.
    void FarRingTestMove(FFarRingTestReport& Report, FVoxelColumnPipeline& Pipeline, FIntPoint To, const FString& Where,
        EFarRingTestJobs JobMode, bool bBounce)
    {
        const FVoxelStreamingRings From = Pipeline.GetRings();
        const FVoxelStreamingRings Next = FarRingTestRings(To, From.ViewRadius, From.FarRadius);
        TMap<FIntPoint, EVoxelMeshKind> Shown;
        TSet<FIntPoint> Changed;
        const TSet<FIntPoint> Edged = FarRingTestEdged(From, Next);
        int32 ToView = 0, ToFar = 0;
        for (const FIntPoint Column :
            FarRingTestColumns([&](FIntPoint Candidate) { return From.IsDrawn(Candidate) && Next.IsDrawn(Candidate); }))
        {
            const EVoxelMeshKind Old = FarRingTestWanted(From, Column), New = FarRingTestWanted(Next, Column);
            const FVoxelColumnStatus Status = Pipeline.GetStatus(Column);
            if (Status.Shown != Old)
                Report.Fail(FString::Printf(TEXT("%s: before the move, column (%d, %d) shows a %s mesh, wanted %s"),
                    *Where, Column.X, Column.Y, FarRingTestKindName(Status.Shown), FarRingTestKindName(Old)));
            Shown.Add(Column, Status.Shown);
            if (Old == New) continue;
            Changed.Add(Column);
            ++(New == EVoxelMeshKind::Full ? ToView : ToFar);
        }
        Report.Test.TestTrue(
            *FString::Printf(
                TEXT("%s: columns go both ways (%d far to in view, %d in view to far, %d drawn throughout)"), *Where,
                ToView, ToFar, Shown.Num()),
            ToView > 0 && ToFar > 0);

        TMap<FIntPoint, int32> Takes;
        int32 Steps = 0, TakeCount = 0, InFlightAtBounce = 0;
        const int64 DroppedBefore = Pipeline.GetStats().Dropped;
        const auto Check = [&](bool bAfterTake)
        {
            Steps += !bAfterTake;
            for (const TPair<FIntPoint, EVoxelMeshKind>& Pair : Shown)
            {
                const EVoxelMeshKind Now = Pipeline.GetStatus(Pair.Key).Shown;
                if (Now == EVoxelMeshKind::None)
                    Report.Fail(FString::Printf(
                        TEXT("%s, step %d: column (%d, %d) shows nothing"), *Where, Steps, Pair.Key.X, Pair.Key.Y));
                else if (Now != Pair.Value)
                    Report.Fail(FString::Printf(TEXT("%s, step %d: column (%d, %d) shows a %s mesh without taking one "
                                                     "(it showed a %s mesh)"),
                        *Where, Steps, Pair.Key.X, Pair.Key.Y, FarRingTestKindName(Now),
                        FarRingTestKindName(Pair.Value)));
            }
        };
        const auto OnTake = [&](const FVoxelColumnPipeline::FReady& Taken)
        {
            ++TakeCount;
            const EVoxelMeshKind Kind = FarRingTestKind(Taken);
            if (EVoxelMeshKind* Tracked = Shown.Find(Taken.Column)) *Tracked = Kind;
            ++Takes.FindOrAdd(Taken.Column);
            const FVoxelStreamingRings& At = Pipeline.GetRings();
            const EVoxelMeshKind Wanted = FarRingTestWanted(At, Taken.Column);
            if (At.IsDrawn(Taken.Column) && Kind != Wanted)
                Report.Fail(
                    FString::Printf(TEXT("%s, step %d: drawn column (%d, %d) took a %s mesh, wanted %s"), *Where, Steps,
                        Taken.Column.X, Taken.Column.Y, FarRingTestKindName(Kind), FarRingTestKindName(Wanted)));
        };
        const auto Bounce = [&]()
        {
            if (!bBounce) return false;
            bool bFullInFlight = false, bFarInFlight = false, bTakenNew = false;
            for (const FIntPoint Column : Changed)
            {
                bTakenNew |= Takes.Contains(Column);
                if (!Pipeline.GetStatus(Column).bMeshing) continue;
                (FarRingTestWanted(Next, Column) == EVoxelMeshKind::Full ? bFullInFlight : bFarInFlight) = true;
            }
            if (!bFullInFlight || !bFarInFlight || !bTakenNew) return false;
            for (const FIntPoint Column : Changed)
                InFlightAtBounce += Pipeline.GetStatus(Column).bMeshing;
            return true;
        };
        const auto Never = [] { return false; };

        Pipeline.SetFocus(Next, 0);
        if (!FarRingTestDrive(Pipeline, JobMode, true, Check, OnTake, Bounce))
        {
            Report.Fail(FString::Printf(TEXT("%s: not idle after the step cap"), *Where));
            return;
        }
        if (bBounce)
        {
            if (Pipeline.IsIdle())
            {
                Report.Fail(FString::Printf(TEXT("%s: idle before meshes of both kinds were in flight"), *Where));
                return;
            }
            Report.Test.AddInfo(FString::Printf(TEXT("%s: moved back after %d steps and %d takes, %d mesh jobs of "
                                                     "columns that changed kind in flight"),
                *Where, Steps, TakeCount, InFlightAtBounce));
            Pipeline.SetFocus(From, 0);
            if (!FarRingTestDrive(Pipeline, JobMode, true, Check, OnTake, Never))
            {
                Report.Fail(FString::Printf(TEXT("%s: not idle after the step cap (moved back)"), *Where));
                return;
            }
        }
        FarRingTestCheckIdle(Report, Pipeline, Where + TEXT(", idle"), FarRingTestFar3, FarRingTestNeeded3);
        const int64 Dropped = Pipeline.GetStats().Dropped - DroppedBefore;
        if (bBounce)
            Report.Test.TestTrue(
                *FString::Printf(TEXT("%s: %lld results dropped, at least the %d mesh jobs (2 or more) "
                                      "of columns that changed kind in flight at the bounce"),
                    *Where, Dropped, InFlightAtBounce),
                InFlightAtBounce >= 2 && Dropped >= InFlightAtBounce);
        else
        {
            Report.Test.TestEqual(*(Where + TEXT(": results dropped")), Dropped, int64(0));
            for (const TPair<FIntPoint, EVoxelMeshKind>& Pair : Shown)
            {
                const int32 Expected = Changed.Contains(Pair.Key) ? 1 : 0, Actual = Takes.FindRef(Pair.Key);
                if (Actual != Expected && !(Edged.Contains(Pair.Key) && Actual == 1))
                    Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) took %d meshes, expected %d"), *Where,
                        Pair.Key.X, Pair.Key.Y, Actual, Expected));
            }
        }
        if (!bBounce)
            Report.Test.TestTrue(
                *FString::Printf(TEXT("%s: far columns beside new view columns (%d)"), *Where, Edged.Num()),
                Edged.Num() > 0);
        Report.Test.AddInfo(FString::Printf(TEXT("%s: %d far to in view, %d in view to far, %d far beside new view "
                                                 "columns; idle after %d steps and %d takes, %lld results dropped"),
            *Where, ToView, ToFar, Edged.Num(), Steps, TakeCount, Dropped));
    }

    // 2. V1/F3 settled at (0, 0), then to (3, 0) and back, then a bounce, with the given job order.
    void FarRingTestSwap(
        FFarRingTestReport& Report, const FFarRingTestWorld& World, EFarRingTestJobs JobMode, const TCHAR* Order)
    {
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = FarRingTestMakePipeline(World, 6);
        Pipeline->SetFocus(FarRingTestRings(FIntPoint::ZeroValue, 1, 3), 0);
        if (!FarRingTestDrive(
                *Pipeline, EFarRingTestJobs::All, true, [](bool) {}, [](const FVoxelColumnPipeline::FReady&) {},
                [] { return false; }))
        {
            Report.Fail(TEXT("2. not idle at (0, 0) after the step cap"));
            return;
        }
        FarRingTestMove(Report, *Pipeline, FIntPoint(3, 0), FString::Printf(TEXT("2. %s: (0, 0) to (3, 0)"), Order),
            JobMode, false);
        FarRingTestMove(Report, *Pipeline, FIntPoint(0, 0),
            FString::Printf(TEXT("2. %s: (3, 0) back to (0, 0)"), Order), JobMode, false);
        FarRingTestMove(Report, *Pipeline, FIntPoint(3, 0),
            FString::Printf(TEXT("2. %s: bounce (0, 0) to (3, 0) and back"), Order), JobMode, true);
    }

    // 3. Near meshes are taken before ready far meshes: every mesh of V1/F3 finished before any is taken, then taken
    // one by one, PeekReady showing each first.
    void FarRingTestNearFirst(FFarRingTestReport& Report, const FFarRingTestWorld& World)
    {
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = FarRingTestMakePipeline(World, 6);
        const FVoxelStreamingRings Home = FarRingTestRings(FIntPoint::ZeroValue, 1, 3);
        Pipeline->SetFocus(Home, 0);
        if (!FarRingTestDrive(
                *Pipeline, EFarRingTestJobs::All, false, [](bool) {}, [](const FVoxelColumnPipeline::FReady&) {},
                [] { return false; }))
        {
            Report.Fail(TEXT("3. jobs left after the step cap"));
            return;
        }
        FAutomationTestBase& Test = Report.Test;
        const FVoxelPipelineStats Waiting = Pipeline->GetStats();
        Test.TestEqual(TEXT("3. meshes waiting"), Waiting.Ready, FarRingTestInView + FarRingTestFar3);
        Test.TestEqual(TEXT("3. far meshes waiting"), Waiting.FarReady, FarRingTestFar3);
        int32 FullTaken = 0, FarTaken = 0, FullAfterFar = 0;
        for (int32 Round = 0; Round <= FarRingTestInView + FarRingTestFar3; ++Round)
        {
            FIntPoint Peeked(MAX_int32, MAX_int32);
            const FVoxelColumnMesh* PeekedMesh = Pipeline->PeekReady(&Peeked);
            FVoxelColumnPipeline::FReady Taken;
            const bool bTaken = Pipeline->PopReady(Taken);
            if (bTaken != (PeekedMesh != nullptr))
            {
                Report.Fail(FString::Printf(TEXT("3. take %d: PeekReady %s a mesh, PopReady %s one"), Round,
                    PeekedMesh ? TEXT("shows") : TEXT("shows no"), bTaken ? TEXT("took") : TEXT("took no")));
                break;
            }
            if (!bTaken) break;
            if (Taken.Column != Peeked || Taken.Mesh.Get() != PeekedMesh)
                Report.Fail(FString::Printf(TEXT("3. take %d: PopReady took (%d, %d), PeekReady showed (%d, %d)%s"),
                    Round, Taken.Column.X, Taken.Column.Y, Peeked.X, Peeked.Y,
                    Taken.Mesh.Get() != PeekedMesh ? TEXT(" and another mesh") : TEXT("")));
            const EVoxelMeshKind Kind = FarRingTestKind(Taken), Wanted = FarRingTestWanted(Home, Taken.Column);
            if (Kind != Wanted)
                Report.Fail(FString::Printf(TEXT("3. take %d: column (%d, %d) took a %s mesh, wanted %s"), Round,
                    Taken.Column.X, Taken.Column.Y, FarRingTestKindName(Kind), FarRingTestKindName(Wanted)));
            if (Kind == EVoxelMeshKind::Far)
                ++FarTaken;
            else
            {
                ++FullTaken;
                if (FarTaken > 0)
                {
                    ++FullAfterFar;
                    Report.Fail(FString::Printf(TEXT("3. take %d: full mesh of (%d, %d) taken after %d far meshes"),
                        Round, Taken.Column.X, Taken.Column.Y, FarTaken));
                }
            }
        }
        Test.TestEqual(TEXT("3. full meshes taken"), FullTaken, FarRingTestInView);
        Test.TestEqual(TEXT("3. far meshes taken"), FarTaken, FarRingTestFar3);
        Test.TestEqual(TEXT("3. full meshes taken after a far one"), FullAfterFar, 0);
        Test.TestTrue(TEXT("3. idle once every mesh is taken"), Pipeline->IsIdle());
    }

    // 4. The far job cap holds: 9 tasks give far mesh jobs 3 slots, and V1/F4's 68 far columns fill them.
    void FarRingTestFarCap(FFarRingTestReport& Report, const FFarRingTestWorld& World)
    {
        constexpr int32 MaxTasks = 9, FarCap = 3;
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = FarRingTestMakePipeline(World, MaxTasks);
        const FVoxelStreamingRings Home = FarRingTestRings(FIntPoint::ZeroValue, 1, 4);
        const TArray<FIntPoint> FarColumns =
            FarRingTestColumns([&Home](FIntPoint Column) { return Home.IsFar(Column); });
        int32 Steps = 0, MostFar = 0, MostJobs = 0;
        const auto Check = [&](bool bAfterTake)
        {
            Steps += !bAfterTake;
            int32 FarMeshing = 0;
            for (const FIntPoint Column : FarColumns)
                FarMeshing += Pipeline->GetStatus(Column).bMeshing;
            const int32 Counted = Pipeline->GetStats().FarMeshing;
            if (FarMeshing > FarCap || FarMeshing != Counted)
                Report.Fail(FString::Printf(TEXT("4. step %d: %d far columns meshing (Stats.FarMeshing %d), cap %d"),
                    Steps, FarMeshing, Counted, FarCap));
            MostFar = FMath::Max(MostFar, FarMeshing);
            MostJobs = FMath::Max(MostJobs, Pipeline->GetJobCount());
        };
        Pipeline->SetFocus(Home, 0);
        if (!FarRingTestDrive(
                *Pipeline, EFarRingTestJobs::All, true, Check, [](const FVoxelColumnPipeline::FReady&) {},
                [] { return false; }))
        {
            Report.Fail(TEXT("4. not idle after the step cap"));
            return;
        }
        FAutomationTestBase& Test = Report.Test;
        Test.TestEqual(TEXT("4. far columns at V1/F4"), FarColumns.Num(), FarRingTestFar4);
        Test.TestEqual(TEXT("4. most far columns meshing at once"), MostFar, FarCap);
        Test.TestTrue(*FString::Printf(TEXT("4. most jobs at once (%d) within %d"), MostJobs, MaxTasks),
            MostJobs >= 1 && MostJobs <= MaxTasks);
        FarRingTestCheckIdle(Report, *Pipeline, TEXT("4. idle at V1/F4"), FarRingTestFar4, FarRingTestNeeded4);
        Test.AddInfo(FString::Printf(
            TEXT("4. V1/F4 with 9 tasks idle after %d steps; most jobs %d, most far %d"), Steps, MostJobs, MostFar));
    }

    // What a scripted run did: the pending jobs' columns after each Update (in job order), and the meshes taken.
    struct FFarRingTestLog
    {
        TArray<TArray<FIntPoint>> Pending;
        TArray<FIntPoint> TakenColumns;
        TArray<int32> TakenQuads;
        int32 FarTaken = 0;
        int64 FarJobs = 0;
    };

    // V2 with the given far radius and 4 tasks: stream (0, 0) in sector 0, then (2, 1) in sector 3, one job at a time.
    bool FarRingTestScript(FFarRingTestReport& Report, const FFarRingTestWorld& World, int32 Far, FFarRingTestLog& Log)
    {
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = FarRingTestMakePipeline(World, 4);
        const auto Check = [&](bool bAfterTake)
        {
            if (bAfterTake) return;
            TArray<FIntPoint>& Step = Log.Pending.AddDefaulted_GetRef();
            for (int32 Index = 0; Index < Pipeline->GetJobCount(); ++Index)
                if (Pipeline->IsJobPending(Index)) Step.Add(Pipeline->GetJobColumn(Index));
        };
        const auto OnTake = [&](const FVoxelColumnPipeline::FReady& Taken)
        {
            Log.TakenColumns.Add(Taken.Column);
            Log.TakenQuads.Add(Taken.Mesh->QuadCount);
            Log.FarTaken += Taken.Mesh->bFar;
        };
        const auto Never = [] { return false; };
        Pipeline->SetFocus(FarRingTestRings(FIntPoint::ZeroValue, 2, Far), 0);
        const bool bHome = FarRingTestDrive(*Pipeline, EFarRingTestJobs::Lowest, true, Check, OnTake, Never);
        Pipeline->SetFocus(FarRingTestRings(FIntPoint(2, 1), 2, Far), 3);
        if (!bHome || !FarRingTestDrive(*Pipeline, EFarRingTestJobs::Lowest, true, Check, OnTake, Never))
        {
            Report.Fail(FString::Printf(TEXT("5. far radius %d: not idle after the step cap"), Far));
            return false;
        }
        Log.FarJobs = Pipeline->GetStats().FarMesh.Count;
        return true;
    }

    // 5. A far radius of 0, of the view radius or below it runs exactly today's jobs.
    void FarRingTestNoFarRing(FFarRingTestReport& Report, const FFarRingTestWorld& World)
    {
        constexpr int32 RunCount = 3;
        const int32 FarRadii[RunCount] = {0, 2, 1};
        FFarRingTestLog Logs[RunCount];
        for (int32 Run = 0; Run < RunCount; ++Run)
            if (!FarRingTestScript(Report, World, FarRadii[Run], Logs[Run])) return;
        FAutomationTestBase& Test = Report.Test;
        const FFarRingTestLog& Base = Logs[0];
        for (int32 Run = 0; Run < RunCount; ++Run)
        {
            Test.TestEqual(
                *FString::Printf(TEXT("5. far radius %d: far meshes taken"), FarRadii[Run]), Logs[Run].FarTaken, 0);
            Test.TestEqual(
                *FString::Printf(TEXT("5. far radius %d: far mesh jobs"), FarRadii[Run]), Logs[Run].FarJobs, int64(0));
            if (Run == 0) continue;
            const FFarRingTestLog& Log = Logs[Run];
            const int32 Shared = FMath::Min(Log.Pending.Num(), Base.Pending.Num());
            int32 FirstDiff = INDEX_NONE;
            for (int32 Step = 0; Step < Shared && FirstDiff == INDEX_NONE; ++Step)
                if (Log.Pending[Step] != Base.Pending[Step]) FirstDiff = Step;
            if (FirstDiff != INDEX_NONE || Log.Pending.Num() != Base.Pending.Num())
                Report.Fail(FString::Printf(TEXT("5. far radius %d: %d updates against %d with far radius 0, the "
                                                 "pending jobs first differing at update %d"),
                    FarRadii[Run], Log.Pending.Num(), Base.Pending.Num(),
                    FirstDiff == INDEX_NONE ? Shared : FirstDiff));
            Test.TestTrue(*FString::Printf(TEXT("5. far radius %d: the same columns taken in order (%d against %d)"),
                              FarRadii[Run], Log.TakenColumns.Num(), Base.TakenColumns.Num()),
                Log.TakenColumns == Base.TakenColumns);
            Test.TestTrue(*FString::Printf(TEXT("5. far radius %d: the same quad counts taken"), FarRadii[Run]),
                Log.TakenQuads == Base.TakenQuads);
        }
        int64 Quads = 0;
        for (const int32 Count : Base.TakenQuads)
            Quads += Count;
        Test.AddInfo(FString::Printf(TEXT("5. V2 script: %d updates, %d meshes taken, %lld quads"), Base.Pending.Num(),
            Base.TakenColumns.Num(), Quads));
    }

    // 6. Meshes already waiting when the rings move are re-checked: V1/F3 settled at (0, 0), every job of the move to
    // (3, 0) run with nothing taken, then back to (0, 0), one Update and everything taken. SetFocus drops each waiting
    // mesh of the kind its drawn, on-screen column no longer wants, so every mesh taken for a drawn column has the
    // wanted kind, no column on screen shows nothing, and each column whose mesh was dropped meshes again once.
    void FarRingTestWaitingKinds(FFarRingTestReport& Report, const FFarRingTestWorld& World)
    {
        // Pinned by the first run: meshes waiting at (3, 0) (all, far) and the ones SetFocus (0, 0) drops (the 10
        // columns going far to in view and the 10 going in view to far).
        // Far columns beside a column that comes into view mesh again too (review A3): computed, on top of the pins.
        constexpr int32 PinnedAwayReady = 47, PinnedAwayFarReady = 32, ExpectPurged = 20;
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = FarRingTestMakePipeline(World, 6);
        const FVoxelStreamingRings Home = FarRingTestRings(FIntPoint::ZeroValue, 1, 3);
        const int32 Edged = FarRingTestEdged(Home, FarRingTestRings(FIntPoint(3, 0), 1, 3)).Num();
        const int32 ExpectAwayReady = PinnedAwayReady + Edged, ExpectAwayFarReady = PinnedAwayFarReady + Edged;
        Report.Test.TestEqual(TEXT("6. far columns beside new view columns at (3, 0)"), Edged, 4);
        const auto Never = [] { return false; };
        Pipeline->SetFocus(Home, 0);
        if (!FarRingTestDrive(
                *Pipeline, EFarRingTestJobs::All, true, [](bool) {}, [](const FVoxelColumnPipeline::FReady&) {}, Never))
        {
            Report.Fail(TEXT("6. not idle at (0, 0) after the step cap"));
            return;
        }
        Pipeline->SetFocus(FarRingTestRings(FIntPoint(3, 0), 1, 3), 0);
        if (!FarRingTestDrive(
                *Pipeline, EFarRingTestJobs::All, false, [](bool) {}, [](const FVoxelColumnPipeline::FReady&) {},
                Never))
        {
            Report.Fail(TEXT("6. jobs left at (3, 0) after the step cap"));
            return;
        }
        FAutomationTestBase& Test = Report.Test;
        const FVoxelPipelineStats Away = Pipeline->GetStats();
        Test.TestEqual(TEXT("6. meshes waiting at (3, 0)"), Away.Ready, ExpectAwayReady);
        Test.TestEqual(TEXT("6. far meshes waiting at (3, 0)"), Away.FarReady, ExpectAwayFarReady);

        // The columns drawn at (0, 0) and on screen, and those whose waiting mesh is of the kind (0, 0) does not want.
        const TArray<FIntPoint> Drawn = FarRingTestDrawn(Home);
        TArray<FIntPoint> OnScreen;
        TSet<FIntPoint> Stale;
        for (const FIntPoint Column : Drawn)
        {
            const FVoxelColumnStatus Status = Pipeline->GetStatus(Column);
            if (!Status.bOnScreen) continue;
            OnScreen.Add(Column);
            if (Status.bReady && Status.Meshed != FarRingTestWanted(Home, Column)) Stale.Add(Column);
        }
        Pipeline->SetFocus(Home, 0);
        const int64 Purged = Pipeline->GetStats().Dropped - Away.Dropped;
        Test.TestEqual(
            TEXT("6. waiting meshes SetFocus (0, 0) drops, against the stale ones"), Purged, int64(Stale.Num()));
        Test.TestEqual(TEXT("6. waiting meshes SetFocus (0, 0) drops"), Purged, int64(ExpectPurged));
        for (const FIntPoint Column : Stale)
            if (Pipeline->GetStatus(Column).bReady)
                Report.Fail(FString::Printf(
                    TEXT("6. column (%d, %d) still has its stale mesh waiting after SetFocus"), Column.X, Column.Y));

        int32 Steps = 0, FirstTakes = 0, TakeCount = 0, UndrawnTakes = 0, WrongKind = 0;
        TMap<FIntPoint, int32> Takes;
        const auto Check = [&](bool bAfterTake)
        {
            Steps += !bAfterTake;
            for (const FIntPoint Column : OnScreen)
                if (Pipeline->GetStatus(Column).Shown == EVoxelMeshKind::None)
                    Report.Fail(FString::Printf(
                        TEXT("6. back at (0, 0), step %d: column (%d, %d) shows nothing"), Steps, Column.X, Column.Y));
        };
        const auto OnTake = [&](const FVoxelColumnPipeline::FReady& Taken)
        {
            ++TakeCount;
            ++Takes.FindOrAdd(Taken.Column);
            const EVoxelMeshKind Kind = FarRingTestKind(Taken), Wanted = FarRingTestWanted(Home, Taken.Column);
            UndrawnTakes += !Home.IsDrawn(Taken.Column);
            if (!Home.IsDrawn(Taken.Column) || Kind == Wanted) return;
            ++WrongKind;
            Report.Fail(FString::Printf(TEXT("6. back at (0, 0), step %d: drawn column (%d, %d) took a %s mesh, "
                                             "wanted %s"),
                Steps, Taken.Column.X, Taken.Column.Y, FarRingTestKindName(Kind), FarRingTestKindName(Wanted)));
        };
        Pipeline->Update();
        Check(false);
        FVoxelColumnPipeline::FReady First;
        while (Pipeline->PopReady(First))
        {
            ++FirstTakes;
            OnTake(First);
            Check(true);
        }
        if (!FarRingTestDrive(*Pipeline, EFarRingTestJobs::All, true, Check, OnTake, Never))
        {
            Report.Fail(TEXT("6. not idle back at (0, 0) after the step cap"));
            return;
        }
        FarRingTestCheckIdle(Report, *Pipeline, TEXT("6. idle back at (0, 0)"), FarRingTestFar3, FarRingTestNeeded3);
        Test.TestEqual(TEXT("6. meshes of the wrong kind taken for drawn columns"), WrongKind, 0);
        const int64 DroppedBack = Pipeline->GetStats().Dropped - Away.Dropped;
        Test.TestEqual(TEXT("6. results dropped back at (0, 0): only SetFocus's"), DroppedBack, Purged);
        for (const FIntPoint Column : Stale)
            if (Takes.FindRef(Column) != 1)
                Report.Fail(FString::Printf(TEXT("6. column (%d, %d), its waiting mesh dropped, took %d meshes, "
                                                 "expected 1"),
                    Column.X, Column.Y, Takes.FindRef(Column)));
        Test.AddInfo(FString::Printf(TEXT("6. at (3, 0) %d meshes waiting (%d far), %d of them stale; back at (0, 0) "
                                          "%lld dropped, %d taken after one Update, %d in all (%d for columns not "
                                          "drawn), idle after %d steps"),
            Away.Ready, Away.FarReady, Stale.Num(), Purged, FirstTakes, TakeCount, UndrawnTakes, Steps));
    }

    // 7. A quick move back after a drop (review A1): V1/F3 settled at (0, 0), every job of the move to (3, 0) run with
    // nothing taken, then (0, 0) drops the waiting meshes and (3, 0) comes back before any job runs. The dropped meshes
    // were never shown, so each column whose mesh was dropped meshes its kind again and shows it at idle, both ways.
    void FarRingTestQuickReturn(FFarRingTestReport& Report, const FFarRingTestWorld& World)
    {
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = FarRingTestMakePipeline(World, 6);
        const FVoxelStreamingRings Home = FarRingTestRings(FIntPoint::ZeroValue, 1, 3);
        const FVoxelStreamingRings Away = FarRingTestRings(FIntPoint(3, 0), 1, 3);
        const auto Never = [] { return false; };
        const auto NoCheck = [](bool) {};
        const auto NoTake = [](const FVoxelColumnPipeline::FReady&) {};
        Pipeline->SetFocus(Home, 0);
        if (!FarRingTestDrive(*Pipeline, EFarRingTestJobs::All, true, NoCheck, NoTake, Never))
        {
            Report.Fail(TEXT("7. not idle at (0, 0) after the step cap"));
            return;
        }
        Pipeline->SetFocus(Away, 0);
        if (!FarRingTestDrive(*Pipeline, EFarRingTestJobs::All, false, NoCheck, NoTake, Never))
        {
            Report.Fail(TEXT("7. jobs left at (3, 0) after the step cap"));
            return;
        }
        const int64 DroppedBefore = Pipeline->GetStats().Dropped;
        Pipeline->SetFocus(Home, 0);
        const int64 Dropped = Pipeline->GetStats().Dropped - DroppedBefore;
        Report.Test.TestTrue(
            *FString::Printf(TEXT("7. SetFocus (0, 0) drops waiting meshes (%lld)"), Dropped), Dropped > 0);
        Pipeline->SetFocus(Away, 0);
        if (!FarRingTestDrive(*Pipeline, EFarRingTestJobs::All, true, NoCheck, NoTake, Never))
        {
            Report.Fail(TEXT("7. not idle back at (3, 0) after the step cap"));
            return;
        }
        FarRingTestCheckIdle(Report, *Pipeline, TEXT("7. idle back at (3, 0)"), FarRingTestFar3, FarRingTestNeeded3);
    }

    // 8. Near meshes only (review A2: the apply loop's way past a far mesh it turned down): with inner-first, the move
    // of 6 leaves an urgent far mesh heading the ready queue before full meshes that refine; near-only peeks and takes
    // show and take every full mesh and nothing else, then none, the far meshes still waiting.
    void FarRingTestNearOnly(FFarRingTestReport& Report, const FFarRingTestWorld& World)
    {
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = FarRingTestMakePipeline(World, 6);
        const auto Never = [] { return false; };
        const auto NoCheck = [](bool) {};
        const auto NoTake = [](const FVoxelColumnPipeline::FReady&) {};
        Pipeline->SetFocus(FarRingTestRings(FIntPoint::ZeroValue, 1, 3), 0);
        if (!FarRingTestDrive(*Pipeline, EFarRingTestJobs::All, true, NoCheck, NoTake, Never))
        {
            Report.Fail(TEXT("8. not idle at (0, 0) after the step cap"));
            return;
        }
        Pipeline->SetFocus(FarRingTestRings(FIntPoint(3, 0), 1, 3), 0);
        if (!FarRingTestDrive(*Pipeline, EFarRingTestJobs::All, false, NoCheck, NoTake, Never))
        {
            Report.Fail(TEXT("8. jobs left at (3, 0) after the step cap"));
            return;
        }
        Pipeline->SetFarInnerFirst(true);
        FAutomationTestBase& Test = Report.Test;
        const FVoxelPipelineStats Waiting = Pipeline->GetStats();
        const FVoxelColumnMesh* Head = Pipeline->PeekReady();
        Test.TestTrue(TEXT("8. a far mesh heads the ready queue"), Head && Head->bFar);
        int32 FullTaken = 0;
        while (const FVoxelColumnMesh* Peeked = Pipeline->PeekReady(nullptr, true))
        {
            FVoxelColumnPipeline::FReady Taken;
            if (!Pipeline->PopReady(Taken, true) || Taken.Mesh.Get() != Peeked || Taken.Mesh->bFar)
            {
                Report.Fail(FString::Printf(TEXT("8. near-only take %d: not the full mesh peeked"), FullTaken));
                return;
            }
            ++FullTaken;
        }
        FVoxelColumnPipeline::FReady None;
        Test.TestFalse(TEXT("8. near-only take with only far meshes waiting"), Pipeline->PopReady(None, true));
        Test.TestEqual(TEXT("8. full meshes taken near-only"), FullTaken, Waiting.Ready - Waiting.FarReady);
        Test.TestEqual(TEXT("8. far meshes still waiting"), Pipeline->GetStats().Ready, Waiting.FarReady);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPipelineFarRingTest, "Unnamed.Voxel.Pipeline.FarRing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPipelineFarRingTest::RunTest(const FString& Parameters)
{
    if (!FFoundationBlockRegistry::GetDefault()->GetAssets())
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    const TSharedRef<const FVoxelBlockTable> BlockTable = VoxelBlocks::GetDefault();
    const auto Seed = FVoxelSeed::Parse(TEXT("Unnamed")).Value;
    const FFarRingTestWorld World{BlockTable, MakeShared<FVoxelHeightTerrain>(Seed, *BlockTable)};
    FFarRingTestReport Report(*this);
    FarRingTestFarOnly(Report, World, EFarRingTestJobs::Lowest, TEXT("1. lowest job first"));
    FarRingTestFarOnly(Report, World, EFarRingTestJobs::Oldest, TEXT("1. oldest job first"));
    FarRingTestSwap(Report, World, EFarRingTestJobs::Lowest, TEXT("lowest job first"));
    FarRingTestSwap(Report, World, EFarRingTestJobs::Oldest, TEXT("oldest job first"));
    FarRingTestNearFirst(Report, World);
    FarRingTestFarCap(Report, World);
    FarRingTestNoFarRing(Report, World);
    FarRingTestWaitingKinds(Report, World);
    FarRingTestQuickReturn(Report, World);
    FarRingTestNearOnly(Report, World);
    return true;
}

// The apply loop's budget steps (review A2), at the default 2 ms budget. A 1,601-quad far mesh's first estimate
// (0.1 + 0.001 x quads = 1.701 ms) passes 85% of it even as the frame's first apply.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelApplyStepTest, "Unnamed.Voxel.Pipeline.ApplyStep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelApplyStepTest::RunTest(const FString& Parameters)
{
    constexpr double Budget = 2, Big = 1.701;
    const auto Expect = [this](const TCHAR* What, EVoxelApplyStep Actual, EVoxelApplyStep Expected)
    { TestEqual(What, int32(Actual), int32(Expected)); };
    Expect(TEXT("far mesh that fits"), GetVoxelApplyStep(0.5, 0.5, Budget, 3, true, false), EVoxelApplyStep::Apply);
    Expect(TEXT("big far mesh first: near meshes go, it waits"), GetVoxelApplyStep(0, Big, Budget, 0, true, false),
        EVoxelApplyStep::SkipFar);
    Expect(TEXT("big far mesh first after its wait: goes"), GetVoxelApplyStep(0, Big, Budget, 0, true, true),
        EVoxelApplyStep::Apply);
    Expect(TEXT("big far mesh after near ones: skipped, even after its wait"),
        GetVoxelApplyStep(0.5, Big, Budget, 2, true, true), EVoxelApplyStep::SkipFar);
    Expect(TEXT("near mesh that fits"), GetVoxelApplyStep(0.5, 0.5, Budget, 3, false, false), EVoxelApplyStep::Apply);
    Expect(TEXT("near mesh past the room after others"), GetVoxelApplyStep(0.5, Big, Budget, 2, false, false),
        EVoxelApplyStep::Stop);
    Expect(TEXT("big near mesh first, within the budget"), GetVoxelApplyStep(0, Big, Budget, 0, false, false),
        EVoxelApplyStep::Apply);
    Expect(TEXT("near mesh first, no room left by planning"), GetVoxelApplyStep(1, 1.5, Budget, 0, false, false),
        EVoxelApplyStep::Wait);
    Expect(TEXT("near mesh first, no room, after its wait"), GetVoxelApplyStep(1, 1.5, Budget, 0, false, true),
        EVoxelApplyStep::Apply);
    Expect(TEXT("near mesh first, above the whole budget"), GetVoxelApplyStep(1, 3, Budget, 0, false, false),
        EVoxelApplyStep::Apply);
    return true;
}
#endif
