#include "Algo/Reverse.h"
#include "Async/ParallelFor.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Decoration/Features/VoxelPatchFeatures.h"
#include "Decoration/Features/VoxelTreeFeature.h"
#include "Decoration/VoxelBlockPredicates.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelRuleTests.h"
#include "Decoration/VoxelStateProviders.h"
#include "Decoration/VoxelWriteLog.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Hash/CityHash.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/VoxelColumn.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelWorldgenReader.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // Trees per configuration: the fits' N, against the probes' 100 (16 for a ceiling).
    constexpr int32 TreeTestCount = 2000;
    constexpr uint64 TreeTestSeed = 263;
    // For manual runs: 0 none, 1 base_height one higher, 2 the foliage radius one wider (both on the configurations
    // RunTreeProbes compiles; Trees.Shapes must fail). With 0, every test must pass.
    constexpr int32 TreeTestMutation = 0;
    // For manual runs: the number of 100-tree self-samples per configuration that check the distribution rules'
    // calibration (RunTreeProbes); 0 in the suite.
    constexpr int32 TreeTestCalibration = 0;

    // One tree probe (facts/feature_probes/tree_*.json, scout J's tool): the parts the tests read. The probe's x, z
    // and y are our X, Y and Z.
    struct FTreeProbe
    {
        struct FLayer
        {
            FString Block;
            int32 From = 0, To = 0;
        };
        // A box of one block around each origin: X and Y relative to it, Z absolute.
        struct FBox
        {
            FIntVector Min, Max;
            FString Block;
        };

        FString Name, Feature, Variant;
        int32 OriginZ = 60, Tries = 0, Placed = 0;
        TArray<FLayer> Layers;
        TArray<FBox> Prep;
        // Per placed tree.
        TArray<double> Logs, Leaves;
        // Leaves vanilla leaves farther than 6 from a log (we drop them), and the dirt under the trunk, per tree.
        double Bfs7 = 0, Dirt = 0;
        TMap<int32, int64> TopHist, Comp6Hist, Comp26Hist;
        int32 Reach = 0, Comp26Max = 0, OriginLogsMin = 0, OriginLogsMax = 0;
        TSet<FString> Blocks;
        // The leaf layers by dy from the highest log: (dx, dy) to the share of trees with a leaf there.
        int32 ProfileCount = 0;
        TMap<int32, TMap<FIntPoint, double>> Occupancy;
        // A ceiling probe's dy (variant "_ceil<H>"), or 0.
        int32 Ceiling = 0;
    };

    // Our trees on a probe's column, measured as the probe measures.
    struct FTreeRun
    {
        int32 Tries = 0, Placed = 0;
        // Failed tries that still emitted a write.
        int32 FailedWrites = 0;
        TArray<double> Logs, Leaves, Cells, Ground;
        TMap<int32, int64> TopHist, Comp6Hist, Comp26Hist;
        int32 Reach = 0, TopMax = MIN_int32, OriginLogsMin = MAX_int32, OriginLogsMax = MIN_int32;
        TSet<int32> Blocks;
        // The blocks of every write but the soil under the trunk (alter_ground's rule in CompareTrees).
        TSet<int32> NonSoilBlocks;
        TMap<int32, TMap<FIntPoint, int32>> Occupancy;
        // Leaves whose distance is not the BFS from the tree's own logs through its own leaves (1 to 6), or that are
        // persistent.
        int32 BadDistances = 0, Persistent = 0;
    };

    TSharedPtr<FJsonObject> LoadTreeJson(const FString& File)
    {
        FString Text;
        TSharedPtr<FJsonObject> Object;
        if (!FFileHelper::LoadFileToString(Text, *File) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object))
            return nullptr;
        return Object;
    }

    TMap<int32, int64> ReadTreeCounts(const TSharedPtr<FJsonObject>* Object)
    {
        TMap<int32, int64> Counts;
        if (Object && Object->IsValid())
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
                Counts.Add(FCString::Atoi(*Pair.Key), int64(Pair.Value->AsNumber()));
        return Counts;
    }

    // A stats object's field ({n, min, p5, p50, p95, max, mean}), or Default.
    double ReadTreeStat(const FJsonObject& Probe, const TCHAR* Key, const TCHAR* Field, double Default = 0)
    {
        const TSharedPtr<FJsonObject>* Stats;
        double Value;
        return Probe.TryGetObjectField(Key, Stats) && (*Stats)->TryGetNumberField(Field, Value) ? Value : Default;
    }

    FString StripNamespace(const FString& Id)
    {
        return Id.StartsWith(TEXT("minecraft:")) ? Id.RightChop(10) : Id;
    }

    bool ReadTreeProbe(const FString& File, FTreeProbe& Out, FString& OutError)
    {
        const TSharedPtr<FJsonObject> Json = LoadTreeJson(File);
        if (!Json.IsValid())
        {
            OutError = TEXT("does not read as JSON");
            return false;
        }
        Out.Name = Json->GetStringField(TEXT("probe"));
        Out.Feature = Json->GetStringField(TEXT("feature"));
        Json->TryGetStringField(TEXT("variant"), Out.Variant);
        Out.OriginZ = int32(Json->GetNumberField(TEXT("y")));
        if (Out.Variant.StartsWith(TEXT("_ceil"))) Out.Ceiling = FCString::Atoi(*Out.Variant.RightChop(5));
        const TSharedPtr<FJsonObject>& World = Json->GetObjectField(TEXT("world"));
        for (const TSharedPtr<FJsonValue>& Layer : World->GetArrayField(TEXT("layers")))
        {
            const TArray<TSharedPtr<FJsonValue>>& Items = Layer->AsArray();
            if (Items.Num() != 3) continue;
            Out.Layers.Add(
                {StripNamespace(Items[0]->AsString()), int32(Items[1]->AsNumber()), int32(Items[2]->AsNumber())});
        }
        // prep: one box [x0, y0, z0, x1, y1, z1, block] or a list of them.
        const TArray<TSharedPtr<FJsonValue>>* Prep;
        if (Json->TryGetArrayField(TEXT("prep"), Prep) && Prep->Num())
        {
            const auto AddBox = [&Out](const TArray<TSharedPtr<FJsonValue>>& Box)
            {
                if (Box.Num() != 7) return;
                Out.Prep.Add(
                    {FIntVector(int32(Box[0]->AsNumber()), int32(Box[2]->AsNumber()), int32(Box[1]->AsNumber())),
                        FIntVector(int32(Box[3]->AsNumber()), int32(Box[5]->AsNumber()), int32(Box[4]->AsNumber())),
                        StripNamespace(Box[6]->AsString())});
            };
            if ((*Prep)[0]->Type == EJson::Array)
                for (const TSharedPtr<FJsonValue>& Box : *Prep)
                    AddBox(Box->AsArray());
            else
                AddBox(*Prep);
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Response : Json->GetObjectField(TEXT("responses"))->Values)
        {
            Out.Tries += int32(Response.Value->AsNumber());
            if (Response.Key == TEXT("placed")) Out.Placed = int32(Response.Value->AsNumber());
        }
        for (const TSharedPtr<FJsonValue>& Each : Json->GetArrayField(TEXT("per_placement")))
        {
            const TSharedPtr<FJsonObject>& Placement = Each->AsObject();
            if (Placement->GetStringField(TEXT("result")) != TEXT("placed")) continue;
            Out.Logs.Add(Placement->GetNumberField(TEXT("logs")));
            Out.Leaves.Add(Placement->GetNumberField(TEXT("leaves")));
        }
        Out.Bfs7 = ReadTreeStat(*Json, TEXT("leaf_bfs7"), TEXT("mean"));
        const TSharedPtr<FJsonObject>* Object;
        if (Json->TryGetObjectField(TEXT("log_top_hist"), Object)) Out.TopHist = ReadTreeCounts(Object);
        if (Json->TryGetObjectField(TEXT("log_comp6_hist"), Object)) Out.Comp6Hist = ReadTreeCounts(Object);
        if (Json->TryGetObjectField(TEXT("log_comp26_hist"), Object)) Out.Comp26Hist = ReadTreeCounts(Object);
        Out.Reach = int32(FMath::Max(ReadTreeStat(*Json, TEXT("leaf_reach_h"), TEXT("max")),
            ReadTreeStat(*Json, TEXT("log_reach_h"), TEXT("max"))));
        Out.Comp26Max = int32(ReadTreeStat(*Json, TEXT("log_comp26"), TEXT("max")));
        Out.OriginLogsMin = int32(ReadTreeStat(*Json, TEXT("origin_column_logs"), TEXT("min")));
        Out.OriginLogsMax = int32(ReadTreeStat(*Json, TEXT("origin_column_logs"), TEXT("max")));
        if (Json->TryGetObjectField(TEXT("blocks"), Object))
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Block : (*Object)->Values)
            {
                Out.Blocks.Add(Block.Key);
                if (Block.Key == TEXT("dirt"))
                    Block.Value->AsObject()->TryGetNumberField(TEXT("mean_per_tree"), Out.Dirt);
            }
        if (Json->TryGetObjectField(TEXT("profile"), Object))
        {
            Out.ProfileCount = int32((*Object)->GetNumberField(TEXT("n")));
            const TArray<TSharedPtr<FJsonValue>>* Layers;
            if ((*Object)->TryGetArrayField(TEXT("leaf_layers"), Layers))
                for (const TSharedPtr<FJsonValue>& Each : *Layers)
                {
                    const TSharedPtr<FJsonObject>& Layer = Each->AsObject();
                    TMap<FIntPoint, double>& Shares =
                        Out.Occupancy.Add(int32(Layer->GetNumberField(TEXT("dy_from_top_log"))));
                    for (const TPair<FString, TSharedPtr<FJsonValue>>& Cell :
                        Layer->GetObjectField(TEXT("occupancy"))->Values)
                    {
                        FString DX, DZ;
                        if (Cell.Key.Split(TEXT(","), &DX, &DZ))
                            Shares.Add(FIntPoint(FCString::Atoi(*DX), FCString::Atoi(*DZ)), Cell.Value->AsNumber());
                    }
                }
        }
        return true;
    }

    // Q(A, X), the regularized upper incomplete gamma function (series below A + 1, continued fraction above).
    double UpperGammaQ(double A, double X)
    {
        if (X <= 0) return 1;
        const double LogFront = -X + A * std::log(X) - std::lgamma(A);
        if (X < A + 1)
        {
            double Term = 1 / A, Sum = Term;
            for (int32 N = 1; N < 1000 && FMath::Abs(Term) > FMath::Abs(Sum) * 1e-15; ++N)
            {
                Term *= X / (A + N);
                Sum += Term;
            }
            return FMath::Clamp(1 - Sum * std::exp(LogFront), 0., 1.);
        }
        constexpr double Tiny = 1e-300;
        double B = X + 1 - A, C = 1 / Tiny, D = 1 / B, H = D;
        for (int32 I = 1; I < 1000; ++I)
        {
            const double An = -I * (I - A);
            B += 2;
            D = An * D + B;
            D = FMath::Abs(D) < Tiny ? Tiny : D;
            C = B + An / C;
            C = FMath::Abs(C) < Tiny ? Tiny : C;
            D = 1 / D;
            H *= D * C;
            if (FMath::Abs(D * C - 1) < 1e-15) break;
        }
        return FMath::Clamp(std::exp(LogFront) * H, 0., 1.);
    }

    // The p value that two histograms come from one distribution (chi-square homogeneity), as the fits' harness.
    double ChiSquareHomogeneity(const TMap<int32, int64>& A, const TMap<int32, int64>& B)
    {
        TSet<int32> Keys;
        double CountA = 0, CountB = 0;
        for (const TPair<int32, int64>& Bin : A)
            Keys.Add(Bin.Key), CountA += Bin.Value;
        for (const TPair<int32, int64>& Bin : B)
            Keys.Add(Bin.Key), CountB += Bin.Value;
        if (CountA == 0 || CountB == 0 || Keys.Num() < 2) return 1;
        double Chi2 = 0;
        for (const int32 Key : Keys)
        {
            const double InA = double(A.FindRef(Key)), InB = double(B.FindRef(Key)), Total = InA + InB;
            const double ExpectA = Total * CountA / (CountA + CountB), ExpectB = Total * CountB / (CountA + CountB);
            Chi2 += FMath::Square(InA - ExpectA) / ExpectA + FMath::Square(InB - ExpectB) / ExpectB;
        }
        return UpperGammaQ((Keys.Num() - 1) / 2., Chi2 / 2);
    }

    // The p value of a histogram against a known distribution (chi-square goodness of fit); 0 when a count lands where
    // the distribution has none.
    double ChiSquareFit(const TMap<int32, int64>& Observed, const TMap<int32, double>& Distribution)
    {
        double Count = 0, Chi2 = 0;
        for (const TPair<int32, int64>& Bin : Observed)
        {
            if (!Distribution.Contains(Bin.Key)) return 0;
            Count += Bin.Value;
        }
        for (const TPair<int32, double>& Bin : Distribution)
            Chi2 += FMath::Square(Observed.FindRef(Bin.Key) - Count * Bin.Value) / (Count * Bin.Value);
        return Distribution.Num() < 2 || Count == 0 ? 1 : UpperGammaQ((Distribution.Num() - 1) / 2., Chi2 / 2);
    }

    // A straight trunk's top log above the origin under open sky, exactly (the data): base_height + uniform(0..a) +
    // uniform(0..b) logs, the top one at height - 1.
    TMap<int32, double> GetStraightTops(const FVoxelTreeConfig& Config)
    {
        const int32 Base = Config.Trunk.GetMaxInt(TEXT("base_height")),
                    A = Config.Trunk.GetMaxInt(TEXT("height_rand_a")),
                    B = Config.Trunk.GetMaxInt(TEXT("height_rand_b"));
        TMap<int32, double> Tops;
        for (int32 RollA = 0; RollA <= A; ++RollA)
            for (int32 RollB = 0; RollB <= B; ++RollB)
                Tops.FindOrAdd(Base + RollA + RollB - 1) += 1. / ((A + 1) * (B + 1));
        return Tops;
    }

    // The two-sided binomial p value of K successes in N tries at rate P.
    double BinomialTwoSided(int32 K, int32 N, double P)
    {
        if (P <= 0) return K == 0 ? 1 : 0;
        if (P >= 1) return K == N ? 1 : 0;
        const auto LogPmf = [N, P](int32 I)
        {
            return std::lgamma(N + 1.) - std::lgamma(I + 1.) - std::lgamma(N - I + 1.) + I * std::log(P) +
                (N - I) * std::log(1 - P);
        };
        const double AtK = LogPmf(K);
        double Sum = 0;
        for (int32 I = 0; I <= N; ++I)
            if (const double Log = LogPmf(I); Log <= AtK + 1e-9) Sum += std::exp(Log);
        return FMath::Min(Sum, 1.);
    }

    void MeanAndDeviation(const TArray<double>& Values, double& OutMean, double& OutDeviation)
    {
        OutMean = OutDeviation = 0;
        if (Values.IsEmpty()) return;
        for (const double Value : Values)
            OutMean += Value;
        OutMean /= Values.Num();
        for (const double Value : Values)
            OutDeviation += FMath::Square(Value - OutMean);
        OutDeviation = FMath::Sqrt(OutDeviation / FMath::Max(1, Values.Num() - 1));
    }

    int32 CountParts(const TSet<FIntVector>& Cells, bool bCorners)
    {
        TSet<FIntVector> Seen;
        TArray<FIntVector> Stack;
        int32 Parts = 0;
        for (const FIntVector& Start : Cells)
        {
            if (Seen.Contains(Start)) continue;
            ++Parts;
            Seen.Add(Start);
            Stack.Add(Start);
            while (Stack.Num())
            {
                const FIntVector Cell = Stack.Pop(EAllowShrinking::No);
                for (int32 DZ = -1; DZ <= 1; ++DZ)
                    for (int32 DY = -1; DY <= 1; ++DY)
                        for (int32 DX = -1; DX <= 1; ++DX)
                        {
                            if (!bCorners && FMath::Abs(DX) + FMath::Abs(DY) + FMath::Abs(DZ) != 1) continue;
                            const FIntVector Next = Cell + FIntVector(DX, DY, DZ);
                            if (Cells.Contains(Next) && !Seen.Contains(Next))
                            {
                                Seen.Add(Next);
                                Stack.Add(Next);
                            }
                        }
            }
        }
        return Parts;
    }

    // The leaves' distances as the probes saw them after settling: a BFS from Logs through face-adjacent Leaves, at
    // most 7.
    TMap<FIntVector, int32> GetBfsDistances(const TSet<FIntVector>& Logs, const TSet<FIntVector>& Leaves)
    {
        static const FIntVector Steps[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        TMap<FIntVector, int32> Distance;
        TArray<FIntVector> Queue;
        for (const FIntVector& Log : Logs)
            for (const FIntVector& Step : Steps)
                if (Leaves.Contains(Log + Step) && !Distance.Contains(Log + Step))
                {
                    Distance.Add(Log + Step, 1);
                    Queue.Add(Log + Step);
                }
        for (int32 Head = 0; Head < Queue.Num(); ++Head)
        {
            const int32 Next = Distance[Queue[Head]] + 1;
            if (Next > 7) continue;
            for (const FIntVector& Step : Steps)
                if (Leaves.Contains(Queue[Head] + Step) && !Distance.Contains(Queue[Head] + Step))
                {
                    Distance.Add(Queue[Head] + Step, Next);
                    Queue.Add(Queue[Head] + Step);
                }
        }
        for (const FIntVector& Leaf : Leaves)
            if (!Distance.Contains(Leaf)) Distance.Add(Leaf, 7);
        return Distance;
    }

    // Everything one test run needs: the reference's graph and tags, the survival table, and the compiled sets and
    // providers the trees share.
    struct FTreeTestKit
    {
        TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
        FVoxelFeatureGraph Graph;
        TUniquePtr<FVoxelBlockTags> Tags;
        FVoxelPlantSurvival Survival;
        FVoxelBlockPredicates Predicates;
        FVoxelStateProviders Providers;
        TMap<uint16, uint16> UpperOf;

        // False (with a warning) when DA_Blocks is missing, or on a read error.
        bool Initialize(FAutomationTestBase& Test)
        {
            if (!Table->FindState(TEXT("stone")))
            {
                Test.AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
                return false;
            }
            FString Error;
            if (!Test.TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
                    VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
                return false;
            Tags = MakeUnique<FVoxelBlockTags>(Graph, *Table);
            if (!Test.TestTrue(FString::Printf(TEXT("the survival table builds (%s)"), *Error),
                    Survival.Initialize(*Table, *Tags, &Error)))
                return false;
            Predicates.Initialize(*Table, Survival);
            Providers.Initialize(*Table, Predicates, TreeTestSeed);
            return true;
        }

        // A tree feature (a configured feature id) compiled; false with OutSkip saying why it is not run here.
        bool Compile(FAutomationTestBase& Test, const FString& Feature, FVoxelTreeConfig& Out, FString& OutSkip)
        {
            int32 Entry = Graph.FindEntry(EVoxelFeatureRegistry::Feature, Feature);
            if (Entry == INDEX_NONE) Entry = Graph.FindEntry(EVoxelFeatureRegistry::Feature, StripNamespace(Feature));
            const int32 Node =
                Entry == INDEX_NONE ? INDEX_NONE : Graph.Entries[int32(EVoxelFeatureRegistry::Feature)][Entry].Node;
            if (Node == INDEX_NONE)
            {
                OutSkip = TEXT("not in the graph");
                return false;
            }
            if (Graph.GetType(Node) != TEXT("minecraft:tree"))
            {
                OutSkip = FString(Graph.GetType(Node)) + TEXT(" (S9b)");
                return false;
            }
            FString Error;
            if (!Test.TestTrue(FString::Printf(TEXT("%s compiles (%s)"), *Feature, *Error),
                    VoxelTreeFeature::Compile(Graph, Node, *Tags, *Table, Predicates, Providers, Out, OutSkip, &Error)))
                return false;
            return OutSkip.IsEmpty();
        }
    };

    // The probe's column: its layers in every cell, then its prep boxes around Origin.
    TSharedRef<FVoxelColumn> MakeProbeColumn(const FVoxelBlockTable& Table, const FTreeProbe& Probe)
    {
        TArray<uint16> Dense;
        Dense.SetNumZeroed(FVoxelColumn::DenseVolume);
        for (const FTreeProbe::FLayer& Layer : Probe.Layers)
        {
            const uint16 State = Table.FindState(FName(*Layer.Block));
            for (int32 Z = FMath::Max(Layer.From, VoxelConstants::MinZ);
                Z <= FMath::Min(Layer.To, VoxelConstants::MaxZ); ++Z)
                for (int32 Cell = 0; Cell < FVoxelColumn::Area; ++Cell)
                    Dense[FVoxelColumn::GetDenseIndex(0, 0, Z) + Cell] = State;
        }
        TSharedRef<FVoxelColumn> Column = MakeShared<FVoxelColumn>();
        Column->Build(Dense, Table);
        return Column;
    }

    // One placed tree's writes, measured.
    void AddTree(
        const FVoxelBlockTable& Table, FIntVector Origin, TConstArrayView<FVoxelPlantWrite> Writes, FTreeRun& Run)
    {
        static const FName Distance(TEXT("distance")), Persistent(TEXT("persistent")), True(TEXT("true"));
        TSet<FIntVector> Logs, Leaves, All;
        for (const FVoxelPlantWrite& Write : Writes)
        {
            const FVoxelStateInfo& Info = Table.Get(Write.State);
            Run.Blocks.Add(Info.Block);
            All.Add(Write.Cell);
            if (Write.Mode == EVoxelWriteMode::IfReplaceable) (Info.bLeaves ? Leaves : Logs).Add(Write.Cell);
        }
        int32 Ground = 0;
        for (const FVoxelPlantWrite& Write : Writes)
            if (Write.Mode != EVoxelWriteMode::IfReplaceable && Logs.Contains(Write.Cell + FIntVector(0, 0, 1)))
                ++Ground;
            else
                Run.NonSoilBlocks.Add(Table.Get(Write.State).Block);
        const TMap<FIntVector, int32> Bfs = GetBfsDistances(Logs, Leaves);
        for (const FVoxelPlantWrite& Write : Writes)
        {
            if (Write.Mode != EVoxelWriteMode::IfReplaceable || !Table.Get(Write.State).bLeaves) continue;
            const int32 Saved = FCString::Atoi(*Table.GetValue(Write.State, Distance).ToString());
            Run.BadDistances += Saved != Bfs.FindRef(Write.Cell) || Saved > 6;
            Run.Persistent += Table.GetValue(Write.State, Persistent) == True;
        }
        int32 Top = MIN_int32, OriginLogs = 0;
        for (const FIntVector& Log : Logs)
        {
            Top = FMath::Max(Top, Log.Z);
            OriginLogs += Log.X == Origin.X && Log.Y == Origin.Y;
        }
        int32 Reach = 0;
        for (const FIntVector& Cell : All)
            Reach = FMath::Max(Reach, FMath::Max(FMath::Abs(Cell.X - Origin.X), FMath::Abs(Cell.Y - Origin.Y)));
        ++Run.Placed;
        Run.Logs.Add(Logs.Num());
        Run.Leaves.Add(Leaves.Num());
        Run.Ground.Add(Ground);
        Run.Cells.Add(All.Num());
        Run.Reach = FMath::Max(Run.Reach, Reach);
        Run.OriginLogsMin = FMath::Min(Run.OriginLogsMin, OriginLogs);
        Run.OriginLogsMax = FMath::Max(Run.OriginLogsMax, OriginLogs);
        if (Logs.IsEmpty()) return;
        Run.TopMax = FMath::Max(Run.TopMax, Top - Origin.Z);
        ++Run.TopHist.FindOrAdd(Top - Origin.Z);
        ++Run.Comp6Hist.FindOrAdd(CountParts(Logs, false));
        ++Run.Comp26Hist.FindOrAdd(CountParts(Logs, true));
        for (const FIntVector& Leaf : Leaves)
            ++Run.Occupancy.FindOrAdd(Leaf.Z - Top).FindOrAdd(FIntPoint(Leaf.X - Origin.X, Leaf.Y - Origin.Y));
    }

    // Count trees of Config on the probe's column at (8, 8, the probe's y), each from its own stream. The trees only
    // read the working copy (Emit keeps their writes), so they all see the probe's untouched column.
    void SimulateTrees(const FTreeTestKit& Kit, const FVoxelTreeConfig& Config, const FTreeProbe& Probe, int32 Count,
        uint64 Seed, FTreeRun& Out)
    {
        const FVoxelBlockTable& Table = *Kit.Table;
        const TSharedRef<FVoxelColumn> Column = MakeProbeColumn(Table, Probe);
        const FVoxelColumn* Around[9];
        for (const FVoxelColumn*& Each : Around)
            Each = &*Column;
        FVoxelDecorationContext Context(FIntPoint(0, 0), Around, &Table);
        const FIntVector Origin(8, 8, Probe.OriginZ);
        for (const FTreeProbe::FBox& Box : Probe.Prep)
        {
            const uint16 State = Table.FindState(FName(*Box.Block));
            for (int32 Z = Box.Min.Z; Z <= Box.Max.Z; ++Z)
                for (int32 Y = Box.Min.Y; Y <= Box.Max.Y; ++Y)
                    for (int32 X = Box.Min.X; X <= Box.Max.X; ++X)
                        Context.SetState(Origin.X + X, Origin.Y + Y, Z, State);
        }
        TArray<FVoxelPlantWrite> Writes;
        const auto Emit = [&Writes](const FVoxelPlantWrite& Write) { Writes.Add(Write); };
        const FVoxelPlantEnvironment Env{
            Table, Kit.Survival, Kit.Predicates, Kit.Providers, Context, Kit.UpperOf, Emit};
        Out.Tries = Count;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Writes.Reset();
            FVoxelXoroshiro Random(VoxelRandom::Hash64(Seed, VoxelRandom::Salt("tree test"), Index, 0));
            if (VoxelTreeFeature::Place(Config, Origin, Random, Env))
                AddTree(Table, Origin, Writes, Out);
            else
                Out.FailedWrites += Writes.Num() > 0;
        }
    }

    // Logs, leaves and cells per tree: ours within 5% of the probe's means unless inside the two samples' noise. The
    // z bound is 3, not the fits' 2.5: a run tests about 80 means, so 2.5 (1.2% each) fails a correct model about
    // once a run.
    void CheckMeans(const FTreeProbe& Probe, const FTreeRun& Run, FString& OutLine, TArray<FString>& Failures)
    {
        TArray<double> ProbeLeaves, ProbeCells;
        for (int32 Index = 0; Index < Probe.Logs.Num(); ++Index)
        {
            ProbeLeaves.Add(Probe.Leaves[Index] - Probe.Bfs7);
            ProbeCells.Add(Probe.Logs[Index] + Probe.Leaves[Index] - Probe.Bfs7 + Probe.Dirt);
        }
        struct FMeanCheck
        {
            const TCHAR* Name;
            const TArray<double>& Theirs;
            const TArray<double>& Ours;
        };
        const FMeanCheck Means[] = {{TEXT("logs"), Probe.Logs, Run.Logs}, {TEXT("leaves"), ProbeLeaves, Run.Leaves},
            {TEXT("cells"), ProbeCells, Run.Cells}};
        for (const FMeanCheck& Mean : Means)
        {
            double TheirMean, TheirDeviation, OurMean, OurDeviation;
            MeanAndDeviation(Mean.Theirs, TheirMean, TheirDeviation);
            MeanAndDeviation(Mean.Ours, OurMean, OurDeviation);
            const double Relative = TheirMean ? (OurMean - TheirMean) / TheirMean : 0;
            const double Error = FMath::Sqrt(FMath::Square(TheirDeviation) / FMath::Max(Mean.Theirs.Num(), 1) +
                FMath::Square(OurDeviation) / FMath::Max(Mean.Ours.Num(), 1));
            const double Z = Error > 0 ? (OurMean - TheirMean) / Error : (OurMean == TheirMean ? 0 : 99);
            OutLine += FString::Printf(
                TEXT(", %s %.2f/%.2f (%+.1f%%, z %+.1f)"), Mean.Name, OurMean, TheirMean, 100 * Relative, Z);
            if (FMath::Abs(Relative) > 0.05 && FMath::Abs(Z) > 3)
                Failures.Add(FString::Printf(TEXT("%s %+.1f%%"), Mean.Name, 100 * Relative));
        }
    }

    // The leaf layers against the probe's, cell by cell (a layer's dX, dY): the probe's count of trees with a leaf
    // there (its share, rounded to 0.01, of ProfileCount trees) against the share of both samples pooled, as an exact
    // two-sided binomial p, Bonferroni-adjusted over the cells (the smallest p times the cells, at most 1). It fails
    // below 0.01, so a correct model fails about 1 probe in 100. The handoff's max z of 4 (with the probe's share for
    // the variance, then with the pooled share) failed fancy oak's own 100-tree samples 64, then 31 times in 100: its
    // thousands of outer cells hold leaves in under 5% of trees, and counts that rare have a tail a z score misreads.
    double GetOccupancyP(const FTreeProbe& Probe, const FTreeRun& Run, FString& OutWorst)
    {
        const int32 ProbeN = FMath::Max(Probe.ProfileCount, 1);
        const double OurN = FMath::Max(Run.Placed, 1);
        TArray<double> LogFactorial;
        LogFactorial.SetNum(ProbeN + 1);
        LogFactorial[0] = 0;
        for (int32 K = 1; K <= ProbeN; ++K)
            LogFactorial[K] = LogFactorial[K - 1] + std::log(double(K));
        const auto TwoSided = [&LogFactorial, ProbeN](int32 K, double Rate)
        {
            const double LogRate = std::log(Rate), LogMiss = std::log(1 - Rate);
            const auto LogPmf = [&](int32 I)
            {
                return LogFactorial[ProbeN] - LogFactorial[I] - LogFactorial[ProbeN - I] + I * LogRate +
                    (ProbeN - I) * LogMiss;
            };
            const double AtK = LogPmf(K);
            double Sum = 0;
            for (int32 I = 0; I <= ProbeN; ++I)
                if (const double Log = LogPmf(I); Log <= AtK + 1e-9) Sum += std::exp(Log);
            return FMath::Min(Sum, 1.);
        };
        TSet<int32> Layers;
        for (const auto& Layer : Probe.Occupancy)
            Layers.Add(Layer.Key);
        for (const auto& Layer : Run.Occupancy)
            Layers.Add(Layer.Key);
        static const TMap<FIntPoint, double> NoShares;
        static const TMap<FIntPoint, int32> NoCounts;
        int32 CellCount = 0;
        double Smallest = 1;
        for (const int32 Layer : Layers)
        {
            const TMap<FIntPoint, double>* Theirs = Probe.Occupancy.Find(Layer);
            const TMap<FIntPoint, int32>* Ours = Run.Occupancy.Find(Layer);
            TSet<FIntPoint> Cells;
            for (const auto& Cell : Theirs ? *Theirs : NoShares)
                Cells.Add(Cell.Key);
            for (const auto& Cell : Ours ? *Ours : NoCounts)
                Cells.Add(Cell.Key);
            for (const FIntPoint& Cell : Cells)
            {
                ++CellCount;
                const double P = Theirs ? Theirs->FindRef(Cell) : 0, V = (Ours ? Ours->FindRef(Cell) : 0) / OurN;
                const int32 K = FMath::Clamp(FMath::RoundToInt32(P * ProbeN), 0, ProbeN);
                const double Rate =
                    FMath::Clamp((K + V * OurN) / (ProbeN + OurN), 0.5 / (ProbeN + OurN), 1 - 0.5 / (ProbeN + OurN));
                // A screen: within two standard errors the exact p is far above any adjusted bound.
                if (FMath::Abs(double(K) / ProbeN - V) < 2 * FMath::Sqrt(Rate * (1 - Rate) * (1. / ProbeN + 1 / OurN)))
                    continue;
                const double Exact = TwoSided(K, Rate);
                if (Exact < Smallest)
                {
                    Smallest = Exact;
                    OutWorst = FString::Printf(
                        TEXT("dy %d (%d, %d) probe %.2f ours %.3f p %.1e"), Layer, Cell.X, Cell.Y, P, V, Exact);
                }
            }
        }
        return FMath::Min(1., Smallest * CellCount);
    }
    // Compares one probe with our run; returns the failures (empty when it passes) and a summary line.
    TArray<FString> CompareTrees(const FTreeTestKit& Kit, const FVoxelTreeConfig& Config, const FTreeProbe& Probe,
        const FTreeRun& Run, FString& OutLine)
    {
        TArray<FString> Failures;
        const double Rate = Run.Tries ? double(Run.Placed) / Run.Tries : 0;
        const double RateP = BinomialTwoSided(Probe.Placed, Probe.Tries, Rate);
        OutLine = FString::Printf(TEXT("%s: placed %.0f%%/%.0f%% (p %.2f)"), *Probe.Name, 100 * Rate,
            100. * Probe.Placed / FMath::Max(Probe.Tries, 1), RateP);
        if (RateP < 0.01) Failures.Add(TEXT("placed rate"));
        if (Run.FailedWrites) Failures.Add(FString::Printf(TEXT("%d failed tries emitted writes"), Run.FailedWrites));
        if (Probe.Placed == 0 || Run.Placed == 0)
        {
            if ((Probe.Placed == 0) != (Run.Placed == 0)) Failures.AddUnique(TEXT("placed none against some"));
            return Failures;
        }
        const bool bSmall = Probe.Tries < 50;
        // Exact rules: the leaves' distance and persistence; the blocks the probe saw; the 26-connected trunk.
        if (Run.BadDistances) Failures.Add(FString::Printf(TEXT("%d leaves off the BFS distance"), Run.BadDistances));
        if (Run.Persistent) Failures.Add(FString::Printf(TEXT("%d persistent leaves"), Run.Persistent));
        // A tree whose alter_ground decorator is left out (until S9c builds it) writes its below-trunk soil where the
        // probe's decorator then wrote its own block (podzol under the giant trunks), so the probe has no dirt: its
        // soil under the trunk is checked exactly instead (one per trunk column, scout J's exact rule 6) and left out
        // of the block set. Every other tree keeps the probe's blocks and dirt (the S9b manager's call).
        const bool bGroundAltered = Config.SkippedDecorators.ContainsByPredicate(
            [](const FString& Type) { return Type.EndsWith(TEXT("alter_ground")); });
        for (const int32 Block : bGroundAltered ? Run.NonSoilBlocks : Run.Blocks)
        {
            bool bSeen = false;
            for (const FString& Id : Probe.Blocks)
                bSeen |= Kit.Table->FindBlock(Id) == Block;
            if (!bSeen) Failures.Add(FString::Printf(TEXT("block %d not in the probe's blocks"), Block));
        }
        int32 Comp26Max = 0;
        for (const auto& Bin : Run.Comp26Hist)
            Comp26Max = FMath::Max(Comp26Max, Bin.Key);
        if (Comp26Max > FMath::Max(Probe.Comp26Max, 1))
            Failures.Add(FString::Printf(TEXT("26-connected parts %d (probe at most %d)"), Comp26Max, Probe.Comp26Max));
        // A straight trunk's height lies in [base, base + a + b] (the data).
        if (EVoxelTrunkPlacer(Config.Trunk.Type) == EVoxelTrunkPlacer::Straight && !bSmall)
        {
            const int32 Base = Config.Trunk.GetMaxInt(TEXT("base_height"));
            const int32 Highest =
                Base + Config.Trunk.GetMaxInt(TEXT("height_rand_a")) + Config.Trunk.GetMaxInt(TEXT("height_rand_b"));
            if (Run.OriginLogsMin < Base || Run.OriginLogsMax > Highest)
                Failures.Add(FString::Printf(
                    TEXT("trunk heights %d..%d outside %d..%d"), Run.OriginLogsMin, Run.OriginLogsMax, Base, Highest));
        }
        if (!bSmall)
        {
            CheckMeans(Probe, Run, OutLine, Failures);
            double DirtMean, DirtDeviation;
            MeanAndDeviation(Run.Ground, DirtMean, DirtDeviation);
            const double SoilWanted = bGroundAltered
                ? double(VoxelTrunkPlacers::GetFootprint(EVoxelTrunkPlacer(Config.Trunk.Type)).Num())
                : Probe.Dirt;
            if (FMath::Abs(DirtMean - SoilWanted) > 0.05)
                Failures.Add(FString::Printf(TEXT("soil under the trunk %.2f (%s %.2f)"), DirtMean,
                    bGroundAltered ? TEXT("one per trunk column, alter_ground left out:") : TEXT("probe"), SoilWanted));
        }
        // The top-log histogram: a straight trunk's (open sky) is known exactly from the data, so ours and the probe's
        // are each tested against it and only the probe's own sampling counts; elsewhere ours against the probe's.
        if (EVoxelTrunkPlacer(Config.Trunk.Type) == EVoxelTrunkPlacer::Straight && !Probe.Ceiling)
        {
            const TMap<int32, double> Tops = GetStraightTops(Config);
            const double OursP = ChiSquareFit(Run.TopHist, Tops), ProbeP = ChiSquareFit(Probe.TopHist, Tops);
            OutLine += FString::Printf(TEXT(", top p %.2f (probe %.2f, against the data)"), OursP, ProbeP);
            if (OursP < 0.01) Failures.Add(TEXT("top-log histogram (ours against the data)"));
            if (ProbeP < 0.01) Failures.Add(TEXT("top-log histogram (the probe against the data)"));
        }
        else if (!Probe.Ceiling)
        {
            // (A ceiling probe's 2 to 16 trees are too few for a histogram: its exact top-log bound below decides.)
            const double TopP = ChiSquareHomogeneity(Run.TopHist, Probe.TopHist);
            OutLine += FString::Printf(TEXT(", top p %.2f"), TopP);
            if (TopP < 0.01) Failures.Add(TEXT("top-log histogram"));
        }
        if (Probe.Ceiling)
        {
            // Scout J's exact rule 7: the feature size keeps layers 0 to height + 1 free, so a straight trunk's top
            // log (at height - 1) lies at the ceiling's dy - 3 or lower. Other trunks top out elsewhere (fancy oak's
            // at its height: fancy_oak_ceil5 placed 2 of 16 with the top log at dy 3), so for them only the two
            // layers under the ceiling stay free of logs.
            const int32 Highest =
                Probe.Ceiling - (EVoxelTrunkPlacer(Config.Trunk.Type) == EVoxelTrunkPlacer::Straight ? 3 : 2);
            OutLine += FString::Printf(TEXT(", top max %d (ceiling %d)"), Run.TopMax, Probe.Ceiling);
            if (Run.TopMax > Highest) Failures.Add(TEXT("a top log too close to the ceiling"));
        }
        if (!bSmall)
        {
            if (Probe.ProfileCount)
            {
                FString Worst;
                const double OccupancyP = GetOccupancyP(Probe, Run, Worst);
                OutLine += FString::Printf(TEXT(", occupancy p %.3f (%s)"), OccupancyP, *Worst);
                if (OccupancyP < 0.01) Failures.Add(TEXT("leaf occupancy"));
            }
            OutLine += FString::Printf(TEXT(", reach %d/%d"), Run.Reach, Probe.Reach);
            if (Run.Reach > Probe.Reach) Failures.Add(TEXT("reach"));
            const double Comp6P = ChiSquareHomogeneity(Run.Comp6Hist, Probe.Comp6Hist);
            OutLine += FString::Printf(TEXT(", 6-connected parts p %.2f"), Comp6P);
            if (Comp6P < 0.01) Failures.Add(TEXT("6-connected log parts"));
        }
        return Failures;
    }

    // A sample of our own trees dressed as a probe, for the calibration below: its per-tree counts and its leaf
    // layers' shares, rounded to 0.01 as the probes keep them.
    FTreeProbe MakeSampleProbe(const FTreeRun& Sample)
    {
        FTreeProbe Probe;
        Probe.Logs = Sample.Logs;
        Probe.Leaves = Sample.Leaves;
        double Deviation;
        MeanAndDeviation(Sample.Ground, Probe.Dirt, Deviation);
        Probe.ProfileCount = Sample.Placed;
        for (const auto& Layer : Sample.Occupancy)
        {
            TMap<FIntPoint, double>& Shares = Probe.Occupancy.Add(Layer.Key);
            for (const auto& Cell : Layer.Value)
                Shares.Add(Cell.Key, FMath::RoundToDouble(100. * Cell.Value / FMath::Max(Sample.Placed, 1)) / 100);
        }
        return Probe;
    }

    // TreeTestMutation's change to a compiled configuration.
    void ApplyTreeTestMutation(FVoxelTreeConfig& Config)
    {
        const auto Raise = [](FVoxelTreePlacerConfig& Placer, FName Name)
        {
            for (TPair<FName, FVoxelIntProvider>& Field : Placer.Ints)
                if (Field.Key == Name)
                {
                    ++Field.Value.Min;
                    ++Field.Value.Max;
                }
        };
        if (TreeTestMutation == 1) Raise(Config.Trunk, TEXT("base_height"));
        if (TreeTestMutation == 2) Raise(Config.Foliage, TEXT("radius"));
    }

    // Runs every tree probe Filter accepts: compiles each probe's feature, simulates TreeTestCount trees in parallel
    // and compares. Probes of types or placers not built yet are listed as skipped.
    bool RunTreeProbes(FAutomationTestBase& Test, TFunctionRef<bool(const FTreeProbe&)> Filter, int32& OutRun)
    {
        OutRun = 0;
        FTreeTestKit Kit;
        if (!Kit.Initialize(Test)) return true;
        const FString Folder = FPaths::Combine(VoxelWorldgen::GetReferenceRoot(), TEXT("facts/feature_probes"));
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *FPaths::Combine(Folder, TEXT("tree_*.json")), true, false);
        Files.Sort();
        Test.TestEqual(TEXT("tree probes"), Files.Num(), 82);
        TArray<FTreeProbe> Probes;
        TArray<FVoxelTreeConfig> Configs;
        TArray<FString> Skipped;
        for (const FString& File : Files)
        {
            FTreeProbe Probe;
            FString Error;
            if (!Test.TestTrue(FString::Printf(TEXT("%s reads (%s)"), *File, *Error),
                    ReadTreeProbe(FPaths::Combine(Folder, File), Probe, Error)))
                return false;
            if (!Filter(Probe)) continue;
            FVoxelTreeConfig Config;
            FString Skip;
            if (!Kit.Compile(Test, Probe.Feature, Config, Skip))
            {
                if (!Skip.IsEmpty()) Skipped.Add(FString::Printf(TEXT("%s (%s)"), *Probe.Name, *Skip));
                continue;
            }
            ApplyTreeTestMutation(Config);
            Probes.Add(MoveTemp(Probe));
            Configs.Add(MoveTemp(Config));
        }
        TArray<FTreeRun> Runs;
        Runs.SetNum(Probes.Num());
        ParallelFor(Probes.Num(), [&](int32 Index)
            { SimulateTrees(Kit, Configs[Index], Probes[Index], TreeTestCount, TreeTestSeed, Runs[Index]); });
        for (int32 Index = 0; Index < Probes.Num(); ++Index)
        {
            FString Line;
            const TArray<FString> Failures = CompareTrees(Kit, Configs[Index], Probes[Index], Runs[Index], Line);
            Test.AddInfo(Line);
            for (const FString& Failure : Failures)
                Test.AddError(FString::Printf(TEXT("%s: %s"), *Probes[Index].Name, *Failure));
        }
        if (Skipped.Num()) Test.AddInfo(FString::Printf(TEXT("skipped: %s"), *FString::Join(Skipped, TEXT(", "))));
        // For manual runs (TreeTestCalibration > 0): each configuration's own 100-tree samples, scored as probes
        // against its TreeTestCount trees. A correct model must rarely fail the distribution rules this way.
        if (TreeTestCalibration > 0)
        {
            TArray<FIntPoint> Fails;
            Fails.SetNumZeroed(Probes.Num());
            ParallelFor(Probes.Num(),
                [&](int32 Index)
                {
                    if (Probes[Index].Tries < 50) return;
                    for (int32 Sample = 0; Sample < TreeTestCalibration; ++Sample)
                    {
                        FTreeRun Own;
                        SimulateTrees(Kit, Configs[Index], Probes[Index], 100, TreeTestSeed + 1 + Sample, Own);
                        const FTreeProbe AsProbe = MakeSampleProbe(Own);
                        FString Line, Worst;
                        TArray<FString> MeanFailures;
                        CheckMeans(AsProbe, Runs[Index], Line, MeanFailures);
                        Fails[Index].X += GetOccupancyP(AsProbe, Runs[Index], Worst) < 0.01;
                        Fails[Index].Y += MeanFailures.Num() > 0;
                    }
                });
            for (int32 Index = 0; Index < Probes.Num(); ++Index)
                if (Probes[Index].Tries >= 50)
                    Test.AddInfo(
                        FString::Printf(TEXT("calibration %s: occupancy p under 0.01 in %d of %d samples, a mean "
                                             "failed in %d"),
                            *Probes[Index].Name, Fails[Index].X, TreeTestCalibration, Fails[Index].Y));
        }
        OutRun = Probes.Num();
        return true;
    }

    // A flat plains world for the decorator (the border and modifier tests): the reference's decoration data with some
    // placed features replaced by test placements, plains alone in the biome table, and columns built by hand.
    struct FTreeTestWorld
    {
        TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
        FVoxelFeatureGraph Graph;
        FVoxelBiomeTable Biomes;
        uint8 Plains = 0;

        // Replaced maps a placed feature's name ("trees_plains") to the text read for it. False (with a warning) when
        // DA_Blocks is missing, or on a read error.
        bool Initialize(FAutomationTestBase& Test, const TMap<FString, FString>& Replaced)
        {
            if (!Table->FindState(TEXT("stone")))
            {
                Test.AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
                return false;
            }
            const FString Root = VoxelWorldgen::GetReferenceRoot();
            const FString Data = FPaths::Combine(Root, TEXT("data/minecraft"));
            const auto Load = [&Replaced, &Data](const FString& Path, FString& OutText)
            {
                const FString Prefix = TEXT("worldgen/placed_feature/");
                if (Path.StartsWith(Prefix) && Path.EndsWith(TEXT(".json")))
                    if (const FString* Text = Replaced.Find(Path.Mid(Prefix.Len()).LeftChop(5)))
                    {
                        OutText = *Text;
                        return true;
                    }
                return FFileHelper::LoadFileToString(OutText, *FPaths::Combine(Data, Path));
            };
            FString Error;
            const bool bRead = VoxelWorldgen::ReadFeatures(Load, Graph, Error);
            if (!Test.TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error), bRead)) return false;
            const TArray<FString> Ids = {TEXT("minecraft:plains")};
            const bool bBiomes = VoxelWorldgen::ReadBiomes(Root, Ids, Biomes, Error);
            if (!Test.TestTrue(FString::Printf(TEXT("plains reads (%s)"), *Error), bBiomes)) return false;
            const int32 Index = Biomes.FindBiome(TEXT("minecraft:plains"));
            if (!Test.TestTrue(TEXT("plains is in the biome table"), Index != INDEX_NONE)) return false;
            Plains = uint8(Index);
            return true;
        }

        // A decorator running only the placed features Names lists (without "minecraft:"), each of which must
        // compile.
        bool MakeDecorator(FAutomationTestBase& Test, const TArray<FString>& Names, FVoxelDecorator& Out) const
        {
            FVoxelDecorationOptions Options;
            Options.Filter = [Names](FStringView Id) { return Names.Contains(StripNamespace(FString(Id))); };
            FString Error;
            const bool bCompiled = Out.Initialize(Graph, *Table, Biomes, TreeTestSeed, Options, &Error);
            if (!Test.TestTrue(FString::Printf(TEXT("the decorator compiles (%s)"), *Error), bCompiled)) return false;
            for (const FString& Name : Names)
                if (!Test.TestTrue(FString::Printf(TEXT("%s compiles"), *Name), FindKey(Out, Name) != MAX_uint16))
                    return false;
            return true;
        }

        // A placed feature's merge key, or MAX_uint16 when it did not compile.
        static uint16 FindKey(const FVoxelDecorator& Decorator, const FString& Name)
        {
            for (const FVoxelDecorator::FFeatureInfo& Info : Decorator.GetFeatureInfos())
                if (StripNamespace(Info.Id) == Name) return Info.Key;
            return MAX_uint16;
        }

        // A column at Position: stone, three dirt and a grass block up to FloorZ, water above it up to WaterZ, then
        // air; plains in every quart.
        TSharedRef<FVoxelColumn> MakeColumn(FIntPoint Position, int32 FloorZ = 63, int32 WaterZ = 63) const
        {
            const uint16 Stone = Table->FindState(TEXT("stone")), Dirt = Table->FindState(TEXT("dirt")),
                         Grass = Table->FindState(TEXT("grass_block")), Water = Table->FindState(TEXT("water"));
            TArray<uint16> Dense;
            Dense.SetNumZeroed(FVoxelColumn::DenseVolume);
            for (int32 Z = VoxelConstants::MinZ; Z <= FMath::Max(FloorZ, WaterZ); ++Z)
            {
                const uint16 State = Z < FloorZ - 3 ? Stone : Z < FloorZ ? Dirt : Z == FloorZ ? Grass : Water;
                for (int32 Cell = 0; Cell < FVoxelColumn::Area; ++Cell)
                    Dense[FVoxelColumn::GetDenseIndex(0, 0, Z) + Cell] = State;
            }
            TSharedRef<FVoxelColumn> Column = MakeShared<FVoxelColumn>(Position);
            Column->Build(Dense, *Table);
            FVoxelBiomeStorage Quarts;
            Quarts.Init(Position, TreeTestSeed);
            for (int32 Z = 0; Z < FVoxelBiomeStorage::Layers; ++Z)
                for (int32 Y = 0; Y < FVoxelBiomeStorage::Width; ++Y)
                    for (int32 X = 0; X < FVoxelBiomeStorage::Width; ++X)
                        Quarts.SetQuart(X, Y, Z, Plains);
            Column->SetBiomes(MoveTemp(Quarts));
            return Column;
        }
    };

    // A placed feature of oak with the given placement modifiers (JSON objects).
    FString MakeOakPlacement(const TArray<FString>& Modifiers)
    {
        return FString::Printf(
            TEXT("{\"feature\": \"minecraft:oak\", \"placement\": [%s]}"), *FString::Join(Modifiers, TEXT(", ")));
    }

    // The offset modifier by (X, Y, Z) (the reference's x, z and y). A placement starts at the column's minimum corner
    // and the world's bottom.
    FString MakeOffsetModifier(int32 X, int32 Y, int32 Z)
    {
        return FString::Printf(TEXT("{\"type\": \"minecraft:offset\", \"x\": %d, \"y\": %d, \"z\": %d}"), X, Z, Y);
    }

    const TCHAR* const TreeTestSurface = TEXT("{\"type\": \"minecraft:heightmap\", \"heightmap\": \"OCEAN_FLOOR\"}");

    // The positions that reached a feature in one Decorate of column (0, 0), with Column all around.
    int32 DecorateTries(const FVoxelDecorator& Decorator, const FVoxelColumn& Column)
    {
        const FVoxelColumn* Around[9];
        for (const FVoxelColumn*& Each : Around)
            Each = &Column;
        FVoxelWriteLog Log;
        FVoxelDecorateStats Stats;
        Decorator.Decorate(FIntPoint::ZeroValue, Around, Log, &Stats);
        return Stats.PlantTries;
    }

    // Columns in the square of Radius around (0, 0), row by row.
    int32 GetSquareIndex(FIntPoint Position, int32 Radius)
    {
        return (Position.Y + Radius) * (2 * Radius + 1) + Position.X + Radius;
    }

    FIntPoint GetSquarePosition(int32 Index, int32 Radius)
    {
        return FIntPoint(Index % (2 * Radius + 1) - Radius, Index / (2 * Radius + 1) - Radius);
    }

    // The column a world cell lies in: X and Y divided by 16, rounded down.
    FIntPoint GetColumnOf(FIntVector Cell)
    {
        return FIntPoint(Cell.X >> 4, Cell.Y >> 4);
    }

    // The final columns of the square of Radius around (0, 0), row by row: every column within Radius + 1 decorated
    // against Snapshots (the square of Radius + 2), then the square finalized from the logs' buckets. Order picks the
    // schedule: 0 every Decorate then every Finalize, row by row; 1 the same backwards; 2 the targets shuffled, each
    // decorating the sources it still lacks just before it finalizes; 3 both stages on every worker; 4 each target
    // decorating its own 3 x 3 afresh (DecorateAlone's oracle). OutLogs gets the Decorates' logs (orders 0 to 3).
    TArray<FVoxelColumn> RunTreeArea(const FVoxelDecorator& Decorator,
        TConstArrayView<TSharedRef<FVoxelColumn>> Snapshots, int32 Radius, int32 Order,
        TArray<FVoxelWriteLog>* OutLogs = nullptr)
    {
        const auto SnapshotAt = [&](FIntPoint Position) { return &*Snapshots[GetSquareIndex(Position, Radius + 2)]; };
        const auto Decorate = [&](FIntPoint Source, FVoxelWriteLog& Out)
        {
            const FVoxelColumn* Around[9];
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                    Around[FVoxelWriteLog::GetBucket(DX, DY)] = SnapshotAt(Source + FIntPoint(DX, DY));
            Decorator.Decorate(Source, Around, Out);
        };
        const auto Finalize =
            [&](FIntPoint Target, TFunctionRef<const FVoxelWriteLog&(FIntPoint)> GetLog, FVoxelColumn& Out)
        {
            const FVoxelWriteBucket* Incoming[9];
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                    Incoming[FVoxelWriteLog::GetBucket(DX, DY)] =
                        &GetLog(Target + FIntPoint(DX, DY)).Buckets[FVoxelWriteLog::GetBucket(-DX, -DY)];
            Decorator.Finalize(*SnapshotAt(Target), Incoming, Out);
        };
        const int32 Sources = FMath::Square(2 * Radius + 3), Targets = FMath::Square(2 * Radius + 1);
        TArray<FVoxelWriteLog> Logs;
        Logs.SetNum(Sources);
        const auto LogAt = [&Logs, Radius](FIntPoint Source) -> const FVoxelWriteLog&
        { return Logs[GetSquareIndex(Source, Radius + 1)]; };
        TArray<FVoxelColumn> Final;
        Final.SetNum(Targets);
        TArray<int32> SourceOrder, TargetOrder;
        for (int32 Index = 0; Index < Sources; ++Index)
            SourceOrder.Add(Index);
        for (int32 Index = 0; Index < Targets; ++Index)
            TargetOrder.Add(Index);
        switch (Order)
        {
        case 0:
        case 1:
            if (Order == 1)
            {
                Algo::Reverse(SourceOrder);
                Algo::Reverse(TargetOrder);
            }
            for (const int32 Index : SourceOrder)
                Decorate(GetSquarePosition(Index, Radius + 1), Logs[Index]);
            for (const int32 Index : TargetOrder)
                Finalize(GetSquarePosition(Index, Radius), LogAt, Final[Index]);
            break;
        case 2:
        {
            FRandomStream Stream{int32(TreeTestSeed)};
            for (int32 Index = TargetOrder.Num() - 1; Index > 0; --Index)
                TargetOrder.Swap(Index, Stream.RandRange(0, Index));
            TBitArray<> Decorated(false, Sources);
            for (const int32 Index : TargetOrder)
            {
                const FIntPoint Target = GetSquarePosition(Index, Radius);
                for (int32 DY = -1; DY <= 1; ++DY)
                    for (int32 DX = -1; DX <= 1; ++DX)
                    {
                        const int32 Source = GetSquareIndex(Target + FIntPoint(DX, DY), Radius + 1);
                        if (Decorated[Source]) continue;
                        Decorate(Target + FIntPoint(DX, DY), Logs[Source]);
                        Decorated[Source] = true;
                    }
                Finalize(Target, LogAt, Final[Index]);
            }
            break;
        }
        case 3:
            ParallelFor(Sources, [&](int32 Index) { Decorate(GetSquarePosition(Index, Radius + 1), Logs[Index]); });
            ParallelFor(Targets, [&](int32 Index) { Finalize(GetSquarePosition(Index, Radius), LogAt, Final[Index]); });
            break;
        default:
            for (const int32 Index : TargetOrder)
            {
                const FIntPoint Target = GetSquarePosition(Index, Radius);
                FVoxelWriteLog Alone[9];
                for (int32 DY = -1; DY <= 1; ++DY)
                    for (int32 DX = -1; DX <= 1; ++DX)
                        Decorate(Target + FIntPoint(DX, DY), Alone[FVoxelWriteLog::GetBucket(DX, DY)]);
                Finalize(
                    Target, [&Alone, Target](FIntPoint Source) -> const FVoxelWriteLog&
                    { return Alone[FVoxelWriteLog::GetBucket(Source.X - Target.X, Source.Y - Target.Y)]; },
                    Final[Index]);
            }
            break;
        }
        if (OutLogs) *OutLogs = MoveTemp(Logs);
        return Final;
    }

    // A final column's states and heightmaps, hashed.
    uint64 HashTreeColumn(const FVoxelColumn& Column)
    {
        TArray<uint16> States;
        States.SetNumUninitialized(FVoxelSection::Volume);
        uint64 Hash = 0;
        for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
        {
            Column.GetSection(Section).Expand(States);
            Hash = CityHash64WithSeed(
                reinterpret_cast<const char*>(States.GetData()), States.Num() * sizeof(uint16), Hash);
        }
        TArray<int32> Heights;
        for (int32 Map = 0; Map < int32(EVoxelHeightmap::Count); ++Map)
            for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
                for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                    Heights.Add(Column.GetHeight(EVoxelHeightmap(Map), X, Y));
        return CityHash64WithSeed(
            reinterpret_cast<const char*>(Heights.GetData()), Heights.Num() * sizeof(int32), Hash);
    }

    // One write of a test tree, in world cells.
    struct FTreeTestWrite
    {
        FIntVector Cell = FIntVector::ZeroValue;
        uint16 State = 0;
        EVoxelWriteMode Mode = EVoxelWriteMode::Force;
    };

    // The writes of placed feature Key in the Decorate of Source (Logs as RunTreeArea gives them for Radius), or
    // none when one of them lands outside the square of Radius.
    TArray<FTreeTestWrite> GetTreeWrites(const TArray<FVoxelWriteLog>& Logs, int32 Radius, FIntPoint Source, uint16 Key)
    {
        TArray<FTreeTestWrite> Out;
        const FVoxelWriteLog& Log = Logs[GetSquareIndex(Source, Radius + 1)];
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
            {
                const FVoxelWriteBucket& Bucket = Log.Buckets[FVoxelWriteLog::GetBucket(DX, DY)];
                for (int32 Run = 0; Run < Bucket.Runs.Num(); ++Run)
                {
                    if (Bucket.Runs[Run].Key != Key) continue;
                    if (FMath::Abs(Source.X + DX) > Radius || FMath::Abs(Source.Y + DY) > Radius) return {};
                    for (int32 Index = Run ? Bucket.Runs[Run - 1].End : 0; Index < Bucket.Runs[Run].End; ++Index)
                    {
                        const FVoxelWrite& Write = Bucket.Writes[Index];
                        Out.Add({FIntVector((Source.X + DX) * VoxelConstants::ColumnSize + Write.GetX(),
                                     (Source.Y + DY) * VoxelConstants::ColumnSize + Write.GetY(), Write.GetZ()),
                            Write.State, Write.GetMode()});
                    }
                }
            }
        return Out;
    }

    // The state at a world cell of RunTreeArea's final columns.
    uint16 GetFinalState(const TArray<FVoxelColumn>& Final, int32 Radius, FIntVector Cell)
    {
        const FIntPoint Column = GetColumnOf(Cell);
        return Final[GetSquareIndex(Column, Radius)].GetState(
            Cell.X - Column.X * VoxelConstants::ColumnSize, Cell.Y - Column.Y * VoxelConstants::ColumnSize, Cell.Z);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTreeShapesTest, "Unnamed.Voxel.Trees.Shapes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTreeShapesTest::RunTest(const FString& Parameters)
{
    // Every built tree configuration against its probe (scout J's oracles, the fits' harness rules): the leaves'
    // distance and persistence, the probe's blocks, one 26-connected trunk and a straight trunk's height exactly; the
    // means, the top-log histogram, leaf occupancy by an exact binomial test per cell, reach and 6-connected parts as
    // distributions. Every built configuration runs: 31 probes since S9b (dark oak, pale oak and mega jungle).
    int32 Ran = 0;
    if (!RunTreeProbes(*this, [](const FTreeProbe& Probe) { return Probe.Ceiling == 0; }, Ran)) return false;
    TestEqual(TEXT("probes run (a placer that stops compiling drops its probes)"), Ran, 31);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTreeFeatureSizeTest, "Unnamed.Voxel.Trees.FeatureSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTreeFeatureSizeTest::RunTest(const FString& Parameters)
{
    // The ceiling probes (a 17 x 17 stone ceiling at dy H, 16 tries): the placed rate, and every placed tree's top log
    // at H - 3 or lower (H - 2 for trunks other than straight): oak, spruce, fancy oak and dark oak, 26 probes since
    // S9b.
    int32 Ran = 0;
    if (!RunTreeProbes(*this, [](const FTreeProbe& Probe) { return Probe.Ceiling != 0; }, Ran)) return false;
    TestEqual(TEXT("ceiling probes run"), Ran, 26);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTreeLeafDistanceTest, "Unnamed.Voxel.Trees.LeafDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTreeLeafDistanceTest::RunTest(const FString& Parameters)
{
    // The leaves' distance exactly, on trees built by hand: a BFS from the builder's own logs through its own
    // face-adjacent leaves, 1 to 6, and 0 (dropped) beyond or without a path; a log already in the working copy, such
    // as another tree's, does not count. Then oak through Place on its probe's column: every emitted leaf's distance is
    // that BFS over the tree's own emitted logs and leaves (so none is 7), and none is persistent.
    FTreeTestKit Kit;
    if (!Kit.Initialize(*this)) return true;
    const FString Folder = FPaths::Combine(VoxelWorldgen::GetReferenceRoot(), TEXT("facts/feature_probes"));
    FTreeProbe Probe;
    FString Error;
    const bool bProbe = ReadTreeProbe(FPaths::Combine(Folder, TEXT("tree_oak.json")), Probe, Error);
    if (!TestTrue(FString::Printf(TEXT("tree_oak.json reads (%s)"), *Error), bProbe)) return false;
    FVoxelTreeConfig Config;
    FString Skip;
    if (!Kit.Compile(*this, Probe.Feature, Config, Skip))
    {
        if (!Skip.IsEmpty()) AddError(FString::Printf(TEXT("%s is not run (%s)"), *Probe.Feature, *Skip));
        return false;
    }

    const FVoxelBlockTable& Table = *Kit.Table;
    TArray<uint16> Dense;
    Dense.SetNumZeroed(FVoxelColumn::DenseVolume);
    const TSharedRef<FVoxelColumn> Air = MakeShared<FVoxelColumn>();
    Air->Build(Dense, Table);
    const FVoxelColumn* Around[9];
    for (const FVoxelColumn*& Each : Around)
        Each = &*Air;
    FVoxelDecorationContext Context(FIntPoint(0, 0), Around, &Table);
    // As Decorate's emit: each write goes into the working copy.
    const auto Emit = [&Context](const FVoxelPlantWrite& Write)
    { Context.SetState(Write.Cell.X, Write.Cell.Y, Write.Cell.Z, Write.State); };
    const FVoxelPlantEnvironment Env{Table, Kit.Survival, Kit.Predicates, Kit.Providers, Context, Kit.UpperOf, Emit};
    // The leaves' distances of a hand-built tree, in cell order.
    const auto GetDistances = [&](TFunctionRef<void(FVoxelTreeBuilder&)> Build)
    {
        FVoxelXoroshiro Random(TreeTestSeed);
        FVoxelTreeBuilder Builder(Config, Env, Random);
        Build(Builder);
        TArray<uint8> Distances;
        VoxelTreeFeature::GetLeafDistances(Builder, Distances);
        TArray<FString> Out;
        for (int32 Index = 0; Index < Builder.Cells.Num(); ++Index)
            if (Builder.Cells[Index].Kind == FVoxelTreeBuilder::ECell::Leaf)
                Out.Add(FString::FromInt(Distances[Index]));
        return FString::Join(Out, TEXT(" "));
    };
    const auto AddChain = [](FVoxelTreeBuilder& Builder, FIntVector From, const TArray<FIntVector>& Steps)
    {
        for (const FIntVector& Step : Steps)
            Builder.SetLeaf(From += Step);
    };
    const FIntVector East(1, 0, 0), Back(-1, 0, 0), South(0, 1, 0), Up(0, 0, 1);

    // One log and a straight chain of 8 leaves, then a chain of 8 that turns along all three axes.
    const FIntVector A(2, 2, 100);
    TestEqual(TEXT("a straight chain"),
        GetDistances(
            [&](FVoxelTreeBuilder& Builder)
            {
                Builder.SetLog(A);
                AddChain(Builder, A, {East, East, East, East, East, East, East, East});
            }),
        FString(TEXT("1 2 3 4 5 6 0 0")));
    TestEqual(TEXT("a turning chain"),
        GetDistances(
            [&](FVoxelTreeBuilder& Builder)
            {
                Builder.SetLog(A);
                AddChain(Builder, A, {East, East, South, Up, Up, South, Back, Back});
            }),
        FString(TEXT("1 2 3 4 5 6 0 0")));
    // Leaves touching the log, and each other, only along an edge or a corner: no path.
    TestEqual(TEXT("leaves touching the log diagonally"),
        GetDistances(
            [&](FVoxelTreeBuilder& Builder)
            {
                Builder.SetLog(A);
                Builder.SetLeaf(A + East + South);
                Builder.SetLeaf(A + East + South + Up);
                Builder.SetLeaf(A + South + Up);
            }),
        FString(TEXT("0 0 0")));
    // Two logs six apart: the nearer wins, and a leaf above the middle is one further.
    TestEqual(TEXT("two logs"),
        GetDistances(
            [&](FVoxelTreeBuilder& Builder)
            {
                Builder.SetLog(A);
                Builder.SetLog(A + East * 6);
                AddChain(Builder, A, {East, East, East, East, East});
                Builder.SetLeaf(A + East * 3 + Up);
            }),
        FString(TEXT("1 2 3 2 1 4")));
    // A log may replace the tree's own leaf (the leaf beyond then counts from it); a leaf never replaces its log.
    const FIntVector B(2, 6, 100);
    TestEqual(TEXT("a log over the tree's own leaf"),
        GetDistances(
            [&](FVoxelTreeBuilder& Builder)
            {
                Builder.SetLog(B);
                TestTrue(TEXT("a leaf goes on air"), Builder.SetLeaf(B + East));
                TestTrue(TEXT("a log replaces the tree's own leaf"), Builder.SetLog(B + East));
                TestFalse(TEXT("a leaf never replaces the tree's log"), Builder.SetLeaf(B + East));
                TestTrue(TEXT("the cell is a log"), Builder.IsLog(B + East));
                Builder.SetLeaf(B + East * 2);
            }),
        FString(TEXT("1")));
    // A log already in the working copy, beside the chain's last leaf, does not count.
    const FIntVector C(2, 10, 100);
    Context.SetState(C.X + 9, C.Y, C.Z, Config.LogStates[2]);
    TestEqual(TEXT("a log in the working copy"),
        GetDistances(
            [&](FVoxelTreeBuilder& Builder)
            {
                Builder.SetLog(C);
                AddChain(Builder, C, {East, East, East, East, East, East, East, East});
            }),
        FString(TEXT("1 2 3 4 5 6 0 0")));
    // Another tree placed through Place (its cells emitted into the working copy), and a chain whose last leaf touches
    // its trunk: that trunk does not count either.
    FVoxelXoroshiro OtherRandom(TreeTestSeed);
    const FIntVector Other(14, 20, 100);
    if (TestTrue(TEXT("another oak places"), VoxelTreeFeature::Place(Config, Other, OtherRandom, Env)))
    {
        TestEqual(TEXT("the other tree's trunk is in the working copy"),
            int32(Context.GetState(Other.X, Other.Y, Other.Z)), int32(Config.LogStates[2]));
        TestEqual(TEXT("a leaf beside another tree's log"),
            GetDistances(
                [&](FVoxelTreeBuilder& Builder)
                {
                    Builder.SetLog(Other - East * 8);
                    AddChain(Builder, Other - East * 8, {East, East, East, East, East, East, East});
                }),
            FString(TEXT("1 2 3 4 5 6 0")));
    }

    // Oak through Place on its probe's column.
    FTreeRun Run;
    SimulateTrees(Kit, Config, Probe, 500, TreeTestSeed, Run);
    double Leaves = 0;
    for (const double Count : Run.Leaves)
        Leaves += Count;
    TestTrue(TEXT("oak places on its probe's column"), Run.Placed > 0 && Leaves > 0);
    TestEqual(TEXT("emitted leaves whose distance is not the BFS over the tree's own logs and leaves, or is 7"),
        Run.BadDistances, 0);
    TestEqual(TEXT("persistent leaves"), Run.Persistent, 0);
    AddInfo(FString::Printf(TEXT("%d of %d oaks placed, %.0f leaves checked"), Run.Placed, Run.Tries, Leaves));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTreeBordersTest, "Unnamed.Voxel.Trees.Borders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTreeBordersTest::RunTest(const FString& Parameters)
{
    // Oaks across column borders through the real Decorate, write log, merge and Finalize, on flat plains: one at every
    // column's corner (local 0, 0), and a pair across every border between columns side by side (local 15, 8 and the
    // next column's 1, 8: trunks two apart, each in the other's canopy), three placed features replaced for the test.
    // Each corner tree comes out whole, its leaves' distances those of the whole tree. In each pair the earlier tree in
    // merge order keeps its logs and the later its own (over the earlier's leaves), no leaf replaces a log, and every
    // cell is what the replay rule gives. Five schedules give the same columns.
    const FString Corner = TEXT("trees_plains"), West = TEXT("flower_plains"), East = TEXT("patch_grass_plain");
    TMap<FString, FString> Replaced;
    Replaced.Add(Corner, MakeOakPlacement({TreeTestSurface}));
    Replaced.Add(West, MakeOakPlacement({MakeOffsetModifier(15, 8, 0), TreeTestSurface}));
    Replaced.Add(East, MakeOakPlacement({MakeOffsetModifier(1, 8, 0), TreeTestSurface}));
    FTreeTestWorld World;
    if (!World.Initialize(*this, Replaced)) return true;
    FVoxelDecorator Decorator;
    if (!World.MakeDecorator(*this, {Corner, West, East}, Decorator)) return false;
    const FVoxelBlockTable& Table = *World.Table;

    constexpr int32 Radius = 2;
    TArray<TSharedRef<FVoxelColumn>> Snapshots;
    for (int32 Index = 0; Index < FMath::Square(2 * Radius + 5); ++Index)
        Snapshots.Add(World.MakeColumn(GetSquarePosition(Index, Radius + 2)));
    TArray<FVoxelWriteLog> Logs;
    const TArray<FVoxelColumn> Final = RunTreeArea(Decorator, Snapshots, Radius, 0, &Logs);
    TArray<uint64> Baseline;
    for (const FVoxelColumn& Column : Final)
        Baseline.Add(HashTreeColumn(Column));
    for (int32 Order = 1; Order <= 4; ++Order)
    {
        const TArray<FVoxelColumn> Other = RunTreeArea(Decorator, Snapshots, Radius, Order);
        int32 Differing = 0;
        for (int32 Index = 0; Index < Other.Num(); ++Index)
            Differing += HashTreeColumn(Other[Index]) != Baseline[Index];
        TestEqual(FString::Printf(TEXT("schedule %d: final columns that differ from the first schedule's"), Order),
            Differing, 0);
    }

    // The corner trees wholly inside the square: sources -1 to 2 on each axis.
    static const FName Distance(TEXT("distance"));
    const uint16 CornerKey = FTreeTestWorld::FindKey(Decorator, Corner);
    int32 Corners = 0, NotFour = 0, Lost = 0, BadDistances = 0, CornerLeaves = 0, LeafModes = 0;
    for (int32 Y = -Radius - 1; Y <= Radius + 1; ++Y)
        for (int32 X = -Radius - 1; X <= Radius + 1; ++X)
        {
            const TArray<FTreeTestWrite> Writes = GetTreeWrites(Logs, Radius, FIntPoint(X, Y), CornerKey);
            if (Writes.IsEmpty()) continue;
            ++Corners;
            TSet<FIntPoint> Columns;
            TSet<FIntVector> TreeLogs, TreeLeaves;
            for (const FTreeTestWrite& Write : Writes)
            {
                Columns.Add(GetColumnOf(Write.Cell));
                Lost += GetFinalState(Final, Radius, Write.Cell) != Write.State;
                const bool bLeaf = Table.Get(Write.State).bLeaves;
                LeafModes += bLeaf && Write.Mode != EVoxelWriteMode::IfReplaceable;
                if (bLeaf)
                    TreeLeaves.Add(Write.Cell);
                else if (Write.Mode == EVoxelWriteMode::IfReplaceable)
                    TreeLogs.Add(Write.Cell);
            }
            NotFour += Columns.Num() != 4;
            CornerLeaves += TreeLeaves.Num();
            const TMap<FIntVector, int32> Bfs = GetBfsDistances(TreeLogs, TreeLeaves);
            for (const FIntVector& Leaf : TreeLeaves)
                BadDistances +=
                    FCString::Atoi(*Table.GetValue(GetFinalState(Final, Radius, Leaf), Distance).ToString()) !=
                    Bfs.FindRef(Leaf);
        }
    TestEqual(TEXT("corner trees wholly inside the square"), Corners, 16);
    TestEqual(TEXT("corner trees not over four columns"), NotFour, 0);
    TestEqual(TEXT("corner trees' writes missing from the final columns"), Lost, 0);
    TestEqual(TEXT("corner trees' leaves not written over air and replaceable_by_trees only"), LeafModes, 0);
    TestEqual(TEXT("corner trees' leaves whose distance is not the whole tree's BFS"), BadDistances, 0);

    // The pairs wholly inside the square: West at source X (-2 to 1), East at X + 1.
    const uint16 WestKey = FTreeTestWorld::FindKey(Decorator, West), EastKey = FTreeTestWorld::FindKey(Decorator, East);
    const bool bWestFirst = WestKey < EastKey;
    int32 Pairs = 0, LostLogs[2] = {}, LeavesOnLogs = 0, Wrong = 0, Refused = 0, LogsOnLeaves = 0;
    for (int32 Y = -Radius; Y <= Radius; ++Y)
        for (int32 X = -Radius - 1; X <= Radius; ++X)
        {
            const TArray<FTreeTestWrite> WestWrites = GetTreeWrites(Logs, Radius, FIntPoint(X, Y), WestKey);
            const TArray<FTreeTestWrite> EastWrites = GetTreeWrites(Logs, Radius, FIntPoint(X + 1, Y), EastKey);
            if (WestWrites.IsEmpty() || EastWrites.IsEmpty()) continue;
            ++Pairs;
            // 0 the earlier tree in merge order, 1 the later: their logs' and leaves' cells and states.
            TMap<FIntVector, uint16> Cells[2];
            TSet<FIntVector> TreeLogs[2], All;
            for (int32 Tree = 0; Tree < 2; ++Tree)
                for (const FTreeTestWrite& Write : (Tree == 0) == bWestFirst ? WestWrites : EastWrites)
                    if (Write.Mode == EVoxelWriteMode::IfReplaceable || Table.Get(Write.State).bLeaves)
                    {
                        Cells[Tree].Add(Write.Cell, Write.State);
                        All.Add(Write.Cell);
                        if (!Table.Get(Write.State).bLeaves) TreeLogs[Tree].Add(Write.Cell);
                    }
            for (const FIntVector& Cell : All)
            {
                const uint16* Earlier = Cells[0].Find(Cell);
                const uint16* Later = Cells[1].Find(Cell);
                const bool bEarlierLog = TreeLogs[0].Contains(Cell), bLaterLog = TreeLogs[1].Contains(Cell);
                const uint16 Got = GetFinalState(Final, Radius, Cell);
                // The replay rule: the later tree's write lands over air and leaves, never over the earlier's log.
                Wrong += Got != (Later && !bEarlierLog ? *Later : *Earlier);
                LostLogs[0] += bEarlierLog && Got != *Earlier;
                LostLogs[1] += bLaterLog && !bEarlierLog && Got != *Later;
                LeavesOnLogs += (bEarlierLog || bLaterLog) && Table.Get(Got).bLeaves;
                Refused += Earlier && Later && bEarlierLog && !bLaterLog;
                LogsOnLeaves += Earlier && Later && !bEarlierLog && bLaterLog;
            }
        }
    TestEqual(TEXT("touching pairs wholly inside the square"), Pairs, 20);
    TestEqual(TEXT("the earlier tree's logs lost"), LostLogs[0], 0);
    TestEqual(TEXT("the later tree's logs lost"), LostLogs[1], 0);
    TestEqual(TEXT("leaves where a log was written"), LeavesOnLogs, 0);
    TestEqual(TEXT("pair cells that are not the replay rule's"), Wrong, 0);
    TestTrue(TEXT("some later leaves met the earlier tree's logs"), Refused > 0);
    TestTrue(TEXT("some later logs met the earlier tree's leaves"), LogsOnLeaves > 0);
    AddInfo(FString::Printf(TEXT("%d corner trees (%d leaves), %d pairs (%s first): %d later leaves refused over logs, "
                                 "%d later logs over leaves"),
        Corners, CornerLeaves, Pairs, bWestFirst ? TEXT("west") : TEXT("east"), Refused, LogsOnLeaves));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTreeWaterDepthTest, "Unnamed.Voxel.Trees.WaterDepth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTreeWaterDepthTest::RunTest(const FString& Parameters)
{
    // surface_water_depth_filter through Decorate on flat columns: a position passes when the water over the floor
    // (WORLD_SURFACE minus OCEAN_FLOOR) is at most max_water_depth (the decorator's documented reading; the wiki's
    // strict "less than" would refuse every tree on land, whose data says 0). Plains' 0, swamp's 2, mangrove swamp's 5,
    // and 1.
    const TCHAR* const Names[] = {
        TEXT("trees_plains"), TEXT("flower_plains"), TEXT("patch_grass_plain"), TEXT("patch_pumpkin")};
    const int32 MaxDepths[] = {0, 1, 2, 5};
    TMap<FString, FString> Replaced;
    for (int32 Index = 0; Index < int32(UE_ARRAY_COUNT(Names)); ++Index)
        Replaced.Add(Names[Index],
            MakeOakPlacement({FString::Printf(TEXT("{\"type\": \"minecraft:surface_water_depth_filter\", "
                                                   "\"max_water_depth\": %d}"),
                                  MaxDepths[Index]),
                TreeTestSurface}));
    FTreeTestWorld World;
    if (!World.Initialize(*this, Replaced)) return true;
    // Land, then water 1 to 6 deep over the floor (the water's top at 63).
    const int32 Depths[] = {0, 1, 2, 3, 5, 6};
    TArray<TSharedRef<FVoxelColumn>> Columns;
    for (const int32 Depth : Depths)
        Columns.Add(World.MakeColumn(FIntPoint::ZeroValue, 63 - Depth, 63));
    for (int32 Index = 0; Index < int32(UE_ARRAY_COUNT(Names)); ++Index)
    {
        FVoxelDecorator Decorator;
        if (!World.MakeDecorator(*this, {Names[Index]}, Decorator)) return false;
        TArray<FString> Row;
        for (int32 Column = 0; Column < int32(UE_ARRAY_COUNT(Depths)); ++Column)
        {
            const int32 Tries = DecorateTries(Decorator, *Columns[Column]);
            TestEqual(FString::Printf(TEXT("max_water_depth %d over water %d deep: tries reaching the tree"),
                          MaxDepths[Index], Depths[Column]),
                Tries, Depths[Column] <= MaxDepths[Index] ? 1 : 0);
            Row.Add(FString::Printf(TEXT("%d deep %d"), Depths[Column], Tries));
        }
        AddInfo(FString::Printf(TEXT("max_water_depth %d: %s"), MaxDepths[Index], *FString::Join(Row, TEXT(", "))));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTreeEnvironmentScanTest, "Unnamed.Voxel.Trees.EnvironmentScan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTreeEnvironmentScanTest::RunTest(const FString& Parameters)
{
    // environment_scan through Decorate on flat columns (grass at 63) with marker blocks above the start: the start
    // cell is tested first, then up to max_steps cells one step each; the target is found even where it fails the
    // allowed condition (the wiki: a cell failing it ends the scan only when it is not the target), and a cell walked
    // through that is neither ends it.
    const auto Scan = [](int32 StartZ, const TCHAR* Direction, int32 Steps, const TCHAR* Target, const TCHAR* Allowed)
    {
        FString Modifier = FString::Printf(TEXT("{\"type\": \"minecraft:environment_scan\", \"direction_of_search\": "
                                                "\"%s\", \"max_steps\": %d, \"target_condition\": %s"),
            Direction, Steps, Target);
        if (Allowed) Modifier += FString::Printf(TEXT(", \"allowed_search_condition\": %s"), Allowed);
        return MakeOakPlacement({MakeOffsetModifier(8, 8, StartZ - VoxelConstants::MinZ), Modifier + TEXT("}")});
    };
    const TCHAR* const Gold = TEXT("{\"type\": \"minecraft:matching_blocks\", \"blocks\": \"minecraft:gold_block\"}");
    const TCHAR* const Solid = TEXT("{\"type\": \"minecraft:solid\"}");
    const TCHAR* const Air = TEXT("{\"type\": \"minecraft:matching_block_tag\", \"tag\": \"minecraft:air\"}");
    TMap<FString, FString> Replaced;
    Replaced.Add(TEXT("trees_plains"), Scan(64, TEXT("up"), 4, Gold, nullptr));
    Replaced.Add(TEXT("flower_plains"), Scan(64, TEXT("up"), 1, Gold, nullptr));
    Replaced.Add(TEXT("patch_grass_plain"), Scan(70, TEXT("down"), 6, Solid, nullptr));
    Replaced.Add(TEXT("patch_pumpkin"), Scan(64, TEXT("up"), 8, Gold, Air));
    FTreeTestWorld World;
    if (!World.Initialize(*this, Replaced)) return true;

    struct FCase
    {
        const TCHAR* Name;
        const TCHAR* Feature;
        int32 FloorZ;
        // Gold and glass above the start at (8, 8, 64): their Z - 64, or -1 for none.
        int32 GoldAt, GlassAt;
        int32 Want;
    };
    const FCase Cases[] = {
        {TEXT("up 4, no gold"), TEXT("trees_plains"), 63, -1, -1, 0},
        {TEXT("up 4, gold at the start"), TEXT("trees_plains"), 63, 0, -1, 1},
        {TEXT("up 4, gold 4 up"), TEXT("trees_plains"), 63, 4, -1, 1},
        {TEXT("up 4, gold 5 up"), TEXT("trees_plains"), 63, 5, -1, 0},
        {TEXT("up 1, gold 1 up"), TEXT("flower_plains"), 63, 1, -1, 1},
        {TEXT("up 1, gold 2 up"), TEXT("flower_plains"), 63, 2, -1, 0},
        {TEXT("down 6 from 70, solid at 64"), TEXT("patch_grass_plain"), 64, -1, -1, 1},
        {TEXT("down 6 from 70, solid at 63"), TEXT("patch_grass_plain"), 63, -1, -1, 0},
        {TEXT("up 8 through air, gold 3 up"), TEXT("patch_pumpkin"), 63, 3, -1, 1},
        {TEXT("up 8 through air, gold 8 up"), TEXT("patch_pumpkin"), 63, 8, -1, 1},
        {TEXT("up 8 through air, glass 1 up, gold 3 up"), TEXT("patch_pumpkin"), 63, 3, 1, 0},
    };
    const uint16 GoldState = World.Table->FindState(TEXT("gold_block"));
    const uint16 GlassState = World.Table->FindState(TEXT("glass"));
    TMap<FString, TUniquePtr<FVoxelDecorator>> Decorators;
    for (const FCase& Case : Cases)
    {
        TUniquePtr<FVoxelDecorator>& Decorator = Decorators.FindOrAdd(Case.Feature);
        if (!Decorator)
        {
            Decorator = MakeUnique<FVoxelDecorator>();
            if (!World.MakeDecorator(*this, {Case.Feature}, *Decorator)) return false;
        }
        const TSharedRef<FVoxelColumn> Column = World.MakeColumn(FIntPoint::ZeroValue, Case.FloorZ);
        if (Case.GoldAt >= 0) Column->SetState(8, 8, 64 + Case.GoldAt, GoldState, *World.Table);
        if (Case.GlassAt >= 0) Column->SetState(8, 8, 64 + Case.GlassAt, GlassState, *World.Table);
        TestEqual(FString::Printf(TEXT("%s: tries reaching the tree"), Case.Name), DecorateTries(*Decorator, *Column),
            Case.Want);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTreeSaplingTest, "Unnamed.Voxel.Trees.Sapling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTreeSaplingTest::RunTest(const FString& Parameters)
{
    // Saplings are not in the block table (the block reference has none), so a tree placement's
    // would_survive(<sapling>) tests the block below against supports_vegetation (minecraft.wiki, "Sapling"):
    // oak_checked's filter passes over grass and dirt and refuses sand and stone. Mangrove's propagule is in the table
    // (S7), so its filter compiles with the propagule's own rule (mangroves skip for their placers, not this filter).
    FTreeTestKit Kit;
    if (!Kit.Initialize(*this)) return true;
    const FVoxelFeatureGraph& Graph = Kit.Graph;
    const auto FilterOf = [&Graph](const TCHAR* Placed) -> int32
    {
        int32 Entry = Graph.FindEntry(EVoxelFeatureRegistry::PlacedFeature, Placed);
        if (Entry == INDEX_NONE)
            Entry = Graph.FindEntry(EVoxelFeatureRegistry::PlacedFeature, FString(TEXT("minecraft:")) + Placed);
        const int32 Node =
            Entry == INDEX_NONE ? INDEX_NONE : Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)][Entry].Node;
        const FVoxelFeatureValue* Placement = Node == INDEX_NONE ? nullptr : Graph.FindField(Node, TEXT("placement"));
        for (int32 Item = 0; Placement && Placement->Type == EVoxelFeatureValueType::List && Item < Placement->Count;
            ++Item)
        {
            const int32 Modifier = Graph.GetNode(Graph.Items[Placement->Index + Item]);
            if (Graph.GetType(Modifier) != TEXT("minecraft:block_predicate_filter")) continue;
            const FVoxelFeatureValue* Predicate = Graph.FindField(Modifier, TEXT("predicate"));
            return Predicate ? Graph.GetNode(*Predicate) : INDEX_NONE;
        }
        return INDEX_NONE;
    };
    const int32 OakFilter = FilterOf(TEXT("oak_checked")), MangroveFilter = FilterOf(TEXT("mangrove_checked"));
    if (!TestTrue(TEXT("oak_checked and mangrove_checked have their would_survive filters"),
            OakFilter != INDEX_NONE && MangroveFilter != INDEX_NONE))
        return false;
    int32 OakRoot = INDEX_NONE, MangroveRoot = INDEX_NONE;
    FString OakSkip, MangroveSkip, Error;
    TestTrue(FString::Printf(TEXT("oak_sapling's would_survive compiles (%s)"), *Error),
        Kit.Predicates.Compile(Graph, OakFilter, *Kit.Tags, OakRoot, OakSkip, &Error));
    TestEqual(TEXT("oak_sapling's would_survive is built"), OakSkip, FString());
    TestTrue(FString::Printf(TEXT("mangrove_propagule's would_survive compiles (%s)"), *Error),
        Kit.Predicates.Compile(Graph, MangroveFilter, *Kit.Tags, MangroveRoot, MangroveSkip, &Error));
    TestEqual(
        TEXT("mangrove_propagule's would_survive is built (the propagule is in the table)"), MangroveSkip, FString());
    if (!OakSkip.IsEmpty() || OakRoot == INDEX_NONE) return false;
    const TPair<const TCHAR*, bool> Grounds[] = {{TEXT("grass_block"), true}, {TEXT("dirt"), true},
        {TEXT("coarse_dirt"), true}, {TEXT("sand"), false}, {TEXT("stone"), false}, {TEXT("water"), false}};
    for (const TPair<const TCHAR*, bool>& Ground : Grounds)
    {
        FTreeProbe Probe;
        Probe.Layers = {{TEXT("stone"), -63, 55}, {TEXT("dirt"), 56, 58}, {Ground.Key, 59, 59}};
        const TSharedRef<FVoxelColumn> Column = MakeProbeColumn(*Kit.Table, Probe);
        const FVoxelColumn* Around[9];
        for (const FVoxelColumn*& Each : Around)
            Each = &*Column;
        FVoxelDecorationContext Context(FIntPoint(0, 0), Around, &*Kit.Table);
        TestEqual(FString::Printf(TEXT("an oak sapling survives over %s"), Ground.Key),
            Kit.Predicates.Test(OakRoot, Context, FIntVector(8, 8, 60)), Ground.Value);
    }
    return true;
}

#endif
