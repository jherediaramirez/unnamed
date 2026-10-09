#pragma once

#include "CoreMinimal.h"

class FVoxelSpline;

// The reference's density-function node types (Reference/minecraft-worldgen-26.3). Each comment says what the node's
// fields hold; "Inputs" are child nodes and "Params" constants, both in the order listed.
enum class EVoxelDensityOp : uint8
{
    Constant, // Params: value. A bare number in the reference.
    Add, // Inputs: left, right. Sub, Mul, Div, Min and Max likewise.
    Sub,
    Mul,
    Div,
    Min,
    Max,
    Abs, // Inputs: input. Negate to BlendDensity likewise.
    Negate,
    Square,
    Cube,
    HalfNegative,
    QuarterNegative,
    Squeeze,
    Cache,
    BlendDensity,
    Clamp, // Inputs: input. Params: min, max.
    Gradient, // Axis. Params: from coordinate, from value, to coordinate, to value.
    Lerp, // Inputs: alpha, first, second.
    RangeChoice, // Inputs: input, when in range, when out of range. Params: min inclusive, max exclusive.
    IntervalSelect, // Inputs: input. Ref and ListCount: the functions in ListNodes, the thresholds in ListValues.
    Noise, // Ref: the noise. Inputs: shift x, shift z, each optional. Params: xz scale, y scale.
    ShiftA, // Ref: the noise. ShiftB likewise.
    ShiftB,
    OldBlendedNoise, // Params: xz scale, y scale, xz factor, y factor, smear scale multiplier.
    Spline, // Ref: the root spline.
    Interpolated, // Inputs: input. Params: cell size xz, cell size y.
    FindTopSurface, // Inputs: density, upper bound. Params: lower bound, cell height.
    BlendAlpha, // No fields. BlendOffset and Beardifier likewise.
    BlendOffset,
    Beardifier,
    Count
};

struct FVoxelDensityNode
{
    EVoxelDensityOp Op = EVoxelDensityOp::Constant;
    // Gradient's axis in the reference's terms: 0 x, 1 y (up, our Z), 2 z.
    uint8 Axis = 0;
    int32 Inputs[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
    // A noise (Noise, ShiftA, ShiftB), a spline (Spline) or IntervalSelect's first list entry.
    int32 Ref = INDEX_NONE;
    int32 ListCount = 0;
    double Params[5] = {};
};

struct FVoxelDensitySplinePoint
{
    double Location = 0;
    double Derivative = 0;
    // The point's value when Child is INDEX_NONE; otherwise the nested spline Child gives it.
    double Value = 0;
    int32 Child = INDEX_NONE;
};

struct FVoxelDensitySpline
{
    // The node whose value the spline reads.
    int32 Coordinate = INDEX_NONE;
    int32 FirstPoint = 0;
    int32 PointCount = 0;
};

struct FVoxelDensityNoise
{
    FString Name;
    int32 BaseOctave = 0;
    double BaseAmplitude = 0;
    // One per octave, lowest frequency first. The reference leaves out the count when it is 1 and the modifiers when
    // they are all 1; both are filled in here.
    TArray<double> AmplitudeModifiers;
};

struct FVoxelDensityFunction
{
    FString Name;
    int32 Node = INDEX_NONE;
};

// A set of density functions as plain data: one flat array of nodes with children by index. A named function is one
// node however many others reference it, so evaluation can compute it once per point. Every node comes after the nodes
// it reads, and every spline after its nested splines, so evaluating in index order needs no recursion.
class UNNAMEDVOXEL_API FVoxelDensityGraph
{
public:
    TArray<FVoxelDensityNode> Nodes;
    // IntervalSelect lists, one entry per function: the function's node, and in ListValues the threshold between it and
    // the next (the last entry's value is unused).
    TArray<int32> ListNodes;
    TArray<double> ListValues;
    TArray<FVoxelDensitySpline> Splines;
    TArray<FVoxelDensitySplinePoint> SplinePoints;
    TArray<FVoxelDensityNoise> Noises;
    // Named functions in the order they were read.
    TArray<FVoxelDensityFunction> Functions;

    // A named function's node, or INDEX_NONE.
    int32 FindFunction(FStringView Name) const;
    // A noise's index, or INDEX_NONE.
    int32 FindNoise(FStringView Name) const;

    // Checks every index and count, each op's inputs, the order above, increasing thresholds and unique names.
    bool Validate(FString* OutError = nullptr) const;

    // Bakes spline Index into Out. OutCoordinates receives the nodes its coordinates read, in first use, depth first:
    // the order Out.Evaluate takes their values in.
    bool BuildSpline(int32 Index, FVoxelSpline& Out, TArray<int32>& OutCoordinates, FString* OutError = nullptr) const;

    // The reference's type name without its namespace ("add", "old_blended_noise").
    static const TCHAR* GetOpName(EVoxelDensityOp Op);
};

// A world type's terrain from the reference's noise settings: its numbers, the router's entry points, the aquifer
// inputs and the debug views, all nodes of one graph.
struct FVoxelTerrainPreset
{
    FString Name;
    int32 SeaLevel = 0;
    int32 MinY = 0;
    int32 Height = 0;
    FString DefaultBlock;
    FString DefaultFluid;

    struct FRouter
    {
        int32 Continents = INDEX_NONE;
        int32 Erosion = INDEX_NONE;
        int32 Ridges = INDEX_NONE;
        int32 Temperature = INDEX_NONE;
        int32 Vegetation = INDEX_NONE;
        int32 Depth = INDEX_NONE;
        int32 ChunkSurfaceLevel = INDEX_NONE;
        int32 FinalDensity = INDEX_NONE;
    } Router;

    struct FAquifers
    {
        int32 Barrier = INDEX_NONE;
        int32 Exclusion = INDEX_NONE;
        int32 FluidLevelFloodedness = INDEX_NONE;
        int32 FluidLevelSpread = INDEX_NONE;
        int32 Lava = INDEX_NONE;
        int32 SurfaceLevel = INDEX_NONE;
    } Aquifers;

    struct FDebugView
    {
        FString Label;
        int32 Node = INDEX_NONE;
    };
    TArray<FDebugView> DebugViews;

    FVoxelDensityGraph Graph;
};
