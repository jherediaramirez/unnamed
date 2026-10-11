#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelWriteLog.h"
#include "Generation/VoxelArea.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Storage/VoxelColumn.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "VoxelBench.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelConstants.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // For manual runs: 0 none, 1 ApplyToFinal (ReturnTrip must fail), 2 MissingBucketAsEmpty (EvictionFuzz must fail),
    // 3 KeepSnapshots (Stages must fail).
    constexpr int32 StageTestMutation = 0;
    constexpr uint64 StageTestSeed = 263;
    // Columns checked around the center, and the oracle's radius.
    constexpr int32 StageScanExtent = 10;
    constexpr int32 StageOracleRadius = 9;
    constexpr int32 StageRoundCap = 2000;
    constexpr int32 StageErrorCap = 20;

    // The world every test shares: the overworld preset with caves and its decorator.
    struct FStageTestWorld
    {
        TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
        FVoxelTerrainPreset Preset;
        FVoxelBiomeTable Biomes;
        FVoxelSurfaceInputs Surface;
        FVoxelFeatureGraph Graph;
        TSharedPtr<FVoxelDensityTerrain> Terrain;
        TSharedPtr<FVoxelDecorator> Decorator;
    };

    bool StageMakeWorld(FAutomationTestBase& Test, FStageTestWorld& Out)
    {
        FString Error;
        if (!Test.TestTrue(FString::Printf(TEXT("overworld inputs read (%s)"), *Error),
                VoxelTerrainFactory::ReadInputs(
                    VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), Out.Preset, Out.Biomes, Out.Surface, Error)))
            return false;
        if (!Test.TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
                VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Out.Graph, Error)))
            return false;
        FVoxelDensityTerrainSettings Settings;
        Settings.bCaves = true;
        Out.Terrain = MakeShared<FVoxelDensityTerrain>(
            Out.Preset, StageTestSeed, *Out.Table, Settings, &Out.Biomes, &Out.Surface);
        if (!Test.TestTrue(
                FString::Printf(TEXT("terrain is valid (%s)"), *Out.Terrain->GetError()), Out.Terrain->IsValid()))
            return false;
        Out.Decorator = MakeShared<FVoxelDecorator>();
        return Test.TestTrue(FString::Printf(TEXT("the decorator compiles (%s)"), *Error),
            Out.Decorator->Initialize(Out.Graph, *Out.Table, Out.Terrain->GetBiomeSource()->GetTable(),
                Out.Terrain->GetSeed(), FVoxelDecorationOptions(), &Error));
    }

    bool StageHasBlocks(FAutomationTestBase& Test)
    {
        if (VoxelBlocks::GetDefault()->FindState(TEXT("stone"))) return true;
        Test.AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }

    TUniquePtr<FVoxelColumnPipeline> StageMakePipeline(const FStageTestWorld& World, int32 MaxTasks, bool bManual)
    {
        FVoxelColumnPipeline::FSettings Settings;
        Settings.MaxTasksInFlight = MaxTasks;
        Settings.bManualJobs = bManual;
        Settings.Decorator = World.Decorator;
        if (StageTestMutation == 1) Settings.Mutation = EVoxelPipelineMutation::ApplyToFinal;
        if (StageTestMutation == 2) Settings.Mutation = EVoxelPipelineMutation::MissingBucketAsEmpty;
        if (StageTestMutation == 3) Settings.Mutation = EVoxelPipelineMutation::KeepSnapshots;
        return MakeUnique<FVoxelColumnPipeline>(World.Table, World.Terrain.ToSharedRef(), Settings);
    }

    FVoxelStreamingRings StageRings(FIntPoint Center)
    {
        return FVoxelStreamingRings(Center, 2, 2);
    }

    // Lists errors up to a cap.
    struct FStageReport
    {
        explicit FStageReport(FAutomationTestBase& InTest) : Test(InTest) {}

        FAutomationTestBase& Test;
        int32 Errors = 0;

        void Fail(const FString& Message)
        {
            if (Errors < StageErrorCap)
                Test.AddError(Message);
            else if (Errors == StageErrorCap)
                Test.AddError(TEXT("Further errors are not listed."));
            ++Errors;
        }
    };

    // The invariants that hold after every Update (a to f of the spec), with the previous check's state for "newly".
    struct FStageChecker
    {
        FStageReport& Report;
        FString Name;
        int32 Limit;
        TSet<FIntPoint> PrevFinalizing, PrevDecorating;

        FStageChecker(FStageReport& InReport, const FString& InName, int32 InLimit)
            : Report(InReport), Name(InName), Limit(InLimit)
        {
        }

        void Check(const FVoxelColumnPipeline& Pipeline, const FString& Step)
        {
            const int32 Jobs = Pipeline.GetJobCount();
            if (Jobs > Limit)
                Report.Fail(
                    FString::Printf(TEXT("%s, %s: %d jobs in flight, the limit is %d"), *Name, *Step, Jobs, Limit));
            if (Pipeline.GetStats().BucketsAppliedToFinal != 0)
                Report.Fail(FString::Printf(TEXT("%s, %s: %lld buckets applied to a final column"), *Name, *Step,
                    Pipeline.GetStats().BucketsAppliedToFinal));
            const FIntPoint Center = Pipeline.GetRings().Center;
            TSet<FIntPoint> Finalizing, Decorating;
            for (int32 DY = -StageScanExtent; DY <= StageScanExtent; ++DY)
                for (int32 DX = -StageScanExtent; DX <= StageScanExtent; ++DX)
                {
                    const FIntPoint Column(Center.X + DX, Center.Y + DY);
                    const FVoxelColumnStatus Status = Pipeline.GetStatus(Column);
                    if (Status.bMeshing)
                        for (int32 NY = -1; NY <= 1; ++NY)
                            for (int32 NX = -1; NX <= 1; ++NX)
                                if (!Pipeline.GetStatus(Column + FIntPoint(NX, NY)).bGenerated)
                                    Report.Fail(
                                        FString::Printf(TEXT("%s, %s: (%d, %d) is meshing, but (%d, %d) is not final"),
                                            *Name, *Step, Column.X, Column.Y, Column.X + NX, Column.Y + NY));
                    if (Status.bFinalizing)
                    {
                        Finalizing.Add(Column);
                        if (!PrevFinalizing.Contains(Column))
                            for (int32 NY = -1; NY <= 1; ++NY)
                                for (int32 NX = -1; NX <= 1; ++NX)
                                {
                                    const FIntPoint Source = Column + FIntPoint(NX, NY);
                                    const FVoxelColumnStatus SourceStatus = Pipeline.GetStatus(Source);
                                    // The target's offset from the source is the negated offset.
                                    const int32 Bucket = FVoxelWriteLog::GetBucket(-NX, -NY);
                                    if (StageTestMutation != 2 && !(SourceStatus.HeldBuckets >> Bucket & 1))
                                        Report.Fail(FString::Printf(
                                            TEXT("%s, %s: (%d, %d) started finalizing, but source (%d, %d) does not "
                                                 "hold its bucket"),
                                            *Name, *Step, Column.X, Column.Y, Source.X, Source.Y));
                                }
                    }
                    if (Status.bDecorating)
                    {
                        Decorating.Add(Column);
                        if (!PrevDecorating.Contains(Column))
                            for (int32 NY = -1; NY <= 1; ++NY)
                                for (int32 NX = -1; NX <= 1; ++NX)
                                    if (!Pipeline.GetStatus(Column + FIntPoint(NX, NY)).bSnapshot)
                                        Report.Fail(FString::Printf(
                                            TEXT("%s, %s: (%d, %d) started decorating without the snapshot of "
                                                 "(%d, %d)"),
                                            *Name, *Step, Column.X, Column.Y, Column.X + NX, Column.Y + NY));
                    }
                    for (int32 Bucket = 0; Bucket < 9; ++Bucket)
                    {
                        if (!(Status.HeldBuckets >> Bucket & 1)) continue;
                        const FIntPoint Target = Column + FIntPoint(Bucket % 3 - 1, Bucket / 3 - 1);
                        if (Pipeline.GetStatus(Target).bGenerated)
                            Report.Fail(FString::Printf(
                                TEXT("%s, %s: (%d, %d) holds a bucket aimed at (%d, %d), which is final"), *Name, *Step,
                                Column.X, Column.Y, Target.X, Target.Y));
                    }
                }
            PrevFinalizing = MoveTemp(Finalizing);
            PrevDecorating = MoveTemp(Decorating);
        }
    };

    // A pipeline with its checker; Settle runs it to idle, taking meshes and checking after every Update.
    struct FStageRun
    {
        FStageRun(FStageReport& InReport, TUniquePtr<FVoxelColumnPipeline> InPipeline, const TCHAR* InName, int32 Limit)
            : Pipeline(MoveTemp(InPipeline)), Checker(InReport, InName, Limit)
        {
        }

        TUniquePtr<FVoxelColumnPipeline> Pipeline;
        FStageChecker Checker;

        void UpdateChecked(const FString& Step)
        {
            Pipeline->Update();
            Checker.Check(*Pipeline, Step);
        }

        void TakeMeshes()
        {
            FVoxelColumnPipeline::FReady Ready;
            while (Pipeline->PopReady(Ready))
            {
            }
        }

        bool Settle(const FString& Step)
        {
            for (int32 Round = 0; Round < StageRoundCap; ++Round)
            {
                UpdateChecked(Step);
                TakeMeshes();
                if (Pipeline->IsIdle()) return true;
                Pipeline->WaitForJobs();
            }
            Checker.Report.Fail(
                FString::Printf(TEXT("%s, %s: not idle after %d rounds"), *Checker.Name, *Step, StageRoundCap));
            return false;
        }

        bool MoveTo(FIntPoint Center, int32 Sector, const FString& Step)
        {
            Pipeline->SetFocus(StageRings(Center), Sector);
            return Settle(Step);
        }
    };

    // Runs a random pending job (manual jobs only); false when none was pending.
    bool StageRunRandomJob(FVoxelColumnPipeline& Pipeline, FRandomStream& Random)
    {
        const int32 Count = Pipeline.GetJobCount();
        if (Pipeline.GetPendingJobCount() == 0 || Count == 0) return false;
        const int32 First = Random.RandHelper(Count);
        for (int32 Offset = 0; Offset < Count; ++Offset)
            if (Pipeline.RunJob((First + Offset) % Count)) return true;
        return false;
    }

    // The final column at every position within StageOracleRadius of (0, 0), decorated alone, as block hashes.
    struct FStageOracle
    {
        FVoxelDecoratedArea Area;

        void Build(const FStageTestWorld& World)
        {
            FVoxelDecoratedArea::Build(
                *World.Decorator, *World.Terrain, FIntPoint::ZeroValue, StageOracleRadius, true, false, Area);
        }

        uint64 Hash(FIntPoint Column) const
        {
            const int32 Side = 2 * StageOracleRadius + 1;
            const FIntPoint Local = Column + FIntPoint(StageOracleRadius, StageOracleRadius);
            return VoxelBench::HashColumnBlocks(*Area.Columns[Local.Y * Side + Local.X]);
        }
    };

    // Compares every loaded final column within the oracle's radius; the first differences are listed.
    int32 StageCompareOracle(
        const FVoxelColumnPipeline& Pipeline, const FStageOracle& Oracle, int32& Compared, TArray<FIntPoint>& Differing)
    {
        int32 Differences = 0;
        for (int32 Y = -StageOracleRadius; Y <= StageOracleRadius; ++Y)
            for (int32 X = -StageOracleRadius; X <= StageOracleRadius; ++X)
            {
                const FVoxelColumn* Column = Pipeline.FindColumn(FIntPoint(X, Y));
                if (!Column) continue;
                ++Compared;
                if (VoxelBench::HashColumnBlocks(*Column) != Oracle.Hash(FIntPoint(X, Y)))
                {
                    ++Differences;
                    Differing.Add(FIntPoint(X, Y));
                }
            }
        return Differences;
    }

    FString StageListColumns(const TArray<FIntPoint>& Columns, int32 Most)
    {
        FString Out;
        for (int32 Index = 0; Index < FMath::Min(Most, Columns.Num()); ++Index)
            Out += FString::Printf(TEXT("(%d, %d) "), Columns[Index].X, Columns[Index].Y);
        return Out;
    }

    // Hashes of the loaded final columns within Extent of (0, 0).
    TMap<FIntPoint, uint64> StageHashColumns(const FVoxelColumnPipeline& Pipeline, int32 Extent)
    {
        TMap<FIntPoint, uint64> Out;
        for (int32 Y = -Extent; Y <= Extent; ++Y)
            for (int32 X = -Extent; X <= Extent; ++X)
                if (const FVoxelColumn* Column = Pipeline.FindColumn(FIntPoint(X, Y)))
                    Out.Add(FIntPoint(X, Y), VoxelBench::HashColumnBlocks(*Column));
        return Out;
    }

    // Columns whose hash differs from, or is missing in, Expected (or that Expected lacks).
    TArray<FIntPoint> StageDiffHashes(const TMap<FIntPoint, uint64>& Expected, const TMap<FIntPoint, uint64>& Actual)
    {
        TArray<FIntPoint> Out;
        for (const TPair<FIntPoint, uint64>& Pair : Expected)
        {
            const uint64* Found = Actual.Find(Pair.Key);
            if (!Found || *Found != Pair.Value) Out.Add(Pair.Key);
        }
        return Out;
    }
} // namespace

