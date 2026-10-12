#if WITH_DEV_AUTOMATION_TESTS
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelSeed.h"
#include "Storage/VoxelColumn.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "VoxelConstants.h"

namespace
{
    // Radius 2 with the smallest margin: 37 columns in view and 69 generated.
    constexpr int32 PipelineViewRadius = 2;
    constexpr int32 PipelineUnloadMargin = 2;
    constexpr int32 PipelineExpectedInView = 37;
    constexpr int32 PipelineExpectedNeeded = 69;
    // Every column in view or needed lies within this many columns of the center on both axes.
    constexpr int32 PipelineScanExtent = 6;
    // Rounds of Update, take and wait before a run gives up.
    constexpr int32 PipelineRoundCap = 4000;
    constexpr int32 PipelineSectionHeight = 16;
    constexpr int32 PipelineErrorCap = 12;

    using FPipelineSections = TArray<TSharedPtr<const FVoxelMeshData, ESPMode::ThreadSafe>>;

    // FNV-1a over raw bytes: the streams hashed are plain floats, integers and colors without padding.
    struct FPipelineHash
    {
        uint64 Value = 14695981039346656037ull;

        void AddBytes(const void* Data, SIZE_T Size)
        {
            const uint8* Bytes = static_cast<const uint8*>(Data);
            for (SIZE_T Index = 0; Index < Size; ++Index)
            {
                Value ^= Bytes[Index];
                Value *= 1099511628211ull;
            }
        }

        template <typename T> void AddValue(const T& Item) { AddBytes(&Item, sizeof(T)); }

        template <typename T> void AddArray(const TArray<T>& Items)
        {
            AddValue(int32(Items.Num()));
            if (Items.Num() > 0) AddBytes(Items.GetData(), static_cast<SIZE_T>(Items.Num()) * sizeof(T));
        }
    };

    // A section's whole mesh, layer by layer, in stream order. A section without geometry (null or empty) hashes as
    // a single zero byte.
    void AddPipelineMesh(FPipelineHash& Hash, const FVoxelMeshData* Mesh)
    {
        if (!Mesh || Mesh->IsEmpty())
        {
            Hash.AddValue(uint8(0));
            return;
        }
        Hash.AddValue(uint8(1));
        for (const FVoxelMeshLayer& Layer : Mesh->Layers)
        {
            Hash.AddArray(Layer.Positions);
            Hash.AddArray(Layer.BlockUVs);
            Hash.AddArray(Layer.Codes);
            Hash.AddArray(Layer.TintFaces);
            Hash.AddArray(Layer.Colors);
            Hash.AddArray(Layer.Indices);
        }
    }

    // Lists errors up to a cap, so one broken invariant does not drown the log.
    struct FPipelineReport
    {
        explicit FPipelineReport(FAutomationTestBase& InTest) : Test(InTest) {}

        FAutomationTestBase& Test;
        int32 Errors = 0;

        void Fail(const FString& Message)
        {
            if (Errors < PipelineErrorCap)
                Test.AddError(Message);
            else if (Errors == PipelineErrorCap)
                Test.AddError(TEXT("Further errors are not listed."));
            ++Errors;
        }
    };

    // What the screen would hold for a column: the sections of the latest mesh taken.
    struct FPipelineScreenColumn
    {
        FPipelineSections Sections;
        int32 Takes = 0;
    };

    // Drives a pipeline the way the world subsystem does, minus the world: Update, take every finished mesh, wait for
    // the jobs, until it is idle. After every Update it checks what holds while streaming: the job bound, and that a
    // column meshes only once its eight neighbours are generated.
    struct FPipelineDriver
    {
        FPipelineDriver(FPipelineReport& InReport, FVoxelColumnPipeline& InPipeline, const TCHAR* InName, int32 InLimit)
            : Report(InReport), Pipeline(InPipeline), Name(InName), Limit(InLimit)
        {
        }

