#include "Async/ParallelFor.h"
#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelFeatureOrder.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Generation/VoxelArea.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Hash/CityHash.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/VoxelColumn.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "VoxelBench.h"
#include "VoxelTestBlocks.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // For manual runs: 0 none, 1 merge in arrival order, 2 Decorate reads neighbours' final columns, 3 one shared
    // stream, 4 swapped order indices, 5 supports_vegetation left out of the survival table, 6 writes no longer raise
    // the live heightmaps (S8). With 0, every test must pass.
    constexpr int32 DecorationTestMutation = 0;
    constexpr uint64 DecorationTestSeed = 263;
    const TCHAR* const DecorationTestPresets[3] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};

    using FColumnPtr = TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe>;

    FVoxelDecorationOptions DecorationTestOptions()
    {
        FVoxelDecorationOptions Options;
        if (DecorationTestMutation == 3) Options.Mutation = EVoxelDecorationMutation::SharedStream;
        if (DecorationTestMutation == 4) Options.Mutation = EVoxelDecorationMutation::SwappedOrder;
        if (DecorationTestMutation == 5) Options.Mutation = EVoxelDecorationMutation::DroppedSupportTag;
        if (DecorationTestMutation == 6) Options.Mutation = EVoxelDecorationMutation::FrozenHeightmaps;
        return Options;
    }

    // The real block table, or a warning when the imported blocks are missing.
    bool DecorationTestHasBlocks(FAutomationTestBase& Test)
    {
        if (VoxelBlocks::GetDefault()->FindState(TEXT("stone"))) return true;
        Test.AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }

    struct FDecorationTestWorld
    {
        TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
        FVoxelTerrainPreset Preset;
        FVoxelBiomeTable Biomes;
        FVoxelSurfaceInputs Surface;
        FVoxelFeatureGraph Graph;
        TSharedPtr<FVoxelDensityTerrain> Terrain;
        TSharedPtr<FVoxelDecorator> Decorator;
    };

    bool InitDecorator(FAutomationTestBase& Test, FDecorationTestWorld& World, const FVoxelDecorationOptions& Options)
    {
        FString Error;
        World.Decorator = MakeShared<FVoxelDecorator>();
        return Test.TestTrue(FString::Printf(TEXT("the decorator compiles (%s)"), *Error),
            World.Decorator->Initialize(World.Graph, *World.Table, World.Terrain->GetBiomeSource()->GetTable(),
                World.Terrain->GetSeed(), Options, &Error));
    }

    bool MakeDecorationWorld(FAutomationTestBase& Test, const TCHAR* PresetName, uint64 Seed, FDecorationTestWorld& Out,
        const FVoxelDecorationOptions& Options, bool bCaves = true, bool bDecorator = true)
    {
        FString Error;
        if (!Test.TestTrue(FString::Printf(TEXT("%s inputs read (%s)"), PresetName, *Error),
                VoxelTerrainFactory::ReadInputs(
                    VoxelWorldgen::GetReferenceRoot(), PresetName, Out.Preset, Out.Biomes, Out.Surface, Error)))
            return false;
        if (!Test.TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
                VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Out.Graph, Error)))
            return false;
        FVoxelDensityTerrainSettings Settings;
        Settings.bCaves = bCaves;
        Out.Terrain =
            MakeShared<FVoxelDensityTerrain>(Out.Preset, Seed, *Out.Table, Settings, &Out.Biomes, &Out.Surface);
        if (!Test.TestTrue(FString::Printf(TEXT("%s terrain is valid (%s)"), PresetName, *Out.Terrain->GetError()),
                Out.Terrain->IsValid()))
            return false;
        return !bDecorator || InitDecorator(Test, Out, Options);
    }

    // A square of generated columns, (2 Radius + 1)^2 around Center.
    struct FSnapshotSquare
    {
        FIntPoint Center = FIntPoint::ZeroValue;
        int32 Radius = 0;
        TArray<FColumnPtr> Columns;

        int32 GetSide() const { return 2 * Radius + 1; }
        const FVoxelColumn* At(FIntPoint Position) const
        {
            const FIntPoint Local = Position - Center + FIntPoint(Radius, Radius);
            return Columns[Local.Y * GetSide() + Local.X].Get();
        }
    };

    void GenerateSnapshots(const IVoxelTerrain& Terrain, FIntPoint Center, int32 Radius, FSnapshotSquare& Out)
    {
        Out.Center = Center;
        Out.Radius = Radius;
        Out.Columns.SetNum(Out.GetSide() * Out.GetSide());
        ParallelFor(Out.Columns.Num(),
            [&](int32 Index)
            {
                TArray<uint16> Dense;
                Out.Columns[Index] = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>();
                Terrain.GenerateSnapshot(
                    Center + FIntPoint(Index % Out.GetSide() - Radius, Index / Out.GetSide() - Radius),
                    *Out.Columns[Index], Dense);
            });
    }

    // The final columns' hashes combined in position order.
    template <typename ColumnType>
    uint64 HashColumns(const TArray<TSharedPtr<ColumnType, ESPMode::ThreadSafe>>& Columns)
    {
        TArray<uint64> Hashes;
        for (const TSharedPtr<ColumnType, ESPMode::ThreadSafe>& Column : Columns)
            Hashes.Add(VoxelBench::HashColumnBlocks(*Column));
        return CityHash64(reinterpret_cast<const char*>(Hashes.GetData()), uint32(Hashes.Num() * sizeof(uint64)));
    }

    // Decorate on radius + 1 and Finalize on the radius over snapshots of radius + 2, each stage across the workers.
    TArray<FColumnPtr> RunStages(
        const FVoxelDecorator& Decorator, const FSnapshotSquare& Snapshots, int32 Radius, int32* OutDropped = nullptr)
    {
        const int32 DecorateSide = 2 * Radius + 3, FinalSide = 2 * Radius + 1;
        TArray<FVoxelWriteLog> Logs;
        Logs.SetNum(DecorateSide * DecorateSide);
        ParallelFor(Logs.Num(),
            [&](int32 Index)
            {
                const FIntPoint Position =
                    Snapshots.Center + FIntPoint(Index % DecorateSide - Radius - 1, Index / DecorateSide - Radius - 1);
                const FVoxelColumn* Around[9];
                for (int32 DY = -1; DY <= 1; ++DY)
                    for (int32 DX = -1; DX <= 1; ++DX)
                        Around[FVoxelWriteLog::GetBucket(DX, DY)] = Snapshots.At(Position + FIntPoint(DX, DY));
                Decorator.Decorate(Position, Around, Logs[Index]);
            });
        TArray<FColumnPtr> Final;
        Final.SetNum(FinalSide * FinalSide);
        ParallelFor(Final.Num(),
            [&](int32 Index)
            {
                const int32 X = Index % FinalSide - Radius, Y = Index / FinalSide - Radius;
                const FVoxelWriteBucket* Incoming[9];
                for (int32 DY = -1; DY <= 1; ++DY)
                    for (int32 DX = -1; DX <= 1; ++DX)
                        Incoming[FVoxelWriteLog::GetBucket(DX, DY)] =
                            &Logs[(Y + DY + Radius + 1) * DecorateSide + X + DX + Radius + 1]
                                 .Buckets[FVoxelWriteLog::GetBucket(-DX, -DY)];
                Final[Index] = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>();
                Decorator.Finalize(*Snapshots.At(Snapshots.Center + FIntPoint(X, Y)), Incoming, *Final[Index]);
            });
        if (OutDropped)
        {
            *OutDropped = 0;
            for (const FVoxelWriteLog& Log : Logs)
                *OutDropped += Log.Dropped;
        }
        return Final;
    }

    // The scheduler: Generate (optional), Decorate and Finalize jobs of a square of final columns, run by workers that
    // take the first ready job in a priority order.
    enum class EDecorationOrder : uint8
    {
        RowMajor,
        Reverse,
        SpiralIn,
        SpiralOut,
        Checker,
        DecoratesFirst,
        FinalizeFirst
    };

    const TCHAR* GetOrderName(EDecorationOrder Order)
    {
        static const TCHAR* const Names[] = {TEXT("row-major"), TEXT("reverse"), TEXT("spiral in"), TEXT("spiral out"),
            TEXT("checkerboard"), TEXT("decorates first"), TEXT("finalize first")};
        return Names[int32(Order)];
    }

    struct FJob
    {
        // 0 Generate, 1 Decorate, 2 Finalize.
        int32 Kind = 0;
        // Column relative to the centre.
        int32 X = 0, Y = 0;
    };

    // Pregenerated, when set, holds the snapshots (radius + 2) and Generate jobs are left out.
    TArray<FColumnPtr> RunSchedule(const FVoxelDecorator& Decorator, const IVoxelTerrain& Terrain,
        const FSnapshotSquare* Pregenerated, FIntPoint Center, int32 Radius, EDecorationOrder Order, int32 Workers,
        uint64 DelaySeed)
    {
        const int32 Sides[3] = {2 * Radius + 5, 2 * Radius + 3, 2 * Radius + 1};
        const bool bGenerate = Pregenerated == nullptr;
        const auto Index = [&](int32 Kind, int32 X, int32 Y)
        {
            const int32 R = Radius + 2 - Kind;
            return (Y + R) * Sides[Kind] + X + R;
        };
        TArray<FColumnPtr> Generated, Final;
        Generated.SetNum(Sides[0] * Sides[0]);
        Final.SetNum(Sides[2] * Sides[2]);
        TArray<FVoxelWriteLog> Logs;
        Logs.SetNum(Sides[1] * Sides[1]);
        TArray<int32> DecorateSeq;
        DecorateSeq.Init(0, Logs.Num());
        TArray<uint8> Done[3], Taken[3];
        for (int32 Kind = 0; Kind < 3; ++Kind)
        {
            Done[Kind].Init(0, Sides[Kind] * Sides[Kind]);
            Taken[Kind].Init(0, Sides[Kind] * Sides[Kind]);
        }
        TArray<FJob> Jobs;
        for (int32 Kind = bGenerate ? 0 : 1; Kind < 3; ++Kind)
            for (int32 Y = -(Radius + 2 - Kind); Y <= Radius + 2 - Kind; ++Y)
                for (int32 X = -(Radius + 2 - Kind); X <= Radius + 2 - Kind; ++X)
                    Jobs.Add({Kind, X, Y});
        const auto MakeKey = [Order](const FJob& Job, int64(&Key)[6])
        {
            const int64 Ring = FMath::Max(FMath::Abs(Job.X), FMath::Abs(Job.Y));
            const int64 Angle = int64(FMath::Atan2(float(Job.Y), float(Job.X)) * 10000.f);
            const int64 Tie[3] = {Job.Kind, Job.Y, Job.X};
            int64 Lead[3] = {};
            switch (Order)
            {
            case EDecorationOrder::RowMajor:
                Lead[0] = Job.Y, Lead[1] = Job.X, Lead[2] = Job.Kind;
                break;
            case EDecorationOrder::Reverse:
                Lead[0] = -Job.Y, Lead[1] = -Job.X, Lead[2] = -Job.Kind;
                break;
            case EDecorationOrder::SpiralIn:
                Lead[0] = -Ring, Lead[1] = Angle, Lead[2] = Job.Kind;
                break;
            case EDecorationOrder::SpiralOut:
                Lead[0] = Ring, Lead[1] = Angle, Lead[2] = Job.Kind;
                break;
            case EDecorationOrder::Checker:
                Lead[0] = (Job.X + Job.Y) & 1, Lead[1] = Job.Y, Lead[2] = Job.X;
                break;
            case EDecorationOrder::DecoratesFirst:
                Lead[0] = Job.Kind, Lead[1] = Job.Y, Lead[2] = Job.X;
                break;
            case EDecorationOrder::FinalizeFirst:
                Lead[0] = -Job.Kind, Lead[1] = Job.Y, Lead[2] = Job.X;
                break;
            }
            for (int32 Part = 0; Part < 3; ++Part)
                Key[Part] = Lead[Part], Key[3 + Part] = Tie[Part];
        };
        Jobs.Sort(
            [&MakeKey](const FJob& A, const FJob& B)
            {
                int64 KeyA[6], KeyB[6];
                MakeKey(A, KeyA);
                MakeKey(B, KeyB);
                for (int32 Part = 0; Part < 6; ++Part)
                    if (KeyA[Part] != KeyB[Part]) return KeyA[Part] < KeyB[Part];
                return false;
            });

        const auto Snapshot = [&](int32 X, int32 Y) -> const FVoxelColumn*
        { return bGenerate ? Generated[Index(0, X, Y)].Get() : Pregenerated->At(Center + FIntPoint(X, Y)); };
        FCriticalSection Lock;
        int32 Remaining = Jobs.Num(), Counter = 0;
        const auto IsReady = [&](const FJob& Job)
        {
            if (Job.Kind == 0 || (Job.Kind == 1 && !bGenerate)) return true;
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                    if (!Done[Job.Kind - 1][Index(Job.Kind - 1, Job.X + DX, Job.Y + DY)]) return false;
            return true;
        };
        const auto Run = [&](const FJob& Job)
        {
            const int32 Own = Index(Job.Kind, Job.X, Job.Y);
            if (DelaySeed)
            {
                const uint64 Hash = VoxelRandom::Hash64(DelaySeed, 7, Job.Kind, Own);
                if (Hash & 1) FPlatformProcess::Sleep(float((Hash >> 8) % 201) * 1e-6f);
            }
            FColumnPtr Made;
            if (Job.Kind == 0)
            {
                TArray<uint16> Dense;
                Made = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>();
                Decorator.GenerateSnapshot(Terrain, Center + FIntPoint(Job.X, Job.Y), *Made, Dense);
            }
            else if (Job.Kind == 1)
            {
                const FVoxelColumn* Around[9];
                for (int32 DY = -1; DY <= 1; ++DY)
                    for (int32 DX = -1; DX <= 1; ++DX)
                        Around[FVoxelWriteLog::GetBucket(DX, DY)] = Snapshot(Job.X + DX, Job.Y + DY);
                // Mutation 2: a neighbour that has already decorated is handed with its own writes in its own
                // column applied (its decorated state), so what this Decorate reads depends on timing.
                TArray<TUniquePtr<FVoxelColumn>> Decorated;
                if (DecorationTestMutation == 2)
                    for (int32 DY = -1; DY <= 1; ++DY)
                        for (int32 DX = -1; DX <= 1; ++DX)
                        {
                            if ((DX == 0 && DY == 0) || FMath::Abs(Job.X + DX) > Radius + 1 ||
                                FMath::Abs(Job.Y + DY) > Radius + 1)
                                continue;
                            const int32 Neighbour = Index(1, Job.X + DX, Job.Y + DY);
                            {
                                FScopeLock Scope(&Lock);
                                if (!Done[1][Neighbour]) continue;
                            }
                            const FVoxelWriteBucket* OwnBucket[9] = {};
                            OwnBucket[FVoxelWriteLog::GetBucket(0, 0)] =
                                &Logs[Neighbour].Buckets[FVoxelWriteLog::GetBucket(0, 0)];
                            TUniquePtr<FVoxelColumn>& Column = Decorated.Add_GetRef(MakeUnique<FVoxelColumn>());
                            Decorator.Finalize(*Snapshot(Job.X + DX, Job.Y + DY), OwnBucket, *Column);
                            Around[FVoxelWriteLog::GetBucket(DX, DY)] = Column.Get();
                        }
                Decorator.Decorate(Center + FIntPoint(Job.X, Job.Y), Around, Logs[Own]);
            }
            else
            {
                const FVoxelWriteBucket* Incoming[9];
                TArray<int32> Arrival;
                for (int32 DY = -1; DY <= 1; ++DY)
                    for (int32 DX = -1; DX <= 1; ++DX)
                    {
                        const int32 Source = Index(1, Job.X + DX, Job.Y + DY);
                        Incoming[FVoxelWriteLog::GetBucket(DX, DY)] =
                            &Logs[Source].Buckets[FVoxelWriteLog::GetBucket(-DX, -DY)];
                        Arrival.Add(FVoxelWriteLog::GetBucket(DX, DY));
                    }
                if (DecorationTestMutation == 1)
                {
                    FScopeLock Scope(&Lock);
                    Arrival.Sort(
                        [&](int32 A, int32 B)
                        {
                            const auto SeqOf = [&](int32 Bucket)
                            { return DecorateSeq[Index(1, Job.X + Bucket % 3 - 1, Job.Y + Bucket / 3 - 1)]; };
                            return SeqOf(A) < SeqOf(B);
                        });
                }
                else
                    Arrival.Reset();
                Made = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>();
                Decorator.Finalize(*Snapshot(Job.X, Job.Y), Incoming, *Made, nullptr, Arrival);
                Decorator.FinishColumn(Terrain, *Made);
            }
            FScopeLock Scope(&Lock);
            if (Job.Kind == 0) Generated[Own] = Made;
            if (Job.Kind == 1) DecorateSeq[Own] = ++Counter;
            if (Job.Kind == 2) Final[Own] = Made;
            Done[Job.Kind][Own] = 1;
            --Remaining;
        };
        const auto Work = [&]()
        {
            for (;;)
            {
                const FJob* Next = nullptr;
                {
                    FScopeLock Scope(&Lock);
                    if (Remaining == 0) return;
                    for (const FJob& Job : Jobs)
                        if (!Taken[Job.Kind][Index(Job.Kind, Job.X, Job.Y)] && IsReady(Job))
                        {
                            Next = &Job;
                            Taken[Job.Kind][Index(Job.Kind, Job.X, Job.Y)] = 1;
                            break;
                        }
                }
                if (Next)
                    Run(*Next);
                else
                    FPlatformProcess::Sleep(0.f);
            }
        };
        if (Workers <= 1)
            Work();
        else
            ParallelFor(TEXT("DecorationTest.Schedule"), Workers, 1, [&](int32) { Work(); });
        return Final;
    }

    TSharedPtr<FJsonObject> LoadDecorationJson(const FString& File)
    {
        FString Text;
        TSharedPtr<FJsonObject> Object;
        if (!FFileHelper::LoadFileToString(Text, *File) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object))
            return nullptr;
        return Object;
    }

    // An object of counts by integer key, as the probes and histograms keep them.
    TMap<int32, int64> ReadCounts(const TSharedPtr<FJsonObject>& Object)
    {
        TMap<int32, int64> Counts;
        if (Object.IsValid())
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
                Counts.Add(FCString::Atoi(*Pair.Key), int64(Pair.Value->AsNumber()));
        return Counts;
    }

    // Wilson-Hilferty normal score of a chi-square with Df degrees of freedom.
    double WilsonHilferty(double Chi2, int32 Df)
    {
        if (Df < 1) return 0;
        const double K = 2. / (9. * Df);
        return (FMath::Pow(Chi2 / Df, 1. / 3.) - (1. - K)) / FMath::Sqrt(K);
    }

    // Two samples of counts by value: bins grow along the values until the smaller sample expects MinExpected in
    // them, and the tail joins the last bin. Fano is the clustering of the counts (blocks come in blobs, so their
    // variance exceeds a Poisson count's): the chi-square is divided by it.
    double TwoSampleZ(const TMap<int32, int64>& A, const TMap<int32, int64>& B, double MinExpected, double Fano = 1.)
    {
        double TotalA = 0, TotalB = 0;
        TSet<int32> KeySet;
        for (const TPair<int32, int64>& Pair : A)
            TotalA += Pair.Value, KeySet.Add(Pair.Key);
        for (const TPair<int32, int64>& Pair : B)
            TotalB += Pair.Value, KeySet.Add(Pair.Key);
        if (TotalA <= 0 || TotalB <= 0) return TotalA == TotalB ? 0 : 99;
        TArray<int32> Keys = KeySet.Array();
        Keys.Sort();
        const double Scale = FMath::Min(TotalA, TotalB) / (TotalA + TotalB);
        TArray<TPair<double, double>> Bins;
        double BinA = 0, BinB = 0;
        for (const int32 Key : Keys)
        {
            BinA += double(A.FindRef(Key));
            BinB += double(B.FindRef(Key));
            if ((BinA + BinB) * Scale >= MinExpected)
            {
                Bins.Add({BinA, BinB});
                BinA = BinB = 0;
            }
        }
        if (BinA + BinB > 0)
        {
            if (Bins.IsEmpty())
                Bins.Add({BinA, BinB});
            else
                Bins.Last().Key += BinA, Bins.Last().Value += BinB;
        }
        const double RatioA = FMath::Sqrt(TotalB / TotalA), RatioB = FMath::Sqrt(TotalA / TotalB);
        double Chi2 = 0;
        for (const TPair<double, double>& Bin : Bins)
            Chi2 += FMath::Square(Bin.Key * RatioA - Bin.Value * RatioB) / (Bin.Key + Bin.Value);
        return WilsonHilferty(Chi2 / FMath::Max(Fano, 1.), Bins.Num() - 1);
    }

    // Graph building for hand-made rule tests.
    FVoxelFeatureValue GraphString(FVoxelFeatureGraph& Graph, const TCHAR* Text)
    {
        FVoxelFeatureValue Value;
        Value.Type = EVoxelFeatureValueType::String;
        Value.Index = Graph.AddString(Text);
        return Value;
    }

    FVoxelFeatureValue GraphNumber(double Number)
    {
        FVoxelFeatureValue Value;
        Value.Number = Number;
        return Value;
    }

    FVoxelFeatureValue GraphNode(int32 Node)
    {
        FVoxelFeatureValue Value;
        Value.Type = EVoxelFeatureValueType::Node;
        Value.Index = Node;
        return Value;
    }

    FVoxelFeatureValue GraphList(FVoxelFeatureGraph& Graph, const TArray<int32>& Nodes)
    {
        FVoxelFeatureValue Value;
        Value.Type = EVoxelFeatureValueType::List;
        Value.Index = Graph.Items.Num();
        Value.Count = Nodes.Num();
        for (const int32 Node : Nodes)
            Graph.Items.Add(GraphNode(Node));
        return Value;
    }

    int32 GraphRule(FVoxelFeatureGraph& Graph, const TCHAR* Type,
        std::initializer_list<TPair<const TCHAR*, FVoxelFeatureValue>> Fields)
    {
        FVoxelFeatureNode Node;
        Node.Kind = EVoxelFeatureKind::RuleTest;
        Node.Type = Graph.AddString(Type);
        Node.FirstField = Graph.Fields.Num();
        Node.FieldCount = int32(Fields.size());
        for (const TPair<const TCHAR*, FVoxelFeatureValue>& Field : Fields)
            Graph.Fields.Add({Graph.AddString(Field.Key), Field.Value});
        return Graph.Nodes.Add(Node);
    }

    // The unique placed feature sequence of a log's writes: per feature key, (bucket << 20 | dense cell) in order.
    TMap<uint16, TArray<int64>> CollectWrites(const FVoxelWriteLog& Log)
    {
        TMap<uint16, TArray<int64>> Out;
        for (int32 Bucket = 0; Bucket < 9; ++Bucket)
        {
            const FVoxelWriteBucket& Source = Log.Buckets[Bucket];
            int32 Begin = 0;
            for (const FVoxelWriteBucket::FRun& Run : Source.Runs)
            {
                TArray<int64>& Writes = Out.FindOrAdd(Run.Key);
                for (int32 Write = Begin; Write < Run.End; ++Write)
                    Writes.Add(int64(Bucket) << 20 | Source.Writes[Write].GetDenseIndex());
                Begin = Run.End;
            }
        }
        return Out;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationMergeTest, "Unnamed.Voxel.Decoration.Merge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationMergeTest::RunTest(const FString& Parameters)
{
    // The merge visits writes in the order of a stable sort by (key, source, sequence), whatever the runs.
    FVoxelXoroshiro Random(VoxelRandom::Salt("merge_test"));
    int32 Wrong = 0, TotalWrites = 0, Nulls = 0, Empties = 0;
    for (int32 Case = 0; Case < 300; ++Case)
    {
        struct FReference
        {
            int32 Key, Source, Id;
        };
        FVoxelWriteBucket Buckets[9];
        const FVoxelWriteBucket* Sources[9] = {};
        TArray<FReference> All;
        for (int32 Source = 0; Source < 9; ++Source)
        {
            const int32 Kind = Random.NextInt(6);
            if (Kind == 0)
            {
                ++Nulls;
                continue;
            }
            Sources[Source] = &Buckets[Source];
            if (Kind == 1)
            {
                ++Empties;
                continue;
            }
            int32 Key = Random.NextInt(3);
            for (int32 Runs = 1 + Random.NextInt(8); Runs > 0; --Runs)
            {
                Key += 1 + Random.NextInt(3);
                for (int32 Count = 1 + Random.NextInt(6); Count > 0; --Count)
                {
                    FVoxelWrite Write;
                    Write.Packed = uint32(All.Num());
                    Buckets[Source].Add(uint16(Key), Write);
                    All.Add({Key, Source, All.Num()});
                }
            }
        }
        All.StableSort([](const FReference& A, const FReference& B)
            { return A.Key != B.Key ? A.Key < B.Key : A.Source < B.Source; });
        TArray<int32> Visited;
        VoxelWriteMerge::Merge(Sources, [&Visited](const FVoxelWrite& Write) { Visited.Add(int32(Write.Packed)); });
        bool bSame = Visited.Num() == All.Num();
        for (int32 Index = 0; bSame && Index < All.Num(); ++Index)
            bSame = Visited[Index] == All[Index].Id;
        Wrong += bSame ? 0 : 1;
        TotalWrites += All.Num();
    }
    AddInfo(FString::Printf(TEXT("300 cases, %d writes, %d null and %d empty sources"), TotalWrites, Nulls, Empties));
    TestEqual(TEXT("cases whose merge order differs from the stable sort"), Wrong, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationRuleTestsTest, "Unnamed.Voxel.Decoration.RuleTests",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationRuleTestsTest::RunTest(const FString& Parameters)
{
    // (a) Hand-made nodes on the hand table.
    {
        const FVoxelBlockTable Table = VoxelTestBlocks::Make();
        FVoxelFeatureGraph Graph;
        const int32 Tag = Graph.AddTag(TEXT("minecraft:test_tag"));
        Graph.Tags[Tag].Values = {TEXT("minecraft:stone"), TEXT("minecraft:dirt")};
        FVoxelBlockTags Tags(Graph, Table);
        const int32 Stone = Table.Get(VoxelTestBlocks::Stone).Block, Dirt = Table.Get(VoxelTestBlocks::Dirt).Block,
                    Glass = Table.Get(VoxelTestBlocks::Glass).Block;
        const auto Compile = [&](int32 Node, FVoxelRuleTest& Out)
        {
            FString Error;
            return TestTrue(
                FString::Printf(TEXT("a rule compiles (%s)"), *Error), Out.Compile(Graph, Node, Tags, &Error));
        };

        const int32 Always = GraphRule(Graph, TEXT("minecraft:always_true"), {});
        FVoxelRuleTest AlwaysTest;
        if (Compile(Always, AlwaysTest))
        {
            TestTrue(TEXT("always_true: stone"), AlwaysTest.Test(Stone, 10));
            TestTrue(TEXT("always_true: air"), AlwaysTest.Test(0, -64));
            TestTrue(TEXT("always_true: top of the world"), AlwaysTest.Test(Glass, VoxelConstants::MaxZ));
            TestFalse(TEXT("always_true: below the world"), AlwaysTest.Test(Stone, VoxelConstants::MinZ - 1));
            TestFalse(TEXT("always_true: above the world"), AlwaysTest.Test(Stone, VoxelConstants::MaxZ + 1));
            TestTrue(TEXT("always_true matches air"), AlwaysTest.CanMatch(0));
        }

        const int32 StoneMatch = GraphRule(
            Graph, TEXT("minecraft:block_match"), {{TEXT("block"), GraphString(Graph, TEXT("minecraft:stone"))}});
        FVoxelRuleTest StoneTest;
        if (Compile(StoneMatch, StoneTest))
        {
            TestTrue(TEXT("block_match: stone"), StoneTest.Test(Stone, 0));
            TestFalse(TEXT("block_match: dirt"), StoneTest.Test(Dirt, 0));
            TestFalse(TEXT("block_match: air"), StoneTest.Test(0, 0));
            TestFalse(TEXT("block_match cannot match air"), StoneTest.CanMatch(0));
            TestTrue(TEXT("block_match can match stone"), StoneTest.CanMatch(Stone));
        }

        const int32 TagMatch = GraphRule(
            Graph, TEXT("minecraft:tag_match"), {{TEXT("tag"), GraphString(Graph, TEXT("minecraft:test_tag"))}});
        FVoxelRuleTest TagTest;
        if (Compile(TagMatch, TagTest))
        {
            TestTrue(TEXT("tag_match: stone"), TagTest.Test(Stone, 5));
            TestTrue(TEXT("tag_match: dirt"), TagTest.Test(Dirt, 5));
            TestFalse(TEXT("tag_match: glass"), TagTest.Test(Glass, 5));
            TestFalse(TEXT("tag_match: air"), TagTest.Test(0, 5));
            TestFalse(TEXT("tag_match cannot match air"), TagTest.CanMatch(0));
        }

        const auto HeightMatch = [&](int32 Min, int32 Max)
        {
            return GraphRule(Graph, TEXT("minecraft:height_match"),
                {{TEXT("min_inclusive"), GraphNumber(Min)}, {TEXT("max_inclusive"), GraphNumber(Max)}});
        };
        FVoxelRuleTest HeightTest;
        if (Compile(HeightMatch(10, 20), HeightTest))
        {
            TestFalse(TEXT("height_match: below the lower bound"), HeightTest.Test(Stone, 9));
            TestTrue(TEXT("height_match: the lower bound"), HeightTest.Test(Stone, 10));
            TestTrue(TEXT("height_match: the upper bound"), HeightTest.Test(Stone, 20));
            TestFalse(TEXT("height_match: above the upper bound"), HeightTest.Test(Stone, 21));
            TestTrue(TEXT("height_match: any block inside"), HeightTest.Test(0, 15));
            TestEqual(TEXT("height_match: bands"), HeightTest.GetBandCount(), 3);
            TestTrue(TEXT("height_match can match air"), HeightTest.CanMatch(0));
        }
        FVoxelRuleTest LowTest, HighTest;
        if (Compile(HeightMatch(VoxelConstants::MinZ, 20), LowTest))
        {
            TestEqual(TEXT("height_match from the bottom: bands"), LowTest.GetBandCount(), 2);
            TestTrue(TEXT("height_match from the bottom: bottom"), LowTest.Test(Stone, VoxelConstants::MinZ));
            TestFalse(TEXT("height_match from the bottom: above"), LowTest.Test(Stone, 21));
        }
        if (Compile(HeightMatch(10, VoxelConstants::MaxZ), HighTest))
        {
            TestEqual(TEXT("height_match to the top: bands"), HighTest.GetBandCount(), 2);
            TestTrue(TEXT("height_match to the top: top"), HighTest.Test(Stone, VoxelConstants::MaxZ));
            TestFalse(TEXT("height_match to the top: below"), HighTest.Test(Stone, 9));
        }

        const int32 NotStone = GraphRule(Graph, TEXT("minecraft:not"), {{TEXT("rule"), GraphNode(StoneMatch)}});
        FVoxelRuleTest NotTest;
        if (Compile(NotStone, NotTest))
        {
            TestFalse(TEXT("not: stone"), NotTest.Test(Stone, 0));
            TestTrue(TEXT("not: dirt"), NotTest.Test(Dirt, 0));
            TestTrue(TEXT("not: air"), NotTest.Test(0, 0));
            TestTrue(TEXT("not matches air"), NotTest.CanMatch(0));
        }

        const int32 DirtMatch = GraphRule(
            Graph, TEXT("minecraft:block_match"), {{TEXT("block"), GraphString(Graph, TEXT("minecraft:dirt"))}});
        const int32 NotDirt = GraphRule(Graph, TEXT("minecraft:not"), {{TEXT("rule"), GraphNode(DirtMatch)}});
        const int32 AllOf =
            GraphRule(Graph, TEXT("minecraft:all_of"), {{TEXT("rules"), GraphList(Graph, {TagMatch, NotDirt})}});
        FVoxelRuleTest AllTest;
        if (Compile(AllOf, AllTest))
        {
            TestTrue(TEXT("all_of: stone"), AllTest.Test(Stone, 0));
            TestFalse(TEXT("all_of: dirt"), AllTest.Test(Dirt, 0));
            TestFalse(TEXT("all_of: glass"), AllTest.Test(Glass, 0));
            TestFalse(TEXT("all_of cannot match air"), AllTest.CanMatch(0));
        }

        const int32 GlassMatch = GraphRule(
            Graph, TEXT("minecraft:block_match"), {{TEXT("block"), GraphString(Graph, TEXT("minecraft:glass"))}});
        const int32 AnyOf = GraphRule(
            Graph, TEXT("minecraft:any_of"), {{TEXT("rules"), GraphList(Graph, {GlassMatch, HeightMatch(0, 0)})}});
        FVoxelRuleTest AnyTest;
        if (Compile(AnyOf, AnyTest))
        {
            TestTrue(TEXT("any_of: glass anywhere"), AnyTest.Test(Glass, 50));
            TestTrue(TEXT("any_of: stone at Z 0"), AnyTest.Test(Stone, 0));
            TestFalse(TEXT("any_of: stone at Z 1"), AnyTest.Test(Stone, 1));
            TestFalse(TEXT("any_of: stone at Z -1"), AnyTest.Test(Stone, -1));
            TestTrue(TEXT("any_of matches air through its height rule"), AnyTest.CanMatch(0));
        }
        const int32 AnyGlass =
            GraphRule(Graph, TEXT("minecraft:any_of"), {{TEXT("rules"), GraphList(Graph, {GlassMatch, StoneMatch})}});
        FVoxelRuleTest AnyGlassTest;
        if (Compile(AnyGlass, AnyGlassTest))
            TestFalse(TEXT("any_of of blocks cannot match air"), AnyGlassTest.CanMatch(0));

        // Nested: (stone or dirt) and not (Z 0 to 10).
        const int32 StoneOrDirt =
            GraphRule(Graph, TEXT("minecraft:any_of"), {{TEXT("rules"), GraphList(Graph, {StoneMatch, DirtMatch})}});
        const int32 NotLow = GraphRule(Graph, TEXT("minecraft:not"), {{TEXT("rule"), GraphNode(HeightMatch(0, 10))}});
        const int32 Nested =
            GraphRule(Graph, TEXT("minecraft:all_of"), {{TEXT("rules"), GraphList(Graph, {StoneOrDirt, NotLow})}});
        FVoxelRuleTest NestedTest;
        if (Compile(Nested, NestedTest))
        {
            TestFalse(TEXT("nested: stone at Z 5"), NestedTest.Test(Stone, 5));
            TestFalse(TEXT("nested: dirt at Z 0"), NestedTest.Test(Dirt, 0));
            TestFalse(TEXT("nested: dirt at Z 10"), NestedTest.Test(Dirt, 10));
            TestTrue(TEXT("nested: stone at Z 11"), NestedTest.Test(Stone, 11));
            TestTrue(TEXT("nested: dirt at Z -1"), NestedTest.Test(Dirt, -1));
            TestFalse(TEXT("nested: glass at Z 11"), NestedTest.Test(Glass, 11));
            TestFalse(TEXT("nested: air at Z 11"), NestedTest.Test(0, 11));
            TestFalse(TEXT("nested cannot match air"), NestedTest.CanMatch(0));
        }
    }

    // (b) The iron ore's two targets on the real data.
    if (!DecorationTestHasBlocks(*this)) return true;
    FVoxelFeatureGraph Graph;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
            VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
        return false;
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const int32 Entry = Graph.FindEntry(EVoxelFeatureRegistry::Feature, TEXT("minecraft:ore_iron"));
    if (!TestTrue(TEXT("ore_iron is in the graph"), Entry != INDEX_NONE)) return false;
    FVoxelBlockTags Tags(Graph, *Table);
    FVoxelOreConfig Config;
    TArray<FVoxelOreTarget> Targets;
    FString Skip;
    if (!TestTrue(FString::Printf(TEXT("ore_iron compiles (%s)"), *Error),
            VoxelOreFeature::Compile(Graph, Graph.Entries[int32(EVoxelFeatureRegistry::Feature)][Entry].Node, Tags,
                *Table, Config, Targets, Skip, &Error) &&
                Skip.IsEmpty()))
        return false;
    TestEqual(TEXT("iron's targets"), Targets.Num(), 2);
    const auto Pick = [&](const TCHAR* Block, int32 Z) -> FString
    {
        const int32 Id = Table->FindBlock(Block);
        for (const FVoxelOreTarget& Target : Targets)
            if (Target.Rule.Test(Id, Z)) return Target.Id;
        return TEXT("none");
    };
    struct FCase
    {
        const TCHAR* Block;
        int32 Z;
        const TCHAR* Expected;
    };
    const TCHAR* const Iron = TEXT("minecraft:iron_ore");
    const TCHAR* const Deep = TEXT("minecraft:deepslate_iron_ore");
    const FCase Cases[] = {{TEXT("tuff"), 0, Iron}, {TEXT("tuff"), 4, Iron}, {TEXT("tuff"), 8, Iron},
        {TEXT("tuff"), -1, Deep}, {TEXT("tuff"), -30, Deep}, {TEXT("stone"), -10, Iron}, {TEXT("stone"), 100, Iron},
        {TEXT("deepslate"), 50, Deep}, {TEXT("granite"), 20, Iron}, {TEXT("dirt"), 20, TEXT("none")},
        {TEXT("air"), 20, TEXT("none")}};
    for (const FCase& Case : Cases)
        TestEqual(FString::Printf(TEXT("iron on %s at Z %d"), Case.Block, Case.Z), Pick(Case.Block, Case.Z),
            FString(Case.Expected));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationDistributionTest, "Unnamed.Voxel.Providers.Distribution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationDistributionTest::RunTest(const FString& Parameters)
{
    // Every int and height provider of the jar's histograms that the core compiles, drawn 10^6 times against the
    // histogram of the jar's own draws.
    FVoxelFeatureGraph Graph;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
            VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
        return false;
    const TSharedPtr<FJsonObject> Histograms =
        LoadDecorationJson(FPaths::Combine(VoxelWorldgen::GetReferenceRoot(), TEXT("facts/provider_histograms.json")));
    const TArray<TSharedPtr<FJsonValue>>* Providers = nullptr;
    if (!TestTrue(TEXT("the histograms read"),
            Histograms.IsValid() && Histograms->TryGetArrayField(TEXT("providers"), Providers)))
        return false;
    constexpr int32 Samples = 1000000;
    int32 Checked = 0, Skipped = 0, Unresolved = 0;
    TMap<FString, int32> SkippedTypes;
    double WorstZ = -1e9;
    FString WorstUse;
    for (const TSharedPtr<FJsonValue>& Item : *Providers)
    {
        const TSharedPtr<FJsonObject> Entry = Item->AsObject();
        const FString Kind = Entry->GetStringField(TEXT("kind"));
        const TSharedPtr<FJsonObject>* Provider = nullptr;
        if ((Kind != TEXT("int") && Kind != TEXT("height")) || !Entry->TryGetObjectField(TEXT("provider"), Provider))
            continue;
        const FString Type = (*Provider)->GetStringField(TEXT("type"));
        const bool bHeight = Kind == TEXT("height");
        const auto IsSimple = [](const FString& Name)
        {
            return Name == TEXT("minecraft:constant") || Name == TEXT("minecraft:uniform") ||
                Name == TEXT("minecraft:trapezoid");
        };
        // S8 adds biased_to_bottom, weighted_list and clamped (over a simple source).
        const TSharedPtr<FJsonObject>* Source = nullptr;
        const bool bSimpleSource = !(*Provider)->TryGetObjectField(TEXT("source"), Source) ||
            IsSimple((*Source)->GetStringField(TEXT("type")));
        const bool bCompiled = bHeight ? Type == TEXT("minecraft:uniform") || Type == TEXT("minecraft:trapezoid")
                                       : IsSimple(Type) || Type == TEXT("minecraft:biased_to_bottom") ||
                Type == TEXT("minecraft:weighted_list") || (Type == TEXT("minecraft:clamped") && bSimpleSource);
        if (!bCompiled)
        {
            ++Skipped;
            ++SkippedTypes.FindOrAdd(Kind + TEXT(" ") + Type);
            continue;
        }
        // The first use in a placed feature or a feature; else in a block state provider or a carver (the few
        // providers only those use).
        FString Use;
        for (const TCHAR* Prefixes : {TEXT("placed_feature/ feature/"), TEXT("block_state_provider/ carver/")})
        {
            TArray<FString> Wanted;
            FString(Prefixes).ParseIntoArray(Wanted, TEXT(" "));
            for (const TSharedPtr<FJsonValue>& Candidate : Entry->GetArrayField(TEXT("uses")))
                if (Use.IsEmpty() &&
                    Wanted.ContainsByPredicate(
                        [&](const FString& Prefix) { return Candidate->AsString().StartsWith(Prefix); }))
                    Use = Candidate->AsString();
        }
        FString Folder, Rest;
        Use.Split(TEXT("/"), &Folder, &Rest);
        FString Id, Path;
        Rest.Split(TEXT("."), &Id, &Path);
        const EVoxelFeatureRegistry Registry = Folder == TEXT("feature") ? EVoxelFeatureRegistry::Feature
            : Folder == TEXT("block_state_provider")                     ? EVoxelFeatureRegistry::Provider
            : Folder == TEXT("carver")                                   ? EVoxelFeatureRegistry::Carver
                                                                         : EVoxelFeatureRegistry::PlacedFeature;
        const int32 EntryIndex = Graph.FindEntry(Registry, TEXT("minecraft:") + Id);
        int32 Node = EntryIndex == INDEX_NONE ? INDEX_NONE : Graph.Entries[int32(Registry)][EntryIndex].Node;
        const FVoxelFeatureValue* Value = nullptr;
        TArray<FString> Segments;
        Path.ParseIntoArray(Segments, TEXT("."));
        for (int32 Segment = 0; Node != INDEX_NONE && Segment < Segments.Num(); ++Segment)
        {
            FString Name = Segments[Segment];
            TArray<int32> Indices;
            int32 Open = INDEX_NONE;
            while (Name.FindLastChar(TEXT('['), Open))
            {
                Indices.Insert(FCString::Atoi(*Name.Mid(Open + 1)), 0);
                Name.LeftInline(Open);
            }
            Value = Graph.FindField(Node, Name);
            for (int32 Position = 0; Value && Position < Indices.Num(); ++Position)
                Value = Value->Type == EVoxelFeatureValueType::List && Indices[Position] < Value->Count
                    ? &Graph.Items[Value->Index + Indices[Position]]
                    : nullptr;
            if (Value && Segment + 1 < Segments.Num()) Node = Graph.GetNode(*Value);
            if (!Value) Node = INDEX_NONE;
        }
        FVoxelIntProvider Compiled;
        if (Use.IsEmpty() || !Value ||
            !(bHeight ? VoxelNumberProviders::CompileHeight(Graph, *Value, Compiled, &Error)
                      : VoxelNumberProviders::CompileInt(Graph, *Value, Compiled, &Error)))
        {
            ++Unresolved;
            AddError(FString::Printf(TEXT("%s provider %s from %s does not resolve (%s)"), *Kind, *Type, *Use, *Error));
            continue;
        }
        TMap<int32, int64> Ours, Jar;
        FVoxelXoroshiro Random(0x5EED0000ull + uint64(Checked));
        for (int32 Sample = 0; Sample < Samples; ++Sample)
            ++Ours.FindOrAdd(Compiled.Sample(Random));
        for (const TSharedPtr<FJsonValue>& Pair : Entry->GetArrayField(TEXT("counts")))
            Jar.Add(int32(Pair->AsArray()[0]->AsNumber()), int64(Pair->AsArray()[1]->AsNumber()));
        // Values with at least 10 draws in the two samples together get a bin; the rest pool in one.
        TSet<int32> Values;
        for (const TPair<int32, int64>& Pair : Ours)
            Values.Add(Pair.Key);
        for (const TPair<int32, int64>& Pair : Jar)
            Values.Add(Pair.Key);
        double Chi2 = 0, PoolA = 0, PoolB = 0;
        int32 Bins = 0, OneSided = 0;
        for (const int32 Value2 : Values)
        {
            const double A = double(Ours.FindRef(Value2)), B = double(Jar.FindRef(Value2));
            if ((A == 0 && B >= 10) || (B == 0 && A >= 10)) ++OneSided;
            if (A + B >= 10)
                Chi2 += FMath::Square(A - B) / (A + B), ++Bins;
            else
                PoolA += A, PoolB += B;
        }
        if (PoolA + PoolB > 0) Chi2 += FMath::Square(PoolA - PoolB) / (PoolA + PoolB), ++Bins;
        const int32 Df = FMath::Max(Bins - 1, 1);
        const double Z = (Chi2 - Df) / FMath::Sqrt(2. * Df);
        if (Z > WorstZ) WorstZ = Z, WorstUse = Use;
        TestTrue(FString::Printf(TEXT("%s %s (%s): z %.2f below 5"), *Kind, *Type, *Use, Z), Z < 5);
        TestEqual(FString::Printf(TEXT("%s: values seen by one sample only"), *Use), OneSided, 0);
        ++Checked;
    }
    TestEqual(TEXT("providers that did not resolve"), Unresolved, 0);
    TArray<FString> SkippedList;
    for (const TPair<FString, int32>& Pair : SkippedTypes)
        SkippedList.Add(FString::Printf(TEXT("%s x%d"), *Pair.Key, Pair.Value));
    AddInfo(FString::Printf(TEXT("%d providers checked (worst z %.2f at %s), %d skipped by type: %s"), Checked, WorstZ,
        *WorstUse, Skipped, *FString::Join(SkippedList, TEXT("; "))));
    // 80 with S2's types; S8's biased_to_bottom, weighted_list and clamped bring 21 more.
    TestEqual(TEXT("providers of compiled types checked"), Checked, 101);
    return true;
}

namespace
{
    // A probe number; the probe writes null where it has nothing to measure (an empty band), read as 0.
    double ProbeNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
    {
        double Value = 0;
        return Object && Object->TryGetNumberField(Key, Value) ? Value : 0;
    }

    // One band of a probe, or of our run of the same: blocks per placement, offsets, and the exposure counts.
    struct FBandStats
    {
        FString Name;
        int32 OriginZ = 0;
        int32 Placements = 0;
        int32 Failed = 0;
        double Mean = 0, SD = 0;
        TMap<int32, int64> Blocks, ByDy, ByDxz;
        int32 MinDy = MAX_int32, MaxDy = MIN_int32;
        int64 Adjacent = 0, Rest = 0, Plane = 0;
        // Tuff band, two-state ores: the deepslate ore at Z >= 0, the stone ore below 0.
        int32 WrongState = 0;
        // Variance of a count over its mean, as blobs cluster the blocks: by dy, and by dx and dz.
        double FanoDy = 1, FanoDxz = 1;
    };

    struct FProbe
    {
        FString File;
        FString Feature;
        double Discard = 0;
        TArray<TTuple<FString, int32, int32>> Layers;
        TArray<FBandStats> Bands;
        double AirAdjacent = 0, AirRest = 0, SolidAdjacent = 0, SolidRest = 0, Ratio = 0;
    };

    bool ReadProbe(const FString& File, FProbe& Out, FString& OutError)
    {
        const TSharedPtr<FJsonObject> Json = LoadDecorationJson(File);
        if (!Json.IsValid())
        {
            OutError = TEXT("unreadable");
            return false;
        }
        Out.File = FPaths::GetBaseFilename(File);
        Out.Feature = Json->GetStringField(TEXT("feature"));
        Out.Discard = Json->GetObjectField(TEXT("config"))->GetNumberField(TEXT("discard_chance_on_air_exposure"));
        const TSharedPtr<FJsonObject> World = Json->GetObjectField(TEXT("world"));
        for (const TSharedPtr<FJsonValue>& Layer : World->GetArrayField(TEXT("layers")))
        {
            const TArray<TSharedPtr<FJsonValue>>& Parts = Layer->AsArray();
            Out.Layers.Emplace(Parts[0]->AsString(), int32(Parts[1]->AsNumber()), int32(Parts[2]->AsNumber()));
        }
        const TSharedPtr<FJsonObject> Exposure = Json->GetObjectField(TEXT("exposure"));
        Out.AirAdjacent = Exposure->GetObjectField(TEXT("air"))->GetNumberField(TEXT("adjacent"));
        Out.AirRest = Exposure->GetObjectField(TEXT("air"))->GetNumberField(TEXT("rest"));
        Out.SolidAdjacent = Exposure->GetObjectField(TEXT("solid"))->GetNumberField(TEXT("adjacent"));
        Out.SolidRest = Exposure->GetObjectField(TEXT("solid"))->GetNumberField(TEXT("rest"));
        Out.Ratio = ProbeNumber(Exposure, TEXT("ratio"));
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Band : World->GetObjectField(TEXT("bands"))->Values)
        {
            FBandStats Stats;
            Stats.Name = Band.Key;
            Stats.OriginZ = int32(ProbeNumber(Band.Value->AsObject(), TEXT("origin_y")));
            const TSharedPtr<FJsonObject> Data = Json->GetObjectField(TEXT("bands"))->GetObjectField(Band.Key);
            Stats.Placements = int32(ProbeNumber(Data, TEXT("placements")));
            Stats.Failed = int32(ProbeNumber(Data, TEXT("failed")));
            const TSharedPtr<FJsonObject> Total = Data->GetObjectField(TEXT("blocks"))->GetObjectField(TEXT("total"));
            Stats.Mean = ProbeNumber(Total, TEXT("mean"));
            Stats.SD = ProbeNumber(Total, TEXT("std"));
            Stats.Blocks = ReadCounts(Total->GetObjectField(TEXT("histogram")));
            Stats.ByDy = ReadCounts(Data->GetObjectField(TEXT("by_dy")));
            Stats.ByDxz = ReadCounts(Data->GetObjectField(TEXT("by_dx")));
            for (const TPair<int32, int64>& Pair : ReadCounts(Data->GetObjectField(TEXT("by_dz"))))
                Stats.ByDxz.FindOrAdd(Pair.Key) += Pair.Value;
            const TSharedPtr<FJsonObject> Dy = Data->GetObjectField(TEXT("extent"))->GetObjectField(TEXT("dy"));
            Stats.MinDy = int32(ProbeNumber(Dy, TEXT("min")));
            Stats.MaxDy = int32(ProbeNumber(Dy, TEXT("max")));
            for (const TPair<int32, int64>& Pair : Stats.ByDy)
            {
                if (FMath::Abs(Pair.Key) == 1) Stats.Adjacent += Pair.Value;
                if (FMath::Abs(Pair.Key) >= 2) Stats.Rest += Pair.Value;
                if (Pair.Key == 0) Stats.Plane += Pair.Value;
            }
            Out.Bands.Add(MoveTemp(Stats));
        }
        return true;
    }

    constexpr int32 ProbePlacements = 2000;

    // Our blobs on the probe's column, band by band; false with OutError when the ore does not compile.
    bool SimulateProbe(const FVoxelFeatureGraph& Graph, const FVoxelBlockTable& Table, const FProbe& Probe,
        int32 OreIndex, TArray<FBandStats>& Out, FString& OutError)
    {
        const int32 Entry = Graph.FindEntry(EVoxelFeatureRegistry::Feature, Probe.Feature);
        if (Entry == INDEX_NONE)
        {
            OutError = TEXT("not in the graph");
            return false;
        }
        FVoxelBlockTags Tags(Graph, Table);
        FVoxelOreConfig Config;
        TArray<FVoxelOreTarget> Targets;
        FString Skip;
        if (!VoxelOreFeature::Compile(Graph, Graph.Entries[int32(EVoxelFeatureRegistry::Feature)][Entry].Node, Tags,
                Table, Config, Targets, Skip, &OutError) ||
            !Skip.IsEmpty())
        {
            OutError += Skip;
            return false;
        }
        // The deepslate ore and the stone ore of a two-state ore.
        TSet<uint16> DeepStates, StoneStates;
        for (const FVoxelOreTarget& Target : Targets)
            if (Target.Id.StartsWith(TEXT("minecraft:deepslate_")))
            {
                const FString Partner = TEXT("minecraft:") + Target.Id.RightChop(20);
                for (const FVoxelOreTarget& Other : Targets)
                    if (Other.Id == Partner)
                    {
                        DeepStates.Add(Target.State);
                        StoneStates.Add(Other.State);
                    }
            }
        TArray<uint16> LayerState;
        LayerState.Init(0, VoxelConstants::Height);
        for (const TTuple<FString, int32, int32>& Layer : Probe.Layers)
        {
            const uint16 State = Table.FindState(FName(*Layer.Get<0>().Replace(TEXT("minecraft:"), TEXT(""))));
            for (int32 Z = Layer.Get<1>(); Z <= Layer.Get<2>(); ++Z)
                LayerState[Z - VoxelConstants::MinZ] = State;
        }
        const TSharedRef<FVoxelColumn> Column = VoxelTestBlocks::MakeColumn(Table, FIntPoint::ZeroValue,
            [&LayerState](int32, int32, int32 Z) { return LayerState[Z - VoxelConstants::MinZ]; });
        const FVoxelColumn* Around[9];
        for (const FVoxelColumn*& Slot : Around)
            Slot = &*Column;
        TArray<FIntVector> Scratch;
        constexpr uint64 Salt = VoxelRandom::Salt("ore_probe");
        for (int32 Band = 0; Band < Probe.Bands.Num(); ++Band)
        {
            FBandStats Stats;
            Stats.Name = Probe.Bands[Band].Name;
            Stats.OriginZ = Probe.Bands[Band].OriginZ;
            Stats.Placements = ProbePlacements;
            double Sum = 0, SumSquares = 0, SquaresDy = 0, SquaresDxz = 0;
            for (int32 Placement = 0; Placement < ProbePlacements; ++Placement)
            {
                FVoxelDecorationContext Context(FIntPoint::ZeroValue, Around);
                FVoxelXoroshiro Random(VoxelRandom::Hash64(1, Salt, OreIndex, Band, Placement));
                int32 Blocks = 0, Placed = 0;
                TMap<int32, int32> LocalDy, LocalDxz;
                VoxelOreFeature::Place(Config, Targets, FIntVector(8, 8, Stats.OriginZ), Random, Context, Table, true,
                    Scratch,
                    [&](int32 X, int32 Y, int32 Z, uint16 State)
                    {
                        ++Placed;
                        if (State == LayerState[Z - VoxelConstants::MinZ]) return;
                        ++Blocks;
                        const int32 Dy = Z - Stats.OriginZ;
                        ++Stats.ByDy.FindOrAdd(Dy);
                        ++Stats.ByDxz.FindOrAdd(X - 8);
                        ++Stats.ByDxz.FindOrAdd(Y - 8);
                        ++LocalDy.FindOrAdd(Dy);
                        ++LocalDxz.FindOrAdd(X - 8);
                        ++LocalDxz.FindOrAdd(Y - 8);
                        Stats.MinDy = FMath::Min(Stats.MinDy, Dy);
                        Stats.MaxDy = FMath::Max(Stats.MaxDy, Dy);
                        if (Stats.Name == TEXT("tuff") &&
                            ((Z >= 0 && DeepStates.Contains(State)) || (Z < 0 && StoneStates.Contains(State))))
                            ++Stats.WrongState;
                    });
                ++Stats.Blocks.FindOrAdd(Blocks);
                // A placement fails when it set no cell at all, even to the state it found.
                Stats.Failed += Placed == 0 ? 1 : 0;
                for (const TPair<int32, int32>& Pair : LocalDy)
                    SquaresDy += double(Pair.Value) * Pair.Value;
                for (const TPair<int32, int32>& Pair : LocalDxz)
                    SquaresDxz += double(Pair.Value) * Pair.Value;
                Sum += Blocks;
                SumSquares += double(Blocks) * Blocks;
            }
            Stats.Mean = Sum / ProbePlacements;
            Stats.FanoDy = SquaresDy / FMath::Max(1., Sum);
            Stats.FanoDxz = SquaresDxz / FMath::Max(1., 2. * Sum);
            Stats.SD = FMath::Sqrt(FMath::Max(0., SumSquares / ProbePlacements - Stats.Mean * Stats.Mean));
            for (const TPair<int32, int64>& Pair : Stats.ByDy)
            {
                if (FMath::Abs(Pair.Key) == 1) Stats.Adjacent += Pair.Value;
                if (FMath::Abs(Pair.Key) >= 2) Stats.Rest += Pair.Value;
                if (Pair.Key == 0) Stats.Plane += Pair.Value;
            }
            Out.Add(MoveTemp(Stats));
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationOreProbesTest, "Unnamed.Voxel.Decoration.OreProbes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationOreProbesTest::RunTest(const FString& Parameters)
{
    int32 ExposureChecks = 0;
    // Our ore blob against the 24 probes of the jar's ore feature, as distributions.
    if (!DecorationTestHasBlocks(*this)) return true;
    FVoxelFeatureGraph Graph;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
            VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
        return false;
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const FString Folder = FPaths::Combine(VoxelWorldgen::GetReferenceRoot(), TEXT("facts/feature_probes"));
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *FPaths::Combine(Folder, TEXT("ore_*.json")), true, false);
    Files.Sort();
    TestEqual(TEXT("ore probes"), Files.Num(), 24);
    TArray<FProbe> Probes;
    Probes.SetNum(Files.Num());
    for (int32 Index = 0; Index < Files.Num(); ++Index)
        if (!TestTrue(FString::Printf(TEXT("%s reads"), *Files[Index]),
                ReadProbe(FPaths::Combine(Folder, Files[Index]), Probes[Index], Error)))
            return false;
    TArray<TArray<FBandStats>> Ours;
    TArray<FString> Errors;
    Ours.SetNum(Probes.Num());
    Errors.SetNum(Probes.Num());
    ParallelFor(Probes.Num(),
        [&](int32 Index)
        {
            if (!SimulateProbe(Graph, *Table, Probes[Index], Index, Ours[Index], Errors[Index]) &&
                Errors[Index].IsEmpty())
                Errors[Index] = TEXT("failed");
        });
    for (int32 Index = 0; Index < Probes.Num(); ++Index)
    {
        const FProbe& Probe = Probes[Index];
        if (!TestTrue(FString::Printf(TEXT("%s compiles and runs (%s)"), *Probe.File, *Errors[Index]),
                Errors[Index].IsEmpty()))
            continue;
        for (int32 Band = 0; Band < Probe.Bands.Num(); ++Band)
        {
            const FBandStats& Jar = Probe.Bands[Band];
            const FBandStats& Mine = Ours[Index][Band];
            const FString Label = FString::Printf(TEXT("%s %s"), *Probe.File, *Jar.Name);
            const double MeanTolerance =
                4. * FMath::Sqrt(FMath::Square(Jar.SD) / Jar.Placements + FMath::Square(Mine.SD) / Mine.Placements) +
                0.05;
            TestTrue(FString::Printf(TEXT("%s: mean blocks %.3f against the probe's %.3f (tolerance %.3f)"), *Label,
                         Mine.Mean, Jar.Mean, MeanTolerance),
                FMath::Abs(Mine.Mean - Jar.Mean) <= MeanTolerance);
            const double FailJar = double(Jar.Failed) / Jar.Placements,
                         FailMine = double(Mine.Failed) / Mine.Placements;
            const double FailTolerance = 4. *
                    FMath::Sqrt(
                        FailJar * (1. - FailJar) / Jar.Placements + FailMine * (1. - FailMine) / Mine.Placements) +
                0.02;
            TestTrue(FString::Printf(TEXT("%s: failure rate %.3f against the probe's %.3f (tolerance %.3f)"), *Label,
                         FailMine, FailJar, FailTolerance),
                FMath::Abs(FailMine - FailJar) <= FailTolerance);
            const double ZBlocks = TwoSampleZ(Mine.Blocks, Jar.Blocks, 5);
            const double ZDy = TwoSampleZ(Mine.ByDy, Jar.ByDy, 5, Mine.FanoDy);
            const double ZDxz = TwoSampleZ(Mine.ByDxz, Jar.ByDxz, 5, Mine.FanoDxz);
            TestTrue(FString::Printf(TEXT("%s: blocks per placement z %.2f below 4"), *Label, ZBlocks), ZBlocks < 4);
            TestTrue(FString::Printf(TEXT("%s: by dy z %.2f below 4"), *Label, ZDy), ZDy < 4);
            TestTrue(FString::Printf(TEXT("%s: by dx and dz z %.2f below 4"), *Label, ZDxz), ZDxz < 4);
            if (Jar.MinDy != MAX_int32 && Mine.MinDy != MAX_int32)
                TestTrue(FString::Printf(TEXT("%s: dy range %d..%d inside ours %d..%d (within 1)"), *Label, Jar.MinDy,
                             Jar.MaxDy, Mine.MinDy, Mine.MaxDy),
                    Jar.MinDy >= Mine.MinDy - 1 && Jar.MaxDy <= Mine.MaxDy + 1);
            if (Jar.Name == TEXT("tuff"))
                TestEqual(FString::Printf(TEXT("%s: ores in the wrong state across Z 0"), *Label), Mine.WrongState, 0);
            AddInfo(FString::Printf(TEXT("%s: blocks %.2f (probe %.2f), failed %.3f (%.3f), z blocks %.1f dy %.1f dxz "
                                         "%.1f"),
                *Label, Mine.Mean, Jar.Mean, FailMine, FailJar, ZBlocks, ZDy, ZDxz));
        }
        // Exposure: air against solid, adjacent against the rest.
        const FBandStats *Air = nullptr, *Stone = nullptr;
        for (int32 Band = 0; Band < Probe.Bands.Num(); ++Band)
        {
            if (Ours[Index][Band].Name == TEXT("air")) Air = &Ours[Index][Band];
            if (Ours[Index][Band].Name == TEXT("stone")) Stone = &Ours[Index][Band];
        }
        if (!Air || !Stone) continue;
        if (Probe.Discard >= 1.)
        {
            TestEqual(FString::Printf(TEXT("%s: blocks next to air with the discard chance 1"), *Probe.File),
                Air->Adjacent, int64(0));
            ++ExposureChecks;
        }
        else if (Probe.AirAdjacent + Probe.AirRest >= 100 && Air->Rest > 0 && Stone->Rest > 0 && Stone->Adjacent > 0)
        {
            const double Ratio = (double(Air->Adjacent) / Air->Rest) / (double(Stone->Adjacent) / Stone->Rest);
            // 0.2, or three standard errors of the probe's own ratio when its counts are small.
            const double ProbeError = Probe.Ratio *
                FMath::Sqrt(1. / FMath::Max(1., Probe.AirAdjacent) + 1. / FMath::Max(1., Probe.AirRest) +
                    1. / FMath::Max(1., Probe.SolidAdjacent) + 1. / FMath::Max(1., Probe.SolidRest));
            const double Tolerance = FMath::Max(0.2, 3. * ProbeError);
            TestTrue(FString::Printf(TEXT("%s: exposure ratio %.3f against the probe's %.3f (tolerance %.3f)"),
                         *Probe.File, Ratio, Probe.Ratio, Tolerance),
                FMath::Abs(Ratio - Probe.Ratio) <= Tolerance);
            AddInfo(FString::Printf(TEXT("%s: exposure ratio %.3f (probe %.3f)"), *Probe.File, Ratio, Probe.Ratio));
            ++ExposureChecks;
        }
    }
    TestTrue(FString::Printf(TEXT("exposure checks run (%d)"), ExposureChecks), ExposureChecks >= 20);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationOracleTest, "Unnamed.Voxel.Decoration.Oracle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationOracleTest::RunTest(const FString& Parameters)
{
    // A column decorated alone, the decorated area and the scheduler's stages all give the same final columns.
    if (!DecorationTestHasBlocks(*this)) return true;
    FDecorationTestWorld World;
    if (!MakeDecorationWorld(*this, TEXT("overworld"), DecorationTestSeed, World, DecorationTestOptions()))
        return false;
    constexpr int32 Radius = 2;
    FSnapshotSquare Snapshots;
    GenerateSnapshots(*World.Terrain, FIntPoint::ZeroValue, Radius + 2, Snapshots);
    const TArray<FColumnPtr> Scheduled = RunSchedule(*World.Decorator, *World.Terrain, &Snapshots, FIntPoint::ZeroValue,
        Radius, EDecorationOrder::FinalizeFirst, 1, 0);
    FVoxelDecoratedArea Area;
    FVoxelDecoratedArea::Build(*World.Decorator, *World.Terrain, FIntPoint::ZeroValue, Radius, false, false, Area);
    if (!TestEqual(TEXT("final columns"), Scheduled.Num(), 25) ||
        !TestEqual(TEXT("area columns"), Area.Columns.Num(), 25))
        return false;
    int32 AreaDifferences = 0;
    for (int32 Index = 0; Index < 25; ++Index)
        AreaDifferences +=
            VoxelBench::HashColumnBlocks(*Scheduled[Index]) != VoxelBench::HashColumnBlocks(*Area.Columns[Index]);
    TestEqual(TEXT("columns where the area differs from the scheduler"), AreaDifferences, 0);
    int32 Writes = 0;
    for (const FVoxelDecorateStats& Stats : Area.Decorates)
        Writes += Stats.Writes;
    TestTrue(FString::Printf(TEXT("the decoration wrote something (%d writes)"), Writes), Writes > 0);
    const FIntPoint Alone[3] = {{0, 0}, {Radius, 0}, {Radius, Radius}};
    const TCHAR* const Names[3] = {TEXT("centre"), TEXT("edge"), TEXT("corner")};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        FVoxelColumn Column;
        World.Decorator->DecorateAlone(*World.Terrain, Alone[Index], Column);
        const int32 Slot = (Alone[Index].Y + Radius) * 5 + Alone[Index].X + Radius;
        TestTrue(FString::Printf(TEXT("%s column decorated alone equals the scheduler's"), Names[Index]),
            VoxelBench::HashColumnBlocks(Column) == VoxelBench::HashColumnBlocks(*Scheduled[Slot]));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationPreviewTest, "Unnamed.Voxel.Decoration.Preview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationPreviewTest::RunTest(const FString& Parameters)
{
    // Streaming's final columns equal the decorated area's, in any load order. (The DecorateAlone loop is dropped:
    // LoadOrder and the area tests cover the oracle.)
    if (!DecorationTestHasBlocks(*this)) return true;
    FDecorationTestWorld World;
    if (!MakeDecorationWorld(*this, TEXT("overworld"), DecorationTestSeed, World, FVoxelDecorationOptions()))
        return true;
    constexpr int32 AreaRadius = 4, CompareRadius = 3, RoundCap = 2000, ManualRoundCap = 200000;
    const FIntPoint Centers[2] = {FIntPoint(0, 0), FIntPoint(7, -5)};

    auto MakePipeline = [&World](int32 MaxTasks, bool bManual)
    {
        FVoxelColumnPipeline::FSettings Settings;
        Settings.MaxTasksInFlight = MaxTasks;
        Settings.bManualJobs = bManual;
        Settings.Decorator = World.Decorator;
        return MakeUnique<FVoxelColumnPipeline>(World.Table, World.Terrain.ToSharedRef(), Settings);
    };
    auto TakeMeshes = [](FVoxelColumnPipeline& Pipeline)
    {
        FVoxelColumnPipeline::FReady Ready;
        while (Pipeline.PopReady(Ready))
        {
        }
    };
    auto Settle = [&TakeMeshes, RoundCap](FVoxelColumnPipeline& Pipeline)
    {
        for (int32 Round = 0; Round < RoundCap; ++Round)
        {
            Pipeline.Update();
            TakeMeshes(Pipeline);
            if (Pipeline.IsIdle()) return true;
            Pipeline.WaitForJobs();
        }
        return false;
    };

    for (const FIntPoint& Center : Centers)
    {
        FVoxelDecoratedArea Area;
        FVoxelDecoratedArea::Build(*World.Decorator, *World.Terrain, Center, AreaRadius, true, false, Area);
        const int32 Side = 2 * AreaRadius + 1;
        auto Compare = [&](const TCHAR* Name, const FVoxelColumnPipeline& Pipeline)
        {
            int32 Compared = 0;
            FString Differing;
            for (int32 DY = -CompareRadius; DY <= CompareRadius; ++DY)
                for (int32 DX = -CompareRadius; DX <= CompareRadius; ++DX)
                {
                    const FVoxelColumn* Found = Pipeline.FindColumn(Center + FIntPoint(DX, DY));
                    if (!Found) continue;
                    ++Compared;
                    const FVoxelColumn& Expected = *Area.Columns[(DY + AreaRadius) * Side + DX + AreaRadius];
                    if (VoxelBench::HashColumnBlocks(*Found) != VoxelBench::HashColumnBlocks(Expected))
                        Differing += FString::Printf(TEXT(" (%d, %d)"), Center.X + DX, Center.Y + DY);
                }
            TestTrue(FString::Printf(TEXT("center (%d, %d), %s: at least 25 columns compared (%d)"), Center.X, Center.Y,
                         Name, Compared),
                Compared >= 25);
            TestTrue(FString::Printf(TEXT("center (%d, %d), %s: columns that differ from the area's:%s"), Center.X,
                         Center.Y, Name, *Differing),
                Differing.IsEmpty());
            AddInfo(FString::Printf(TEXT("center (%d, %d), %s: %d compared, %s"), Center.X, Center.Y, Name, Compared,
                Differing.IsEmpty() ? TEXT("none differ") : *Differing));
        };

        // (a) Threaded, wide.
        {
            TUniquePtr<FVoxelColumnPipeline> Pipeline = MakePipeline(16, false);
            Pipeline->SetFocus(FVoxelStreamingRings(Center, 2, 2), 0);
            TestTrue(TEXT("threaded pipeline settles"), Settle(*Pipeline));
            Compare(TEXT("threaded"), *Pipeline);
        }
        // (b) Threaded, one task at a time, walked in from the west.
        {
            TUniquePtr<FVoxelColumnPipeline> Pipeline = MakePipeline(1, false);
            Pipeline->SetFocus(FVoxelStreamingRings(Center + FIntPoint(-6, 0), 2, 2), 0);
            bool bSettled = Settle(*Pipeline);
            for (int32 Step = -5; Step <= 0; ++Step)
            {
                Pipeline->SetFocus(FVoxelStreamingRings(Center + FIntPoint(Step, 0), 2, 2), 3);
                bSettled = Settle(*Pipeline) && bSettled;
            }
            TestTrue(TEXT("walked pipeline settles"), bSettled);
            Compare(TEXT("walked"), *Pipeline);
        }
        // (c) Manual jobs in a random order.
        {
            TUniquePtr<FVoxelColumnPipeline> Pipeline = MakePipeline(8, true);
            Pipeline->SetFocus(FVoxelStreamingRings(Center, 2, 2), 0);
            FRandomStream Random(77);
            bool bIdle = false;
            for (int32 Round = 0; Round < ManualRoundCap && !bIdle; ++Round)
            {
                const int32 JobCount = Pipeline->GetJobCount();
                if (Pipeline->GetPendingJobCount() > 0 && JobCount > 0)
                {
                    const int32 First = Random.RandHelper(JobCount);
                    for (int32 Offset = 0; Offset < JobCount; ++Offset)
                        if (Pipeline->RunJob((First + Offset) % JobCount)) break;
                }
                Pipeline->Update();
                TakeMeshes(*Pipeline);
                bIdle = Pipeline->IsIdle();
            }
            TestTrue(TEXT("manual pipeline settles"), bIdle);
            Compare(TEXT("manual random order"), *Pipeline);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationLoadOrderTest, "Unnamed.Voxel.Decoration.LoadOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationLoadOrderTest::RunTest(const FString& Parameters)
{
    // Any priority order, worker count and timing of the stages gives the forward single-thread columns.
    if (!DecorationTestHasBlocks(*this)) return true;
    FDecorationTestWorld World;
    if (!MakeDecorationWorld(*this, TEXT("overworld"), DecorationTestSeed, World, DecorationTestOptions()))
        return false;
    constexpr int32 Radius = 4;
    FVoxelDecoratedArea Area;
    FVoxelDecoratedArea::Build(*World.Decorator, *World.Terrain, FIntPoint::ZeroValue, Radius, false, false, Area);
    const uint64 Baseline = HashColumns(Area.Columns);
    FSnapshotSquare Snapshots;
    GenerateSnapshots(*World.Terrain, FIntPoint::ZeroValue, Radius + 2, Snapshots);
    int32 Schedules = 0, Differing = 0;
    TArray<FString> Hashes;
    const auto Check = [&](EDecorationOrder Order, int32 Workers, bool bGenerate)
    {
        const uint64 Hash = HashColumns(RunSchedule(*World.Decorator, *World.Terrain, bGenerate ? nullptr : &Snapshots,
            FIntPoint::ZeroValue, Radius, Order, Workers, 0xD31A0000ull + uint64(Schedules)));
        ++Schedules;
        Hashes.Add(FString::Printf(TEXT("%016llx"), Hash));
        if (Hash != Baseline)
        {
            ++Differing;
            AddError(FString::Printf(TEXT("%s, %d workers%s gives %016llx, not %016llx"), GetOrderName(Order), Workers,
                bGenerate ? TEXT(", with Generate jobs") : TEXT(""), Hash, Baseline));
        }
    };
    for (int32 Order = 0; Order <= int32(EDecorationOrder::FinalizeFirst); ++Order)
        for (const int32 Workers : {1, 2, 30})
            Check(EDecorationOrder(Order), Workers, false);
    Check(EDecorationOrder::FinalizeFirst, 30, true);
    TestEqual(TEXT("schedules that differ from the baseline"), Differing, 0);
    AddInfo(FString::Printf(
        TEXT("%d schedules; baseline %016llx; hashes %s"), Schedules, Baseline, *FString::Join(Hashes, TEXT(" "))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationReachTest, "Unnamed.Voxel.Decoration.Reach",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationReachTest::RunTest(const FString& Parameters)
{
    // No write falls outside the 3 x 3 columns around its source.
    if (!DecorationTestHasBlocks(*this)) return true;
    for (const TCHAR* PresetName : DecorationTestPresets)
    {
        FDecorationTestWorld World;
        if (!MakeDecorationWorld(*this, PresetName, DecorationTestSeed, World, DecorationTestOptions())) continue;
        FVoxelDecoratedArea Area;
        FVoxelDecoratedArea::Build(*World.Decorator, *World.Terrain, FIntPoint::ZeroValue, 15, true, false, Area);
        int64 Dropped = 0, Writes = 0;
        for (const FVoxelDecorateStats& Stats : Area.Decorates)
            Dropped += Stats.Dropped, Writes += Stats.Writes;
        TestEqual(
            FString::Printf(TEXT("%s: %d Decorates"), PresetName, Area.Decorates.Num()), Area.Decorates.Num(), 1089);
        TestEqual(FString::Printf(TEXT("%s: dropped cells"), PresetName), Dropped, int64(0));
        // Glow lichen's search stays in the window (Jesus's answer to scout I's question 3), so it compiles.
        TestFalse(FString::Printf(TEXT("%s: multiface_growth compiles"), PresetName),
            World.Decorator->GetSkipped().Contains(TEXT("minecraft:multiface_growth")));
        AddInfo(FString::Printf(TEXT("%s: %lld writes in %d Decorates"), PresetName, Writes, Area.Decorates.Num()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationGoldenTest, "Unnamed.Voxel.Decoration.Golden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationGoldenTest::RunTest(const FString& Parameters)
{
    // The decorated columns of seed 263 are pinned: the hash of the 81 final columns and the writes of the 121
    // Decorates, per preset, with plants off (S2 to S6's values, ores only), plants on with trees off (S8, the decided
    // change) and both on (S9a).
    if (!DecorationTestHasBlocks(*this)) return true;
    // Plants off: recorded on the first passing run with ores on: overworld, large_biomes, amplified.
    constexpr uint64 GoldenHashes[3] = {0xa28113883ba6e7caull, 0xf87e27cd1455c61aull, 0xfc7c6279d46e6b76ull};
    constexpr int64 GoldenWrites[3] = {761145, 768534, 823270};
    // Plants on, trees off: recorded on S8 part B's first passing run (S8's plants plus glow lichen).
    constexpr uint64 PlantHashes[3] = {0xb77656a8d37bde4aull, 0xa290112228773eefull, 0xed3582d140b6c106ull};
    constexpr int64 PlantWrites[3] = {769195, 774817, 829385};
    // Plants and trees on: recorded on S9a's first run with trees in the world; S9c re-recorded (tree decorators, mega
    // spruce and pine); S9d part 2 re-recorded overworld and amplified (fallen trees, huge mushrooms, trunk_vine at
    // 2/3, the shelf mushrooms' one rule; large_biomes unchanged); S9e part 1 re-recorded large_biomes (azalea trees
    // with their root systems over a lush cave; overworld and amplified unchanged); S9e part 2 re-recorded large_biomes
    // (84 more writes of bamboo or vines, model blocks; overworld and amplified unchanged).
    constexpr uint64 TreeHashes[3] = {0x95645bbf53f73c12ull, 0x5e72b754b1f20fa6ull, 0x98ae73a412fa50caull};
    constexpr int64 TreeWrites[3] = {822150, 793454, 857244};
    for (const int32 Case : {0, 1, 2})
        for (int32 Preset = 0; Preset < 3; ++Preset)
        {
            FDecorationTestWorld World;
            FVoxelDecorationOptions Options = DecorationTestOptions();
            Options.bPlants = Case > 0;
            Options.bTrees = Case > 1;
            if (!MakeDecorationWorld(*this, DecorationTestPresets[Preset], DecorationTestSeed, World, Options))
                continue;
            FVoxelDecoratedArea Area;
            FVoxelDecoratedArea::Build(*World.Decorator, *World.Terrain, FIntPoint::ZeroValue, 4, true, false, Area);
            int64 Writes = 0;
            for (const FVoxelDecorateStats& Stats : Area.Decorates)
                Writes += Stats.Writes;
            const uint64 Hash = HashColumns(Area.Columns);
            const uint64 WantHash = Case == 2 ? TreeHashes[Preset] : Case ? PlantHashes[Preset] : GoldenHashes[Preset];
            const int64 WantWrites = Case == 2 ? TreeWrites[Preset] : Case ? PlantWrites[Preset] : GoldenWrites[Preset];
            const TCHAR* Side = Case == 2 ? TEXT("trees on") : Case ? TEXT("plants on, trees off") : TEXT("plants off");
            AddInfo(FString::Printf(
                TEXT("%s, %s: hash 0x%016llx, writes %lld"), DecorationTestPresets[Preset], Side, Hash, Writes));
            TestTrue(FString::Printf(TEXT("%s, %s: hash 0x%016llx is the golden 0x%016llx"),
                         DecorationTestPresets[Preset], Side, Hash, WantHash),
                Hash == WantHash);
            TestTrue(FString::Printf(TEXT("%s, %s: %lld writes are the golden %lld"), DecorationTestPresets[Preset],
                         Side, Writes, WantWrites),
                Writes == WantWrites);
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationSaltStabilityTest, "Unnamed.Voxel.Decoration.SaltStability",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationSaltStabilityTest::RunTest(const FString& Parameters)
{
    // Removing one placed feature leaves every other feature's writes where they were: each has a stream of its own.
    if (!DecorationTestHasBlocks(*this)) return true;
    const FString Removed = TEXT("minecraft:ore_granite_lower");
    FDecorationTestWorld World;
    if (!MakeDecorationWorld(*this, TEXT("overworld"), DecorationTestSeed, World, DecorationTestOptions()))
        return false;
    FVoxelDecorationOptions Reduced = DecorationTestOptions();
    Reduced.Filter = [&Removed](FStringView Id) { return Id != FStringView(Removed); };
    FVoxelDecorator ReducedDecorator;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("the reduced decorator compiles (%s)"), *Error),
            ReducedDecorator.Initialize(World.Graph, *World.Table, World.Terrain->GetBiomeSource()->GetTable(),
                DecorationTestSeed, Reduced, &Error)))
        return false;
    const TArray<FVoxelDecorator::FFeatureInfo> Infos = World.Decorator->GetFeatureInfos();
    int32 RemovedKey = INDEX_NONE;
    for (const FVoxelDecorator::FFeatureInfo& Info : Infos)
        if (Info.Id == Removed) RemovedKey = Info.Key;
    if (!TestTrue(TEXT("the removed feature is compiled"), RemovedKey != INDEX_NONE)) return false;
    TestEqual(TEXT("the reduced decorator's features"), ReducedDecorator.GetFeatureInfos().Num(), Infos.Num() - 1);
    const FIntPoint Columns[4] = {{0, 0}, {7, -4}, {-12, 9}, {31, 22}};
    int32 Compared = 0, WithWrites = 0, Different = 0;
    int64 RemovedWrites = 0, RemovedInReduced = 0;
    for (const FIntPoint& Column : Columns)
    {
        FSnapshotSquare Snapshots;
        GenerateSnapshots(*World.Terrain, Column, 1, Snapshots);
        const FVoxelColumn* Around[9];
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
                Around[FVoxelWriteLog::GetBucket(DX, DY)] = Snapshots.At(Column + FIntPoint(DX, DY));
        FVoxelWriteLog FullLog, ReducedLog;
        World.Decorator->Decorate(Column, Around, FullLog);
        ReducedDecorator.Decorate(Column, Around, ReducedLog);
        const TMap<uint16, TArray<int64>> Full = CollectWrites(FullLog), Mine = CollectWrites(ReducedLog);
        const TArray<int64>* Held = Full.Find(uint16(RemovedKey));
        RemovedWrites += Held ? Held->Num() : 0;
        RemovedInReduced += Mine.Contains(uint16(RemovedKey)) ? 1 : 0;
        const TSet<int64> Taken(Held ? *Held : TArray<int64>());
        for (const FVoxelDecorator::FFeatureInfo& Info : Infos)
        {
            if (Info.Key == RemovedKey) continue;
            const auto Without = [&Taken](const TMap<uint16, TArray<int64>>& Writes, uint16 Key)
            {
                TArray<int64> Out;
                if (const TArray<int64>* Found = Writes.Find(Key))
                    for (const int64 Write : *Found)
                        if (!Taken.Contains(Write)) Out.Add(Write);
                return Out;
            };
            const TArray<int64> InFull = Without(Full, Info.Key), InReduced = Without(Mine, Info.Key);
            ++Compared;
            WithWrites += InFull.IsEmpty() ? 0 : 1;
            if (InFull != InReduced)
            {
                ++Different;
                AddError(FString::Printf(TEXT("column (%d, %d): %s writes %d cells in the full run and %d in the "
                                              "reduced one"),
                    Column.X, Column.Y, *Info.Id, InFull.Num(), InReduced.Num()));
            }
        }
    }
    TestEqual(TEXT("features whose writes moved"), Different, 0);
    TestEqual(TEXT("writes of the removed feature in the reduced run"), RemovedInReduced, int64(0));
    TestTrue(FString::Printf(TEXT("the removed feature wrote in the full run (%lld cells)"), RemovedWrites),
        RemovedWrites > 0);
    AddInfo(FString::Printf(TEXT("%d feature runs compared (%d with writes); the removed feature wrote %lld cells"),
        Compared, WithWrites, RemovedWrites));
    TestTrue(TEXT("other features wrote in the compared columns"), WithWrites > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationOreHistogramTest, "Unnamed.Voxel.Decoration.OreHistogram",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationOreHistogramTest::RunTest(const FString& Parameters)
{
    // Ore blocks per Z in decorated columns (caves off, discard off) against the expectation from the data: tries per
    // column, the height provider, the blob's profile and what the generated columns offer at each Z.
    if (!DecorationTestHasBlocks(*this)) return true;
    FVoxelDecorationOptions Options = DecorationTestOptions();
    Options.bDiscard = false;
    FDecorationTestWorld World;
    if (!MakeDecorationWorld(*this, TEXT("overworld"), DecorationTestSeed, World, Options, false)) return false;
    constexpr int32 Radius = 8;
    const FVoxelBlockTable& Table = *World.Table;
    const int32 BlockLimit = Table.GetBlockLimit(), Height = VoxelConstants::Height;
    FSnapshotSquare Snapshots;
    GenerateSnapshots(*World.Terrain, FIntPoint::ZeroValue, Radius + 2, Snapshots);
    const int32 FinalSide = 2 * Radius + 1, Columns = FinalSide * FinalSide;
    // The blocks of the final region's generated columns, per Z.
    TArray<int32> BlocksAtZ;
    BlocksAtZ.Init(0, Height * BlockLimit);
    {
        FCriticalSection Lock;
        ParallelFor(Columns,
            [&](int32 Index)
            {
                const FVoxelColumn* Column =
                    Snapshots.At(FIntPoint(Index % FinalSide - Radius, Index / FinalSide - Radius));
                TArray<int32> Local;
                Local.Init(0, Height * BlockLimit);
                for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
                    for (int32 Y = 0; Y < 16; ++Y)
                        for (int32 X = 0; X < 16; ++X)
                            ++Local[(Z - VoxelConstants::MinZ) * BlockLimit +
                                Table.Get(Column->GetState(X, Y, Z)).Block];
                FScopeLock Scope(&Lock);
                for (int32 Cell = 0; Cell < Local.Num(); ++Cell)
                    BlocksAtZ[Cell] += Local[Cell];
            });
    }
    const double CellsPerZ = double(Columns) * 256.;
    const TArray<FVoxelFeatureEntry>& Placed = World.Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)];
    for (const TCHAR* Name : {TEXT("redstone"), TEXT("lapis"), TEXT("diamond"), TEXT("iron"), TEXT("coal")})
    {
        const FString Prefix = FString(TEXT("minecraft:ore_")) + Name;
        FVoxelDecorationOptions OreOptions = Options;
        OreOptions.Filter = [Prefix](FStringView Id) { return Id.StartsWith(Prefix); };
        if (!InitDecorator(*this, World, OreOptions)) return false;
        const TArray<FVoxelDecorator::FFeatureInfo> Infos = World.Decorator->GetFeatureInfos();
        if (!TestTrue(FString::Printf(TEXT("%s: placed features compiled"), Name), Infos.Num() > 0)) continue;

        // The expectation, feature by feature.
        TArray<double> Expected;
        Expected.Init(0, Height);
        // Its variance: blobs put several cells at one Z together, so a count there varies more than a Poisson's.
        TArray<double> Variance;
        Variance.Init(0, Height);
        TSet<uint16> OreStates;
        FString Features;
        FVoxelBlockTags Tags(World.Graph, Table);
        for (const FVoxelDecorator::FFeatureInfo& Info : Infos)
        {
            const int32 Node = Placed[World.Graph.FindEntry(EVoxelFeatureRegistry::PlacedFeature, Info.Id)].Node;
            double Tries = 1;
            FVoxelIntProvider HeightProvider;
            bool bHeight = false;
            const FVoxelFeatureValue* Placement = World.Graph.FindField(Node, TEXT("placement"));
            FString Error;
            for (int32 Item = 0; Placement && Item < Placement->Count; ++Item)
            {
                const int32 Modifier = World.Graph.GetNode(World.Graph.Items[Placement->Index + Item]);
                const FStringView Type = World.Graph.GetType(Modifier);
                if (Type == TEXT("minecraft:count"))
                {
                    FVoxelIntProvider Count;
                    TArray<double> Distribution;
                    VoxelNumberProviders::CompileInt(
                        World.Graph, *World.Graph.FindField(Modifier, TEXT("count")), Count, &Error);
                    Count.GetDistribution(Distribution);
                    double Mean = 0;
                    for (int32 Value = 0; Value < Distribution.Num(); ++Value)
                        Mean += (Count.Min + Value) * Distribution[Value];
                    Tries *= Mean;
                }
                else if (Type == TEXT("minecraft:rarity_filter"))
                    Tries /= World.Graph.FindField(Modifier, TEXT("chance"))->Number;
                else if (Type == TEXT("minecraft:height_range"))
                    bHeight = VoxelNumberProviders::CompileHeight(
                        World.Graph, *World.Graph.FindField(Modifier, TEXT("height")), HeightProvider, &Error);
            }
            FVoxelOreConfig Config;
            TArray<FVoxelOreTarget> Targets;
            FString Skip;
            const int32 FeatureNode = World.Graph.GetNode(*World.Graph.FindField(Node, TEXT("feature")));
            if (!TestTrue(FString::Printf(TEXT("%s compiles (%s%s)"), *Info.Id, *Error, *Skip),
                    bHeight &&
                        VoxelOreFeature::Compile(
                            World.Graph, FeatureNode, Tags, Table, Config, Targets, Skip, &Error) &&
                        Skip.IsEmpty()))
                continue;
            for (const FVoxelOreTarget& Target : Targets)
                OreStates.Add(Target.State);
            // The blob's mean cells at each Z relative to its origin block.
            TMap<int32, double> Profile, Squares;
            TArray<FIntVector> Cells;
            constexpr int32 Blobs = 100000;
            for (int32 Blob = 0; Blob < Blobs; ++Blob)
            {
                FVoxelXoroshiro Random(VoxelRandom::Hash64(5, VoxelRandom::Salt("ore_profile"), Config.Size, Blob));
                VoxelOreFeature::GetBlobCells(Config.Size, FIntVector::ZeroValue, Random, Cells);
                TMap<int32, int32> Local;
                for (const FIntVector& Cell : Cells)
                    ++Local.FindOrAdd(Cell.Z);
                for (const TPair<int32, int32>& Pair : Local)
                {
                    Profile.FindOrAdd(Pair.Key) += double(Pair.Value) / Blobs;
                    Squares.FindOrAdd(Pair.Key) += double(Pair.Value) * Pair.Value / Blobs;
                }
            }
            TArray<double> Origins;
            HeightProvider.GetDistribution(Origins);
            // What fraction of the generated cells at each Z one of the targets takes.
            TArray<double> Fraction;
            Fraction.Init(0, Height);
            for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
            {
                double Matching = 0;
                for (int32 Block = 0; Block < BlockLimit; ++Block)
                {
                    const int32 Count = BlocksAtZ[(Z - VoxelConstants::MinZ) * BlockLimit + Block];
                    if (!Count) continue;
                    for (const FVoxelOreTarget& Target : Targets)
                        if (Target.Rule.Test(Block, Z))
                        {
                            Matching += Count;
                            break;
                        }
                }
                Fraction[Z - VoxelConstants::MinZ] = Matching / CellsPerZ;
            }
            for (int32 Origin = 0; Origin < Origins.Num(); ++Origin)
                for (const TPair<int32, double>& Cell : Profile)
                {
                    const int32 Z = HeightProvider.Min + Origin + Cell.Key;
                    if (Z >= VoxelConstants::MinZ && Z <= VoxelConstants::MaxZ)
                    {
                        const double Weight = Tries * Columns * Origins[Origin] * Fraction[Z - VoxelConstants::MinZ];
                        Expected[Z - VoxelConstants::MinZ] += Weight * Cell.Value;
                        Variance[Z - VoxelConstants::MinZ] += Weight * Squares.FindRef(Cell.Key);
                    }
                }
            Features += FString::Printf(TEXT(" %s(%.2f tries)"), *Info.Id, Tries);
        }

        // The observation: ore blocks per Z the features placed in the final columns (ore the generated column already
        // held, such as an ore vein's since S4, is not theirs).
        const TArray<FColumnPtr> Final = RunStages(*World.Decorator, Snapshots, Radius);
        TArray<double> Observed;
        Observed.Init(0, Height);
        {
            FCriticalSection Lock;
            ParallelFor(Final.Num(),
                [&](int32 Index)
                {
                    TArray<double> Local;
                    Local.Init(0, Height);
                    const FVoxelColumn* Generated = Snapshots.At(Final[Index]->GetPosition());
                    for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
                        for (int32 Y = 0; Y < 16; ++Y)
                            for (int32 X = 0; X < 16; ++X)
                            {
                                const uint16 State = Final[Index]->GetState(X, Y, Z);
                                if (OreStates.Contains(State) && State != Generated->GetState(X, Y, Z))
                                    Local[Z - VoxelConstants::MinZ] += 1;
                            }
                    FScopeLock Scope(&Lock);
                    for (int32 Cell = 0; Cell < Height; ++Cell)
                        Observed[Cell] += Local[Cell];
                });
        }
        double TotalObserved = 0, TotalExpected = 0;
        for (int32 Cell = 0; Cell < Height; ++Cell)
            TotalObserved += Observed[Cell], TotalExpected += Expected[Cell];
        // Bins of consecutive Z until 20 are expected, the expectation scaled to the observed total.
        const double Scale = TotalExpected > 0 ? TotalObserved / TotalExpected : 0;
        double Chi2 = 0, ChiClustered = 0, BinObserved = 0, BinExpected = 0, BinVariance = 0;
        int32 Bins = 0;
        TArray<TTuple<double, double, double>> BinList;
        for (int32 Cell = 0; Cell < Height; ++Cell)
        {
            BinObserved += Observed[Cell], BinExpected += Expected[Cell] * Scale, BinVariance += Variance[Cell] * Scale;
            if (BinExpected >= 20)
            {
                BinList.Add({BinObserved, BinExpected, BinVariance});
                BinObserved = BinExpected = BinVariance = 0;
            }
        }
        if (BinExpected > 0 || BinObserved > 0)
        {
            if (BinList.IsEmpty())
                BinList.Add({BinObserved, BinExpected, BinVariance});
            else
            {
                BinList.Last().Get<0>() += BinObserved;
                BinList.Last().Get<1>() += BinExpected;
                BinList.Last().Get<2>() += BinVariance;
            }
        }
        for (const TTuple<double, double, double>& Bin : BinList)
        {
            const double Square = FMath::Square(Bin.Get<0>() - Bin.Get<1>());
            Chi2 += Bin.Get<1>() > 0 ? Square / Bin.Get<1>() : 0;
            ChiClustered += Bin.Get<2>() > 0 ? Square / Bin.Get<2>() : 0;
            ++Bins;
        }
        const int32 Df = FMath::Max(Bins - 1, 1);
        // The score with a Poisson's variance is logged; the pass mark uses the blobs' variance.
        const double PoissonZ = (Chi2 - Df) / FMath::Sqrt(2. * Df);
        const double Z = (ChiClustered - Df) / FMath::Sqrt(2. * Df);
        AddInfo(FString::Printf(
            TEXT("%s: observed %.0f, expected %.0f (ratio %.3f); %d bins, chi2 %.1f (z %.2f with a Poisson's "
                 "variance), %.1f with the blobs' (z %.2f);%s"),
            Name, TotalObserved, TotalExpected, TotalExpected > 0 ? TotalObserved / TotalExpected : 0., Bins, Chi2,
            PoissonZ, ChiClustered, Z, *Features));
        TestTrue(FString::Printf(TEXT("%s: shape z %.2f below 6"), Name, Z), Z < 6);
        TestTrue(FString::Printf(
                     TEXT("%s: observed %.0f within 10%% of the expected %.0f"), Name, TotalObserved, TotalExpected),
            TotalExpected > 0 && FMath::Abs(TotalObserved / TotalExpected - 1.) <= 0.1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationHeightmapTagsTest, "Unnamed.Voxel.Decoration.HeightmapTags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationHeightmapTagsTest::RunTest(const FString& Parameters)
{
    // The heightmaps' motion rules against the jar's two tags, block by block.
    if (!DecorationTestHasBlocks(*this)) return true;
    FVoxelFeatureGraph Graph;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
            VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
        return false;
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    const auto Expand = [&](const TCHAR* Tag, TSet<int32>& Out)
    {
        const int32 Index = Graph.FindTag(Tag);
        TArray<FString> Blocks;
        if (!TestTrue(FString::Printf(TEXT("%s is in the graph"), Tag), Index != INDEX_NONE) ||
            !TestTrue(FString::Printf(TEXT("%s expands (%s)"), Tag, *Error), Graph.ExpandTag(Index, Blocks, &Error)))
            return false;
        int32 Missing = 0;
        for (const FString& Id : Blocks)
        {
            const int32 Block = Table->FindBlock(Id);
            if (Block == INDEX_NONE)
                ++Missing;
            else
                Out.Add(Block);
        }
        AddInfo(FString::Printf(TEXT("%s: %d blocks, %d not in the table"), Tag, Blocks.Num(), Missing));
        return true;
    };
    TSet<int32> Motion, NoLeaves;
    if (!Expand(TEXT("minecraft:blocks_motion_in_heightmap"), Motion) ||
        !Expand(TEXT("minecraft:blocks_motion_in_heightmap_no_leaves"), NoLeaves))
        return false;
    int32 Blocks = 0;
    TArray<FString> Disagreements;
    for (int32 Index = 0; Index < Registry->GetBlockCount(); ++Index)
    {
        const FName Id = Registry->GetBlockAt(Index).Id;
        const uint16 State = Table->FindState(Id);
        if (!State) continue;
        const FVoxelStateInfo& Info = Table->Get(State);
        ++Blocks;
        const bool bOurs = Info.bSolid || Info.IsFluid();
        const bool bOursNoLeaves = bOurs && !Info.bLeaves;
        const bool bTag = Motion.Contains(Info.Block) || Info.IsFluid();
        const bool bTagNoLeaves = NoLeaves.Contains(Info.Block) || Info.IsFluid();
        if (bOurs != bTag)
            Disagreements.Add(FString::Printf(TEXT("%s: ours %d, tag %d (motion)"), *Id.ToString(), bOurs, bTag));
        if (bOursNoLeaves != bTagNoLeaves)
            Disagreements.Add(
                FString::Printf(TEXT("%s: ours %d, tag %d (no leaves)"), *Id.ToString(), bOursNoLeaves, bTagNoLeaves));
    }
    // S2's check found one disagreement, recorded rather than changed (it would move the heightmaps of generated
    // snow): powder snow holds the player in our table, but 26.3's heightmap tags leave it out (entities sink into
    // it). Any other block fails.
    int32 Unknown = 0;
    for (const FString& Line : Disagreements)
        if (Line.StartsWith(TEXT("powder_snow:")))
            AddInfo(TEXT("known: ") + Line);
        else
        {
            AddError(Line);
            ++Unknown;
        }
    AddInfo(FString::Printf(TEXT("%d blocks, %d disagreements"), Blocks, Disagreements.Num()));
    TestTrue(TEXT("blocks checked"), Blocks > 0);
    TestEqual(TEXT("blocks whose motion rule disagrees with the tags, beyond powder snow"), Unknown, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDecorationOrderTest, "Unnamed.Voxel.Decoration.Order",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelDecorationOrderTest::RunTest(const FString& Parameters)
{
    // The run order keeps each biome's list order, and its merge keys rise along it.
    if (!DecorationTestHasBlocks(*this)) return true;
    FDecorationTestWorld World;
    if (!MakeDecorationWorld(*this, TEXT("overworld"), DecorationTestSeed, World, DecorationTestOptions()))
        return false;
    const TArray<FVoxelDecorator::FFeatureInfo> Infos = World.Decorator->GetFeatureInfos();
    TMap<FString, int32> Position;
    for (int32 Index = 0; Index < Infos.Num(); ++Index)
        Position.Add(Infos[Index].Id, Index);
    TestTrue(FString::Printf(TEXT("placed features compiled (%d)"), Infos.Num()), Infos.Num() > 0);
    int32 Pairs = 0, Broken = 0;
    const TArray<FVoxelFeatureEntry>& Placed = World.Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)];
    for (const FVoxelBiomeDecoration& Biome : World.Graph.Biomes)
        for (int32 Step = 0; Step < Biome.Steps.Num(); ++Step)
        {
            int32 Previous = INDEX_NONE;
            for (const int32 Entry : Biome.Steps[Step])
            {
                const int32* Found = Position.Find(Placed[Entry].Id);
                if (!Found) continue;
                if (Infos[*Found].Step != Step) ++Broken;
                if (Previous != INDEX_NONE)
                {
                    ++Pairs;
                    Broken += *Found > Previous ? 0 : 1;
                }
                Previous = *Found;
            }
        }
    TestEqual(TEXT("biome list pairs out of order"), Broken, 0);
    int32 KeyBroken = 0;
    for (int32 Index = 1; Index < Infos.Num(); ++Index)
        KeyBroken += Infos[Index].Key > Infos[Index - 1].Key && Infos[Index].Step >= Infos[Index - 1].Step ? 0 : 1;
    TestEqual(TEXT("keys that do not rise along the run order"), KeyBroken, 0);
    AddInfo(FString::Printf(
        TEXT("%d features, %d biomes, %d consecutive pairs checked"), Infos.Num(), World.Graph.Biomes.Num(), Pairs));
    TestTrue(TEXT("pairs checked"), Pairs > 0);
    return true;
}

#endif