// Rings and the stages' order: counts at radius 12, then a random order of jobs and threaded jobs give the oracle's
// columns, with nothing recomputed when nothing moved.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPipelineStagesTest, "Unnamed.Voxel.Pipeline.Stages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPipelineStagesTest::RunTest(const FString& Parameters)
{
    if (!StageHasBlocks(*this)) return true;

    // 1. The rings' counts.
    {
        const FVoxelStreamingRings Rings(FIntPoint::ZeroValue, 12, 2, 3);
        const FVoxelStreamingRings Old(FIntPoint::ZeroValue, 12, 2, 1);
        int32 Count[4] = {}, OldCount[4] = {}, OldOther = 0;
        for (int32 Y = -16; Y <= 16; ++Y)
            for (int32 X = -16; X <= 16; ++X)
            {
                const int32 Ring = Rings.GetRing(FIntPoint(X, Y));
                if (Ring >= 0 && Ring <= 3) ++Count[Ring];
                const int32 OldRing = Old.GetRing(FIntPoint(X, Y));
                if (OldRing >= 0 && OldRing <= 1)
                    ++OldCount[OldRing];
                else if (OldRing != INDEX_NONE)
                    ++OldOther;
            }
        TestEqual(TEXT("Ring 0 columns at radius 12"), Count[0], 545);
        TestEqual(TEXT("Ring 1 columns at radius 12"), Count[1], 112);
        TestEqual(TEXT("Ring 2 columns at radius 12"), Count[2], 120);
        TestEqual(TEXT("Ring 3 columns at radius 12"), Count[3], 128);
        TestEqual(TEXT("Unload distance with 3 stages (margin raised to 3)"), Rings.GetUnloadDistance(), 18);
        TestEqual(TEXT("Ring 0 columns with 1 stage"), OldCount[0], 545);
        TestEqual(TEXT("Ring 1 columns with 1 stage"), OldCount[1], 112);
        TestEqual(TEXT("Other rings with 1 stage"), OldOther, 0);
        TestEqual(TEXT("Unload distance with 1 stage"), Old.GetUnloadDistance(), 15);
    }

    FStageTestWorld World;
    if (!StageMakeWorld(*this, World)) return false;
    FStageOracle Oracle;
    const double Start = FPlatformTime::Seconds();
    Oracle.Build(World);
    FStageReport Report(*this);

    // 2. Manual jobs in a random order.
    {
        FStageRun Run(Report, StageMakePipeline(World, 8, true), TEXT("manual"), 8);
        FVoxelColumnPipeline& Pipeline = *Run.Pipeline;
        Pipeline.SetFocus(StageRings(FIntPoint::ZeroValue), 0);
        FRandomStream Random(1234);
        bool bIdle = false;
        for (int32 Round = 0; Round < 100000 && !bIdle; ++Round)
        {
            StageRunRandomJob(Pipeline, Random);
            Run.UpdateChecked(TEXT("random order"));
            Run.TakeMeshes();
            bIdle = Pipeline.IsIdle();
        }
        TestTrue(TEXT("The random order reaches idle"), bIdle);

        const FVoxelStreamingRings& Rings = Pipeline.GetRings();
        int32 RingErrors = 0, WithoutSnapshot = 0, HoldingSnapshot = 0;
        for (int32 Y = -6; Y <= 6; ++Y)
            for (int32 X = -6; X <= 6; ++X)
            {
                const FIntPoint Column(X, Y);
                const int32 Ring = Rings.GetRing(Column);
                const FVoxelColumnStatus Status = Pipeline.GetStatus(Column);
                if ((Ring == 0 || Ring == 1) && !Status.bGenerated) ++RingErrors;
                if (Ring == 2 && !Status.bDecorated) ++RingErrors;
                if (Ring == 3 && !Status.bSnapshot && !Status.bGenerated) ++RingErrors;
                if (!Status.bGenerated) continue;
                bool bNeighboursDecorated = true;
                for (int32 NY = -1; NY <= 1; ++NY)
                    for (int32 NX = -1; NX <= 1; ++NX)
                        bNeighboursDecorated &= Pipeline.GetStatus(Column + FIntPoint(NX, NY)).bDecorated;
                if (bNeighboursDecorated && Status.bSnapshot) ++WithoutSnapshot;
                if (Status.bSnapshot) ++HoldingSnapshot;
            }
        TestEqual(TEXT("Columns missing the stage their ring wants"), RingErrors, 0);
        TestEqual(TEXT("Final columns with all neighbours decorated that still hold a snapshot"), WithoutSnapshot, 0);
        // At rest a final column's 8 neighbours are its sources, all decorated, so no final column keeps its snapshot:
        // the snapshots held are exactly those of the decorated and generated rings.
        const FVoxelPipelineStats RestStats = Pipeline.GetStats();
        TestEqual(TEXT("Final columns holding a snapshot at rest"), HoldingSnapshot, 0);
        TestEqual(TEXT("Snapshots held at rest (rings 2 and 3)"), RestStats.Snapshots,
            RestStats.NeededPerRing[2] + RestStats.NeededPerRing[3]);
        TestTrue(TEXT("The view is on screen"), Pipeline.IsViewOnScreen());
        int32 Compared = 0;
        TArray<FIntPoint> Differing;
        TestEqual(
            FString::Printf(TEXT("Final columns differing from the oracle (%s)"), *StageListColumns(Differing, 3)),
            StageCompareOracle(Pipeline, Oracle, Compared, Differing), 0);
        TestTrue(TEXT("Final columns were compared"), Compared > 0);
        const FVoxelPipelineStats Stats = Pipeline.GetStats();
        TestEqual(TEXT("Regenerated"), Stats.Regenerated, int64(0));
        TestEqual(TEXT("Redecorated"), Stats.Redecorated, int64(0));
        TestEqual(TEXT("BucketsDropped"), Stats.BucketsDropped, int64(0));
        AddInfo(FString::Printf(
            TEXT("Manual random order: %d final columns compared, %d hold a snapshot"), Compared, HoldingSnapshot));
    }

    // 3. Threaded jobs.
    {
        FStageRun Run(Report, StageMakePipeline(World, 16, false), TEXT("threaded"), 16);
        Run.MoveTo(FIntPoint::ZeroValue, 0, TEXT("threaded"));
        int32 Compared = 0;
        TArray<FIntPoint> Differing;
        TestEqual(FString::Printf(
                      TEXT("Threaded: final columns differing from the oracle (%s)"), *StageListColumns(Differing, 3)),
            StageCompareOracle(*Run.Pipeline, Oracle, Compared, Differing), 0);
        TestTrue(TEXT("Threaded: the view is on screen"), Run.Pipeline->IsViewOnScreen());
        AddInfo(FString::Printf(TEXT("Threaded: %d final columns compared"), Compared));
        // Hysteresis: the margin covers the rings' corners, so stepping back and forth across a diagonal border
        // unloads nothing, and the second step generates nothing.
        Run.MoveTo(FIntPoint(1, 1), 1, TEXT("diagonal step"));
        const int64 Generated = Run.Pipeline->GetStats().Generate.Count;
        Run.MoveTo(FIntPoint::ZeroValue, 0, TEXT("diagonal back"));
        Run.MoveTo(FIntPoint(1, 1), 1, TEXT("diagonal again"));
        TestEqual(TEXT("Columns generated stepping back and forth across a diagonal border"),
            Run.Pipeline->GetStats().Generate.Count - Generated, int64(0));
    }
    AddInfo(FString::Printf(TEXT("Stages test: %.1f s"), FPlatformTime::Seconds() - Start));
    return Report.Errors == 0;
}

