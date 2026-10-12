#include "Blocks/VoxelBlockTable.h"
#include "Density/VoxelColumnDensity.h"
#include "Density/VoxelDensityEvaluator.h"
#include "Density/VoxelDensityGraph.h"
#include "Misc/AutomationTest.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelAquifer.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelBench.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr uint64 AquiferTestSeed = 263;
    const TCHAR* const AquiferTestPresets[3] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};

    // Recorded once on 2026-10-09 (caves on, phase 4 session 4), re-recorded once for the surface rules (phase 5
    // session 5, a decided change); never edit to pass a test.
    // Recorded under the decided change "veins on" (phase 6, S4); veins off must give the values from before S4.
    constexpr uint64 AquiferTestGolden[3] = {0x06587391fe15bb98ull, 0x6bef4434e7dee57cull, 0x82816e657968c3f1ull};
    constexpr uint64 AquiferTestGoldenNoVeins[3] = {
        0x22523b7400c2f38eull, 0x6b19b1927b73266aull, 0x863f9b8b01468a3full};

    constexpr int32 AquiferTestMinY = -64;
    constexpr int32 AquiferTestHeight = 384;
    constexpr int32 AquiferTestSeaLevel = 63;

    bool AquiferTestLoad(const TCHAR* Name, FVoxelTerrainPreset& Preset, FString& OutError)
    {
        return VoxelWorldgen::ReadPreset(VoxelWorldgen::GetReferenceRoot(), Name, Preset, OutError);
    }

    uint64 AquiferTestHash(uint64 Hash, TConstArrayView<uint16> States)
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

    // Prepare and Fill one terrain column (our axes) with the given aquifer.
    void AquiferTestRun(const FVoxelAquifer& Aquifer, const FVoxelDensityTerrain& Terrain, FIntPoint Position,
        FVoxelAquiferScratch& Scratch, TArray<float>& Density, TArray<EVoxelAquiferFluid>& Out)
    {
        Terrain.GenerateDensity(Position, Density, false, nullptr);
        Aquifer.Prepare(Position.X * 16, Position.Y * 16, Scratch);
        Aquifer.Fill(Position.X * 16, Position.Y * 16, Density, Scratch, Out);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelAquiferDeterminismTest, "Unnamed.Voxel.Aquifer.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelAquiferDeterminismTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    FVoxelTerrainPreset Preset;
    FString Error;
    if (!TestTrue(
            FString::Printf(TEXT("overworld reads (%s)"), *Error), AquiferTestLoad(TEXT("overworld"), Preset, Error)))
        return true;
    const FVoxelDensityTerrain Terrain(Preset, AquiferTestSeed, *Table);
    if (!TestTrue(FString::Printf(TEXT("terrain is valid (%s)"), *Terrain.GetError()), Terrain.IsValid())) return true;

    FVoxelAquifer AquiferA, AquiferB, AquiferOther;
    if (!TestTrue(FString::Printf(TEXT("aquifer A initializes (%s)"), *Error),
            AquiferA.Initialize(Preset, AquiferTestSeed, &Error)))
        return true;
    if (!TestTrue(FString::Printf(TEXT("aquifer B initializes (%s)"), *Error),
            AquiferB.Initialize(Preset, AquiferTestSeed, &Error)))
        return true;
    if (!TestTrue(FString::Printf(TEXT("other-seed aquifer initializes (%s)"), *Error),
            AquiferOther.Initialize(Preset, AquiferTestSeed + 1, &Error)))
        return true;

    // (a) Two instances, same preset and seed.
    {
        const FIntPoint Position(2, -5);
        TArray<float> Density;
        Terrain.GenerateDensity(Position, Density, false, nullptr);
        FVoxelAquiferScratch ScratchA, ScratchB;
        TArray<EVoxelAquiferFluid> OutA, OutB;
        AquiferA.Prepare(Position.X * 16, Position.Y * 16, ScratchA);
        AquiferA.Fill(Position.X * 16, Position.Y * 16, Density, ScratchA, OutA);
        AquiferB.Prepare(Position.X * 16, Position.Y * 16, ScratchB);
        AquiferB.Fill(Position.X * 16, Position.Y * 16, Density, ScratchB, OutB);
        TestTrue(TEXT("two instances: Fill output is not empty"), OutA.Num() > 0);
        TestTrue(TEXT("two instances: identical Fill output"), OutA == OutB);
        TestTrue(TEXT("two instances: identical KeepEmpty"),
            TArray<uint8>(AquiferA.GetKeepEmpty(ScratchA)) == TArray<uint8>(AquiferB.GetKeepEmpty(ScratchB)));
    }

    // (b) Scratch history independence: A, B, A again with one scratch.
    {
        const FIntPoint PositionA(0, 0), PositionB(-7, 11);
        FVoxelAquiferScratch Scratch;
        TArray<float> Density;
        TArray<EVoxelAquiferFluid> First, Between, Second;
        AquiferTestRun(AquiferA, Terrain, PositionA, Scratch, Density, First);
        AquiferTestRun(AquiferA, Terrain, PositionB, Scratch, Density, Between);
        AquiferTestRun(AquiferA, Terrain, PositionA, Scratch, Density, Second);
        TestTrue(TEXT("scratch reuse: column A is unchanged after column B"), First == Second);
    }

    // (c) Different seeds differ somewhere over 9 columns.
    {
        FVoxelAquiferScratch ScratchA, ScratchB;
        TArray<float> Density;
        int32 Differing = 0;
        for (int32 Y = -1; Y <= 1; ++Y)
        {
            for (int32 X = -1; X <= 1; ++X)
            {
                TArray<EVoxelAquiferFluid> OutA, OutB;
                const FIntPoint Position(X * 3, Y * 3);
                AquiferTestRun(AquiferA, Terrain, Position, ScratchA, Density, OutA);
                AquiferTestRun(AquiferOther, Terrain, Position, ScratchB, Density, OutB);
                if (!(OutA == OutB)) ++Differing;
            }
        }
        TestTrue(FString::Printf(TEXT("different seeds differ in at least one of 9 columns (%d differ)"), Differing),
            Differing > 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelAquiferFluidsTest, "Unnamed.Voxel.Aquifer.Fluids",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelAquiferFluidsTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    FVoxelTerrainPreset Preset;
    FString Error;
    if (!TestTrue(
            FString::Printf(TEXT("overworld reads (%s)"), *Error), AquiferTestLoad(TEXT("overworld"), Preset, Error)))
        return true;
    const FVoxelDensityTerrain Terrain(Preset, AquiferTestSeed, *Table);
    if (!TestTrue(FString::Printf(TEXT("terrain is valid (%s)"), *Terrain.GetError()), Terrain.IsValid())) return true;
    FVoxelAquifer Aquifer;
    if (!TestTrue(FString::Printf(TEXT("aquifer initializes (%s)"), *Error),
            Aquifer.Initialize(Preset, AquiferTestSeed, &Error)))
        return true;

    const uint16 Water = Table->FindState(TEXT("water"));
    const uint16 Lava = Table->FindState(TEXT("lava"));
    if (!Lava) AddWarning(TEXT("lava is missing from the block table: lava checks skipped"));

    TArray<FIntPoint> Positions;
    for (int32 Y = -3; Y <= 3; ++Y)
    {
        for (int32 X = -3; X <= 3; ++X)
            Positions.Add(FIntPoint(X, Y));
    }
    for (int32 Y = -3; Y <= 3; ++Y)
    {
        for (int32 X = -3; X <= 3; ++X)
            Positions.Add(FIntPoint(40 + X, -25 + Y));
    }

    int64 WaterCount = 0, LavaCount = 0, AirBelowSea = 0, BarrierCount = 0;
    int64 DeepWater = 0, DeepNonLava = 0, HighLava = 0, DryWater = 0;
    FVoxelAquiferScratch Scratch;
    TArray<float> Density;
    TArray<EVoxelAquiferFluid> Fluids;

    for (const FIntPoint& Position : Positions)
    {
        FVoxelColumn Column;
        TArray<uint16> Dense;
        Terrain.Generate(Position, Column, Dense);

        for (int32 LocalY = 0; LocalY < 16; ++LocalY)
        {
            for (int32 LocalX = 0; LocalX < 16; ++LocalX)
            {
                bool bSolidAbove = false;
                for (int32 Z = AquiferTestMinY + AquiferTestHeight - 1; Z >= AquiferTestMinY; --Z)
                {
                    const uint16 State = Dense[FVoxelColumn::GetDenseIndex(LocalX, LocalY, Z)];
                    const bool bIsWater = State != 0 && State == Water;
                    const bool bIsLava = State != 0 && State == Lava;
                    if (bIsWater)
                    {
                        ++WaterCount;
                        if (Z < FVoxelAquifer::LavaLine) ++DeepWater;
                        if (Z > AquiferTestSeaLevel - 1 && !bSolidAbove) ++DryWater;
                    }
                    else if (bIsLava)
                    {
                        ++LavaCount;
                        if (Z >= -10) ++HighLava;
                    }
                    else if (State == 0 && Z <= AquiferTestSeaLevel - 1)
                    {
                        ++AirBelowSea;
                    }
                    if (Z < FVoxelAquifer::LavaLine && Lava && State != Lava)
                    {
                        const bool bSolid = State != 0 && State != Water;
                        if (!bSolid) ++DeepNonLava;
                    }
                    if (State != 0 && !bIsWater && !bIsLava) bSolidAbove = true;
                }
            }
        }

        Aquifer.Prepare(Position.X * 16, Position.Y * 16, Scratch);
        Terrain.GenerateDensity(Position, Density, false, nullptr);
        Aquifer.Fill(Position.X * 16, Position.Y * 16, Density, Scratch, Fluids);
        for (const EVoxelAquiferFluid Fluid : Fluids)
        {
            if (Fluid == EVoxelAquiferFluid::Barrier) ++BarrierCount;
        }
    }

    AddInfo(FString::Printf(TEXT("overworld 98 columns: water %lld, lava %lld, air below sea level %lld, barrier %lld"),
        WaterCount, LavaCount, AirBelowSea, BarrierCount));

    TestEqual(FString::Printf(TEXT("no water below y %d"), FVoxelAquifer::LavaLine), DeepWater, int64(0));
    if (Lava)
    {
        TestEqual(FString::Printf(TEXT("open space below y %d is lava (water %lld, lava %lld)"),
                      FVoxelAquifer::LavaLine, WaterCount, LavaCount),
            DeepNonLava, int64(0));
        TestEqual(TEXT("no lava at y -10 or above"), HighLava, int64(0));
        TestTrue(FString::Printf(TEXT("lava exists over the 98 columns (%lld)"), LavaCount), LavaCount > 0);
    }
    TestEqual(TEXT("water above sea level only under a solid block"), DryWater, int64(0));
    TestTrue(
        FString::Printf(TEXT("aquifer barriers exist over the 98 columns (%lld)"), BarrierCount), BarrierCount > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelAquiferGoldenTest, "Unnamed.Voxel.Aquifer.Golden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelAquiferGoldenTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    const TCHAR* const StateNames[] = {TEXT("water"), TEXT("lava"), TEXT("stone"), TEXT("deepslate")};
    const FIntPoint Positions[] = {{0, 0}, {1, 0}, {-3, 7}, {100, -50}};

    for (int32 PresetIndex = 0; PresetIndex < 3; ++PresetIndex)
    {
        const TCHAR* const PresetName = AquiferTestPresets[PresetIndex];
        FVoxelTerrainPreset Preset;
        FString Error;
        FVoxelBiomeTable Biomes;
        FVoxelSurfaceInputs Surface;
        if (!TestTrue(FString::Printf(TEXT("%s reads (%s)"), PresetName, *Error),
                VoxelTerrainFactory::ReadInputs(
                    VoxelWorldgen::GetReferenceRoot(), PresetName, Preset, Biomes, Surface, Error)))
            continue;
        // With biomes and the surface rules since phase 5 session 5.
        FVoxelDensityTerrainSettings Settings;
        Settings.bCaves = true;
        const FVoxelDensityTerrain Terrain(Preset, AquiferTestSeed, *Table, Settings, &Biomes, &Surface);
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
            Hash = AquiferTestHash(Hash, Dense);
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
        {
            const uint16 State = Table->FindState(StateName);
            Info += FString::Printf(TEXT(", %s %lld"), StateName, State ? Counts.FindRef(State) : int64(0));
        }
        AddInfo(Info);

        if (AquiferTestGolden[PresetIndex] == 0)
        {
            AddInfo(FString::Printf(TEXT("%s: record 0x%016llx"), PresetName, static_cast<unsigned long long>(Hash)));
        }
        else if (Hash != AquiferTestGolden[PresetIndex])
        {
            AddError(FString::Printf(TEXT("%s: block hash 0x%016llx, golden 0x%016llx"), PresetName,
                static_cast<unsigned long long>(Hash),
                static_cast<unsigned long long>(AquiferTestGolden[PresetIndex])));
        }

        // Veins off: the blocks from before S4, exactly.
        FVoxelDensityTerrainOverrides NoVeins;
        NoVeins.bOreVeins = false;
        uint64 NoVeinsHash = 0xcbf29ce484222325ull;
        for (const FIntPoint& Position : Positions)
        {
            FVoxelColumn Column;
            TArray<uint16> Dense;
            Terrain.Generate(Position, Column, Dense, nullptr, NoVeins);
            NoVeinsHash = AquiferTestHash(NoVeinsHash, Dense);
        }
        if (NoVeinsHash != AquiferTestGoldenNoVeins[PresetIndex])
            AddError(FString::Printf(TEXT("%s: veins off: block hash 0x%016llx, golden 0x%016llx"), PresetName,
                static_cast<unsigned long long>(NoVeinsHash),
                static_cast<unsigned long long>(AquiferTestGoldenNoVeins[PresetIndex])));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDensityLoadOrderTest, "Unnamed.Voxel.Density.LoadOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Generator 2 with caves gives every column the same blocks whatever ran before it: 5 x 5 columns per preset, row by
// row on one thread (scratch reused column after column) and backwards across every worker.
bool FVoxelDensityLoadOrderTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    for (const TCHAR* Name : {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")})
    {
        FVoxelTerrainPreset Preset;
        FString Error;
        if (!TestTrue(FString::Printf(TEXT("%s reads (%s)"), Name, *Error), AquiferTestLoad(Name, Preset, Error)))
            continue;
        const FVoxelDensityTerrain Terrain(Preset, AquiferTestSeed, *Table);
        if (!TestTrue(FString::Printf(TEXT("%s terrain is valid (%s)"), Name, *Terrain.GetError()), Terrain.IsValid()))
            continue;
        const VoxelBench::FLoadOrderHashes Hashes = VoxelBench::HashLoadOrders(Terrain, 2);
        TestEqual(FString::Printf(TEXT("%s: columns hashed"), Name), Hashes.Columns, 25);
        TestEqual(FString::Printf(TEXT("%s: columns that differ between load orders"), Name), Hashes.Mismatches, 0);
        TestEqual(FString::Printf(TEXT("%s: combined hashes"), Name), Hashes.Forward, Hashes.Backward);
    }
    return true;
}

#endif
