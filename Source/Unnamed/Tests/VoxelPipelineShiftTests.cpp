#if WITH_DEV_AUTOMATION_TESTS
#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelSeed.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelPendingReleases.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"

// Far LOD step 1b: the shift-based plans against the full ones, and the pending releases.
//
// ShiftPlan drives two pipelines with the same settings through the same random walk (moves, jumps, sector changes and
// shape changes, with jobs run in step), one with bFullReplan and one without, and compares everything either shows
// after every step. PendingReleases checks the queue of unloaded columns waiting for their chunks to be released.
namespace
{
    constexpr int32 ShiftTestMargin = 2;
    constexpr int32 ShiftTestSteps = 500;
    constexpr int32 ShiftTestSeedValue = 20261011;
    constexpr uint64 ShiftTestTerrainSeed = 263;

    // The terrain and block table of the undecorated configurations, and the overworld with its decorator for the
    // decorated ones.
    struct FShiftTestWorld
    {
        TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
        TSharedPtr<const IVoxelTerrain> Plain;
        FVoxelTerrainPreset Preset;
        FVoxelBiomeTable Biomes;
        FVoxelSurfaceInputs Surface;
        FVoxelFeatureGraph Graph;
        TSharedPtr<FVoxelDensityTerrain> Density;
        TSharedPtr<FVoxelDecorator> Decorator;
    };