// Evictions and focus moves at random while jobs run in a random order: whatever is lost is recomputed, so the final
// columns still equal the oracle's.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPipelineEvictionFuzzTest, "Unnamed.Voxel.Pipeline.EvictionFuzz",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPipelineEvictionFuzzTest::RunTest(const FString& Parameters)
{
    if (!StageHasBlocks(*this)) return true;
    FStageTestWorld World;
    if (!StageMakeWorld(*this, World)) return false;
    FStageOracle Oracle;
    const double Start = FPlatformTime::Seconds();
    Oracle.Build(World);
    FStageReport Report(*this);

    int64 TotalRegenerated = 0, TotalRedecorated = 0;
    int32 TotalCompared = 0;
    for (int32 Seed = 1; Seed <= 12; ++Seed)
    {
        const FString Name = FString::Printf(TEXT("seed %d"), Seed);
        FStageRun Run(Report, StageMakePipeline(World, 6, true), *Name, 6);
        FVoxelColumnPipeline& Pipeline = *Run.Pipeline;
        Pipeline.SetFocus(StageRings(FIntPoint::ZeroValue), 0);
        FRandomStream Random(Seed);
        for (int32 Step = 0; Step < 700; ++Step)
        {
            const int32 Roll = Random.RandHelper(100);
            if (Roll < 55)
                StageRunRandomJob(Pipeline, Random);
            else if (Roll < 75)
            {
                Run.UpdateChecked(TEXT("fuzz"));
                Run.TakeMeshes();
            }
            else if (Roll < 83)
                Pipeline.EvictSnapshot(FIntPoint(Random.RandRange(-6, 6), Random.RandRange(-6, 6)));
            else if (Roll < 91)
                Pipeline.EvictBuckets(FIntPoint(Random.RandRange(-6, 6), Random.RandRange(-6, 6)));
            else
            {
                const FIntPoint Center(Random.RandRange(-2, 2), Random.RandRange(-2, 2));
                Pipeline.SetFocus(StageRings(Center), Random.RandRange(0, 7));
            }
        }
        Run.MoveTo(FIntPoint::ZeroValue, 0, TEXT("settle"));
        int32 Compared = 0;
        TArray<FIntPoint> Differing;
        const int32 Differences = StageCompareOracle(Pipeline, Oracle, Compared, Differing);
        TestEqual(FString::Printf(TEXT("seed %d: final columns differing from the oracle (first: %s)"), Seed,
                      *StageListColumns(Differing, 3)),
            Differences, 0);
        TestTrue(FString::Printf(TEXT("seed %d: the view is on screen"), Seed), Pipeline.IsViewOnScreen());
        const FVoxelPipelineStats Stats = Pipeline.GetStats();
        TotalRegenerated += Stats.Regenerated;
        TotalRedecorated += Stats.Redecorated;
        TotalCompared += Compared;
    }
    TestTrue(TEXT("The fuzz regenerated snapshots"), TotalRegenerated > 0);
    TestTrue(TEXT("The fuzz decorated again"), TotalRedecorated > 0);
    AddInfo(FString::Printf(TEXT("Fuzz: %d columns compared over 12 seeds, Regenerated %lld, Redecorated %lld, %.1f s"),
        TotalCompared, TotalRegenerated, TotalRedecorated, FPlatformTime::Seconds() - Start));
    return Report.Errors == 0;
}

