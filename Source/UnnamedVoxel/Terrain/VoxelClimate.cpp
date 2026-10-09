#include "Terrain/VoxelClimate.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "VoxelConstants.h"
#include "VoxelStats.h"

namespace
{
    const TCHAR* const ClimateLabels[] = {
        TEXT("C"), TEXT("E"), TEXT("W"), TEXT("PV"), TEXT("T"), TEXT("V"), TEXT("O"), TEXT("F"), TEXT("J")};
    static_assert(UE_ARRAY_COUNT(ClimateLabels) == int32(EVoxelClimate::Count));

    // The named function at Node, or empty.
    FString ClimateFunctionName(const FVoxelDensityGraph& Graph, int32 Node)
    {
        for (const FVoxelDensityFunction& Function : Graph.Functions)
            if (Function.Node == Node) return Function.Name;
        return FString();
    }
} // namespace

const TCHAR* FVoxelClimateSampler::GetLabel(EVoxelClimate Parameter)
{
    return ClimateLabels[int32(Parameter)];
}

bool FVoxelClimateSampler::ParseLabel(FStringView Label, EVoxelClimate& OutParameter)
{
    for (int32 Index = 0; Index < int32(EVoxelClimate::Count); ++Index)
        if (Label.Equals(ClimateLabels[Index], ESearchCase::IgnoreCase))
        {
            OutParameter = EVoxelClimate(Index);
            return true;
        }
    return false;
}

bool FVoxelClimateSampler::Initialize(const FVoxelTerrainPreset& Preset, uint64 WorldSeed, FString* OutError)
{
    auto Fail = [OutError, &Preset](const FString& Reason)
    {
        if (OutError) *OutError = FString::Printf(TEXT("climate for preset %s: %s"), *Preset.Name, *Reason);
        return false;
    };
    const FVoxelDensityGraph& Graph = Preset.Graph;
    // A failed Initialize leaves the sampler uninitialized, never half replaced.
    Evaluator = FVoxelDensityEvaluator();
    DepthEvaluator = FVoxelDensityEvaluator();

    int32 PeaksAndValleys = INDEX_NONE;
    for (const FVoxelTerrainPreset::FDebugView& View : Preset.DebugViews)
        if (View.Label == TEXT("PV")) PeaksAndValleys = View.Node;

    // The spline functions are not router entries; they sit in the same folder as the router's depth, which reads the
    // offset ("minecraft:overworld_amplified/depth" -> ".../offset").
    const FString DepthName = ClimateFunctionName(Graph, Preset.Router.Depth);
    if (!DepthName.EndsWith(TEXT("/depth"), ESearchCase::CaseSensitive))
        return Fail(TEXT("the router's depth is not a named .../depth function"));
    const FString Folder = DepthName.LeftChop(5);

    Nodes[int32(EVoxelClimate::Continents)] = Preset.Router.Continents;
    Nodes[int32(EVoxelClimate::Erosion)] = Preset.Router.Erosion;
    Nodes[int32(EVoxelClimate::Ridges)] = Preset.Router.Ridges;
    Nodes[int32(EVoxelClimate::PeaksAndValleys)] = PeaksAndValleys;
    Nodes[int32(EVoxelClimate::Temperature)] = Preset.Router.Temperature;
    Nodes[int32(EVoxelClimate::Vegetation)] = Preset.Router.Vegetation;
    Nodes[int32(EVoxelClimate::Offset)] = Graph.FindFunction(Folder + TEXT("offset"));
    Nodes[int32(EVoxelClimate::Factor)] = Graph.FindFunction(Folder + TEXT("factor"));
    Nodes[int32(EVoxelClimate::Jaggedness)] = Graph.FindFunction(Folder + TEXT("jaggedness"));
    for (int32 Index = 0; Index < int32(EVoxelClimate::Count); ++Index)
        if (Nodes[Index] == INDEX_NONE) return Fail(FString::Printf(TEXT("no node for %s"), ClimateLabels[Index]));

    FString Error;
    if (!Evaluator.Compile(Graph, WorldSeed, Nodes, &Error)) return Fail(Error);
    DepthNode = Preset.Router.Depth;
    if (!DepthEvaluator.Compile(Graph, WorldSeed, {DepthNode}, &Error)) return Fail(Error);
    return true;
}

void FVoxelClimateSampler::Sample(const double* X, const double* Y, int32 Count, FVoxelDensityScratch& Scratch,
    TArrayView<FVoxelClimateSample> Out) const
{
    check(IsInitialized() && Out.Num() >= Count);
    // The climate ignores height (its noises have y scale 0), so the reference's y is 0 throughout.
    TArray<double, TInlineAllocator<64>> Zero;
    Zero.SetNumZeroed(Count);
    Evaluator.Evaluate(X, Zero.GetData(), Y, Count, Scratch);
    for (int32 Parameter = 0; Parameter < int32(EVoxelClimate::Count); ++Parameter)
    {
        const TConstArrayView<float> Values = Evaluator.GetValues(Scratch, Nodes[Parameter]);
        for (int32 Index = 0; Index < Count; ++Index)
            Out[Index].Values[Parameter] = Values[Index];
    }
}

void FVoxelClimateSampler::SampleColumn(
    int32 ColumnX, int32 ColumnY, FVoxelDensityScratch& Scratch, FVoxelColumnClimate& Out) const
{
    SCOPE_CYCLE_COUNTER(STAT_VoxelClimateColumn);
    TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_ClimateColumn);
    const double Start = FPlatformTime::Seconds();

    constexpr int32 Count = FVoxelColumnClimate::Corners * FVoxelColumnClimate::Corners;
    double X[Count], Y[Count];
    const double BaseX = double(ColumnX) * VoxelConstants::ColumnSize;
    const double BaseY = double(ColumnY) * VoxelConstants::ColumnSize;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        X[Index] = BaseX + (Index % FVoxelColumnClimate::Corners) * FVoxelColumnClimate::CellWidth;
        Y[Index] = BaseY + (Index / FVoxelColumnClimate::Corners) * FVoxelColumnClimate::CellWidth;
    }
    Sample(X, Y, Count, Scratch, Out.Samples);
    Out.Seconds = FPlatformTime::Seconds() - Start;
}

float FVoxelClimateSampler::SampleDepth(double X, double Y, double Z, FVoxelDensityScratch& Scratch) const
{
    check(IsInitialized());
    DepthEvaluator.Evaluate(&X, &Z, &Y, 1, Scratch);
    return DepthEvaluator.GetValues(Scratch, DepthNode)[0];
}
