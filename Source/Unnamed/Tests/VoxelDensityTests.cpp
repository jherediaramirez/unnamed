#include "Async/ParallelFor.h"
#include "Blocks/VoxelBlockTable.h"
#include "Density/VoxelColumnDensity.h"
#include "Density/VoxelDensityEvaluator.h"
#include "Density/VoxelDensityGraph.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr uint64 DensityTestSeed = 263;
    const TCHAR* const DensityTestPresets[3] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};

    // Recorded from the first run; never update to pass. A changed value means saved worlds changed.
    constexpr uint64 DensityTestGolden[3] = {0x800a889edf05371dull, 0xb2f78828e8b1de94ull, 0xe11d7425da82c0b7ull};

    const TCHAR* const DensityTestNoiseJson = TEXT("{'base_octave':-3,'base_amplitude':0.9381732587751005,'octave_"
                                                   "count':4,'amplitude_modifiers':[1.0,1.0,1.0,0.0]}");

    FString DensityTestQuote(const TCHAR* Json)
    {
        return FString(Json).Replace(TEXT("'"), TEXT("\""));
    }

    void DensityTestAddFunction(TMap<FString, FString>& Files, const FString& Name, const FString& Json)
    {
        Files.Add(FString::Printf(TEXT("density_function/test/%s.json"), *Name), DensityTestQuote(*Json));
    }

    bool DensityTestRead(const TMap<FString, FString>& Files, FVoxelDensityGraph& Graph, FString& OutError)
    {
        TArray<FString> Functions;
        for (const TPair<FString, FString>& Pair : Files)
        {
            if (Pair.Key.StartsWith(TEXT("density_function/test/")))
                Functions.Add(TEXT("minecraft:test/") + FPaths::GetBaseFilename(Pair.Key));
        }
        const TArray<FString> Noises = {TEXT("minecraft:test_noise")};
        return VoxelWorldgen::ReadReferences(
            [&Files](const FString& Path, FString& OutText)
            {
                if (const FString* Text = Files.Find(Path))
                {
                    OutText = *Text;
                    return true;
                }
                return false;
            },
            Functions, Noises, Graph, OutError);
    }

    bool DensityTestEvaluate(const FVoxelDensityGraph& Graph, const FString& Name, TConstArrayView<FVector3d> Points,
        TArray<float>& OutValues, FString& OutError)
    {
        const int32 Node = Graph.FindFunction(TEXT("minecraft:test/") + Name);
        if (Node == INDEX_NONE)
        {
            OutError = FString::Printf(TEXT("function %s missing"), *Name);
            return false;
        }
        FVoxelDensityEvaluator Evaluator;
        if (!Evaluator.Compile(Graph, DensityTestSeed, {Node}, &OutError)) return false;
        TArray<double> X, Y, Z;
        for (const FVector3d& Point : Points)
        {
            X.Add(Point.X);
            Y.Add(Point.Y);
            Z.Add(Point.Z);
        }
        FVoxelDensityScratch Scratch;
        Evaluator.Evaluate(X.GetData(), Y.GetData(), Z.GetData(), Points.Num(), Scratch);
        OutValues = TArray<float>(Evaluator.GetValues(Scratch, Node));
        return true;
    }

    bool DensityTestSame(float A, float B)
    {
        return FMemory::Memcmp(&A, &B, sizeof(float)) == 0;
    }

    bool DensityTestLoad(const TCHAR* Name, FVoxelTerrainPreset& Preset, FString& OutError)
    {
        return VoxelWorldgen::ReadPreset(VoxelWorldgen::GetReferenceRoot(), Name, Preset, OutError);
    }

    uint64 DensityTestHash(uint64 Hash, TConstArrayView<uint16> States)
    {
        constexpr uint64 Prime = 0x100000001b3ull;
        for (const uint16 State : States)
        {
            Hash ^= uint64(State & 0xff);
            Hash *= Prime;
            Hash ^= uint64(State >> 8);
            Hash *= Prime;
        }
        return Hash;
    }

    struct FDensityTestTopCase
    {
        const TCHAR* Density;
        double Upper, Lower;
        int32 Cell;
        float Expected;
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDensityOpsTest, "Unnamed.Voxel.Density.Ops",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDensityOpsTest::RunTest(const FString& Parameters)
{
    TMap<FString, FString> Files;
    Files.Add(TEXT("noise/test_noise.json"), DensityTestQuote(DensityTestNoiseJson));

    const TCHAR* const GradientJson =
        TEXT("{'type':'minecraft:gradient','axis':'y','from_coordinate':-64,'from_value':1.5,'to_coordinate':320,'to_"
             "value':-1.5}");
    DensityTestAddFunction(Files, TEXT("gradient"), GradientJson);

    // interval_select
    struct FIntervalCase
    {
        double Input;
        float Expected;
    };
    const FIntervalCase IntervalCases[] = {
        {-0.6, 1}, {-0.5, 2}, {-0.49, 2}, {0.0, 3}, {0.01, 3}, {0.5, 4}, {0.51, 4}, {9, 4}};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(IntervalCases); ++Index)
    {
        DensityTestAddFunction(Files, FString::Printf(TEXT("interval%d"), Index),
            FString::Printf(TEXT("{'type':'minecraft:interval_select','input':%g,'functions':[1.0,2.0,3.0,4.0],"
                                 "'thresholds':[-0.5,0.0,0.5]}"),
                IntervalCases[Index].Input));
    }

    // find_top_surface
    const TCHAR* const GradA =
        TEXT("{'type':'minecraft:gradient','axis':'y','from_coordinate':0,'from_value':60.5,'to_coordinate':121,'to_"
             "value':-60.5}");
    const TCHAR* const GradB =
        TEXT("{'type':'minecraft:gradient','axis':'y','from_coordinate':0,'from_value':64,'to_coordinate':128,'to_"
             "value':-64}");
    const FDensityTestTopCase TopCases[] = {
        {GradA, 200, -64, 8, 56},
        {GradA, 203, -64, 8, 56},
        {GradA, 200, -64, 1, 60},
        {GradA, 40, -64, 8, 40},
        {GradB, 200, -64, 8, 56},
        {TEXT("-1.0"), 200, -64, 8, -64},
        {TEXT("-1.0"), 200, -60, 8, -60},
        {TEXT("1.0"), 203, -64, 8, 200},
        {TEXT("1.0"), -5.5, -61, 8, -8},
        {TEXT("1.0"), 7.9, -64, 8, 0},
    };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(TopCases); ++Index)
    {
        const FDensityTestTopCase& Case = TopCases[Index];
        DensityTestAddFunction(Files, FString::Printf(TEXT("top%d"), Index),
            FString::Printf(TEXT("{'type':'minecraft:find_top_surface','density':%s,'upper_bound':%g,'lower_bound':%g,"
                                 "'cell_height':%d}"),
                Case.Density, Case.Upper, Case.Lower, Case.Cell));
    }

    // interpolated
    const TCHAR* const NoiseJson =
        TEXT("{'type':'minecraft:noise','noise':'minecraft:test_noise','xz_scale':40,'y_scale':40}");
    DensityTestAddFunction(Files, TEXT("noise_n"), NoiseJson);
    DensityTestAddFunction(Files, TEXT("interp"),
        FString::Printf(
            TEXT("{'type':'minecraft:interpolated','cell_size_xz':4,'cell_size_y':8,'input':%s}"), NoiseJson));

    FVoxelDensityGraph Graph;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("test graph reads (%s)"), *Error), DensityTestRead(Files, Graph, Error)))
        return false;

    TArray<float> Values;
    const auto Evaluate = [&](const FString& Name, TConstArrayView<FVector3d> Points) -> bool
    {
        FString EvaluateError;
        const bool bOk = DensityTestEvaluate(Graph, Name, Points, Values, EvaluateError);
        TestTrue(FString::Printf(TEXT("%s evaluates (%s)"), *Name, *EvaluateError), bOk);
        return bOk;
    };

    // gradient clamps beyond its ends
    if (Evaluate(TEXT("gradient"),
            {FVector3d(0, -200, 0), FVector3d(0, -64, 0), FVector3d(0, 320, 0), FVector3d(0, 500, 0)}))
    {
        const float Expected[4] = {1.5f, 1.5f, -1.5f, -1.5f};
        for (int32 Index = 0; Index < 4; ++Index)
            TestTrue(FString::Printf(TEXT("gradient clamp [%d] = %.9g"), Index, Values[Index]),
                DensityTestSame(Values[Index], Expected[Index]));
    }

    for (int32 Index = 0; Index < UE_ARRAY_COUNT(IntervalCases); ++Index)
    {
        if (Evaluate(FString::Printf(TEXT("interval%d"), Index), {FVector3d(0, 0, 0)}))
            TestTrue(FString::Printf(TEXT("interval_select %g = %g, expected %g"), IntervalCases[Index].Input,
                         Values[0], IntervalCases[Index].Expected),
                DensityTestSame(Values[0], IntervalCases[Index].Expected));
    }

    for (int32 Index = 0; Index < UE_ARRAY_COUNT(TopCases); ++Index)
    {
        const FDensityTestTopCase& Case = TopCases[Index];
        // The sample's y is ignored: use an unrelated one.
        if (Evaluate(FString::Printf(TEXT("top%d"), Index), {FVector3d(3, 17, -9)}))
            TestTrue(FString::Printf(TEXT("find_top_surface case %d (upper %g, lower %g, cell %d) = %g, expected %g"),
                         Index, Case.Upper, Case.Lower, Case.Cell, Values[0], Case.Expected),
                DensityTestSame(Values[0], Case.Expected));
    }

    // interpolated: exact at corners, trilinear inside
    TArray<float> Direct;
    const FVector3d CornerPoints[] = {
        FVector3d(0, 0, 0), FVector3d(4, 8, 4), FVector3d(-4, -8, -12), FVector3d(8, -16, 20), FVector3d(-8, 16, -4)};
    if (Evaluate(TEXT("noise_n"), CornerPoints))
    {
        Direct = Values;
        if (Evaluate(TEXT("interp"), CornerPoints))
        {
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(CornerPoints); ++Index)
                TestTrue(FString::Printf(
                             TEXT("interpolated at corner %d = %.9g, input %.9g"), Index, Values[Index], Direct[Index]),
                    DensityTestSame(Values[Index], Direct[Index]));
        }
    }
    const FVector3d InnerPoints[] = {FVector3d(1, 3, 2), FVector3d(5, 9, 7), FVector3d(-3, -5, -1)};
    TArray<float> InnerValues;
    if (Evaluate(TEXT("interp"), InnerPoints))
    {
        InnerValues = Values;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(InnerPoints); ++Index)
        {
            const FVector3d& Point = InnerPoints[Index];
            const double X0 = FMath::FloorToInt(Point.X / 4) * 4.0;
            const double Y0 = FMath::FloorToInt(Point.Y / 8) * 8.0;
            const double Z0 = FMath::FloorToInt(Point.Z / 4) * 4.0;
            TArray<FVector3d> Corners;
            for (int32 Corner = 0; Corner < 8; ++Corner)
                Corners.Add(
                    FVector3d(X0 + (Corner & 1) * 4, Y0 + ((Corner >> 1) & 1) * 8, Z0 + ((Corner >> 2) & 1) * 4));
            if (!Evaluate(TEXT("noise_n"), Corners)) continue;
            float CornerValues[8];
            for (int32 Corner = 0; Corner < 8; ++Corner)
                CornerValues[Corner] = Values[Corner];
            const float Expected = VoxelDensity::Trilinear(
                CornerValues, float((Point.X - X0) / 4), float((Point.Y - Y0) / 8), float((Point.Z - Z0) / 4));
            TestTrue(FString::Printf(TEXT("interpolated inside point %d = %.9g, trilinear %.9g"), Index,
                         InnerValues[Index], Expected),
                DensityTestSame(InnerValues[Index], Expected));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDensityColumnTest, "Unnamed.Voxel.Density.Column",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDensityColumnTest::RunTest(const FString& Parameters)
{
    const FIntPoint Origins[] = {{0, 0}, {-16, 32}, {4096, -2048}};
    for (const TCHAR* PresetName : DensityTestPresets)
    {
        FVoxelTerrainPreset Preset;
        FString Error;
        if (!TestTrue(
                FString::Printf(TEXT("%s reads (%s)"), PresetName, *Error), DensityTestLoad(PresetName, Preset, Error)))
            continue;
        FVoxelDensityGraph Graph = Preset.Graph;
        TestTrue(FString::Printf(TEXT("%s: RemoveCaves rewrites nodes"), PresetName),
            FVoxelDensityTerrain::RemoveCaves(Graph) > 0);
        const int32 Root = Preset.Router.FinalDensity;
        FVoxelDensityEvaluator Evaluator;
        if (!TestTrue(FString::Printf(TEXT("%s compiles (%s)"), PresetName, *Error),
                Evaluator.Compile(Graph, DensityTestSeed, {Root}, &Error)))
            continue;
        FVoxelColumnDensity Column;
        if (!TestTrue(FString::Printf(TEXT("%s column initializes (%s)"), PresetName, *Error),
                Column.Initialize(Evaluator, Root, Preset.MinY, Preset.Height, &Error)))
            continue;

        FVoxelColumnDensityScratch ColumnScratch;
        FVoxelDensityScratch PointScratch;
        int32 Mismatches = 0, DirectMismatches = 0, SkippedBlocks = 0, TotalBlocks = 0;
        for (const FIntPoint& Origin : Origins)
        {
            TArray<float> NoSkip, Skip;
            Column.Generate(Origin.X, Origin.Y, false, ColumnScratch, NoSkip);
            Column.Generate(Origin.X, Origin.Y, true, ColumnScratch, Skip);
            if (!TestEqual(TEXT("no-skip density size"), NoSkip.Num(), 16 * 16 * Preset.Height) ||
                !TestEqual(TEXT("skip density size"), Skip.Num(), NoSkip.Num()))
                continue;

            // The no-skip column against direct evaluation at 300 random blocks.
            FRandomStream Stream(1234);
            TArray<double> X, Y, Z;
            TArray<int32> Indices;
            for (int32 Pick = 0; Pick < 300; ++Pick)
            {
                const int32 LocalX = Stream.RandRange(0, 15);
                const int32 LocalZ = Stream.RandRange(0, 15);
                const int32 BlockY = Stream.RandRange(Preset.MinY, Preset.MinY + Preset.Height - 1);
                X.Add(Origin.X + LocalX);
                Y.Add(BlockY);
                Z.Add(Origin.Y + LocalZ);
                Indices.Add(((BlockY - Preset.MinY) * 16 + LocalZ) * 16 + LocalX);
            }
            Evaluator.Evaluate(X.GetData(), Y.GetData(), Z.GetData(), X.Num(), PointScratch);
            const TConstArrayView<float> Direct = Evaluator.GetValues(PointScratch, Root);
            for (int32 Pick = 0; Pick < Indices.Num(); ++Pick)
                DirectMismatches += !DensityTestSame(NoSkip[Indices[Pick]], Direct[Pick]);

            // Skipping keeps every evaluated block and replaces skipped cells with a certain sign.
            for (int32 Index = 0; Index < NoSkip.Num(); ++Index)
            {
                ++TotalBlocks;
                const float Exact = NoSkip[Index], Fast = Skip[Index];
                if (DensityTestSame(Fast, Exact)) continue;
                ++SkippedBlocks;
                const bool bOk = (Fast == 1.f && Exact > 0.f) || (Fast == -1.f && Exact <= 0.f);
                Mismatches += !bOk;
            }
        }
        TestEqual(
            FString::Printf(TEXT("%s: columns equal direct evaluation bit for bit"), PresetName), DirectMismatches, 0);
        TestEqual(FString::Printf(TEXT("%s: skipped cells keep the sign"), PresetName), Mismatches, 0);
        AddInfo(FString::Printf(TEXT("%s: %d of %d blocks skipped (%.1f%%)"), PresetName, SkippedBlocks, TotalBlocks,
            TotalBlocks ? 100.0 * SkippedBlocks / TotalBlocks : 0.0));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDensityTerrainTest, "Unnamed.Voxel.Density.Terrain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDensityTerrainTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    const uint16 Bedrock = Table->FindState(TEXT("bedrock"));
    const uint16 Water = Table->FindState(TEXT("water"));
    constexpr int32 TopWater = VoxelConstants::SeaLevel - 1;

    for (const TCHAR* PresetName : DensityTestPresets)
    {
        FVoxelTerrainPreset Preset;
        FString Error;
        if (!TestTrue(
                FString::Printf(TEXT("%s reads (%s)"), PresetName, *Error), DensityTestLoad(PresetName, Preset, Error)))
            continue;
        const FVoxelDensityTerrain Terrain(Preset, DensityTestSeed, *Table);
        if (!TestTrue(
                FString::Printf(TEXT("%s terrain is valid (%s)"), PresetName, *Terrain.GetError()), Terrain.IsValid()))
            continue;

        const auto GenerateDense = [&Terrain](FIntPoint Position, const bool* bSkip) -> TArray<uint16>
        {
            FVoxelColumn Column;
            TArray<uint16> Dense;
            FVoxelDensityTerrainTimings Timings;
            Terrain.Generate(Position, Column, Dense, &Timings, bSkip);
            return Dense;
        };

        // Skip on against skip off, 5 x 5 columns, and the structure of each.
        const bool bOn = true, bOff = false;
        int32 SkipDiffers = 0, BadBedrock = 0, BadWater = 0, BadAir = 0, BadSurface = 0, BadDry = 0, Checked = 0;
        for (int32 CY = -2; CY <= 2; ++CY)
        {
            for (int32 CX = -2; CX <= 2; ++CX)
            {
                const FIntPoint Position(CX, CY);
                const TArray<uint16> On = GenerateDense(Position, &bOn);
                const TArray<uint16> Off = GenerateDense(Position, &bOff);
                SkipDiffers += On != Off;
                if (On.Num() != FVoxelColumn::DenseVolume) continue;

                FRandomStream Stream(CX * 31 + CY * 17 + 5);
                for (int32 LocalY = 0; LocalY < 16; ++LocalY)
                {
                    for (int32 LocalX = 0; LocalX < 16; ++LocalX)
                    {
                        BadBedrock += On[FVoxelColumn::GetDenseIndex(LocalX, LocalY, VoxelConstants::MinZ)] != Bedrock;
                        int32 Top = VoxelConstants::MinZ - 1;
                        for (int32 Z = VoxelConstants::MaxZ; Z >= VoxelConstants::MinZ; --Z)
                        {
                            const uint16 State = On[FVoxelColumn::GetDenseIndex(LocalX, LocalY, Z)];
                            if (State != 0 && State != Water)
                            {
                                Top = Z;
                                break;
                            }
                        }
                        for (int32 Z = FMath::Max(Top, TopWater) + 1; Z <= VoxelConstants::MaxZ; ++Z)
                            BadAir += On[FVoxelColumn::GetDenseIndex(LocalX, LocalY, Z)] != 0;
                        for (int32 Z = TopWater + 1; Z <= VoxelConstants::MaxZ; ++Z)
                            BadWater += On[FVoxelColumn::GetDenseIndex(LocalX, LocalY, Z)] == Water;
                    }
                }
                for (int32 Pick = 0; Pick < 16; ++Pick)
                {
                    const int32 LocalX = Stream.RandRange(0, 15), LocalY = Stream.RandRange(0, 15);
                    int32 Top = VoxelConstants::MinZ - 1;
                    for (int32 Z = VoxelConstants::MaxZ; Z >= VoxelConstants::MinZ; --Z)
                    {
                        const uint16 State = On[FVoxelColumn::GetDenseIndex(LocalX, LocalY, Z)];
                        if (State != 0 && State != Water)
                        {
                            Top = Z;
                            break;
                        }
                    }
                    const int32 WorldX = CX * 16 + LocalX, WorldY = CY * 16 + LocalY;
                    const int32 Height = Terrain.GetSurfaceHeight(WorldX, WorldY);
                    BadSurface += Height != Top;
                    BadDry += Terrain.ConfirmDry(WorldX, WorldY) != (Height >= TopWater);
                    ++Checked;
                }
            }
        }
        TestEqual(FString::Printf(TEXT("%s: skip on equals skip off (columns differing)"), PresetName), SkipDiffers, 0);
        TestEqual(FString::Printf(TEXT("%s: bedrock floor (blocks wrong)"), PresetName), BadBedrock, 0);
        TestEqual(FString::Printf(TEXT("%s: no water above the sea (blocks)"), PresetName), BadWater, 0);
        TestEqual(FString::Printf(TEXT("%s: air above the top and the sea (blocks wrong)"), PresetName), BadAir, 0);
        TestEqual(FString::Printf(TEXT("%s: GetSurfaceHeight is the top solid block (of %d)"), PresetName, Checked),
            BadSurface, 0);
        TestEqual(FString::Printf(TEXT("%s: ConfirmDry follows the surface (of %d)"), PresetName, Checked), BadDry, 0);

        // Order and thread independence over a 3 x 3 area.
        constexpr int32 Count = 9;
        FIntPoint Positions[Count];
        for (int32 Index = 0; Index < Count; ++Index)
            Positions[Index] = FIntPoint(Index % 3 - 1, Index / 3 - 1);
        TArray<TArray<uint16>> Forward, Backward, Parallel;
        Forward.SetNum(Count);
        Backward.SetNum(Count);
        Parallel.SetNum(Count);
        for (int32 Index = 0; Index < Count; ++Index)
            Forward[Index] = GenerateDense(Positions[Index], nullptr);
        for (int32 Index = Count - 1; Index >= 0; --Index)
            Backward[Index] = GenerateDense(Positions[Index], nullptr);
        ParallelFor(Count, [&](int32 Job) { Parallel[Job] = GenerateDense(Positions[Job], nullptr); });
        int32 OrderDiffers = 0, ThreadDiffers = 0;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            OrderDiffers += Forward[Index] != Backward[Index];
            ThreadDiffers += Forward[Index] != Parallel[Index];
        }
        TestEqual(FString::Printf(TEXT("%s: reverse order gives the same columns"), PresetName), OrderDiffers, 0);
        TestEqual(FString::Printf(TEXT("%s: ParallelFor gives the same columns"), PresetName), ThreadDiffers, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDensityGoldenTest, "Unnamed.Voxel.Density.Golden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDensityGoldenTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    const TCHAR* const StateNames[] = {TEXT("stone"), TEXT("water"), TEXT("grass_block"), TEXT("sand"), TEXT("gravel"),
        TEXT("bedrock"), TEXT("deepslate")};
    const FIntPoint Positions[] = {{0, 0}, {1, 0}, {-3, 7}, {100, -50}};

    for (int32 PresetIndex = 0; PresetIndex < 3; ++PresetIndex)
    {
        const TCHAR* const PresetName = DensityTestPresets[PresetIndex];
        FVoxelTerrainPreset Preset;
        FString Error;
        if (!TestTrue(
                FString::Printf(TEXT("%s reads (%s)"), PresetName, *Error), DensityTestLoad(PresetName, Preset, Error)))
            continue;
        const FVoxelDensityTerrain Terrain(Preset, DensityTestSeed, *Table);
        if (!TestTrue(
                FString::Printf(TEXT("%s terrain is valid (%s)"), PresetName, *Terrain.GetError()), Terrain.IsValid()))
            continue;

        uint64 Hash = 0xcbf29ce484222325ull;
        TMap<uint16, int64> Counts;
        int64 Air = 0;
        for (const FIntPoint& Position : Positions)
        {
            FVoxelColumn Column;
            TArray<uint16> Dense;
            Terrain.Generate(Position, Column, Dense);
            Hash = DensityTestHash(Hash, Dense);
            for (const uint16 State : Dense)
            {
                if (State == 0)
                    ++Air;
                else
                    ++Counts.FindOrAdd(State);
            }
        }
        FString Info = FString::Printf(TEXT("%s: air %lld"), PresetName, Air);
        for (const TCHAR* StateName : StateNames)
            Info += FString::Printf(TEXT(", %s %lld"), StateName, Counts.FindRef(Table->FindState(StateName)));
        AddInfo(Info);

        if (Hash != DensityTestGolden[PresetIndex])
            AddError(FString::Printf(TEXT("%s: block hash 0x%016llx, golden 0x%016llx"), PresetName,
                static_cast<unsigned long long>(Hash),
                static_cast<unsigned long long>(DensityTestGolden[PresetIndex])));
    }
    return true;
}

#endif