        FPipelineReport& Report;
        FVoxelColumnPipeline& Pipeline;
        FString Name;
        int32 Limit;
        TMap<FIntPoint, FPipelineScreenColumn> Screen;
        // Every column taken, in order.
        TArray<FIntPoint> Taken;
        int32 MostJobs = 0;

        void CheckUpdate(const FString& Step)
        {
            const int32 Jobs = Pipeline.GetJobCount();
            MostJobs = FMath::Max(MostJobs, Jobs);
            if (Jobs > Limit)
                Report.Fail(FString::Printf(
                    TEXT("%s, %s: %d jobs in flight after Update, the limit is %d"), *Name, *Step, Jobs, Limit));
            const FVoxelStreamingRings& Rings = Pipeline.GetRings();
            for (int32 DY = -PipelineScanExtent; DY <= PipelineScanExtent; ++DY)
                for (int32 DX = -PipelineScanExtent; DX <= PipelineScanExtent; ++DX)
                {
                    const FIntPoint Column(Rings.Center.X + DX, Rings.Center.Y + DY);
                    if (!Rings.IsInView(Column) || !Pipeline.GetStatus(Column).bMeshing) continue;
                    for (int32 NY = -1; NY <= 1; ++NY)
                        for (int32 NX = -1; NX <= 1; ++NX)
                        {
                            const FIntPoint Neighbor = Column + FIntPoint(NX, NY);
                            if (!Pipeline.GetStatus(Neighbor).bGenerated)
                                Report.Fail(FString::Printf(
                                    TEXT("%s, %s: column (%d, %d) is meshing, but neighbour (%d, %d) is not generated"),
                                    *Name, *Step, Column.X, Column.Y, Neighbor.X, Neighbor.Y));
                        }
                }
        }

        // False after PipelineRoundCap rounds.
        bool RunUntilIdle(const FString& Step)
        {
            for (int32 Round = 0; Round < PipelineRoundCap; ++Round)
            {
                Pipeline.Update();
                CheckUpdate(Step);
                FVoxelColumnPipeline::FReady Ready;
                while (Pipeline.PopReady(Ready))
                {
                    FPipelineScreenColumn& Slot = Screen.FindOrAdd(Ready.Column);
                    Slot.Sections = Ready.Mesh->Sections;
                    ++Slot.Takes;
                    Taken.Add(Ready.Column);
                }
                if (Pipeline.IsIdle()) return true;
                Pipeline.WaitForJobs();
            }
            Report.Test.AddError(
                FString::Printf(TEXT("%s, %s: not idle after %d rounds (%d jobs in flight, %d meshes waiting)"), *Name,
                    *Step, PipelineRoundCap, Pipeline.GetJobCount(), Pipeline.GetStats().Ready));
            return false;
        }
    };

    // A column's blocks and the mesh the screen shows for it, as per-section hashes and their totals.
    struct FPipelineHashes
    {
        bool bBlocks = false;
        bool bMesh = false;
        uint64 Blocks = 0;
        uint64 Mesh = 0;
        TArray<uint64> BlockSections;
        TArray<uint64> MeshSections;
    };

    FPipelineHashes GetPipelineHashes(
        const FVoxelColumnPipeline& Pipeline, const FPipelineDriver& Driver, FIntPoint Column)
    {
        FPipelineHashes Hashes;
        if (const FVoxelColumn* Data = Pipeline.FindColumn(Column))
        {
            Hashes.bBlocks = true;
            FPipelineHash Total;
            for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
            {
                FPipelineHash Part;
                const int32 FirstZ = VoxelConstants::MinZ + Section * PipelineSectionHeight;
                for (int32 Z = FirstZ; Z < FirstZ + PipelineSectionHeight; ++Z)
                    for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
                        for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                            Part.AddValue(Data->GetState(X, Y, Z));
                Hashes.BlockSections.Add(Part.Value);
                Total.AddValue(Part.Value);
            }
            Hashes.Blocks = Total.Value;
        }
        if (const FPipelineScreenColumn* Shown = Driver.Screen.Find(Column))
        {
            Hashes.bMesh = true;
            FPipelineHash Total;
            for (const TSharedPtr<const FVoxelMeshData, ESPMode::ThreadSafe>& Section : Shown->Sections)
            {
                FPipelineHash Part;
                AddPipelineMesh(Part, Section.Get());
                Hashes.MeshSections.Add(Part.Value);
                Total.AddValue(Part.Value);
            }
            Hashes.Mesh = Total.Value;
        }
        return Hashes;
    }

