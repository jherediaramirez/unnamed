#include "Blocks/VoxelBlockTable.h"
#include "Density/VoxelDensityGraph.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelSeed.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Terrain/VoxelSpawnFinder.h"
#include "Terrain/VoxelTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelConstants.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // The dev seed's spawn by the nearest-land search (-VoxelSpawnAt=0,0), read from the build.txt of a default
    // CaptureVoxel run on the build before the climate search (ff075ef, 2026-10-11,
    // Tests/Evidence/2026-10-11/VoxelShots-20261011-113429-spawn-before).
    const FIntVector SpawnTestNearestCell(129, 101, 71);

    FString SpawnTestFunctionName(const FVoxelDensityGraph& Graph, int32 Node)
    {
        for (const FVoxelDensityFunction& Function : Graph.Functions)
            if (Function.Node == Node) return Function.Name;
        return TEXT("(unnamed)");
    }

    bool SpawnTestLoad(const TCHAR* Name, FVoxelTerrainPreset& Preset, FString& OutError)
    {
        return VoxelWorldgen::ReadPreset(VoxelWorldgen::GetReferenceRoot(), Name, Preset, OutError);
    }

    // A terrain whose climate search finds no dry land at Pick, with one dry column at Land (top 70) in a sea whose
    // floor is at 40; bSearch false stands for a terrain without a climate search.
    class FSpawnTestSea final : public IVoxelTerrain
    {
    public:
        FIntPoint Pick = FIntPoint::ZeroValue;
        TOptional<FIntPoint> Land;
        bool bSearch = true;

        virtual int32 GetSurfaceHeight(int32 X, int32 Y) const override
        {
            return Land && X == Land->X && Y == Land->Y ? 70 : 40;
        }
        virtual bool IsDry(int32 X, int32 Y) const override { return GetSurfaceHeight(X, Y) >= 62; }
        virtual void Generate(FIntPoint, FVoxelColumn&, TArray<uint16>&) const override {}
        virtual bool FindSpawn(FIntVector& OutCell, bool& bOutDry) const override
        {
            if (!bSearch) return false;
            OutCell = FIntVector(Pick.X, Pick.Y, VoxelConstants::SeaLevel);
            bOutDry = false;
            return true;
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSpawnReaderTest, "Unnamed.Voxel.Spawn.Reader",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSpawnReaderTest::RunTest(const FString& Parameters)
{
    FVoxelTerrainPreset Overworld, Large, Amplified;
    FString Error;
    if (!TestTrue(TEXT("The three presets read"),
            SpawnTestLoad(TEXT("overworld"), Overworld, Error) && SpawnTestLoad(TEXT("large_biomes"), Large, Error) &&
                SpawnTestLoad(TEXT("amplified"), Amplified, Error)))
    {
        AddError(Error);
        return false;
    }
    // Inland (continents >= -0.11) and out of the river valleys (|ridges| >= 0.16), quantized x 10000.
    for (const FVoxelTerrainPreset* Preset : {&Overworld, &Large})
    {
        const FVoxelTerrainPreset::FRouter& Router = Preset->Router;
        TestEqual(Preset->Name + TEXT(": two targets"), Preset->SpawnTargets.Num(), 2);
        for (int32 Target = 0; Target < Preset->SpawnTargets.Num(); ++Target)
        {
            TestEqual(FString::Printf(TEXT("%s target %d: five ranges"), *Preset->Name, Target),
                Preset->SpawnTargets[Target].Num(), 5);
            for (const FVoxelTerrainPreset::FSpawnRange& Range : Preset->SpawnTargets[Target])
            {
                const FString What = FString::Printf(
                    TEXT("%s target %d %s"), *Preset->Name, Target, *SpawnTestFunctionName(Preset->Graph, Range.Node));
                const bool bRidges = Range.Node == Router.Ridges;
                TestTrue(What + TEXT(" is a router climate function"),
                    Range.Node == Router.Continents || Range.Node == Router.Erosion || bRidges ||
                        Range.Node == Router.Temperature || Range.Node == Router.Vegetation);
                const int64 Min = Range.Node == Router.Continents ? -1100 : bRidges && Target == 1 ? 1600 : -10000;
                const int64 Max = bRidges && Target == 0 ? -1600 : 10000;
                TestEqual(What + TEXT(" min"), Range.Min, Min);
                TestEqual(What + TEXT(" max"), Range.Max, Max);
            }
        }
    }
    TestEqual(TEXT("large_biomes reads its own continents"), Large.Router.Continents,
        Large.Graph.FindFunction(TEXT("minecraft:overworld_large_biomes/continents")));
    TestEqual(TEXT("...and the overworld's ridges"), Large.Router.Ridges,
        Large.Graph.FindFunction(TEXT("minecraft:overworld/ridges")));
    TestEqual(TEXT("The overworld reads overworld/continents"), Overworld.Router.Continents,
        Overworld.Graph.FindFunction(TEXT("minecraft:overworld/continents")));
    // amplified: the overworld's targets, function for function.
    bool bSame = Amplified.SpawnTargets.Num() == Overworld.SpawnTargets.Num();
    for (int32 Target = 0; bSame && Target < Overworld.SpawnTargets.Num(); ++Target)
    {
        bSame = Amplified.SpawnTargets[Target].Num() == Overworld.SpawnTargets[Target].Num();
        for (int32 Index = 0; bSame && Index < Overworld.SpawnTargets[Target].Num(); ++Index)
        {
            const FVoxelTerrainPreset::FSpawnRange& A = Amplified.SpawnTargets[Target][Index];
            const FVoxelTerrainPreset::FSpawnRange& O = Overworld.SpawnTargets[Target][Index];
            bSame = A.Min == O.Min && A.Max == O.Max &&
                SpawnTestFunctionName(Amplified.Graph, A.Node) == SpawnTestFunctionName(Overworld.Graph, O.Node);
        }
    }
    TestTrue(TEXT("amplified's targets equal the overworld's"), bSame);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSpawnScoreTest, "Unnamed.Voxel.Spawn.Score",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSpawnScoreTest::RunTest(const FString& Parameters)
{
    // Candidates: ceil(2 pi r / step) per ring.
    TArray<FIntPoint> First, Second;
    FVoxelSpawnFinder::GetFirstCandidates(First);
    FVoxelSpawnFinder::GetSecondCandidates(FIntPoint(100, -300), Second);
    TestEqual(TEXT("Stage 1: 1 + 7 + 13 + 19 + 26 points"), First.Num(), 66);
    TestEqual(TEXT("Stage 2: 7 + 13 + ... + 101 points"), Second.Num(), 862);
    TestEqual(TEXT("Stage 1 starts at the origin"), First[0], FIntPoint::ZeroValue);
    TestEqual(TEXT("...then its first ring at +X"), First[1], FIntPoint(512, 0));
    TestEqual(TEXT("Stage 2 starts 32 blocks +X of its centre"), Second[0], FIntPoint(132, -300));
    TestEqual(TEXT("...and ends on the 512 ring"), Second.Last(),
        FIntPoint(100 + FMath::RoundToInt32(512 * FMath::Cos(UE_DOUBLE_TWO_PI * 100 / 101)),
            -300 + FMath::RoundToInt32(512 * FMath::Sin(UE_DOUBLE_TWO_PI * 100 / 101))));

    // The distance penalty.
    TestEqual(TEXT("Penalty at the origin"), FVoxelSpawnFinder::GetDistancePenalty({0, 0}), int64(0));
    TestEqual(TEXT("Penalty at 512"), FVoxelSpawnFinder::GetDistancePenalty({512, 0}), int64(175921));
    TestEqual(TEXT("Penalty at 2500"), FVoxelSpawnFinder::GetDistancePenalty({2500, 0}), int64(100000000));
    TestEqual(TEXT("Penalty across"), FVoxelSpawnFinder::GetDistancePenalty({0, -2500}), int64(100000000));

    // The climate term over the overworld's targets.
    FVoxelTerrainPreset Overworld;
    FString Error;
    if (!TestTrue(TEXT("The overworld reads"), SpawnTestLoad(TEXT("overworld"), Overworld, Error)))
    {
        AddError(Error);
        return false;
    }
    const FVoxelTerrainPreset::FRouter& Router = Overworld.Router;
    auto Climate = [&](int64 Continents, int64 Ridges)
    {
        return FVoxelSpawnFinder::GetClimateDistance(Overworld.SpawnTargets, [&](int32 Node)
            { return Node == Router.Continents ? Continents
                  : Node == Router.Ridges      ? Ridges
                                               : int64(0); });
    };
    TestEqual(TEXT("Inside every range"), Climate(0, 5000), int64(0));
    TestEqual(TEXT("Ridges inside the first target"), Climate(0, -5000), int64(0));
    TestEqual(TEXT("Continents -0.2"), Climate(-2000, 5000), int64(810000));
    TestEqual(TEXT("Ridges 0, outside both"), Climate(0, 0), int64(2560000));
    TestEqual(
        TEXT("No targets"), FVoxelSpawnFinder::GetClimateDistance({}, [](int32) { return int64(99999); }), int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSpawnFallbackTest, "Unnamed.Voxel.Spawn.Fallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSpawnFallbackTest::RunTest(const FString& Parameters)
{
    // A wet pick with land 100 blocks east: found at any view radius (the fallback's reach is fixed).
    FSpawnTestSea Sea;
    Sea.Pick = FIntPoint(1000, 40);
    Sea.Land = FIntPoint(1100, 40);
    for (const int32 ViewRadius : {4, 12})
    {
        FVoxelSpawnRequest Request;
        Request.ViewReach = FMath::Max(8, ViewRadius * VoxelConstants::ColumnSize);
        const FVoxelSpawn Spawn = FVoxelSpawnFinder::Find(Sea, Request);
        TestEqual(FString::Printf(TEXT("View radius %d: the land nearest the pick"), ViewRadius), Spawn.Cell,
            FIntVector(1100, 40, 71));
        TestTrue(TEXT("...stood on its blocks"), Spawn.bStandOnBlocks);
    }
    // No land anywhere: the water's surface over the pick, not the sea floor.
    Sea.Land.Reset();
    const FVoxelSpawn Wet = FVoxelSpawnFinder::Find(Sea, {});
    TestEqual(TEXT("All sea: the water's surface over the pick"), Wet.Cell, FIntVector(1000, 40, 63));
    TestFalse(TEXT("...not moved onto blocks"), Wet.bStandOnBlocks);
    // Without a climate search, and on request: the nearest-land search, falling back to the sea floor.
    Sea.Land = FIntPoint(3, -2);
    FVoxelSpawnRequest Nearest;
    Nearest.bNearest = true;
    Nearest.Center = FIntPoint(1, 0);
    TestEqual(TEXT("Requested: the land nearest the centre"), FVoxelSpawnFinder::Find(Sea, Nearest).Cell,
        FIntVector(3, -2, 71));
    TestFalse(TEXT("...as found"), FVoxelSpawnFinder::Find(Sea, Nearest).bStandOnBlocks);
    Sea.bSearch = false;
    TestEqual(TEXT("No climate search: the land nearest the origin"), FVoxelSpawnFinder::Find(Sea, {}).Cell,
        FIntVector(3, -2, 71));
    Sea.Land.Reset();
    TestEqual(TEXT("...or the sea floor at the origin"), FVoxelSpawnFinder::Find(Sea, {}).Cell, FIntVector(0, 0, 41));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSpawnStandingTest, "Unnamed.Voxel.Spawn.Standing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSpawnStandingTest::RunTest(const FString& Parameters)
{
    // A tree on grass with tall grass on its crown: the feet go on the top leaves, as 26.3's do.
    const FVoxelBlockTable& Table = *VoxelBlocks::GetDefault();
    TMap<int32, uint16> Column;
    Column.Add(64, Table.FindState(TEXT("grass_block")));
    for (int32 Z = 65; Z <= 68; ++Z)
        Column.Add(Z, Table.FindState(TEXT("oak_log")));
    Column.Add(69, Table.FindState(TEXT("oak_leaves")));
    Column.Add(70, Table.FindState(TEXT("oak_leaves")));
    Column.Add(71, Table.FindState(TEXT("short_grass")));
    for (const TPair<int32, uint16>& Block : Column)
        TestNotEqual(FString::Printf(TEXT("The block at %d exists"), Block.Key), int32(Block.Value), 0);
    auto Collides = [&](int32 Z)
    {
        const uint16* State = Column.Find(Z);
        return State && Table.Get(*State).bSolid;
    };
    // The density's top is the grass: 65 is where the search stood.
    TestEqual(TEXT("On the top leaves"),
        FVoxelSpawnFinder::GetStandingZ(Collides, VoxelConstants::MinZ, VoxelConstants::MaxZ, 65), 71);
    TestEqual(TEXT("Nothing collides: where the search stood"),
        FVoxelSpawnFinder::GetStandingZ([](int32) { return false; }, VoxelConstants::MinZ, VoxelConstants::MaxZ, 65),
        65);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSpawnSeedsTest, "Unnamed.Voxel.Spawn.Seeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSpawnSeedsTest::RunTest(const FString& Parameters)
{
    const FVoxelBlockTable& Table = *VoxelBlocks::GetDefault();
    const int64 DevSeed = FVoxelSeed::Parse(TEXT("Unnamed")).Value;
    TestEqual(TEXT("The dev seed's text"), DevSeed, int64(-3224043261468865476ll));
    FVoxelTerrainPreset Overworld, Large, Amplified;
    FString Error;
    if (!TestTrue(TEXT("The three presets read"),
            SpawnTestLoad(TEXT("overworld"), Overworld, Error) && SpawnTestLoad(TEXT("large_biomes"), Large, Error) &&
                SpawnTestLoad(TEXT("amplified"), Amplified, Error)))
    {
        AddError(Error);
        return false;
    }
    // Twenty seeds and the dev seed, terrain only: every spawn stands on dry land within the search's reach.
    TArray<int64> Seeds;
    for (int64 Seed = 0; Seed < 20; ++Seed)
        Seeds.Add(Seed);
    Seeds.Add(DevSeed);
    // The seed whose origin lies deepest under the sea.
    TOptional<int64> OceanSeed;
    int32 OceanTop = 62;
    int32 AtOrigin = 0;
    for (const int64 Seed : Seeds)
    {
        const FVoxelDensityTerrain Terrain(Overworld, uint64(Seed), Table);
        if (!TestTrue(TEXT("The terrain builds"), Terrain.IsValid())) return false;
        const FIntPoint Pick = Terrain.GetSpawnFinder().FindClimateSpot();
        FIntVector Cell;
        bool bDry = false;
        const double Start = FPlatformTime::Seconds();
        const bool bFound = Terrain.FindSpawn(Cell, bDry);
        const double Ms = (FPlatformTime::Seconds() - Start) * 1000;
        const int32 OriginTop = Terrain.GetSurfaceHeight(0, 0);
        const int32 Top = Terrain.GetSurfaceHeight(Cell.X, Cell.Y);
        AddInfo(FString::Printf(TEXT("Seed %lld: pick %s, spawn %s, %.0f blocks out, top %d, origin top %d, %.1f ms"),
            Seed, *Pick.ToString(), *Cell.ToString(), FVector2D(Cell.X, Cell.Y).Length(), Top, OriginTop, Ms));
        TestTrue(FString::Printf(TEXT("Seed %lld: dry land found"), Seed), bFound && bDry);
        TestTrue(FString::Printf(TEXT("Seed %lld: its top solid block is out of the water"), Seed), Top >= 62);
        TestEqual(FString::Printf(TEXT("Seed %lld: the feet are on it"), Seed), Cell.Z, Top + 1);
        TestTrue(
            FString::Printf(TEXT("Seed %lld: within 2650 blocks"), Seed), FVector2D(Cell.X, Cell.Y).Length() <= 2650);
        if (OriginTop < OceanTop)
        {
            OceanSeed = Seed;
            OceanTop = OriginTop;
        }
        AtOrigin += Pick == FIntPoint::ZeroValue;
    }
    AddInfo(FString::Printf(TEXT("%d of %d picks at the origin"), AtOrigin, Seeds.Num()));
    TestTrue(TEXT("A seed has the sea at its origin"), OceanSeed.IsSet());

    // Each block column's height from one density equals GetSurfaceHeight's.
    {
        const FVoxelDensityTerrain Terrain(Overworld, uint64(DevSeed), Table);
        int32 Heights[256];
        Terrain.GetSurfaceHeights(FIntPoint(8, 6), Heights);
        for (const int32 Index : {0, 17, 130, 255})
            TestEqual(FString::Printf(TEXT("Block column %d of one density"), Index), Heights[Index],
                Terrain.GetSurfaceHeight(8 * 16 + Index % 16, 6 * 16 + Index / 16));
    }

    AddInfo(FString::Printf(TEXT("Deepest origin: seed %lld, top %d"), OceanSeed.Get(0), OceanTop));
    // Timing, per preset: the dev seed and the deepest origin's seed. When nothing near the pick is dry (rare), the
    // whole search (Find) includes the nearest-land fallback.
    for (const FVoxelTerrainPreset* Preset : {&Overworld, &Large, &Amplified})
        // large_biomes seed 1: nothing near the pick is dry, so Find falls back to the nearest land.
        for (const int64 Seed : {DevSeed, OceanSeed.Get(0), int64(1)})
        {
            const FVoxelDensityTerrain Terrain(*Preset, uint64(Seed), Table);
            FIntVector Cell;
            bool bDry = false;
            const double Start = FPlatformTime::Seconds();
            Terrain.FindSpawn(Cell, bDry);
            const double Middle = FPlatformTime::Seconds();
            const FVoxelSpawn Spawn = FVoxelSpawnFinder::Find(Terrain, {});
            AddInfo(FString::Printf(TEXT("%s seed %lld: FindSpawn %.1f ms (%s%s); Find %.1f ms, spawn %s%s"),
                *Preset->Name, Seed, (Middle - Start) * 1000, *Cell.ToString(), bDry ? TEXT("") : TEXT(", none dry"),
                (FPlatformTime::Seconds() - Middle) * 1000, *Spawn.Cell.ToString(),
                Spawn.bStandOnBlocks ? TEXT("") : TEXT(", on the water")));
            if (Spawn.bStandOnBlocks)
                TestTrue(FString::Printf(TEXT("%s seed %lld: out of the water"), *Preset->Name, Seed),
                    Terrain.GetSurfaceHeight(Spawn.Cell.X, Spawn.Cell.Y) >= 62);
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSpawnDevSeedTest, "Unnamed.Voxel.Spawn.DevSeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSpawnDevSeedTest::RunTest(const FString& Parameters)
{
    // The terrain the dev seed's world streams.
    const int64 DevSeed = FVoxelSeed::Parse(TEXT("Unnamed")).Value;
    FString Error;
    TSharedPtr<const IVoxelTerrain> Terrain = VoxelTerrainFactory::Make(
        nullptr, DevSeed, *VoxelBlocks::GetDefault(), VoxelWorldgen::GetReferenceRoot(), Error);
    TSharedPtr<const IVoxelTerrain> Again = VoxelTerrainFactory::Make(
        nullptr, DevSeed, *VoxelBlocks::GetDefault(), VoxelWorldgen::GetReferenceRoot(), Error);
    if (!TestTrue(TEXT("The dev seed's terrain builds"), Terrain.IsValid() && Again.IsValid()))
    {
        AddError(Error);
        return false;
    }
    // The climate search: the same cell twice and at any view radius.
    FVoxelSpawnRequest Near, Far;
    Near.ViewReach = 4 * VoxelConstants::ColumnSize;
    Far.ViewReach = 12 * VoxelConstants::ColumnSize;
    const FVoxelSpawn Spawn = FVoxelSpawnFinder::Find(*Terrain, Near);
    AddInfo(FString::Printf(TEXT("The dev seed's climate spawn: %s"), *Spawn.Cell.ToString()));
    TestEqual(TEXT("The same at view radius 12"), FVoxelSpawnFinder::Find(*Terrain, Far).Cell, Spawn.Cell);
    TestEqual(TEXT("The same on a second terrain"), FVoxelSpawnFinder::Find(*Again, Near).Cell, Spawn.Cell);
    TestTrue(TEXT("On dry land"), Spawn.bStandOnBlocks);
    // Recorded on the first run (2026-10-11, Native-20261011-114150-spawn-tests); never edit to pass a test.
    TestEqual(TEXT("The dev seed's climate spawn"), Spawn.Cell, FIntVector(16, -32, 67));
    // The nearest-land search (-VoxelSpawnAt=0,0, captures) keeps the spawn from before the climate search.
    FVoxelSpawnRequest Capture = Far;
    Capture.bNearest = true;
    TestEqual(TEXT("-VoxelSpawnAt=0,0: the spawn before the climate search"),
        FVoxelSpawnFinder::Find(*Terrain, Capture).Cell, SpawnTestNearestCell);
    return true;
}

#endif
