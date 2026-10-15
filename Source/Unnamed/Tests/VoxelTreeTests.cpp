#include "Algo/IndexOf.h"
#include "Algo/Reverse.h"
#include "Async/ParallelFor.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Decoration/Features/VoxelBigVegetation.h"
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
    // RunTreeProbes compiles; Trees.Shapes must fail), 3 to 7 as ApplyTreeTestMutation says, 8 the fallen log one
    // longer (scout J's: fallen super birch must then fail its reach of 15 with 16; RunTreeProbes), 9 every fallen log
    // pointing the same way (the direction test must fail; SimulateTrees), 10 and 11 as RunTreeProbes says (S9e: the
    // azalea's leaves and the rooted dirt must fail), 12 and 13 as RunTreeProbes says and 14 as the vines test (S9e
    // part 2: bamboo's height, the forest rock's moves, the vines' face order), 15 as RunTreeProbes says (S9e part 3:
    // a re-probe pool of its first run alone), 16 ChiSquareHomogeneity unmerged (S9e part 4: the before table; the
    // verdicts of S9e part 3) and 17 as ApplyTreeTestMutation says (S9e part 4: the mangroves' branches). With 0,
    // every test must pass.
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
        // The reach of every cell, the decorators' included (max_reach_h_any_cell).
        int32 AllReach = 0;
        TSet<FString> Blocks;
        // Each block's mean count per tree (the decorators' oracle, scout J's distribution 4).
        TMap<FString, double> BlockMeans;
        // The leaf layers by dy from the highest log: (dx, dy) to the share of trees with a leaf there.
        int32 ProfileCount = 0;
        TMap<int32, TMap<FIntPoint, double>> Occupancy;
        // A ceiling probe's dy (variant "_ceil<H>"), or 0.
        int32 Ceiling = 0;
        // A fallen tree probe's trees per log direction (S9d part 2; 0 west, 1 east, 2 north, 3 south: the side its
        // per-placement box reaches farthest), else empty.
        TMap<int32, int64> SideHist;
        // A huge mushroom probe's (S9d part 2): per placed mushroom its cells, the count of mushrooms per cap top dy
        // (its box's highest), and its examples' cells (relative to the origin) by state name.
        TArray<double> Cells;
        TMap<int32, int64> CapTopHist;
        TArray<TMap<FIntVector, FString>> Examples;
        // A bamboo probe's (S9e part 2): per placed stalk its box's highest dy (its top; CapTopHist counts them too,
        // and Examples holds its examples). The forest rock probe's: its boxes' extremes (bbox_extremes) in our axes.
        TArray<double> Tops;
        FIntVector BoxMin = FIntVector::ZeroValue, BoxMax = FIntVector::ZeroValue;
        // Compared by its placed rate alone (S9e part 3: the accepted tall_mangrove@mudwater, whose survivors' trunk
        // heights did not reproduce in 300 new trees, C:\dev\sandbox\phase6s9\s9e2\reprobe\reprobe-report.md).
        bool bPlacedRateOnly = false;
    };

    // A probe of under 50 tries (a ceiling's 16): too few trees for the distribution rules (the means, decorations,
    // soil, occupancy, reach and parts). CompareTrees and the calibration (RunTreeProbes) both skip them through this.
    bool IsSmallProbe(const FTreeProbe& Probe)
    {
        return Probe.Tries < 50;
    }

    // Our trees on a probe's column, measured as the probe measures.
    struct FTreeRun
    {
        int32 Tries = 0, Placed = 0;
        // Failed tries that still emitted a write.
        int32 FailedWrites = 0;
        TArray<double> Logs, Leaves, Cells, Ground;
        TMap<int32, int64> TopHist, Comp6Hist, Comp26Hist;
        int32 Reach = 0, TopMax = MIN_int32, OriginLogsMin = MAX_int32, OriginLogsMax = MIN_int32;
        // Logs and leaves (Reach), and every write (AllReach); per tree, its every-write reach (the count of trees
        // per reach).
        int32 AllReach = 0;
        TMap<int32, int64> AllReachHist;
        TSet<int32> Blocks;
        // The decorators' blocks (every write but logs, leaves and the dirt under the trunk) per block: the sum over
        // trees of each tree's count, and of its square.
        TMap<int32, double> DecorationSums, DecorationSquares;
        TMap<int32, TMap<FIntPoint, int32>> Occupancy;
        // Leaves whose distance is not the BFS from the tree's own logs through its own leaves (1 to 6), or that are
        // persistent.
        int32 BadDistances = 0, Persistent = 0;
        // A fallen tree's trees per log direction (FTreeProbe::SideHist).
        TMap<int32, int64> SideHist;
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
            double Cells = 0;
            Placement->TryGetNumberField(TEXT("n"), Cells);
            Out.Cells.Add(Cells);
            // A fallen tree's box [x0, x1, y0, y1, z0, z1]: its log lies toward the farthest side (the stump's vines
            // reach 1, the log at least 3).
            const TArray<TSharedPtr<FJsonValue>>* Box;
            if (Out.Name.StartsWith(TEXT("fallen_")) && Placement->TryGetArrayField(TEXT("bbox"), Box) &&
                Box->Num() == 6)
            {
                const double Reach[4] = {
                    -(*Box)[0]->AsNumber(), (*Box)[1]->AsNumber(), -(*Box)[4]->AsNumber(), (*Box)[5]->AsNumber()};
                int32 Side = 0;
                for (int32 Candidate = 1; Candidate < 4; ++Candidate)
                    if (Reach[Candidate] > Reach[Side]) Side = Candidate;
                // A stump alone (its vines reach 1) has no direction, as ours (SimulateTrees) counts none.
                if (Reach[Side] >= 2) ++Out.SideHist.FindOrAdd(Side);
            }
            // A huge mushroom's cap top, or a bamboo stalk's top (S9e part 2): its box's highest dy.
            const bool bBamboo = Out.Name.StartsWith(TEXT("bamboo_"));
            if ((Out.Name.StartsWith(TEXT("huge_")) || bBamboo) && Placement->TryGetArrayField(TEXT("bbox"), Box) &&
                Box->Num() == 6)
            {
                ++Out.CapTopHist.FindOrAdd(int32((*Box)[3]->AsNumber()));
                if (bBamboo) Out.Tops.Add((*Box)[3]->AsNumber());
            }
        }
        // A huge mushroom's or a bamboo's examples: [dx, dy, dz, state] per cell, in the reference's axes.
        const TArray<TSharedPtr<FJsonValue>>* Examples;
        if ((Out.Name.StartsWith(TEXT("huge_")) || Out.Name.StartsWith(TEXT("bamboo_"))) &&
            Json->TryGetArrayField(TEXT("examples"), Examples))
            for (const TSharedPtr<FJsonValue>& Example : *Examples)
            {
                TMap<FIntVector, FString>& Cells = Out.Examples.AddDefaulted_GetRef();
                for (const TSharedPtr<FJsonValue>& Cell : Example->AsObject()->GetArrayField(TEXT("cells")))
                {
                    const TArray<TSharedPtr<FJsonValue>>& Items = Cell->AsArray();
                    if (Items.Num() == 4)
                        Cells.Add(FIntVector(int32(Items[0]->AsNumber()), int32(Items[2]->AsNumber()),
                                      int32(Items[1]->AsNumber())),
                            StripNamespace(Items[3]->AsString()));
                }
            }
        Out.Bfs7 = ReadTreeStat(*Json, TEXT("leaf_bfs7"), TEXT("mean"));
        const TSharedPtr<FJsonObject>* Object;
        // The boxes' extremes (S9e part 2's forest rock): the reference's x, y (up) and z as ours X, Z and Y.
        if (Json->TryGetObjectField(TEXT("bbox_extremes"), Object))
        {
            const auto Extreme = [&Object](const TCHAR* Key) { return int32((*Object)->GetNumberField(Key)); };
            Out.BoxMin = FIntVector(Extreme(TEXT("min_dx")), Extreme(TEXT("min_dz")), Extreme(TEXT("min_dy")));
            Out.BoxMax = FIntVector(Extreme(TEXT("max_dx")), Extreme(TEXT("max_dz")), Extreme(TEXT("max_dy")));
        }
        if (Json->TryGetObjectField(TEXT("log_top_hist"), Object)) Out.TopHist = ReadTreeCounts(Object);
        if (Json->TryGetObjectField(TEXT("log_comp6_hist"), Object)) Out.Comp6Hist = ReadTreeCounts(Object);
        if (Json->TryGetObjectField(TEXT("log_comp26_hist"), Object)) Out.Comp26Hist = ReadTreeCounts(Object);
        Out.Reach = int32(FMath::Max(ReadTreeStat(*Json, TEXT("leaf_reach_h"), TEXT("max")),
            ReadTreeStat(*Json, TEXT("log_reach_h"), TEXT("max"))));
        Out.Comp26Max = int32(ReadTreeStat(*Json, TEXT("log_comp26"), TEXT("max")));
        double AllReach = 0;
        Json->TryGetNumberField(TEXT("max_reach_h_any_cell"), AllReach);
        Out.AllReach = FMath::Max(int32(AllReach), Out.Reach);
        Out.OriginLogsMin = int32(ReadTreeStat(*Json, TEXT("origin_column_logs"), TEXT("min")));
        Out.OriginLogsMax = int32(ReadTreeStat(*Json, TEXT("origin_column_logs"), TEXT("max")));
        if (Json->TryGetObjectField(TEXT("blocks"), Object))
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Block : (*Object)->Values)
            {
                Out.Blocks.Add(Block.Key);
                double Mean = 0;
                Block.Value->AsObject()->TryGetNumberField(TEXT("mean_per_tree"), Mean);
                Out.BlockMeans.Add(Block.Key, Mean);
                if (Block.Key == TEXT("dirt")) Out.Dirt = Mean;
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

    // A re-probe's run number (S9e part 3: tree_<probe>_r<N>.json from tools/tree_probe.py --run-tag) and the probe
    // it re-runs, or INDEX_NONE.
    int32 GetProbeRun(const FString& Name, FString& OutBase)
    {
        int32 Under;
        if (!Name.FindLastChar(TEXT('_'), Under) || Name.Len() < Under + 3 || Name[Under + 1] != TEXT('r') ||
            Name[Under + 2] == TEXT('0'))
            return INDEX_NONE;
        for (int32 Index = Under + 2; Index < Name.Len(); ++Index)
            if (!FChar::IsDigit(Name[Index])) return INDEX_NONE;
        OutBase = Name.Left(Under);
        return FCString::Atoi(*Name.RightChop(Under + 2));
    }

    // One probe's re-probe runs read as one (the manager's call Q1 (a), S9e part 3): the tries, placed trees, their
    // lists and histograms added; each mean per tree weighted by its run's placed trees and each leaf occupancy share
    // by its run's profiled trees; the extremes over the runs.
    FTreeProbe PoolTreeProbes(const TArray<FTreeProbe>& Runs, const FString& Name)
    {
        FTreeProbe Out = Runs[0];
        Out.Name = Name;
        Out.Bfs7 *= Out.Placed;
        Out.Dirt *= Out.Placed;
        for (TPair<FString, double>& Mean : Out.BlockMeans)
            Mean.Value *= Out.Placed;
        for (TPair<int32, TMap<FIntPoint, double>>& Layer : Out.Occupancy)
            for (TPair<FIntPoint, double>& Cell : Layer.Value)
                Cell.Value *= Out.ProfileCount;
        const auto AddCounts = [](TMap<int32, int64>& To, const TMap<int32, int64>& From)
        {
            for (const TPair<int32, int64>& Bin : From)
                To.FindOrAdd(Bin.Key) += Bin.Value;
        };
        for (int32 Index = 1; Index < Runs.Num(); ++Index)
        {
            const FTreeProbe& Run = Runs[Index];
            Out.Tries += Run.Tries;
            Out.Placed += Run.Placed;
            Out.Logs.Append(Run.Logs);
            Out.Leaves.Append(Run.Leaves);
            Out.Cells.Append(Run.Cells);
            Out.Tops.Append(Run.Tops);
            Out.Examples.Append(Run.Examples);
            Out.Bfs7 += Run.Bfs7 * Run.Placed;
            Out.Dirt += Run.Dirt * Run.Placed;
            AddCounts(Out.TopHist, Run.TopHist);
            AddCounts(Out.Comp6Hist, Run.Comp6Hist);
            AddCounts(Out.Comp26Hist, Run.Comp26Hist);
            AddCounts(Out.SideHist, Run.SideHist);
            AddCounts(Out.CapTopHist, Run.CapTopHist);
            Out.Reach = FMath::Max(Out.Reach, Run.Reach);
            Out.AllReach = FMath::Max(Out.AllReach, Run.AllReach);
            Out.Comp26Max = FMath::Max(Out.Comp26Max, Run.Comp26Max);
            Out.OriginLogsMin = FMath::Min(Out.OriginLogsMin, Run.OriginLogsMin);
            Out.OriginLogsMax = FMath::Max(Out.OriginLogsMax, Run.OriginLogsMax);
            Out.BoxMin = FIntVector(FMath::Min(Out.BoxMin.X, Run.BoxMin.X), FMath::Min(Out.BoxMin.Y, Run.BoxMin.Y),
                FMath::Min(Out.BoxMin.Z, Run.BoxMin.Z));
            Out.BoxMax = FIntVector(FMath::Max(Out.BoxMax.X, Run.BoxMax.X), FMath::Max(Out.BoxMax.Y, Run.BoxMax.Y),
                FMath::Max(Out.BoxMax.Z, Run.BoxMax.Z));
            Out.Blocks.Append(Run.Blocks);
            for (const TPair<FString, double>& Mean : Run.BlockMeans)
                Out.BlockMeans.FindOrAdd(Mean.Key) += Mean.Value * Run.Placed;
            for (const TPair<int32, TMap<FIntPoint, double>>& Layer : Run.Occupancy)
                for (const TPair<FIntPoint, double>& Cell : Layer.Value)
                    Out.Occupancy.FindOrAdd(Layer.Key).FindOrAdd(Cell.Key) += Cell.Value * Run.ProfileCount;
            Out.ProfileCount += Run.ProfileCount;
        }
        const double Placed = FMath::Max(Out.Placed, 1), Profiled = FMath::Max(Out.ProfileCount, 1);
        Out.Bfs7 /= Placed;
        Out.Dirt /= Placed;
        for (TPair<FString, double>& Mean : Out.BlockMeans)
            Mean.Value /= Placed;
        for (TPair<int32, TMap<FIntPoint, double>>& Layer : Out.Occupancy)
            for (TPair<FIntPoint, double>& Cell : Layer.Value)
                Cell.Value /= Profiled;
        return Out;
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

    // The p value that two histograms come from one distribution (chi-square homogeneity), as the fits' harness; with
    // OutChi2, the statistic and its degrees of freedom (0 and 0 where there is no test). Sparse bins merge (S9e part
    // 4, the manager's call (a), Cochran's rule): taken in key order, bins join until both histograms' expected counts
    // in the group reach 5, and a short last group joins the one before (the unpooled test failed the accepted
    // mangrove@mudwater's 6-connected parts on 4 probe trees in one bin where 2 of 269 pooled ones sit).
    double ChiSquareHomogeneity(
        const TMap<int32, int64>& A, const TMap<int32, int64>& B, TPair<double, int32>* OutChi2 = nullptr)
    {
        TMap<int32, TPair<double, double>> Bins;
        double CountA = 0, CountB = 0;
        for (const TPair<int32, int64>& Bin : A)
            Bins.FindOrAdd(Bin.Key).Key += Bin.Value, CountA += Bin.Value;
        for (const TPair<int32, int64>& Bin : B)
            Bins.FindOrAdd(Bin.Key).Value += Bin.Value, CountB += Bin.Value;
        if (OutChi2) *OutChi2 = TPair<double, int32>(0., 0);
        if (CountA == 0 || CountB == 0 || Bins.Num() < 2) return 1;
        Bins.KeySort(TLess<int32>());
        const double ShareA = CountA / (CountA + CountB), ShareB = CountB / (CountA + CountB);
        // The trees a group needs for both expected counts to reach 5 (mutation 16: none, the unmerged test).
        const double Least = TreeTestMutation == 16 ? 0 : 5 / FMath::Min(ShareA, ShareB);
        TArray<TPair<double, double>> Groups;
        TPair<double, double> Open(0., 0.);
        for (const TPair<int32, TPair<double, double>>& Bin : Bins)
        {
            Open.Key += Bin.Value.Key;
            Open.Value += Bin.Value.Value;
            if (Open.Key + Open.Value < Least) continue;
            Groups.Add(Open);
            Open = TPair<double, double>(0., 0.);
        }
        if (Open.Key + Open.Value > 0)
        {
            if (Groups.IsEmpty()) return 1;
            Groups.Last().Key += Open.Key;
            Groups.Last().Value += Open.Value;
        }
        if (Groups.Num() < 2) return 1;
        double Chi2 = 0;
        for (const TPair<double, double>& Group : Groups)
        {
            const double Total = Group.Key + Group.Value, ExpectA = Total * ShareA, ExpectB = Total * ShareB;
            Chi2 += FMath::Square(Group.Key - ExpectA) / ExpectA + FMath::Square(Group.Value - ExpectB) / ExpectB;
        }
        if (OutChi2) *OutChi2 = TPair<double, int32>(Chi2, Groups.Num() - 1);
        return UpperGammaQ((Groups.Num() - 1) / 2., Chi2 / 2);
    }

    // A histogram's mean key.
    double GetHistogramMean(const TMap<int32, int64>& Histogram)
    {
        double Sum = 0, Count = 0;
        for (const TPair<int32, int64>& Bin : Histogram)
            Sum += double(Bin.Key) * Bin.Value, Count += Bin.Value;
        return Count ? Sum / Count : 0;
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

        // False (with an error) when DA_Blocks is missing, or on a read error.
        bool Initialize(FAutomationTestBase& Test)
        {
            if (!Table->FindState(TEXT("stone")))
            {
                Test.AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
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

        // A tree feature (a configured feature id) compiled; false with OutSkip saying why it is not run here. With
        // OutFallen, a fallen tree (S9d part 2) compiles into it, and Out is its builder's tree with the stump's and
        // the log's decorators together (what the block set and the decoration rules read).
        // With OutMushroom, a huge mushroom compiles into it (Out stays empty). With OutRootSystem, a root system (S9e)
        // compiles into it, and Out is its tree. With OutBamboo or OutBlob, a bamboo or a block_blob (S9e part 2)
        // compiles into it (Out stays empty).
        bool Compile(FAutomationTestBase& Test, const FString& Feature, FVoxelTreeConfig& Out, FString& OutSkip,
            TSharedPtr<FVoxelFallenTreeConfig>* OutFallen = nullptr,
            TSharedPtr<FVoxelHugeMushroomConfig>* OutMushroom = nullptr,
            TSharedPtr<FVoxelRootSystemConfig>* OutRootSystem = nullptr,
            TSharedPtr<FVoxelBambooConfig>* OutBamboo = nullptr, TSharedPtr<FVoxelBlockBlobConfig>* OutBlob = nullptr)
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
            if (OutFallen && Graph.GetType(Node) == TEXT("minecraft:fallen_tree"))
            {
                const TSharedRef<FVoxelFallenTreeConfig> Fallen = MakeShared<FVoxelFallenTreeConfig>();
                FString Error;
                if (!Test.TestTrue(FString::Printf(TEXT("%s compiles"), *Feature),
                        VoxelBigVegetation::CompileFallenTree(
                            Graph, Node, *Tags, *Table, Predicates, Providers, *Fallen, OutSkip, &Error)))
                {
                    Test.AddError(Error);
                    return false;
                }
                if (!OutSkip.IsEmpty()) return false;
                Out = Fallen->Tree;
                Out.Decorators.Append(Fallen->LogDecorators);
                *OutFallen = Fallen;
                return true;
            }
            const FStringView Type = Graph.GetType(Node);
            if (OutMushroom &&
                (Type == TEXT("minecraft:huge_red_mushroom") || Type == TEXT("minecraft:huge_brown_mushroom")))
            {
                const TSharedRef<FVoxelHugeMushroomConfig> Mushroom = MakeShared<FVoxelHugeMushroomConfig>();
                FString Error;
                if (!Test.TestTrue(FString::Printf(TEXT("%s compiles"), *Feature),
                        VoxelBigVegetation::CompileHugeMushroom(Graph, Node,
                            Type == TEXT("minecraft:huge_red_mushroom"), *Tags, *Table, Predicates, Providers,
                            *Mushroom, OutSkip, &Error)))
                {
                    Test.AddError(Error);
                    return false;
                }
                *OutMushroom = Mushroom;
                return OutSkip.IsEmpty();
            }
            if (OutRootSystem && Type == TEXT("minecraft:root_system"))
            {
                const TSharedRef<FVoxelRootSystemConfig> RootSystem = MakeShared<FVoxelRootSystemConfig>();
                FString Error;
                if (!Test.TestTrue(FString::Printf(TEXT("%s compiles"), *Feature),
                        VoxelBigVegetation::CompileRootSystem(
                            Graph, Node, *Tags, *Table, Predicates, Providers, *RootSystem, OutSkip, &Error)))
                {
                    Test.AddError(Error);
                    return false;
                }
                if (!OutSkip.IsEmpty()) return false;
                Out = RootSystem->Tree;
                *OutRootSystem = RootSystem;
                return true;
            }
            if (OutBamboo && Type == TEXT("minecraft:bamboo"))
            {
                const TSharedRef<FVoxelBambooConfig> Bamboo = MakeShared<FVoxelBambooConfig>();
                FString Error;
                if (!Test.TestTrue(FString::Printf(TEXT("%s compiles"), *Feature),
                        VoxelBigVegetation::CompileBamboo(Graph, Node, *Tags, *Table, Predicates, *Bamboo, &Error)))
                {
                    Test.AddError(Error);
                    return false;
                }
                *OutBamboo = Bamboo;
                return true;
            }
            if (OutBlob && Type == TEXT("minecraft:block_blob"))
            {
                const TSharedRef<FVoxelBlockBlobConfig> Blob = MakeShared<FVoxelBlockBlobConfig>();
                FString Error;
                if (!Test.TestTrue(FString::Printf(TEXT("%s compiles"), *Feature),
                        VoxelBigVegetation::CompileBlockBlob(
                            Graph, Node, *Tags, *Table, Predicates, *Blob, OutSkip, &Error)))
                {
                    Test.AddError(Error);
                    return false;
                }
                *OutBlob = Blob;
                return OutSkip.IsEmpty();
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
        // The soil under the trunk is the dirt below a log; every other write but logs and leaves is a decoration.
        const int32 DirtBlock = Table.FindBlock(TEXT("dirt"));
        int32 Ground = 0;
        TMap<int32, int32> Decorations;
        for (const FVoxelPlantWrite& Write : Writes)
        {
            if (Write.Mode == EVoxelWriteMode::IfReplaceable) continue;
            const int32 Block = Table.Get(Write.State).Block;
            if (Block == DirtBlock && Logs.Contains(Write.Cell + FIntVector(0, 0, 1)))
                ++Ground;
            else
                ++Decorations.FindOrAdd(Block);
        }
        for (const TPair<int32, int32>& Decoration : Decorations)
        {
            Run.DecorationSums.FindOrAdd(Decoration.Key) += Decoration.Value;
            Run.DecorationSquares.FindOrAdd(Decoration.Key) += FMath::Square(double(Decoration.Value));
        }
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
        const auto GetReach = [&Origin](const TSet<FIntVector>& Cells, int32 Reach)
        {
            for (const FIntVector& Cell : Cells)
                Reach = FMath::Max(Reach, FMath::Max(FMath::Abs(Cell.X - Origin.X), FMath::Abs(Cell.Y - Origin.Y)));
            return Reach;
        };
        const int32 Reach = GetReach(Leaves, GetReach(Logs, 0));
        const int32 AllReach = GetReach(All, 0);
        Run.AllReach = FMath::Max(Run.AllReach, AllReach);
        ++Run.AllReachHist.FindOrAdd(AllReach);
        ++Run.Placed;
        Run.Logs.Add(Logs.Num());
        Run.Leaves.Add(Leaves.Num());
        Run.Ground.Add(Ground);
        Run.Cells.Add(Logs.Num() + Leaves.Num() + Ground);
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

    // Count trees of Config (or of Fallen, a fallen tree, or RootSystem, a root system and its tree) on the probe's
    // column at (8, 8, the probe's y), each from its own stream. The trees only read the working copy (Emit keeps their
    // writes), so they all see the probe's untouched column.
    void SimulateTrees(const FTreeTestKit& Kit, const FVoxelTreeConfig& Config, const FTreeProbe& Probe, int32 Count,
        uint64 Seed, FTreeRun& Out, const FVoxelFallenTreeConfig* Fallen = nullptr,
        const FVoxelRootSystemConfig* RootSystem = nullptr)
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
            if (Fallen           ? VoxelBigVegetation::PlaceFallenTree(*Fallen, Origin, Random, Env)
                    : RootSystem ? VoxelBigVegetation::PlaceRootSystem(*RootSystem, Origin, Random, Env)
                                 : VoxelTreeFeature::Place(Config, Origin, Random, Env))
                AddTree(Table, Origin, Writes, Out);
            else
                Out.FailedWrites += Writes.Num() > 0;
            if (!Fallen || Writes.IsEmpty()) continue;
            // The log's direction: the side its farthest log cell lies on.
            int32 Side = INDEX_NONE, Farthest = 0;
            for (const FVoxelPlantWrite& Write : Writes)
            {
                const FIntVector Offset = Write.Cell - Origin;
                const int32 Reach = FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y));
                if (Write.Mode != EVoxelWriteMode::IfReplaceable || Reach <= Farthest) continue;
                Farthest = Reach;
                Side = Offset.X ? (Offset.X < 0 ? 0 : 1) : (Offset.Y < 0 ? 2 : 3);
            }
            // Mutation 9 (the manager's): every log measured as pointing east, as though the direction were fixed.
            if (Side != INDEX_NONE) ++Out.SideHist.FindOrAdd(TreeTestMutation == 9 ? 1 : Side);
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
    // The signed normal z of a two-sided p value (the sign of Sign, 0 for 0): erfc(|z| / sqrt 2) = P, by bisection.
    double GetDecorationPZ(double P, double Sign)
    {
        double Low = 0, High = 40;
        for (int32 Step = 0; Step < 100; ++Step)
        {
            const double Mid = (Low + High) / 2;
            (std::erfc(Mid / std::sqrt(2.)) > P ? Low : High) = Mid;
        }
        return Sign > 0 ? Low : (Sign < 0 ? -Low : 0);
    }

    // The decorators' blocks per tree against the probe's (scout J's distribution 4): every block but the tree's logs,
    // leaves and dirt, ours within 5% of the probe's mean unless they differ: |z| > 3 for a block at 1 or more per
    // tree, an exact p under 0.01 for a rarer one (below). z: both samples take our deviation (the null is one
    // distribution); where ours has none, the larger mean stands in as a Poisson variance. A block the probe saw and we
    // never write is only reported while the tree has decorators left out. OutZ, when given, takes the z of each
    // compared block that ours expects at least once in the probe's trees (the pooled check, PoolDecorationZ).
    // A block under 1 per tree in both (the S9c manager's call B) took the z rule until S9c, which failed some probe in
    // 587 of 1000 calibration runs (TreeTestCalibration), all from the four 0.002 bee probes: ours draws about one nest
    // in 2000 trees, so a 100-tree sample that draws one gives z near -4 (a count that small has a tail z misreads).
    // Now its total counts take the exact conditional test, a the probe's (its mean times its placed trees, rounded)
    // and b ours: under the null a ~ Binomial(a + b, n1 / (n1 + n2)), n1 the probe's placed trees and n2 ours,
    // two-sided as BinomialTwoSided (GetOccupancyP's style). Both counts are first divided by our variance over our
    // mean where that is over 1 (a quasi-Poisson scale): cocoa fires on 1 jungle tree in 5 and then sets about 2, shelf
    // mushrooms cluster too, and their raw counts failed jungle_tree's samples 96 times in 1000 and the poplars'
    // about 50. With the scale, some probe fails 138 runs in 1000, each probe at most 21 (birch_bees_0002: where our
    // 2000 trees drew 0 or 1 nest, a sample drawing 2 fails; the test keeps 1% over our draws, not given one), the
    // poplars 7 to 9. The line's z for a rare block is the exact p's signed equivalent (GetDecorationPZ, |z| > 2.58 at
    // p 0.01); OutZ keeps the z rule's z (the pooled check's calibration, unchanged: the p's z shrinks for a nest at
    // probability 1, under 1 per tree and far less variable than Poisson, so it would weaken the bee pool).
    void CheckDecorations(const FTreeTestKit& Kit, const FVoxelTreeConfig& Config, const FTreeProbe& Probe,
        const FTreeRun& Run, FString& OutLine, TArray<FString>& Failures, TMap<int32, double>* OutZ = nullptr)
    {
        const FVoxelBlockTable& Table = *Kit.Table;
        TSet<int32> TreeBlocks;
        TreeBlocks.Add(Table.FindBlock(TEXT("dirt")));
        for (const uint16 State : Config.LogStates)
            TreeBlocks.Add(Table.Get(State).Block);
        for (const TPair<uint16, TArray<uint16>>& Leaf : Config.LeafStates)
            TreeBlocks.Add(Table.Get(Leaf.Key).Block);
        TMap<int32, double> Theirs;
        TMap<int32, FString> Names;
        for (const TPair<FString, double>& Block : Probe.BlockMeans)
            if (const int32 Id = Table.FindBlock(Block.Key); !TreeBlocks.Contains(Id))
            {
                Theirs.Add(Id, Block.Value);
                Names.Add(Id, Block.Key);
            }
        TSet<int32> Blocks;
        for (const TPair<int32, double>& Each : Theirs)
            Blocks.Add(Each.Key);
        // The tree's own blocks are left out on our side too (until the S9c review only the probe's were): a log, leaf
        // or dirt state a decorator writes (dirt beside the trunk rather than under it) is no decoration block here.
        for (const TPair<int32, double>& Each : Run.DecorationSums)
            if (!TreeBlocks.Contains(Each.Key)) Blocks.Add(Each.Key);
        const double OurN = FMath::Max(Run.Placed, 1), ProbeN = FMath::Max(Probe.Placed, 1);
        for (const int32 Block : Blocks)
        {
            const FString Name = Names.Contains(Block) ? Names[Block] : FString::Printf(TEXT("block %d"), Block);
            const double Sum = Run.DecorationSums.FindRef(Block);
            if (Sum == 0 && !Config.SkippedDecorators.IsEmpty())
            {
                OutLine += FString::Printf(TEXT(", %s left out"), *Name);
                continue;
            }
            const double OurMean = Sum / OurN, TheirMean = Theirs.FindRef(Block);
            const double OurVariance = Run.DecorationSquares.FindRef(Block) / OurN - FMath::Square(OurMean);
            const double Variance = OurVariance > 1e-9 ? OurVariance : FMath::Max(OurMean, TheirMean);
            const double Error = FMath::Sqrt(Variance * (1 / ProbeN + 1 / OurN));
            double Z = Error > 0 ? (OurMean - TheirMean) / Error : 0;
            if (OutZ && Error > 0 && OurMean * ProbeN >= 1) OutZ->Add(Block, Z);
            const double Relative = TheirMean ? (OurMean - TheirMean) / TheirMean : (OurMean ? 1 : 0);
            bool bDiffers = FMath::Abs(Z) > 3;
            // The quasi-Poisson scale D a rare block's counts are divided by, logged on the line (the S9c manager's
            // call E: a reader sees how far it widens the test).
            double Dispersion = 0;
            if (OurMean < 1 && TheirMean < 1)
            {
                Dispersion = OurMean > 0 ? FMath::Max(OurVariance / OurMean, 1.) : 1;
                const int32 TheirCount = FMath::RoundToInt32(TheirMean * ProbeN / Dispersion),
                            OurCount = FMath::RoundToInt32(Sum / Dispersion);
                const double P = BinomialTwoSided(TheirCount, TheirCount + OurCount, ProbeN / (ProbeN + OurN));
                bDiffers = P < 0.01;
                Z = GetDecorationPZ(P, OurMean - TheirMean);
            }
            OutLine += Dispersion
                ? FString::Printf(TEXT(", %s %.2f/%.2f (z %+.1f, D %.2f)"), *Name, OurMean, TheirMean, Z, Dispersion)
                : FString::Printf(TEXT(", %s %.2f/%.2f (z %+.1f)"), *Name, OurMean, TheirMean, Z);
            if (FMath::Abs(Relative) > 0.05 && bDiffers)
                Failures.Add(FString::Printf(TEXT("%s per tree %+.1f%%"), *Name, 100 * Relative));
        }
    }

    // The blocks a tree's built decorators can write: their fixed states and their providers' states (the block-set
    // rule's exemption, CompareTrees).
    TSet<int32> GetDecoratorBlocks(const FTreeTestKit& Kit, const FVoxelTreeConfig& Config)
    {
        TArray<uint16> States;
        for (const FVoxelTreeDecoratorConfig& Decorator : Config.Decorators)
        {
            States.Append(Decorator.States);
            if (Decorator.Provider != INDEX_NONE) Kit.Providers.GetStates(Decorator.Provider, States);
        }
        TSet<int32> Blocks;
        for (const uint16 State : States)
            Blocks.Add(Kit.Table->Get(State).Block);
        return Blocks;
    }

    // The pooled decoration check (the S9c manager's call 2a), after every probe is compared: per decoration block,
    // Stouffer's combined z of its per-probe z (CheckDecorations) over the k probes that compared it (not those that
    // left it out), sum / sqrt(k). One probe's 100 trees cannot tell a beehive at 0.05 from one at 0.10 (z about 2,
    // scout J's "beehive probability x2" passed every probe); the bee probes together can. Returns per block k and the
    // combined z; IsPooledFailure decides.
    // Calibrated on 1000 runs of the configurations' own samples (TreeTestCalibration): with every compared probe in
    // the sum, the check false-failed 70 runs in 1000, bee_nest 58 of them (its combined z's deviation 1.55, not 1).
    // A block ours draws about 0.002 per tree is expected 0.2 times in a probe's 100 trees, and one drawn there gives
    // a z near -4 (where our 2000 trees drew one or two, the deviation is small), so z is far from normal. A probe
    // therefore joins the sum only where ours expects the block at least once in its trees (CheckDecorations), and a
    // block one probe compares is the per-probe rule's alone (cocoa on jungle_tree: 10 of the 70).
    // The bound was 3 until the pool held ten leaf-litter probes and the pale garden's blocks: then the check
    // false-failed 21 runs in 1000 (leaf_litter 13, its combined z's deviation 1.30), so it is 3.5 (the S9c manager's
    // call F; recalibrated below).
    constexpr double PooledDecorationBound = 3.5;
    bool IsPooledFailure(const TPair<int32, double>& Pooled)
    {
        return Pooled.Key >= 2 && FMath::Abs(Pooled.Value) > PooledDecorationBound;
    }
    TMap<int32, TPair<int32, double>> PoolDecorationZ(TConstArrayView<TMap<int32, double>> PerProbe)
    {
        TMap<int32, TPair<int32, double>> Pooled;
        for (const TMap<int32, double>& Probe : PerProbe)
            for (const TPair<int32, double>& Block : Probe)
            {
                TPair<int32, double>& Sum = Pooled.FindOrAdd(Block.Key, TPair<int32, double>(0, 0.));
                ++Sum.Key;
                Sum.Value += Block.Value;
            }
        for (TPair<int32, TPair<int32, double>>& Block : Pooled)
            Block.Value.Value /= FMath::Sqrt(double(Block.Value.Key));
        return Pooled;
    }

    // Compares one probe with our run; returns the failures (empty when it passes) and a summary line. DecoratorBlocks
    // are the blocks the tree's built decorators can write (GetDecoratorBlocks); OutDecorationZ takes each compared
    // decoration block's z.
    TArray<FString> CompareTrees(const FTreeTestKit& Kit, const FVoxelTreeConfig& Config, const FTreeProbe& Probe,
        const FTreeRun& Run, const TSet<int32>& DecoratorBlocks, FString& OutLine, TMap<int32, double>* OutDecorationZ)
    {
        TArray<FString> Failures;
        const double Rate = Run.Tries ? double(Run.Placed) / Run.Tries : 0;
        const double RateP = BinomialTwoSided(Probe.Placed, Probe.Tries, Rate);
        OutLine = FString::Printf(TEXT("%s: placed %.0f%%/%.0f%% (p %.2f)"), *Probe.Name, 100 * Rate,
            100. * Probe.Placed / FMath::Max(Probe.Tries, 1), RateP);
        if (RateP < 0.01) Failures.Add(TEXT("placed rate"));
        if (Run.FailedWrites) Failures.Add(FString::Printf(TEXT("%d failed tries emitted writes"), Run.FailedWrites));
        if (Probe.bPlacedRateOnly)
        {
            OutLine += TEXT(" (placed rate only)");
            return Failures;
        }
        // "Placed none against some" failed every probe that placed none while ours placed some, until S9c. That fails
        // a correct model: under mega_spruce_ceil15's ceiling ours places only h 13, 1/45 = 2.2% of its tries, and the
        // probe's 0 of 16 has p (44/45)^16 = 0.70 under that rate; with ours' tries in the thousands, every run places
        // some. So a probe that placed none now fails only when its none is unlikely under our placed rate q, (1 - q)^n
        // < 0.01 for its n tries (16 tries: q over about 25%). Ours none against the probe's some stays categorical,
        // and the placed-rate binomial above still applies (the S9c manager's call).
        if (Probe.Placed == 0 || Run.Placed == 0)
        {
            if (Run.Placed == 0 && Probe.Placed != 0) Failures.AddUnique(TEXT("placed none against some"));
            const double NoneP = std::pow(1 - Rate, double(Probe.Tries));
            if (Probe.Placed == 0 && Run.Placed != 0 && NoneP < 0.01)
                Failures.AddUnique(
                    FString::Printf(TEXT("placed none against some (ours %.1f%%, (1 - q)^n %.4f)"), 100 * Rate, NoneP));
            return Failures;
        }
        const bool bSmall = IsSmallProbe(Probe);
        // Exact rules: the leaves' distance and persistence; the blocks the probe saw; the 26-connected trunk.
        if (Run.BadDistances) Failures.Add(FString::Printf(TEXT("%d leaves off the BFS distance"), Run.BadDistances));
        if (Run.Persistent) Failures.Add(FString::Printf(TEXT("%d persistent leaves"), Run.Persistent));
        for (const int32 Block : Run.Blocks)
        {
            bool bSeen = false;
            for (const FString& Id : Probe.Blocks)
                bSeen |= Kit.Table->FindBlock(Id) == Block;
            // Until S9c every block the probe never saw failed here. D2 then exempted every decoration block on a
            // probe of 50 tries or more, so that a nest the probe's 100 trees never drew (oak_bees_002 drew none at
            // probability 0.02; ours draw some in 2000) is tested by CheckDecorations instead, its mean per tree
            // against the probe's 0 by the z rule. That also let through any decoration a decorator should never
            // write. Since the S9c manager's call 1a, an unseen block passes only when it is a decoration one of this
            // tree's built decorators can write (their states and their providers' states, DecoratorBlocks), and is
            // then z-tested as D2 had it; every other unseen block fails as before S9c.
            if (!bSeen && (bSmall || !DecoratorBlocks.Contains(Block) || !Run.DecorationSums.Contains(Block)))
                Failures.Add(FString::Printf(TEXT("block %d not in the probe's blocks"), Block));
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
            CheckDecorations(Kit, Config, Probe, Run, OutLine, Failures, OutDecorationZ);
            double DirtMean, DirtDeviation;
            MeanAndDeviation(Run.Ground, DirtMean, DirtDeviation);
            if (FMath::Abs(DirtMean - Probe.Dirt) > 0.05)
                Failures.Add(FString::Printf(TEXT("soil under the trunk %.2f (probe %.2f)"), DirtMean, Probe.Dirt));
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
            OutLine += FString::Printf(TEXT(", top p %.3f"), TopP);
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
            // (A fallen tree's probe has no leaf layers to compare.)
            if (Probe.ProfileCount && !Probe.Occupancy.IsEmpty())
            {
                FString Worst;
                const double OccupancyP = GetOccupancyP(Probe, Run, Worst);
                OutLine += FString::Printf(TEXT(", occupancy p %.3f (%s)"), OccupancyP, *Worst);
                if (OccupancyP < 0.01) Failures.Add(TEXT("leaf occupancy"));
            }
            else if (Probe.ProfileCount)
                OutLine += TEXT(", occupancy skipped (no leaf layers)");
            OutLine += FString::Printf(TEXT(", reach %d/%d"), Run.Reach, Probe.Reach);
            if (Run.Reach > Probe.Reach) Failures.Add(TEXT("reach"));
            // The reach of every cell (the decorators' writes included). Until the S9c review it failed whenever our
            // largest over TreeTestCount trees passed the probe's largest over its 100, so a correct decorator with a
            // rare far cell failed by sampling alone. Now it is a guard calibrated like the probe-none one: under the
            // null one of our trees passes the largest of the probe's 100 with probability about 1/101, so it fails
            // only when more than 5% of our placed trees reach beyond the probe's largest (a correct model's 100 probe
            // trees all fall short of that 5% quantile with probability 0.95^100 = 0.006).
            int64 Beyond = 0;
            for (const TPair<int32, int64>& Bin : Run.AllReachHist)
                if (Bin.Key > Probe.AllReach) Beyond += Bin.Value;
            const double BeyondShare = double(Beyond) / FMath::Max(Run.Placed, 1);
            if (Run.AllReach != Run.Reach || Probe.AllReach != Probe.Reach || Beyond)
            {
                OutLine += FString::Printf(
                    TEXT(", every cell %d/%d (beyond %.1f%%)"), Run.AllReach, Probe.AllReach, 100 * BeyondShare);
                if (BeyondShare > 0.05)
                    Failures.Add(FString::Printf(
                        TEXT("reach of every cell (%.1f%% of trees beyond %d)"), 100 * BeyondShare, Probe.AllReach));
            }
            const double Comp6P = ChiSquareHomogeneity(Run.Comp6Hist, Probe.Comp6Hist);
            OutLine += FString::Printf(TEXT(", 6-connected parts %.2f/%.2f p %.3f"), GetHistogramMean(Run.Comp6Hist),
                GetHistogramMean(Probe.Comp6Hist), Comp6P);
            if (Comp6P < 0.01) Failures.Add(TEXT("6-connected log parts"));
            // A fallen tree's log directions (S9d part 2, the manager's call Q3): ours against the probe's four bins,
            // shown here; the test is the pooled one over the fallen probes (RunTreeProbes), since one rule draws the
            // direction for all of them and one probe's 100 trees give 25 per bin.
            if (!Probe.SideHist.IsEmpty())
                OutLine += FString::Printf(
                    TEXT(", log directions p %.3f"), ChiSquareHomogeneity(Run.SideHist, Probe.SideHist));
        }
        return Failures;
    }

    // A sample of our own trees dressed as a probe, for the calibration below: its per-tree counts, its leaf layers'
    // shares, rounded to 0.01 as the probes keep them, its tries and placed trees, and its decoration blocks' means
    // per tree by id (Names: the probes' ids by block).
    FTreeProbe MakeSampleProbe(const FTreeRun& Sample, const TMap<int32, FString>& Names)
    {
        FTreeProbe Probe;
        Probe.Tries = Sample.Tries;
        Probe.Placed = Sample.Placed;
        for (const TPair<int32, double>& Decoration : Sample.DecorationSums)
            if (const FString* Name = Names.Find(Decoration.Key))
                Probe.BlockMeans.Add(*Name, Decoration.Value / FMath::Max(Sample.Placed, 1));
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
    void ApplyTreeTestMutation(const FVoxelBlockTable& Table, FVoxelTreeConfig& Config)
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
        // Scout J's "beehive probability x2" (must fail distribution 4: the nests per tree).
        if (TreeTestMutation == 3)
            for (FVoxelTreeDecoratorConfig& Decorator : Config.Decorators)
                if (Decorator.Type == EVoxelTreeDecorator::Beehive)
                    for (TPair<FName, double>& Number : Decorator.Numbers)
                        if (Number.Key == FName(TEXT("probability"))) Number.Value *= 2;
        // The probe-none guard's (CompareTrees): the giant trunk's base_height 5 lower, so under mega_spruce_ceil15's
        // ceiling (h at most 13) ours places 15/45 = 33% of its tries, and the probe's 0 of 16 has (2/3)^16 = 0.0015
        // (must fail the guard; ceil12's 6/45 = 13% passes it).
        if (TreeTestMutation == 4 && EVoxelTrunkPlacer(Config.Trunk.Type) == EVoxelTrunkPlacer::Giant)
            for (TPair<FName, FVoxelIntProvider>& Field : Config.Trunk.Ints)
                if (Field.Key == FName(TEXT("base_height")))
                {
                    Field.Value.Min -= 5;
                    Field.Value.Max -= 5;
                }
        // The block-set rule's (CompareTrees, the S9c manager's call 1a): the beehive writes oak_planks, a block none
        // of the tree's built decorators can write (RunTreeProbes reads their blocks before this change), so every bee
        // probe whose trees draw a nest must fail "not in the probe's blocks".
        if (TreeTestMutation == 5)
            for (FVoxelTreeDecoratorConfig& Decorator : Config.Decorators)
                if (Decorator.Type == EVoxelTreeDecorator::Beehive)
                    for (uint16& State : Decorator.States)
                        State = Table.FindState(TEXT("oak_planks"));
        // The rare-block rule's (CheckDecorations, the S9c manager's call B): cocoa's probability zeroed, so ours sets
        // none where jungle_tree's probe has 0.44 per tree (must fail "cocoa per tree -100.0%" on jungle_tree).
        if (TreeTestMutation == 6)
            for (FVoxelTreeDecoratorConfig& Decorator : Config.Decorators)
                if (Decorator.Type == EVoxelTreeDecorator::Cocoa)
                    for (TPair<FName, double>& Number : Decorator.Numbers)
                        if (Number.Key == FName(TEXT("probability"))) Number.Value = 0;
        // The every-cell reach guard's (CompareTrees, the S9c review): place_on_ground's radius 2 wider (its default 2
        // where the configuration gives none), so the litter reaches past the litter probes' largest on most trees
        // (must fail "reach of every cell" on them).
        if (TreeTestMutation == 7)
            for (FVoxelTreeDecoratorConfig& Decorator : Config.Decorators)
                if (Decorator.Type == EVoxelTreeDecorator::PlaceOnGround)
                {
                    bool bFound = false;
                    for (TPair<FName, double>& Number : Decorator.Numbers)
                        if (Number.Key == FName(TEXT("radius")))
                        {
                            Number.Value += 2;
                            bFound = true;
                        }
                    if (!bFound) Decorator.Numbers.Emplace(FName(TEXT("radius")), 4.);
                }
        // The merged chi-square's (S9e part 4): upwards_branching's place_branch_per_log_probability 0.8 times, so the
        // mangroves grow a fifth fewer branches, each its own 6-connected part (must fail "6-connected log parts" on
        // the mangrove pools).
        if (TreeTestMutation == 17)
            for (TPair<FName, double>& Number : Config.Trunk.Numbers)
                if (Number.Key == FName(TEXT("place_branch_per_log_probability"))) Number.Value *= 0.8;
    }

    // An example's cells (relative to the origin, by state name "block[property=value,...]") made in our table, against
    // ours (null: none of ours of its height): empty when every cell matches, else the first mismatch.
    FString MatchExample(
        const FVoxelBlockTable& Table, const TMap<FIntVector, FString>& Example, const TMap<FIntVector, uint16>* Ours)
    {
        int32 Top = MIN_int32;
        for (const TPair<FIntVector, FString>& Cell : Example)
            Top = FMath::Max(Top, Cell.Key.Z);
        if (!Ours) return FString::Printf(TEXT("none of ours with its top at %d"), Top);
        if (Ours->Num() != Example.Num())
            return FString::Printf(TEXT("%d cells against %d"), Ours->Num(), Example.Num());
        for (const TPair<FIntVector, FString>& Cell : Example)
        {
            FString Block = Cell.Value, Properties;
            TArray<TPair<FName, FName>> Pairs;
            if (Cell.Value.Split(TEXT("["), &Block, &Properties))
            {
                TArray<FString> Items;
                Properties.LeftChop(1).ParseIntoArray(Items, TEXT(","));
                for (const FString& Item : Items)
                {
                    FString Key, Value;
                    if (Item.Split(TEXT("="), &Key, &Value)) Pairs.Add({FName(*Key), FName(*Value)});
                }
            }
            const int32 State = Table.MakeState(Block, Pairs);
            const uint16* Our = Ours->Find(Cell.Key);
            if (!Our || State == INDEX_NONE || *Our != State)
                return FString::Printf(TEXT("(%d, %d, %d) %s"), Cell.Key.X, Cell.Key.Y, Cell.Key.Z, *Cell.Value);
        }
        return FString();
    }

    // A huge mushroom probe (S9d part 2, the manager's call Q3) against TreeTestCount of ours on its column, each from
    // its own stream: the placed rate; caps exactly 45 per mushroom (scout J's exact rule 6); stems per mushroom (the
    // probe's cells less 45) by CheckMeans' rule; the cap's top dy histogram (chi-square homogeneity); the reach and
    // the blocks exactly; and each of the probe's examples cell for cell against our first mushroom of its height (ours
    // draws nothing but the height). Returns the failures and a summary line.
    TArray<FString> CompareMushrooms(
        const FTreeTestKit& Kit, const FVoxelHugeMushroomConfig& Config, const FTreeProbe& Probe, FString& OutLine)
    {
        const FVoxelBlockTable& Table = *Kit.Table;
        const TSharedRef<FVoxelColumn> Column = MakeProbeColumn(Table, Probe);
        const FVoxelColumn* Around[9];
        for (const FVoxelColumn*& Each : Around)
            Each = &*Column;
        FVoxelDecorationContext Context(FIntPoint(0, 0), Around, &Table);
        const FIntVector Origin(8, 8, Probe.OriginZ);
        TArray<FVoxelPlantWrite> Writes;
        const auto Emit = [&Writes](const FVoxelPlantWrite& Write) { Writes.Add(Write); };
        const FVoxelPlantEnvironment Env{
            Table, Kit.Survival, Kit.Predicates, Kit.Providers, Context, Kit.UpperOf, Emit};
        const int32 CapBlock = Config.Cap.IsEmpty() ? INDEX_NONE : Table.Get(Config.Cap[0].Value).Block;
        const int32 StemBlock = Table.Get(Config.Stem).Block;
        int32 Placed = 0, BadCaps = 0, Reach = 0;
        TArray<double> Stems;
        TMap<int32, int64> Tops;
        TSet<int32> Blocks;
        TMap<int32, TMap<FIntVector, uint16>> ByTop;
        for (int32 Index = 0; Index < TreeTestCount; ++Index)
        {
            Writes.Reset();
            FVoxelXoroshiro Random(VoxelRandom::Hash64(TreeTestSeed, VoxelRandom::Salt("tree test"), Index, 0));
            if (!VoxelBigVegetation::PlaceHugeMushroom(Config, Origin, Random, Env)) continue;
            ++Placed;
            int32 Caps = 0, StemCount = 0, Top = MIN_int32;
            TMap<FIntVector, uint16> Cells;
            for (const FVoxelPlantWrite& Write : Writes)
            {
                const int32 Block = Table.Get(Write.State).Block;
                Blocks.Add(Block);
                Caps += Block == CapBlock;
                StemCount += Block == StemBlock;
                Top = FMath::Max(Top, Write.Cell.Z - Origin.Z);
                Reach = FMath::Max(
                    Reach, FMath::Max(FMath::Abs(Write.Cell.X - Origin.X), FMath::Abs(Write.Cell.Y - Origin.Y)));
                Cells.Add(Write.Cell - Origin, Write.State);
            }
            BadCaps += Caps != 45;
            Stems.Add(StemCount);
            ++Tops.FindOrAdd(Top);
            if (!ByTop.Contains(Top)) ByTop.Add(Top, MoveTemp(Cells));
        }
        TArray<FString> Failures;
        const double Rate = double(Placed) / TreeTestCount;
        const double RateP = BinomialTwoSided(Probe.Placed, Probe.Tries, Rate);
        OutLine = FString::Printf(TEXT("%s: placed %.0f%%/%.0f%% (p %.2f)"), *Probe.Name, 100 * Rate,
            100. * Probe.Placed / FMath::Max(Probe.Tries, 1), RateP);
        if (RateP < 0.01) Failures.Add(TEXT("placed rate"));
        if (!Placed) return Failures;
        OutLine += FString::Printf(TEXT(", caps not 45 on %d"), BadCaps);
        if (BadCaps) Failures.Add(FString::Printf(TEXT("%d mushrooms without exactly 45 caps"), BadCaps));
        TArray<double> TheirStems;
        for (const double Cells : Probe.Cells)
            TheirStems.Add(Cells - 45);
        double OurMean, OurDeviation, TheirMean, TheirDeviation;
        MeanAndDeviation(Stems, OurMean, OurDeviation);
        MeanAndDeviation(TheirStems, TheirMean, TheirDeviation);
        const double Error = FMath::Sqrt(FMath::Square(TheirDeviation) / FMath::Max(TheirStems.Num(), 1) +
            FMath::Square(OurDeviation) / FMath::Max(Stems.Num(), 1));
        const double Z = Error > 0 ? (OurMean - TheirMean) / Error : (OurMean == TheirMean ? 0 : 99);
        const double Relative = TheirMean ? (OurMean - TheirMean) / TheirMean : 0;
        OutLine += FString::Printf(TEXT(", stems %.2f/%.2f (%+.1f%%, z %+.1f)"), OurMean, TheirMean, 100 * Relative, Z);
        if (FMath::Abs(Relative) > 0.05 && FMath::Abs(Z) > 3)
            Failures.Add(FString::Printf(TEXT("stems %+.1f%%"), 100 * Relative));
        const double TopP = ChiSquareHomogeneity(Tops, Probe.CapTopHist);
        OutLine += FString::Printf(TEXT(", cap top p %.3f, reach %d/%d"), TopP, Reach, Probe.AllReach);
        if (TopP < 0.01) Failures.Add(TEXT("cap top histogram"));
        if (Reach != Probe.AllReach) Failures.Add(TEXT("reach"));
        for (const int32 Block : Blocks)
        {
            bool bSeen = false;
            for (const FString& Id : Probe.Blocks)
                bSeen |= Table.FindBlock(Id) == Block;
            if (!bSeen) Failures.Add(FString::Printf(TEXT("block %d not in the probe's blocks"), Block));
        }
        // The examples: each against our first mushroom of its top (MatchExample).
        int32 Exact = 0;
        for (const TMap<FIntVector, FString>& Example : Probe.Examples)
        {
            int32 Top = MIN_int32;
            for (const TPair<FIntVector, FString>& Cell : Example)
                Top = FMath::Max(Top, Cell.Key.Z);
            const FString Mismatch = MatchExample(Table, Example, ByTop.Find(Top));
            Exact += Mismatch.IsEmpty();
            if (!Mismatch.IsEmpty()) Failures.Add(FString::Printf(TEXT("an example differs: %s"), *Mismatch));
        }
        OutLine += FString::Printf(TEXT(", examples exact %d/%d"), Exact, Probe.Examples.Num());
        return Failures;
    }

    // Ours against the probe's per-tree values by CheckMeans' rule (fails when over 5% off and |z| > 3), on the line.
    void CompareMeanValues(const TCHAR* Name, const TArray<double>& Ours, const TArray<double>& Theirs,
        FString& OutLine, TArray<FString>& Failures)
    {
        double OurMean, OurDeviation, TheirMean, TheirDeviation;
        MeanAndDeviation(Ours, OurMean, OurDeviation);
        MeanAndDeviation(Theirs, TheirMean, TheirDeviation);
        const double Error = FMath::Sqrt(FMath::Square(TheirDeviation) / FMath::Max(Theirs.Num(), 1) +
            FMath::Square(OurDeviation) / FMath::Max(Ours.Num(), 1));
        const double Z = Error > 0 ? (OurMean - TheirMean) / Error : (OurMean == TheirMean ? 0 : 99);
        const double Relative = TheirMean ? (OurMean - TheirMean) / TheirMean : (OurMean ? 1 : 0);
        OutLine +=
            FString::Printf(TEXT(", %s %.2f/%.2f (%+.1f%%, z %+.1f)"), Name, OurMean, TheirMean, 100 * Relative, Z);
        if (FMath::Abs(Relative) > 0.05 && FMath::Abs(Z) > 3)
            Failures.Add(FString::Printf(TEXT("%s %+.1f%%"), Name, 100 * Relative));
    }

    // Writes of TreeTestCount bamboo stalks or block blobs (S9e part 2) on the probe's column, each from its own
    // stream, as CompareMushrooms runs them; per placed one, its writes.
    TArray<TArray<FVoxelPlantWrite>> SimulatePlants(const FTreeTestKit& Kit, const FTreeProbe& Probe,
        TFunctionRef<bool(FVoxelXoroshiro&, const FVoxelPlantEnvironment&)> Place)
    {
        const FVoxelBlockTable& Table = *Kit.Table;
        const TSharedRef<FVoxelColumn> Column = MakeProbeColumn(Table, Probe);
        const FVoxelColumn* Around[9];
        for (const FVoxelColumn*& Each : Around)
            Each = &*Column;
        FVoxelDecorationContext Context(FIntPoint(0, 0), Around, &Table);
        TArray<FVoxelPlantWrite> Writes;
        const auto Emit = [&Writes](const FVoxelPlantWrite& Write) { Writes.Add(Write); };
        const FVoxelPlantEnvironment Env{
            Table, Kit.Survival, Kit.Predicates, Kit.Providers, Context, Kit.UpperOf, Emit};
        TArray<TArray<FVoxelPlantWrite>> Placed;
        for (int32 Index = 0; Index < TreeTestCount; ++Index)
        {
            Writes.Reset();
            FVoxelXoroshiro Random(VoxelRandom::Hash64(TreeTestSeed, VoxelRandom::Salt("tree test"), Index, 0));
            if (Place(Random, Env)) Placed.Add(Writes);
        }
        return Placed;
    }

    // The placed rate against the probe's (an exact binomial, fails below 0.01), on the line; false when none placed.
    bool ComparePlaced(const FTreeProbe& Probe, int32 Placed, FString& OutLine, TArray<FString>& Failures)
    {
        const double Rate = double(Placed) / TreeTestCount;
        const double RateP = BinomialTwoSided(Probe.Placed, Probe.Tries, Rate);
        OutLine = FString::Printf(TEXT("%s: placed %.0f%%/%.0f%% (p %.2f)"), *Probe.Name, 100 * Rate,
            100. * Probe.Placed / FMath::Max(Probe.Tries, 1), RateP);
        if (RateP < 0.01) Failures.Add(TEXT("placed rate"));
        return Placed > 0;
    }

    // A bamboo probe (S9e part 2, F3b's checks) against TreeTestCount of ours: the placed rate; the stalk's top dy
    // histogram (chi-square homogeneity); the share of stalks with a podzol disk (an exact binomial, fails below 0.01)
    // and podzol per stalk (CheckMeans' rule); the reach and the blocks exactly; each example's stalk cell for cell
    // against our first stalk of its height (its podzol left out: ours draws the disk apart from the height).
    TArray<FString> CompareBamboo(
        const FTreeTestKit& Kit, const FVoxelBambooConfig& Config, const FTreeProbe& Probe, FString& OutLine)
    {
        const FVoxelBlockTable& Table = *Kit.Table;
        const FIntVector Origin(8, 8, Probe.OriginZ);
        const TArray<TArray<FVoxelPlantWrite>> Placed =
            SimulatePlants(Kit, Probe, [&](FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env)
                { return VoxelBigVegetation::PlaceBamboo(Config, Origin, Random, Env); });
        TArray<FString> Failures;
        if (!ComparePlaced(Probe, Placed.Num(), OutLine, Failures)) return Failures;
        const int32 PodzolBlock = Table.Get(Config.Podzol).Block;
        int32 Reach = 0, PodzolStalks = 0;
        TArray<double> Podzols;
        TMap<int32, int64> Tops;
        TSet<int32> Blocks;
        TMap<int32, TMap<FIntVector, uint16>> ByTop;
        for (const TArray<FVoxelPlantWrite>& Writes : Placed)
        {
            int32 Podzol = 0, Top = MIN_int32;
            TMap<FIntVector, uint16> Stalk;
            for (const FVoxelPlantWrite& Write : Writes)
            {
                const int32 Block = Table.Get(Write.State).Block;
                Blocks.Add(Block);
                Reach = FMath::Max(
                    Reach, FMath::Max(FMath::Abs(Write.Cell.X - Origin.X), FMath::Abs(Write.Cell.Y - Origin.Y)));
                if (Block == PodzolBlock)
                {
                    ++Podzol;
                    continue;
                }
                Top = FMath::Max(Top, Write.Cell.Z - Origin.Z);
                Stalk.Add(Write.Cell - Origin, Write.State);
            }
            Podzols.Add(Podzol);
            PodzolStalks += Podzol > 0;
            ++Tops.FindOrAdd(Top);
            if (!ByTop.Contains(Top)) ByTop.Add(Top, MoveTemp(Stalk));
        }
        const double TopP = ChiSquareHomogeneity(Tops, Probe.CapTopHist);
        OutLine += FString::Printf(TEXT(", top p %.3f"), TopP);
        if (TopP < 0.01) Failures.Add(TEXT("stalk top histogram"));
        // The probe's podzol per stalk: its cells less the stalk's (top + 1), on open flat ground.
        TArray<double> TheirPodzols;
        int32 TheirPodzolStalks = 0;
        for (int32 Index = 0; Index < Probe.Cells.Num() && Index < Probe.Tops.Num(); ++Index)
        {
            TheirPodzols.Add(Probe.Cells[Index] - (Probe.Tops[Index] + 1));
            TheirPodzolStalks += TheirPodzols.Last() > 0;
        }
        const double ShareP =
            BinomialTwoSided(TheirPodzolStalks, TheirPodzols.Num(), double(PodzolStalks) / Placed.Num());
        OutLine += FString::Printf(TEXT(", podzol stalks %.1f%%/%.1f%% (p %.2f)"), 100. * PodzolStalks / Placed.Num(),
            100. * TheirPodzolStalks / FMath::Max(TheirPodzols.Num(), 1), ShareP);
        if (ShareP < 0.01) Failures.Add(TEXT("podzol stalks"));
        CompareMeanValues(TEXT("podzol"), Podzols, TheirPodzols, OutLine, Failures);
        OutLine += FString::Printf(TEXT(", reach %d/%d"), Reach, Probe.AllReach);
        if (Reach != Probe.AllReach) Failures.Add(TEXT("reach"));
        for (const int32 Block : Blocks)
        {
            bool bSeen = false;
            for (const FString& Id : Probe.Blocks)
                bSeen |= Table.FindBlock(Id) == Block;
            if (!bSeen) Failures.Add(FString::Printf(TEXT("block %d not in the probe's blocks"), Block));
        }
        int32 Exact = 0;
        for (const TMap<FIntVector, FString>& Example : Probe.Examples)
        {
            TMap<FIntVector, FString> Stalk;
            int32 Top = MIN_int32;
            for (const TPair<FIntVector, FString>& Cell : Example)
                if (Cell.Value.StartsWith(TEXT("bamboo")))
                {
                    Stalk.Add(Cell.Key, Cell.Value);
                    Top = FMath::Max(Top, Cell.Key.Z);
                }
            const FString Mismatch = MatchExample(Table, Stalk, ByTop.Find(Top));
            Exact += Mismatch.IsEmpty();
            if (!Mismatch.IsEmpty()) Failures.Add(FString::Printf(TEXT("an example differs: %s"), *Mismatch));
        }
        OutLine += FString::Printf(TEXT(", examples exact %d/%d"), Exact, Probe.Examples.Num());
        return Failures;
    }

    // The forest rock probe (block_blob, S9e part 2, F3b's checks) against TreeTestCount of ours: the placed rate;
    // cells per rock (CheckMeans' rule) and their histogram (chi-square homogeneity in bins of three cells, 24 and more
    // in one, F3b's bins, kept when ChiSquareHomogeneity began merging sparse bins); the box's extremes on every
    // axis, the reach and the blocks exactly. (F3b matched its two examples by an exhaustive search over the draws.)
    TArray<FString> CompareBlob(
        const FTreeTestKit& Kit, const FVoxelBlockBlobConfig& Config, const FTreeProbe& Probe, FString& OutLine)
    {
        const FVoxelBlockTable& Table = *Kit.Table;
        const FIntVector Origin(8, 8, Probe.OriginZ);
        const TArray<TArray<FVoxelPlantWrite>> Placed =
            SimulatePlants(Kit, Probe, [&](FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env)
                { return VoxelBigVegetation::PlaceBlockBlob(Config, Origin, Random, Env); });
        TArray<FString> Failures;
        if (!ComparePlaced(Probe, Placed.Num(), OutLine, Failures)) return Failures;
        TArray<double> Cells;
        TMap<int32, int64> Counts, TheirCounts;
        FIntVector Low(MAX_int32), High(MIN_int32);
        int32 Reach = 0;
        TSet<int32> Blocks;
        for (const TArray<FVoxelPlantWrite>& Writes : Placed)
        {
            Cells.Add(Writes.Num());
            ++Counts.FindOrAdd(FMath::Min(Writes.Num(), 24) / 3);
            for (const FVoxelPlantWrite& Write : Writes)
            {
                const FIntVector At = Write.Cell - Origin;
                Low = FIntVector(FMath::Min(Low.X, At.X), FMath::Min(Low.Y, At.Y), FMath::Min(Low.Z, At.Z));
                High = FIntVector(FMath::Max(High.X, At.X), FMath::Max(High.Y, At.Y), FMath::Max(High.Z, At.Z));
                Reach = FMath::Max(Reach, FMath::Max(FMath::Abs(At.X), FMath::Abs(At.Y)));
                Blocks.Add(Table.Get(Write.State).Block);
            }
        }
        for (const double Count : Probe.Cells)
            ++TheirCounts.FindOrAdd(FMath::Min(int32(Count), 24) / 3);
        CompareMeanValues(TEXT("cells"), Cells, Probe.Cells, OutLine, Failures);
        const double CountP = ChiSquareHomogeneity(Counts, TheirCounts);
        OutLine +=
            FString::Printf(TEXT(", cells p %.3f, box (%d, %d, %d) to (%d, %d, %d) against (%d, %d, %d) to (%d, %d, "
                                 "%d), reach %d/%d"),
                CountP, Low.X, Low.Y, Low.Z, High.X, High.Y, High.Z, Probe.BoxMin.X, Probe.BoxMin.Y, Probe.BoxMin.Z,
                Probe.BoxMax.X, Probe.BoxMax.Y, Probe.BoxMax.Z, Reach, Probe.AllReach);
        if (CountP < 0.01) Failures.Add(TEXT("cells histogram"));
        if (Low != Probe.BoxMin || High != Probe.BoxMax) Failures.Add(TEXT("box extremes"));
        if (Reach != Probe.AllReach) Failures.Add(TEXT("reach"));
        for (const int32 Block : Blocks)
        {
            bool bSeen = false;
            for (const FString& Id : Probe.Blocks)
                bSeen |= Table.FindBlock(Id) == Block;
            if (!bSeen) Failures.Add(FString::Printf(TEXT("block %d not in the probe's blocks"), Block));
        }
        return Failures;
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
        Test.TestEqual(TEXT("tree probes"), Files.Num(), 94);
        // Every file, a probe's re-probe runs (S9e part 3: the four mangroves, three runs each) pooled into one probe
        // after the others. Mutation 15: only each probe's first run (its tries must fail).
        TArray<FTreeProbe> Read;
        TMap<FString, TArray<FTreeProbe>> RunsOf;
        for (const FString& File : Files)
        {
            FTreeProbe Probe;
            FString Error, Base;
            if (!Test.TestTrue(FString::Printf(TEXT("%s reads (%s)"), *File, *Error),
                    ReadTreeProbe(FPaths::Combine(Folder, File), Probe, Error)))
                return false;
            // The accepted tall_mangrove@mudwater: its placed rate only (FTreeProbe::bPlacedRateOnly).
            Probe.bPlacedRateOnly = Probe.Name == TEXT("tall_mangrove@mudwater");
            if (GetProbeRun(Probe.Name, Base) == INDEX_NONE)
                Read.Add(MoveTemp(Probe));
            else if (TreeTestMutation != 15 || !RunsOf.Contains(Base))
                RunsOf.FindOrAdd(Base).Add(MoveTemp(Probe));
        }
        RunsOf.KeySort(TLess<FString>());
        for (const TPair<FString, TArray<FTreeProbe>>& Pool : RunsOf)
        {
            const FTreeProbe& First = Pool.Value[0];
            bool bSameProbe = true;
            for (const FTreeProbe& Run : Pool.Value)
                bSameProbe &= Run.Feature == First.Feature && Run.Variant == First.Variant &&
                    Run.OriginZ == First.OriginZ && Run.Layers.Num() == First.Layers.Num() &&
                    Run.Prep.Num() == First.Prep.Num();
            Test.TestTrue(FString::Printf(TEXT("%s's runs are one probe"), *Pool.Key), bSameProbe);
            Read.Add(PoolTreeProbes(Pool.Value, FString::Printf(TEXT("%s pooled"), *Pool.Key)));
            Test.TestEqual(FString::Printf(TEXT("%s pooled tries"), *Pool.Key), Read.Last().Tries, 300);
        }
        Test.TestEqual(TEXT("re-probed probes"), RunsOf.Num(), 4);
        TArray<FTreeProbe> Probes;
        TArray<FVoxelTreeConfig> Configs;
        // Per probe, its fallen tree (S9d part 2) and its root system (S9e), else null; and the huge mushroom probes
        // compared.
        TArray<TSharedPtr<FVoxelFallenTreeConfig>> Fallen;
        TArray<TSharedPtr<FVoxelRootSystemConfig>> RootSystems;
        int32 ComparedApart = 0;
        TArray<TSet<int32>> DecoratorBlocks;
        TArray<FString> Skipped;
        for (FTreeProbe& Probe : Read)
        {
            if (!Filter(Probe)) continue;
            FVoxelTreeConfig Config;
            TSharedPtr<FVoxelFallenTreeConfig> FallenTree;
            TSharedPtr<FVoxelHugeMushroomConfig> Mushroom;
            TSharedPtr<FVoxelRootSystemConfig> RootSystem;
            TSharedPtr<FVoxelBambooConfig> Bamboo;
            TSharedPtr<FVoxelBlockBlobConfig> Blob;
            FString Skip;
            if (!Kit.Compile(Test, Probe.Feature, Config, Skip, &FallenTree, &Mushroom, &RootSystem, &Bamboo, &Blob))
            {
                if (!Skip.IsEmpty()) Skipped.Add(FString::Printf(TEXT("%s (%s)"), *Probe.Name, *Skip));
                continue;
            }
            // A huge mushroom is compared here (CompareMushrooms), not by the tree rules.
            if (Mushroom)
            {
                FString Line;
                const TArray<FString> Failures = CompareMushrooms(Kit, *Mushroom, Probe, Line);
                Test.AddInfo(Line);
                for (const FString& Failure : Failures)
                    Test.AddError(FString::Printf(TEXT("%s: %s"), *Probe.Name, *Failure));
                ++ComparedApart;
                continue;
            }
            // Bamboo and the forest rock (S9e part 2) are compared here too (CompareBamboo, CompareBlob). Mutation 12:
            // every stalk one block taller (the top histogram must fail); 13: the blob's centre never moves (its box
            // and cells must fail).
            if (Bamboo || Blob)
            {
                if (TreeTestMutation == 12 && Bamboo) ++Bamboo->MinHeight;
                if (TreeTestMutation == 13 && Blob) Blob->MoveChance = 0;
                FString Line;
                const TArray<FString> Failures =
                    Bamboo ? CompareBamboo(Kit, *Bamboo, Probe, Line) : CompareBlob(Kit, *Blob, Probe, Line);
                Test.AddInfo(Line);
                for (const FString& Failure : Failures)
                    Test.AddError(FString::Printf(TEXT("%s: %s"), *Probe.Name, *Failure));
                ++ComparedApart;
                continue;
            }
            // The built decorators' blocks, read before a mutation changes what one writes (mutation 5).
            DecoratorBlocks.Add(GetDecoratorBlocks(Kit, Config));
            // Mutations 1 to 7 change Config, which a fallen tree's simulation does not run (it runs FallenTree); they
            // aim at the standing trees. Mutation 8 changes the fallen tree itself.
            ApplyTreeTestMutation(*Kit.Table, Config);
            if (TreeTestMutation == 8 && FallenTree)
            {
                ++FallenTree->LogLength.Min;
                ++FallenTree->LogLength.Max;
            }
            // Mutation 10 (S9e): random_spread's leaf_placement_attempts halved (azalea and the rooted azalea's tree);
            // 11: the root system's root_placement_attempts halved (its rooted dirt).
            if (TreeTestMutation == 10)
                for (FVoxelTreeConfig* Tree : {&Config, RootSystem ? &RootSystem->Tree : nullptr})
                    if (Tree && Tree->Foliage.Type == uint8(EVoxelFoliagePlacer::RandomSpread))
                        for (TPair<FName, FVoxelIntProvider>& Field : Tree->Foliage.Ints)
                            if (Field.Key == FName(TEXT("leaf_placement_attempts")))
                                Field.Value = FVoxelIntProvider::MakeConstant(Field.Value.Max / 2);
            if (TreeTestMutation == 11 && RootSystem) RootSystem->RootPlacementAttempts /= 2;
            Probes.Add(MoveTemp(Probe));
            Configs.Add(MoveTemp(Config));
            Fallen.Add(FallenTree);
            RootSystems.Add(RootSystem);
        }
        TArray<FTreeRun> Runs;
        Runs.SetNum(Probes.Num());
        ParallelFor(Probes.Num(),
            [&](int32 Index)
            {
                SimulateTrees(Kit, Configs[Index], Probes[Index], TreeTestCount, TreeTestSeed, Runs[Index],
                    Fallen[Index].Get(), RootSystems[Index].Get());
            });
        TArray<TMap<int32, double>> DecorationZ;
        DecorationZ.SetNum(Probes.Num());
        for (int32 Index = 0; Index < Probes.Num(); ++Index)
        {
            FString Line;
            const TArray<FString> Failures = CompareTrees(
                Kit, Configs[Index], Probes[Index], Runs[Index], DecoratorBlocks[Index], Line, &DecorationZ[Index]);
            Test.AddInfo(Line);
            for (const FString& Failure : Failures)
                Test.AddError(FString::Printf(TEXT("%s: %s"), *Probes[Index].Name, *Failure));
        }
        // The pooled decoration check (PoolDecorationZ), one line per block.
        TMap<int32, FString> Names;
        for (const FTreeProbe& Probe : Probes)
            for (const TPair<FString, double>& Block : Probe.BlockMeans)
                Names.Add(Kit.Table->FindBlock(Block.Key), Block.Key);
        const auto GetName = [&Names](int32 Block)
        {
            const FString* Name = Names.Find(Block);
            return Name ? *Name : FString::Printf(TEXT("block %d"), Block);
        };
        TMap<int32, TPair<int32, double>> Pooled = PoolDecorationZ(DecorationZ);
        Pooled.KeySort([&GetName](int32 A, int32 B) { return GetName(A) < GetName(B); });
        for (const TPair<int32, TPair<int32, double>>& Block : Pooled)
        {
            Test.AddInfo(FString::Printf(TEXT("pooled decoration %s: k %d, combined z %+.2f"), *GetName(Block.Key),
                Block.Value.Key, Block.Value.Value));
            if (IsPooledFailure(Block.Value))
                Test.AddError(FString::Printf(TEXT("pooled decoration %s: combined z %+.2f over %d probes"),
                    *GetName(Block.Key), Block.Value.Value, Block.Value.Key));
        }
        // The fallen logs' directions over the fallen probes (the manager's call): each probe's chi-square statistic
        // summed, against the sum of their degrees of freedom, so one skewed probe still counts (CompareTrees shows
        // each probe's p).
        double SideChi2 = 0;
        int32 SideDof = 0, SideProbes = 0;
        for (int32 Index = 0; Index < Probes.Num(); ++Index)
        {
            if (Probes[Index].SideHist.IsEmpty()) continue;
            TPair<double, int32> Chi2;
            ChiSquareHomogeneity(Runs[Index].SideHist, Probes[Index].SideHist, &Chi2);
            SideChi2 += Chi2.Key;
            SideDof += Chi2.Value;
            ++SideProbes;
        }
        if (SideDof > 0)
        {
            const double SideP = UpperGammaQ(SideDof / 2., SideChi2 / 2);
            Test.AddInfo(FString::Printf(TEXT("fallen log directions over %d probes: chi-square %.2f on %d degrees of "
                                              "freedom (p %.3f)"),
                SideProbes, SideChi2, SideDof, SideP));
            if (SideP < 0.01) Test.AddError(FString::Printf(TEXT("fallen log directions (p %.4f)"), SideP));
        }
        if (Skipped.Num()) Test.AddInfo(FString::Printf(TEXT("skipped: %s"), *FString::Join(Skipped, TEXT(", "))));
        // For manual runs (TreeTestCalibration > 0): each configuration's own 100-tree samples, scored as probes
        // against its TreeTestCount trees. A correct model must rarely fail the distribution rules this way. The
        // samples' seeds differ per configuration as well, so that they are independent across probes as the probes
        // are (the pooled check adds their z); sample s of every configuration makes one run of the pooled check.
        if (TreeTestCalibration > 0)
        {
            // Per configuration: samples failing occupancy (X), a mean (Y), a decoration block (Z).
            TArray<FIntVector> Fails;
            Fails.SetNumZeroed(Probes.Num());
            TArray<TArray<TMap<int32, double>>> SampleZ;
            SampleZ.SetNum(TreeTestCalibration);
            for (TArray<TMap<int32, double>>& Each : SampleZ)
                Each.SetNum(Probes.Num());
            TArray<bool> DecorationFailed;
            DecorationFailed.SetNumZeroed(TreeTestCalibration * Probes.Num());
            ParallelFor(Probes.Num(),
                [&](int32 Index)
                {
                    // As CompareTrees: a small (ceiling) probe takes no distribution rule, so no sample of it either.
                    if (IsSmallProbe(Probes[Index])) return;
                    for (int32 Sample = 0; Sample < TreeTestCalibration; ++Sample)
                    {
                        FTreeRun Own;
                        SimulateTrees(Kit, Configs[Index], Probes[Index], 100,
                            TreeTestSeed + 1 + Sample + uint64(Index) * TreeTestCalibration, Own, Fallen[Index].Get(),
                            RootSystems[Index].Get());
                        const FTreeProbe AsProbe = MakeSampleProbe(Own, Names);
                        FString Line, Worst;
                        TArray<FString> MeanFailures, DecorationFailures;
                        CheckMeans(AsProbe, Runs[Index], Line, MeanFailures);
                        Fails[Index].X += GetOccupancyP(AsProbe, Runs[Index], Worst) < 0.01;
                        Fails[Index].Y += MeanFailures.Num() > 0;
                        if (Own.Placed && Runs[Index].Placed)
                            CheckDecorations(Kit, Configs[Index], AsProbe, Runs[Index], Line, DecorationFailures,
                                &SampleZ[Sample][Index]);
                        Fails[Index].Z += DecorationFailures.Num() > 0;
                        DecorationFailed[Sample * Probes.Num() + Index] = DecorationFailures.Num() > 0;
                    }
                });
            for (int32 Index = 0; Index < Probes.Num(); ++Index)
                if (!IsSmallProbe(Probes[Index]))
                    Test.AddInfo(
                        FString::Printf(TEXT("calibration %s: occupancy p under 0.01 in %d of %d samples, a mean "
                                             "failed in %d, a decoration block in %d"),
                            *Probes[Index].Name, Fails[Index].X, TreeTestCalibration, Fails[Index].Y, Fails[Index].Z));
            // Per block: the samples failing the pooled check (X), the largest k (Y), and the combined z's squares.
            TMap<int32, FIntPoint> PooledFails;
            TMap<int32, double> PooledSquares;
            int32 AnyDecoration = 0, AnyPooled = 0;
            for (int32 Sample = 0; Sample < TreeTestCalibration; ++Sample)
            {
                bool bDecoration = false, bPooled = false;
                for (int32 Index = 0; Index < Probes.Num(); ++Index)
                    bDecoration |= DecorationFailed[Sample * Probes.Num() + Index];
                for (const TPair<int32, TPair<int32, double>>& Block : PoolDecorationZ(SampleZ[Sample]))
                {
                    const bool bFail = IsPooledFailure(Block.Value);
                    FIntPoint& Count = PooledFails.FindOrAdd(Block.Key, FIntPoint(0, 0));
                    Count.X += bFail;
                    Count.Y = FMath::Max(Count.Y, Block.Value.Key);
                    PooledSquares.FindOrAdd(Block.Key) += FMath::Square(Block.Value.Value);
                    bPooled |= bFail;
                }
                AnyDecoration += bDecoration;
                AnyPooled += bPooled;
            }
            PooledFails.KeySort([&GetName](int32 A, int32 B) { return GetName(A) < GetName(B); });
            for (const TPair<int32, FIntPoint>& Block : PooledFails)
                Test.AddInfo(FString::Printf(TEXT("calibration pooled decoration %s: k %d, failed in %d of %d samples "
                                                  "(k 2 or more, |combined z| over %.1f), sd %.2f"),
                    *GetName(Block.Key), Block.Value.Y, Block.Value.X, TreeTestCalibration, PooledDecorationBound,
                    FMath::Sqrt(PooledSquares[Block.Key] / TreeTestCalibration)));
            Test.AddInfo(FString::Printf(TEXT("calibration runs (sample s of every configuration): a probe's "
                                              "decoration rule failed in %d of %d, the pooled check in %d"),
                AnyDecoration, TreeTestCalibration, AnyPooled));
        }
        OutRun = Probes.Num() + ComparedApart;
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

        // Replaced maps a placed feature's name ("trees_plains") to the text read for it. False (with an error) when
        // DA_Blocks is missing, or on a read error.
        bool Initialize(FAutomationTestBase& Test, const TMap<FString, FString>& Replaced)
        {
            if (!Table->FindState(TEXT("stone")))
            {
                Test.AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
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
    // distributions. Every built configuration runs: 46 probes since S9e part 2 (S9c's 33, the six fallen trees, which
    // add their log directions, the two huge mushrooms, CompareMushrooms, the azalea tree and the rooted azalea tree,
    // its root system's roots counted as decorations, the two bamboos and the forest rock, CompareBamboo and
    // CompareBlob). The mangroves skip at their root placer until IsMangroveBuilt(); then the four accepted mangrove
    // probes (tall_mangrove@mudwater's by its placed rate alone) and the four mangroves' re-probe pools (S9e part 3)
    // make 54.
    int32 Ran = 0;
    if (!RunTreeProbes(*this, [](const FTreeProbe& Probe) { return Probe.Ceiling == 0; }, Ran)) return false;
    TestEqual(TEXT("probes run (a placer that stops compiling drops its probes)"), Ran, 46);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTreeFeatureSizeTest, "Unnamed.Voxel.Trees.FeatureSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTreeFeatureSizeTest::RunTest(const FString& Parameters)
{
    // The ceiling probes (a 17 x 17 stone ceiling at dy H, 16 tries): the placed rate, and every placed tree's top log
    // at H - 3 or lower (H - 2 for trunks other than straight): oak, spruce, fancy oak, dark oak and mega spruce, 32
    // probes since S9c.
    int32 Ran = 0;
    if (!RunTreeProbes(*this, [](const FTreeProbe& Probe) { return Probe.Ceiling != 0; }, Ran)) return false;
    TestEqual(TEXT("ceiling probes run"), Ran, 32);
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
    // The 9 saplings are in the block table since survival B1, so a tree placement's would_survive(<sapling>)
    // compiles to the sapling's own rule, which equals supports_vegetation for the block below (minecraft.wiki,
    // "Sapling"; Unnamed.Voxel.Plants.SaplingRule proves it): oak_checked's filter passes over grass and dirt and refuses sand and stone. Mangrove's propagule is in the table
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTreeVinesTest, "Unnamed.Voxel.Trees.Vines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTreeVinesTest::RunTest(const FString& Parameters)
{
    // The vines feature (S9e part 2, no probe: F3b's rule and its exact wall cases) on an empty column: each case's
    // blocks around the position, then the one vine written, or none. The wiki's support ("any block that fills its
    // whole cube and blocks entity movement") takes leaves (case g; F3b's own rule had them fail).
    FTreeTestKit Kit;
    if (!Kit.Initialize(*this)) return true;
    const FVoxelBlockTable& Table = *Kit.Table;
    FVoxelVinesConfig Config;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("the vines' states (%s)"), *Error),
            VoxelBigVegetation::CompileVines(Table, Config, &Error)))
        return false;
    // Mutation 14 (S9e part 2): the faces tested in the reverse order (cases c and i must fail).
    if (TreeTestMutation == 14) Algo::Reverse(Config.Order);
    const uint16 Stone = Table.FindState(TEXT("stone")), Leaves = Table.FindState(TEXT("oak_leaves"));
    const uint16 Slab = Table.FindState(TEXT("stone_slab"));
    const uint16 Vine = Config.FaceStates[int32(EVoxelFace::Up)];
    FTreeProbe Empty;
    const TSharedRef<FVoxelColumn> Column = MakeProbeColumn(Table, Empty);
    const FVoxelColumn* Around[9];
    for (const FVoxelColumn*& Each : Around)
        Each = &*Column;
    const FIntVector At(8, 8, 70);
    const FIntVector Up(0, 0, 1), North(0, -1, 0), East(1, 0, 0), South(0, 1, 0), West(-1, 0, 0);
    struct FCase
    {
        const TCHAR* Name;
        TArray<TPair<FIntVector, uint16>> Blocks;
        // The face written (an EVoxelFace), or -1 for none.
        int32 Face;
    };
    const FCase Cases[] = {
        {TEXT("a: stone north"), {{North, Stone}}, int32(EVoxelFace::North)},
        {TEXT("b: stone above"), {{Up, Stone}}, int32(EVoxelFace::Up)},
        {TEXT("c: stone above, west and north"), {{Up, Stone}, {West, Stone}, {North, Stone}}, int32(EVoxelFace::Up)},
        {TEXT("d: stone at the position"), {{FIntVector::ZeroValue, Stone}, {North, Stone}}, -1},
        {TEXT("e: six air neighbours"), {}, -1},
        {TEXT("f: a vine west"), {{West, Vine}}, -1},
        {TEXT("g: oak leaves west"), {{West, Leaves}}, int32(EVoxelFace::West)},
        {TEXT("h: a stone slab west"), {{West, Slab}}, -1},
        {TEXT("i: stone west and south"), {{West, Stone}, {South, Stone}}, int32(EVoxelFace::South)},
    };
    TestTrue(TEXT("the case blocks are in the table"), Stone && Leaves && Slab && Vine);
    for (const FCase& Case : Cases)
    {
        FVoxelDecorationContext Context(FIntPoint(0, 0), Around, &Table);
        for (const TPair<FIntVector, uint16>& Block : Case.Blocks)
            Context.SetState(At.X + Block.Key.X, At.Y + Block.Key.Y, At.Z + Block.Key.Z, Block.Value);
        TArray<FVoxelPlantWrite> Writes;
        const auto Emit = [&Writes](const FVoxelPlantWrite& Write) { Writes.Add(Write); };
        const FVoxelPlantEnvironment Env{
            Table, Kit.Survival, Kit.Predicates, Kit.Providers, Context, Kit.UpperOf, Emit};
        const bool bPlaced = VoxelBigVegetation::PlaceVine(Config, At, Env);
        const int32 Face = Writes.Num() == 1 && Writes[0].Cell == At ? Algo::IndexOf(Config.FaceStates, Writes[0].State)
                                                                     : (Writes.IsEmpty() ? -1 : -2);
        TestEqual(FString::Printf(TEXT("vines %s: the face written"), Case.Name), Face, Case.Face);
        TestEqual(FString::Printf(TEXT("vines %s: placed"), Case.Name), bPlaced, Case.Face >= 0);
        // The decorator's early-out skips a try only where no support can stand (none here but in case e).
        TestEqual(FString::Printf(TEXT("vines %s: skipped by the early-out"), Case.Name),
            VoxelBigVegetation::HasNoVineSupport(Config, At, Context), Case.Blocks.IsEmpty());
    }
    return true;
}
