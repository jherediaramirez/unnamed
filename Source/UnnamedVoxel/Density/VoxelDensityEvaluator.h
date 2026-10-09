#pragma once

#include "CoreMinimal.h"
#include "Density/VoxelDensityGraph.h"
#include "Noise/VoxelNormalNoise.h"
#include "Spline/VoxelSpline.h"

// Per-thread working memory for FVoxelDensityEvaluator: one float buffer per compiled node. Reused between calls, so a
// worker allocates only when a batch grows.
struct FVoxelDensityScratch
{
    TArray<float> Values;
    TArray<float> SplineInputs;
    int32 Count = 0;
};

// Evaluates part of a density graph for one world seed over batches of points, one node at a time in the graph's
// order (inputs always come first), so each op's loop runs over the whole batch. Only the nodes the requested roots
// read are compiled. Positions use the reference's axes: x, y up, z. Values are 32-bit floats, as 26.3 evaluates
// density.
// Immutable after Compile; any thread may evaluate with its own scratch, and equal inputs give bit-identical values.
class UNNAMEDVOXEL_API FVoxelDensityEvaluator
{
public:
    // Fails, with a reason, when the graph is invalid or a root reads an op this evaluator does not run yet
    // (interpolated, find_top_surface, old_blended_noise and interval_select come with the 3D density).
    bool Compile(
        const FVoxelDensityGraph& InGraph, uint64 WorldSeed, TConstArrayView<int32> Roots, FString* OutError = nullptr);

    bool IsCompiled() const { return !Slots.IsEmpty(); }

    // Evaluates every compiled node at Count points.
    void Evaluate(const double* X, const double* Y, const double* Z, int32 Count, FVoxelDensityScratch& Scratch) const;

    // A compiled node's values from the last Evaluate with this scratch.
    TConstArrayView<float> GetValues(const FVoxelDensityScratch& Scratch, int32 Node) const;

    const FVoxelDensityGraph& GetGraph() const { return Graph; }

private:
    struct FCompiledSpline
    {
        FVoxelSpline Spline;
        TArray<int32> Coordinates;
    };

    void EvaluateNode(
        int32 Node, const double* X, const double* Y, const double* Z, FVoxelDensityScratch& Scratch) const;
    float* Buffer(FVoxelDensityScratch& Scratch, int32 Node) const;
    const float* Input(const FVoxelDensityScratch& Scratch, int32 Node, int32 InputIndex) const;

    FVoxelDensityGraph Graph;
    // Per graph node: its buffer's index among compiled nodes, or INDEX_NONE.
    TArray<int32> Slots;
    // Compiled nodes in evaluation order.
    TArray<int32> Order;
    // Per graph noise, created only when a compiled node reads it.
    TArray<FVoxelNormalNoise> Noises;
    // Per graph spline that a compiled Spline node uses as its root.
    TMap<int32, FCompiledSpline> Splines;
};
