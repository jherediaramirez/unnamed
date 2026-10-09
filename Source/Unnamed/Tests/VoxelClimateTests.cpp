#include "Async/ParallelFor.h"
#include "Density/VoxelDensityEvaluator.h"
#include "Density/VoxelDensityGraph.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Noise/VoxelNormalNoise.h"
#include "Terrain/VoxelClimate.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // Recorded from the first run; never update to pass. A changed value means saved worlds changed.
    constexpr double ClimateTestNoiseGolden = -7.6433860408142209;
    constexpr double ClimateTestPresetGolden[3] = {2572.1885651089251, 4151.0642563095316, 1466.150923255831};

    constexpr uint64 ClimateTestSeed = 263;

    // The overworld's offset noise, so the test noise spreads like a real one.
    const TCHAR* const ClimateTestNoiseJson = TEXT("{'base_octave':-3,'base_amplitude':0.9381732587751005,'octave_"
                                                   "count':4,'amplitude_modifiers':[1.0,1.0,1.0,0.0]}");

    FString ClimateTestQuote(const TCHAR* Json)
    {
        // Single quotes keep the inline JSON readable in C++ source.
        return FString(Json).Replace(TEXT("'"), TEXT("\""));
    }

    // Inline function files, keyed by relative path under worldgen/, for ReadReferences.
    void ClimateTestAddFunction(TMap<FString, FString>& Files, const TCHAR* Name, const TCHAR* Json)
    {
        Files.Add(FString::Printf(TEXT("density_function/test/%s.json"), Name), ClimateTestQuote(Json));
    }

    FString ClimateTestFunctionId(const TCHAR* Name)
    {
        return FString(TEXT("minecraft:test/")) + Name;
    }

    // Reads every added function plus the test noise into Graph.
    bool ClimateTestRead(const TMap<FString, FString>& Files, FVoxelDensityGraph& Graph, FString& OutError)
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

    // Compiles one function and evaluates it at the given points (reference axes: x, y up, z).
    bool ClimateTestEvaluate(const FVoxelDensityGraph& Graph, const TCHAR* Name, TConstArrayView<FVector3d> Points,
        FVoxelDensityScratch& Scratch, TArray<float>& OutValues, FString* OutError = nullptr)
    {
        const int32 Node = Graph.FindFunction(ClimateTestFunctionId(Name));
        if (Node == INDEX_NONE)
        {
            if (OutError) *OutError = FString::Printf(TEXT("function %s missing"), Name);
            return false;
        }
        FVoxelDensityEvaluator Evaluator;
        if (!Evaluator.Compile(Graph, ClimateTestSeed, {Node}, OutError)) return false;
        TArray<double> X, Y, Z;
        for (const FVector3d& Point : Points)
        {
            X.Add(Point.X);
            Y.Add(Point.Y);
            Z.Add(Point.Z);
        }
        Evaluator.Evaluate(X.GetData(), Y.GetData(), Z.GetData(), Points.Num(), Scratch);
        OutValues = TArray<float>(Evaluator.GetValues(Scratch, Node));
        return true;
    }

    FVoxelDensityNoise ClimateTestMakeNoise(const TCHAR* Name, std::initializer_list<double> Modifiers)
    {
        FVoxelDensityNoise Noise;
        Noise.Name = Name;
        Noise.BaseOctave = -3;
        Noise.BaseAmplitude = 0.9381732587751005;
        Noise.AmplitudeModifiers = TArray<double>(Modifiers);
        return Noise;
    }

    bool ClimateTestSamplesEqual(const FVoxelClimateSample& A, const FVoxelClimateSample& B)
    {
        return FMemory::Memcmp(&A, &B, sizeof(FVoxelClimateSample)) == 0;
    }

    bool ClimateTestColumnsEqual(const FVoxelColumnClimate& A, const FVoxelColumnClimate& B)
    {
        // Seconds is a timing and differs between runs.
        return FMemory::Memcmp(A.Samples, B.Samples, sizeof(A.Samples)) == 0;
    }

    // Each parameter weighted by its index + 1, so a swap between parameters changes the checksum.
    double ClimateTestSum(const FVoxelColumnClimate& Column)
    {
        double Sum = 0;
        for (const FVoxelClimateSample& Sample : Column.Samples)
        {
            for (int32 Parameter = 0; Parameter < int32(EVoxelClimate::Count); ++Parameter)
                Sum += (Parameter + 1) * double(Sample.Values[Parameter]);
        }
        return Sum;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelClimateNormalNoiseTest, "Unnamed.Voxel.Climate.NormalNoise",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelClimateNormalNoiseTest::RunTest(const FString& Parameters)
{
    const FVoxelDensityNoise Description = ClimateTestMakeNoise(TEXT("minecraft:test_noise"), {1.0, 1.0, 1.0, 0.0});
    const FVoxelNormalNoise First(ClimateTestSeed, Description);
    const FVoxelNormalNoise Second(ClimateTestSeed, Description);
    const FVoxelNormalNoise OtherSeed(ClimateTestSeed + 1, Description);
    const FVoxelNormalNoise OtherName(
        ClimateTestSeed, ClimateTestMakeNoise(TEXT("minecraft:other_noise"), {1.0, 1.0, 1.0, 0.0}));

    bool bSame = true, bSeedDiffers = false, bNameDiffers = false;
    for (int32 Index = 0; Index < 64; ++Index)
    {
        const double X = Index * 13.7 - 400, Y = Index * 2.3 - 50, Z = 900 - Index * 29.1;
        const float A = First.Sample(X, Y, Z);
        bSame &= A == Second.Sample(X, Y, Z);
        bSeedDiffers |= A != OtherSeed.Sample(X, Y, Z);
        bNameDiffers |= A != OtherName.Sample(X, Y, Z);
    }
    TestTrue(TEXT("same seed and description sample bit-identically"), bSame);
    TestTrue(TEXT("a different seed changes values"), bSeedDiffers);
    TestTrue(TEXT("a different name changes values"), bNameDiffers);

    // Each non-zero modifier costs one octave in each of the two stacks.
    TestEqual(TEXT("octave count [1,0,2,0]"),
        FVoxelNormalNoise(ClimateTestSeed, ClimateTestMakeNoise(TEXT("n"), {1.0, 0.0, 2.0, 0.0})).GetOctaveCount(), 4);
    TestEqual(TEXT("octave count of the test noise"), First.GetOctaveCount(), 6);

    const FVoxelNormalNoise Silent(ClimateTestSeed, ClimateTestMakeNoise(TEXT("n"), {0.0, 0.0, 0.0}));
    bool bZero = true;
    for (int32 Index = 0; Index < 64; ++Index)
        bZero &= Silent.Sample(Index * 5.1, Index * 1.7, -Index * 9.3) == 0;
    TestTrue(TEXT("all-zero modifiers sample 0"), bZero);

    double Sum = 0;
    for (int32 X = 0; X < 8; ++X)
    {
        for (int32 Y = 0; Y < 8; ++Y)
        {
            for (int32 Z = 0; Z < 8; ++Z)
                Sum += First.Sample(X * 37.1, Y * 3.3, Z * 41.7);
        }
    }
    AddInfo(FString::Printf(TEXT("noise golden sum: %.17g"), Sum));
    TestTrue(
        TEXT("noise golden"), FMath::Abs(Sum - ClimateTestNoiseGolden) <= 1e-9 * FMath::Abs(ClimateTestNoiseGolden));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelClimateOpsTest, "Unnamed.Voxel.Climate.Ops",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelClimateOpsTest::RunTest(const FString& Parameters)
{
    const FString Noise = TEXT("'noise':'minecraft:test_noise'");
    TMap<FString, FString> Files;
    Files.Add(TEXT("noise/test_noise.json"), ClimateTestQuote(ClimateTestNoiseJson));
    ClimateTestAddFunction(
        Files, TEXT("lerp"), TEXT("{'type':'minecraft:lerp','alpha':0.25,'first':1.0,'second':3.0}"));
    ClimateTestAddFunction(Files, TEXT("squeeze"), TEXT("{'type':'minecraft:squeeze','input':2.0}"));
    ClimateTestAddFunction(Files, TEXT("squeeze_inside"), TEXT("{'type':'minecraft:squeeze','input':0.5}"));
    ClimateTestAddFunction(Files, TEXT("half_positive"), TEXT("{'type':'minecraft:half_negative','input':2.0}"));
    ClimateTestAddFunction(Files, TEXT("quarter_positive"), TEXT("{'type':'minecraft:quarter_negative','input':2.0}"));
    ClimateTestAddFunction(Files, TEXT("clamp"), TEXT("{'type':'minecraft:clamp','input':5.0,'min':-1.0,'max':2.0}"));
    ClimateTestAddFunction(Files, TEXT("min_max_abs"),
        TEXT("{'type':'minecraft:add','left':{'type':'minecraft:min','left':3.0,'right':-2.0},'right':{'type':'"
             "minecraft:"
             "max','left':{'type':'minecraft:abs','input':-4.0},'right':{'type':'minecraft:cube','input':{'type':'"
             "minecraft:"
             "square','input':1.5}}}}"));
    ClimateTestAddFunction(Files, TEXT("gradient_x"),
        TEXT("{'type':'minecraft:gradient','axis':'x','from_coordinate':0,'from_value':0.0,'to_coordinate':10,'to_"
             "value':1.0}"));
    ClimateTestAddFunction(Files, TEXT("gradient_z"),
        TEXT("{'type':'minecraft:gradient','axis':'z','from_coordinate':0,'from_value':0.0,'to_coordinate':10,'to_"
             "value':1.0}"));
    ClimateTestAddFunction(Files, TEXT("half_negative"), TEXT("{'type':'minecraft:half_negative','input':-2.0}"));
    ClimateTestAddFunction(Files, TEXT("quarter_negative"), TEXT("{'type':'minecraft:quarter_negative','input':-2.0}"));
    ClimateTestAddFunction(Files, TEXT("div"), TEXT("{'type':'minecraft:div','left':1.0,'right':4.0}"));
    ClimateTestAddFunction(Files, TEXT("gradient"),
        TEXT("{'type':'minecraft:gradient','axis':'y','from_coordinate':-64,'from_value':1.5,'to_coordinate':320,'to_"
             "value':-1.5}"));
    ClimateTestAddFunction(Files, TEXT("range_edge"),
        TEXT("{'type':'minecraft:range_choice','input':1.0,'min_inclusive':0.0,'max_exclusive':1.0,'when_in_range':5.0,"
             "'when_out_of_range':7.0}"));
    ClimateTestAddFunction(Files, TEXT("range_inside"),
        TEXT("{'type':'minecraft:range_choice','input':0.0,'min_inclusive':0.0,'max_exclusive':1.0,'when_in_range':5.0,"
             "'when_out_of_range':7.0}"));
    ClimateTestAddFunction(Files, TEXT("blend_alpha"), TEXT("{'type':'minecraft:blend_alpha'}"));
    ClimateTestAddFunction(Files, TEXT("blend_offset"), TEXT("{'type':'minecraft:blend_offset'}"));
    ClimateTestAddFunction(Files, TEXT("beardifier"), TEXT("{'type':'minecraft:beardifier'}"));
    ClimateTestAddFunction(Files, TEXT("noise_flat"),
        TEXT("{'type':'minecraft:noise','noise':'minecraft:test_noise','xz_scale':0.25,'y_scale':0.0}"));
    ClimateTestAddFunction(Files, TEXT("noise_tall"),
        TEXT("{'type':'minecraft:noise','noise':'minecraft:test_noise','xz_scale':0.5,'y_scale':2.0}"));
    ClimateTestAddFunction(Files, TEXT("shift_a"), TEXT("{'type':'minecraft:shift_a','noise':'minecraft:test_noise'}"));
    ClimateTestAddFunction(Files, TEXT("shift_b"), TEXT("{'type':'minecraft:shift_b','noise':'minecraft:test_noise'}"));
    ClimateTestAddFunction(Files, TEXT("noise_shifted"),
        TEXT("{'type':'minecraft:noise','noise':'minecraft:test_noise','shift_x':3.0,'shift_z':-5.0,'xz_scale':0.25,'y_"
             "scale':0.0}"));
    ClimateTestAddFunction(Files, TEXT("interpolated"),
        TEXT("{'type':'minecraft:interpolated','cell_size_xz':4,'cell_size_y':8,'input':1.0}"));
    // Mixes noise, gradient and shift so the batch test covers ops that read x, y and z.
    ClimateTestAddFunction(Files, TEXT("mix"),
        TEXT("{'type':'minecraft:add','left':{'type':'minecraft:noise','noise':'minecraft:test_noise','xz_scale':0.5,'"
             "y_scale':2.0},"
             "'right':{'type':'minecraft:mul','left':{'type':'minecraft:gradient','axis':'y','from_coordinate':-64,'"
             "from_value':1.5,"
             "'to_coordinate':320,'to_value':-1.5},'right':{'type':'minecraft:shift_a','noise':'minecraft:test_noise'}}"
             "}"));

    FVoxelDensityGraph Graph;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("test graph reads (%s)"), *Error), ClimateTestRead(Files, Graph, Error)))
        return false;
    const int32 NoiseIndex = Graph.FindNoise(TEXT("minecraft:test_noise"));
    if (!TestTrue(TEXT("test noise read"), NoiseIndex != INDEX_NONE)) return false;

    FVoxelDensityScratch Scratch;
    TArray<float> Values;
    const FVector3d Origin(0, 0, 0);

    const auto ExpectAt = [&](const TCHAR* Name, TConstArrayView<FVector3d> Points, TConstArrayView<double> Expected)
    {
        FString CompileError;
        if (!TestTrue(FString::Printf(TEXT("%s compiles (%s)"), Name, *CompileError),
                ClimateTestEvaluate(Graph, Name, Points, Scratch, Values, &CompileError)))
            return;
        for (int32 Index = 0; Index < Expected.Num(); ++Index)
        {
            TestTrue(
                FString::Printf(TEXT("%s [%d] = %.9g, expected %.9g"), Name, Index, Values[Index], Expected[Index]),
                FMath::Abs(Values[Index] - Expected[Index]) <= 1e-6);
        }
    };
    const auto ExpectConstant = [&](const TCHAR* Name, double Expected) { ExpectAt(Name, {Origin}, {Expected}); };

    // Black-box measurements of the 26.3 jar.
    ExpectConstant(TEXT("lerp"), 1.5);
    ExpectConstant(TEXT("squeeze"), 0.45833334);
    ExpectConstant(TEXT("half_negative"), -1);
    // Our own expectations from the ops' definitions.
    ExpectConstant(TEXT("squeeze_inside"), 0.5 / 2 - 0.125 / 24);
    ExpectConstant(TEXT("half_positive"), 2);
    ExpectConstant(TEXT("quarter_positive"), 2);
    ExpectConstant(TEXT("clamp"), 2);
    ExpectConstant(TEXT("min_max_abs"), -2 + 11.390625); // min(3, -2) + max(|-4|, (1.5^2)^3)
    ExpectAt(TEXT("gradient_x"), {FVector3d(5, 100, 0), FVector3d(20, 0, 0)}, {.5, 1});
    ExpectAt(TEXT("gradient_z"), {FVector3d(100, 0, 5), FVector3d(0, 0, -3)}, {.5, 0});
    ExpectConstant(TEXT("quarter_negative"), -0.5);
    ExpectConstant(TEXT("div"), 0.25);
    ExpectAt(TEXT("gradient"), {FVector3d(0, -64, 0), FVector3d(0, 0, 0), FVector3d(0, 128, 0), FVector3d(0, 320, 0)},
        {1.5, 1.0, 0.0, -1.5});
    ExpectConstant(TEXT("range_edge"), 7); // max_exclusive is exclusive
    ExpectConstant(TEXT("range_inside"), 5);
    ExpectConstant(TEXT("blend_alpha"), 1);
    ExpectConstant(TEXT("blend_offset"), 0);
    ExpectConstant(TEXT("beardifier"), 0);

    // Noise ops against the same noise built directly, exactly.
    const FVoxelNormalNoise N(ClimateTestSeed, Graph.Noises[NoiseIndex]);
    const FVector3d Points[] = {FVector3d(100, 70, -300), FVector3d(-12345, 5, 6789), FVector3d(7, -40, 3)};
    const auto ExpectExact = [&](const TCHAR* Name, TFunctionRef<float(const FVector3d&)> Expected)
    {
        if (!TestTrue(
                FString::Printf(TEXT("%s compiles"), Name), ClimateTestEvaluate(Graph, Name, Points, Scratch, Values)))
            return;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
            TestEqual(FString::Printf(TEXT("%s point %d"), Name, Index), Values[Index], Expected(Points[Index]));
    };
    ExpectExact(TEXT("noise_flat"), [&](const FVector3d& P) { return N.Sample(P.X * .25, 0, P.Z * .25); });
    ExpectExact(TEXT("noise_tall"), [&](const FVector3d& P) { return N.Sample(P.X * .5, P.Y * 2, P.Z * .5); });
    ExpectExact(
        TEXT("shift_a"), [&](const FVector3d& P) { return float(4 * double(N.Sample(P.X * .25, 0, P.Z * .25))); });
    ExpectExact(
        TEXT("shift_b"), [&](const FVector3d& P) { return float(4 * double(N.Sample(P.Z * .25, P.X * .25, 0))); });
    ExpectExact(TEXT("noise_shifted"), [&](const FVector3d& P) { return N.Sample(P.X * .25 + 3, 0, P.Z * .25 - 5); });

    // Ops that wait for the 3D density must refuse to compile rather than guess.
    {
        FVoxelDensityEvaluator Evaluator;
        FString CompileError;
        const int32 Node = Graph.FindFunction(ClimateTestFunctionId(TEXT("interpolated")));
        TestTrue(TEXT("interpolated root refuses to compile"),
            !Evaluator.Compile(Graph, ClimateTestSeed, {Node}, &CompileError));
        TestTrue(TEXT("the refusal names the op"), CompileError.Contains(TEXT("interpolated")));
    }

    // Batch independence: a point's value never depends on its neighbors or on what the scratch held before.
    TArray<FVector3d> Batch;
    for (int32 Index = 0; Index < 25; ++Index)
        Batch.Add(FVector3d(Index * 211.5 - 2500, Index * 17.0 - 100, 3000 - Index * 263.25));
    FVoxelDensityScratch BatchScratch;
    TArray<float> BatchValues;
    if (!TestTrue(
            TEXT("mix compiles"), ClimateTestEvaluate(Graph, TEXT("mix"), Batch, BatchScratch, BatchValues, &Error)))
        return false;
    bool bSingles = true;
    for (int32 Index = 0; Index < Batch.Num(); ++Index)
    {
        FVoxelDensityScratch OneScratch;
        TArray<float> One;
        ClimateTestEvaluate(Graph, TEXT("mix"), {Batch[Index]}, OneScratch, One);
        bSingles &= One.Num() == 1 && One[0] == BatchValues[Index];
    }
    TestTrue(TEXT("25 points in one call equal 25 one-point calls"), bSingles);

    // BatchScratch still holds 25 points' buffers; reuse it for 7.
    TArray<float> Small;
    ClimateTestEvaluate(Graph, TEXT("mix"), TConstArrayView<FVector3d>(Batch).Left(7), BatchScratch, Small);
    bool bReused = Small.Num() == 7;
    for (int32 Index = 0; bReused && Index < 7; ++Index)
        bReused &= Small[Index] == BatchValues[Index];
    TestTrue(TEXT("a scratch reused from a larger batch gives the same values"), bReused);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelClimatePresetsTest, "Unnamed.Voxel.Climate.Presets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelClimatePresetsTest::RunTest(const FString& Parameters)
{
    const TCHAR* const Presets[] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};
    const FIntPoint Columns[] = {FIntPoint(0, 0), FIntPoint(-3, 7), FIntPoint(1000, -2000), FIntPoint(-123456, 98765)};
    const FString Root = VoxelWorldgen::GetReferenceRoot();

    for (int32 PresetIndex = 0; PresetIndex < UE_ARRAY_COUNT(Presets); ++PresetIndex)
    {
        const FString Name = Presets[PresetIndex];
        FVoxelTerrainPreset Preset;
        FString Error;
        if (!TestTrue(FString::Printf(TEXT("%s reads (%s)"), *Name, *Error),
                VoxelWorldgen::ReadPreset(Root, Name, Preset, Error)))
            continue;
        FVoxelClimateSampler Sampler;
        if (!TestTrue(FString::Printf(TEXT("%s initializes (%s)"), *Name, *Error),
                Sampler.Initialize(Preset, ClimateTestSeed, &Error)))
            continue;

        FVoxelDensityScratch Scratch;
        double Sum = 0;
        bool bFinite = true, bFolded = true, bRanges = true, bShared = true;
        for (const FIntPoint& Column : Columns)
        {
            FVoxelColumnClimate Here, East;
            Sampler.SampleColumn(Column.X, Column.Y, Scratch, Here);
            Sampler.SampleColumn(Column.X + 1, Column.Y, Scratch, East);
            Sum += ClimateTestSum(Here);
            for (const FVoxelClimateSample& Sample : Here.Samples)
            {
                for (float Value : Sample.Values)
                    bFinite &= FMath::IsFinite(Value);
                const float Ridges = Sample[EVoxelClimate::Ridges];
                bFolded &= FMath::Abs(Sample[EVoxelClimate::PeaksAndValleys] -
                               (1.f - FMath::Abs(3.f * FMath::Abs(Ridges) - 2.f))) <= 1e-5f;
                bRanges &= Sample[EVoxelClimate::Factor] > 0 && Sample[EVoxelClimate::Jaggedness] >= 0;
            }
            // Neighboring columns share the edge corners, so chunks agree on their seam.
            for (int32 Y = 0; Y < FVoxelColumnClimate::Corners; ++Y)
                bShared &= ClimateTestSamplesEqual(Here.At(FVoxelColumnClimate::Corners - 1, Y), East.At(0, Y));
        }
        TestTrue(FString::Printf(TEXT("%s: every value finite"), *Name), bFinite);
        TestTrue(FString::Printf(TEXT("%s: PV = 1 - |3|W| - 2|"), *Name), bFolded);
        TestTrue(FString::Printf(TEXT("%s: factor > 0, jaggedness >= 0"), *Name), bRanges);
        TestTrue(FString::Printf(TEXT("%s: neighboring columns share edge corners"), *Name), bShared);

        AddInfo(FString::Printf(TEXT("%s golden sum: %.17g"), *Name, Sum));
        TestTrue(FString::Printf(TEXT("%s: golden"), *Name),
            FMath::Abs(Sum - ClimateTestPresetGolden[PresetIndex]) <=
                1e-9 * FMath::Abs(ClimateTestPresetGolden[PresetIndex]));

        // Height matters only for the depth: higher blocks sit deeper above the surface.
        const float Low = Sampler.SampleDepth(0, 0, -64, Scratch);
        const float Middle = Sampler.SampleDepth(0, 0, 64, Scratch);
        const float High = Sampler.SampleDepth(0, 0, 256, Scratch);
        TestTrue(FString::Printf(TEXT("%s: depth falls with height (%g, %g, %g)"), *Name, Low, Middle, High),
            Low > Middle && Middle > High);

        if (PresetIndex == 0)
        {
            FVoxelClimateSampler Other;
            if (TestTrue(TEXT("second seed initializes"), Other.Initialize(Preset, ClimateTestSeed + 1, &Error)))
            {
                FVoxelColumnClimate A, B;
                Sampler.SampleColumn(0, 0, Scratch, A);
                Other.SampleColumn(0, 0, Scratch, B);
                bool bDiffers = false;
                for (int32 Corner = 0; Corner < UE_ARRAY_COUNT(A.Samples); ++Corner)
                    bDiffers |=
                        A.Samples[Corner][EVoxelClimate::Continents] != B.Samples[Corner][EVoxelClimate::Continents];
                TestTrue(TEXT("different seeds give different continents"), bDiffers);
            }
        }
    }

    for (int32 Index = 0; Index < int32(EVoxelClimate::Count); ++Index)
    {
        const EVoxelClimate Parameter = EVoxelClimate(Index);
        EVoxelClimate Parsed = EVoxelClimate::Count;
        const bool bParsed = FVoxelClimateSampler::ParseLabel(FVoxelClimateSampler::GetLabel(Parameter), Parsed);
        TestTrue(FString::Printf(TEXT("label %s round-trips"), FVoxelClimateSampler::GetLabel(Parameter)),
            bParsed && Parsed == Parameter);
    }
    EVoxelClimate Lower = EVoxelClimate::Count;
    TestTrue(TEXT("labels parse case-insensitively"),
        FVoxelClimateSampler::ParseLabel(TEXT("pv"), Lower) && Lower == EVoxelClimate::PeaksAndValleys);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelClimateThreadsTest, "Unnamed.Voxel.Climate.Threads",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelClimateThreadsTest::RunTest(const FString& Parameters)
{
    FVoxelTerrainPreset Preset;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("overworld reads (%s)"), *Error),
            VoxelWorldgen::ReadPreset(VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), Preset, Error)))
        return false;
    FVoxelClimateSampler Sampler;
    if (!TestTrue(FString::Printf(TEXT("overworld initializes (%s)"), *Error),
            Sampler.Initialize(Preset, ClimateTestSeed, &Error)))
        return false;

    constexpr int32 Width = 8, Count = Width * Width, Threads = 8;
    // Column Index is (Index % Width, Index / Width), offset so negative coordinates are covered too.
    const auto ColumnOf = [](int32 Index) { return FIntPoint(Index % Width - 4, Index / Width - 4); };

    TArray<FVoxelColumnClimate> Expected;
    Expected.SetNum(Count);
    {
        FVoxelDensityScratch Scratch;
        for (int32 Index = 0; Index < Count; ++Index)
            Sampler.SampleColumn(ColumnOf(Index).X, ColumnOf(Index).Y, Scratch, Expected[Index]);
    }

    TArray<TArray<FVoxelColumnClimate>> Results;
    Results.SetNum(Threads);
    ParallelFor(Threads,
        [&](int32 Thread)
        {
            TArray<int32> Order;
            for (int32 Index = 0; Index < Count; ++Index)
                Order.Add(Index);
            // A different order per thread, so the scratches see different batch histories.
            FRandomStream Random(7000 + Thread);
            for (int32 Index = Count - 1; Index > 0; --Index)
                Order.Swap(Index, Random.RandRange(0, Index));

            Results[Thread].SetNum(Count);
            FVoxelDensityScratch Scratch;
            for (int32 Index : Order)
                Sampler.SampleColumn(ColumnOf(Index).X, ColumnOf(Index).Y, Scratch, Results[Thread][Index]);
        });

    for (int32 Thread = 0; Thread < Threads; ++Thread)
    {
        bool bEqual = true;
        for (int32 Index = 0; Index < Count; ++Index)
            bEqual &= ClimateTestColumnsEqual(Results[Thread][Index], Expected[Index]);
        TestTrue(FString::Printf(TEXT("thread %d matches the sequential pass bit-exactly"), Thread), bEqual);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
