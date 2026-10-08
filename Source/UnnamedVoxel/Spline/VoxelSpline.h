#pragma once

#include "CoreMinimal.h"

// A cubic Hermite spline over one coordinate whose point values may themselves be splines over other coordinates: the
// reference's terrain splines (offset, factor and jaggedness from continentalness, erosion and peaks-and-valleys).
// Between two points the curve is the cubic with their values and derivatives; beyond the ends it continues linearly
// with the end derivative. Baked into flat arrays (FVoxelSplineBuilder), so evaluating allocates nothing, reads no
// UObject and is safe on any thread.
class UNNAMEDVOXEL_API FVoxelSpline
{
public:
    struct FPoint
    {
        float Location = 0;
        // The point's value when Child is INDEX_NONE.
        float Value = 0;
        float Derivative = 0;
        // A nested node evaluated at the same coordinates, or INDEX_NONE.
        int32 Child = INDEX_NONE;
    };

    struct FNode
    {
        // Which entry of the coordinate array this node reads.
        int32 Coordinate = 0;
        int32 FirstPoint = 0;
        int32 PointCount = 0;
    };

    bool IsValid() const { return !Nodes.IsEmpty(); }
    int32 GetCoordinateCount() const { return CoordinateCount; }
    int32 GetNodeCount() const { return Nodes.Num(); }
    TConstArrayView<FNode> GetNodes() const { return Nodes; }
    TConstArrayView<FPoint> GetPoints() const { return Points; }

    // Coordinates holds at least GetCoordinateCount() values; nodes read them by index. An empty spline returns 0.
    float Evaluate(TConstArrayView<float> Coordinates) const;

private:
    friend class FVoxelSplineBuilder;
    float EvaluateNode(int32 NodeIndex, TConstArrayView<float> Coordinates) const;

    // Node 0 is the root; every node's points are contiguous and sorted by location.
    TArray<FNode> Nodes;
    TArray<FPoint> Points;
    int32 CoordinateCount = 0;
};

// Assembles a spline tree, then bakes it: add nodes, give each its points, Build from the root.
class UNNAMEDVOXEL_API FVoxelSplineBuilder
{
public:
    // A node over the given coordinate index; returns its handle.
    int32 AddNode(int32 Coordinate);
    // A point with a constant value.
    void AddPoint(int32 Node, float Location, float Value, float Derivative = 0);
    // A point whose value is another node, evaluated at the same coordinates.
    void AddChildPoint(int32 Node, float Location, int32 Child, float Derivative = 0);

    // Bakes the tree reachable from Root, root first, into OutSpline. Fails, with a reason, when a node has no points,
    // its locations do not strictly increase, a coordinate is outside [0, CoordinateCount), a child is unknown, or a
    // node contains itself.
    bool Build(int32 Root, int32 CoordinateCount, FVoxelSpline& OutSpline, FString* OutError = nullptr) const;

private:
    struct FBuilderNode
    {
        int32 Coordinate = 0;
        TArray<FVoxelSpline::FPoint> Points;
    };
    TArray<FBuilderNode> Nodes;
};
