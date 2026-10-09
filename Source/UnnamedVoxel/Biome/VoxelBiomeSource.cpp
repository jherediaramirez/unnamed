#include "Biome/VoxelBiomeSource.h"
#include "Algo/Sort.h"
#include "Biome/VoxelBiomeStorage.h"
#include "Density/VoxelDensityGraph.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "VoxelConstants.h"
#include "VoxelStats.h"

namespace
{
    // The five parameters the group trees search, in EVoxelClimateAxis terms; depth is the groups' own.
    constexpr EVoxelClimateAxis BiomeFlatAxes[5] = {EVoxelClimateAxis::Temperature, EVoxelClimateAxis::Humidity,
        EVoxelClimateAxis::Continentalness, EVoxelClimateAxis::Erosion, EVoxelClimateAxis::Weirdness};
    // Points per tree leaf.
    constexpr int32 BiomeLeafSize = 8;

    bool BiomeSourceFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    // How far a target lies outside a range (0 inside), as the jar's Climate.Parameter.distance.
    FORCEINLINE int64 BiomeDistance(int64 Target, int64 Min, int64 Max)
    {
        return Target < Min ? Min - Target : (Target > Max ? Target - Max : 0);
    }

    FORCEINLINE int64 BiomeSquare(int64 Value)
    {
        return Value * Value;
    }

    // The fitness of a point on the five flat parameters, plus its offset squared.
    int64 BiomeFlatFitness(const FVoxelBiomeParameter& Point, const int64 Flat[5])
    {
        int64 Fitness = BiomeSquare(Point.Offset);
        for (int32 Axis = 0; Axis < 5; ++Axis)
        {
            const int32 Index = int32(BiomeFlatAxes[Axis]);
            Fitness += BiomeSquare(BiomeDistance(Flat[Axis], Point.Min[Index], Point.Max[Index]));
        }
        return Fitness;
    }

    // The quantized five flat values of a climate sample.
    void BiomeFlatTarget(const FVoxelClimateSample& Sample, int64 Out[5])
    {
        Out[0] = FVoxelBiomeTable::Quantize(Sample[EVoxelClimate::Temperature]);
        Out[1] = FVoxelBiomeTable::Quantize(Sample[EVoxelClimate::Vegetation]);
        Out[2] = FVoxelBiomeTable::Quantize(Sample[EVoxelClimate::Continents]);
        Out[3] = FVoxelBiomeTable::Quantize(Sample[EVoxelClimate::Erosion]);
        Out[4] = FVoxelBiomeTable::Quantize(Sample[EVoxelClimate::Ridges]);
    }
} // namespace

int64 FVoxelBiomeSource::GetFitness(const FVoxelBiomeParameter& Point, const FVoxelBiomeTarget& Target)
{
    int64 Fitness = BiomeSquare(Point.Offset);
    for (int32 Axis = 0; Axis < int32(EVoxelClimateAxis::Count); ++Axis)
        Fitness += BiomeSquare(BiomeDistance(Target.Values[Axis], Point.Min[Axis], Point.Max[Axis]));
    return Fitness;
}

bool FVoxelBiomeSource::InitializeTable(const FVoxelBiomeTable& InTable, FString* OutError)
{
    // A failed Initialize leaves the source uninitialized, never half replaced.
    *this = FVoxelBiomeSource();
    FString Reason;
    if (!InTable.Validate(&Reason)) return BiomeSourceFail(OutError, TEXT("biome table: ") + Reason);
    // Storage keeps biome indices in a byte, with FVoxelBiomeStorage::None free.
    if (InTable.Biomes.Num() >= FVoxelBiomeStorage::None)
        return BiomeSourceFail(OutError,
            FString::Printf(
                TEXT("biome table: %d biomes, at most %d"), InTable.Biomes.Num(), int32(FVoxelBiomeStorage::None) - 1));
    Table = InTable;
    return true;
}