    // The first index at which two per-section hash lists differ, or INDEX_NONE.
    int32 FindPipelineSectionDifference(const TArray<uint64>& Left, const TArray<uint64>& Right)
    {
        const int32 Count = FMath::Min(Left.Num(), Right.Num());
        for (int32 Index = 0; Index < Count; ++Index)
            if (Left[Index] != Right[Index]) return Index;
        return Left.Num() == Right.Num() ? INDEX_NONE : Count;
    }

    void ComparePipelineHashes(FPipelineReport& Report, const FString& Step, FIntPoint Column, const TCHAR* LeftName,
        const FPipelineHashes& Left, const TCHAR* RightName, const FPipelineHashes& Right)
    {
        if (!Left.bBlocks || !Right.bBlocks)
            Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) is not generated in %s"), *Step, Column.X, Column.Y,
                Left.bBlocks ? RightName : LeftName));
        else if (Left.Blocks != Right.Blocks)
        {
            const int32 Section = FindPipelineSectionDifference(Left.BlockSections, Right.BlockSections);
            const int32 FirstZ = VoxelConstants::MinZ + Section * PipelineSectionHeight;
            Report.Fail(FString::Printf(
                TEXT("%s: column (%d, %d) blocks differ between %s (hash %016llx) and %s (hash %016llx), first in "
                     "section %d (Z %d to %d)"),
                *Step, Column.X, Column.Y, LeftName, Left.Blocks, RightName, Right.Blocks, Section, FirstZ,
                FirstZ + PipelineSectionHeight - 1));
        }
        if (!Left.bMesh || !Right.bMesh)
            Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) has no mesh taken in %s"), *Step, Column.X, Column.Y,
                Left.bMesh ? RightName : LeftName));
        else if (Left.Mesh != Right.Mesh)
        {
            const int32 Section = FindPipelineSectionDifference(Left.MeshSections, Right.MeshSections);
            Report.Fail(FString::Printf(
                TEXT("%s: column (%d, %d) meshes differ between %s (hash %016llx) and %s (hash %016llx), first in "
                     "section %d"),
                *Step, Column.X, Column.Y, LeftName, Left.Mesh, RightName, Right.Mesh, Section));
        }
    }

    // The first cell at which two columns differ, named for the report.
    void ReportPipelineCellDifference(FPipelineReport& Report, const FString& Step, FIntPoint Column,
        const TCHAR* LeftName, const FVoxelColumn& Left, const TCHAR* RightName, const FVoxelColumn& Right)
    {
        for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
            for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
                for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                    if (Left.GetState(X, Y, Z) != Right.GetState(X, Y, Z))
                    {
                        Report.Fail(FString::Printf(
                            TEXT("%s: column (%d, %d) cell (%d, %d, %d) holds state %d in %s and state %d in %s"),
                            *Step, Column.X, Column.Y, Column.X * VoxelConstants::ColumnSize + X,
                            Column.Y * VoxelConstants::ColumnSize + Y, Z, int32(Left.GetState(X, Y, Z)), LeftName,
                            int32(Right.GetState(X, Y, Z)), RightName));
                        return;
                    }
    }

    FVoxelStreamingRings GetPipelineRings(FIntPoint Center)
    {
        return FVoxelStreamingRings(Center, PipelineViewRadius, PipelineUnloadMargin);
    }

    // The columns of the scan square around the rings' center that are needed, or only those in view.
    void GetPipelineColumns(const FVoxelStreamingRings& Rings, bool bNeeded, TArray<FIntPoint>& Out)
    {
        Out.Reset();
        for (int32 DY = -PipelineScanExtent; DY <= PipelineScanExtent; ++DY)
            for (int32 DX = -PipelineScanExtent; DX <= PipelineScanExtent; ++DX)
            {
                const FIntPoint Column(Rings.Center.X + DX, Rings.Center.Y + DY);
                if (bNeeded ? Rings.IsNeeded(Column) : Rings.IsInView(Column)) Out.Add(Column);
            }
    }

    bool IsPipelineWithin(FIntPoint Column, FIntPoint Center, int32 Extent)
    {
        return FMath::Abs(Column.X - Center.X) <= Extent && FMath::Abs(Column.Y - Center.Y) <= Extent;
    }

    // 6. Done means done: the view is on screen, no work is left, and the statistics agree with the rings.
    void CheckPipelineDone(
        FPipelineReport& Report, const FVoxelColumnPipeline& Pipeline, const FString& Name, const FString& Step)
    {
        const FVoxelStreamingRings& Rings = Pipeline.GetRings();
        TArray<FIntPoint> Needed, InView;
        GetPipelineColumns(Rings, true, Needed);
        GetPipelineColumns(Rings, false, InView);
        if (!Pipeline.IsViewOnScreen())
            Report.Fail(FString::Printf(TEXT("%s, %s: IsViewOnScreen is false at idle"), *Name, *Step));
        const FVoxelPipelineStats Stats = Pipeline.GetStats();
        if (Stats.InView != InView.Num() || Stats.InViewOnScreen != Stats.InView)
            Report.Fail(FString::Printf(
                TEXT("%s, %s: %d of %d columns in view are on screen at idle, the rings put %d in view"), *Name, *Step,
                Stats.InViewOnScreen, Stats.InView, InView.Num()));
        if (Stats.Needed != Needed.Num())
            Report.Fail(FString::Printf(TEXT("%s, %s: the statistics count %d needed columns, the rings need %d"),
                *Name, *Step, Stats.Needed, Needed.Num()));
        if (Stats.Generating != 0 || Stats.Meshing != 0 || Stats.Ready != 0 || Pipeline.GetJobCount() != 0)
            Report.Fail(FString::Printf(
                TEXT("%s, %s: work is left at idle: %d generating, %d meshing, %d meshes waiting, %d jobs"), *Name,
                *Step, Stats.Generating, Stats.Meshing, Stats.Ready, Pipeline.GetJobCount()));
        for (const FIntPoint Column : Needed)
            if (!Pipeline.GetStatus(Column).bGenerated)
                Report.Fail(FString::Printf(TEXT("%s, %s: column (%d, %d) is needed but not generated at idle"), *Name,
                    *Step, Column.X, Column.Y));
        for (const FIntPoint Column : InView)
        {
            const FVoxelColumnStatus Status = Pipeline.GetStatus(Column);
            if (!Status.bOnScreen || Status.DirtySections != 0 || Status.bReady || Status.bMeshing)
                Report.Fail(FString::Printf(
                    TEXT("%s, %s: column (%d, %d) in view is not done at idle: on screen %d, dirty sections 0x%x, "
                         "ready %d, meshing %d"),
                    *Name, *Step, Column.X, Column.Y, int32(Status.bOnScreen), Status.DirtySections,
                    int32(Status.bReady), int32(Status.bMeshing)));
        }
    }

    // The table and terrain every pipeline of a test shares. The table outlives the terrain built on it.
    struct FPipelineContext
    {
        FPipelineContext(
            const TSharedRef<const FVoxelBlockTable>& InTable, const TSharedRef<const IVoxelTerrain>& InTerrain)
            : Table(InTable), Terrain(InTerrain)
        {
        }

        TSharedRef<const FVoxelBlockTable> Table;
        TSharedRef<const IVoxelTerrain> Terrain;

        TUniquePtr<FVoxelColumnPipeline> MakePipeline(int32 MaxTasks) const
        {
            FVoxelColumnPipeline::FSettings Settings;
            Settings.MaxTasksInFlight = MaxTasks;
            return MakeUnique<FVoxelColumnPipeline>(Table, Terrain, Settings);
        }
    };

    // A pipeline with its driver.
    struct FPipelineRun
    {
        FPipelineRun(FPipelineReport& InReport, const FPipelineContext& Context, const TCHAR* InName, int32 InLimit)
            : Pipeline(Context.MakePipeline(InLimit)), Driver(InReport, *Pipeline, InName, InLimit), Name(InName)
        {
        }

        TUniquePtr<FVoxelColumnPipeline> Pipeline;
        FPipelineDriver Driver;
        FString Name;

        // Moves the focus, runs until idle and checks that the work is done.
        bool MoveTo(FIntPoint Center, int32 Sector, const FString& Step)
        {
            Pipeline->SetFocus(GetPipelineRings(Center), Sector);
            if (!Driver.RunUntilIdle(Step)) return false;
            CheckPipelineDone(Driver.Report, *Pipeline, Name, Step);
            return true;
        }
    };

    // The in-view columns of two runs at idle: equal blocks and meshes, or the first difference of each column.
    void ComparePipelineRuns(FPipelineReport& Report, const FString& Step, const FPipelineRun& Left,
        const FPipelineRun& Right, const TArray<FIntPoint>& Columns)
    {
        for (const FIntPoint Column : Columns)
        {
            const FPipelineHashes LeftHashes = GetPipelineHashes(*Left.Pipeline, Left.Driver, Column);
            const FPipelineHashes RightHashes = GetPipelineHashes(*Right.Pipeline, Right.Driver, Column);
            ComparePipelineHashes(Report, Step, Column, *Left.Name, LeftHashes, *Right.Name, RightHashes);
            if (LeftHashes.bBlocks && RightHashes.bBlocks && LeftHashes.Blocks != RightHashes.Blocks)
                ReportPipelineCellDifference(Report, Step, Column, *Left.Name, *Left.Pipeline->FindColumn(Column),
                    *Right.Name, *Right.Pipeline->FindColumn(Column));
        }
    }

    // 2. Unload and reload: moving far away unloads every column, TakeUnloaded lists those that had a mesh taken, and
    // coming back gives the blocks and meshes of the first visit.
    bool CheckPipelineReload(FPipelineReport& Report, FPipelineRun& Run, const TArray<FIntPoint>& InView)
    {
        const FIntPoint Home(0, 0), Away(40, 0);
        TArray<FIntPoint> Needed;
        GetPipelineColumns(Run.Pipeline->GetRings(), true, Needed);
        TMap<FIntPoint, FPipelineHashes> FirstVisit;
        for (const FIntPoint Column : InView)
            FirstVisit.Add(Column, GetPipelineHashes(*Run.Pipeline, Run.Driver, Column));
        TSet<FIntPoint> Meshed;
        for (const TPair<FIntPoint, FPipelineScreenColumn>& Pair : Run.Driver.Screen)
            Meshed.Add(Pair.Key);

        if (!Run.MoveTo(Away, 0, TEXT("move to (40, 0)"))) return false;
        TArray<FIntPoint> Unloaded;
        Run.Pipeline->TakeUnloaded(Unloaded);
        TSet<FIntPoint> UnloadedSet;
        for (const FIntPoint Column : Unloaded)
            UnloadedSet.Add(Column);
        if (UnloadedSet.Num() != Unloaded.Num())
            Report.Fail(FString::Printf(TEXT("unload: TakeUnloaded lists %d columns, only %d of them different"),
                Unloaded.Num(), UnloadedSet.Num()));
        for (const FIntPoint Column : Meshed)
            if (!UnloadedSet.Contains(Column))
                Report.Fail(FString::Printf(
                    TEXT("unload: column (%d, %d) had a mesh taken, but TakeUnloaded does not list it after the move "
                         "to (40, 0)"),
                    Column.X, Column.Y));
        for (const FIntPoint Column : UnloadedSet)
            if (!Meshed.Contains(Column))
                Report.Fail(
                    FString::Printf(TEXT("unload: TakeUnloaded lists column (%d, %d), whose mesh was never taken"),
                        Column.X, Column.Y));
        for (const FIntPoint Column : Needed)
        {
            if (Run.Pipeline->GetStatus(Column).bPresent || Run.Pipeline->FindColumn(Column))
                Report.Fail(FString::Printf(
                    TEXT("unload: column (%d, %d) is still present after the move to (40, 0)"), Column.X, Column.Y));
            // The screen forgets what it unloaded, so the second visit is compared with fresh meshes.
            Run.Driver.Screen.Remove(Column);
        }

        if (!Run.MoveTo(Home, 0, TEXT("move back to (0, 0)"))) return false;
        for (const FIntPoint Column : InView)
            ComparePipelineHashes(Report, TEXT("reload: second visit against the first"), Column, TEXT("first visit"),
                FirstVisit.FindChecked(Column), TEXT("second visit"),
                GetPipelineHashes(*Run.Pipeline, Run.Driver, Column));
        return true;
    }

    // 3. Stale results: jobs still running when their columns unload are dropped when they arrive.
    bool CheckPipelineStale(FPipelineReport& Report, const FPipelineContext& Context)
    {
        const FIntPoint Home(0, 0), Away(500, 0);
        FPipelineRun Run(Report, Context, TEXT("stale"), 8);
        Run.Pipeline->SetFocus(GetPipelineRings(Home), 0);
        Run.Pipeline->Update();
        const int32 InFlight = Run.Pipeline->GetJobCount();
        if (InFlight == 0)
            Report.Fail(TEXT("stale: no job is in flight after the first Update at (0, 0), so nothing can go stale"));
        Run.Pipeline->SetFocus(GetPipelineRings(Away), 0);
        Run.Pipeline->WaitForJobs();
        Run.Pipeline->Update();

        TArray<FIntPoint> Near;
        for (int32 DY = -PipelineScanExtent; DY <= PipelineScanExtent; ++DY)
            for (int32 DX = -PipelineScanExtent; DX <= PipelineScanExtent; ++DX)
                Near.Add(FIntPoint(Home.X + DX, Home.Y + DY));
        for (const FIntPoint Column : Near)
            if (Run.Pipeline->GetStatus(Column).bPresent || Run.Pipeline->FindColumn(Column))
                Report.Fail(FString::Printf(
                    TEXT("stale: column (%d, %d) near (0, 0) is present after the focus moved to (500, 0)"), Column.X,
                    Column.Y));
        FIntPoint ReadyColumn = Home;
        if (Run.Pipeline->PeekReady(&ReadyColumn) && IsPipelineWithin(ReadyColumn, Home, PipelineScanExtent))
            Report.Fail(FString::Printf(
                TEXT("stale: a mesh of column (%d, %d) near (0, 0) waits to be taken after the move to (500, 0)"),
                ReadyColumn.X, ReadyColumn.Y));
        const int64 Dropped = Run.Pipeline->GetStats().Dropped;
        if (Dropped <= 0)
            Report.Fail(FString::Printf(
                TEXT("stale: Dropped is %lld although %d jobs were in flight when their columns unloaded"), Dropped,
                InFlight));

        // Nothing from the first focus comes back while the new one streams.
        if (!Run.MoveTo(Away, 0, TEXT("stream (500, 0)"))) return false;
        for (const FIntPoint Column : Run.Driver.Taken)
            if (IsPipelineWithin(Column, Home, PipelineScanExtent))
                Report.Fail(FString::Printf(
                    TEXT("stale: a mesh was taken for column (%d, %d) near (0, 0) while streaming (500, 0)"), Column.X,
                    Column.Y));
        for (const FIntPoint Column : Near)
            if (Run.Pipeline->GetStatus(Column).bPresent)
                Report.Fail(
                    FString::Printf(TEXT("stale: column (%d, %d) near (0, 0) is present after streaming (500, 0)"),
                        Column.X, Column.Y));
        return true;
    }

    // Streaming without unloading or editing while jobs run has nothing to drop.
    void CheckPipelineNoDrops(FPipelineReport& Report, const FPipelineRun& Run)
    {
        const int64 Dropped = Run.Pipeline->GetStats().Dropped;
        if (Dropped != 0)
            Report.Fail(FString::Printf(
                TEXT("%s: %lld results were dropped although no column unloaded or changed while a job ran"), *Run.Name,
                Dropped));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelColumnPipelineTest, "Unnamed.Voxel.Streaming.Pipeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelColumnPipelineTest::RunTest(const FString& Parameters)
{
    if (!FFoundationBlockRegistry::GetDefault()->GetAssets())
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const auto Seed = FVoxelSeed::Parse(TEXT("Unnamed")).Value;
    const TSharedRef<const IVoxelTerrain> Terrain = MakeShared<FVoxelHeightTerrain>(Seed, *Table);
    const FPipelineContext Context(Table, Terrain);
    FPipelineReport Report(*this);
    const FIntPoint Home(0, 0);

    TArray<FIntPoint> InView, Needed;
    GetPipelineColumns(GetPipelineRings(Home), false, InView);
    GetPipelineColumns(GetPipelineRings(Home), true, Needed);
    TestEqual(TEXT("Columns in view at radius 2"), InView.Num(), PipelineExpectedInView);
    TestEqual(TEXT("Columns needed at radius 2"), Needed.Num(), PipelineExpectedNeeded);

    // 1. Order independence. Every run also checks 4 (the job limit), 5 (neighbours first) and 6 (done means done).
    FPipelineRun Direct(Report, Context, TEXT("direct"), 8);
    if (!Direct.MoveTo(Home, 0, TEXT("stream (0, 0)"))) return false;

    FPipelineRun Walked(Report, Context, TEXT("walked"), 1);
    for (int32 X = -6; X <= 0; ++X)
        if (!Walked.MoveTo(FIntPoint(X, 0), 3, FString::Printf(TEXT("walk to (%d, 0)"), X))) return false;
    ComparePipelineRuns(Report, TEXT("order: walked (1 task, sector 3) against direct"), Direct, Walked, InView);

    FPipelineRun Wide(Report, Context, TEXT("wide"), 16);
    if (!Wide.MoveTo(Home, 5, TEXT("stream (0, 0), sector 5"))) return false;
    ComparePipelineRuns(Report, TEXT("order: wide (16 tasks, sector 5) against direct"), Direct, Wide, InView);

    // 4. Bounded work at limits 1 (walked), 3, 8 (direct) and 16 (wide): every Update stayed within the limit, and
    // jobs were in flight, so the limit was exercised.
    FPipelineRun Narrow(Report, Context, TEXT("limit 3"), 3);
    if (!Narrow.MoveTo(Home, 0, TEXT("stream (0, 0)"))) return false;
    for (const FPipelineRun* Run : {&Direct, &Walked, &Wide, &Narrow})
    {
        TestTrue(*FString::Printf(TEXT("%s: jobs in flight stayed between 1 and the limit %d (most %d)"), *Run->Name,
                     Run->Driver.Limit, Run->Driver.MostJobs),
            Run->Driver.MostJobs >= 1 && Run->Driver.MostJobs <= Run->Driver.Limit);
        CheckPipelineNoDrops(Report, *Run);
    }

    // 2. Unload and reload, on the direct run.
    if (!CheckPipelineReload(Report, Direct, InView)) return false;

    // 3. Stale results.
    if (!CheckPipelineStale(Report, Context)) return false;
    return true;
}
#endif
