#include "Biome/VoxelBiomeClimate.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Surface/VoxelOreVeins.h"
#include "Biome/VoxelBiomeStorage.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Misc/AutomationTest.h"
#include "Noise/VoxelNormalNoise.h"
#include "Storage/VoxelColumn.h"
#include "Surface/VoxelFreezePass.h"
#include "Surface/VoxelSurfaceContext.h"
#include "Surface/VoxelSurfaceEvaluator.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"

#include <cfloat>
#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr uint64 SurfaceTestSeed = 263;
    const TCHAR* const SurfaceTestPresets[3] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};

    struct FSurfaceTestWorld
    {
        FVoxelTerrainPreset Preset;
        FVoxelBiomeTable Biomes;
        FVoxelSurfaceInputs Surface;
    };

    bool SurfaceTestRead(FAutomationTestBase& Test, const TCHAR* PresetName, FSurfaceTestWorld& Out)
    {
        FString Error;
        const bool bRead = VoxelTerrainFactory::ReadInputs(
            VoxelWorldgen::GetReferenceRoot(), PresetName, Out.Preset, Out.Biomes, Out.Surface, Error);
        return Test.TestTrue(FString::Printf(TEXT("%s inputs read (%s)"), PresetName, *Error), bRead);
    }

    FVoxelSurfaceCondition SurfaceTestStoneDepth(int32 Offset, bool bAddDepth, int32 Range, bool bCeiling)
    {
        FVoxelSurfaceCondition Condition;
        Condition.Op = EVoxelSurfaceConditionOp::StoneDepth;
        Condition.Offset = Offset;
        Condition.bAddDepth = bAddDepth;
        Condition.SecondaryDepthRange = Range;
        Condition.bCeiling = bCeiling;
        return Condition;
    }

    FVoxelSurfaceCondition SurfaceTestYAbove(EVoxelAnchorKind Kind, int32 Value, int32 Multiplier, bool bAddDepth)
    {
        FVoxelSurfaceCondition Condition;
        Condition.Op = EVoxelSurfaceConditionOp::YAbove;
        Condition.Anchor = {Kind, Value};
        Condition.SurfaceDepthMultiplier = Multiplier;
        Condition.bAddDepth = bAddDepth;
        return Condition;
    }

    FVoxelSurfaceCondition SurfaceTestWater(int32 Offset, int32 Multiplier, bool bAddDepth)
    {
        FVoxelSurfaceCondition Condition;
        Condition.Op = EVoxelSurfaceConditionOp::Water;
        Condition.Offset = Offset;
        Condition.SurfaceDepthMultiplier = Multiplier;
        Condition.bAddDepth = bAddDepth;
        return Condition;
    }

    FVoxelSurfaceCondition SurfaceTestSimple(EVoxelSurfaceConditionOp Op, int32 Input = INDEX_NONE)
    {
        FVoxelSurfaceCondition Condition;
        Condition.Op = Op;
        Condition.Input = Input;
        return Condition;
    }

    // One condition tested on one hand-made block and column.
    struct FSurfaceTestCheck
    {
        FString Name;
        int32 Condition = INDEX_NONE;
        bool bExpected = false;
        int32 Z = 0;
        int32 Up = 1, Down = 1;
        int32 Water = FVoxelSurfaceBlock::NoWater;
        int32 SurfaceDepth = 0, MinLevel = 0;
        double Secondary = 0;
        FString Noise;
        double NoiseValue = 0;
        int32 X = 0, Y = 0;
        int32 Biome = FVoxelSurfaceBlock::UnknownBiome;
    };

    bool SurfaceTestHolds(const FVoxelSurfaceEvaluator& Evaluator, const FSurfaceTestCheck& Check,
        const FVoxelSurfaceChunk& Chunk, int32 LocalX = 0, int32 LocalY = 0)
    {
        FVoxelSurfaceColumn Column;
        Column.X = Check.X;
        Column.Y = Check.Y;
        Column.SurfaceDepth = Check.SurfaceDepth;
        Column.MinSurfaceLevel = Check.MinLevel;
        Column.SetNoise(Evaluator.GetSecondarySlot(), Check.Secondary);
        if (!Check.Noise.IsEmpty()) Column.SetNoise(Evaluator.FindNoiseSlot(Check.Noise), Check.NoiseValue);
        FVoxelSurfaceBlock Block;
        Block.Z = Check.Z;
        Block.DepthAbove = Check.Up;
        Block.DepthBelow = Check.Down;
        Block.WaterHeight = Check.Water;
        Block.Biome = Check.Biome;
        return Evaluator.TestCondition(Check.Condition, Chunk, Column, LocalX, LocalY, Block);
    }

    // A tree whose root gives gold_block when the chosen condition holds (or always, without one).
    struct FSurfaceTestBench
    {
        FVoxelSurfaceInputs Inputs;
        FVoxelSurfaceEvaluator Evaluator;

        int32 Add(const FVoxelSurfaceCondition& Condition) { return Inputs.Rules.Conditions.Add(Condition); }

        int32 AddNoise(const FString& Id, double Min, double Max, bool bIs3D)
        {
            FVoxelSurfaceCondition Condition;
            Condition.Op = EVoxelSurfaceConditionOp::NoiseThreshold;
            Condition.First = Inputs.Rules.AddString(Id);
            Condition.MinThreshold = Min;
            Condition.MaxThreshold = Max;
            Condition.bIs3D = bIs3D;
            return Add(Condition);
        }

        int32 AddGradient(const FString& Name, int32 TrueAt, int32 FalseAt)
        {
            FVoxelSurfaceCondition Condition;
            Condition.Op = EVoxelSurfaceConditionOp::VerticalGradient;
            Condition.First = Inputs.Rules.AddString(Name);
            Condition.Anchor = {EVoxelAnchorKind::Absolute, TrueAt};
            Condition.FalseAnchor = {EVoxelAnchorKind::Absolute, FalseAt};
            return Add(Condition);
        }

        int32 AddBiome(const TArray<FString>& Ids)
        {
            FVoxelSurfaceCondition Condition;
            Condition.Op = EVoxelSurfaceConditionOp::Biome;
            Condition.First = Inputs.Rules.Strings.Num();
            Condition.Count = Ids.Num();
            Inputs.Rules.Strings.Append(Ids);
            return Add(Condition);
        }

        bool Compile(FAutomationTestBase& Test, const FSurfaceTestWorld& World, const FVoxelBlockTable& Table,
            int32 Condition = INDEX_NONE)
        {
            FVoxelSurfaceRules& Rules = Inputs.Rules;
            Inputs.Noises = World.Surface.Noises;
            FVoxelSurfaceRule Gold;
            Gold.Op = EVoxelSurfaceRuleOp::Block;
            Gold.Block = Rules.AddString(TEXT("gold_block"));
            Rules.Rules = {Gold};
            Rules.Root = 0;
            if (Condition != INDEX_NONE)
            {
                FVoxelSurfaceRule If;
                If.Op = EVoxelSurfaceRuleOp::Condition;
                If.Condition = Condition;
                If.Then = 0;
                Rules.Root = Rules.Rules.Add(If);
            }
            FString Error;
            const bool bCompiled = Evaluator.Compile(Inputs, Table, World.Biomes, SurfaceTestSeed, &Error);
            return Test.TestTrue(FString::Printf(TEXT("hand-made tree compiles (%s)"), *Error), bCompiled);
        }
    };

    struct FSurfaceTestRun
    {
        int32 FromZ, ToZ;
        EVoxelSurfaceKind Kind;
    };

    TArray<EVoxelSurfaceKind> SurfaceTestKinds(const TArray<FSurfaceTestRun>& Runs)
    {
        TArray<EVoxelSurfaceKind> Kinds;
        Kinds.Init(EVoxelSurfaceKind::Air, VoxelConstants::Height);
        for (const FSurfaceTestRun& Run : Runs)
            for (int32 Z = Run.FromZ; Z <= Run.ToZ; ++Z)
                Kinds[Z - VoxelConstants::MinZ] = Run.Kind;
        return Kinds;
    }

    // Applies "if the last condition holds, gold_block" down a column and returns the states (stone where solid).
    TArray<uint16> SurfaceTestMarked(FAutomationTestBase& Test, const FSurfaceTestWorld& World,
        const FVoxelBlockTable& Table, const TArray<FVoxelSurfaceCondition>& Conditions,
        TConstArrayView<EVoxelSurfaceKind> Kinds, int32 SurfaceDepth = 0)
    {
        FSurfaceTestBench Bench;
        for (const FVoxelSurfaceCondition& Condition : Conditions)
            Bench.Add(Condition);
        TArray<uint16> States;
        States.Init(0, VoxelConstants::Height);
        if (!Bench.Compile(Test, World, Table, Conditions.Num() - 1)) return States;
        const uint16 Stone = Table.FindState(TEXT("stone"));
        for (int32 Index = 0; Index < States.Num(); ++Index)
            if (Kinds[Index] == EVoxelSurfaceKind::Solid) States[Index] = Stone;
        const FVoxelSurfaceChunk Chunk;
        FVoxelSurfaceColumn Column;
        Column.SurfaceDepth = SurfaceDepth;
        Bench.Evaluator.ApplyColumn(Chunk, Column, 0, 0, Kinds, States);
        return States;
    }

    // Column scan (facts 2.1): depth above and below and the water height at one block, through the markers.
    void SurfaceTestScan(FAutomationTestBase& Test, const FSurfaceTestWorld& World, const FVoxelBlockTable& Table,
        const TCHAR* Name, const TArray<EVoxelSurfaceKind>& Kinds, int32 Z, int32 Up, int32 Down, int32 Water)
    {
        const uint16 Gold = Table.FindState(TEXT("gold_block"));
        const int32 Index = Z - VoxelConstants::MinZ;
        const auto Marked = [&](const FVoxelSurfaceCondition& Condition)
        { return SurfaceTestMarked(Test, World, Table, {Condition}, Kinds)[Index] == Gold; };

        TArray<int32> DepthBelow;
        DepthBelow.SetNumZeroed(Kinds.Num());
        VoxelSurfaceContext::ComputeDepthBelow(Kinds, DepthBelow);
        Test.TestEqual(FString::Printf(TEXT("%s: ComputeDepthBelow at %d"), Name, Z), DepthBelow[Index], Down);

        Test.TestTrue(FString::Printf(TEXT("%s: depth above %d at %d holds"), Name, Up, Z),
            Marked(SurfaceTestStoneDepth(Up - 1, false, 0, false)));
        if (Up >= 2)
            Test.TestFalse(FString::Printf(TEXT("%s: depth above is not %d at %d"), Name, Up - 1, Z),
                Marked(SurfaceTestStoneDepth(Up - 2, false, 0, false)));
        Test.TestTrue(FString::Printf(TEXT("%s: depth below %d at %d holds"), Name, Down, Z),
            Marked(SurfaceTestStoneDepth(Down - 1, false, 0, true)));
        if (Down >= 2)
            Test.TestFalse(FString::Printf(TEXT("%s: depth below is not %d at %d"), Name, Down - 1, Z),
                Marked(SurfaceTestStoneDepth(Down - 2, false, 0, true)));

        if (Water == FVoxelSurfaceBlock::NoWater)
            Test.TestTrue(
                FString::Printf(TEXT("%s: no water at %d"), Name, Z), Marked(SurfaceTestWater(1000, 0, false)));
        else
        {
            Test.TestTrue(FString::Printf(TEXT("%s: water height %d at %d holds"), Name, Water, Z),
                Marked(SurfaceTestWater(Z - Water, 0, false)));
            Test.TestFalse(FString::Printf(TEXT("%s: water height is not %d at %d"), Name, Water + 1, Z),
                Marked(SurfaceTestWater(Z - Water + 1, 0, false)));
        }
    }

    void SurfaceTestScans(FAutomationTestBase& Test, const FSurfaceTestWorld& World, const FVoxelBlockTable& Table)
    {
        constexpr int32 None = FVoxelSurfaceBlock::NoWater;
        const EVoxelSurfaceKind Solid = EVoxelSurfaceKind::Solid;
        const EVoxelSurfaceKind Water = EVoxelSurfaceKind::Water;
        const EVoxelSurfaceKind Lava = EVoxelSurfaceKind::Lava;
        const auto Check = [&](const TCHAR* Name, const TArray<FSurfaceTestRun>& Runs, int32 Z, int32 Up, int32 Down,
                               int32 WaterHeight)
        { SurfaceTestScan(Test, World, Table, Name, SurfaceTestKinds(Runs), Z, Up, Down, WaterHeight); };

        const TArray<FSurfaceTestRun> Cave = {{-64, 39, Solid}, {50, 70, Solid}};
        Check(TEXT("C1"), Cave, 70, 1, 21, None);
        Check(TEXT("C2"), Cave, 50, 21, 1, None);
        Check(TEXT("C3"), Cave, 39, 1, 104, None);
        Check(TEXT("C4"), Cave, -64, 104, 1, None);
        Check(TEXT("C5"), {{-64, 60, Solid}, {61, 70, Water}}, 60, 1, 125, 71);
        const TArray<FSurfaceTestRun> Pocket = {{-64, 40, Solid}, {41, 45, Water}, {46, 60, Solid}};
        Check(TEXT("C6"), Pocket, 46, 15, 1, None);
        Check(TEXT("C7"), Pocket, 40, 16, 105, 46);
        Check(TEXT("C8"), {{-64, 59, Solid}, {60, 69, Water}, {70, 79, Solid}, {80, 89, Water}}, 59, 11, 124, 90);
        Check(TEXT("C9"), {{-64, 40, Solid}, {51, 60, Solid}, {61, 70, Water}}, 40, 1, 105, None);
        Check(TEXT("C10"), {{-64, 60, Solid}, {61, 70, Lava}}, 60, 1, 125, 71);
        const TArray<FSurfaceTestRun> Thin = {{-64, 66, Solid}, {68, 68, Solid}, {70, 70, Solid}};
        Check(TEXT("C11 at 70"), Thin, 70, 1, 1, None);
        Check(TEXT("C11 at 68"), Thin, 68, 1, 1, None);
        Check(TEXT("C11 at 66"), Thin, 66, 1, 131, None);

        // Y7 and Y8: with the stone depth added, a y test holds for a whole run or none of it.
        const uint16 Gold = Table.FindState(TEXT("gold_block"));
        const TArray<EVoxelSurfaceKind> Land = SurfaceTestKinds({{-64, 60, Solid}});
        const auto CountMarked = [&](const FVoxelSurfaceCondition& Condition)
        {
            int32 Count = 0;
            for (const uint16 State : SurfaceTestMarked(Test, World, Table, {Condition}, Land))
                Count += State == Gold;
            return Count;
        };
        Test.TestEqual(TEXT("Y7: absolute 61 with stone depth marks the whole run"),
            CountMarked(SurfaceTestYAbove(EVoxelAnchorKind::Absolute, 61, 0, true)), 125);
        Test.TestEqual(TEXT("Y8: absolute 62 with stone depth marks none"),
            CountMarked(SurfaceTestYAbove(EVoxelAnchorKind::Absolute, 62, 0, true)), 0);

        // W7 and W8: under an air gap the water is forgotten; lava counts as water.
        const uint16 Marked9 = SurfaceTestMarked(Test, World, Table, {SurfaceTestWater(-1, 0, false)},
            SurfaceTestKinds({{-64, 40, Solid}, {51, 60, Solid}, {61, 70, Water}}))[40 + 64];
        Test.TestTrue(TEXT("W7: not_underwater holds on the floor below an air gap"), Marked9 == Gold);
        const uint16 Marked10 = SurfaceTestMarked(Test, World, Table, {SurfaceTestWater(-1, 0, false)},
            SurfaceTestKinds({{-64, 60, Solid}, {61, 70, Lava}}))[60 + 64];
        Test.TestTrue(TEXT("W8: not_underwater fails under lava"), Marked10 != Gold);
    }

    struct FSurfaceTestDepthCase
    {
        const TCHAR* Name;
        int32 Offset;
        bool bAddDepth;
        int32 Range;
        bool bCeiling;
        int32 SurfaceDepth;
        double Secondary;
        int32 Depth;
        bool bExpected;
    };

    struct FSurfaceTestYCase
    {
        const TCHAR* Name;
        EVoxelAnchorKind Kind;
        int32 Value;
        int32 Multiplier;
        bool bAddDepth;
        int32 SurfaceDepth;
        int32 Z;
        bool bExpected;
    };

    struct FSurfaceTestWaterCase
    {
        const TCHAR* Name;
        int32 Offset;
        int32 Multiplier;
        bool bAddDepth;
        int32 SurfaceDepth;
        int32 Z;
        int32 Up;
        int32 Water;
        bool bExpected;
    };

    struct FSurfaceTestNoiseCase
    {
        const TCHAR* Name;
        double Min, Max, Value;
        bool bExpected;
    };

    void SurfaceTestFillStorage(
        FVoxelBiomeStorage& Storage, int32 Lower, int32 SplitLayer = FVoxelBiomeStorage::Layers, int32 Upper = 0)
    {
        Storage.Init(FIntPoint(0, 0), SurfaceTestSeed);
        for (int32 Layer = 0; Layer < FVoxelBiomeStorage::Layers; ++Layer)
            for (int32 Y = 0; Y < FVoxelBiomeStorage::Width; ++Y)
                for (int32 X = 0; X < FVoxelBiomeStorage::Width; ++X)
                    Storage.SetQuart(X, Y, Layer, uint8(Layer < SplitLayer ? Lower : Upper));
    }

    // Everything TestCondition decides from plain values: stone depth, y_above, water, noise, hole, preliminary
    // surface, vertical gradient and biome (facts 2.2 to 2.5 and 2.7).
    void SurfaceTestHandBuilt(FAutomationTestBase& Test, const FSurfaceTestWorld& World, const FVoxelBlockTable& Table)
    {
        FSurfaceTestBench Bench;
        TArray<FSurfaceTestCheck> Checks;
        const auto Record = [&](FSurfaceTestCheck Check, const FVoxelSurfaceCondition& Condition)
        {
            Check.Condition = Bench.Add(Condition);
            Checks.Add(Check);
        };

        const FSurfaceTestDepthCase DepthCases[] = {
            {TEXT("S1 on_floor d1"), 0, false, 0, false, 0, 0, 1, true},
            {TEXT("S2 on_floor d2"), 0, false, 0, false, 0, 0, 2, false},
            {TEXT("S3 floor offset 2 d3"), 2, false, 0, false, 0, 0, 3, true},
            {TEXT("S3 floor offset 2 d4"), 2, false, 0, false, 0, 0, 4, false},
            {TEXT("S4 under_floor sd5 d6"), 0, true, 0, false, 5, 0, 6, true},
            {TEXT("S4 under_floor sd5 d7"), 0, true, 0, false, 5, 0, 7, false},
            {TEXT("S5 under_floor sd0 d1"), 0, true, 0, false, 0, 0, 1, true},
            {TEXT("S5 under_floor sd0 d2"), 0, true, 0, false, 0, 0, 2, false},
            {TEXT("S6 deep_under_floor d8"), 0, true, 6, false, 4, 0.0, 8, true},
            {TEXT("S6 deep_under_floor d9"), 0, true, 6, false, 4, 0.0, 9, false},
            {TEXT("S7 deep_under_floor ss-1.3 d5"), 0, true, 6, false, 4, -1.3, 5, true},
            {TEXT("S7 deep_under_floor ss-1.3 d6"), 0, true, 6, false, 4, -1.3, 6, false},
            {TEXT("S8 deep_under_floor ss-1.44 d4"), 0, true, 6, false, 4, -1.44, 4, true},
            {TEXT("S8 deep_under_floor ss-1.44 d5"), 0, true, 6, false, 4, -1.44, 5, false},
            {TEXT("S9 very_deep_under_floor d19"), 0, true, 30, false, 5, -0.0907, 19, true},
            {TEXT("S9 very_deep_under_floor d20"), 0, true, 30, false, 5, -0.0907, 20, false},
            {TEXT("S10 very_deep_under_floor negative limit"), 0, true, 30, false, 4, -1.44, 1, false},
            {TEXT("S11 very_deep_under_floor d41"), 0, true, 30, false, 4, 1.4, 41, true},
            {TEXT("S11 very_deep_under_floor d42"), 0, true, 30, false, 4, 1.4, 42, false},
            {TEXT("S12 floor range 6 without depth d3"), 0, false, 6, false, 5, -0.0907, 3, true},
            {TEXT("S12 floor range 6 without depth d4"), 0, false, 6, false, 5, -0.0907, 4, false},
            {TEXT("S13 on_ceiling d1"), 0, false, 0, true, 0, 0, 1, true},
            {TEXT("S13 on_ceiling d2"), 0, false, 0, true, 0, 0, 2, false},
            {TEXT("S14 ceiling offset 2 with depth d8"), 2, true, 0, true, 5, 0, 8, true},
            {TEXT("S14 ceiling offset 2 with depth d9"), 2, true, 0, true, 5, 0, 9, false},
        };
        for (const FSurfaceTestDepthCase& Case : DepthCases)
        {
            FSurfaceTestCheck Check;
            Check.Name = Case.Name;
            Check.bExpected = Case.bExpected;
            Check.SurfaceDepth = Case.SurfaceDepth;
            Check.Secondary = Case.Secondary;
            (Case.bCeiling ? Check.Down : Check.Up) = Case.Depth;
            Record(Check, SurfaceTestStoneDepth(Case.Offset, Case.bAddDepth, Case.Range, Case.bCeiling));
        }

        constexpr EVoxelAnchorKind Abs = EVoxelAnchorKind::Absolute;
        const FSurfaceTestYCase YCases[] = {
            {TEXT("Y1 at 70"), Abs, 70, 0, false, 0, 70, true},
            {TEXT("Y1 at 69"), Abs, 70, 0, false, 0, 69, false},
            {TEXT("Y2 at 75"), Abs, 70, 1, false, 5, 75, true},
            {TEXT("Y2 at 74"), Abs, 70, 1, false, 5, 74, false},
            {TEXT("Y3 at 76"), Abs, 70, 2, false, 3, 76, true},
            {TEXT("Y3 at 75"), Abs, 70, 2, false, 3, 75, false},
            {TEXT("Y4 at 65"), Abs, 70, -1, false, 5, 65, true},
            {TEXT("Y4 at 64"), Abs, 70, -1, false, 5, 64, false},
            {TEXT("Y5 at -59"), EVoxelAnchorKind::AboveBottom, 5, 0, false, 0, -59, true},
            {TEXT("Y5 at -60"), EVoxelAnchorKind::AboveBottom, 5, 0, false, 0, -60, false},
            {TEXT("Y6 at 309"), EVoxelAnchorKind::BelowTop, 10, 0, false, 0, 309, true},
            {TEXT("Y6 at 308"), EVoxelAnchorKind::BelowTop, 10, 0, false, 0, 308, false},
            {TEXT("Y9 run top 59"), Abs, 63, -1, true, 3, 59, true},
            {TEXT("Y9 run top 58"), Abs, 63, -1, true, 3, 58, false},
            {TEXT("Y10 run top 76"), Abs, 74, 1, true, 3, 76, true},
            {TEXT("Y10 run top 75"), Abs, 74, 1, true, 3, 75, false},
        };
        for (const FSurfaceTestYCase& Case : YCases)
        {
            FSurfaceTestCheck Check;
            Check.Name = Case.Name;
            Check.bExpected = Case.bExpected;
            Check.SurfaceDepth = Case.SurfaceDepth;
            Check.Z = Case.Z;
            Record(Check, SurfaceTestYAbove(Case.Kind, Case.Value, Case.Multiplier, Case.bAddDepth));
        }

        // Y11: the swamp's "y_above 62 and not y_above 63".
        const int32 Above62 = Bench.Add(SurfaceTestYAbove(Abs, 62, 0, false));
        const int32 Above63 = Bench.Add(SurfaceTestYAbove(Abs, 63, 0, false));
        const int32 NotAbove63 = Bench.Add(SurfaceTestSimple(EVoxelSurfaceConditionOp::Not, Above63));
        const int32 SwampZs[3] = {62, 61, 63};
        const bool SwampExpected[3] = {true, false, false};

        const FSurfaceTestWaterCase WaterCases[] = {
            {TEXT("W1 no water"), -1, 0, false, 0, 60, 1, FVoxelSurfaceBlock::NoWater, true},
            {TEXT("W2 not_underwater one block of water"), -1, 0, false, 0, 60, 1, 62, false},
            {TEXT("W3 offset 0 one block of water"), 0, 0, false, 0, 60, 1, 62, false},
            {TEXT("W4 offset -1 with stone depth, W62"), -1, 0, true, 0, 60, 1, 62, true},
            {TEXT("W5 offset -1 with stone depth, W63"), -1, 0, true, 0, 60, 1, 63, false},
            {TEXT("W6 deep water 10"), -6, -1, true, 4, 60, 1, 71, true},
            {TEXT("W6 deep water 11"), -6, -1, true, 4, 60, 1, 72, false},
            {TEXT("W9 offset -25 under W90"), -25, 0, false, 0, 59, 11, 90, false},
        };
        for (const FSurfaceTestWaterCase& Case : WaterCases)
        {
            FSurfaceTestCheck Check;
            Check.Name = Case.Name;
            Check.bExpected = Case.bExpected;
            Check.SurfaceDepth = Case.SurfaceDepth;
            Check.Z = Case.Z;
            Check.Up = Case.Up;
            Check.Water = Case.Water;
            Record(Check, SurfaceTestWater(Case.Offset, Case.Multiplier, Case.bAddDepth));
        }

        // Noise: the column's value, both bounds inclusive.
        const FString NoiseId = TEXT("minecraft:calcite");
        constexpr double Boundary = 0.5557277202606201;
        const double NextUp = std::nextafter(Boundary, DBL_MAX), NextDown = std::nextafter(Boundary, -DBL_MAX);
        const FSurfaceTestNoiseCase NoiseCases[] = {
            {TEXT("N1 inside"), -0.2, 0.3, 0.0, true},
            {TEXT("N2 at min"), Boundary, 1.0, Boundary, true},
            {TEXT("N3 at max"), -1.0, Boundary, Boundary, true},
            {TEXT("N4 min = max = v"), Boundary, Boundary, Boundary, true},
            {TEXT("N5 below min"), Boundary, 1.0, NextDown, false},
            {TEXT("N5 above max"), -1.0, Boundary, NextUp, false},
            {TEXT("N6 unbounded max"), 0.0, DBL_MAX, 5.0, true},
        };
        for (const FSurfaceTestNoiseCase& Case : NoiseCases)
        {
            FSurfaceTestCheck Check;
            Check.Name = Case.Name;
            Check.bExpected = Case.bExpected;
            Check.Noise = NoiseId;
            Check.NoiseValue = Case.Value;
            Record(Check, FVoxelSurfaceCondition());
            Bench.Inputs.Rules.Conditions.Pop();
            Checks.Last().Condition = Bench.AddNoise(NoiseId, Case.Min, Case.Max, false);
        }
        // N7: a flat noise reads the column's value at every y.
        const int32 FlatNoise = Bench.AddNoise(NoiseId, 0.2, 0.4, false);
        for (const int32 Z : {-60, 0, 300})
        {
            FSurfaceTestCheck Check;
            Check.Name = FString::Printf(TEXT("N7 flat noise at %d"), Z);
            Check.bExpected = true;
            Check.Z = Z;
            Check.Noise = NoiseId;
            Check.NoiseValue = 0.3;
            Check.Condition = FlatNoise;
            Checks.Add(Check);
        }
        // N8: a 3D noise is sampled at the block's y, whatever the column holds.
        const FVoxelDensityNoise* Description = World.Surface.Noises.FindByPredicate(
            [&NoiseId](const FVoxelDensityNoise& Noise) { return Noise.Name == NoiseId; });
        if (!Test.TestNotNull(TEXT("the calcite noise is read"), Description)) return;
        const FVoxelNormalNoise Calcite(SurfaceTestSeed, *Description);
        constexpr int32 NoiseX = 123, NoiseY = -45, NoiseZ = 20;
        const double Sample20 = Calcite.Sample(NoiseX, NoiseZ, NoiseY);
        const double Sample21 = Calcite.Sample(NoiseX, NoiseZ + 1, NoiseY);
        Test.TestTrue(TEXT("N8: the noise differs between y 20 and y 21"), Sample20 != Sample21);
        const int32 Exact3D = Bench.AddNoise(NoiseId, Sample20, Sample20, true);
        const int32 Wide3D = Bench.AddNoise(NoiseId, -100, 100, true);
        const int32 Wide2D = Bench.AddNoise(NoiseId, -100, 100, false);
        const auto Record3D = [&](const TCHAR* Name, int32 Condition, int32 Z, bool bExpected)
        {
            FSurfaceTestCheck Check;
            Check.Name = Name;
            Check.bExpected = bExpected;
            Check.Z = Z;
            Check.X = NoiseX;
            Check.Y = NoiseY;
            Check.Noise = NoiseId;
            Check.NoiseValue = 500;
            Check.Condition = Condition;
            Checks.Add(Check);
        };
        Record3D(TEXT("N8 3D matches the noise at its y"), Exact3D, NoiseZ, true);
        Record3D(TEXT("N8 3D does not match another y"), Exact3D, NoiseZ + 1, false);
        Record3D(TEXT("N8 3D ignores the column's value"), Wide3D, NoiseZ, true);
        Record3D(TEXT("N7 flat reads the column's value"), Wide2D, NoiseZ, false);

        // Hole, preliminary surface.
        Record({.Name = TEXT("H1 sd 0"), .bExpected = true, .SurfaceDepth = 0},
            SurfaceTestSimple(EVoxelSurfaceConditionOp::Hole));
        Record({.Name = TEXT("H1 sd 1"), .SurfaceDepth = 1}, SurfaceTestSimple(EVoxelSurfaceConditionOp::Hole));
        Record({.Name = TEXT("H1 sd -1"), .bExpected = true, .SurfaceDepth = -1},
            SurfaceTestSimple(EVoxelSurfaceConditionOp::Hole));
        const FVoxelSurfaceCondition AbovePreliminary =
            SurfaceTestSimple(EVoxelSurfaceConditionOp::AbovePreliminarySurface);
        Record({.Name = TEXT("P1 at 37"), .bExpected = true, .Z = 37, .MinLevel = 37}, AbovePreliminary);
        Record({.Name = TEXT("P1 at 36"), .Z = 36, .MinLevel = 37}, AbovePreliminary);
        Record({.Name = TEXT("P3 at 33"), .bExpected = true, .Z = 33, .MinLevel = 33}, AbovePreliminary);
        Record({.Name = TEXT("P3 at 32"), .Z = 32, .MinLevel = 33}, AbovePreliminary);

        const int32 Gradient = Bench.AddGradient(TEXT("minecraft:deepslate"), 0, 8);
        const int32 PlainsOrDesert = Bench.AddBiome({TEXT("minecraft:plains"), TEXT("minecraft:desert")});
        const int32 Plains = World.Biomes.FindBiome(TEXT("minecraft:plains"));
        const int32 Desert = World.Biomes.FindBiome(TEXT("minecraft:desert"));
        const int32 Swamp = World.Biomes.FindBiome(TEXT("minecraft:swamp"));
        if (!Test.TestTrue(TEXT("plains, desert and swamp are overworld biomes"),
                Plains != INDEX_NONE && Desert != INDEX_NONE && Swamp != INDEX_NONE))
            return;
        Record({.Name = TEXT("B1 plains"), .bExpected = true, .Biome = Plains}, FVoxelSurfaceCondition());
        Bench.Inputs.Rules.Conditions.Pop();
        Checks.Last().Condition = PlainsOrDesert;
        for (const auto& [Name, Biome, bExpected] : {TTuple<const TCHAR*, int32, bool>{TEXT("B1 desert"), Desert, true},
                 {TEXT("B1 swamp"), Swamp, false}, {TEXT("B1 no biome"), INDEX_NONE, false}})
        {
            FSurfaceTestCheck Check;
            Check.Name = Name;
            Check.Condition = PlainsOrDesert;
            Check.bExpected = bExpected;
            Check.Biome = Biome;
            Checks.Add(Check);
        }

        if (!Bench.Compile(Test, World, Table)) return;
        const FVoxelSurfaceEvaluator& Evaluator = Bench.Evaluator;
        const FVoxelSurfaceChunk NoBiomes;
        for (const FSurfaceTestCheck& Check : Checks)
            Test.TestEqual(Check.Name, SurfaceTestHolds(Evaluator, Check, NoBiomes), Check.bExpected);
        for (int32 Index = 0; Index < 3; ++Index)
        {
            FSurfaceTestCheck Check;
            Check.Condition = Index == 2 ? NotAbove63 : Above62;
            Check.Z = SwampZs[Index];
            const bool bAbove62 = SurfaceTestHolds(Evaluator, Check, NoBiomes);
            Check.Condition = NotAbove63;
            const bool bSwamp = bAbove62 && SurfaceTestHolds(Evaluator, Check, NoBiomes);
            Test.TestEqual(FString::Printf(TEXT("Y11 swamp rule at %d"), Check.Z), bSwamp, SwampExpected[Index]);
        }

        // PrepareColumn: the minimum surface level from the preliminary surface and the surface depth.
        const double Preliminary[3] = {40.5, 40.0, 41.9375};
        for (const double P : Preliminary)
        {
            FVoxelSurfaceColumn Column;
            Evaluator.PrepareColumn(Column, 0, 1, P, false);
            Test.TestEqual(FString::Printf(TEXT("minimum surface level for P %g"), P), Column.MinSurfaceLevel,
                FMath::FloorToInt32(P) + Column.SurfaceDepth - 8);
        }

        // G1 to G3: the vertical gradient.
        FSurfaceTestCheck Probe;
        Probe.Condition = Gradient;
        bool bAlwaysTrue = true, bAlwaysFalse = true;
        for (int32 Y = 0; Y < 10; ++Y)
            for (int32 X = 0; X < 10; ++X)
            {
                Probe.X = X;
                Probe.Y = Y;
                Probe.Z = 0;
                bAlwaysTrue &= SurfaceTestHolds(Evaluator, Probe, NoBiomes);
                Probe.Z = -30;
                bAlwaysTrue &= SurfaceTestHolds(Evaluator, Probe, NoBiomes);
                Probe.Z = 8;
                bAlwaysFalse &= !SurfaceTestHolds(Evaluator, Probe, NoBiomes);
                Probe.Z = 50;
                bAlwaysFalse &= !SurfaceTestHolds(Evaluator, Probe, NoBiomes);
            }
        Test.TestTrue(TEXT("G1: the gradient holds at and below its start"), bAlwaysTrue);
        Test.TestTrue(TEXT("G1: the gradient fails at and above its end"), bAlwaysFalse);
        const auto Pattern = [&](int32 Biome)
        {
            TArray<uint8> Result;
            Probe.Z = 4;
            Probe.Biome = Biome;
            for (int32 Y = 0; Y < 64; ++Y)
                for (int32 X = 0; X < 64; ++X)
                {
                    Probe.X = X;
                    Probe.Y = Y;
                    Result.Add(SurfaceTestHolds(Evaluator, Probe, NoBiomes));
                }
            return Result;
        };
        const TArray<uint8> First = Pattern(FVoxelSurfaceBlock::UnknownBiome);
        int32 Holding = 0;
        for (const uint8 Held : First)
            Holding += Held;
        const double Share = double(Holding) / double(First.Num());
        Test.TestTrue(FString::Printf(TEXT("G2: about half of 4096 columns hold at y 4 (%.4f)"), Share),
            FMath::Abs(Share - 0.5) <= 0.03);
        Test.TestTrue(
            TEXT("G3: the same positions give the same answers"), First == Pattern(FVoxelSurfaceBlock::UnknownBiome));
        Test.TestTrue(TEXT("G3: another biome gives the same answers"), First == Pattern(Swamp));

        // B1: the biome is read per block from the storage, one quart layer at a time.
        FVoxelBiomeStorage Storage;
        SurfaceTestFillStorage(Storage, Plains, 40, Swamp);
        FVoxelSurfaceChunk Direct, Shortcut;
        Direct.Biomes = &Storage;
        Evaluator.PrepareChunk(Shortcut, &Storage, true);
        for (const FVoxelSurfaceChunk* Chunk : {&Direct, &Shortcut})
        {
            FSurfaceTestCheck Low, High;
            Low.Condition = High.Condition = PlainsOrDesert;
            Low.Z = 0;
            High.Z = 200;
            Test.TestTrue(TEXT("B1: a low block of one column takes the lower biome"),
                SurfaceTestHolds(Evaluator, Low, *Chunk, 5, 5));
            Test.TestFalse(TEXT("B1: a high block of the same column takes the upper biome"),
                SurfaceTestHolds(Evaluator, High, *Chunk, 5, 5));
        }
    }

    // The overworld's named conditions, evaluated with plain values.
    void SurfaceTestNamed(FAutomationTestBase& Test, const FSurfaceTestWorld& World, const FVoxelBlockTable& Table)
    {
        FVoxelSurfaceEvaluator Evaluator;
        FString Error;
        const bool bCompiled = Evaluator.Compile(World.Surface, Table, World.Biomes, SurfaceTestSeed, &Error);
        if (!Test.TestTrue(FString::Printf(TEXT("overworld rules compile (%s)"), *Error), bCompiled)) return;
        const FVoxelSurfaceChunk NoBiomes;
        const auto Check = [&](const TCHAR* Name, FSurfaceTestCheck Case)
        {
            Case.Condition = World.Surface.Rules.FindCondition(Name);
            if (!Test.TestTrue(FString::Printf(TEXT("%s is a named condition"), Name), Case.Condition != INDEX_NONE))
                return;
            Test.TestEqual(FString::Printf(TEXT("%s: %s"), Name, *Case.Name),
                SurfaceTestHolds(Evaluator, Case, NoBiomes), Case.bExpected);
        };
        Check(TEXT("minecraft:on_floor"), {.Name = TEXT("depth 1"), .bExpected = true, .Up = 1});
        Check(TEXT("minecraft:on_floor"), {.Name = TEXT("depth 2"), .Up = 2});
        Check(TEXT("minecraft:under_floor"),
            {.Name = TEXT("sd 5 depth 6"), .bExpected = true, .Up = 6, .SurfaceDepth = 5});
        Check(TEXT("minecraft:under_floor"), {.Name = TEXT("sd 5 depth 7"), .Up = 7, .SurfaceDepth = 5});
        Check(TEXT("minecraft:deep_under_floor"),
            {.Name = TEXT("sd 4 depth 8"), .bExpected = true, .Up = 8, .SurfaceDepth = 4});
        Check(TEXT("minecraft:deep_under_floor"), {.Name = TEXT("sd 4 depth 9"), .Up = 9, .SurfaceDepth = 4});
        Check(TEXT("minecraft:very_deep_under_floor"),
            {.Name = TEXT("sd 5 depth 19"), .bExpected = true, .Up = 19, .SurfaceDepth = 5, .Secondary = -0.0907});
        Check(TEXT("minecraft:very_deep_under_floor"),
            {.Name = TEXT("sd 5 depth 20"), .Up = 20, .SurfaceDepth = 5, .Secondary = -0.0907});
        Check(TEXT("minecraft:on_ceiling"), {.Name = TEXT("depth 1"), .bExpected = true, .Down = 1});
        Check(TEXT("minecraft:on_ceiling"), {.Name = TEXT("depth 2"), .Down = 2});
        Check(TEXT("minecraft:not_underwater"), {.Name = TEXT("no water"), .bExpected = true, .Z = 60});
        Check(TEXT("minecraft:not_underwater"), {.Name = TEXT("one block of water"), .Z = 60, .Water = 62});
        Check(TEXT("minecraft:not_under_deep_water"),
            {.Name = TEXT("10 deep"), .bExpected = true, .Z = 60, .Water = 71, .SurfaceDepth = 4});
        Check(
            TEXT("minecraft:not_under_deep_water"), {.Name = TEXT("11 deep"), .Z = 60, .Water = 72, .SurfaceDepth = 4});
    }

    // Steep (facts 2.6) and the preliminary surface's lerp.
    void SurfaceTestSteep(FAutomationTestBase& Test)
    {
        constexpr int32 Size = 16;
        const auto Flat = [](int32 Height)
        {
            TArray<int32> Heights;
            Heights.Init(Height, Size * Size);
            return Heights;
        };
        const auto Steep = [](const TArray<int32>& Heights, int32 X, int32 Y)
        { return VoxelSurfaceContext::IsSteep(Heights, Size, X, Y); };
        const auto CountSteep = [&](const TArray<int32>& Heights)
        {
            int32 Count = 0;
            for (int32 Y = 0; Y < Size; ++Y)
                for (int32 X = 0; X < Size; ++X)
                    Count += Steep(Heights, X, Y);
            return Count;
        };

        TArray<int32> Grid = Flat(64);
        Grid[9 * Size + 8] = 68;
        Test.TestTrue(TEXT("T1: rising toward +Y by 4 is steep"), Steep(Grid, 8, 8));
        Grid = Flat(64);
        Grid[7 * Size + 8] = 68;
        Test.TestFalse(TEXT("T2: rising toward -Y by 4 is not steep"), Steep(Grid, 8, 8));
        Grid = Flat(64);
        Grid[8 * Size + 7] = 68;
        Test.TestTrue(TEXT("T3: rising toward -X by 4 is steep"), Steep(Grid, 8, 8));
        Grid = Flat(64);
        Grid[8 * Size + 9] = 68;
        Test.TestFalse(TEXT("T4: rising toward +X by 4 is not steep"), Steep(Grid, 8, 8));
        Grid = Flat(64);
        Grid[9 * Size + 8] = 67;
        Grid[8 * Size + 7] = 67;
        Test.TestFalse(TEXT("T5: a difference of 3 is not steep"), Steep(Grid, 8, 8));

        for (int32 Y = 0; Y < Size; ++Y)
            for (int32 X = 0; X < Size; ++X)
                Grid[Y * Size + X] = 64 + 2 * Y;
        Test.TestFalse(TEXT("T6: a plane rising 2 per row is not steep at the first row"), Steep(Grid, 5, 0));
        Test.TestTrue(TEXT("T6: it is steep inside the chunk"), Steep(Grid, 5, 8));
        Test.TestFalse(TEXT("T6: it is not steep at the last row"), Steep(Grid, 5, Size - 1));
        for (int32 Y = 0; Y < Size; ++Y)
            for (int32 X = 0; X < Size; ++X)
                Grid[Y * Size + X] = 64 + 4 * Y;
        Test.TestEqual(TEXT("T7: a plane rising 4 per row is steep everywhere"), CountSteep(Grid), Size * Size);

        for (int32 Y = 0; Y < Size; ++Y)
            for (int32 X = 0; X < Size; ++X)
                Grid[Y * Size + X] = 64 + 4 * X;
        Test.TestEqual(TEXT("a plane rising toward +X is never steep"), CountSteep(Grid), 0);
        for (int32 Y = 0; Y < Size; ++Y)
            for (int32 X = 0; X < Size; ++X)
                Grid[Y * Size + X] = 64 + 4 * (Size - 1 - X);
        Test.TestEqual(TEXT("a plane rising toward -X is steep everywhere"), CountSteep(Grid), Size * Size);
        Test.TestEqual(TEXT("T8: a flat sea is not steep"), CountSteep(Flat(62)), 0);

        Grid = Flat(64);
        for (int32 Y = 3; Y <= 12; ++Y)
            for (int32 X = 3; X <= 12; ++X)
                Grid[Y * Size + X] = 68;
        Test.TestEqual(TEXT("T9: the plateau has 39 steep columns"), CountSteep(Grid), 39);
        Test.TestTrue(TEXT("T9: the row before the plateau is steep"), Steep(Grid, 5, 2));
        Test.TestFalse(TEXT("T9: two rows before it is not"), Steep(Grid, 5, 1));
        Test.TestTrue(TEXT("T9: the column past its east edge is steep"), Steep(Grid, 13, 8));
        Test.TestFalse(TEXT("T9: two columns past it is not"), Steep(Grid, 14, 8));
        Test.TestFalse(TEXT("T9: the row past its south edge is not"), Steep(Grid, 5, 13));

        Test.TestEqual(
            TEXT("lerp at the minimum corner"), VoxelSurfaceContext::LerpPreliminary(0, 0, 3, 16, 32, 48), 3.);
        Test.TestEqual(TEXT("lerp at the middle"), VoxelSurfaceContext::LerpPreliminary(.5, .5, 0, 16, 32, 48), 24.);
    }

    // Fills dense columns with Stone up to and including StoneTop.
    TArray<uint16> SurfaceTestDense(uint16 Stone, int32 StoneTop)
    {
        TArray<uint16> Dense;
        Dense.Init(0, FVoxelColumn::DenseVolume);
        for (int32 Z = VoxelConstants::MinZ; Z <= StoneTop; ++Z)
            for (int32 Y = 0; Y < 16; ++Y)
                for (int32 X = 0; X < 16; ++X)
                    Dense[FVoxelColumn::GetDenseIndex(X, Y, Z)] = Stone;
        return Dense;
    }

    void SurfaceTestSetLayer(TArray<uint16>& Dense, int32 Z, uint16 State)
    {
        for (int32 Y = 0; Y < 16; ++Y)
            for (int32 X = 0; X < 16; ++X)
                Dense[FVoxelColumn::GetDenseIndex(X, Y, Z)] = State;
    }

    int32 SurfaceTestCountLayer(const TArray<uint16>& Dense, int32 Z, uint16 State)
    {
        int32 Count = 0;
        for (int32 Y = 0; Y < 16; ++Y)
            for (int32 X = 0; X < 16; ++X)
                Count += Dense[FVoxelColumn::GetDenseIndex(X, Y, Z)] == State;
        return Count;
    }

    // Expands "sand*6 sandstone*13 stone" into state ids; an unknown block gives 0.
    TArray<uint16> SurfaceTestExpand(const FVoxelBlockTable& Table, const FString& Text, bool& bAllKnown)
    {
        TArray<uint16> States;
        TArray<FString> Tokens;
        Text.ParseIntoArray(Tokens, TEXT(" "));
        for (const FString& Token : Tokens)
        {
            FString Name = Token, CountText;
            int32 Count = 1;
            if (Token.Split(TEXT("*"), &Name, &CountText)) Count = FCString::Atoi(*CountText);
            const uint16 State = Table.FindState(FName(*Name));
            bAllKnown &= State != 0;
            for (int32 Index = 0; Index < Count; ++Index)
                States.Add(State);
        }
        return States;
    }

    struct FSurfaceTestGolden
    {
        const TCHAR* Biome;
        const TCHAR* Land[2];
        const TCHAR* Shore[2];
        const TCHAR* Sea[2];
    };

    // The jar's results (facts 2.8, golden_columns.json) from the first non-air block down; column 0 then column 9.
    const FSurfaceTestGolden SurfaceTestGoldens[] = {
        {TEXT("minecraft:plains"), {TEXT("grass_block dirt*5 stone*35"), TEXT("grass_block dirt*4 stone*36")},
            {TEXT("grass_block dirt*5 stone*35"), TEXT("grass_block dirt*4 stone*36")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:desert"), {TEXT("sand*6 sandstone*13 stone*22"), TEXT("sand*5 sandstone*16 stone*20")},
            {TEXT("sand*6 sandstone*13 stone*22"), TEXT("sand*5 sandstone*16 stone*20")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:beach"), {TEXT("sand*6 sandstone*2 stone*33"), TEXT("sand*5 sandstone*3 stone*33")},
            {TEXT("sand*6 sandstone*2 stone*33"), TEXT("sand*5 sandstone*3 stone*33")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:warm_ocean"), {TEXT("sand*6 sandstone*2 stone*33"), TEXT("sand*5 sandstone*3 stone*33")},
            {TEXT("sand*6 sandstone*2 stone*33"), TEXT("sand*5 sandstone*3 stone*33")},
            {TEXT("water*12 sand stone*28"), TEXT("water*12 sand stone*28")}},
        {TEXT("minecraft:frozen_peaks"), {TEXT("snow_block packed_ice*5 stone*35"), TEXT("packed_ice*5 stone*36")},
            {TEXT("snow_block packed_ice*5 stone*35"), TEXT("packed_ice*5 stone*36")},
            {TEXT("water*12 stone*29"), TEXT("water*12 stone*29")}},
        {TEXT("minecraft:jagged_peaks"), {TEXT("snow_block stone*40"), TEXT("snow_block stone*40")},
            {TEXT("snow_block stone*40"), TEXT("snow_block stone*40")},
            {TEXT("water*12 stone*29"), TEXT("water*12 stone*29")}},
        {TEXT("minecraft:snowy_slopes"), {TEXT("snow_block*6 stone*35"), TEXT("snow_block*5 stone*36")},
            {TEXT("snow_block*6 stone*35"), TEXT("snow_block*5 stone*36")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:grove"), {TEXT("snow_block dirt*5 stone*35"), TEXT("snow_block dirt*4 stone*36")},
            {TEXT("snow_block dirt*5 stone*35"), TEXT("snow_block dirt*4 stone*36")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:ice_spikes"), {TEXT("snow_block dirt*5 stone*35"), TEXT("snow_block dirt*4 stone*36")},
            {TEXT("snow_block dirt*5 stone*35"), TEXT("snow_block dirt*4 stone*36")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:swamp"), {TEXT("grass_block dirt*5 stone*35"), TEXT("grass_block dirt*4 stone*36")},
            {TEXT("grass_block dirt*5 stone*35"), TEXT("water dirt*4 stone*36")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:mangrove_swamp"), {TEXT("mud*6 stone*35"), TEXT("mud*5 stone*36")},
            {TEXT("mud*6 stone*35"), TEXT("water mud*4 stone*36")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:old_growth_pine_taiga"),
            {TEXT("coarse_dirt dirt*5 stone*35"), TEXT("coarse_dirt dirt*4 stone*36")},
            {TEXT("coarse_dirt dirt*5 stone*35"), TEXT("coarse_dirt dirt*4 stone*36")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:windswept_gravelly_hills"), {TEXT("gravel*6 stone*35"), TEXT("gravel*5 stone*36")},
            {TEXT("gravel*6 stone*35"), TEXT("gravel*5 stone*36")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:stony_shore"), {TEXT("stone*41"), TEXT("stone*41")}, {TEXT("stone*41"), TEXT("stone*41")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:stony_peaks"), {TEXT("stone*41"), TEXT("stone*41")}, {TEXT("stone*41"), TEXT("stone*41")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
        {TEXT("minecraft:windswept_hills"), {TEXT("stone*41"), TEXT("stone*41")}, {TEXT("stone*41"), TEXT("stone*41")},
            {TEXT("water*12 gravel stone*28"), TEXT("water*12 gravel stone*28")}},
    };

    // A column's noise values from facts 2.8 (golden_columns.json, inputs_seed0).
    struct FSurfaceTestInputs
    {
        int32 Depth;
        double Surface, Secondary, Swamp, PackedIce, Ice, PowderSnow, Calcite, Gravel, SmallPatch, Bands;
    };

    constexpr FSurfaceTestInputs SurfaceTestColumnInputs[2] = {
        {5, 0.788184, -0.09068188, -0.5108396, -0.15626045, -0.06538212, -0.07839361, -0.2416313, -0.15015224,
            -0.5854087, 0.10431531},
        {4, 0.5520025, 0.09072266, 0.43054658, 0.02630872, 0.20394929, 0.06066466, -0.22099137, -0.17198029, 1.5446203,
            0.08992983},
    };
    constexpr int32 SurfaceTestColumnX[2] = {0, 9};

    // False for an id with no injected value. The sulfur caves' gradient noise is not in the facts and no case reads
    // it.
    bool SurfaceTestNoiseValue(const FSurfaceTestInputs& Inputs, const FString& Id, double& Out)
    {
        Out = 0;
        if (Id == TEXT("minecraft:surface"))
            Out = Inputs.Surface;
        else if (Id == TEXT("minecraft:surface_secondary"))
            Out = Inputs.Secondary;
        else if (Id == TEXT("minecraft:surface_swamp"))
            Out = Inputs.Swamp;
        else if (Id == TEXT("minecraft:packed_ice"))
            Out = Inputs.PackedIce;
        else if (Id == TEXT("minecraft:ice"))
            Out = Inputs.Ice;
        else if (Id == TEXT("minecraft:powder_snow"))
            Out = Inputs.PowderSnow;
        else if (Id == TEXT("minecraft:calcite"))
            Out = Inputs.Calcite;
        else if (Id == TEXT("minecraft:gravel"))
            Out = Inputs.Gravel;
        else if (Id == TEXT("minecraft:small_patch"))
            Out = Inputs.SmallPatch;
        else if (Id == TEXT("minecraft:clay_bands_offset"))
            Out = Inputs.Bands;
        else
            return Id == TEXT("minecraft:sulfur_cave_gradient");
        return true;
    }

    struct FSurfaceTestProfile
    {
        const TCHAR* Name;
        int32 SolidTop;
        // The top of the water over it, or INDEX_NONE.
        int32 WaterTop;
    };

    // One hand-made column of Profile in Biome through the whole tree; compares the top Expected blocks.
    void SurfaceTestRunColumn(FAutomationTestBase& Test, const FVoxelSurfaceEvaluator& Evaluator,
        const FVoxelSurfaceChunk& Chunk, const FVoxelBlockTable& Table, const FString& Label,
        const TArray<FString>& NoiseIds, const FSurfaceTestProfile& Profile, int32 Which, const FString& Expected,
        int32 MinSurfaceLevel = VoxelConstants::MinZ)
    {
        const FSurfaceTestInputs& Inputs = SurfaceTestColumnInputs[Which];
        TArray<FSurfaceTestRun> Runs = {{VoxelConstants::MinZ, Profile.SolidTop, EVoxelSurfaceKind::Solid}};
        const uint16 Stone = Table.FindState(TEXT("stone")), Water = Table.FindState(TEXT("water"));
        if (Profile.WaterTop != INDEX_NONE)
            Runs.Add({Profile.SolidTop + 1, Profile.WaterTop, EVoxelSurfaceKind::Water});
        const TArray<EVoxelSurfaceKind> Kinds = SurfaceTestKinds(Runs);
        TArray<uint16> States;
        States.Init(0, VoxelConstants::Height);
        for (int32 Index = 0; Index < States.Num(); ++Index)
            States[Index] = Kinds[Index] == EVoxelSurfaceKind::Solid ? Stone
                : Kinds[Index] == EVoxelSurfaceKind::Water           ? Water
                                                                     : uint16(0);

        FVoxelSurfaceColumn Column;
        Column.X = SurfaceTestColumnX[Which];
        Column.Y = 0;
        Column.SurfaceDepth = Inputs.Depth;
        Column.MinSurfaceLevel = MinSurfaceLevel;
        bool bInjected = true;
        for (const FString& Id : NoiseIds)
        {
            double Value = 0;
            bInjected &= SurfaceTestNoiseValue(Inputs, Id, Value);
            Column.SetNoise(Evaluator.FindNoiseSlot(Id), Value);
        }
        Test.TestTrue(TEXT("every noise the rules read has an injected value"), bInjected);
        Evaluator.ApplyColumn(Chunk, Column, Column.X, 0, Kinds, States);

        bool bKnown = true;
        const TArray<uint16> Wanted = SurfaceTestExpand(Table, Expected, bKnown);
        const int32 Top = FMath::Max(Profile.SolidTop, Profile.WaterTop);
        int32 Bad = 0, FirstBad = 0;
        for (int32 Index = Wanted.Num() - 1; Index >= 0; --Index)
            if (States[Top - Index - VoxelConstants::MinZ] != Wanted[Index])
            {
                ++Bad;
                FirstBad = Index;
            }
        Test.TestTrue(FString::Printf(TEXT("%s %s column %d: %d of %d blocks differ (first %d: wanted %d, got %d)"),
                          *Label, Profile.Name, Which, Bad, Wanted.Num(), FirstBad, Wanted[FirstBad],
                          States[Top - FirstBad - VoxelConstants::MinZ]),
            Bad == 0 && bKnown);
    }

    // Badlands: only the blocks above the bands, which are our own pattern.
    struct FSurfaceTestBadlands
    {
        const TCHAR* Biome;
        const TCHAR* Land;
        const TCHAR* Shore;
        const TCHAR* High;
    };

    void SurfaceTestFillAll(FVoxelBiomeStorage& Storage, int32 Biome)
    {
        SurfaceTestFillStorage(Storage, Biome);
    }

    // Climate (facts 3.1 to 3.3).
    void SurfaceTestClimate(FAutomationTestBase& Test, const FSurfaceTestWorld& World)
    {
        struct FRow
        {
            float Base;
            float Expected[5];
        };
        const FRow Rows[] = {
            {0.8f, {0.8f, 0.775f, 0.7f, 0.65f, 0.50125f}},
            {0.5f, {0.5f, 0.475f, 0.4f, 0.35f, 0.20125f}},
            {0.3f, {0.3f, 0.275f, 0.2f, 0.15f, 0.00125f}},
            {0.25f, {0.25f, 0.225f, 0.15f, 0.1f, -0.04875f}},
            {0.2f, {0.2f, 0.175f, 0.1f, 0.05f, -0.09875f}},
            {1.0f, {1.0f, 0.975f, 0.9f, 0.85f, 0.70125f}},
        };
        const int32 Heights[5] = {64, 100, 160, 200, 319};
        for (const FRow& Row : Rows)
            for (int32 Index = 0; Index < 5; ++Index)
            {
                const float Value = FVoxelBiomeClimate::AdjustForHeight(Row.Base, Heights[Index], 63, 0);
                Test.TestTrue(FString::Printf(TEXT("T %g at y %d is %g (got %g)"), Row.Base, Heights[Index],
                                  Row.Expected[Index], Value),
                    FMath::Abs(Value - Row.Expected[Index]) <= 1e-6f);
            }
        const float Pine = FVoxelBiomeClimate::AdjustForHeight(0.3f, 200, 63, 0);
        Test.TestTrue(TEXT("old growth pine taiga at y 200 is exactly 0.15"), Pine == 0.15f);
        Test.TestFalse(TEXT("0.15 is not cold enough to snow"), FVoxelBiomeClimate::IsColdEnoughToSnow(Pine));
        Test.TestFalse(TEXT("taiga at y 160 is not cold enough to snow"),
            FVoxelBiomeClimate::IsColdEnoughToSnow(FVoxelBiomeClimate::AdjustForHeight(0.25f, 160, 63, 0)));
        Test.TestTrue(TEXT("taiga at y 161 is cold enough to snow"),
            FVoxelBiomeClimate::IsColdEnoughToSnow(FVoxelBiomeClimate::AdjustForHeight(0.25f, 161, 63, 0)));
        Test.TestTrue(TEXT("a positive wobble lowers the temperature"),
            FVoxelBiomeClimate::AdjustForHeight(0.3f, 200, 63, 8) <
                FVoxelBiomeClimate::AdjustForHeight(0.3f, 200, 63, 0));
        for (const int32 Z : {-64, 64, 80})
            Test.TestTrue(FString::Printf(TEXT("y %d keeps the base temperature"), Z),
                FVoxelBiomeClimate::AdjustForHeight(0.3f, Z, 63, 7.f) == 0.3f);

        const FVoxelBiomeClimate Climate;
        const int32 Plains = World.Biomes.FindBiome(TEXT("minecraft:plains"));
        const int32 Frozen = World.Biomes.FindBiome(TEXT("minecraft:frozen_ocean"));
        const int32 DeepFrozen = World.Biomes.FindBiome(TEXT("minecraft:deep_frozen_ocean"));
        if (!Test.TestTrue(TEXT("plains and the frozen oceans are overworld biomes"),
                Plains != INDEX_NONE && Frozen != INDEX_NONE && DeepFrozen != INDEX_NONE))
            return;
        const FVoxelBiome& PlainsBiome = World.Biomes.Biomes[Plains];
        const FVoxelBiome& FrozenBiome = World.Biomes.Biomes[Frozen];
        const FVoxelBiome& DeepBiome = World.Biomes.Biomes[DeepFrozen];
        bool bWobbleInRange = true, bPlainsKept = true, bFrozenValues = true, bDeepValues = true;
        int32 Patches = 0;
        for (int32 J = 0; J < 64; ++J)
            for (int32 I = 0; I < 64; ++I)
            {
                const int32 X = I * 37 - 1000, Y = J * 37 - 1000;
                bWobbleInRange &= FMath::Abs(Climate.GetWobble(X, Y)) <= FVoxelBiomeClimate::WobbleBlocks;
                bPlainsKept &= Climate.GetBaseTemperature(PlainsBiome, X, Y) == PlainsBiome.Temperature &&
                    Climate.GetTemperature(PlainsBiome, X, Y, 64, 63) == PlainsBiome.Temperature;
                const float Cold = Climate.GetBaseTemperature(FrozenBiome, X, Y);
                const bool bPatch = FMath::Abs(Cold - 0.2f) < 1e-6f;
                Patches += bPatch;
                bFrozenValues &= bPatch || FMath::Abs(Cold) < 1e-6f;
                const float Deep = Climate.GetBaseTemperature(DeepBiome, X, Y);
                bDeepValues &= FMath::Abs(Deep - (bPatch ? 0.2f : 0.5f)) < 1e-6f;
            }
        Test.TestTrue(TEXT("the wobble stays within 8 blocks"), bWobbleInRange);
        Test.TestTrue(TEXT("plains keeps its temperature"), bPlainsKept);
        Test.TestTrue(TEXT("frozen_ocean is 0.0 or 0.2"), bFrozenValues);
        Test.TestTrue(TEXT("deep_frozen_ocean is 0.2 where frozen_ocean is, else 0.5"), bDeepValues);
        const double PatchShare = double(Patches) / 4096.;
        Test.TestTrue(FString::Printf(TEXT("frozen_ocean's open-water share is plausible (%.3f)"), PatchShare),
            PatchShare >= 0.3 && PatchShare <= 0.75);
    }

    // The freeze pass on hand-made columns (facts 4.3) and the snow line.
    void SurfaceTestFreeze(FAutomationTestBase& Test, const FSurfaceTestWorld& World, const FVoxelBlockTable& Table)
    {
        FVoxelFreezePass Pass;
        FString Error;
        const bool bInitialized = Pass.Initialize(Table, &Error);
        if (!Test.TestTrue(FString::Printf(TEXT("the freeze pass initializes (%s)"), *Error), bInitialized)) return;
        const FVoxelBiomeClimate Climate;
        const int32 Cold = World.Biomes.FindBiome(TEXT("minecraft:snowy_taiga"));
        const int32 Warm = World.Biomes.FindBiome(TEXT("minecraft:plains"));
        const int32 Taiga = World.Biomes.FindBiome(TEXT("minecraft:taiga"));
        if (!Test.TestTrue(TEXT("snowy_taiga, plains and taiga are overworld biomes"),
                Cold != INDEX_NONE && Warm != INDEX_NONE && Taiga != INDEX_NONE))
            return;
        Test.TestTrue(
            TEXT("taiga's temperature is 0.25"), FMath::IsNearlyEqual(World.Biomes.Biomes[Taiga].Temperature, 0.25f));
        FVoxelBiomeStorage ColdStorage, WarmStorage, TaigaStorage;
        SurfaceTestFillAll(ColdStorage, Cold);
        SurfaceTestFillAll(WarmStorage, Warm);
        SurfaceTestFillAll(TaigaStorage, Taiga);
        const auto Apply = [&](TArray<uint16>& Dense, const FVoxelBiomeStorage& Storage)
        { Pass.Apply(FIntPoint(0, 0), Dense, Storage, World.Biomes, Climate, 63); };

        const uint16 Stone = Table.FindState(TEXT("stone")), Grass = Table.FindState(TEXT("grass_block"));
        const uint16 Snow = Table.FindState(TEXT("snow")), Ice = Table.FindState(TEXT("ice"));
        const uint16 PackedIce = Table.FindState(TEXT("packed_ice")), Powder = Table.FindState(TEXT("powder_snow"));
        const uint16 Lava = Table.FindState(TEXT("lava")), Water = Table.FindState(TEXT("water"));
        const uint16 SnowyGrass = Table.GetSnowyState(Grass);
        Test.TestTrue(TEXT("grass has a snowy variant"), SnowyGrass != Grass);

        TArray<uint16> Dense = SurfaceTestDense(Stone, 69);
        SurfaceTestSetLayer(Dense, 70, Grass);
        const TArray<uint16> GrassScene = Dense;
        Apply(Dense, ColdStorage);
        Test.TestEqual(TEXT("cold: snow layer above the grass"), SurfaceTestCountLayer(Dense, 71, Snow), 256);
        Test.TestEqual(TEXT("cold: the grass turns snowy"), SurfaceTestCountLayer(Dense, 70, SnowyGrass), 256);
        Test.TestEqual(TEXT("cold: nothing above the snow layer"), SurfaceTestCountLayer(Dense, 72, 0), 256);
        Dense = GrassScene;
        Apply(Dense, WarmStorage);
        Test.TestTrue(TEXT("warm: grass is left as it is"), Dense == GrassScene);

        Dense = SurfaceTestDense(Stone, 55);
        for (int32 Z = 56; Z <= 62; ++Z)
            SurfaceTestSetLayer(Dense, Z, Water);
        const TArray<uint16> WaterScene = Dense;
        Apply(Dense, ColdStorage);
        Test.TestEqual(TEXT("cold: still water at the top turns to ice"), SurfaceTestCountLayer(Dense, 62, Ice), 256);
        Test.TestEqual(TEXT("cold: the water under the ice stays"), SurfaceTestCountLayer(Dense, 61, Water), 256);
        Test.TestEqual(TEXT("cold: nothing above the ice"), SurfaceTestCountLayer(Dense, 63, 0), 256);
        Dense = WaterScene;
        Apply(Dense, WarmStorage);
        Test.TestTrue(TEXT("warm: water is left as it is"), Dense == WaterScene);

        Dense = SurfaceTestDense(Stone, 55);
        for (int32 Z = 56; Z <= 62; ++Z)
            SurfaceTestSetLayer(Dense, Z, Lava);
        const TArray<uint16> LavaScene = Dense;
        Apply(Dense, ColdStorage);
        Test.TestTrue(TEXT("cold: lava is left as it is"), Dense == LavaScene);

        const TPair<const TCHAR*, uint16> NoSnow[] = {
            {TEXT("ice"), Ice}, {TEXT("packed_ice"), PackedIce}, {TEXT("powder_snow"), Powder}};
        for (const TPair<const TCHAR*, uint16>& Top : NoSnow)
        {
            Dense = SurfaceTestDense(Stone, 69);
            SurfaceTestSetLayer(Dense, 70, Top.Value);
            const TArray<uint16> Scene = Dense;
            Apply(Dense, ColdStorage);
            Test.TestTrue(FString::Printf(TEXT("cold: %s gets no snow"), Top.Key), Dense == Scene);
        }

        // The snow line in taiga (0.25): about y 161, moved by the wobble.
        Dense = SurfaceTestDense(Stone, 150);
        Apply(Dense, TaigaStorage);
        Test.TestEqual(TEXT("taiga: no snow on a top at 150"), SurfaceTestCountLayer(Dense, 151, Snow), 0);
        Dense = SurfaceTestDense(Stone, 175);
        Apply(Dense, TaigaStorage);
        Test.TestEqual(TEXT("taiga: snow on a top at 175"), SurfaceTestCountLayer(Dense, 176, Snow), 256);
    }

    bool SurfaceTestHasAny(const TArray<uint16>& Dense, const TArray<uint16>& States)
    {
        for (const uint16 State : Dense)
            if (States.Contains(State)) return true;
        return false;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurfaceConditionsTest, "Unnamed.Voxel.Surface.Conditions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSurfaceConditionsTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    FSurfaceTestWorld World;
    if (!SurfaceTestRead(*this, TEXT("overworld"), World)) return true;
    TestTrue(TEXT("gold_block exists"), Table->FindState(TEXT("gold_block")) != 0);
    SurfaceTestScans(*this, World, *Table);
    SurfaceTestHandBuilt(*this, World, *Table);
    SurfaceTestNamed(*this, World, *Table);
    SurfaceTestSteep(*this);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurfaceRulesTest, "Unnamed.Voxel.Surface.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSurfaceRulesTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    FSurfaceTestWorld World;
    if (!SurfaceTestRead(*this, TEXT("overworld"), World)) return true;
    FVoxelSurfaceEvaluator Evaluator;
    FString Error;
    const bool bCompiled = Evaluator.Compile(World.Surface, *Table, World.Biomes, SurfaceTestSeed, &Error);
    if (!TestTrue(FString::Printf(TEXT("overworld rules compile (%s)"), *Error), bCompiled)) return true;
    const TArray<FString> NoiseIds = FVoxelSurfaceEvaluator::GetNoiseIds(World.Surface.Rules);

    const FSurfaceTestProfile Land = {TEXT("land70"), 70, INDEX_NONE};
    const FSurfaceTestProfile High = {TEXT("high140"), 140, INDEX_NONE};
    const FSurfaceTestProfile Shore = {TEXT("shore62"), 62, INDEX_NONE};
    const FSurfaceTestProfile Sea = {TEXT("sea floor 50, water to 62"), 50, 62};

    // Every case with the shortcut on and off.
    int32 ShortcutBlocks = 0, DirectBlocks = 0;
    const auto RunBiome = [&](const TCHAR* BiomeName, const auto& Cases)
    {
        const int32 Biome = World.Biomes.FindBiome(BiomeName);
        if (!TestTrue(FString::Printf(TEXT("%s is an overworld biome"), BiomeName), Biome != INDEX_NONE)) return;
        FVoxelBiomeStorage Storage;
        SurfaceTestFillAll(Storage, Biome);
        for (const bool bShortcut : {true, false})
        {
            FVoxelSurfaceChunk Chunk;
            Evaluator.PrepareChunk(Chunk, &Storage, bShortcut);
            const FString Label =
                FString::Printf(TEXT("%s%s"), BiomeName, bShortcut ? TEXT("") : TEXT(" (no shortcut)"));
            Cases(Chunk, Label);
            (bShortcut ? ShortcutBlocks : DirectBlocks) += Chunk.InteriorBlocks + Chunk.DeepBlocks;
        }
    };

    for (const FSurfaceTestGolden& Golden : SurfaceTestGoldens)
    {
        RunBiome(Golden.Biome,
            [&](const FVoxelSurfaceChunk& Chunk, const FString& Label)
            {
                for (int32 Which = 0; Which < 2; ++Which)
                {
                    SurfaceTestRunColumn(
                        *this, Evaluator, Chunk, *Table, Label, NoiseIds, Land, Which, Golden.Land[Which]);
                    SurfaceTestRunColumn(
                        *this, Evaluator, Chunk, *Table, Label, NoiseIds, High, Which, Golden.Land[Which]);
                    SurfaceTestRunColumn(
                        *this, Evaluator, Chunk, *Table, Label, NoiseIds, Shore, Which, Golden.Shore[Which]);
                    SurfaceTestRunColumn(
                        *this, Evaluator, Chunk, *Table, Label, NoiseIds, Sea, Which, Golden.Sea[Which]);
                    // A realistic minimum surface level, so the deep table decides the blocks below it.
                    const int32 DeepBefore = Chunk.DeepBlocks;
                    SurfaceTestRunColumn(
                        *this, Evaluator, Chunk, *Table, Label, NoiseIds, Land, Which, Golden.Land[Which], 37);
                    if (Chunk.bShortcut)
                        TestTrue(FString::Printf(TEXT("%s: the deep table decided blocks"), *Label),
                            Chunk.DeepBlocks > DeepBefore);
                    else
                        TestEqual(FString::Printf(TEXT("%s: no deep table without the shortcut"), *Label),
                            Chunk.DeepBlocks, DeepBefore);
                }
            });
    }

    const FSurfaceTestBadlands Badlands[] = {
        {TEXT("minecraft:badlands"), TEXT("red_sand orange_terracotta*7"), TEXT("red_sand"), TEXT("terracotta")},
        {TEXT("minecraft:wooded_badlands"), TEXT("red_sand orange_terracotta*7"), TEXT("red_sand"),
            TEXT("coarse_dirt")},
    };
    for (const FSurfaceTestBadlands& Case : Badlands)
    {
        RunBiome(Case.Biome,
            [&](const FVoxelSurfaceChunk& Chunk, const FString& Label)
            {
                for (int32 Which = 0; Which < 2; ++Which)
                {
                    SurfaceTestRunColumn(*this, Evaluator, Chunk, *Table, Label, NoiseIds, Land, Which, Case.Land);
                    SurfaceTestRunColumn(*this, Evaluator, Chunk, *Table, Label, NoiseIds, Shore, Which, Case.Shore);
                    SurfaceTestRunColumn(*this, Evaluator, Chunk, *Table, Label, NoiseIds, High, Which, Case.High);
                }
            });
    }
    TestTrue(TEXT("the shortcut decides blocks from its tables"), ShortcutBlocks > 0);
    TestEqual(TEXT("without the shortcut no block comes from a table"), DirectBlocks, 0);
    // Skipped: eroded_badlands, the badlands band layers below the first blocks and the sea floor's orange terracotta
    // (they come from our own band pattern), and sulfur caves (the jar's gradient noise is not in the facts).
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurfaceSnowTest, "Unnamed.Voxel.Surface.Snow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSurfaceSnowTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    FSurfaceTestWorld World;
    if (!SurfaceTestRead(*this, TEXT("overworld"), World)) return true;
    SurfaceTestClimate(*this, World);
    SurfaceTestFreeze(*this, World, *Table);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurfaceExactTest, "Unnamed.Voxel.Surface.Exact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSurfaceExactTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    TArray<uint16> SurfaceBlocks;
    for (const TCHAR* Name : {TEXT("grass_block"), TEXT("sand"), TEXT("snow_block"), TEXT("terracotta"),
             TEXT("red_sand"), TEXT("gravel"), TEXT("podzol"), TEXT("mud"), TEXT("coarse_dirt")})
        SurfaceBlocks.Add(Table->FindState(Name));
    const FIntPoint Positions[] = {{0, 0}, {1, 0}, {-3, 7}, {100, -50}, {40, 40}, {-200, 13}};

    for (const TCHAR* PresetName : SurfaceTestPresets)
    {
        FSurfaceTestWorld World;
        if (!SurfaceTestRead(*this, PresetName, World)) continue;
        const FVoxelDensityTerrain Terrain(
            World.Preset, SurfaceTestSeed, *Table, FVoxelDensityTerrainSettings(), &World.Biomes, &World.Surface);
        if (!TestTrue(
                FString::Printf(TEXT("%s terrain is valid (%s)"), PresetName, *Terrain.GetError()), Terrain.IsValid()))
            continue;
        FVoxelDensityTerrainOverrides NoShortcut, NoRules;
        NoShortcut.bSurfaceShortcut = false;
        NoRules.bSurfaceRules = false;
        int32 ShortcutDifferences = 0, RuleDifferences = 0;
        bool bDressed = true;
        int32 ShortcutTable = 0, DirectTable = 0, BedrockWrong = 0, HighBedrock = 0, Solid30 = 0, Deepslate30 = 0;
        const uint16 Bedrock = Table->FindState(TEXT("bedrock")), Deepslate = Table->FindState(TEXT("deepslate"));
        // Sulfur caves dress their stone with bands of their own, so their cells are left out of the deepslate share.
        const int32 SulfurCaves = World.Biomes.FindBiome(TEXT("minecraft:sulfur_caves"));
        const auto IsSolid = [&](uint16 State) { return Table->Get(State).bSolid; };
        for (const FIntPoint& Position : Positions)
        {
            FVoxelColumn ColumnA, ColumnB, ColumnC;
            TArray<uint16> DenseA, DenseB, DenseC;
            FVoxelDensityTerrainTimings TimingsA, TimingsB;
            Terrain.Generate(Position, ColumnA, DenseA, &TimingsA);
            Terrain.Generate(Position, ColumnB, DenseB, &TimingsB, NoShortcut);
            ShortcutTable += TimingsA.SurfaceDeep + TimingsA.SurfaceInterior;
            DirectTable += TimingsB.SurfaceDeep + TimingsB.SurfaceInterior;
            for (int32 Cell = 0; Cell < FVoxelColumn::Area; ++Cell)
            {
                const int32 X = Cell % 16, Y = Cell / 16;
                const uint16 Bottom = DenseA[FVoxelColumn::GetDenseIndex(X, Y, VoxelConstants::MinZ)];
                BedrockWrong += IsSolid(Bottom) && Bottom != Bedrock;
                const uint16 Deep30 = DenseA[FVoxelColumn::GetDenseIndex(X, Y, -30)];
                const bool bSulfur = ColumnA.GetBiome(X, Y, -30) == SulfurCaves;
                Solid30 += IsSolid(Deep30) && !bSulfur;
                Deepslate30 += Deep30 == Deepslate && !bSulfur;
                for (int32 Z = -58; Z <= VoxelConstants::MaxZ; ++Z)
                    HighBedrock += DenseA[FVoxelColumn::GetDenseIndex(X, Y, Z)] == Bedrock;
            }
            Terrain.Generate(Position, ColumnC, DenseC, nullptr, NoRules);
            for (int32 Index = 0; Index < DenseA.Num(); ++Index)
            {
                ShortcutDifferences += DenseA[Index] != DenseB[Index];
                RuleDifferences += DenseA[Index] != DenseC[Index];
            }
            bDressed &= SurfaceTestHasAny(DenseA, SurfaceBlocks);
        }
        TestEqual(FString::Printf(TEXT("%s: the shortcut changes no block"), PresetName), ShortcutDifferences, 0);
        TestTrue(FString::Printf(TEXT("%s: the rules change blocks (%d)"), PresetName, RuleDifferences),
            RuleDifferences > 0);
        TestTrue(FString::Printf(TEXT("%s: each column has at least one dressed surface block"), PresetName), bDressed);
        TestTrue(
            FString::Printf(TEXT("%s: the shortcut decides blocks from its tables (%d)"), PresetName, ShortcutTable),
            ShortcutTable > 0);
        TestEqual(
            FString::Printf(TEXT("%s: no table decides blocks without the shortcut"), PresetName), DirectTable, 0);
        TestTrue(TEXT("bedrock and deepslate exist"), Bedrock != 0 && Deepslate != 0);
        TestEqual(FString::Printf(TEXT("%s: every solid block at the world's bottom is bedrock"), PresetName),
            BedrockWrong, 0);
        TestEqual(FString::Printf(TEXT("%s: no bedrock above z -59"), PresetName), HighBedrock, 0);
        TestTrue(FString::Printf(TEXT("%s: at least 90%% of the solid blocks at z -30 are deepslate (%d of %d)"),
                     PresetName, Deepslate30, Solid30),
            Deepslate30 * 10 >= Solid30 * 9);
    }
    return true;
}

namespace
{
    // Vein 0 is copper, vein 1 iron (the data's types); Kind 1 filler, 2 ore, 3 raw ore.
    struct FSurfaceVeinBlockClass
    {
        const TCHAR* Name;
        int32 Type;
        int32 Kind;
    };

    constexpr FSurfaceVeinBlockClass SurfaceVeinBlockClasses[] = {{TEXT("minecraft:granite"), 0, 1},
        {TEXT("minecraft:copper_ore"), 0, 2}, {TEXT("minecraft:raw_copper_block"), 0, 3},
        {TEXT("minecraft:tuff"), 1, 1}, {TEXT("minecraft:deepslate_iron_ore"), 1, 2},
        {TEXT("minecraft:raw_iron_block"), 1, 3}};

    // The probe's own cache/uncached difference is up to 2.4e-7.
    constexpr float SurfaceVeinTolerance = 2.4e-7f;

    // One recorded position, in the reference's axes (y up).
    struct FSurfaceVeinSample
    {
        int32 X = 0, Y = 0, Z = 0, Block = 0;
        float Toggle = 0, Mask = 0, Density[2] = {}, Richness = 0, Gap = 0;
    };

    struct FSurfaceVeinSeed
    {
        int64 Seed = 0;
        // Per vein type: vein block and density > 0, no vein block and density > 0, vein block and density <= 0.
        int64 Agreement[2][3] = {};
        TArray<FSurfaceVeinSample> Samples;
    };

    struct FSurfaceVeinShare
    {
        int64 Hits = 0, Total = 0;
        double Sum = 0;
    };

    bool SurfaceVeinLoad(FAutomationTestBase& Test, TArray<FSurfaceVeinSeed>& OutSeeds, TArray<FString>& OutBlocks)
    {
        FString Text;
        TSharedPtr<FJsonObject> Root;
        if (!FFileHelper::LoadFileToString(Text,
                *FPaths::Combine(VoxelWorldgen::GetReferenceRoot(), TEXT("facts"), TEXT("ore_vein_samples.json"))) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            Test.AddError(TEXT("facts/ore_vein_samples.json cannot be read"));
            return false;
        }
        for (const TSharedPtr<FJsonValue>& Name : Root->GetArrayField(TEXT("blocks")))
            OutBlocks.Add(Name->AsString());
        const TCHAR* const TypeNames[2] = {TEXT("copper"), TEXT("iron")};
        for (const TSharedPtr<FJsonValue>& SeedValue : Root->GetArrayField(TEXT("seeds")))
        {
            const TSharedPtr<FJsonObject> SeedObject = SeedValue->AsObject();
            FSurfaceVeinSeed& Out = OutSeeds.AddDefaulted_GetRef();
            Out.Seed = int64(SeedObject->GetNumberField(TEXT("seed")));
            const TSharedPtr<FJsonObject> Agreement = SeedObject->GetObjectField(TEXT("agreement"));
            for (int32 Type = 0; Type < 2; ++Type)
            {
                const TArray<TSharedPtr<FJsonValue>>& Counts = Agreement->GetArrayField(TypeNames[Type]);
                for (int32 Index = 0; Index < 3; ++Index)
                    Out.Agreement[Type][Index] = int64(Counts[Index]->AsNumber());
            }
            const TArray<TSharedPtr<FJsonValue>>& Rows = SeedObject->GetArrayField(TEXT("samples"));
            Out.Samples.Reserve(Rows.Num());
            for (const TSharedPtr<FJsonValue>& RowValue : Rows)
            {
                const TArray<TSharedPtr<FJsonValue>>& Row = RowValue->AsArray();
                if (Row.Num() < 10) continue;
                FSurfaceVeinSample& Sample = Out.Samples.AddDefaulted_GetRef();
                Sample.X = int32(Row[0]->AsNumber());
                Sample.Y = int32(Row[1]->AsNumber());
                Sample.Z = int32(Row[2]->AsNumber());
                Sample.Block = int32(Row[3]->AsNumber());
                Sample.Toggle = float(Row[4]->AsNumber());
                Sample.Mask = float(Row[5]->AsNumber());
                Sample.Density[0] = float(Row[6]->AsNumber());
                Sample.Density[1] = float(Row[7]->AsNumber());
                Sample.Richness = float(Row[8]->AsNumber());
                Sample.Gap = float(Row[9]->AsNumber());
            }
        }
        return !OutSeeds.IsEmpty();
    }

    bool SurfaceVeinDiffers(float Value, float Recorded)
    {
        return !(FMath::Abs(Value - Recorded) <= SurfaceVeinTolerance);
    }

    // The compiled nodes reachable from Roots through what they read, in evaluation order, without passing through
    // Stops (or, with bStopInterpolated, through interpolated nodes); the stops reached are returned apart.
    void SurfaceVeinCollect(const FVoxelDensityEvaluator& Evaluator, TConstArrayView<int32> Roots,
        TConstArrayView<int32> Stops, bool bStopInterpolated, TArray<int32>& OutList, TArray<int32>& OutStops)
    {
        const FVoxelDensityGraph& Graph = Evaluator.GetGraph();
        TSet<int32> Visited;
        TArray<int32> Pending, Reads;
        Pending.Append(Roots.GetData(), Roots.Num());
        while (!Pending.IsEmpty())
        {
            const int32 Node = Pending.Pop();
            if (Visited.Contains(Node)) continue;
            const bool bStop = Stops.Contains(Node) ||
                (bStopInterpolated && !Roots.Contains(Node) && Graph.Nodes[Node].Op == EVoxelDensityOp::Interpolated);
            if (bStop)
            {
                OutStops.AddUnique(Node);
                continue;
            }
            Visited.Add(Node);
            Reads.Reset();
            Evaluator.GetReads(Node, Reads);
            Pending.Append(Reads);
        }
        for (const int32 Node : Evaluator.GetOrder())
            if (Visited.Contains(Node)) OutList.Add(Node);
    }

    void SurfaceVeinCheckShare(FAutomationTestBase& Test, const TCHAR* Label, int64 Hits, int64 Total, double Expected)
    {
        if (!Test.TestTrue(FString::Printf(TEXT("%s: there are samples (%lld)"), Label, Total), Total > 0)) return;
        const double Observed = double(Hits) / double(Total);
        const double Bound = 4.0 * FMath::Sqrt(Expected * (1.0 - Expected) / double(Total));
        Test.AddInfo(FString::Printf(
            TEXT("%s: %lld of %lld = %.4f, expected %.4f, bound %.4f"), Label, Hits, Total, Observed, Expected, Bound));
        Test.TestTrue(FString::Printf(TEXT("%s: %.4f is within %.4f of %.4f"), Label, Observed, Bound, Expected),
            FMath::Abs(Observed - Expected) <= Bound);
    }

    struct FSurfaceVeinColumn
    {
        FIntPoint Position;
        int32 Blocks = 0;
        bool bEnd = false;
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurfaceOreVeinTest, "Unnamed.Voxel.Surface.OreVein",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// The ore_vein rule (phase 6, S4) against facts/ore_vein_samples.json, 26.3's own values at 5,575 positions: (a) our
// compiled density functions give the recorded densities and richness from the recorded toggle and mask, and the mask
// the recorded -1 where the toggle is small; (b) the recorded rule and ours never place a block where the density is
// not positive, or ore where the gap is; (c) the shares (fill, ore, raw ore) of the recorded blocks and of our draws
// sit within 4 standard deviations of the density, the richness and the raw ore chance; our own draws cannot match the
// jar's block for block; (d) over the columns around the origin, the vein shortcut and the surface shortcut change no
// block, and every vein block is its vein's, within its heights.
bool FVoxelSurfaceOreVeinTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    FSurfaceTestWorld World;
    if (!SurfaceTestRead(*this, TEXT("overworld"), World)) return true;
    TArray<FSurfaceVeinSeed> SeedData;
    TArray<FString> BlockNames;
    if (!SurfaceVeinLoad(*this, SeedData, BlockNames)) return true;
    // Per recorded block index: its vein type and kind, or -1.
    TArray<FIntPoint> BlockClasses;
    for (const FString& BlockName : BlockNames)
    {
        FIntPoint Class(-1, 0);
        for (const FSurfaceVeinBlockClass& Candidate : SurfaceVeinBlockClasses)
            if (BlockName == Candidate.Name) Class = FIntPoint(Candidate.Type, Candidate.Kind);
        BlockClasses.Add(Class);
    }

    int64 Samples = 0, DensityDiffers = 0, RichnessDiffers = 0, MaskBranch = 0, MaskDiffers = 0;
    int64 VeinBlocks = 0, VeinBlocksNoDensity = 0, OreWithGap = 0, OurBlockNoDensity = 0, OurOreWithGap = 0;
    FSurfaceVeinShare VanillaFill, VanillaOre, VanillaRaw, OurFill, OurOre, OurRaw;
    for (const FSurfaceVeinSeed& Data : SeedData)
    {
        FVoxelSurfaceEvaluator Rules;
        FString Error;
        if (!TestTrue(FString::Printf(TEXT("seed %lld: the rules compile (%s)"), Data.Seed, *Error),
                Rules.Compile(World.Surface, *Table, World.Biomes, uint64(Data.Seed), &Error)))
            continue;
        const FVoxelOreVeins& Veins = Rules.GetOreVeins();
        int32 VeinOf[2] = {INDEX_NONE, INDEX_NONE};
        for (int32 Index = 0; Index < Veins.Num(); ++Index)
        {
            if (Veins.GetVein(Index).Ore == Table->FindState(TEXT("copper_ore"))) VeinOf[0] = Index;
            if (Veins.GetVein(Index).Ore == Table->FindState(TEXT("deepslate_iron_ore"))) VeinOf[1] = Index;
        }
        if (!TestTrue(FString::Printf(TEXT("seed %lld: the copper and iron veins exist"), Data.Seed),
                VeinOf[0] != INDEX_NONE && VeinOf[1] != INDEX_NONE))
            continue;
        const FVoxelDensityEvaluator& Evaluator = Veins.GetEvaluator();
        const FVoxelDensityGraph& Graph = Evaluator.GetGraph();
        const int32 ToggleNode = Graph.FindFunction(TEXT("minecraft:overworld/ore_vein/toggle"));
        const int32 MaskNode = Graph.FindFunction(TEXT("minecraft:overworld/ore_vein/mask"));
        if (!TestTrue(TEXT("the toggle and mask functions exist"), ToggleNode != INDEX_NONE && MaskNode != INDEX_NONE))
            continue;
        const int32 DensityNodes[2] = {Veins.GetVein(VeinOf[0]).Density, Veins.GetVein(VeinOf[1]).Density};
        const int32 RichnessNodes[2] = {Veins.GetVein(VeinOf[0]).Richness, Veins.GetVein(VeinOf[1]).Richness};

        TArray<int32> DensityList, DensityStops, MaskList, MaskStops;
        const int32 DensityRoots[4] = {DensityNodes[0], DensityNodes[1], RichnessNodes[0], RichnessNodes[1]};
        const int32 DensityStopNodes[2] = {ToggleNode, MaskNode};
        const int32 MaskRoots[1] = {MaskNode};
        const int32 MaskStopNodes[1] = {ToggleNode};
        SurfaceVeinCollect(Evaluator, DensityRoots, DensityStopNodes, false, DensityList, DensityStops);
        SurfaceVeinCollect(Evaluator, MaskRoots, MaskStopNodes, true, MaskList, MaskStops);
        FVoxelDensityScratch DensityScratch, MaskScratch;
        Evaluator.Prepare(DensityScratch, 1);
        Evaluator.Prepare(MaskScratch, 1);

        for (const FSurfaceVeinSample& Sample : Data.Samples)
        {
            ++Samples;
            // (a) our functions from the recorded toggle and mask (reference axes: x, y up, z).
            const double X = Sample.X, Y = Sample.Y, Z = Sample.Z;
            Evaluator.GetMutableValues(DensityScratch, ToggleNode)[0] = Sample.Toggle;
            Evaluator.GetMutableValues(DensityScratch, MaskNode)[0] = Sample.Mask;
            Evaluator.EvaluateNodes(DensityList, &X, &Y, &Z, DensityScratch);
            for (int32 Type = 0; Type < 2; ++Type)
            {
                DensityDiffers += SurfaceVeinDiffers(
                    Evaluator.GetValues(DensityScratch, DensityNodes[Type])[0], Sample.Density[Type]);
                RichnessDiffers +=
                    SurfaceVeinDiffers(Evaluator.GetValues(DensityScratch, RichnessNodes[Type])[0], Sample.Richness);
            }
            if (Sample.Toggle >= -0.4f && Sample.Toggle < 0.4f)
            {
                ++MaskBranch;
                Evaluator.GetMutableValues(MaskScratch, ToggleNode)[0] = Sample.Toggle;
                for (const int32 Stop : MaskStops)
                    if (Stop != ToggleNode && Evaluator.IsCompiledNode(Stop))
                        Evaluator.GetMutableValues(MaskScratch, Stop)[0] = 0.0f;
                Evaluator.EvaluateNodes(MaskList, &X, &Y, &Z, MaskScratch);
                MaskDiffers += Sample.Mask != -1.0f || Evaluator.GetValues(MaskScratch, MaskNode)[0] != -1.0f;
            }

            // (b) the recorded rule, then ours on the recorded values (our axes: X, Y horizontal, Z up).
            const FIntPoint Class =
                BlockClasses.IsValidIndex(Sample.Block) ? BlockClasses[Sample.Block] : FIntPoint(-1, 0);
            if (Class.X >= 0)
            {
                ++VeinBlocks;
                VeinBlocksNoDensity += !(Sample.Density[Class.X] > 0);
                OreWithGap += Class.Y >= 2 && Sample.Gap > 0;
                if (Sample.Gap <= 0)
                {
                    ++VanillaOre.Total;
                    VanillaOre.Hits += Class.Y >= 2;
                    VanillaOre.Sum += Sample.Richness;
                }
                if (Class.Y >= 2)
                {
                    ++VanillaRaw.Total;
                    VanillaRaw.Hits += Class.Y == 3;
                }
            }
            for (int32 Type = 0; Type < 2; ++Type)
            {
                const FVoxelOreVein& Vein = Veins.GetVein(VeinOf[Type]);
                const FVoxelOreVeins::EOutcome Outcome =
                    FVoxelOreVeins::ApplyGap(FVoxelOreVeins::Draw(Vein, uint64(Data.Seed), Sample.X, Sample.Z, Sample.Y,
                                                 Sample.Density[Type], Sample.Richness),
                        Sample.Gap);
                const bool bOre =
                    Outcome == FVoxelOreVeins::EOutcome::Ore || Outcome == FVoxelOreVeins::EOutcome::RawOre;
                OurBlockNoDensity += !(Sample.Density[Type] > 0) && Outcome != FVoxelOreVeins::EOutcome::Nothing;
                OurOreWithGap += Sample.Gap > 0 && bOre;
                if (!(Sample.Density[Type] > 0)) continue;
                // (c) our draws at the samples whose recorded density is positive.
                ++OurFill.Total;
                OurFill.Hits += Outcome != FVoxelOreVeins::EOutcome::Nothing;
                if (Outcome != FVoxelOreVeins::EOutcome::Nothing && Sample.Gap <= 0)
                {
                    ++OurOre.Total;
                    OurOre.Hits += bOre;
                    OurOre.Sum += Sample.Richness;
                }
                if (bOre)
                {
                    ++OurRaw.Total;
                    OurRaw.Hits += Outcome == FVoxelOreVeins::EOutcome::RawOre;
                }
            }
        }
        for (int32 Type = 0; Type < 2; ++Type)
        {
            VanillaFill.Hits += Data.Agreement[Type][0];
            VanillaFill.Total += Data.Agreement[Type][0] + Data.Agreement[Type][1];
        }
    }

    AddInfo(FString::Printf(TEXT("%lld samples, %lld vein blocks, %lld with the toggle in the mask's -1 branch"),
        Samples, VeinBlocks, MaskBranch));
    TestEqual(TEXT("recorded samples were read (5575)"), Samples, int64(5575));
    TestEqual(TEXT("our density functions match the recorded densities"), DensityDiffers, int64(0));
    TestEqual(TEXT("our richness matches the recorded richness"), RichnessDiffers, int64(0));
    TestTrue(TEXT("samples in the mask's -1 branch exist"), MaskBranch > 0);
    TestEqual(TEXT("our mask is -1 where the recorded mask is"), MaskDiffers, int64(0));
    TestTrue(TEXT("vein blocks were recorded"), VeinBlocks > 0);
    TestEqual(TEXT("no recorded vein block where the density is not positive"), VeinBlocksNoDensity, int64(0));
    TestEqual(TEXT("no recorded ore or raw ore where the gap is positive"), OreWithGap, int64(0));
    TestEqual(TEXT("our draws give no block where the density is not positive"), OurBlockNoDensity, int64(0));
    TestEqual(TEXT("our draws give no ore or raw ore where the gap is positive"), OurOreWithGap, int64(0));

    SurfaceVeinCheckShare(*this, TEXT("vanilla fill (density 0.7)"), VanillaFill.Hits, VanillaFill.Total, 0.7);
    SurfaceVeinCheckShare(*this, TEXT("vanilla ore share (mean richness)"), VanillaOre.Hits, VanillaOre.Total,
        VanillaOre.Total ? VanillaOre.Sum / double(VanillaOre.Total) : 0.5);
    SurfaceVeinCheckShare(*this, TEXT("vanilla raw share (0.02)"), VanillaRaw.Hits, VanillaRaw.Total, 0.02);
    SurfaceVeinCheckShare(*this, TEXT("our fill (density 0.7)"), OurFill.Hits, OurFill.Total, 0.7);
    SurfaceVeinCheckShare(*this, TEXT("our ore share (mean richness)"), OurOre.Hits, OurOre.Total,
        OurOre.Total ? OurOre.Sum / double(OurOre.Total) : 0.5);
    SurfaceVeinCheckShare(*this, TEXT("our raw share (0.02)"), OurRaw.Hits, OurRaw.Total, 0.02);

    // (d) columns that hold veins, with the real terrain.
    const FVoxelDensityTerrain Terrain(
        World.Preset, SurfaceTestSeed, *Table, FVoxelDensityTerrainSettings(), &World.Biomes, &World.Surface);
    if (!TestTrue(FString::Printf(TEXT("terrain is valid (%s)"), *Terrain.GetError()), Terrain.IsValid())) return true;
    const FVoxelOreVeins& TerrainVeins = Terrain.GetSurfaceRules().GetOreVeins();
    if (!TestTrue(TEXT("the terrain compiled its veins"), TerrainVeins.IsCompiled() && TerrainVeins.Num() > 0))
        return true;
    FVoxelDensityTerrainOverrides NoVeins, NoVeinShortcut, NoSurfaceShortcut;
    NoVeins.bOreVeins = false;
    NoVeinShortcut.bOreVeinShortcut = false;
    NoSurfaceShortcut.bSurfaceShortcut = false;

    constexpr int32 ScanRadius = 16;
    TArray<FSurfaceVeinColumn> Found;
    int64 WrongBlocks = 0, ScannedVeinBlocks = 0;
    for (int32 PositionY = -ScanRadius; PositionY <= ScanRadius; ++PositionY)
    {
        for (int32 PositionX = -ScanRadius; PositionX <= ScanRadius; ++PositionX)
        {
            FVoxelColumn ColumnOn, ColumnOff;
            TArray<uint16> DenseOn, DenseOff;
            Terrain.Generate(FIntPoint(PositionX, PositionY), ColumnOn, DenseOn, nullptr);
            Terrain.Generate(FIntPoint(PositionX, PositionY), ColumnOff, DenseOff, nullptr, NoVeins);
            if (DenseOn.Num() != DenseOff.Num())
            {
                AddError(TEXT("the dense arrays differ in size"));
                return true;
            }
            FSurfaceVeinColumn Column;
            Column.Position = FIntPoint(PositionX, PositionY);
            for (int32 Index = 0; Index < DenseOn.Num(); ++Index)
            {
                if (DenseOn[Index] == DenseOff[Index]) continue;
                ++Column.Blocks;
                const int32 BlockZ =
                    VoxelConstants::MinZ + Index / (VoxelConstants::ColumnSize * VoxelConstants::ColumnSize);
                bool bRight = false;
                for (int32 VeinIndex = 0; VeinIndex < TerrainVeins.Num(); ++VeinIndex)
                {
                    const FVoxelOreVein& Vein = TerrainVeins.GetVein(VeinIndex);
                    const uint16 State = DenseOn[Index];
                    if (State != Vein.Ore && State != Vein.RawOre && State != Vein.Filler) continue;
                    if (BlockZ >= Vein.MinZ && BlockZ <= Vein.MaxZ) bRight = true;
                    Column.bEnd |= BlockZ == Vein.MinZ || BlockZ == Vein.MaxZ;
                }
                WrongBlocks += !bRight;
            }
            ScannedVeinBlocks += Column.Blocks;
            if (Column.Blocks > 0) Found.Add(Column);
        }
    }
    AddInfo(FString::Printf(TEXT("%d of %d columns hold vein blocks (%lld in all)"), Found.Num(),
        (2 * ScanRadius + 1) * (2 * ScanRadius + 1), ScannedVeinBlocks));
    TestEqual(TEXT("every vein block is its vein's, within the vein's heights"), WrongBlocks, int64(0));
    TestTrue(TEXT("veins change blocks"), !Found.IsEmpty());

    TArray<FSurfaceVeinColumn> Picked;
    {
        TArray<FSurfaceVeinColumn> ByBlocks = Found;
        ByBlocks.Sort([](const FSurfaceVeinColumn& A, const FSurfaceVeinColumn& B) { return A.Blocks > B.Blocks; });
        for (int32 Index = 0; Index < FMath::Min(4, ByBlocks.Num()); ++Index)
            Picked.Add(ByBlocks[Index]);
        const FSurfaceVeinColumn* End = Found.FindByPredicate([](const FSurfaceVeinColumn& C) { return C.bEnd; });
        if (TestTrue(TEXT("a column holds a vein block at its vein's lowest or highest height"), End != nullptr))
            if (!Picked.ContainsByPredicate([End](const FSurfaceVeinColumn& C) { return C.Position == End->Position; }))
                Picked.Add(*End);
    }
    for (const FSurfaceVeinColumn& Column : Picked)
    {
        FVoxelColumn ColumnOn, ColumnDirect, ColumnTree;
        TArray<uint16> DenseOn, DenseDirect, DenseTree;
        Terrain.Generate(Column.Position, ColumnOn, DenseOn, nullptr);
        Terrain.Generate(Column.Position, ColumnDirect, DenseDirect, nullptr, NoVeinShortcut);
        Terrain.Generate(Column.Position, ColumnTree, DenseTree, nullptr, NoSurfaceShortcut);
        int32 DirectDiffers = 0, TreeDiffers = 0;
        for (int32 Index = 0; Index < DenseOn.Num(); ++Index)
        {
            DirectDiffers += DenseOn[Index] != DenseDirect[Index];
            TreeDiffers += DenseOn[Index] != DenseTree[Index];
        }
        AddInfo(FString::Printf(TEXT("column (%d, %d): %d vein blocks%s"), Column.Position.X, Column.Position.Y,
            Column.Blocks, Column.bEnd ? TEXT(", one at an end of its vein") : TEXT("")));
        TestEqual(FString::Printf(TEXT("column (%d, %d): the vein shortcut changes no block"), Column.Position.X,
                      Column.Position.Y),
            DirectDiffers, 0);
        TestEqual(FString::Printf(TEXT("column (%d, %d): the surface shortcut changes no block"), Column.Position.X,
                      Column.Position.Y),
            TreeDiffers, 0);
    }
    return true;
}

#endif