bool FVoxelBiomeSource::Initialize(
    const FVoxelBiomeTable& InTable, const FVoxelTerrainPreset& Preset, uint64 WorldSeed, FString* OutError)
{
    if (!InitializeTable(InTable, OutError)) return false;
    auto Fail = [this, OutError, &Preset](const FString& Reason)
    {
        *this = FVoxelBiomeSource();
        return BiomeSourceFail(OutError, FString::Printf(TEXT("biomes for preset %s: %s"), *Preset.Name, *Reason));
    };
    if (InTable.Points.IsEmpty()) return Fail(TEXT("the parameter list has no points"));

    // The depth must be the offset plus a y gradient, so a quart column's depths follow from its offset alone.
    const FVoxelDensityGraph& Graph = Preset.Graph;
    if (!Graph.Nodes.IsValidIndex(Preset.Router.Depth)) return Fail(TEXT("the router has no depth"));
    FString DepthName;
    for (const FVoxelDensityFunction& Function : Graph.Functions)
        if (Function.Node == Preset.Router.Depth) DepthName = Function.Name;
    if (!DepthName.EndsWith(TEXT("/depth"), ESearchCase::CaseSensitive))
        return Fail(TEXT("the router's depth is not a named .../depth function"));
    const int32 Offset = Graph.FindFunction(DepthName.LeftChop(5) + TEXT("offset"));
    const FVoxelDensityNode& Depth = Graph.Nodes[Preset.Router.Depth];
    int32 Gradient = INDEX_NONE;
    if (Depth.Op == EVoxelDensityOp::Add && Offset != INDEX_NONE)
        Gradient = Depth.Inputs[0] == Offset ? Depth.Inputs[1] : (Depth.Inputs[1] == Offset ? Depth.Inputs[0] : -1);
    if (!Graph.Nodes.IsValidIndex(Gradient) || Graph.Nodes[Gradient].Op != EVoxelDensityOp::Gradient ||
        Graph.Nodes[Gradient].Axis != 1)
        return Fail(DepthName + TEXT(" is not a y gradient plus the offset"));
    for (int32 Index = 0; Index < 4; ++Index)
        DepthGradient[Index] = Graph.Nodes[Gradient].Params[Index];

    FString Reason;
    if (!Climate.Initialize(Preset, WorldSeed, &Reason)) return Fail(Reason);
    Seed = WorldSeed;
    BuildGroups();
    return true;
}

bool FVoxelBiomeSource::InitializeFixed(const FVoxelBiomeTable& InTable, int32 Biome, FString* OutError)
{
    if (!InitializeTable(InTable, OutError)) return false;
    if (!Table.Biomes.IsValidIndex(Biome))
    {
        *this = FVoxelBiomeSource();
        return BiomeSourceFail(OutError, FString::Printf(TEXT("fixed biome index %d is not in the table"), Biome));
    }
    FixedBiome = Biome;
    return true;
}

void FVoxelBiomeSource::BuildGroups()
{
    for (int32 Index = 0; Index < Table.Points.Num(); ++Index)
    {
        const FVoxelBiomeParameter& Point = Table.Points[Index];
        const int64 DepthMin = Point.Min[int32(EVoxelClimateAxis::Depth)];
        const int64 DepthMax = Point.Max[int32(EVoxelClimateAxis::Depth)];
        FGroup* Group = Groups.FindByPredicate([DepthMin, DepthMax](const FGroup& Each)
            { return Each.DepthMin == DepthMin && Each.DepthMax == DepthMax; });
        if (!Group)
        {
            Group = &Groups.AddDefaulted_GetRef();
            Group->DepthMin = DepthMin;
            Group->DepthMax = DepthMax;
        }
        FFlatPoint& Flat = Group->Points.AddDefaulted_GetRef();
        for (int32 Axis = 0; Axis < 5; ++Axis)
        {
            Flat.Min[Axis] = Point.Min[int32(BiomeFlatAxes[Axis])];
            Flat.Max[Axis] = Point.Max[int32(BiomeFlatAxes[Axis])];
        }
        Flat.OffsetSquared = BiomeSquare(Point.Offset);
        Flat.Index = Index;
    }
    for (FGroup& Group : Groups)
    {
        Group.Nodes.SetNum(1);
        BuildNode(Group, 0, 0, Group.Points.Num());
    }
}

