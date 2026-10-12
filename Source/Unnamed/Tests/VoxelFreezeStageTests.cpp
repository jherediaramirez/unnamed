#include "Async/ParallelFor.h"
#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelWriteLog.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelSeed.h"
#include "Storage/VoxelColumn.h"
#include "Surface/VoxelFreezePass.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "VoxelBench.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldSettings.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelConstants.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr uint64 FreezeTestSeed = 263;
    const TCHAR* const FreezeTestPresets[3] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};

    struct FFreezeTestWorld
    {
        TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
        FVoxelTerrainPreset Preset;
        FVoxelBiomeTable Biomes;
        FVoxelSurfaceInputs Surface;
        FVoxelFeatureGraph Graph;
        TSharedPtr<FVoxelDensityTerrain> Terrain;
    };

    bool FreezeHasBlocks(FAutomationTestBase& Test)
    {
        if (VoxelBlocks::GetDefault()->FindState(TEXT("stone"))) return true;
        Test.AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }

    // The preset's terrain with caves; the decoration data is read only when a decorator will be compiled.
    bool FreezeMakeWorld(FAutomationTestBase& Test, const TCHAR* PresetName, FFreezeTestWorld& Out, bool bFeatures,
        uint64 Seed = FreezeTestSeed)
    {
        FString Error;
        if (!Test.TestTrue(FString::Printf(TEXT("%s inputs read (%s)"), PresetName, *Error),
                VoxelTerrainFactory::ReadInputs(
                    VoxelWorldgen::GetReferenceRoot(), PresetName, Out.Preset, Out.Biomes, Out.Surface, Error)))
            return false;
        if (bFeatures &&
            !Test.TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
                VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Out.Graph, Error)))
            return false;
        FVoxelDensityTerrainSettings Settings;
        Settings.bCaves = true;
        Out.Terrain =
            MakeShared<FVoxelDensityTerrain>(Out.Preset, Seed, *Out.Table, Settings, &Out.Biomes, &Out.Surface);
        return Test.TestTrue(FString::Printf(TEXT("%s terrain is valid (%s)"), PresetName, *Out.Terrain->GetError()),
            Out.Terrain->IsValid());
    }

    TSharedPtr<FVoxelDecorator> FreezeMakeDecorator(
        FAutomationTestBase& Test, const FFreezeTestWorld& World, bool bFreezeLast)
    {
        FVoxelDecorationOptions Options;
        Options.bFreezeLast = bFreezeLast;
        FString Error;
        TSharedPtr<FVoxelDecorator> Decorator = MakeShared<FVoxelDecorator>();
        if (!Test.TestTrue(FString::Printf(TEXT("the decorator compiles (%s)"), *Error),
                Decorator->Initialize(World.Graph, *World.Table, World.Terrain->GetBiomeSource()->GetTable(),
                    World.Terrain->GetSeed(), Options, &Error)))
            return nullptr;
        return Decorator;
    }

    // The column on a grid (every Spacing columns within 16 Spacing of the origin) with the most block columns topped
    // by snow (ties: nearest the origin); OutSnowTops is that count.
    FIntPoint FreezeFindSnowyColumn(
        const IVoxelTerrain& Terrain, const FVoxelBlockTable& Table, int32& OutSnowTops, int32 Spacing = 8)
    {
        constexpr int32 Side = 33;
        const uint16 Snow = Table.FindState(TEXT("snow"));
        TArray<int32> Counts;
        Counts.Init(0, Side * Side);
        ParallelFor(Side * Side,
            [&Terrain, &Counts, Snow, Spacing](int32 Index)
            {
                FVoxelColumn Column;
                TArray<uint16> Dense;
                Terrain.Generate(
                    FIntPoint(Index % Side * Spacing - 16 * Spacing, Index / Side * Spacing - 16 * Spacing), Column,
                    Dense);
                int32 Tops = 0;
                for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
                    for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                        Tops += Column.GetState(X, Y, Column.GetHeight(EVoxelHeightmap::WorldSurface, X, Y)) == Snow;
                Counts[Index] = Tops;
            });
        FIntPoint Best = FIntPoint::ZeroValue;
        int32 BestCount = -1, BestDistance = 0;
        for (int32 Index = 0; Index < Side * Side; ++Index)
        {
            const FIntPoint Position(Index % Side * Spacing - 16 * Spacing, Index / Side * Spacing - 16 * Spacing);
            const int32 Distance = Position.X * Position.X + Position.Y * Position.Y;
            if (Counts[Index] > BestCount || (Counts[Index] == BestCount && Distance < BestDistance))
            {
                Best = Position;
                BestCount = Counts[Index];
                BestDistance = Distance;
            }
        }
        OutSnowTops = BestCount;
        return Best;
    }

    // Blocks, biomes and every heightmap equal.
    bool FreezeColumnsEqual(const FVoxelColumn& Left, const FVoxelColumn& Right)
    {
        if (VoxelBench::HashColumnBlocks(Left) != VoxelBench::HashColumnBlocks(Right)) return false;
        for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
            for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
            {
                for (int32 Map = 0; Map < int32(EVoxelHeightmap::Count); ++Map)
                    if (Left.GetHeight(EVoxelHeightmap(Map), X, Y) != Right.GetHeight(EVoxelHeightmap(Map), X, Y))
                        return false;
                for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; Z += 4)
                    if (Left.GetBiome(X, Y, Z) != Right.GetBiome(X, Y, Z)) return false;
            }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFreezeMovedTest, "Unnamed.Voxel.Freeze.Moved",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFreezeMovedTest::RunTest(const FString& Parameters)
{
    // FinishColumn(GenerateSnapshot(C)) equals Generate(C), and the snapshot is a real step short of it.
    if (!FreezeHasBlocks(*this)) return true;
    for (const TCHAR* const PresetName : FreezeTestPresets)
    {
        FFreezeTestWorld World;
        // large_biomes stretches the climate 4 times: seed 263 has no snow within 512 columns, the dev seed has some
        // near (80, 0).
        const bool bLarge = FCString::Strcmp(PresetName, TEXT("large_biomes")) == 0;
        if (!FreezeMakeWorld(*this, PresetName, World, false,
                bLarge ? uint64(FVoxelSeed::Parse(UVoxelWorldSettings::Get().DevSeed).Value) : FreezeTestSeed))
            continue;
        int32 SnowTops = 0;
        const FIntPoint Snowy = FreezeFindSnowyColumn(*World.Terrain, *World.Table, SnowTops, bLarge ? 32 : 8);
        TestTrue(FString::Printf(TEXT("%s: the snowiest column has at least 32 snow tops (%d)"), PresetName, SnowTops),
            SnowTops >= 32);
        TArray<FIntPoint> Positions;
        for (int32 DY = -2; DY <= 2; ++DY)
            for (int32 DX = -2; DX <= 2; ++DX)
                Positions.Add(Snowy + FIntPoint(DX, DY));
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
                Positions.AddUnique(FIntPoint(DX, DY));

        int32 Unequal = 0, Moved = 0;
        TArray<uint16> Dense;
        for (const FIntPoint& Position : Positions)
        {
            FVoxelColumn Whole, Staged;
            World.Terrain->Generate(Position, Whole, Dense);
            World.Terrain->GenerateSnapshot(Position, Staged, Dense);
            Moved += VoxelBench::HashColumnBlocks(Staged) != VoxelBench::HashColumnBlocks(Whole);
            World.Terrain->FinishColumn(Staged);
            Unequal += !FreezeColumnsEqual(Whole, Staged);
        }
        TestEqual(
            FString::Printf(TEXT("%s: columns where FinishColumn(GenerateSnapshot) differs from Generate"), PresetName),
            Unequal, 0);
        TestTrue(FString::Printf(TEXT("%s: the freeze left Generate (%d columns changed by it)"), PresetName, Moved),
            Moved > 0);
        AddInfo(FString::Printf(TEXT("%s: snowy column (%d, %d) with %d snow tops; %d columns checked, %d changed by "
                                     "the freeze, %d unequal"),
            PresetName, Snowy.X, Snowy.Y, SnowTops, Positions.Num(), Moved, Unequal));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFreezeAfterDecorationTest, "Unnamed.Voxel.Freeze.AfterDecoration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFreezeAfterDecorationTest::RunTest(const FString& Parameters)
{
    // Snow lands on a block another column placed when the freeze runs last; S2's order leaves it bare.
    if (!FreezeHasBlocks(*this)) return true;
    FFreezeTestWorld World;
    if (!FreezeMakeWorld(*this, TEXT("overworld"), World, true)) return true;
    const TSharedPtr<FVoxelDecorator> DecoratorLast = FreezeMakeDecorator(*this, World, true);
    const TSharedPtr<FVoxelDecorator> DecoratorFirst = FreezeMakeDecorator(*this, World, false);
    if (!DecoratorLast || !DecoratorFirst) return true;
    const FVoxelBlockTable& Table = *World.Table;
    const IVoxelTerrain& Terrain = *World.Terrain;

    int32 SnowTops = 0;
    const FIntPoint Snowy = FreezeFindSnowyColumn(Terrain, Table, SnowTops);
    if (!TestTrue(
            FString::Printf(TEXT("the snowiest column has at least 32 snow tops (%d)"), SnowTops), SnowTops >= 32))
        return true;

    const uint16 Snow = Table.FindState(TEXT("snow"));
    const uint16 Stone = Table.FindState(TEXT("stone"));
    TArray<uint16> Dense;
    FVoxelColumn Frozen, Snapshot;
    Terrain.Generate(Snowy, Frozen, Dense);
    DecoratorLast->GenerateSnapshot(Terrain, Snowy, Snapshot, Dense);
    FVoxelFreezePass Freeze;
    if (!TestTrue(TEXT("the freeze pass resolves"), Freeze.Initialize(Table))) return true;

    // A block column topped by a snow layer on a block that holds snow, with the layer absent from the snapshot.
    int32 PickX = -1, PickY = -1, PickZ = 0;
    for (int32 Y = 0; Y < VoxelConstants::ColumnSize && PickX < 0; ++Y)
        for (int32 X = 0; X < VoxelConstants::ColumnSize && PickX < 0; ++X)
        {
            const int32 Top = Frozen.GetHeight(EVoxelHeightmap::WorldSurface, X, Y);
            if (Top < VoxelConstants::MinZ + 1 || Top + 2 > VoxelConstants::MaxZ) continue;
            if (Frozen.GetState(X, Y, Top) != Snow) continue;
            if (!Table.Get(Frozen.GetState(X, Y, Top + 1)).IsAir() ||
                !Table.Get(Frozen.GetState(X, Y, Top + 2)).IsAir())
                continue;
            if (!Freeze.CanHoldSnow(Frozen.GetState(X, Y, Top - 1))) continue;
            if (!Table.Get(Snapshot.GetState(X, Y, Top)).IsAir()) continue;
            PickX = X;
            PickY = Y;
            PickZ = Top;
        }
    if (!TestTrue(TEXT("a snow layer on a block that holds snow was found"), PickX >= 0)) return true;

    FVoxelWriteBucket Bucket;
    Bucket.Add(
        FVoxelWriteBucket::MakeKey(0, 0), FVoxelWrite::Make(PickX, PickY, PickZ, Stone, EVoxelWriteMode::Force, 0));
    const FVoxelWriteBucket* Incoming[9] = {};
    Incoming[FVoxelWriteLog::GetBucket(1, 0)] = &Bucket;

    // Freeze last: Finalize on the snapshot, then the freeze.
    FVoxelColumn Last;
    DecoratorLast->Finalize(Snapshot, Incoming, Last);
    DecoratorLast->FinishColumn(Terrain, Last);
    TestEqual(TEXT("freeze last: the neighbour's stone is at Z"), Last.GetState(PickX, PickY, PickZ), Stone);
    TestEqual(TEXT("freeze last: snow landed on it at Z+1"), Last.GetState(PickX, PickY, PickZ + 1), Snow);

    // The heightmaps agree with the blocks.
    int32 ScanSurface = VoxelConstants::MinZ - 1, ScanMotion = VoxelConstants::MinZ - 1;
    for (int32 Z = VoxelConstants::MaxZ; Z >= VoxelConstants::MinZ; --Z)
    {
        const FVoxelStateInfo& Info = Table.Get(Last.GetState(PickX, PickY, Z));
        if (ScanSurface < VoxelConstants::MinZ && !Info.IsAir()) ScanSurface = Z;
        if (ScanMotion < VoxelConstants::MinZ && (Info.bSolid || Info.FluidAmount > 0)) ScanMotion = Z;
    }
    const int32 Surface = Last.GetHeight(EVoxelHeightmap::WorldSurface, PickX, PickY);
    const int32 Motion = Last.GetHeight(EVoxelHeightmap::MotionBlocking, PickX, PickY);
    TestEqual(TEXT("freeze last: WorldSurface is the snow layer (Z+1)"), Surface, PickZ + 1);
    TestEqual(TEXT("freeze last: WorldSurface equals the blocks' top"), Surface, ScanSurface);
    TestEqual(TEXT("freeze last: MotionBlocking equals the blocks' top"), Motion, ScanMotion);
    AddInfo(FString::Printf(TEXT("column (%d, %d), block (%d, %d, Z %d): WorldSurface %d, MotionBlocking %d (snow "
                                 "solid: %d)"),
        Snowy.X, Snowy.Y, PickX, PickY, PickZ, Surface, Motion, Table.Get(Snow).bSolid ? 1 : 0));
    AddInfo(FString::Printf(TEXT("block under the stone (Z-1): state %d in Generate, %d freeze last"),
        int32(Frozen.GetState(PickX, PickY, PickZ - 1)), int32(Last.GetState(PickX, PickY, PickZ - 1))));

    // S2's order: Finalize on the frozen column; the neighbour's block is bare.
    FVoxelColumn FirstSnapshot, First;
    DecoratorFirst->GenerateSnapshot(Terrain, Snowy, FirstSnapshot, Dense);
    DecoratorFirst->Finalize(FirstSnapshot, Incoming, First);
    DecoratorFirst->FinishColumn(Terrain, First);
    TestEqual(TEXT("S2 order: the neighbour's stone is at Z"), First.GetState(PickX, PickY, PickZ), Stone);
    TestTrue(
        TEXT("S2 order: the cell above the stone is air"), Table.Get(First.GetState(PickX, PickY, PickZ + 1)).IsAir());
    return true;
}

#endif
