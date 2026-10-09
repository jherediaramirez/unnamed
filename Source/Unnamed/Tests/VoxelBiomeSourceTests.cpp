#include "Async/ParallelFor.h"
#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeStorage.h"
#include "Blocks/VoxelBlockTable.h"
#include "Density/VoxelDensityGraph.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelClimate.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Voxel/VoxelBiomeDebug.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr uint64 BiomeSourceTestSeed = 263;
    const TCHAR* const BiomeSourceTestPresets[3] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};
    // Unnamed.Voxel.Density.Golden's positions.
    const FIntPoint BiomeSourceTestColumns[] = {{0, 0}, {1, 0}, {-3, 7}, {100, -50}};
    // Biome hashes (FNV-1a over each column's quarts, in that order) for seed 263, recorded once (phase 5, session 4).
    constexpr uint64 BiomeSourceTestGolden[3] = {0xe3013e9a963d42feull, 0x6d37af6332445934ull, 0xbdc71ddbc5a89adfull};
    // The same columns' jittered block biomes (every block on every seventh layer), which pin the jitter as well.
    constexpr uint64 BiomeSourceTestBlockGolden[3] = {
        0x9aa40aa00117a819ull, 0x83db5e7e69587903ull, 0x6bbcb6f996e2a018ull};
    // The lookup samples where the jar's indexed search answers another biome than brute force: all ties.
    constexpr int32 BiomeSourceTestIndexedTies = 95;

    // The overworld parameter list and a source over Preset for seed 263.
    bool BiomeSourceTestMake(FAutomationTestBase& Test, const TCHAR* PresetName, FVoxelBiomeSource& OutSource,
        FVoxelTerrainPreset* OutPreset = nullptr)
    {
        const FString Root = VoxelWorldgen::GetReferenceRoot();
        FVoxelBiomeTable Table;
        FVoxelTerrainPreset Preset;
        FString Error;
        const bool bRead = VoxelWorldgen::ReadPreset(Root, PresetName, Preset, Error) &&
            VoxelWorldgen::ReadBiomeParameters(Root, TEXT("overworld"), Table, Error);
        if (!Test.TestTrue(
                FString::Printf(TEXT("%s and the overworld parameter list read (%s)"), PresetName, *Error), bRead))
            return false;
        const bool bMade = OutSource.Initialize(Table, Preset, BiomeSourceTestSeed, &Error);
        if (OutPreset) *OutPreset = MoveTemp(Preset);
        return Test.TestTrue(FString::Printf(TEXT("the %s biome source initializes (%s)"), PresetName, *Error), bMade);
    }

    TSharedPtr<FJsonObject> BiomeSourceTestFacts(const TCHAR* File)
    {
        FString Text;
        TSharedPtr<FJsonObject> Object;
        if (!FFileHelper::LoadFileToString(
                Text, *FPaths::Combine(VoxelWorldgen::GetReferenceRoot(), TEXT("facts"), File)) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object))
            return nullptr;
        return Object;
    }

    bool BiomeSourceTestSame(TConstArrayView<uint8> A, TConstArrayView<uint8> B)
    {
        return A.Num() == B.Num() && FMemory::Memcmp(A.GetData(), B.GetData(), A.Num()) == 0;
    }

    uint64 BiomeSourceTestHash(uint64 Hash, TConstArrayView<uint8> Bytes)
    {
        for (const uint8 Byte : Bytes)
        {
            Hash ^= Byte;
            Hash *= 0x100000001b3ull;
        }
        return Hash;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelBiomeLookupTest, "Unnamed.Voxel.Biome.Lookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelBiomeLookupTest::RunTest(const FString& Parameters)
{
    FVoxelBiomeSource Source;
    if (!BiomeSourceTestMake(*this, TEXT("overworld"), Source)) return false;
    const FVoxelBiomeTable& Table = Source.GetTable();
    // 26.3's overworld points form four depth groups: 0, 1, 0.2 to 0.9 (the cave biomes) and 1.1 (the deep dark).
    TestEqual(TEXT("depth groups"), Source.GetGroupCount(), 4);

    const TSharedPtr<FJsonObject> Samples = BiomeSourceTestFacts(TEXT("biome_lookup_samples.json"));
    const TSharedPtr<FJsonObject> ParameterFacts = BiomeSourceTestFacts(TEXT("biome_parameters.json"));
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* FactBiomes = nullptr;
    if (!TestTrue(TEXT("the lookup samples and the parameter facts read"),
            Samples && ParameterFacts && Samples->TryGetArrayField(TEXT("samples"), Rows) &&
                ParameterFacts->TryGetArrayField(TEXT("biomes"), FactBiomes)))
        return false;
    TestEqual(TEXT("lookup samples"), Rows->Num(), 5120);

    // The facts name biomes by their index in biome_parameters.json's list; ours are the table's.
    TArray<int32> FactToTable;
    for (const TSharedPtr<FJsonValue>& Name : *FactBiomes)
        FactToTable.Add(Table.FindBiome(Name->AsString()));
    TestFalse(TEXT("every fact biome is in the table"), FactToTable.Contains(INDEX_NONE));

    int32 Wrong = 0, WrongBrute = 0, Ties = 0, BadTies = 0;
    for (int32 Row = 0; Row < Rows->Num(); ++Row)
    {
        const TArray<TSharedPtr<FJsonValue>>& Values = (*Rows)[Row]->AsArray();
        if (!TestEqual(TEXT("sample columns"), Values.Num(), 8)) return false;
        FVoxelBiomeTarget Target;
        for (int32 Axis = 0; Axis < int32(EVoxelClimateAxis::Count); ++Axis)
            Target.Values[Axis] = int64(Values[Axis]->AsNumber());
        const int32 Brute = FactToTable[int32(Values[6]->AsNumber())];
        const int32 Indexed = FactToTable[int32(Values[7]->AsNumber())];

        const FVoxelBiomeMatch Pruned = Source.Find(Target);
        const FVoxelBiomeMatch Scan = Source.FindBruteForce(Target);
        if (Source.GetBiome(Pruned) != Brute || Pruned.Point != Scan.Point || Pruned.Fitness != Scan.Fitness)
        {
            if (++Wrong <= 5)
                AddError(FString::Printf(
                    TEXT("sample %d: pruned %s (point %d, fitness %lld), brute force %s (point %d, "
                         "fitness %lld), jar %s"),
                    Row, *Table.Biomes[Source.GetBiome(Pruned)].Name, Pruned.Point, Pruned.Fitness,
                    *Table.Biomes[Source.GetBiome(Scan)].Name, Scan.Point, Scan.Fitness, *Table.Biomes[Brute].Name));
        }
        WrongBrute += Source.GetBiome(Scan) != Brute;
        if (Indexed == Brute) continue;
        // The jar's indexed answer differs only on ties: its biome has a point exactly as fit as the best.
        ++Ties;
        bool bTie = false;
        for (const FVoxelBiomeParameter& Point : Table.Points)
            bTie |= Point.Biome == Indexed && FVoxelBiomeSource::GetFitness(Point, Target) == Scan.Fitness;
        BadTies += !bTie;
    }
    TestEqual(TEXT("samples where the pruned search differs from the jar's brute force"), Wrong, 0);
    TestEqual(TEXT("samples where our brute force differs from the jar's"), WrongBrute, 0);
    TestEqual(TEXT("samples where the jar's indexed search differs"), Ties, BiomeSourceTestIndexedTies);
    TestEqual(TEXT("...of which are not ties"), BadTies, 0);

    // A fixed source answers its one biome everywhere.
    FVoxelBiomeSource Fixed;
    const int32 Plains = Table.FindBiome(TEXT("minecraft:plains"));
    FString Error;
    const bool bFixed = Fixed.InitializeFixed(Table, Plains, &Error);
    if (TestTrue(FString::Printf(TEXT("a fixed source initializes (%s)"), *Error), bFixed))
    {
        TestEqual(TEXT("a fixed source's biome"), Fixed.GetBiome(Fixed.Find(FVoxelBiomeTarget())), Plains);
        FVoxelDensityScratch Scratch;
        FVoxelBiomeStorage Storage;
        Fixed.FillColumn(FIntPoint(5, -2), Scratch, Storage);
        TestTrue(TEXT("a fixed source fills every quart with it"),
            !Storage.IsEmpty() &&
                !Storage.GetQuarts().ContainsByPredicate([Plains](uint8 Biome) { return Biome != Plains; }));
    }
    TestFalse(TEXT("a fixed source refuses a biome outside the table"),
        FVoxelBiomeSource().InitializeFixed(Table, Table.Biomes.Num(), &Error));

    // The debug maps' palette (Config/DefaultGame.ini) gives every biome its own colour; the on-screen views start off.
    TSet<uint32> Colors;
    TArray<FString> Unlisted;
    for (const FVoxelBiome& Biome : Table.Biomes)
    {
        bool bListed = false;
        Colors.Add(VoxelBiomeDebug::GetColor(Biome.Name, &bListed).DWColor());
        if (!bListed) Unlisted.Add(Biome.Name);
    }
    TestEqual(FString::Printf(TEXT("biomes without a map colour (%s)"), *FString::Join(Unlisted, TEXT(", "))),
        Unlisted.Num(), 0);
    TestEqual(TEXT("distinct map colours"), Colors.Num(), Table.Biomes.Num());
    TestFalse(TEXT("the biome debug views are off by default"), VoxelBiomeDebug::IsOverlayOn());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelBiomeExactTest, "Unnamed.Voxel.Biome.Exact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelBiomeExactTest::RunTest(const FString& Parameters)
{
    for (const TCHAR* PresetName : BiomeSourceTestPresets)
    {
        FVoxelBiomeSource Source;
        FVoxelTerrainPreset Preset;
        if (!BiomeSourceTestMake(*this, PresetName, Source, &Preset)) continue;
        FVoxelClimateSampler Climate;
        FString Error;
        const bool bClimate = Climate.Initialize(Preset, BiomeSourceTestSeed, &Error);
        if (!TestTrue(FString::Printf(TEXT("%s climate (%s)"), PresetName, *Error), bClimate)) continue;

        // Every quart of the golden columns: the pruned search equals brute force.
        TArray<int64> Differ;
        Differ.SetNumZeroed(UE_ARRAY_COUNT(BiomeSourceTestColumns));
        TArray<TSet<uint8>> Seen;
        Seen.SetNum(UE_ARRAY_COUNT(BiomeSourceTestColumns));
        ParallelFor(UE_ARRAY_COUNT(BiomeSourceTestColumns),
            [&](int32 Index)
            {
                FVoxelDensityScratch Scratch;
                FVoxelBiomeStorage Pruned, Brute;
                Source.FillColumn(BiomeSourceTestColumns[Index], Scratch, Pruned, EVoxelBiomeFill::Pruned);
                Source.FillColumn(BiomeSourceTestColumns[Index], Scratch, Brute, EVoxelBiomeFill::BruteForce);
                for (int32 Quart = 0; Quart < FVoxelBiomeStorage::Volume; ++Quart)
                {
                    Differ[Index] += Pruned.GetQuarts()[Quart] != Brute.GetQuarts()[Quart];
                    Seen[Index].Add(Pruned.GetQuarts()[Quart]);
                }
            });
        int64 Total = 0;
        TSet<uint8> AllBiomes;
        for (int32 Index = 0; Index < Differ.Num(); ++Index)
        {
            Total += Differ[Index];
            AllBiomes.Append(Seen[Index]);
        }
        TestEqual(FString::Printf(TEXT("%s: quarts where pruned and brute force differ"), PresetName), Total, int64(0));
        TestFalse(FString::Printf(TEXT("%s: every quart has a biome"), PresetName),
            AllBiomes.Contains(FVoxelBiomeStorage::None));
        AddInfo(FString::Printf(TEXT("%s: %d biomes in %d columns"), PresetName, AllBiomes.Num(), Differ.Num()));

        // The column's depths equal the density evaluator's depth function, and a target sampled at a quart (the maps'
        // and the readout's path) finds the biome the column stored there.
        FVoxelDensityScratch Scratch;
        FVoxelBiomeStorage Storage;
        const FIntPoint Column = BiomeSourceTestColumns[3];
        Source.FillColumn(Column, Scratch, Storage);
        int32 DepthMismatches = 0, TargetMismatches = 0;
        for (int32 QuartColumn = 0; QuartColumn < FVoxelBiomeStorage::Width * FVoxelBiomeStorage::Width;
            QuartColumn += 6)
        {
            const int32 QX = QuartColumn % FVoxelBiomeStorage::Width, QY = QuartColumn / FVoxelBiomeStorage::Width;
            const double X = Column.X * 16. + QX * 4, Y = Column.Y * 16. + QY * 4;
            FVoxelClimateSample Sample;
            Climate.Sample(&X, &Y, 1, Scratch, MakeArrayView(&Sample, 1));
            for (int32 Layer = 0; Layer < FVoxelBiomeStorage::Layers; Layer += 5)
            {
                const double Z = VoxelConstants::MinZ + Layer * 4.;
                const float Depth = Source.GetDepth(Sample[EVoxelClimate::Offset], Z);
                const float Expected = Climate.SampleDepth(X, Y, Z, Scratch);
                DepthMismatches += FMemory::Memcmp(&Depth, &Expected, sizeof(float)) != 0;
                FVoxelBiomeTarget Target;
                Source.SampleTargets(&X, &Y, &Z, 1, Scratch, MakeArrayView(&Target, 1));
                TargetMismatches += Source.GetBiome(Source.Find(Target)) != Storage.GetQuart(QX, QY, Layer);
            }
        }
        TestEqual(
            FString::Printf(TEXT("%s: depths that differ from the depth function"), PresetName), DepthMismatches, 0);
        TestEqual(FString::Printf(TEXT("%s: sampled targets whose biome differs from the column's"), PresetName),
            TargetMismatches, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelBiomeJitterTest, "Unnamed.Voxel.Biome.Jitter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelBiomeJitterTest::RunTest(const FString& Parameters)
{
    FVoxelBiomeSource Source;
    FVoxelTerrainPreset Preset;
    if (!BiomeSourceTestMake(*this, TEXT("overworld"), Source, &Preset)) return false;
    constexpr int32 Width = FVoxelBiomeStorage::Width, Layers = FVoxelBiomeStorage::Layers;

    // A 5 x 5 area filled forward on one thread and backwards across workers: equal column for column.
    constexpr int32 Side = 5, Count = Side * Side;
    TArray<FVoxelBiomeStorage> Forward, Backward;
    Forward.SetNum(Count);
    Backward.SetNum(Count);
    const auto PositionOf = [](int32 Index) { return FIntPoint(Index % Side - 2, Index / Side - 2); };
    {
        FVoxelDensityScratch Scratch;
        for (int32 Index = 0; Index < Count; ++Index)
            Source.FillColumn(PositionOf(Index), Scratch, Forward[Index]);
    }
    ParallelFor(Count,
        [&](int32 Step)
        {
            FVoxelDensityScratch Scratch;
            const int32 Index = Count - 1 - Step;
            Source.FillColumn(PositionOf(Index), Scratch, Backward[Index]);
        });
    int32 OrderMismatches = 0;
    for (int32 Index = 0; Index < Count; ++Index)
        OrderMismatches += !BiomeSourceTestSame(Forward[Index].GetQuarts(), Backward[Index].GetQuarts());
    TestEqual(TEXT("columns that differ between the two load orders"), OrderMismatches, 0);

    // A column's fifth row and column of quarts are its neighbours' first: the same pure values.
    int32 RingMismatches = 0;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FIntPoint Position = PositionOf(Index);
        if (Position.X < 2)
            for (int32 Layer = 0; Layer < Layers; ++Layer)
                for (int32 QY = 0; QY < Width; ++QY)
                    RingMismatches +=
                        Forward[Index].GetQuart(Width - 1, QY, Layer) != Forward[Index + 1].GetQuart(0, QY, Layer);
        if (Position.Y < 2)
            for (int32 Layer = 0; Layer < Layers; ++Layer)
                for (int32 QX = 0; QX < Width; ++QX)
                    RingMismatches +=
                        Forward[Index].GetQuart(QX, Width - 1, Layer) != Forward[Index + Side].GetQuart(QX, 0, Layer);
    }
    TestEqual(TEXT("ring quarts that differ from the neighbour's own"), RingMismatches, 0);

    // Every block takes one of its cell's 8 corners; some take another corner than their own cell's, and on borders
    // that moves the biome.
    int64 Blocks = 0, OutsideCell = 0, Moved = 0, Recolored = 0, NotStored = 0;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FVoxelBiomeStorage& Storage = Forward[Index];
        for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; Z += 3)
            for (int32 LocalY = 0; LocalY < VoxelConstants::ColumnSize; ++LocalY)
                for (int32 LocalX = 0; LocalX < VoxelConstants::ColumnSize; ++LocalX)
                {
                    const FIntVector Cell(LocalX / 4, LocalY / 4, (Z - VoxelConstants::MinZ) / 4);
                    const FIntVector Quart = Storage.GetBlockQuart(LocalX, LocalY, Z);
                    const FIntVector Step = Quart - Cell;
                    ++Blocks;
                    OutsideCell += Step.X < 0 || Step.X > 1 || Step.Y < 0 || Step.Y > 1 || Step.Z < 0 || Step.Z > 1 ||
                        Quart.Z >= Layers;
                    Moved += Step != FIntVector::ZeroValue;
                    const int32 Biome = Storage.GetBlock(LocalX, LocalY, Z);
                    NotStored += Biome != Storage.GetQuart(Quart.X, Quart.Y, Quart.Z);
                    Recolored += Biome != Storage.GetQuart(Cell.X, Cell.Y, Cell.Z);
                }
    }
    TestEqual(TEXT("blocks outside their cell's corners"), OutsideCell, int64(0));
    TestEqual(TEXT("blocks whose biome is not their quart's"), NotStored, int64(0));
    TestTrue(FString::Printf(TEXT("the jitter moves blocks off their own corner (%lld of %lld)"), Moved, Blocks),
        Moved > Blocks / 4 && Moved < Blocks);
    TestTrue(FString::Printf(TEXT("the jitter moves borders (%lld blocks take a neighbour's biome)"), Recolored),
        Recolored > 0);

    // The jitter is a pure function, inside its bounds.
    bool bBounded = true, bRepeats = true;
    for (int32 Quart = -50; Quart < 50; ++Quart)
    {
        const FVector3f Jitter = FVoxelBiomeStorage::GetJitter(BiomeSourceTestSeed, Quart, -Quart, Quart / 3);
        bRepeats &= Jitter == FVoxelBiomeStorage::GetJitter(BiomeSourceTestSeed, Quart, -Quart, Quart / 3);
        for (int32 Axis = 0; Axis < 3; ++Axis)
            bBounded &= Jitter[Axis] >= -.45f && Jitter[Axis] < .45f;
    }
    TestTrue(TEXT("the jitter repeats"), bRepeats);
    TestTrue(TEXT("the jitter stays within 0.45 quart"), bBounded);

    // The terrain stores the same biomes in its columns, and a block reads them through the column. Its blocks do not
    // depend on biomes yet.
    const TSharedRef<const FVoxelBlockTable> BlockTable = VoxelBlocks::GetDefault();
    FVoxelDensityTerrainSettings Settings;
    Settings.bCaves = false;
    const FVoxelDensityTerrain Terrain(Preset, BiomeSourceTestSeed, *BlockTable, Settings, &Source.GetTable());
    if (TestTrue(
            FString::Printf(TEXT("the terrain with biomes is valid (%s)"), *Terrain.GetError()), Terrain.IsValid()))
    {
        FVoxelColumn Column, Plain;
        TArray<uint16> Dense, PlainDense;
        const FIntPoint Position = PositionOf(7);
        Terrain.Generate(Position, Column, Dense);
        TestTrue(TEXT("the column's biomes are the source's"),
            BiomeSourceTestSame(Column.GetBiomes().GetQuarts(), Forward[7].GetQuarts()));
        TestEqual(TEXT("a block's biome through the column"), Column.GetBiome(9, 3, 70), Forward[7].GetBlock(9, 3, 70));
        TestTrue(TEXT("the column counts its biomes' memory"),
            Column.GetAllocatedSize() >= SIZE_T(FVoxelBiomeStorage::Volume));
        FVoxelDensityTerrainOverrides Off;
        Off.BiomeFill = EVoxelBiomeFill::Off;
        Terrain.Generate(Position, Plain, PlainDense, nullptr, Off);
        TestTrue(TEXT("biomes off leaves the column without biomes"), Plain.GetBiomes().IsEmpty());
        TestEqual(TEXT("...and a block without a biome"), Plain.GetBiome(9, 3, 70), int32(INDEX_NONE));
        TestTrue(TEXT("...and the same blocks"), Dense == PlainDense);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelBiomeGoldenTest, "Unnamed.Voxel.Biome.Golden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelBiomeGoldenTest::RunTest(const FString& Parameters)
{
    for (int32 PresetIndex = 0; PresetIndex < 3; ++PresetIndex)
    {
        const TCHAR* const PresetName = BiomeSourceTestPresets[PresetIndex];
        FVoxelBiomeSource Source;
        if (!BiomeSourceTestMake(*this, PresetName, Source)) continue;
        uint64 Hash = 0xcbf29ce484222325ull, BlockHash = 0xcbf29ce484222325ull;
        TMap<uint8, int32> Counts;
        FVoxelDensityScratch Scratch;
        for (const FIntPoint& Position : BiomeSourceTestColumns)
        {
            FVoxelBiomeStorage Storage;
            Source.FillColumn(Position, Scratch, Storage);
            Hash = BiomeSourceTestHash(Hash, Storage.GetQuarts());
            for (const uint8 Biome : Storage.GetQuarts())
                ++Counts.FindOrAdd(Biome);
            TArray<uint8> Blocks;
            for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; Z += 7)
                for (int32 LocalY = 0; LocalY < VoxelConstants::ColumnSize; ++LocalY)
                    for (int32 LocalX = 0; LocalX < VoxelConstants::ColumnSize; ++LocalX)
                        Blocks.Add(uint8(Storage.GetBlock(LocalX, LocalY, Z)));
            BlockHash = BiomeSourceTestHash(BlockHash, Blocks);
        }
        Counts.ValueSort([](int32 A, int32 B) { return A > B; });
        FString Info = FString::Printf(TEXT("%s: 0x%016llx, blocks 0x%016llx;"), PresetName,
            static_cast<unsigned long long>(Hash), static_cast<unsigned long long>(BlockHash));
        for (const TPair<uint8, int32>& Count : Counts)
            Info += FString::Printf(TEXT(" %s %d"),
                Source.GetTable().Biomes.IsValidIndex(Count.Key)
                    ? *Source.GetTable().Biomes[Count.Key].Name.RightChop(10)
                    : TEXT("none"),
                Count.Value);
        AddInfo(Info);
        if (Hash != BiomeSourceTestGolden[PresetIndex])
            AddError(FString::Printf(TEXT("%s: biome hash 0x%016llx, golden 0x%016llx"), PresetName,
                static_cast<unsigned long long>(Hash),
                static_cast<unsigned long long>(BiomeSourceTestGolden[PresetIndex])));
        if (BlockHash != BiomeSourceTestBlockGolden[PresetIndex])
            AddError(FString::Printf(TEXT("%s: block biome hash 0x%016llx, golden 0x%016llx"), PresetName,
                static_cast<unsigned long long>(BlockHash),
                static_cast<unsigned long long>(BiomeSourceTestBlockGolden[PresetIndex])));
    }
    return true;
}

#endif