void FVoxelBiomeSource::BuildNode(FGroup& Group, int32 Node, int32 First, int32 Count)
{
    FNode Result;
    Result.MinOffsetSquared = MAX_int64;
    Result.MinIndex = MAX_int32;
    int64 CenterMin[5], CenterMax[5];
    for (int32 Axis = 0; Axis < 5; ++Axis)
    {
        Result.Min[Axis] = CenterMin[Axis] = MAX_int64;
        Result.Max[Axis] = CenterMax[Axis] = MIN_int64;
    }
    for (int32 Index = First; Index < First + Count; ++Index)
    {
        const FFlatPoint& Point = Group.Points[Index];
        for (int32 Axis = 0; Axis < 5; ++Axis)
        {
            Result.Min[Axis] = FMath::Min(Result.Min[Axis], Point.Min[Axis]);
            Result.Max[Axis] = FMath::Max(Result.Max[Axis], Point.Max[Axis]);
            // Twice the range's centre, which orders the same.
            const int64 Center = Point.Min[Axis] + Point.Max[Axis];
            CenterMin[Axis] = FMath::Min(CenterMin[Axis], Center);
            CenterMax[Axis] = FMath::Max(CenterMax[Axis], Center);
        }
        Result.MinOffsetSquared = FMath::Min(Result.MinOffsetSquared, Point.OffsetSquared);
        Result.MinIndex = FMath::Min(Result.MinIndex, Point.Index);
    }
    if (Count <= BiomeLeafSize)
    {
        Result.First = First;
        Result.Count = Count;
        Group.Nodes[Node] = Result;
        return;
    }

    // Split at the median along the axis whose range centres spread most.
    int32 Split = 0;
    for (int32 Axis = 1; Axis < 5; ++Axis)
        if (CenterMax[Axis] - CenterMin[Axis] > CenterMax[Split] - CenterMin[Split]) Split = Axis;
    Algo::Sort(MakeArrayView(Group.Points.GetData() + First, Count),
        [Split](const FFlatPoint& A, const FFlatPoint& B)
        {
            const int64 CenterA = A.Min[Split] + A.Max[Split], CenterB = B.Min[Split] + B.Max[Split];
            return CenterA != CenterB ? CenterA < CenterB : A.Index < B.Index;
        });
    Result.Left = Group.Nodes.AddDefaulted(2);
    Group.Nodes[Node] = Result;
    const int32 Half = Count / 2;
    BuildNode(Group, Result.Left, First, Half);
    BuildNode(Group, Result.Left + 1, First + Half, Count - Half);
}

FVoxelBiomeMatch FVoxelBiomeSource::FindInGroup(const FGroup& Group, const int64 Flat[5], int32 Hint) const
{
    FVoxelBiomeMatch Best;
    // A hint only tightens the first bound: it is a real point of the group, so the answer stays exact.
    if (Hint != INDEX_NONE)
    {
        Best.Point = Hint;
        Best.Fitness = BiomeFlatFitness(Table.Points[Hint], Flat);
    }
    const auto Bound = [Flat](const FNode& Node)
    {
        int64 Result = Node.MinOffsetSquared;
        for (int32 Axis = 0; Axis < 5; ++Axis)
            Result += BiomeSquare(BiomeDistance(Flat[Axis], Node.Min[Axis], Node.Max[Axis]));
        return Result;
    };
    // A node can hold a better point only if its bound is lower, or equal with a lower list index somewhere below.
    const auto MayImprove = [&Best](int64 NodeBound, int32 MinIndex)
    { return NodeBound < Best.Fitness || (NodeBound == Best.Fitness && MinIndex < Best.Point); };

    struct FEntry
    {
        int32 Node;
        int64 Bound;
    };
    // Depth-first, nearer child first; a balanced tree over a few thousand points is far shallower than this.
    FEntry Stack[128];
    int32 Top = 0;
    Stack[Top++] = {0, Bound(Group.Nodes[0])};
    while (Top > 0)
    {
        const FEntry Entry = Stack[--Top];
        const FNode& Node = Group.Nodes[Entry.Node];
        if (!MayImprove(Entry.Bound, Node.MinIndex)) continue;
        if (Node.Left == INDEX_NONE)
        {
            for (int32 Index = Node.First; Index < Node.First + Node.Count; ++Index)
            {
                const FFlatPoint& Point = Group.Points[Index];
                int64 Fitness = Point.OffsetSquared;
                for (int32 Axis = 0; Axis < 5; ++Axis)
                    Fitness += BiomeSquare(BiomeDistance(Flat[Axis], Point.Min[Axis], Point.Max[Axis]));
                if (Fitness < Best.Fitness || (Fitness == Best.Fitness && Point.Index < Best.Point))
                {
                    Best.Fitness = Fitness;
                    Best.Point = Point.Index;
                }
            }
            continue;
        }
        const FEntry Left = {Node.Left, Bound(Group.Nodes[Node.Left])};
        const FEntry Right = {Node.Left + 1, Bound(Group.Nodes[Node.Left + 1])};
        check(Top + 2 <= int32(UE_ARRAY_COUNT(Stack)));
        if (Left.Bound <= Right.Bound)
        {
            Stack[Top++] = Right;
            Stack[Top++] = Left;
        }
        else
        {
            Stack[Top++] = Left;
            Stack[Top++] = Right;
        }
    }
    return Best;
}

