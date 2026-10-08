#include "Spline/VoxelSpline.h"

float FVoxelSpline::Evaluate(TConstArrayView<float> Coordinates) const
{
    if (Nodes.IsEmpty()) return 0;
    check(Coordinates.Num() >= CoordinateCount);
    return EvaluateNode(0, Coordinates);
}

float FVoxelSpline::EvaluateNode(int32 NodeIndex, TConstArrayView<float> Coordinates) const
{
    const FNode& Node = Nodes[NodeIndex];
    const FPoint* Point = &Points[Node.FirstPoint];
    const int32 Last = Node.PointCount - 1;
    const float X = Coordinates[Node.Coordinate];
    auto ValueOf = [this, Coordinates](const FPoint& P)
    { return P.Child == INDEX_NONE ? P.Value : EvaluateNode(P.Child, Coordinates); };

    // Beyond the ends: a straight line along the end derivative.
    if (X <= Point[0].Location) return ValueOf(Point[0]) + Point[0].Derivative * (X - Point[0].Location);
    if (X >= Point[Last].Location) return ValueOf(Point[Last]) + Point[Last].Derivative * (X - Point[Last].Location);

    // The segment holding X; nodes have a handful of points, so a scan beats a binary search.
    int32 Upper = 1;
    while (Point[Upper].Location <= X)
        ++Upper;
    const FPoint& A = Point[Upper - 1];
    const FPoint& B = Point[Upper];
    const float Width = B.Location - A.Location;
    const float T = (X - A.Location) / Width, T2 = T * T, T3 = T2 * T;
    // The cubic Hermite basis: values at both ends, derivatives scaled by the segment width.
    return (2 * T3 - 3 * T2 + 1) * ValueOf(A) + (T3 - 2 * T2 + T) * Width * A.Derivative +
        (3 * T2 - 2 * T3) * ValueOf(B) + (T3 - T2) * Width * B.Derivative;
}

int32 FVoxelSplineBuilder::AddNode(int32 Coordinate)
{
    FBuilderNode& Node = Nodes.AddDefaulted_GetRef();
    Node.Coordinate = Coordinate;
    return Nodes.Num() - 1;
}

void FVoxelSplineBuilder::AddPoint(int32 Node, float Location, float Value, float Derivative)
{
    Nodes[Node].Points.Add({Location, Value, Derivative, INDEX_NONE});
}

void FVoxelSplineBuilder::AddChildPoint(int32 Node, float Location, int32 Child, float Derivative)
{
    Nodes[Node].Points.Add({Location, 0, Derivative, Child});
}

bool FVoxelSplineBuilder::Build(int32 Root, int32 CoordinateCount, FVoxelSpline& OutSpline, FString* OutError) const
{
    auto Fail = [OutError](FString Reason)
    {
        if (OutError) *OutError = MoveTemp(Reason);
        return false;
    };
    if (!Nodes.IsValidIndex(Root)) return Fail(FString::Printf(TEXT("Unknown root node %d"), Root));

    // Depth-first from the root: assign baked indices in visiting order and reject cycles (a node reached again while
    // it is still on the path).
    TArray<int32> BakedIndex, Order;
    TArray<uint8> State; // 0 unvisited, 1 on the current path, 2 done
    BakedIndex.Init(INDEX_NONE, Nodes.Num());
    State.Init(0, Nodes.Num());
    TFunction<bool(int32)> Visit = [&](int32 Index) -> bool
    {
        const FBuilderNode& Node = Nodes[Index];
        if (Node.Points.IsEmpty()) return Fail(FString::Printf(TEXT("Node %d has no points"), Index));
        if (Node.Coordinate < 0 || Node.Coordinate >= CoordinateCount)
            return Fail(
                FString::Printf(TEXT("Node %d reads coordinate %d of %d"), Index, Node.Coordinate, CoordinateCount));
        State[Index] = 1;
        BakedIndex[Index] = Order.Add(Index);
        for (int32 PointIndex = 0; PointIndex < Node.Points.Num(); ++PointIndex)
        {
            const FVoxelSpline::FPoint& Point = Node.Points[PointIndex];
            if (PointIndex > 0 && !(Point.Location > Node.Points[PointIndex - 1].Location))
                return Fail(FString::Printf(TEXT("Node %d: locations must strictly increase (point %d at %g)"), Index,
                    PointIndex, Point.Location));
            if (Point.Child == INDEX_NONE) continue;
            if (!Nodes.IsValidIndex(Point.Child))
                return Fail(
                    FString::Printf(TEXT("Node %d point %d: unknown child %d"), Index, PointIndex, Point.Child));
            if (State[Point.Child] == 1)
                return Fail(FString::Printf(TEXT("Node %d contains itself through node %d"), Point.Child, Index));
            if (State[Point.Child] == 0 && !Visit(Point.Child)) return false;
        }
        State[Index] = 2;
        return true;
    };
    if (!Visit(Root)) return false;

    FVoxelSpline Spline;
    Spline.CoordinateCount = CoordinateCount;
    for (const int32 Index : Order)
    {
        const FBuilderNode& Node = Nodes[Index];
        Spline.Nodes.Add({Node.Coordinate, Spline.Points.Num(), Node.Points.Num()});
        for (FVoxelSpline::FPoint Point : Node.Points)
        {
            if (Point.Child != INDEX_NONE) Point.Child = BakedIndex[Point.Child];
            Spline.Points.Add(Point);
        }
    }
    OutSpline = MoveTemp(Spline);
    return true;
}