    bool ShiftTestMakeWorld(FAutomationTestBase& Test, FShiftTestWorld& Out)
    {
        Out.Plain = MakeShared<FVoxelHeightTerrain>(FVoxelSeed::Parse(TEXT("Unnamed")).Value, *Out.Table);
        FString Error;
        const bool bInputs = VoxelTerrainFactory::ReadInputs(
            VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), Out.Preset, Out.Biomes, Out.Surface, Error);
        if (!Test.TestTrue(FString::Printf(TEXT("overworld inputs read (%s)"), *Error), bInputs)) return false;
        const bool bFeatures = VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Out.Graph, Error);
        if (!Test.TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error), bFeatures)) return false;
        FVoxelDensityTerrainSettings TerrainSettings;
        TerrainSettings.bCaves = true;
        Out.Density = MakeShared<FVoxelDensityTerrain>(
            Out.Preset, ShiftTestTerrainSeed, *Out.Table, TerrainSettings, &Out.Biomes, &Out.Surface);
        if (!Test.TestTrue(
                FString::Printf(TEXT("terrain is valid (%s)"), *Out.Density->GetError()), Out.Density->IsValid()))
            return false;
        Out.Decorator = MakeShared<FVoxelDecorator>();
        const bool bCompiled = Out.Decorator->Initialize(Out.Graph, *Out.Table,
            Out.Density->GetBiomeSource()->GetTable(), Out.Density->GetSeed(), FVoxelDecorationOptions(), &Error);
        return Test.TestTrue(FString::Printf(TEXT("the decorator compiles (%s)"), *Error), bCompiled);
    }

    TUniquePtr<FVoxelColumnPipeline> ShiftTestMakePipeline(const FShiftTestWorld& World, bool bDecorate, bool bFull)
    {
        FVoxelColumnPipeline::FSettings PipelineSettings;
        PipelineSettings.MaxTasksInFlight = 6;
        PipelineSettings.bManualJobs = true;
        PipelineSettings.bFullReplan = bFull;
        if (bDecorate) PipelineSettings.Decorator = World.Decorator;
        const TSharedRef<const IVoxelTerrain> Terrain =
            bDecorate ? TSharedRef<const IVoxelTerrain>(World.Density.ToSharedRef()) : World.Plain.ToSharedRef();
        return MakeUnique<FVoxelColumnPipeline>(World.Table, Terrain, PipelineSettings);
    }

    FString ShiftTestPoint(FIntPoint Point)
    {
        return FString::Printf(TEXT("(%d, %d)"), Point.X, Point.Y);
    }

    // The first field in which two statuses differ, with each side's value; false when they match.
    bool ShiftTestStatusDiff(
        const FVoxelColumnStatus& A, const FVoxelColumnStatus& B, FString& OutField, FString& OutA, FString& OutB)
    {
#define SHIFT_TEST_FIELD(Field)                                                                                        \
    if (A.Field != B.Field)                                                                                            \
    {                                                                                                                  \
        OutField = TEXT(#Field);                                                                                       \
        OutA = FString::FromInt(int32(A.Field));                                                                       \
        OutB = FString::FromInt(int32(B.Field));                                                                       \
        return true;                                                                                                   \
    }
        SHIFT_TEST_FIELD(bPresent)
        SHIFT_TEST_FIELD(bGenerated)
        SHIFT_TEST_FIELD(bGenerating)
        SHIFT_TEST_FIELD(bMeshing)
        SHIFT_TEST_FIELD(bReady)
        SHIFT_TEST_FIELD(bOnScreen)
        SHIFT_TEST_FIELD(Shown)
        SHIFT_TEST_FIELD(Meshed)
        SHIFT_TEST_FIELD(DirtySections)
        SHIFT_TEST_FIELD(bCached)
        SHIFT_TEST_FIELD(bSnapshot)
        SHIFT_TEST_FIELD(bDecorated)
        SHIFT_TEST_FIELD(bDecorating)
        SHIFT_TEST_FIELD(bFinalizing)
        SHIFT_TEST_FIELD(HeldBuckets)
#undef SHIFT_TEST_FIELD
        return false;
    }

    // Off: edge walls stay off. FromStart: on from the start, and now and then toggled on both pipelines. Toggled: off
    // at the start, and now and then toggled. Edge walls make the columns at the view's edge mesh against air.
    enum class EShiftTestEdges
    {
        Off,
        FromStart,
        Toggled
    };

    struct FShiftTestConfig
    {
        const TCHAR* Name;
        bool bDecorate;
        int32 BaseView;
        // 0: no far ring; otherwise the far radius starts at the view radius plus this, and then changes on its own.
        int32 FarExtra;
        EShiftTestEdges Edges;
        FIntPoint Start;
        int32 Steps;
        // Moves are rarer, so the slow (decorated) walks build up progress for the shift path to carry.
        bool bCalm = false;
    };

    struct FShiftTestSide
    {
        TUniquePtr<FVoxelColumnPipeline> Pipeline;
        // The columns whose mesh this side's PopReady has handed out and that TakeUnloaded has not reported since.
        TSet<FIntPoint> Taken;
    };

    struct FShiftTestResult
    {
        int32 JobsRun = 0, Meshes = 0, Unloaded = 0, EdgeToggles = 0, FarChanges = 0, CarriedWithEdgeWalls = 0;
        int64 ShiftedPlans = 0, CarriedColumns = 0, FullShiftedPlans = 0;
    };

    // Runs one configuration; returns false (after one error) at the first mismatch.
    bool ShiftTestRun(FAutomationTestBase& Test, const FShiftTestWorld& World, const FShiftTestConfig& Config,
        int32 Salt, FShiftTestResult& Out)
    {
        FShiftTestSide Full, Shift;
        Full.Pipeline = ShiftTestMakePipeline(World, Config.bDecorate, true);
        Shift.Pipeline = ShiftTestMakePipeline(World, Config.bDecorate, false);
        FVoxelColumnPipeline& A = *Full.Pipeline;
        FVoxelColumnPipeline& B = *Shift.Pipeline;
        bool bEdges = Config.Edges == EShiftTestEdges::FromStart;
        if (bEdges)
        {
            A.SetEdgeWalls(true);
            B.SetEdgeWalls(true);
        }
        FRandomStream Random(
            ShiftTestSeedValue + Config.BaseView * 7 + (Config.bDecorate ? 1 : 0) + Config.FarExtra * 13 + Salt * 101);
        FIntPoint Center = Config.Start;
        int32 View = Config.BaseView, Sector = 0;
        int32 FarRadius = Config.FarExtra > 0 ? View + Config.FarExtra : 0;
        const auto Fail = [&](int32 Step, const FString& What, const FString& ValueA, const FString& ValueB)
        {
            Test.AddError(FString::Printf(TEXT("%s, step %d (center %s, view %d, far %d, sector %d, edge walls %d): %s "
                                               "differs: full path %s, shift path %s"),
                Config.Name, Step, *ShiftTestPoint(Center), View, FarRadius, Sector, bEdges ? 1 : 0, *What, *ValueA,
                *ValueB));
            return false;
        };
        for (int32 Step = 0; Step < Config.Steps; ++Step)
        {
            // Three independent changes, each rolled on its own, so a step can do several.
            // The move. Now and then a teleport far beyond the unload distance on one axis (about 1 step in 50): the
            // planned shift keeps the shape, so the shift path must handle a jump that leaves nothing of the old plan.
            if (Random.RandRange(0, Config.bCalm ? 99 : 49) == 0)
            {
                const int32 Jump = Random.RandRange(40, 60) * (Random.RandRange(0, 1) ? 1 : -1);
                Center += Random.RandRange(0, 1) ? FIntPoint(Jump, 0) : FIntPoint(0, Jump);
            }
            else
            {
                const int32 Roll = Random.RandRange(0, 99);
                if (Roll < (Config.bCalm ? 25 : 55))
                    Center += FIntPoint(Random.RandRange(-1, 1), Random.RandRange(-1, 1));
                else if (Roll < (Config.bCalm ? 35 : 70))
                    Center += FIntPoint(Random.RandRange(-10, 10), Random.RandRange(-10, 10));
            }
            // The sector.
            if (Random.RandRange(0, 99) < 12) Sector = Random.RandRange(0, VoxelStreaming::ViewSectorCount - 1);
            // The shape: the view radius, and the far radius on its own (never to the view radius or below, unless it
            // goes to none and back).
            if (Random.RandRange(0, 99) < 5)
            {
                View = FMath::Clamp(View + (Random.RandRange(0, 1) ? 1 : -1), Config.BaseView - 1, Config.BaseView + 1);
                if (FarRadius > 0) FarRadius = FMath::Max(FarRadius, View + 1);
            }
            if (Config.FarExtra > 0 && Random.RandRange(0, 99) < 8)
            {
                ++Out.FarChanges;
                if (FarRadius == 0 || Random.RandRange(0, 99) < 15)
                    FarRadius = FarRadius > 0 ? 0 : View + Config.FarExtra;
                else
                    FarRadius = FMath::Clamp(FarRadius + Random.RandRange(1, 2) * (Random.RandRange(0, 1) ? 1 : -1),
                        View + 1, View + Config.FarExtra + 2);
            }
            // Edge walls.
            if (Config.Edges != EShiftTestEdges::Off && Random.RandRange(0, 99) < 2)
            {
                bEdges = !bEdges;
                A.SetEdgeWalls(bEdges);
                B.SetEdgeWalls(bEdges);
                ++Out.EdgeToggles;
            }
            const FVoxelStreamingRings Rings(Center, View, ShiftTestMargin, Config.bDecorate ? 3 : 1, FarRadius);
            const int64 CarriedBefore = B.GetStats().CarriedColumns;
            A.SetFocus(Rings, Sector);
            B.SetFocus(Rings, Sector);
            if (bEdges && B.GetStats().CarriedColumns != CarriedBefore) ++Out.CarriedWithEdgeWalls;
            A.Update();
            B.Update();
            // Jobs: a few, or (now and then) all of them for a while, so the walk's progress builds up.
            const bool bSettle = Random.RandRange(0, 99) < 30;
            const int32 Rounds = bSettle ? 4 : 1;
            for (int32 Round = 0; Round < Rounds; ++Round)
            {
                if (A.GetJobCount() != B.GetJobCount())
                    return Fail(Step, TEXT("job count before running"), FString::FromInt(A.GetJobCount()),
                        FString::FromInt(B.GetJobCount()));
                for (int32 Index = 0; Index < A.GetJobCount(); ++Index)
                    if (A.GetJobColumn(Index) != B.GetJobColumn(Index) ||
                        A.IsJobPending(Index) != B.IsJobPending(Index))
                        return Fail(Step, FString::Printf(TEXT("job %d before running"), Index),
                            ShiftTestPoint(A.GetJobColumn(Index)) +
                                (A.IsJobPending(Index) ? TEXT(" pending") : TEXT(" done")),
                            ShiftTestPoint(B.GetJobColumn(Index)) +
                                (B.IsJobPending(Index) ? TEXT(" pending") : TEXT(" done")));
                const int32 Count = A.GetJobCount();
                const int32 Runs = bSettle ? Count : FMath::Min(Count, Random.RandRange(0, 4));
                for (int32 Run = 0; Run < Runs; ++Run)
                {
                    const int32 Index = bSettle ? Run : Random.RandRange(0, Count - 1);
                    const bool bRanA = A.RunJob(Index), bRanB = B.RunJob(Index);
                    if (bRanA != bRanB)
                        return Fail(Step, FString::Printf(TEXT("RunJob(%d)"), Index), bRanA ? TEXT("ran") : TEXT("no"),
                            bRanB ? TEXT("ran") : TEXT("no"));
                    Out.JobsRun += bRanA ? 1 : 0;
                }
                A.Update();
                B.Update();
            }
            if (Random.RandRange(0, 99) < 60)
            {
                const int32 Pops = Random.RandRange(1, 4);
                for (int32 Pop = 0; Pop < Pops; ++Pop)
                {
                    FVoxelColumnPipeline::FReady ReadyA, ReadyB;
                    const bool bPoppedA = A.PopReady(ReadyA), bPoppedB = B.PopReady(ReadyB);
                    if (bPoppedA != bPoppedB)
                        return Fail(Step, TEXT("PopReady result"), bPoppedA ? TEXT("a mesh") : TEXT("none"),
                            bPoppedB ? TEXT("a mesh") : TEXT("none"));
                    if (!bPoppedA) break;
                    ++Out.Meshes;
                    Full.Taken.Add(ReadyA.Column);
                    Shift.Taken.Add(ReadyB.Column);
                    if (ReadyA.Column != ReadyB.Column || ReadyA.Mesh->bFar != ReadyB.Mesh->bFar)
                        return Fail(Step, TEXT("PopReady mesh"),
                            ShiftTestPoint(ReadyA.Column) + (ReadyA.Mesh->bFar ? TEXT(" far") : TEXT(" full")),
                            ShiftTestPoint(ReadyB.Column) + (ReadyB.Mesh->bFar ? TEXT(" far") : TEXT(" full")));
                }
            }
            TArray<FIntPoint> UnloadedA, UnloadedB;
            A.TakeUnloaded(UnloadedA);
            B.TakeUnloaded(UnloadedB);
            if (UnloadedA != UnloadedB)
                return Fail(Step, TEXT("TakeUnloaded"), FString::Printf(TEXT("%d columns"), UnloadedA.Num()),
                    FString::Printf(TEXT("%d columns, or another order"), UnloadedB.Num()));
            Out.Unloaded += UnloadedB.Num();
            // Each side reports only columns it handed out, and every handed-out column that lost its record was
            // reported (a record is gone when GetStatus no longer says present).
            for (FShiftTestSide* Side : {&Full, &Shift})
            {
                const bool bIsFull = Side == &Full;
                for (const FIntPoint Column : bIsFull ? UnloadedA : UnloadedB)
                    if (Side->Taken.Remove(Column) == 0)
                    {
                        Test.AddError(FString::Printf(TEXT("%s, step %d: the %s path's TakeUnloaded reported %s, whose "
                                                           "mesh it never handed out (or reported twice)"),
                            Config.Name, Step, bIsFull ? TEXT("full") : TEXT("shift"), *ShiftTestPoint(Column)));
                        return false;
                    }
                for (const FIntPoint Column : Side->Taken)
                    if (!Side->Pipeline->GetStatus(Column).bPresent)
                    {
                        Test.AddError(FString::Printf(TEXT("%s, step %d: the %s path handed out %s, which lost its "
                                                           "record but was not reported by TakeUnloaded"),
                            Config.Name, Step, bIsFull ? TEXT("full") : TEXT("shift"), *ShiftTestPoint(Column)));
                        return false;
                    }
            }
            // Everything either shows.
            if (A.GetJobCount() != B.GetJobCount())
                return Fail(
                    Step, TEXT("job count"), FString::FromInt(A.GetJobCount()), FString::FromInt(B.GetJobCount()));
            for (int32 Index = 0; Index < A.GetJobCount(); ++Index)
                if (A.GetJobColumn(Index) != B.GetJobColumn(Index) || A.IsJobPending(Index) != B.IsJobPending(Index))
                    return Fail(Step, FString::Printf(TEXT("job %d"), Index),
                        ShiftTestPoint(A.GetJobColumn(Index)) +
                            (A.IsJobPending(Index) ? TEXT(" pending") : TEXT(" done")),
                        ShiftTestPoint(B.GetJobColumn(Index)) +
                            (B.IsJobPending(Index) ? TEXT(" pending") : TEXT(" done")));
            const int32 Reach = Rings.GetUnloadDistance() + 2;
            for (int32 DY = -Reach; DY <= Reach; ++DY)
                for (int32 DX = -Reach; DX <= Reach; ++DX)
                {
                    const FIntPoint Column = Center + FIntPoint(DX, DY);
                    FString Field, ValueA, ValueB;
                    if (ShiftTestStatusDiff(A.GetStatus(Column), B.GetStatus(Column), Field, ValueA, ValueB))
                        return Fail(Step,
                            FString::Printf(TEXT("status of column %s, field %s"), *ShiftTestPoint(Column), *Field),
                            ValueA, ValueB);
                }
            const FVoxelPipelineStats StatsA = A.GetStats(), StatsB = B.GetStats();
            if (StatsA.Columns != StatsB.Columns)
                return Fail(
                    Step, TEXT("Stats.Columns"), FString::FromInt(StatsA.Columns), FString::FromInt(StatsB.Columns));
            if (StatsA.Needed != StatsB.Needed)
                return Fail(
                    Step, TEXT("Stats.Needed"), FString::FromInt(StatsA.Needed), FString::FromInt(StatsB.Needed));
            const auto Flag = [](bool bValue) { return bValue ? TEXT("true") : TEXT("false"); };
            if (A.IsViewOnScreen() != B.IsViewOnScreen())
                return Fail(Step, TEXT("IsViewOnScreen"), Flag(A.IsViewOnScreen()), Flag(B.IsViewOnScreen()));
            if (A.IsNearViewOnScreen() != B.IsNearViewOnScreen())
                return Fail(
                    Step, TEXT("IsNearViewOnScreen"), Flag(A.IsNearViewOnScreen()), Flag(B.IsNearViewOnScreen()));
            if (A.IsIdle() != B.IsIdle()) return Fail(Step, TEXT("IsIdle"), Flag(A.IsIdle()), Flag(B.IsIdle()));
        }
        Out.ShiftedPlans = B.GetStats().ShiftedPlans;
        Out.CarriedColumns = B.GetStats().CarriedColumns;
        Out.FullShiftedPlans = A.GetStats().ShiftedPlans;
        return true;
    }

    // The subsystem's chunks as ids, with the pipeline's view of each column: the column map (live chunks, and those of
    // unloaded columns waiting for release), the pool and the pending releases, as UVoxelWorldSubsystem keeps them.
    struct FShiftTestPool
    {
        TMap<int32, int32> Chunks;
        TArray<int32> Pool;
        TVoxelPendingReleases<int32> Pending;
        // Per chunk: the column its mesh shows, or -1 when empty.
        TArray<int32> Mesh;
        // Per column: the pipeline has a record for it, and its latest mesh was taken for the screen.
        TSet<int32> Present, OnScreen;
        int32 Releases = 0, ReleasedOnScreen = 0, KeptWhileWaiting = 0;

        void Load(int32 Column) { Present.Add(Column); }
        // The pipeline unloads the column; TakeUnloaded reports it only when its mesh was on screen.
        void Unload(int32 Column)
        {
            if (!Present.Remove(Column)) return;
            if (OnScreen.Remove(Column) && Chunks.Contains(Column)) Pending.Add(Column);
        }
        // A finished mesh for a present column goes on screen: AcquireChunk, then SetMesh.
        void Apply(int32 Column)
        {
            if (!Present.Contains(Column)) return;
            OnScreen.Add(Column);
            const bool bWaited = Pending.IsWaiting(Column);
            Pending.Remove(Column);
            if (const int32* Id = Chunks.Find(Column))
            {
                if (bWaited) ++KeptWhileWaiting;
                Mesh[*Id] = Column;
                return;
            }
            const int32 Id = Pool.Num() ? Pool.Pop() : Mesh.Add(-1);
            Chunks.Add(Column, Id);
            Mesh[Id] = Column;
        }
        // A finished mesh with no triangles goes on screen: ApplyReady releases the column's chunk at once.
        void ApplyEmpty(int32 Column)
        {
            if (!Present.Contains(Column)) return;
            OnScreen.Add(Column);
            Release(Column, false);
        }
        // ReleaseChunk: empty, hide and pool the column's chunk.
        void Release(int32 Column, bool bFromQueue = true)
        {
            Pending.Remove(Column);
            int32 Id;
            if (!Chunks.RemoveAndCopyValue(Column, Id)) return;
            ++Releases;
            if (bFromQueue && OnScreen.Contains(Column)) ++ReleasedOnScreen;
            Mesh[Id] = -1;
            Pool.Add(Id);
        }
        void Drain(int32 Count)
        {
            int32 Column;
            for (int32 Index = 0; Index < Count && Pending.Pop(Column); ++Index)
                Release(Column);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPipelineShiftTest, "Unnamed.Voxel.Pipeline.ShiftPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPipelineShiftTest::RunTest(const FString& Parameters)
{
    if (!FFoundationBlockRegistry::GetDefault()->GetAssets())
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    FShiftTestWorld World;
    if (!ShiftTestMakeWorld(*this, World)) return false;
    const FIntPoint Zero = FIntPoint::ZeroValue;
    const FShiftTestConfig Configs[] = {
        {TEXT("no decoration, no far ring"), false, 3, 0, EShiftTestEdges::Off, Zero, ShiftTestSteps},
        {TEXT("no decoration, far ring"), false, 3, 3, EShiftTestEdges::Off, Zero, ShiftTestSteps},
        {TEXT("decoration, no far ring"), true, 3, 0, EShiftTestEdges::Off, Zero, ShiftTestSteps, true},
        {TEXT("decoration, far ring"), true, 3, 3, EShiftTestEdges::Off, Zero, ShiftTestSteps, true},
        {TEXT("no decoration, far ring, edge walls from the start"), false, 3, 3, EShiftTestEdges::FromStart, Zero,
            300},
        {TEXT("decoration, no far ring, edge walls from the start"), true, 3, 0, EShiftTestEdges::FromStart, Zero, 200,
            true},
        {TEXT("no decoration, far ring, edge walls toggled on"), false, 3, 3, EShiftTestEdges::Toggled, Zero, 300},
        {TEXT("no decoration, far ring, near (1000000, -1000000)"), false, 3, 3, EShiftTestEdges::Off,
            FIntPoint(1000000, -1000000), 300},
    };
    int32 Salt = 0;
    for (const FShiftTestConfig& Config : Configs)
    {
        const double Start = FPlatformTime::Seconds();
        FShiftTestResult Result;
        const bool bSame = ShiftTestRun(*this, World, Config, Salt++, Result);
        AddInfo(FString::Printf(TEXT("%s: %d steps %s, %d jobs run, %d meshes taken, %d columns reported unloaded, "
                                     "%d far radius changes, %d edge wall toggles, shift pipeline: %lld shifted "
                                     "plans and %lld carried columns, full pipeline: %lld shifted plans, %.1f s"),
            Config.Name, Config.Steps, bSame ? TEXT("identical") : TEXT("STOPPED at a mismatch"), Result.JobsRun,
            Result.Meshes, Result.Unloaded, Result.FarChanges, Result.EdgeToggles, Result.ShiftedPlans,
            Result.CarriedColumns, Result.FullShiftedPlans, FPlatformTime::Seconds() - Start));
        if (!bSame) continue;
        TestTrue(FString::Printf(TEXT("%s: some jobs ran and some meshes were taken"), Config.Name),
            Result.JobsRun > 0 && Result.Meshes > 0);
        TestTrue(
            FString::Printf(TEXT("%s: the shift pipeline made shifted plans"), Config.Name), Result.ShiftedPlans > 0);
        TestEqual(FString::Printf(TEXT("%s: the full pipeline made no shifted plans"), Config.Name),
            Result.FullShiftedPlans, int64(0));
        // With edge walls on the shift path regresses instead of carrying progress, so a step with them on carries
        // nothing.
        TestEqual(FString::Printf(TEXT("%s: steps that carried columns' walks with edge walls on"), Config.Name),
            Result.CarriedWithEdgeWalls, 0);
        if (Config.Edges == EShiftTestEdges::Off)
            TestTrue(FString::Printf(TEXT("%s: the shift pipeline carried some columns' walks"), Config.Name),
                Result.CarriedColumns > 0);
        if (Config.Edges != EShiftTestEdges::Off)
            TestTrue(FString::Printf(TEXT("%s: edge walls were toggled"), Config.Name), Result.EdgeToggles > 0);
        if (Config.FarExtra > 0)
            TestTrue(FString::Printf(TEXT("%s: the far radius changed"), Config.Name), Result.FarChanges > 0);
        TestTrue(FString::Printf(TEXT("%s: columns were reported unloaded"), Config.Name), Result.Unloaded > 0);
    }
    return true;
}

// Far LOD item 8: with bFarInnerFirst an urgent far mesh (a column within ViewRadius + 2 that shows nothing) is taken
// before a full mesh that only refines a column that shows something, and before a full mesh that fills a column but
// is farther; every other far mesh still waits while a full one does. Each take is checked against a model of that
// rule built from the ready columns' statuses (InnerFirstTestExpect), so the order is pinned step by step.
namespace
{
    constexpr int32 InnerFirstTestView = 3;
    constexpr int32 InnerFirstTestFar = 6;

    struct FInnerFirstTestPick
    {
        bool bAny = false;
        FIntPoint Column = FIntPoint::ZeroValue;
        bool bFar = false;
        bool bFullWaiting = false;
        bool bUrgentWaiting = false;
        // With a full mesh and an urgent far one both waiting: the full head refines a column that shows something, and
        // whether the urgent far head is nearer than the full head.
        bool bFullHeadRefines = false;
        bool bFarNearer = false;
        // A far pick that is urgent (inside the band, shows nothing).
        bool bUrgent = false;
    };

    struct FInnerFirstTestStats
    {
        int32 Pops = 0, FarPops = 0;
        // Far meshes taken while a full mesh waited, in all, and those that were not urgent.
        int32 FarWhileFull = 0, NonUrgentFarWhileFull = 0;
        // Urgent far meshes taken while a full mesh waited, by what the full queue head was (a refine of a column that
        // shows something, or a fill of one that shows nothing) and whether the far mesh was nearer than that head.
        int32 FarOverNearerRefineHead = 0, FarOverFartherRefineHead = 0, FarOverFartherFillHead = 0,
              FarOverNearerFillHead = 0;
    };

    int64 InnerFirstTestPriority(const FVoxelStreamingRings& Rings, int32 Sector, FIntPoint Column)
    {
        const FIntPoint Ahead = VoxelStreaming::GetViewSectorDirection(Sector);
        const FIntPoint Offset = Column - Rings.Center;
        const int64 Distance = int64(Offset.X) * Offset.X + int64(Offset.Y) * Offset.Y;
        return int64(Offset.X) * Ahead.X + int64(Offset.Y) * Ahead.Y < 0 ? 4 * Distance : Distance;
    }

    // The queue order: lower priority first, then lower Y, then lower X.
    bool InnerFirstTestBefore(const FVoxelStreamingRings& Rings, int32 Sector, FIntPoint A, FIntPoint B)
    {
        const int64 PriorityA = InnerFirstTestPriority(Rings, Sector, A),
                    PriorityB = InnerFirstTestPriority(Rings, Sector, B);
        if (PriorityA != PriorityB) return PriorityA < PriorityB;
        return A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
    }

    // The mesh the rule says is taken next.
    FInnerFirstTestPick InnerFirstTestExpect(
        const FVoxelColumnPipeline& Pipeline, const FVoxelStreamingRings& Rings, int32 Sector, bool bInnerFirst)
    {
        FInnerFirstTestPick Pick;
        const int64 Band = int64(Rings.ViewRadius) + 2;
        TOptional<FIntPoint> FullHead, FarHead, UrgentHead;
        bool bFullHeadRefines = false;
        const int32 Reach = Rings.GetUnloadDistance() + 2;
        for (int32 DY = -Reach; DY <= Reach; ++DY)
            for (int32 DX = -Reach; DX <= Reach; ++DX)
            {
                const FIntPoint Column = Rings.Center + FIntPoint(DX, DY);
                const FVoxelColumnStatus Status = Pipeline.GetStatus(Column);
                if (!Status.bReady) continue;
                if (Status.Meshed != EVoxelMeshKind::Far)
                {
                    if (!FullHead || InnerFirstTestBefore(Rings, Sector, Column, *FullHead))
                    {
                        FullHead = Column;
                        bFullHeadRefines = Status.Shown != EVoxelMeshKind::None;
                    }
                    continue;
                }
                if (!FarHead || InnerFirstTestBefore(Rings, Sector, Column, *FarHead)) FarHead = Column;
                const bool bUrgent =
                    bInnerFirst && Rings.GetGapSquared(Column) <= Band * Band && Status.Shown == EVoxelMeshKind::None;
                if (bUrgent && (!UrgentHead || InnerFirstTestBefore(Rings, Sector, Column, *UrgentHead)))
                    UrgentHead = Column;
            }
        Pick.bAny = FullHead || FarHead;
        Pick.bFullWaiting = FullHead.IsSet();
        Pick.bUrgentWaiting = UrgentHead.IsSet();
        if (!Pick.bAny) return Pick;
        Pick.bFar = !FullHead.IsSet();
        if (FullHead && UrgentHead)
        {
            const int64 FarPriority = InnerFirstTestPriority(Rings, Sector, *UrgentHead);
            const int64 FullPriority = InnerFirstTestPriority(Rings, Sector, *FullHead);
            Pick.bFar = bFullHeadRefines || FarPriority < FullPriority;
            Pick.bFullHeadRefines = bFullHeadRefines;
            Pick.bFarNearer = FarPriority < FullPriority;
        }
        if (Pick.bFar)
        {
            Pick.Column = UrgentHead ? *UrgentHead : *FarHead;
            Pick.bUrgent = UrgentHead.IsSet();
        }
        else
            Pick.Column = *FullHead;
        return Pick;
    }

    // Runs the manual jobs until none is left to run; false if the pipeline never settles.
    bool InnerFirstTestSettle(FVoxelColumnPipeline& Pipeline)
    {
        for (int32 Round = 0; Round < 400; ++Round)
        {
            Pipeline.Update();
            bool bRan = false;
            const int32 Count = Pipeline.GetJobCount();
            for (int32 Index = 0; Index < Count; ++Index)
                if (Pipeline.IsJobPending(Index)) bRan |= Pipeline.RunJob(Index);
            if (!bRan) return true;
        }
        return false;
    }

    void InnerFirstTestFocus(
        FVoxelColumnPipeline& Pipeline, FVoxelStreamingRings& Rings, FIntPoint Center, int32 Far, int32 Sector)
    {
        Rings = FVoxelStreamingRings(Center, InnerFirstTestView, ShiftTestMargin, 1, Far);
        Pipeline.SetFocus(Rings, Sector);
        Pipeline.Update();
        TArray<FIntPoint> Unloaded;
        Pipeline.TakeUnloaded(Unloaded);
    }

    // Takes one mesh and checks it against the model and PeekReady; false when nothing was ready.
    bool InnerFirstTestPop(FAutomationTestBase& Test, FVoxelColumnPipeline& Pipeline, const FVoxelStreamingRings& Rings,
        int32 Sector, bool bInnerFirst, const FString& Label, FInnerFirstTestStats& Stats, FIntPoint& OutColumn,
        bool& bOutFar)
    {
        const FInnerFirstTestPick Pick = InnerFirstTestExpect(Pipeline, Rings, Sector, bInnerFirst);
        FIntPoint Peeked(MAX_int32, MAX_int32);
        const FVoxelColumnMesh* PeekedMesh = Pipeline.PeekReady(&Peeked);
        FVoxelColumnPipeline::FReady Taken;
        const bool bTaken = Pipeline.PopReady(Taken);
        if (bTaken != Pick.bAny || bTaken != (PeekedMesh != nullptr))
        {
            Test.AddError(FString::Printf(TEXT("%s: model expects %s, PeekReady %s, PopReady %s"), *Label,
                Pick.bAny ? TEXT("a mesh") : TEXT("none"), PeekedMesh ? TEXT("a mesh") : TEXT("none"),
                bTaken ? TEXT("took one") : TEXT("took none")));
            return false;
        }
        if (!bTaken) return false;
        OutColumn = Taken.Column;
        bOutFar = Taken.Mesh->bFar;
        ++Stats.Pops;
        if (Taken.Column != Peeked || Taken.Mesh.Get() != PeekedMesh)
            Test.AddError(FString::Printf(TEXT("%s: PeekReady showed (%d, %d), PopReady took (%d, %d)"), *Label,
                Peeked.X, Peeked.Y, Taken.Column.X, Taken.Column.Y));
        if (Taken.Column != Pick.Column || Taken.Mesh->bFar != Pick.bFar)
            Test.AddError(FString::Printf(TEXT("%s: took (%d, %d) %s, the rule says (%d, %d) %s"), *Label,
                Taken.Column.X, Taken.Column.Y, Taken.Mesh->bFar ? TEXT("far") : TEXT("full"), Pick.Column.X,
                Pick.Column.Y, Pick.bFar ? TEXT("far") : TEXT("full")));
        if (Taken.Mesh->bFar)
        {
            ++Stats.FarPops;
            if (Pick.bFullWaiting)
            {
                ++Stats.FarWhileFull;
                if (!Pick.bUrgent) ++Stats.NonUrgentFarWhileFull;
                if (Pick.bUrgent)
                {
                    if (Pick.bFullHeadRefines)
                        ++(Pick.bFarNearer ? Stats.FarOverFartherRefineHead : Stats.FarOverNearerRefineHead);
                    else
                        ++(Pick.bFarNearer ? Stats.FarOverFartherFillHead : Stats.FarOverNearerFillHead);
                }
            }
        }
        return true;
    }

    TUniquePtr<FVoxelColumnPipeline> InnerFirstTestMakePipeline(const FShiftTestWorld& World, bool bInnerFirst)
    {
        TUniquePtr<FVoxelColumnPipeline> Pipeline = ShiftTestMakePipeline(World, false, false);
        Pipeline->SetFarInnerFirst(bInnerFirst);
        return Pipeline;
    }

    // Takes every ready mesh.
    void InnerFirstTestDrain(FAutomationTestBase& Test, FVoxelColumnPipeline& Pipeline,
        const FVoxelStreamingRings& Rings, int32 Sector, bool bInnerFirst, const FString& Label,
        FInnerFirstTestStats& Stats)
    {
        FIntPoint Column;
        bool bFar;
        while (InnerFirstTestPop(Test, Pipeline, Rings, Sector, bInnerFirst, Label, Stats, Column, bFar))
        {
        }
    }

    // a, b: one settled ring at the center with nothing on screen, drained under the rule.
    void InnerFirstTestFill(FAutomationTestBase& Test, const FShiftTestWorld& World, bool bInnerFirst)
    {
        const FString Label = bInnerFirst ? TEXT("b (flag on, empty)") : TEXT("a (flag off)");
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = InnerFirstTestMakePipeline(World, bInnerFirst);
        FVoxelStreamingRings Rings;
        InnerFirstTestFocus(*Pipeline, Rings, FIntPoint::ZeroValue, InnerFirstTestFar, 0);
        if (!Test.TestTrue(*(Label + TEXT(": jobs settle")), InnerFirstTestSettle(*Pipeline))) return;
        FInnerFirstTestStats Stats;
        InnerFirstTestDrain(Test, *Pipeline, Rings, 0, bInnerFirst, Label, Stats);
        Test.TestTrue(
            *(Label + TEXT(": full and far meshes were taken")), Stats.FarPops > 0 && Stats.Pops > Stats.FarPops);
        Test.TestEqual(*(Label + TEXT(": far meshes taken beyond the band while a full one waited")),
            Stats.NonUrgentFarWhileFull, 0);
        if (bInnerFirst)
        {
            Test.TestTrue(
                *(Label + TEXT(": some urgent far mesh was taken while the full head filled a farther column")),
                Stats.FarOverFartherFillHead > 0);
            Test.TestEqual(*(Label + TEXT(": far meshes taken while the full head filled a nearer column")),
                Stats.FarOverNearerFillHead, 0);
            Test.TestEqual(
                *(Label + TEXT(": far meshes taken while the full head refined a column (nothing shows yet)")),
                Stats.FarOverNearerRefineHead + Stats.FarOverFartherRefineHead, 0);
        }
        else
            Test.TestEqual(*(Label + TEXT(": far meshes taken while a full one waited")), Stats.FarWhileFull, 0);
        Test.TestTrue(*(Label + TEXT(": idle at the end")), Pipeline->IsIdle());
    }

    // c: far meshes on screen, a move east brings some of them into view (their full meshes refine) and uncovers fresh
    // ground, whose far meshes within the band are urgent. The refining full meshes at the new center are nearer than
    // those urgent ones, so a far mesh going first can only be the refine rule.
    void InnerFirstTestRefine(FAutomationTestBase& Test, const FShiftTestWorld& World)
    {
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = InnerFirstTestMakePipeline(World, true);
        FVoxelStreamingRings Rings;
        FInnerFirstTestStats Stats;
        InnerFirstTestFocus(*Pipeline, Rings, FIntPoint::ZeroValue, InnerFirstTestFar, 0);
        if (!Test.TestTrue(TEXT("c: first jobs settle"), InnerFirstTestSettle(*Pipeline))) return;
        InnerFirstTestDrain(Test, *Pipeline, Rings, 0, true, TEXT("c (setup)"), Stats);
        const FIntPoint Home(4, 0);
        InnerFirstTestFocus(*Pipeline, Rings, Home, InnerFirstTestFar, 0);
        if (!Test.TestTrue(TEXT("c: jobs after the move settle"), InnerFirstTestSettle(*Pipeline))) return;
        int32 Refining = 0, FreshUrgent = 0;
        const int64 Band = int64(InnerFirstTestView) + 2;
        for (int32 DY = -9; DY <= 9; ++DY)
            for (int32 DX = -9; DX <= 9; ++DX)
            {
                const FIntPoint Column = Home + FIntPoint(DX, DY);
                const FVoxelColumnStatus Status = Pipeline->GetStatus(Column);
                if (!Status.bReady) continue;
                if (Rings.IsInView(Column) && Status.Shown == EVoxelMeshKind::Far) ++Refining;
                if (!Rings.IsInView(Column) && Status.Shown == EVoxelMeshKind::None &&
                    Rings.GetGapSquared(Column) <= Band * Band)
                    ++FreshUrgent;
            }
        Test.TestTrue(TEXT("c: full meshes wait that refine a far one"), Refining > 0);
        Test.TestTrue(TEXT("c: urgent far meshes wait on fresh ground"), FreshUrgent > 0);
        FInnerFirstTestStats Moved;
        InnerFirstTestDrain(Test, *Pipeline, Rings, 0, true, TEXT("c (after the move)"), Moved);
        Test.TestTrue(
            TEXT("c: some urgent far mesh was taken while the full head was a nearer column that refines a far "
                 "one"),
            Moved.FarOverNearerRefineHead > 0);
        Test.TestEqual(
            TEXT("c: far meshes taken while the full head filled a nearer column"), Moved.FarOverNearerFillHead, 0);
        Test.TestEqual(
            TEXT("c: far meshes taken beyond the band while a full one waited"), Moved.NonUrgentFarWhileFull, 0);
        Test.TestTrue(TEXT("c: idle at the end"), Pipeline->IsIdle());
    }

    // e: the band's edge, with literal columns straight ahead of the center on an axis (view 3, far 6, nothing on
    // screen): the far mesh at gap exactly ViewRadius + 2 (offset ViewRadius + 3) is urgent, and goes before full
    // meshes still waiting; the one at gap ViewRadius + 3 (offset ViewRadius + 4) is not, and goes after the last one.
    void InnerFirstTestBand(FAutomationTestBase& Test, const FShiftTestWorld& World)
    {
        const TUniquePtr<FVoxelColumnPipeline> Pipeline = InnerFirstTestMakePipeline(World, true);
        FVoxelStreamingRings Rings;
        InnerFirstTestFocus(*Pipeline, Rings, FIntPoint::ZeroValue, InnerFirstTestFar, 0);
        if (!Test.TestTrue(TEXT("e: jobs settle"), InnerFirstTestSettle(*Pipeline))) return;
        const FIntPoint Ahead = VoxelStreaming::GetViewSectorDirection(0);
        const FIntPoint Axis = Ahead.X != 0 ? FIntPoint(Ahead.X > 0 ? 1 : -1, 0) : FIntPoint(0, Ahead.Y > 0 ? 1 : -1);
        const FIntPoint InBand = Axis * (InnerFirstTestView + 3), OutOfBand = Axis * (InnerFirstTestView + 4);
        const int64 Band = int64(InnerFirstTestView) + 2;
        Test.TestEqual(
            TEXT("e: the first column's gap is exactly ViewRadius + 2"), Rings.GetGapSquared(InBand), Band * Band);
        Test.TestEqual(TEXT("e: the second column's gap is exactly ViewRadius + 3"), Rings.GetGapSquared(OutOfBand),
            (Band + 1) * (Band + 1));
        for (const FIntPoint Column : {InBand, OutOfBand})
        {
            const FVoxelColumnStatus Status = Pipeline->GetStatus(Column);
            if (!Test.TestTrue(*FString::Printf(TEXT("e: column %s is a ready far mesh that shows nothing"),
                                   *ShiftTestPoint(Column)),
                    Status.bReady && Status.Meshed == EVoxelMeshKind::Far && Status.Shown == EVoxelMeshKind::None))
                return;
        }
        FInnerFirstTestStats Stats;
        TArray<FIntPoint> Taken;
        TArray<bool> TakenFar;
        FIntPoint Column;
        bool bFar = false;
        while (InnerFirstTestPop(Test, *Pipeline, Rings, 0, true, TEXT("e"), Stats, Column, bFar))
        {
            Taken.Add(Column);
            TakenFar.Add(bFar);
        }
        int32 LastFull = INDEX_NONE;
        for (int32 Index = 0; Index < Taken.Num(); ++Index)
            if (!TakenFar[Index]) LastFull = Index;
        const int32 InBandIndex = Taken.Find(InBand), OutOfBandIndex = Taken.Find(OutOfBand);
        if (!Test.TestTrue(
                TEXT("e: both columns were taken"), InBandIndex != INDEX_NONE && OutOfBandIndex != INDEX_NONE))
            return;
        Test.TestTrue(TEXT("e: the far mesh at gap ViewRadius + 2 was taken while a full mesh still waited"),
            InBandIndex < LastFull);
        Test.TestTrue(TEXT("e: the far mesh at gap ViewRadius + 3 was taken after the last full mesh"),
            OutOfBandIndex > LastFull);
    }

    // d: without a far ring the flag changes nothing: the same session in both gives the same takes.
    void InnerFirstTestNoFar(FAutomationTestBase& Test, const FShiftTestWorld& World)
    {
        struct FStep
        {
            FIntPoint Center;
            int32 Sector;
            // Meshes taken after the step; negative takes all.
            int32 Takes;
        };
        const FStep Steps[] = {
            {FIntPoint(0, 0), 0, 20},
            {FIntPoint(2, 1), 0, 10},
            {FIntPoint(2, 1), 3, 5},
            {FIntPoint(5, -3), 2, 30},
            {FIntPoint(-4, 4), 1, 0},
            {FIntPoint(-4, 4), 1, -1},
        };
        const TUniquePtr<FVoxelColumnPipeline> Off = InnerFirstTestMakePipeline(World, false);
        const TUniquePtr<FVoxelColumnPipeline> On = InnerFirstTestMakePipeline(World, true);
        FVoxelStreamingRings RingsOff, RingsOn;
        FInnerFirstTestStats StatsOff, StatsOn;
        int32 Index = 0, Compared = 0;
        for (const FStep& Step : Steps)
        {
            const FString Label = FString::Printf(TEXT("d step %d"), Index++);
            InnerFirstTestFocus(*Off, RingsOff, Step.Center, 0, Step.Sector);
            InnerFirstTestFocus(*On, RingsOn, Step.Center, 0, Step.Sector);
            if (!Test.TestTrue(
                    *(Label + TEXT(": jobs settle")), InnerFirstTestSettle(*Off) && InnerFirstTestSettle(*On)))
                return;
            for (int32 Take = 0; Step.Takes < 0 || Take < Step.Takes; ++Take)
            {
                FIntPoint ColumnOff, ColumnOn;
                bool bFarOff = false, bFarOn = false;
                const bool bOff =
                    InnerFirstTestPop(Test, *Off, RingsOff, Step.Sector, false, Label, StatsOff, ColumnOff, bFarOff);
                const bool bOn =
                    InnerFirstTestPop(Test, *On, RingsOn, Step.Sector, true, Label, StatsOn, ColumnOn, bFarOn);
                if (bOff != bOn || (bOff && (ColumnOff != ColumnOn || bFarOff != bFarOn)))
                {
                    Test.AddError(FString::Printf(TEXT("%s take %d: flag off (%d, %d), flag on (%d, %d)"), *Label, Take,
                        ColumnOff.X, ColumnOff.Y, ColumnOn.X, ColumnOn.Y));
                    return;
                }
                if (!bOff) break;
                ++Compared;
            }
        }
        Test.TestTrue(TEXT("d: a session of meshes was compared"), Compared > 100);
        Test.TestEqual(TEXT("d: no far meshes without a far ring"), StatsOn.FarPops + StatsOff.FarPops, 0);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPipelineInnerFirstTest, "Unnamed.Voxel.Pipeline.InnerFirst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPipelineInnerFirstTest::RunTest(const FString& Parameters)
{
    if (!FFoundationBlockRegistry::GetDefault()->GetAssets())
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    FShiftTestWorld World;
    if (!ShiftTestMakeWorld(*this, World)) return false;
    const double Start = FPlatformTime::Seconds();
    InnerFirstTestFill(*this, World, false);
    InnerFirstTestFill(*this, World, true);
    InnerFirstTestRefine(*this, World);
    InnerFirstTestBand(*this, World);
    InnerFirstTestNoFar(*this, World);
    AddInfo(FString::Printf(TEXT("inner-first cases: %.1f s"), FPlatformTime::Seconds() - Start));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPendingReleasesTest, "Unnamed.Voxel.PendingReleases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPendingReleasesTest::RunTest(const FString& Parameters)
{
    // FIFO pops.
    {
        TVoxelPendingReleases<int32> Queue;
        for (int32 Item : {5, 3, 9})
            Queue.Add(Item);
        TestEqual(TEXT("three wait"), Queue.Num(), 3);
        int32 Out = -1;
        TArray<int32> Popped;
        while (Queue.Pop(Out))
            Popped.Add(Out);
        TestTrue(TEXT("pops come oldest first"), Popped == TArray<int32>({5, 3, 9}));
        TestEqual(TEXT("none waits afterwards"), Queue.Num(), 0);
        TestFalse(TEXT("an empty queue pops nothing"), Queue.Pop(Out));
    }
    // Remove before Pop: never popped.
    {
        TVoxelPendingReleases<int32> Queue;
        Queue.Add(1);
        Queue.Add(2);
        Queue.Add(3);
        Queue.Remove(2);
        TestEqual(TEXT("two wait after a remove"), Queue.Num(), 2);
        int32 Out = -1;
        TArray<int32> Popped;
        while (Queue.Pop(Out))
            Popped.Add(Out);
        TestTrue(TEXT("a removed item is never popped"), Popped == TArray<int32>({1, 3}));
        Queue.Remove(7);
        TestEqual(TEXT("removing an unknown item changes nothing"), Queue.Num(), 0);
    }
    // Add twice while waiting keeps one entry, in its first place.
    {
        TVoxelPendingReleases<int32> Queue;
        Queue.Add(1);
        Queue.Add(2);
        Queue.Add(1);
        TestEqual(TEXT("an item added twice waits once"), Queue.Num(), 2);
        int32 Out = -1;
        TArray<int32> Popped;
        while (Queue.Pop(Out))
            Popped.Add(Out);
        TestTrue(TEXT("it pops once, in its first place"), Popped == TArray<int32>({1, 2}));
    }
    // Add, Remove, Add again pops exactly once.
    {
        TVoxelPendingReleases<int32> Queue;
        Queue.Add(4);
        Queue.Remove(4);
        Queue.Add(4);
        TestEqual(TEXT("one waits"), Queue.Num(), 1);
        int32 Out = -1;
        int32 Pops = 0;
        while (Queue.Pop(Out))
        {
            TestEqual(TEXT("the item"), Out, 4);
            ++Pops;
        }
        TestEqual(TEXT("popped exactly once"), Pops, 1);
    }
    // Peak, and Reset.
    {
        TVoxelPendingReleases<int32> Queue;
        for (int32 Item = 0; Item < 10; ++Item)
            Queue.Add(Item);
        int32 Out = -1;
        for (int32 Index = 0; Index < 4; ++Index)
            Queue.Pop(Out);
        Queue.Add(100);
        TestEqual(TEXT("the peak is the most that waited at once"), Queue.GetPeak(), 10);
        Queue.Reset();
        TestEqual(TEXT("reset empties the queue"), Queue.Num(), 0);
        TestFalse(TEXT("nothing pops after a reset"), Queue.Pop(Out));
        TestEqual(TEXT("the peak survives a reset"), Queue.GetPeak(), 10);
    }
    // The peak counts the items waiting, so a removal lowers what the next adds build on.
    {
        TVoxelPendingReleases<int32> Queue;
        Queue.Add(1);
        Queue.Add(2);
        Queue.Add(3);
        Queue.Remove(2);
        Queue.Add(4);
        Queue.Add(5);
        TestEqual(TEXT("four wait after adds 1 2 3, remove 2, adds 4 5"), Queue.Num(), 4);
        TestEqual(TEXT("the peak is 4, not the 5 adds"), Queue.GetPeak(), 4);
    }
    // 200 adds with every other one removed: each remaining exactly once, in order.
    {
        TVoxelPendingReleases<int32> Queue;
        for (int32 Item = 0; Item < 200; ++Item)
            Queue.Add(Item);
        for (int32 Item = 0; Item < 200; Item += 2)
            Queue.Remove(Item);
        TestEqual(TEXT("100 wait"), Queue.Num(), 100);
        TArray<int32> Popped;
        int32 Out = -1;
        while (Queue.Pop(Out))
            Popped.Add(Out);
        bool bExpected = Popped.Num() == 100;
        for (int32 Index = 0; bExpected && Index < Popped.Num(); ++Index)
            bExpected = Popped[Index] == 2 * Index + 1;
        TestTrue(TEXT("the odd items pop once each, in order"), bExpected);
    }
    // The reload-before-release path: a column unloads, comes back and gets a mesh before its release. It keeps the
    // same chunk, live, and the drain releases nothing; unloading again releases it once.
    {
        FShiftTestPool Pool;
        Pool.Load(1);
        Pool.Apply(1);
        const int32 Chunk = Pool.Chunks.FindRef(1);
        Pool.Unload(1);
        TestTrue(TEXT("the unloaded column waits"), Pool.Pending.IsWaiting(1));
        Pool.Load(1);
        Pool.Apply(1);
        TestFalse(TEXT("back on screen, it no longer waits"), Pool.Pending.IsWaiting(1));
        Pool.Drain(MAX_int32);
        TestEqual(TEXT("it keeps its chunk"), Pool.Chunks.FindRef(1), Chunk);
        TestEqual(TEXT("the chunk still shows its mesh"), Pool.Mesh[Chunk], 1);
        TestEqual(TEXT("nothing was released"), Pool.Releases, 0);
        Pool.Unload(1);
        Pool.Unload(1);
        Pool.Drain(MAX_int32);
        TestEqual(TEXT("unloading again releases it once"), Pool.Releases, 1);
        TestTrue(TEXT("its chunk is pooled, empty"), Pool.Pool.Contains(Chunk) && Pool.Mesh[Chunk] < 0);
        // Unloaded, back without a mesh, unloaded again (not reported a second time): still released, no leak.
        Pool.Load(2);
        Pool.Apply(2);
        Pool.Unload(2);
        Pool.Load(2);
        Pool.Unload(2);
        Pool.Drain(MAX_int32);
        TestFalse(TEXT("a column back without a mesh and gone again leaves no chunk"), Pool.Chunks.Contains(2));
        TestEqual(TEXT("and is released once"), Pool.Releases, 2);
    }
    // The subsystem's reload-before-release path, over a fixed-seed random sequence.
    {
        FShiftTestPool Pool;
        FRandomStream Random(1998);
        constexpr int32 Columns = 40;
        for (int32 Step = 0; Step < 20000; ++Step)
        {
            const int32 Roll = Random.RandRange(0, 99);
            const int32 Column = Random.RandRange(0, Columns - 1);
            if (Roll < 25)
                Pool.Load(Column);
            else if (Roll < 50)
                Pool.Unload(Column);
            else if (Roll < 75)
                Pool.Apply(Column);
            else if (Roll < 78)
                Pool.ApplyEmpty(Column);
            else if (Roll < 84)
                Pool.Drain(Random.RandRange(1, 3));
        }
        const int32 ReleasesBeforeFinal = Pool.Releases;
        Pool.Drain(MAX_int32);
        TestTrue(TEXT("the random sequence released some chunks"), ReleasesBeforeFinal > 100);
        TestTrue(TEXT("some columns came back and kept their chunk while waiting"), Pool.KeptWhileWaiting > 10);
        TestEqual(TEXT("a waiting release never takes a column's chunk while its mesh is on screen"),
            Pool.ReleasedOnScreen, 0);
        TestEqual(TEXT("nothing waits after draining"), Pool.Pending.Num(), 0);
        bool bPooledEmpty = true, bChunksOnScreen = true;
        for (const int32 Id : Pool.Pool)
            bPooledEmpty &= Pool.Mesh[Id] < 0;
        // After the drain, only columns on screen hold chunks, each showing its own mesh: none leaked.
        for (const TPair<int32, int32>& Pair : Pool.Chunks)
            bChunksOnScreen &= Pool.OnScreen.Contains(Pair.Key) && Pool.Mesh[Pair.Value] == Pair.Key;
        TestTrue(TEXT("every pooled chunk is empty"), bPooledEmpty);
        TestTrue(TEXT("every chunk left belongs to a column on screen and shows its mesh"), bChunksOnScreen);
    }
    return true;
}
#endif
