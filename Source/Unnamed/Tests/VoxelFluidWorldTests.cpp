#if WITH_DEV_AUTOMATION_TESTS
#include "Blocks/VoxelBlockTable.h"
#include "Clock/VoxelWorldClock.h"
#include "Dom/JsonObject.h"
#include "Fluids/VoxelFluidRules.h"
#include "Fluids/VoxelFluidScheduler.h"
#include "Fluids/VoxelFluidTestWorld.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/VoxelColumn.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Terrain/VoxelFlatTerrain.h"
#include "Voxel/VoxelBlockStorage.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelFluidSystem.h"
#include "VoxelConstants.h"

// The game's fluids (water and sky W2a) on streamed columns: FVoxelFluidSystem over a flat FVoxelColumnPipeline,
// checked against W1's oracle world (FVoxelFluidTestWorld) and against itself under budgets, frame rates and freezes.

namespace
{
    // Radius 2 with the smallest margin (VoxelEditTests' rings): 37 columns in view.
    constexpr int32 FluidWorldViewRadius = 2;
    constexpr int32 FluidWorldUnloadMargin = 2;
    // FVoxelFluidTestWorld's floor: stone from the world's bottom up to this Z, air above.
    constexpr int32 FluidWorldFloorTop = 63;
    // Pipeline rounds before a run gives up, ticks before a flow counts as never settling, and frames for the budget
    // run.
    constexpr int32 FluidWorldRoundCap = 4000;
    constexpr int32 FluidWorldTickCap = 2000;
    constexpr int32 FluidWorldFrameCap = 200000;
    // Differences listed per failed comparison.
    constexpr int32 FluidWorldErrorCap = 4;

    FVoxelStreamingRings GetFluidWorldRings(FIntPoint Center)
    {
        return FVoxelStreamingRings(Center, FluidWorldViewRadius, FluidWorldUnloadMargin);
    }

    FString GetFluidWorldCellText(const FIntVector& Cell)
    {
        return FString::Printf(TEXT("(%d, %d, %d)"), Cell.X, Cell.Y, Cell.Z);
    }

    FString DescribeFluidWorldCell(const FVoxelFluidCell& Cell)
    {
        if (Cell.bUnknown) return TEXT("unknown");
        if (Cell.Fluid == EVoxelFluid::Water) return FString::Printf(TEXT("water %d"), Cell.Level);
        if (Cell.Fluid == EVoxelFluid::Lava) return FString::Printf(TEXT("lava %d"), Cell.Level);
        if (Cell.bSolid) return FString::Printf(TEXT("solid (rules block %d)"), int32(Cell.Block));
        return TEXT("air");
    }

    // What the oracle pins: fluid, level and solidity, and the block where the expected cell names one (obsidian,
    // cobblestone). The oracle's floor names none where the game's is stone.
    bool IsSameFluidWorldCell(const FVoxelFluidCell& Got, const FVoxelFluidCell& Expected)
    {
        return Got.Fluid == Expected.Fluid && Got.Level == Expected.Level && Got.bSolid == Expected.bSolid &&
            Got.bUnknown == Expected.bUnknown &&
            (Expected.Block == EVoxelFluidBlock::None || Got.Block == Expected.Block);
    }

    // An inclusive box of cells, visited Z, then Y, then X.
    struct FFluidWorldBox
    {
        FIntVector Min = FIntVector::ZeroValue;
        FIntVector Max = FIntVector::ZeroValue;

        // Radius cells around Center on both axes, from the floor's top to two cells above Center.
        static FFluidWorldBox Around(const FIntVector& Center, int32 Radius)
        {
            return FFluidWorldBox{FIntVector(Center.X - Radius, Center.Y - Radius, FluidWorldFloorTop),
                FIntVector(Center.X + Radius, Center.Y + Radius, Center.Z + 2)};
        }

        template <typename TVisit> void ForEach(TVisit&& Visit) const
        {
            for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
                for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
                    for (int32 X = Min.X; X <= Max.X; ++X)
                        Visit(FIntVector(X, Y, Z));
        }
    };

    // A box's cells as a world read them (the rules' TWorld: Get), readable as a world itself.
    struct FFluidWorldSnapshot
    {
        FFluidWorldBox Box;
        TArray<FVoxelFluidCell> Cells;

        template <typename TWorld> static FFluidWorldSnapshot Take(const TWorld& World, const FFluidWorldBox& InBox)
        {
            FFluidWorldSnapshot Out;
            Out.Box = InBox;
            InBox.ForEach([&](const FIntVector& Cell) { Out.Cells.Add(World.Get(Cell)); });
            return Out;
        }

        FVoxelFluidCell Get(FIntVector Cell) const
        {
            const FIntVector Size = Box.Max - Box.Min + FIntVector(1);
            const FIntVector At = Cell - Box.Min;
            return Cells[(At.Z * Size.Y + At.Y) * Size.X + At.X];
        }

        int32 CountFluid() const
        {
            int32 Count = 0;
            for (const FVoxelFluidCell& Cell : Cells)
                Count += Cell.Fluid != EVoxelFluid::None;
            return Count;
        }
    };

    // Compares two worlds over a box; an error naming the first differences when they differ.
    template <typename TGot, typename TExpected>
    bool CheckFluidWorldEqual(FAutomationTestBase& Test, const FString& Step, const TGot& Got,
        const TExpected& Expected, const FFluidWorldBox& Box)
    {
        int32 Count = 0;
        FString First;
        Box.ForEach(
            [&](const FIntVector& Cell)
            {
                const FVoxelFluidCell Have = Got.Get(Cell);
                const FVoxelFluidCell Want = Expected.Get(Cell);
                if (IsSameFluidWorldCell(Have, Want)) return;
                if (Count++ < FluidWorldErrorCap)
                    First += FString::Printf(TEXT(" %s: %s, expected %s;"), *GetFluidWorldCellText(Cell),
                        *DescribeFluidWorldCell(Have), *DescribeFluidWorldCell(Want));
            });
        if (Count == 0) return true;
        Test.AddError(FString::Printf(TEXT("%s: %d cells differ:%s"), *Step, Count, *First));
        return false;
    }

    // Update, take every finished mesh, and run or wait for the jobs until the pipeline is idle (VoxelEditTests'
    // driver). Taken, when given, lists the columns whose meshes were taken, in order.
    bool RunFluidWorldPipeline(FAutomationTestBase& Test, FVoxelColumnPipeline& Pipeline, const TCHAR* Step,
        TArray<FIntPoint>* Taken = nullptr)
    {
        for (int32 Round = 0; Round < FluidWorldRoundCap; ++Round)
        {
            Pipeline.Update();
            FVoxelColumnPipeline::FReady Ready;
            while (Pipeline.PopReady(Ready))
                if (Taken) Taken->Add(Ready.Column);
            if (Pipeline.IsIdle()) return true;
            Pipeline.WaitForJobs();
        }
        Test.AddError(FString::Printf(TEXT("%s: the pipeline is not idle after %d rounds (%d jobs)"), Step,
            FluidWorldRoundCap, Pipeline.GetJobCount()));
        return false;
    }

    // The default block table and a flat terrain on it: stone up to FluidWorldFloorTop, as the oracle's floor.
    struct FFluidWorldContext
    {
        FFluidWorldContext()
            : Table(VoxelBlocks::GetDefault()),
              Stone(Table->FindState(TEXT("stone"))),
              Terrain(MakeTerrain(*Table, Stone))
        {
        }

        TSharedRef<const FVoxelBlockTable> Table;
        uint16 Stone = 0;
        TSharedRef<const IVoxelTerrain> Terrain;

        static TSharedRef<const IVoxelTerrain> MakeTerrain(const FVoxelBlockTable& InTable, uint16 InStone)
        {
            const FVoxelFlatLayer Layer{InStone, FluidWorldFloorTop - VoxelConstants::MinZ + 1};
            return MakeShared<FVoxelFlatTerrain>(MakeArrayView(&Layer, 1), InTable);
        }

