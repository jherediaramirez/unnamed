#pragma once

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "Density/VoxelDensityGraph.h"
#include "Noise/VoxelBlendedNoise.h"
#include "Noise/VoxelNormalNoise.h"
#include "Spline/VoxelSpline.h"

// Per-thread working memory for FVoxelDensityEvaluator: one float buffer per compiled node. Reused between calls, so a
// worker allocates only when a batch grows.
struct FVoxelDensityScratch
{
    TArray<float> Values;
    TArray<float> SplineInputs;
    int32 Count = 0;
    // For interpolated and find_top_surface: their inner functions' scratch, the positions they are read at and sums.
    TIndirectArray<FVoxelDensityScratch> Nested;
    TArray<double> NestedX, NestedY, NestedZ;
    TArray<float> NestedSums;
    // For a lazy range_choice: the points that pick a branch and their positions, read by the branch's scratch (in
    // Nested under the choice).
    TArray<int32> ChoicePoints;
    TArray<double> ChoiceX, ChoiceY, ChoiceZ;
    TArray<int32> NestedPoints;
    // For interval_select with lazy functions: each point's function.
    TArray<int32> Choices;
};

// A conservative range of a node's values over a box of positions.
struct FVoxelDensityBounds
{
    float Min = -UE_BIG_NUMBER;
    float Max = UE_BIG_NUMBER;
};

namespace VoxelDensity
{
    // Trilinear interpolation between a cell's 8 corner values, corner C at (C & 1, C >> 1 & 1, C >> 2 & 1) in
    // (x, y, z). Every caller uses this one function, so a column's interpolated values match direct evaluation.
    FORCEINLINE float Trilinear(const float Corners[8], float TX, float TY, float TZ)
    {
        const float X00 = Corners[0] + TX * (Corners[1] - Corners[0]);
        const float X10 = Corners[2] + TX * (Corners[3] - Corners[2]);
        const float X01 = Corners[4] + TX * (Corners[5] - Corners[4]);
        const float X11 = Corners[6] + TX * (Corners[7] - Corners[6]);
        const float Y0 = X00 + TY * (X10 - X00);
        const float Y1 = X01 + TY * (X11 - X01);
        return Y0 + TZ * (Y1 - Y0);
    }
} // namespace VoxelDensity

// Evaluates part of a density graph for one world seed over batches of points, one node at a time in the graph's
// order (inputs always come first), so each op's loop runs over the whole batch. Only the nodes the requested roots
// read are compiled. Positions use the reference's axes: x, y up, z. Values are 32-bit floats, as 26.3 evaluates
// density. Every op runs as the 26.3 jar showed by black-box sampling (Docs/VoxelWorld.md, Density evaluator).
// Immutable after Compile; any thread may evaluate with its own scratch, and equal inputs give bit-identical values.
class UNNAMEDVOXEL_API FVoxelDensityEvaluator
{
public:
    // Fails, with a reason, when the graph is invalid or a root reads an op this evaluator cannot run.
    bool Compile(
        const FVoxelDensityGraph& InGraph, uint64 WorldSeed, TConstArrayView<int32> Roots, FString* OutError = nullptr);

    bool IsCompiled() const { return !Slots.IsEmpty(); }

    // Evaluates every compiled node at Count points. Interpolated nodes read their input at the 8 corners of the
    // point's cell, so they cost 8 times their input.
    void Evaluate(const double* X, const double* Y, const double* Z, int32 Count, FVoxelDensityScratch& Scratch) const;

    // A compiled node's values from the last Evaluate with this scratch.
    TConstArrayView<float> GetValues(const FVoxelDensityScratch& Scratch, int32 Node) const;

    const FVoxelDensityGraph& GetGraph() const { return Graph; }