FVoxelBiomeMatch FVoxelBiomeSource::PickGroup(const FVoxelBiomeMatch* Winners, int64 Depth) const
{
    FVoxelBiomeMatch Best;
    for (int32 Index = 0; Index < Groups.Num(); ++Index)
    {
        const FVoxelBiomeMatch& Winner = Winners[Index];
        if (Winner.Point == INDEX_NONE) continue;
        const int64 Fitness =
            Winner.Fitness + BiomeSquare(BiomeDistance(Depth, Groups[Index].DepthMin, Groups[Index].DepthMax));
        if (Fitness < Best.Fitness || (Fitness == Best.Fitness && Winner.Point < Best.Point))
        {
            Best.Fitness = Fitness;
            Best.Point = Winner.Point;
        }
    }
    return Best;
}

FVoxelBiomeMatch FVoxelBiomeSource::Find(const FVoxelBiomeTarget& Target) const
{
    check(IsInitialized());
    if (IsFixed()) return {INDEX_NONE, 0};
    int64 Flat[5];
    for (int32 Axis = 0; Axis < 5; ++Axis)
        Flat[Axis] = Target[BiomeFlatAxes[Axis]];
    TArray<FVoxelBiomeMatch, TInlineAllocator<8>> Winners;
    for (const FGroup& Group : Groups)
        Winners.Add(FindInGroup(Group, Flat, INDEX_NONE));
    return PickGroup(Winners.GetData(), Target[EVoxelClimateAxis::Depth]);
}

FVoxelBiomeMatch FVoxelBiomeSource::FindBruteForce(const FVoxelBiomeTarget& Target) const
{
    check(IsInitialized());
    if (IsFixed()) return {INDEX_NONE, 0};
    FVoxelBiomeMatch Best;
    for (int32 Index = 0; Index < Table.Points.Num(); ++Index)
    {
        const int64 Fitness = GetFitness(Table.Points[Index], Target);
        // Strictly lower only: of equal fitness, the earlier point stays.
        if (Fitness < Best.Fitness)
        {
            Best.Fitness = Fitness;
            Best.Point = Index;
        }
    }
    return Best;
}

int32 FVoxelBiomeSource::GetBiome(const FVoxelBiomeMatch& Match) const
{
    if (IsFixed()) return FixedBiome;
    return Table.Points.IsValidIndex(Match.Point) ? Table.Points[Match.Point].Biome : INDEX_NONE;
}

float FVoxelBiomeSource::GetDepth(float Offset, double Z) const
{
    // As FVoxelDensityEvaluator's gradient, then its add.
    const double From = DepthGradient[0], To = DepthGradient[2];
    const double Alpha = To == From ? (Z >= To ? 1. : 0.) : FMath::Clamp((Z - From) / (To - From), 0., 1.);
    const float Gradient = float(DepthGradient[1] + Alpha * (DepthGradient[3] - DepthGradient[1]));
    return Gradient + Offset;
}