        // No decorator. Manual jobs have no task limit, so a job left pending never holds the others back.
        TUniquePtr<FVoxelColumnPipeline> MakePipeline(bool bManualJobs) const
        {
            FVoxelColumnPipeline::FSettings Settings;
            Settings.MaxTasksInFlight = bManualJobs ? 1024 : 8;
            Settings.bManualJobs = bManualJobs;
            return MakeUnique<FVoxelColumnPipeline>(Table, Terrain, Settings);
        }

        // The first fluid state of a level, as FVoxelFluidSystem::Start picks it.
        uint16 FindFluid(EVoxelFluid Fluid, int32 Level) const
        {
            for (int32 State = 0; State < Table->GetStateCount(); ++State)
            {
                const FVoxelStateInfo& Info = Table->Get(uint16(State));
                if (Info.IsFluid() && Info.Fluid == Fluid && Info.FluidLevel == Level) return uint16(State);
            }
            return 0;
        }
    };

    // The game's wiring without a world: a pipeline, the authority's block world over its columns (wide bounds,
    // FVoxelBlockStorage) and the fluids on it, enabled with no budget on the whole ring once started. As in
    // AVoxelWorld, the authority pops what the fluid washes away and settles the plants after its writes (W2b).
    struct FFluidWorldRun
    {
        FFluidWorldRun(const FFluidWorldContext& InContext, bool bManualJobs)
            : Context(InContext), Pipeline(InContext.MakePipeline(bManualJobs))
        {
            World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            World.BoundsMin = FIntVector(-30000000, -30000000, VoxelConstants::MinZ);
            World.BoundsMax = FIntVector(30000000, 30000000, VoxelConstants::MaxZ);
            FVoxelColumnPipeline* Raw = Pipeline.Get();
            World.SetStorage(MakeUnique<FVoxelBlockStorage>([Raw]() { return Raw; }));
            Authority.PlantSurvival = VoxelBlocks::GetPlantSurvival(World.GetRegistry());
            Settings.bEnabled = true;
            Settings.BudgetMs = 0;
            Settings.SimRadius = -1;
        }

        const FFluidWorldContext& Context;
        TUniquePtr<FVoxelColumnPipeline> Pipeline;
        FFoundationAuthority Authority;
        FFoundationBlockWorld& World = Authority.World;
        FVoxelFluidSystem Fluids;
        FVoxelFluidSystem::FSettings Settings;

        bool Stream(FAutomationTestBase& Test, FIntPoint Center, const TCHAR* Step)
        {
            Pipeline->SetFocus(GetFluidWorldRings(Center), 0);
            return RunFluidWorldPipeline(Test, *Pipeline, Step);
        }

        // Starts the fluids at tick 0 and runs one enabled frame: the hook ignores edits until then.
        bool Start(FAutomationTestBase& Test)
        {
            FVoxelColumnPipeline* Raw = Pipeline.Get();
            const bool bStarted = Fluids.Start(World, Context.Table, [Raw]() { return Raw; }, FVoxelFluidOptions(), 0);
            if (!Test.TestTrue(TEXT("the fluids start on the default table"), bStarted)) return false;
            Fluids.OnFlushed = [this](const FIntVector& Cell, FFoundationBlockState Before, bool bWashedAway)
            { Authority.SettleAfterFluid(Cell, Before, bWashedAway); };
            Fluids.Advance(0, Settings);
            return true;
        }

        // A player on the project's rules whose inventory takes nothing at the start: a drop would land there.
        int32 AddPlayer()
        {
            UFoundationInteractionSettings::Get().BuildRules(Authority.Rules);
            Authority.Options.bInfiniteSupply = false;
            Authority.Options.FiniteBlockCount = 0;
            return Authority.AddPlayer();
        }

        // An edit, as the player makes one: the hook wakes the fluid around it.
        void Place(const FIntVector& Cell, const FVoxelFluidCell& Value)
        {
            World.Set(Cell, FFoundationBlockState{Fluids.GetState(Value)});
        }

        void Step() { Fluids.Advance(1, Settings); }

        bool IsSettled() const
        {
            const FVoxelFluidSystem::FStats Stats = Fluids.GetStats();
            return Stats.Queued == 0 && Stats.Parked == 0 && Stats.Behind == 0;
        }
    };

    // Steps a run and its oracle together, comparing the box after each tick.
    bool StepFluidWorldWithOracle(FAutomationTestBase& Test, const TCHAR* Name, FFluidWorldRun& Run,
        FVoxelFluidTestWorld& Oracle, const FFluidWorldBox& Box, int32 Ticks)
    {
        for (int32 Index = 0; Index < Ticks; ++Index)
        {
            Run.Step();
            Oracle.Step();
            const FString Step = FString::Printf(
                TEXT("%s, tick %llu (oracle %llu)"), Name, Run.Fluids.GetStats().Tick, Oracle.GetTick());
            if (!CheckFluidWorldEqual(Test, Step, Run.Fluids, Oracle, Box)) return false;
        }
        return true;
    }

    // The same until both have nothing pending.
    bool SettleFluidWorldWithOracle(FAutomationTestBase& Test, const TCHAR* Name, FFluidWorldRun& Run,
        FVoxelFluidTestWorld& Oracle, const FFluidWorldBox& Box)
    {
        for (int32 Index = 0; Index < FluidWorldTickCap; ++Index)
        {
            if (Run.IsSettled() && Oracle.GetScheduler().Num() == 0) return true;
            if (!StepFluidWorldWithOracle(Test, Name, Run, Oracle, Box, 1)) return false;
        }
        Test.AddError(FString::Printf(TEXT("%s: not settled after %d ticks"), Name, FluidWorldTickCap));
        return false;
    }

    bool SettleFluidWorld(FFluidWorldRun& Run)
    {
        for (int32 Index = 0; Index < FluidWorldTickCap && !Run.IsSettled(); ++Index)
            Run.Step();
        return Run.IsSettled();
    }

    bool SettleFluidWorldOracle(FVoxelFluidTestWorld& Oracle)
    {
        for (int32 Index = 0; Index < FluidWorldTickCap && Oracle.GetScheduler().Num() != 0; ++Index)
            Oracle.Step();
        return Oracle.GetScheduler().Num() == 0;
    }

    bool HasFluidWorldAssets(FAutomationTestBase& Test)
    {
        if (FFoundationBlockRegistry::GetDefault()->GetAssets()) return true;
        Test.AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }

    const FVoxelFluidCell FluidWorldWater = FVoxelFluidCell::MakeFluid(EVoxelFluid::Water, 0);
    const FVoxelFluidCell FluidWorldLava = FVoxelFluidCell::MakeFluid(EVoxelFluid::Lava, 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldMatchesTest, "Unnamed.Voxel.Fluid.World.Matches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldMatchesTest::RunTest(const FString& Parameters)
{
    // The game's fluids on streamed columns equal W1's oracle at every tick: water from a column corner (its flow
    // crosses four columns), lava, and lava placed beside water (obsidian at once, before any tick).
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    const FIntVector Corner(16, 16, 64);
    struct FCase
    {
        const TCHAR* Name;
        FVoxelFluidCell First;
        bool bLavaBeside;
        int32 Ticks;
    };
    const FCase Cases[] = {{TEXT("water"), FluidWorldWater, false, 60}, {TEXT("lava"), FluidWorldLava, false, 200},
        {TEXT("lava beside water"), FluidWorldWater, true, 60}};
    int32 Passed = 0;
    FString Counts;
    for (const FCase& Case : Cases)
    {
        FFluidWorldRun Run(Context, false);
        if (!Run.Stream(*this, FIntPoint(0, 0), Case.Name) || !Run.Start(*this)) return false;
        FVoxelFluidTestWorld Oracle(FluidWorldFloorTop);
        Run.Place(Corner, Case.First);
        Oracle.Place(Corner, Case.First);
        if (Case.bLavaBeside)
        {
            const FIntVector Beside = Corner + FIntVector(1, 0, 0);
            Run.Place(Beside, FluidWorldLava);
            Oracle.Place(Beside, FluidWorldLava);
            TestTrue(TEXT("lava placed beside water is obsidian before any tick"),
                Run.Fluids.Get(Beside).Block == EVoxelFluidBlock::Obsidian && Run.Fluids.GetStats().Tick == 0);
        }
        const FFluidWorldBox Box = FFluidWorldBox::Around(Corner, 12);
        if (!CheckFluidWorldEqual(*this, FString::Printf(TEXT("%s, placed"), Case.Name), Run.Fluids, Oracle, Box))
            continue;
        if (!StepFluidWorldWithOracle(*this, Case.Name, Run, Oracle, Box, Case.Ticks)) continue;
        ++Passed;
        Counts += FString::Printf(TEXT(" %s %d;"), Case.Name, FFluidWorldSnapshot::Take(Oracle, Box).CountFluid());
    }
    AddInfo(FString::Printf(TEXT("World.Matches: %d of 3 placements equal the oracle at every tick (60 water, 200 lava "
                                 "ticks); fluid cells at the end:%s"),
        Passed, *Counts));
    return Passed == int32(UE_ARRAY_COUNT(Cases));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldBorderTest, "Unnamed.Voxel.Fluid.World.Border",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldBorderTest::RunTest(const FString& Parameters)
{
    // An act that reads a column not generated yet waits, parked on that column, and writes nothing; once the column
    // is final the act runs, and the settled world equals the oracle's.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    FFluidWorldRun Run(Context, true);
    FVoxelColumnPipeline& Pipeline = *Run.Pipeline;
    const FIntPoint A(0, 0), B(1, 0);
    Pipeline.SetFocus(GetFluidWorldRings(A), 0);
    // Every job but B's, until no other is left to run.
    for (int32 Round = 0; Round < FluidWorldRoundCap; ++Round)
    {
        Pipeline.Update();
        FVoxelColumnPipeline::FReady Ready;
        while (Pipeline.PopReady(Ready))
        {
        }
        bool bRan = false;
        for (int32 Index = 0; Index < Pipeline.GetJobCount(); ++Index)
            if (Pipeline.IsJobPending(Index) && Pipeline.GetJobColumn(Index) != B) bRan |= Pipeline.RunJob(Index);
        if (!bRan) break;
    }
    bool bReady = true;
    for (int32 DY = -1; DY <= 1; ++DY)
        for (int32 DX = -1; DX <= 1; ++DX)
        {
            const FIntPoint Column = A + FIntPoint(DX, DY);
            if (Column != B)
                bReady &= TestTrue(
                    FString::Printf(TEXT("(%d, %d) is generated"), Column.X, Column.Y), Pipeline.IsGenerated(Column));
        }
    bReady &= TestFalse(TEXT("B is not generated"), Pipeline.IsGenerated(B));
    if (!bReady || !Run.Start(*this)) return false;

    // Water one cell from the A/B border: its first act (tick 5) reads B looking for a drop.
    FVoxelFluidTestWorld Oracle(FluidWorldFloorTop);
    const FIntVector Source(14, 8, 64);
    Run.Place(Source, FluidWorldWater);
    Oracle.Place(Source, FluidWorldWater);
    constexpr int32 WaitTicks = 20;
    for (int32 Tick = 0; Tick < WaitTicks; ++Tick)
        Run.Step();
    const FVoxelFluidSystem::FStats Waiting = Run.Fluids.GetStats();
    TestTrue(FString::Printf(TEXT("the act reading B waited (%lld waits)"), Waiting.Waits), Waiting.Waits > 0);
    TestTrue(FString::Printf(TEXT("and is parked (%d parked)"), Waiting.Parked), Waiting.Parked > 0);
    TestTrue(FString::Printf(TEXT("no act ran (%lld acts)"), Waiting.Acts), Waiting.Acts == 0);
    TestTrue(FString::Printf(TEXT("no write was lost (%lld lost)"), Waiting.Lost), Waiting.Lost == 0);
    TestFalse(TEXT("B is still not generated"), Pipeline.IsGenerated(B));
    TestTrue(TEXT("the source is the only fluid around it"),
        FFluidWorldSnapshot::Take(Run.Fluids, FFluidWorldBox::Around(Source, 8)).CountFluid() == 1);

    // B's jobs: it is generated with no fluid in it (nothing was written there).
    if (!RunFluidWorldPipeline(*this, Pipeline, TEXT("generate B"))) return false;
    if (!TestTrue(TEXT("B is generated"), Pipeline.IsGenerated(B))) return false;
    const FFluidWorldBox BoxB{FIntVector(16, 0, 64), FIntVector(31, 15, 66)};
    TestTrue(
        TEXT("B holds no fluid before the act runs"), FFluidWorldSnapshot::Take(Run.Fluids, BoxB).CountFluid() == 0);

    // Settled, the world equals the oracle's settled world (the flow ran later, not differently).
    if (!TestTrue(TEXT("the game's flow settles"), SettleFluidWorld(Run))) return false;
    if (!TestTrue(TEXT("the oracle's flow settles"), SettleFluidWorldOracle(Oracle))) return false;
    const FFluidWorldBox Box = FFluidWorldBox::Around(Source, 12);
    const bool bEqual = CheckFluidWorldEqual(*this, TEXT("settled"), Run.Fluids, Oracle, Box);
    const FVoxelFluidSystem::FStats Settled = Run.Fluids.GetStats();
    AddInfo(FString::Printf(TEXT("World.Border: the source's act waited on column (1, 0) (%lld waits, %d parked) for "
                                 "%d ticks; settled at tick %llu %s the oracle's settled world (tick %llu, %d fluid "
                                 "cells)"),
        Waiting.Waits, Waiting.Parked, WaitTicks, Settled.Tick, bEqual ? TEXT("equal to") : TEXT("unlike"),
        Oracle.GetTick(), FFluidWorldSnapshot::Take(Oracle, Box).CountFluid()));
    return bEqual;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldReturnTripTest, "Unnamed.Voxel.Fluid.World.ReturnTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldReturnTripTest::RunTest(const FString& Parameters)
{
    // A flow whose columns unload sleeps with the delays it had left: nothing acts while away, its cells survive as
    // edits, and back home it goes on exactly as if no time had passed.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    FFluidWorldRun Run(Context, false);
    if (!Run.Stream(*this, FIntPoint(0, 0), TEXT("stream (0, 0)")) || !Run.Start(*this)) return false;
    FVoxelFluidTestWorld Oracle(FluidWorldFloorTop);
    const FIntVector Source(0, 0, 64);
    const FFluidWorldBox Box = FFluidWorldBox::Around(Source, 12);
    Run.Place(Source, FluidWorldWater);
    Oracle.Place(Source, FluidWorldWater);
    constexpr int32 LeaveTicks = 12;
    if (!StepFluidWorldWithOracle(*this, TEXT("before leaving"), Run, Oracle, Box, LeaveTicks)) return false;
    const FFluidWorldSnapshot Left = FFluidWorldSnapshot::Take(Run.Fluids, Box);
    const FVoxelFluidSystem::FStats Leaving = Run.Fluids.GetStats();
    TestTrue(FString::Printf(TEXT("the flow was still moving when it left (%d acts queued)"), Leaving.Queued),
        Leaving.Queued > 0);

    // Away: the flow's columns unload and its acts sleep.
    if (!Run.Stream(*this, FIntPoint(40, 0), TEXT("stream (40, 0)"))) return false;
    TestFalse(TEXT("the flow's column is unloaded"), Run.Pipeline->IsGenerated(FIntPoint(0, 0)));
    constexpr int32 AwayTicks = 50;
    for (int32 Tick = 0; Tick < AwayTicks; ++Tick)
        Run.Step();
    const FVoxelFluidSystem::FStats Away = Run.Fluids.GetStats();
    TestTrue(FString::Printf(TEXT("the acts sleep (%lld sleeps, %d parked)"), Away.Sleeps, Away.Parked),
        Away.Sleeps > 0 && Away.Parked > 0);
    TestTrue(FString::Printf(TEXT("nothing acts away (%lld acts, then %lld)"), Leaving.Acts, Away.Acts),
        Away.Acts == Leaving.Acts && Away.Queued == 0);
    TestTrue(FString::Printf(TEXT("the tick runs on to %d (%llu)"), LeaveTicks + AwayTicks, Away.Tick),
        Away.Tick == uint64(LeaveTicks + AwayTicks));

    // Back: the cells read as they were left, before the fluids run again.
    if (!Run.Stream(*this, FIntPoint(0, 0), TEXT("stream back"))) return false;
    if (!CheckFluidWorldEqual(*this, TEXT("cells after the return trip"), Run.Fluids, Left, Box)) return false;
    Run.Fluids.Advance(0, Run.Settings);
    const FVoxelFluidSystem::FStats Back = Run.Fluids.GetStats();
    TestTrue(FString::Printf(TEXT("every act is queued again (%d queued, %d parked; %d were queued)"), Back.Queued,
                 Back.Parked, Leaving.Queued),
        Back.Parked == 0 && Back.Queued == Leaving.Queued);

    // Tick 62 + k equals the oracle's 12 + k.
    constexpr int32 AfterTicks = 40;
    const bool bEqual = StepFluidWorldWithOracle(*this, TEXT("after the return"), Run, Oracle, Box, AfterTicks);
    AddInfo(FString::Printf(TEXT("World.ReturnTrip: left at tick %llu with %d acts queued; %d ticks away (%lld "
                                 "sleeps, %d parked, 0 acts); cells read back; ticks %llu+k %s the oracle's %d+k for "
                                 "k <= %d"),
        Leaving.Tick, Leaving.Queued, AwayTicks, Away.Sleeps, Away.Parked, Back.Tick,
        bEqual ? TEXT("equal") : TEXT("differ from"), LeaveTicks, AfterTicks));
    return bEqual;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldSetCellsTest, "Unnamed.Voxel.Fluid.World.SetCells",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldSetCellsTest::RunTest(const FString& Parameters)
{
    // SetCells for a column's cells at once leaves the same cells, stamps, heightmaps and dirty sections as SetCell for
    // each in turn.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    const TUniquePtr<FVoxelColumnPipeline> One = Context.MakePipeline(false);
    const TUniquePtr<FVoxelColumnPipeline> Many = Context.MakePipeline(false);
    for (FVoxelColumnPipeline* Pipeline : {One.Get(), Many.Get()})
    {
        Pipeline->SetFocus(GetFluidWorldRings(FIntPoint(0, 0)), 0);
        if (!RunFluidWorldPipeline(*this, *Pipeline, TEXT("stream (0, 0)"))) return false;
    }
    const uint16 Stone = Context.Stone;
    const uint16 Water = Context.FindFluid(EVoxelFluid::Water, 0);
    if (!TestTrue(TEXT("stone and a water source exist"), Stone != 0 && Water != 0)) return false;
    const int32 SurfaceBefore = One->FindColumn(FIntPoint(0, 0))->GetHeight(EVoxelHeightmap::WorldSurface, 8, 8);

    using FWrite = FVoxelColumnPipeline::FCellWrite;
    const FWrite Writes[] = {
        // Column (0, 0): a section's bottom cell, the west border, the corner and a section's top cell (lowering the
        // surface), the top height raised, fluid on it, a cell carved under it, the world's bottom, air over air.
        {FIntVector(5, 6, 64), Stone, 101},
        {FIntVector(0, 9, 70), Stone, 102},
        {FIntVector(15, 15, 63), 0, 103},
        {FIntVector(8, 8, 100), Stone, 104},
        {FIntVector(8, 8, 101), Water, 105},
        {FIntVector(8, 8, 63), 0, 106},
        {FIntVector(3, 12, VoxelConstants::MinZ), 0, 107},
        {FIntVector(4, 4, 90), 0, 108},
        // Column (-1, 0): its east border at a section's top, its west border with water.
        {FIntVector(-1, 4, 79), Stone, 109},
        {FIntVector(-16, 4, 64), Water, 110},
    };
    TArray<FIntPoint> Columns;
    TMap<FIntPoint, TArray<FWrite>> Grouped;
    for (const FWrite& Write : Writes)
    {
        TestTrue(FString::Printf(TEXT("SetCell %s"), *GetFluidWorldCellText(Write.Cell)),
            One->SetCell(Write.Cell, Write.State, Write.Stamp));
        const FIntPoint Column = FVoxelFluidScheduler::GetColumn(Write.Cell);
        Columns.AddUnique(Column);
        Grouped.FindOrAdd(Column).Add(Write);
    }
    for (const FIntPoint Column : Columns)
        TestTrue(FString::Printf(TEXT("SetCells (%d, %d)"), Column.X, Column.Y),
            Many->SetCells(Column, Grouped.FindChecked(Column)));

    int32 Errors = 0;
    const auto Fail = [&](const FString& What)
    {
        if (Errors++ < FluidWorldErrorCap) AddError(What);
    };
    // Every written cell and its 26 neighbours: state and stamp.
    TSet<FIntVector> Checked;
    for (const FWrite& Write : Writes)
    {
        uint16 State = 0;
        int64 Stamp = 0;
        if (!One->GetCell(Write.Cell, State, Stamp) || State != Write.State || Stamp != Write.Stamp)
            Fail(FString::Printf(TEXT("SetCell's %s holds %d stamped %lld, not %d stamped %lld"),
                *GetFluidWorldCellText(Write.Cell), State, Stamp, Write.State, Write.Stamp));
        for (int32 DZ = -1; DZ <= 1; ++DZ)
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                {
                    const FIntVector Cell = Write.Cell + FIntVector(DX, DY, DZ);
                    bool bAlready = false;
                    Checked.Add(Cell, &bAlready);
                    if (bAlready) continue;
                    uint16 StateOne = 0, StateMany = 0;
                    int64 StampOne = 0, StampMany = 0;
                    const bool bOne = One->GetCell(Cell, StateOne, StampOne);
                    const bool bMany = Many->GetCell(Cell, StateMany, StampMany);
                    if (bOne != bMany || StateOne != StateMany || StampOne != StampMany)
                        Fail(FString::Printf(TEXT("%s: SetCell %d stamped %lld, SetCells %d stamped %lld"),
                            *GetFluidWorldCellText(Cell), StateOne, StampOne, StateMany, StampMany));
                }
    }
    // Every heightmap value of the touched columns, and the dirty sections around them.
    int32 Heights = 0;
    TSet<FIntPoint> Around;
    for (const FIntPoint Column : Columns)
    {
        const FVoxelColumn* DataOne = One->FindColumn(Column);
        const FVoxelColumn* DataMany = Many->FindColumn(Column);
        if (!DataOne || !DataMany)
        {
            Fail(FString::Printf(TEXT("(%d, %d) is not loaded"), Column.X, Column.Y));
            continue;
        }
        for (int32 Map = 0; Map < int32(EVoxelHeightmap::Count); ++Map)
            for (int32 Y = 0; Y < 16; ++Y)
                for (int32 X = 0; X < 16; ++X)
                {
                    const int32 HeightOne = DataOne->GetHeight(EVoxelHeightmap(Map), X, Y);
                    const int32 HeightMany = DataMany->GetHeight(EVoxelHeightmap(Map), X, Y);
                    ++Heights;
                    if (HeightOne != HeightMany)
                        Fail(FString::Printf(TEXT("(%d, %d) map %d at (%d, %d): SetCell %d, SetCells %d"), Column.X,
                            Column.Y, Map, X, Y, HeightOne, HeightMany));
                }
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
                Around.Add(Column + FIntPoint(DX, DY));
    }
    int32 DirtyColumns = 0;
    for (const FIntPoint Column : Around)
    {
        const uint32 DirtyOne = One->GetStatus(Column).DirtySections;
        const uint32 DirtyMany = Many->GetStatus(Column).DirtySections;
        DirtyColumns += DirtyOne != 0;
        if (DirtyOne != DirtyMany)
            Fail(FString::Printf(
                TEXT("(%d, %d) dirty sections: SetCell 0x%x, SetCells 0x%x"), Column.X, Column.Y, DirtyOne, DirtyMany));
    }
    const int32 SurfaceAfter = One->FindColumn(FIntPoint(0, 0))->GetHeight(EVoxelHeightmap::WorldSurface, 8, 8);
    TestTrue(FString::Printf(TEXT("the top height changed (%d to %d)"), SurfaceBefore, SurfaceAfter),
        SurfaceAfter != SurfaceBefore);
    TestTrue(TEXT("the edits marked sections dirty"), DirtyColumns > 0);
    AddInfo(FString::Printf(TEXT("World.SetCells: %d writes in %d columns: %d cells, %d heightmap values and %d dirty "
                                 "masks (%d dirty) %s"),
        int32(UE_ARRAY_COUNT(Writes)), Columns.Num(), Checked.Num(), Heights, Around.Num(), DirtyColumns,
        Errors == 0 ? TEXT("equal") : TEXT("differ")));
    return Errors == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldBudgetTest, "Unnamed.Voxel.Fluid.World.Budget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldBudgetTest::RunTest(const FString& Parameters)
{
    // A frame budget makes the fluids fall behind and catch up later, never differently: caught up and settled, the
    // world equals the unlimited run's, after the same acts.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    FFluidWorldRun Fast(Context, false);
    FFluidWorldRun Slow(Context, false);
    Slow.Settings.BudgetMs = 0.0001;
    for (FFluidWorldRun* Run : {&Fast, &Slow})
    {
        if (!Run->Stream(*this, FIntPoint(0, 0), TEXT("stream (0, 0)")) || !Run->Start(*this)) return false;
        // A flood: a 3 x 3 grid of sources 6 apart.
        for (int32 J = -1; J <= 1; ++J)
            for (int32 I = -1; I <= 1; ++I)
                Run->Place(FIntVector(8 + 6 * I, 8 + 6 * J, 64), FluidWorldWater);
    }
    if (!TestTrue(TEXT("the unlimited run settles"), SettleFluidWorld(Fast))) return false;
    uint64 MostBehind = 0;
    int32 Frames = 0;
    for (; Frames < FluidWorldFrameCap && !Slow.IsSettled(); ++Frames)
    {
        Slow.Step();
        MostBehind = FMath::Max(MostBehind, Slow.Fluids.GetStats().Behind);
    }
    if (!TestTrue(FString::Printf(TEXT("the budget run catches up and settles in %d frames"), FluidWorldFrameCap),
            Slow.IsSettled()))
        return false;
    const FVoxelFluidSystem::FStats FastStats = Fast.Fluids.GetStats();
    const FVoxelFluidSystem::FStats SlowStats = Slow.Fluids.GetStats();
    TestTrue(FString::Printf(TEXT("the budget run fell behind (%llu ticks at most)"), MostBehind), MostBehind > 0);
    TestTrue(FString::Printf(TEXT("frames went over the budget (%lld)"), SlowStats.OverBudgetFrames),
        SlowStats.OverBudgetFrames > 0);
    TestTrue(FString::Printf(TEXT("the same acts (%lld unlimited, %lld budgeted)"), FastStats.Acts, SlowStats.Acts),
        FastStats.Acts == SlowStats.Acts);
    const FFluidWorldBox Box{FIntVector(-8, -8, FluidWorldFloorTop), FIntVector(24, 24, 66)};
    const bool bEqual = CheckFluidWorldEqual(*this, TEXT("budgeted against unlimited"), Slow.Fluids, Fast.Fluids, Box);
    AddInfo(FString::Printf(TEXT("World.Budget: BudgetMs 0.0001 fell up to %llu ticks behind (%lld of %d frames over "
                                 "budget) and settled at tick %llu; unlimited settled at tick %llu; %lld acts each; "
                                 "worlds %s (%d fluid cells)"),
        MostBehind, SlowStats.OverBudgetFrames, Frames, SlowStats.Tick, FastStats.Tick, SlowStats.Acts,
        bEqual ? TEXT("equal") : TEXT("differ"), FFluidWorldSnapshot::Take(Fast.Fluids, Box).CountFluid()));
    return bEqual;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldFrameRatesTest, "Unnamed.Voxel.Fluid.World.FrameRates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldFrameRatesTest::RunTest(const FString& Parameters)
{
    // The fluids run on game ticks, not frames: at 4, 10, 30, 60 and 144 fps (up to 5 ticks a frame, or one every few
    // frames) every tick holds the same cells as at 144 fps, which sees every tick.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    constexpr uint64 LastTick = 120;
    const FFluidWorldBox Box{FIntVector(-14, -6, FluidWorldFloorTop), FIntVector(18, 22, 66)};
    constexpr int32 RateCount = 5;
    const int32 Rates[RateCount] = {144, 60, 30, 10, 4};
    TMap<uint64, TArray<FVoxelFluidCell>> Snapshots[RateCount];
    int32 Frames[RateCount] = {};
    for (int32 Index = 0; Index < RateCount; ++Index)
    {
        FFluidWorldRun Run(Context, false);
        if (!Run.Stream(*this, FIntPoint(0, 0), TEXT("stream (0, 0)")) || !Run.Start(*this)) return false;
        Run.Place(FIntVector(8, 8, 64), FluidWorldWater);
        Run.Place(FIntVector(-8, 8, 64), FluidWorldLava);
        FVoxelWorldClock Clock(.05, 5);
        uint64 Tick = Run.Fluids.GetStats().Tick;
        Snapshots[Index].Add(Tick, FFluidWorldSnapshot::Take(Run.Fluids, Box).Cells);
        // The reference runs past the others' last frame.
        const uint64 Last = Index == 0 ? LastTick + 5 : LastTick;
        while (Tick < Last && Frames[Index] < FluidWorldFrameCap)
        {
            ++Frames[Index];
            Run.Fluids.Advance(Clock.TakeTicks(1. / Rates[Index]), Run.Settings);
            const uint64 Now = Run.Fluids.GetStats().Tick;
            if (Now != Tick) Snapshots[Index].Add(Now, FFluidWorldSnapshot::Take(Run.Fluids, Box).Cells);
            Tick = Now;
        }
    }
    const auto CountFluid = [](const TArray<FVoxelFluidCell>* Cells)
    {
        int32 Count = 0;
        if (Cells)
            for (const FVoxelFluidCell& Cell : *Cells)
                Count += Cell.Fluid != EVoxelFluid::None;
        return Count;
    };
    const int32 FirstFluid = CountFluid(Snapshots[0].Find(0)), LastFluid = CountFluid(Snapshots[0].Find(LastTick));
    TestTrue(FString::Printf(
                 TEXT("the fluids flowed (%d fluid cells at tick 0, %d at %llu)"), FirstFluid, LastFluid, LastTick),
        LastFluid > FirstFluid);
    int32 Compared = 0;
    TArray<uint64> Differ, Missing;
    for (int32 Index = 1; Index < RateCount; ++Index)
    {
        for (const TPair<uint64, TArray<FVoxelFluidCell>>& Entry : Snapshots[Index])
        {
            const TArray<FVoxelFluidCell>* Reference = Snapshots[0].Find(Entry.Key);
            if (!Reference)
            {
                Missing.Add(Entry.Key);
                continue;
            }
            ++Compared;
            if (*Reference != Entry.Value) Differ.Add(Entry.Key);
        }
    }
    uint64 Reached = MAX_uint64;
    for (int32 Index = 1; Index < RateCount; ++Index)
    {
        uint64 Most = 0;
        for (const TPair<uint64, TArray<FVoxelFluidCell>>& Entry : Snapshots[Index])
            Most = FMath::Max(Most, Entry.Key);
        Reached = FMath::Min(Reached, Most);
    }
    Differ.Sort();
    TestTrue(FString::Printf(TEXT("every run reached tick %llu (the slowest reached %llu)"), LastTick, Reached),
        Reached >= LastTick);
    TestTrue(FString::Printf(TEXT("the 144 fps reference saw every tick the others did (%d missing)"), Missing.Num()),
        Missing.IsEmpty());
    if (!Differ.IsEmpty())
        AddError(
            FString::Printf(TEXT("%d ticks differ from 144 fps, the first at tick %llu"), Differ.Num(), Differ[0]));
    AddInfo(FString::Printf(TEXT("World.FrameRates: 144/60/30/10/4 fps ran %d/%d/%d/%d/%d frames to tick %llu; %d "
                                 "snapshots compared with 144 fps, %d differ; fluid cells %d to %d"),
        Frames[0], Frames[1], Frames[2], Frames[3], Frames[4], LastTick, Compared, Differ.Num(), FirstFluid,
        LastFluid));
    return Differ.IsEmpty() && Missing.IsEmpty() && Reached >= LastTick && LastFluid > FirstFluid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldFreezeTest, "Unnamed.Voxel.Fluid.World.Freeze",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldFreezeTest::RunTest(const FString& Parameters)
{
    // voxel.Fluids 0 freezes the fluids: no cell changes, the tick stands still and edits wake nothing. Enabled again,
    // the flow goes on as if never stopped, and the source placed while frozen stays a lone still source.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    FFluidWorldRun Run(Context, false);
    if (!Run.Stream(*this, FIntPoint(0, 0), TEXT("stream (0, 0)")) || !Run.Start(*this)) return false;
    FVoxelFluidTestWorld Oracle(FluidWorldFloorTop);
    const FIntVector Source(8, 8, 64);
    const FFluidWorldBox Box = FFluidWorldBox::Around(Source, 12);
    Run.Place(Source, FluidWorldWater);
    Oracle.Place(Source, FluidWorldWater);
    if (!StepFluidWorldWithOracle(*this, TEXT("before the freeze"), Run, Oracle, Box, 15)) return false;

    const FFluidWorldSnapshot Before = FFluidWorldSnapshot::Take(Run.Fluids, Box);
    const FVoxelFluidSystem::FStats Frozen = Run.Fluids.GetStats();
    FVoxelFluidSystem::FSettings Off = Run.Settings;
    Off.bEnabled = false;
    // 30 cells from the flow, placed mid-freeze.
    const FIntVector Lone = Source + FIntVector(30, 0, 0);
    constexpr int32 FrozenFrames = 100;
    for (int32 Frame = 0; Frame < FrozenFrames; ++Frame)
    {
        if (Frame == FrozenFrames / 2)
        {
            Run.Place(Lone, FluidWorldWater);
            TestTrue(
                TEXT("a source placed while frozen schedules nothing"), Run.Fluids.GetStats().Queued == Frozen.Queued);
        }
        Run.Fluids.Advance(1, Off);
    }
    const FVoxelFluidSystem::FStats Thawed = Run.Fluids.GetStats();
    TestTrue(FString::Printf(TEXT("the tick stood still (%llu, then %llu)"), Frozen.Tick, Thawed.Tick),
        Thawed.Tick == Frozen.Tick && Thawed.Behind == Frozen.Behind);
    TestTrue(FString::Printf(TEXT("nothing acted (%lld acts, then %lld)"), Frozen.Acts, Thawed.Acts),
        Thawed.Acts == Frozen.Acts && Thawed.Queued == Frozen.Queued);
    if (!CheckFluidWorldEqual(*this, TEXT("frozen"), Run.Fluids, Before, Box)) return false;

    // Thawed: tick 15 + k equals the oracle's 15 + k, to the settled world.
    if (!SettleFluidWorldWithOracle(*this, TEXT("after the freeze"), Run, Oracle, Box)) return false;
    bool bLone = Run.Fluids.Get(Lone) == Run.Fluids.GetCell(Run.Fluids.GetState(FluidWorldWater));
    for (int32 Face = 0; Face < 4; ++Face)
        bLone &= Run.Fluids.Get(Lone + VoxelFluidRules::GetFaceOffset(Face)).Fluid == EVoxelFluid::None;
    TestTrue(TEXT("the source placed while frozen stays a lone still source"), bLone);
    const FVoxelFluidSystem::FStats Settled = Run.Fluids.GetStats();
    AddInfo(FString::Printf(TEXT("World.Freeze: %d frozen frames at tick %llu changed nothing; thawed, settled at tick "
                                 "%llu equal to the oracle at every tick; the frozen-time source %s"),
        FrozenFrames, Frozen.Tick, Settled.Tick, bLone ? TEXT("stayed alone and still") : TEXT("moved")));
    return bLone;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldStaleMeshTest, "Unnamed.Voxel.Fluid.World.StaleMesh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldStaleMeshTest::RunTest(const FString& Parameters)
{
    // A mesh whose column changed while its job ran is still shown (flowing water would otherwise show none), counted
    // as stale; the column stays dirty and meshes again.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    const TUniquePtr<FVoxelColumnPipeline> Pipeline = Context.MakePipeline(true);
    Pipeline->SetFocus(GetFluidWorldRings(FIntPoint(0, 0)), 0);
    if (!RunFluidWorldPipeline(*this, *Pipeline, TEXT("stream (0, 0)"))) return false;
    const FIntPoint C(0, 0);
    const int64 StaleBefore = Pipeline->GetStats().Stale;

    // An edit queues C's mesh job; a second edit lands before it runs.
    Pipeline->SetCell(FIntVector(8, 8, 64), Context.Stone, 1);
    Pipeline->Update();
    int32 Job = INDEX_NONE;
    for (int32 Index = 0; Index < Pipeline->GetJobCount(); ++Index)
        if (Pipeline->IsJobPending(Index) && Pipeline->GetJobColumn(Index) == C) Job = Index;
    if (!TestTrue(TEXT("the edit queued a mesh job for C"), Job != INDEX_NONE)) return false;
    Pipeline->SetCell(FIntVector(5, 9, 64), Context.Stone, 2);
    Pipeline->RunJob(Job);
    Pipeline->Update();

    FIntPoint Peeked(INT32_MAX, INT32_MAX);
    const bool bShown = Pipeline->PeekReady(&Peeked) != nullptr && Peeked == C;
    TestTrue(TEXT("the stale mesh is ready for C"), bShown);
    const int64 StaleAfter = Pipeline->GetStats().Stale;
    TestTrue(FString::Printf(TEXT("counted as stale (%lld, then %lld)"), StaleBefore, StaleAfter),
        StaleAfter == StaleBefore + 1);
    const uint32 Dirty = Pipeline->GetStatus(C).DirtySections;
    TestTrue(FString::Printf(TEXT("C is still dirty (0x%x)"), Dirty), Dirty != 0);

    // Run until idle: the stale mesh is taken, then the latest one, and C is clean.
    TArray<FIntPoint> Taken;
    if (!RunFluidWorldPipeline(*this, *Pipeline, TEXT("remesh"), &Taken)) return false;
    int32 TakenC = 0;
    for (const FIntPoint Column : Taken)
        TakenC += Column == C;
    const FVoxelColumnStatus Status = Pipeline->GetStatus(C);
    TestTrue(FString::Printf(TEXT("C's stale and latest meshes were taken (%d taken)"), TakenC), TakenC == 2);
    TestTrue(TEXT("C is clean and its latest mesh on screen"),
        Status.DirtySections == 0 && Status.bOnScreen && !Status.bReady && !Status.bMeshing);
    TestTrue(TEXT("no more stale meshes"), Pipeline->GetStats().Stale == StaleAfter);
    AddInfo(FString::Printf(TEXT("World.StaleMesh: the stale mesh %s (stale %lld to %lld, dirty 0x%x); then %d meshes "
                                 "of C taken, C %s"),
        bShown ? TEXT("was shown") : TEXT("was not shown"), StaleBefore, StaleAfter, Dirty, TakenC,
        Status.DirtySections == 0 ? TEXT("clean") : TEXT("dirty")));
    return bShown && StaleAfter == StaleBefore + 1 && Status.DirtySections == 0 && TakenC == 2;
}

namespace
{
    // A scout M probe (W1's oracles, Source/Unnamed/Tests/VoxelFluidOracles.json) for the game world (W2b): its setup
    // and fluid commands as edits of the real blocks, in /fill's order, and its settled cells with the tick each last
    // changed. The probe's (x, height, z) is the cell (x, z, height); its floor top is the run's.
    struct FFluidWorldProbe
    {
        TArray<TPair<FIntVector, FString>> Edits;
        TArray<TTuple<FIntVector, FString, int32>> Cells;
        FFluidWorldBox Box;
        int32 SettledBy = 0;
    };

    bool LoadFluidWorldProbe(FAutomationTestBase& Test, const TCHAR* Name, FFluidWorldProbe& Out)
    {
        const FString Path = FPaths::ProjectDir() / TEXT("Source/Unnamed/Tests/VoxelFluidOracles.json");
        FString Text;
        TSharedPtr<FJsonObject> Root;
        const TArray<TSharedPtr<FJsonValue>>* Scenarios = nullptr;
        const TSharedPtr<FJsonObject>* Scenario = nullptr;
        if (FFileHelper::LoadFileToString(Text, *Path) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid() &&
            Root->TryGetArrayField(TEXT("scenarios"), Scenarios))
            for (const TSharedPtr<FJsonValue>& Value : *Scenarios)
                if (Value->TryGetObject(Scenario) && (*Scenario)->GetStringField(TEXT("name")) == Name)
                    break;
                else
                    Scenario = nullptr;
        if (!Scenario)
        {
            Test.AddError(FString::Printf(TEXT("%s has no scenario %s"), *Path, Name));
            return false;
        }
        const auto Number = [](const TArray<TSharedPtr<FJsonValue>>& Values, int32 Index)
        { return int32(Values[Index]->AsNumber()); };
        const auto Cell = [&Number](const TArray<TSharedPtr<FJsonValue>>& Values, int32 First)
        { return FIntVector(Number(Values, First), Number(Values, First + 2), Number(Values, First + 1)); };
        for (const TCHAR* Field : {TEXT("setup"), TEXT("fluids")})
            for (const TSharedPtr<FJsonValue>& Command : (*Scenario)->GetArrayField(Field))
            {
                const TArray<TSharedPtr<FJsonValue>>& Fields = Command->AsArray();
                const FIntVector From = Cell(Fields, 1);
                const FIntVector To = Fields[0]->AsString() == TEXT("fill") ? Cell(Fields, 4) : From;
                // /fill's order: x fastest, then height, then z.
                for (int32 Y = FMath::Min(From.Y, To.Y); Y <= FMath::Max(From.Y, To.Y); ++Y)
                    for (int32 Z = FMath::Min(From.Z, To.Z); Z <= FMath::Max(From.Z, To.Z); ++Z)
                        for (int32 X = FMath::Min(From.X, To.X); X <= FMath::Max(From.X, To.X); ++X)
                            Out.Edits.Emplace(FIntVector(X, Y, Z), Fields.Last()->AsString());
            }
        for (const TSharedPtr<FJsonValue>& Value : (*Scenario)->GetArrayField(TEXT("cells")))
        {
            const TArray<TSharedPtr<FJsonValue>>& Fields = Value->AsArray();
            Out.Cells.Emplace(Cell(Fields, 0), Fields[3]->AsString(), Number(Fields, 4));
        }
        // [[x min, max], [height min, max], [z min, max]].
        const TArray<TSharedPtr<FJsonValue>>& Box = (*Scenario)->GetArrayField(TEXT("box"));
        const auto Bound = [&Box](int32 Axis, int32 End) { return int32(Box[Axis]->AsArray()[End]->AsNumber()); };
        Out.Box.Min = FIntVector(Bound(0, 0), Bound(2, 0), Bound(1, 0));
        Out.Box.Max = FIntVector(Bound(0, 1), Bound(2, 1), Bound(1, 1));
        Out.SettledBy = int32((*Scenario)->GetNumberField(TEXT("settled_by")));
        return Test.TestTrue(FString::Printf(TEXT("%s pins cells"), Name), !Out.Cells.IsEmpty());
    }

    FFoundationBlockState ParseFluidWorldState(
        FAutomationTestBase& Test, const FFluidWorldRun& Run, const FString& Text)
    {
        FFoundationBlockState State;
        if (!Run.World.GetRegistry().Parse(Text, State))
            Test.AddError(FString::Printf(TEXT("%s does not parse"), *Text));
        return State;
    }

    // Replays the probe Name in Run, streamed and started around the origin, through the block world's edits, and pins
    // its settled cells with the tick each last changed; the rest of its box is air or stone, which the probe left out.
    bool RunFluidWorldProbe(FAutomationTestBase& Test, const TCHAR* Name, FFluidWorldRun& Run)
    {
        FFluidWorldProbe Probe;
        if (!LoadFluidWorldProbe(Test, Name, Probe) || !Run.Stream(Test, FIntPoint(0, 0), Name) || !Run.Start(Test))
            return false;
        TMap<FIntVector, int32> Changed;
        for (const TPair<FIntVector, FString>& Edit : Probe.Edits)
        {
            // The oak sapling isn't imported: a dandelion, washed away as well, stands in.
            const FString Block = Edit.Value == TEXT("oak_sapling") ? FString(TEXT("dandelion")) : Edit.Value;
            Run.World.Set(Edit.Key, ParseFluidWorldState(Test, Run, Block));
            Changed.Add(Edit.Key, 0);
        }
        TArray<FIntVector> Cells;
        Probe.Box.ForEach([&Cells](const FIntVector& Cell) { Cells.Add(Cell); });
        TArray<FFoundationBlockState> Last;
        for (const FIntVector& Cell : Cells)
            Last.Add(Run.World.Get(Cell).State);
        for (int32 Tick = 1; Tick <= Probe.SettledBy + 60; ++Tick)
        {
            Run.Step();
            for (int32 Index = 0; Index < Cells.Num(); ++Index)
            {
                const FFoundationBlockState Now = Run.World.Get(Cells[Index]).State;
                if (!(Now == Last[Index])) Changed.Add(Cells[Index], Tick);
                Last[Index] = Now;
            }
        }
        Test.TestTrue(FString::Printf(TEXT("%s: settled %d ticks after its last change"), Name, 60), Run.IsSettled());
        const FFoundationBlockRegistry& Registry = Run.World.GetRegistry();
        TSet<FIntVector> Pinned;
        int32 Differ = 0;
        FString First;
        const auto Report = [&](const FIntVector& Cell, const FString& Expected)
        {
            const int32* When = Changed.Find(Cell);
            if (Differ++ < FluidWorldErrorCap)
                First += FString::Printf(TEXT(" %s: %s at %d, expected %s;"), *GetFluidWorldCellText(Cell),
                    *Registry.ToString(Run.World.Get(Cell).State), When ? *When : -1, *Expected);
        };
        for (const TTuple<FIntVector, FString, int32>& Expected : Probe.Cells)
        {
            const FIntVector& Cell = Expected.Get<0>();
            Pinned.Add(Cell);
            const int32* When = Changed.Find(Cell);
            if (!(Run.World.Get(Cell).State == ParseFluidWorldState(Test, Run, Expected.Get<1>())) || !When ||
                *When != Expected.Get<2>())
                Report(Cell, FString::Printf(TEXT("%s at %d"), *Expected.Get<1>(), Expected.Get<2>()));
        }
        const FFoundationBlockState Stone = ParseFluidWorldState(Test, Run, TEXT("stone"));
        for (const FIntVector& Cell : Cells)
        {
            const FFoundationBlockState Have = Run.World.Get(Cell).State;
            if (!Pinned.Contains(Cell) && !Have.IsAir() && !(Have == Stone)) Report(Cell, TEXT("air or stone"));
        }
        if (Differ != 0)
            Test.AddError(FString::Printf(TEXT("%s: %d cells differ from the probe:%s"), Name, Differ, *First));
        else
            Test.AddInfo(FString::Printf(
                TEXT("%s: %d cells and the tick each last changed equal the probe's"), Name, Probe.Cells.Num()));
        return Differ == 0;
    }

    // The blocks of the authority's Popped events in Run, sorted, and an error for any other event.
    FString GetFluidWorldPops(FAutomationTestBase& Test, const FFluidWorldRun& Run)
    {
        TArray<FString> Pops;
        for (const FFoundationEvent& Event : Run.Authority.GetEvents())
            if (Event.Kind == EFoundationEventKind::Popped)
                Pops.Add(Run.World.GetRegistry().ToString(Event.State));
            else
                Test.AddError(FString::Printf(TEXT("an event of kind %d besides the pops"), int32(Event.Kind)));
        Pops.Sort();
        return FString::Join(Pops, TEXT(", "));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldSlabTest, "Unnamed.Voxel.Fluid.World.Slab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldSlabTest::RunTest(const FString& Parameters)
{
    // Scout M's w_slab in game (W2b): a waterlogged bottom slab (the property alone; the table reads slabs dry) is a
    // source, and its flow equals the probe's, cell for cell and tick for tick.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    FFluidWorldRun Run(Context, false);
    return RunFluidWorldProbe(*this, TEXT("w_slab"), Run);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldSlabWallTest, "Unnamed.Voxel.Fluid.World.SlabWall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldSlabWallTest::RunTest(const FString& Parameters)
{
    // Scout M's w_slab_wall in game (W2b): flowing water never enters the two slabs, and takes the cells of a poppy, a
    // short grass, a torch and an oak sapling (a dandelion here). Each pops once with no drop (plan Q7): a player's
    // inventory is untouched. (On the probe's stone floor, the authority's survival rules may pop a plant as the water
    // arrives beside it; the water takes its cell on the same tick either way.)
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    FFluidWorldRun Run(Context, false);
    const int32 Player = Run.AddPlayer();
    const int32 Revision = Run.Authority.FindPlayer(Player)->Inventory.Revision;
    const bool bProbe = RunFluidWorldProbe(*this, TEXT("w_slab_wall"), Run);
    TestEqual(TEXT("the four plants pop, each once"), GetFluidWorldPops(*this, Run),
        FString(TEXT("dandelion, poppy, short_grass, torch")));
    TestEqual(TEXT("nothing drops"), Run.Authority.FindPlayer(Player)->Inventory.Revision, Revision);
    return bProbe;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldWaterloggedBottomTest, "Unnamed.Voxel.Fluid.World.WaterloggedBottom",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldWaterloggedBottomTest::RunTest(const FString& Parameters)
{
    // Scout M's w_waterlogged_bottom in game (W2b): a water source over a dry bottom slab, walled in, leaves the slab
    // dry (waterlogged=false), and the rest equals the probe's.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    FFluidWorldRun Run(Context, false);
    return RunFluidWorldProbe(*this, TEXT("w_waterlogged_bottom"), Run);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldBreakWaterloggedTest, "Unnamed.Voxel.Fluid.World.BreakWaterlogged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldBreakWaterloggedTest::RunTest(const FString& Parameters)
{
    // What breaking a waterlogged slab leaves (W2b): its remainder, a water source, which the authority writes
    // (Unnamed.Foundation.Interaction.WaterRemains pins the command path). Walled in on four sides over a one-cell
    // hole, the waterlogged slab holds still; its remainder falls into the hole, as W1's oracle world does with a
    // source placed there, at every tick.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    FFluidWorldRun Run(Context, false);
    if (!Run.Stream(*this, FIntPoint(0, 0), TEXT("break")) || !Run.Start(*this)) return false;
    FVoxelFluidTestWorld Oracle(FluidWorldFloorTop);
    const FIntVector Slab(0, 0, 64), Hole(0, 0, 63);
    const FFoundationBlockState Stone = ParseFluidWorldState(*this, Run, TEXT("stone"));
    const FFoundationBlockState Wet =
        ParseFluidWorldState(*this, Run, TEXT("stone_slab[type=bottom,waterlogged=true]"));
    Run.World.Set(Hole, {});
    Oracle.Place(Hole, FVoxelFluidCell());
    for (int32 Face = 0; Face < 4; ++Face)
    {
        Run.World.Set(Slab + VoxelFluidRules::GetFaceOffset(Face), Stone);
        Oracle.Place(Slab + VoxelFluidRules::GetFaceOffset(Face), FVoxelFluidCell::MakeSolid());
    }
    FVoxelFluidCell WetSlab = FluidWorldWater;
    WetSlab.Walls = uint8(1u << int32(EVoxelFace::Down));
    WetSlab.bSolid = WetSlab.bWaterloggable = true;
    Run.World.Set(Slab, Wet);
    Oracle.Place(Slab, WetSlab);
    const FFluidWorldBox Box = FFluidWorldBox::Around(Slab, 2);
    if (!StepFluidWorldWithOracle(*this, TEXT("walled in"), Run, Oracle, Box, 30)) return false;
    TestTrue(TEXT("the walled-in waterlogged slab holds still over the hole"),
        Run.World.Get(Hole).State.IsAir() && Run.World.Get(Slab).State == Wet && Run.IsSettled());
    const FFoundationBlockState Left = Run.World.GetRegistry().GetRemainder(Wet);
    TestEqual(TEXT("a broken waterlogged slab leaves a water source"), Run.World.GetRegistry().ToString(Left),
        FString(TEXT("water[level=0]")));
    Run.World.Set(Slab, Left);
    Oracle.Place(Slab, FluidWorldWater);
    if (!StepFluidWorldWithOracle(*this, TEXT("broken"), Run, Oracle, Box, 30)) return false;
    return TestEqual(TEXT("the source fell into the hole"), Run.World.GetRegistry().ToString(Run.World.Get(Hole).State),
        FString(TEXT("water[level=8]")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWorldWashedAwayTest, "Unnamed.Voxel.Fluid.World.WashedAway",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWorldWashedAwayTest::RunTest(const FString& Parameters)
{
    // Washed away in game (W2b): water spreading over grass reaches a poppy and a tall grass, which survive there until
    // it does. Both pop with no drop (Q7) and the water takes their cells, as in W1's oracle world at every tick; the
    // tall grass's upper half pops with its lower half (the authority's settling after the fluid's writes) and stays
    // air.
    if (!HasFluidWorldAssets(*this)) return true;
    const FFluidWorldContext Context;
    FFluidWorldRun Run(Context, false);
    const int32 Player = Run.AddPlayer();
    const int32 Revision = Run.Authority.FindPlayer(Player)->Inventory.Revision;
    if (!Run.Stream(*this, FIntPoint(0, 0), TEXT("washed away")) || !Run.Start(*this)) return false;
    FVoxelFluidTestWorld Oracle(FluidWorldFloorTop);
    const FIntVector Source(0, 0, 64), Poppy(2, 0, 64), Lower(0, 2, 64), Upper(0, 2, 65);
    FVoxelFluidCell Plant;
    Plant.bWashedAway = true;
    for (const FIntVector& Cell : {Poppy, Lower})
    {
        Run.World.Set(Cell - FIntVector(0, 0, 1), ParseFluidWorldState(*this, Run, TEXT("grass_block")));
        Oracle.Place(Cell, Plant);
    }
    Run.World.Set(Poppy, ParseFluidWorldState(*this, Run, TEXT("poppy")));
    Run.World.Set(Lower, ParseFluidWorldState(*this, Run, TEXT("tall_grass[half=lower]")));
    Run.World.Set(Upper, ParseFluidWorldState(*this, Run, TEXT("tall_grass[half=upper]")));
    Run.Place(Source, FluidWorldWater);
    Oracle.Place(Source, FluidWorldWater);
    if (!StepFluidWorldWithOracle(*this, TEXT("washed away"), Run, Oracle, FFluidWorldBox::Around(Source, 9), 60))
        return false;
    const FFoundationBlockRegistry& Registry = Run.World.GetRegistry();
    TestEqual(TEXT("the water took the poppy's and the lower half's cells, then the upper half was air"),
        Registry.ToString(Run.World.Get(Poppy).State) + TEXT(" ") + Registry.ToString(Run.World.Get(Lower).State) +
            TEXT(" ") + Registry.ToString(Run.World.Get(Upper).State),
        FString(TEXT("water[level=2] water[level=2] air")));
    TestEqual(TEXT("the three pop, each once"), GetFluidWorldPops(*this, Run),
        FString(TEXT("poppy, tall_grass[half=lower], tall_grass[half=upper]")));
    TestEqual(TEXT("nothing drops"), Run.Authority.FindPlayer(Player)->Inventory.Revision, Revision);
    return true;
}

#endif