    // Pieces for callers that schedule nodes themselves (FVoxelColumnDensity): which nodes are compiled, which read
    // the vertical coordinate, and evaluation of a chosen subset whose other inputs the caller has filled in.
    bool IsCompiledNode(int32 Node) const { return Slots.IsValidIndex(Node) && Slots[Node] != INDEX_NONE; }
    TConstArrayView<int32> GetOrder() const { return Order; }
    bool ReadsY(int32 Node) const { return NodeReadsY[Node]; }
    // The compiled nodes that never read y, and those that may, each in evaluation order.
    TConstArrayView<int32> GetFlatOrder() const { return FlatOrder; }
    TConstArrayView<int32> GetVaryingOrder() const { return VaryingOrder; }
    // The nodes a node reads here: its inputs, a spline's coordinates or an interval list (find_top_surface: only its
    // upper bound).
    void GetReads(int32 Node, TArray<int32>& Out) const;
    // Sizes the scratch for Count points; values are left unset.
    void Prepare(FVoxelDensityScratch& Scratch, int32 Count) const;
    TArrayView<float> GetMutableValues(FVoxelDensityScratch& Scratch, int32 Node) const;
    // Whether a lazy range_choice evaluates this node (only at the points that pick its branch). Evaluate and
    // EvaluateNodes leave such nodes to their choice, so their buffers hold nothing; EvaluateBounds still bounds them.
    bool IsDeferred(int32 Node) const { return Owners.IsValidIndex(Node) && Owners[Node] != INDEX_NONE; }
    // Evaluates Nodes, in the order given (inputs first), at the scratch's prepared count of points. Deferred nodes
    // are skipped.
    void EvaluateNodes(TConstArrayView<int32> Nodes, const double* X, const double* Y, const double* Z,
        FVoxelDensityScratch& Scratch) const;
    // Conservative bounds of Nodes over positions whose y lies in [MinY, MaxY], in graph-node indexing. The caller
    // fills Bounds for every input outside Nodes. Noise is bounded by its octaves' amplitudes, splines and divisions
    // by ranges that may hold 0 are unbounded, and rounding is covered by a small widening.
    void EvaluateBounds(
        TConstArrayView<int32> Nodes, double MinY, double MaxY, TArray<FVoxelDensityBounds>& Bounds) const;

private:
    struct FCompiledSpline
    {
        FVoxelSpline Spline;
        TArray<int32> Coordinates;
    };

    void EvaluateNode(
        int32 Node, const double* X, const double* Y, const double* Z, FVoxelDensityScratch& Scratch) const;
    void EvaluateInterpolated(
        int32 Node, const double* X, const double* Y, const double* Z, FVoxelDensityScratch& Scratch) const;
    void EvaluateTopSurface(int32 Node, const double* X, const double* Z, FVoxelDensityScratch& Scratch) const;
    FVoxelDensityScratch& GetNested(FVoxelDensityScratch& Scratch, int32 Node) const;
    FVoxelDensityBounds GetBounds(
        int32 Node, double MinY, double MaxY, const TArray<FVoxelDensityBounds>& Bounds) const;
    float* Buffer(FVoxelDensityScratch& Scratch, int32 Node) const;
    const float* Input(const FVoxelDensityScratch& Scratch, int32 Node, int32 InputIndex) const;

    FVoxelDensityGraph Graph;
    // Per graph node: its buffer's index among compiled nodes, or INDEX_NONE.
    TArray<int32> Slots;
    // Compiled nodes in evaluation order.
    TArray<int32> Order;
    // Per graph node: whether its value can change with y.
    TArray<bool> NodeReadsY;
    TArray<int32> FlatOrder, VaryingOrder;
    // Per graph noise, created only when a compiled node reads it.
    TArray<FVoxelNormalNoise> Noises;
    // Per graph spline that a compiled Spline node uses as its root.
    TMap<int32, FCompiledSpline> Splines;
    // Per OldBlendedNoise node.
    TMap<int32, FVoxelBlendedNoise> BlendedNoises;
    // Per Interpolated (its input) and FindTopSurface (its density) node: an evaluator of that inner function, and its
    // index in the scratch's Nested list.
    TMap<int32, TSharedPtr<const FVoxelDensityEvaluator>> NestedEvaluators;
    TMap<int32, int32> NestedIndices;
    // Lazy interval_select functions, by their node: functions whose whole subtree nothing else reads. They are not
    // compiled here; the select evaluates each only at the points that pick it, through a nested evaluator (in
    // NestedEvaluators under the function's node), and bounds it by a bound over every y, found at compile.
    TMap<int32, FVoxelDensityBounds> LazyBounds;
    void FindLazyFunctions(TArray<bool>& Needed, TConstArrayView<int32> Roots, uint64 WorldSeed);
    // Lazy range_choice, by its node: a branch's private nodes (read only by that branch, and none of them interpolated
    // or find_top_surface) run only at the points that pick it, in a nested scratch, when they include costly work.
    // Exact: every op is per point, so a subset of points gets the same values.
    struct FLazyChoice
    {
        // Per branch (when in range, otherwise): its private nodes in order, and the other nodes they read.
        TArray<int32> Nodes[2], Frontier[2];
    };
    TMap<int32, FLazyChoice> LazyChoices;
    // Per graph node: the lazy range_choice that evaluates it, or INDEX_NONE.
    TArray<int32> Owners;
    void FindLazyChoices(TConstArrayView<int32> Roots);
    void EvaluateLazyChoice(
        int32 Node, const double* X, const double* Y, const double* Z, FVoxelDensityScratch& Scratch) const;
    void EvaluateLazySelect(
        int32 Node, const double* X, const double* Y, const double* Z, FVoxelDensityScratch& Scratch) const;
};