void FVoxelBiomeSource::SampleTargets(const double* X, const double* Y, const double* Z, int32 Count,
    FVoxelDensityScratch& Scratch, TArrayView<FVoxelBiomeTarget> Out) const
{
    check(IsInitialized() && !IsFixed() && Out.Num() >= Count);
    TArray<FVoxelClimateSample, TInlineAllocator<64>> Samples;
    Samples.SetNumUninitialized(Count);
    Climate.Sample(X, Y, Count, Scratch, Samples);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        int64 Flat[5];
        BiomeFlatTarget(Samples[Index], Flat);
        FVoxelBiomeTarget& Target = Out[Index];
        for (int32 Axis = 0; Axis < 5; ++Axis)
            Target[BiomeFlatAxes[Axis]] = Flat[Axis];
        Target[EVoxelClimateAxis::Depth] =
            Z ? FVoxelBiomeTable::Quantize(GetDepth(Samples[Index][EVoxelClimate::Offset], Z[Index])) : 0;
    }
}

void FVoxelBiomeSource::FillColumn(
    FIntPoint Column, FVoxelDensityScratch& Scratch, FVoxelBiomeStorage& Out, EVoxelBiomeFill Fill) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_BiomeColumn);
    SCOPE_CYCLE_COUNTER(STAT_VoxelBiomeColumn);
    check(IsInitialized());
    if (Fill == EVoxelBiomeFill::Off)
    {
        Out.Reset();
        return;
    }
    Out.Init(Column, Seed);
    constexpr int32 Width = FVoxelBiomeStorage::Width, Layers = FVoxelBiomeStorage::Layers;
    if (IsFixed())
    {
        for (int32 Layer = 0; Layer < Layers; ++Layer)
            for (int32 Index = 0; Index < Width * Width; ++Index)
                Out.SetQuart(Index % Width, Index / Width, Layer, uint8(FixedBiome));
        return;
    }

    // Each quart's minimum corner, 4 blocks apart from the column's minimum corner.
    constexpr int32 Count = Width * Width;
    double X[Count], Y[Count];
    for (int32 Index = 0; Index < Count; ++Index)
    {
        X[Index] = double(Column.X) * VoxelConstants::ColumnSize + (Index % Width) * FVoxelBiomeStorage::QuartSize;
        Y[Index] = double(Column.Y) * VoxelConstants::ColumnSize + (Index / Width) * FVoxelBiomeStorage::QuartSize;
    }
    FVoxelClimateSample Samples[Count];
    Climate.Sample(X, Y, Count, Scratch, MakeArrayView(Samples, Count));

    TArray<FVoxelBiomeMatch, TInlineAllocator<8>> Winners;
    Winners.SetNum(Groups.Num());
    for (int32 Index = 0; Index < Count; ++Index)
    {
        int64 Flat[5];
        BiomeFlatTarget(Samples[Index], Flat);
        const float Offset = Samples[Index][EVoxelClimate::Offset];
        // Neighbouring quart columns mostly share winners, so the last one is each group's first bound.
        if (Fill == EVoxelBiomeFill::Pruned)
            for (int32 Group = 0; Group < Groups.Num(); ++Group)
                Winners[Group] = FindInGroup(Groups[Group], Flat, Winners[Group].Point);
        FVoxelBiomeTarget Target;
        for (int32 Axis = 0; Axis < 5; ++Axis)
            Target[BiomeFlatAxes[Axis]] = Flat[Axis];
        for (int32 Layer = 0; Layer < Layers; ++Layer)
        {
            const int64 Depth = FVoxelBiomeTable::Quantize(
                GetDepth(Offset, double(VoxelConstants::MinZ + Layer * FVoxelBiomeStorage::QuartSize)));
            FVoxelBiomeMatch Match;
            if (Fill == EVoxelBiomeFill::Pruned)
                Match = PickGroup(Winners.GetData(), Depth);
            else
            {
                Target[EVoxelClimateAxis::Depth] = Depth;
                Match = FindBruteForce(Target);
            }
            Out.SetQuart(Index % Width, Index / Width, Layer, uint8(GetBiome(Match)));
        }
    }
}