// A bucket aimed at a final column is never applied again: edits survive moving away and back, and DropEdits gives
// back the first visit's decoration.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPipelineReturnTripTest, "Unnamed.Voxel.Pipeline.ReturnTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPipelineReturnTripTest::RunTest(const FString& Parameters)
{
    if (!StageHasBlocks(*this)) return true;
    FStageTestWorld World;
    if (!StageMakeWorld(*this, World)) return false;
    const double Start = FPlatformTime::Seconds();
    FStageReport Report(*this);
    FStageRun Run(Report, StageMakePipeline(World, 16, false), TEXT("return trip"), 16);
    FVoxelColumnPipeline& Pipeline = *Run.Pipeline;
    const FIntPoint Home = FIntPoint::ZeroValue;
    constexpr int32 Extent = 4;

    // 1. First visit.
    if (!Run.MoveTo(Home, 0, TEXT("first visit"))) return false;
    const TMap<FIntPoint, uint64> First = StageHashColumns(Pipeline, Extent);
    TestTrue(TEXT("The first visit has final columns"), First.Num() > 0);

    // 2. Edits that applying a bucket again would undo.
    const FIntPoint F(3, 0), N(4, 0);
    struct FStageEdit
    {
        FIntVector Cell;
        uint16 State;
        int64 Stamp;
    };
    TArray<FStageEdit> Edits;
    {
        TArray<TUniquePtr<FVoxelColumn>> Snapshots;
        TArray<uint16> Dense;
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
            {
                const FIntPoint Position = N + FIntPoint(DX, DY);
                Snapshots.Add(MakeUnique<FVoxelColumn>(Position));
                World.Decorator->GenerateSnapshot(*World.Terrain, Position, *Snapshots.Last(), Dense);
            }
        TArray<const FVoxelColumn*> Views;
        for (const TUniquePtr<FVoxelColumn>& Snapshot : Snapshots)
            Views.Add(Snapshot.Get());
        TUniquePtr<FVoxelWriteLog> Log = MakeUnique<FVoxelWriteLog>();
        World.Decorator->Decorate(N, Views, *Log);
        // F is the snapshot at offset (-1, 0) from N.
        const FVoxelColumn& FSnapshot = *Snapshots[FVoxelWriteLog::GetBucket(-1, 0)];
        if (!TestNotNull(TEXT("F is final"), Pipeline.FindColumn(F))) return false;
        const FVoxelWriteBucket& Bucket = Log->Buckets[FVoxelWriteLog::GetBucket(-1, 0)];
        TSet<FIntVector> Seen;
        for (const FVoxelWrite& Write : Bucket.Writes)
        {
            if (Edits.Num() >= 24) break;
            const int32 X = Write.GetX(), Y = Write.GetY(), Z = Write.GetZ();
            const FIntVector Cell(F.X * 16 + X, F.Y * 16 + Y, Z);
            if (Seen.Contains(Cell)) continue;
            Seen.Add(Cell);
            const uint16 Snap = FSnapshot.GetState(X, Y, Z);
            // SetCell replaces F's column (copy on write) and frees the one before, so F is looked up for each write.
            if (Pipeline.FindColumn(F)->GetState(X, Y, Z) == Snap) continue;
            const int64 Stamp = 1000 + Edits.Num();
            if (Pipeline.SetCell(Cell, Snap, Stamp)) Edits.Add({Cell, Snap, Stamp});
        }
    }
    TestTrue(
        FString::Printf(TEXT("At least 4 cells of F differ from its snapshot (%d)"), Edits.Num()), Edits.Num() >= 4);
    {
        const FVoxelColumn* Home00 = Pipeline.FindColumn(Home);
        if (!TestNotNull(TEXT("(0, 0) is final"), Home00)) return false;
        const int32 Top = Home00->GetHeight(EVoxelHeightmap::WorldSurface, 8, 8);
        const uint16 Stone = World.Table->FindState(TEXT("stone"));
        const FIntVector Cell(8, 8, Top + 1);
        if (TestTrue(TEXT("The plain edit is set"), Pipeline.SetCell(Cell, Stone, 2000)))
            Edits.Add({Cell, Stone, 2000});
    }
    if (!Run.Settle(TEXT("edited"))) return false;
    const TMap<FIntPoint, uint64> Edited = StageHashColumns(Pipeline, Extent);
    auto CheckEdits = [&](const TCHAR* Step)
    {
        int32 Wrong = 0;
        for (const FStageEdit& Edit : Edits)
        {
            uint16 State = 0;
            int64 Stamp = 0;
            if (!Pipeline.GetCell(Edit.Cell, State, Stamp) || State != Edit.State || Stamp != Edit.Stamp) ++Wrong;
        }
        TestEqual(FString::Printf(TEXT("%s: edits that read back wrong"), Step), Wrong, 0);
    };
    auto CheckEdited = [&](const TCHAR* Step)
    {
        const TArray<FIntPoint> Differing = StageDiffHashes(Edited, StageHashColumns(Pipeline, Extent));
        TestEqual(FString::Printf(
                      TEXT("%s: columns differing from the edited state (%s)"), Step, *StageListColumns(Differing, 3)),
            Differing.Num(), 0);
        CheckEdits(Step);
    };
    CheckEdits(TEXT("edited"));
    TestTrue(TEXT("The edits changed some column"), StageDiffHashes(First, Edited).Num() > 0);

    // 3. Partial return: F stays loaded while N unloads.
    int32 K = 0;
    for (int32 Try = 1; Try <= 30 && K == 0; ++Try)
    {
        const FVoxelStreamingRings Away(FIntPoint(-Try, 0), 2, 2, 3);
        if (!Away.IsBeyondUnload(F) && Away.IsBeyondUnload(N)) K = Try;
    }
    if (!TestTrue(TEXT("A focus exists where F stays and N unloads"), K > 0)) return false;
    if (!Run.MoveTo(FIntPoint(-K, 0), 0, TEXT("partial away"))) return false;
    TestTrue(TEXT("F is still final while away"), Pipeline.GetStatus(F).bGenerated);
    TestFalse(TEXT("N is unloaded while away"), Pipeline.GetStatus(N).bPresent);
    const FVoxelPipelineStats Away = Pipeline.GetStats();
    if (!Run.MoveTo(Home, 0, TEXT("partial back"))) return false;
    const FVoxelPipelineStats Back = Pipeline.GetStats();
    CheckEdited(TEXT("partial return"));
    TestTrue(FString::Printf(TEXT("BucketsDropped rose (%lld to %lld)"), Away.BucketsDropped, Back.BucketsDropped),
        Back.BucketsDropped > Away.BucketsDropped);
    TestTrue(FString::Printf(TEXT("Regenerated rose (%lld to %lld)"), Away.Regenerated, Back.Regenerated),
        Back.Regenerated > Away.Regenerated);
    TestEqual(TEXT("BucketsAppliedToFinal"), Back.BucketsAppliedToFinal, int64(0));
    TestTrue(TEXT("The view is on screen after the partial return"), Pipeline.IsViewOnScreen());

    // 4. Full return.
    if (!Run.MoveTo(FIntPoint(30, 0), 0, TEXT("far away"))) return false;
    int32 Present = 0;
    for (int32 Y = -Extent; Y <= Extent; ++Y)
        for (int32 X = -Extent; X <= Extent; ++X)
            if (Pipeline.GetStatus(FIntPoint(X, Y)).bPresent) ++Present;
    TestEqual(TEXT("Columns near home still present far away"), Present, 0);
    if (!Run.MoveTo(Home, 0, TEXT("full return"))) return false;
    CheckEdited(TEXT("full return"));
    TestTrue(TEXT("The view is on screen after the full return"), Pipeline.IsViewOnScreen());

    // 5. Without the edits.
    Pipeline.DropEdits();
    if (!Run.Settle(TEXT("edits dropped"))) return false;
    const TArray<FIntPoint> Differing = StageDiffHashes(First, StageHashColumns(Pipeline, Extent));
    TestEqual(FString::Printf(
                  TEXT("Columns differing from the first visit after DropEdits (%s)"), *StageListColumns(Differing, 3)),
        Differing.Num(), 0);
    AddInfo(FString::Printf(TEXT("ReturnTrip: %d edits, k = %d, BucketsDropped %lld to %lld, Regenerated %lld to %lld, "
                                 "%.1f s"),
        Edits.Num(), K, Away.BucketsDropped, Back.BucketsDropped, Away.Regenerated, Back.Regenerated,
        FPlatformTime::Seconds() - Start));
    return Report.Errors == 0;
}

#endif
