#include "Density/VoxelDensityGraph.h"

#include "Spline/VoxelSpline.h"

namespace
{
    // Required inputs per op. Noise's two shift inputs are optional and checked apart.
    int32 GetDensityOpInputCount(EVoxelDensityOp Op)
    {
        switch (Op)
        {
        case EVoxelDensityOp::Add:
        case EVoxelDensityOp::Sub:
        case EVoxelDensityOp::Mul:
        case EVoxelDensityOp::Div:
        case EVoxelDensityOp::Min:
        case EVoxelDensityOp::Max:
        case EVoxelDensityOp::FindTopSurface:
            return 2;
        case EVoxelDensityOp::Lerp:
        case EVoxelDensityOp::RangeChoice:
            return 3;
        case EVoxelDensityOp::Abs:
        case EVoxelDensityOp::Negate:
        case EVoxelDensityOp::Square:
        case EVoxelDensityOp::Cube:
        case EVoxelDensityOp::HalfNegative:
        case EVoxelDensityOp::QuarterNegative:
        case EVoxelDensityOp::Squeeze:
        case EVoxelDensityOp::Cache:
        case EVoxelDensityOp::BlendDensity:
        case EVoxelDensityOp::Clamp:
        case EVoxelDensityOp::IntervalSelect:
        case EVoxelDensityOp::Interpolated:
            return 1;
        default:
            return 0;
        }
    }

    bool FailDensityGraph(FString* OutError, FString Message)
    {
        if (OutError)
        {
            *OutError = MoveTemp(Message);
        }
        return false;
    }
} // namespace

int32 FVoxelDensityGraph::FindFunction(FStringView Name) const
{
    const FVoxelDensityFunction* Found = Functions.FindByPredicate([Name](const FVoxelDensityFunction& Function)
        { return FStringView(Function.Name).Equals(Name, ESearchCase::CaseSensitive); });
    return Found ? Found->Node : INDEX_NONE;
}

int32 FVoxelDensityGraph::FindNoise(FStringView Name) const
{
    return Noises.IndexOfByPredicate([Name](const FVoxelDensityNoise& Noise)
        { return FStringView(Noise.Name).Equals(Name, ESearchCase::CaseSensitive); });
}

bool FVoxelDensityGraph::Validate(FString* OutError) const
{
    if (ListValues.Num() != ListNodes.Num())
    {
        return FailDensityGraph(OutError, TEXT("interval lists differ in length"));
    }
    for (int32 SplineIndex = 0; SplineIndex < Splines.Num(); ++SplineIndex)
    {
        const FVoxelDensitySpline& Spline = Splines[SplineIndex];
        if (!Nodes.IsValidIndex(Spline.Coordinate))
        {
            return FailDensityGraph(OutError,
                FString::Printf(TEXT("spline %d: coordinate %d is not a node"), SplineIndex, Spline.Coordinate));
        }
        if (Spline.PointCount < 1 || Spline.FirstPoint < 0 ||
            Spline.FirstPoint + Spline.PointCount > SplinePoints.Num())
        {
            return FailDensityGraph(OutError,
                FString::Printf(TEXT("spline %d: points %d+%d are out of range"), SplineIndex, Spline.FirstPoint,
                    Spline.PointCount));
        }
        for (int32 PointIndex = 0; PointIndex < Spline.PointCount; ++PointIndex)
        {
            const int32 Child = SplinePoints[Spline.FirstPoint + PointIndex].Child;
            if (Child != INDEX_NONE && (Child < 0 || Child >= SplineIndex))
            {
                return FailDensityGraph(OutError,
                    FString::Printf(TEXT("spline %d: nested spline %d is not an earlier "
                                         "spline"),
                        SplineIndex, Child));
            }
        }
    }

    for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
    {
        const FVoxelDensityNode& Node = Nodes[NodeIndex];
        if (Node.Op >= EVoxelDensityOp::Count)
        {
            return FailDensityGraph(
                OutError, FString::Printf(TEXT("node %d: unknown op %d"), NodeIndex, int32(Node.Op)));
        }
        const TCHAR* Name = GetOpName(Node.Op);
        const int32 Required = GetDensityOpInputCount(Node.Op);
        for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Node.Inputs); ++Slot)
        {
            const int32 Input = Node.Inputs[Slot];
            const bool bOptional = Node.Op == EVoxelDensityOp::Noise && Slot < 2;
            if (Slot < Required || (bOptional && Input != INDEX_NONE))
            {
                if (Input < 0 || Input >= NodeIndex)
                {
                    return FailDensityGraph(OutError,
                        FString::Printf(TEXT("node %d (%s): input %d is %d, not an "
                                             "earlier node"),
                            NodeIndex, Name, Slot, Input));
                }
            }
            else if (Input != INDEX_NONE)
            {
                return FailDensityGraph(
                    OutError, FString::Printf(TEXT("node %d (%s): unexpected input %d"), NodeIndex, Name, Slot));
            }
        }

        switch (Node.Op)
        {
        case EVoxelDensityOp::Noise:
        case EVoxelDensityOp::ShiftA:
        case EVoxelDensityOp::ShiftB:
            if (!Noises.IsValidIndex(Node.Ref))
            {
                return FailDensityGraph(
                    OutError, FString::Printf(TEXT("node %d (%s): noise %d is unknown"), NodeIndex, Name, Node.Ref));
            }
            break;
        case EVoxelDensityOp::Spline:
        {
            if (!Splines.IsValidIndex(Node.Ref))
            {
                return FailDensityGraph(
                    OutError, FString::Printf(TEXT("node %d (spline): spline %d is unknown"), NodeIndex, Node.Ref));
            }
            // Every coordinate of the spline and its nested splines comes before the node.
            TArray<int32, TInlineAllocator<16>> Pending = {Node.Ref};
            while (!Pending.IsEmpty())
            {
                const FVoxelDensitySpline& Spline = Splines[Pending.Pop()];
                if (Spline.Coordinate >= NodeIndex)
                {
                    return FailDensityGraph(OutError,
                        FString::Printf(TEXT("node %d (spline): coordinate %d is not an "
                                             "earlier node"),
                            NodeIndex, Spline.Coordinate));
                }
                for (int32 PointIndex = 0; PointIndex < Spline.PointCount; ++PointIndex)
                {
                    const int32 Child = SplinePoints[Spline.FirstPoint + PointIndex].Child;
                    if (Child != INDEX_NONE)
                    {
                        Pending.Add(Child);
                    }
                }
            }
            break;
        }
        case EVoxelDensityOp::IntervalSelect:
            if (Node.ListCount < 1 || Node.Ref < 0 || Node.Ref + Node.ListCount > ListNodes.Num())
            {
                return FailDensityGraph(OutError,
                    FString::Printf(TEXT("node %d (interval_select): list %d+%d is out "
                                         "of range"),
                        NodeIndex, Node.Ref, Node.ListCount));
            }
            for (int32 Entry = 0; Entry < Node.ListCount; ++Entry)
            {
                const int32 Function = ListNodes[Node.Ref + Entry];
                if (Function < 0 || Function >= NodeIndex)
                {
                    return FailDensityGraph(OutError,
                        FString::Printf(TEXT("node %d (interval_select): function %d is "
                                             "%d, not an earlier node"),
                            NodeIndex, Entry, Function));
                }
                if (Entry > 0 && Entry < Node.ListCount - 1 &&
                    !(ListValues[Node.Ref + Entry] > ListValues[Node.Ref + Entry - 1]))
                {
                    return FailDensityGraph(OutError,
                        FString::Printf(TEXT("node %d (interval_select): thresholds do "
                                             "not increase"),
                            NodeIndex));
                }
            }
            break;
        case EVoxelDensityOp::Gradient:
            if (Node.Axis > 2)
            {
                return FailDensityGraph(
                    OutError, FString::Printf(TEXT("node %d (gradient): axis %d"), NodeIndex, int32(Node.Axis)));
            }
            break;
        default:
            break;
        }
    }

    TSet<FString> Seen;
    for (const FVoxelDensityNoise& Noise : Noises)
    {
        bool bDuplicate = false;
        Seen.Add(Noise.Name, &bDuplicate);
        if (bDuplicate || Noise.AmplitudeModifiers.IsEmpty())
        {
            return FailDensityGraph(
                OutError, FString::Printf(TEXT("noise %s: duplicate or without octaves"), *Noise.Name));
        }
    }
    Seen.Reset();
    for (const FVoxelDensityFunction& Function : Functions)
    {
        bool bDuplicate = false;
        Seen.Add(Function.Name, &bDuplicate);
        if (bDuplicate || !Nodes.IsValidIndex(Function.Node))
        {
            return FailDensityGraph(
                OutError, FString::Printf(TEXT("function %s: duplicate or without a node"), *Function.Name));
        }
    }
    return true;
}

bool FVoxelDensityGraph::BuildSpline(
    int32 Index, FVoxelSpline& Out, TArray<int32>& OutCoordinates, FString* OutError) const
{
    OutCoordinates.Reset();
    if (!Splines.IsValidIndex(Index))
    {
        return FailDensityGraph(OutError, FString::Printf(TEXT("spline %d is unknown"), Index));
    }

    FVoxelSplineBuilder Builder;
    TMap<int32, int32> Handles;
    bool bValid = true;
    // Depth first with points in order, so coordinates take their slots in first use.
    const auto AddSpline = [&](const auto& Self, int32 SplineIndex) -> int32
    {
        if (const int32* Found = Handles.Find(SplineIndex))
        {
            return *Found;
        }
        const FVoxelDensitySpline& Spline = Splines[SplineIndex];
        const int32 Handle = Builder.AddNode(OutCoordinates.AddUnique(Spline.Coordinate));
        Handles.Add(SplineIndex, Handle);
        for (int32 PointIndex = 0; PointIndex < Spline.PointCount; ++PointIndex)
        {
            const FVoxelDensitySplinePoint& Point = SplinePoints[Spline.FirstPoint + PointIndex];
            if (Point.Child == INDEX_NONE)
            {
                Builder.AddPoint(Handle, float(Point.Location), float(Point.Value), float(Point.Derivative));
            }
            else if (Splines.IsValidIndex(Point.Child))
            {
                Builder.AddChildPoint(Handle, float(Point.Location), Self(Self, Point.Child), float(Point.Derivative));
            }
            else
            {
                bValid = false;
            }
        }
        return Handle;
    };
    const int32 Root = AddSpline(AddSpline, Index);
    if (!bValid)
    {
        return FailDensityGraph(OutError, FString::Printf(TEXT("spline %d nests an unknown spline"), Index));
    }
    return Builder.Build(Root, OutCoordinates.Num(), Out, OutError);
}

const TCHAR* FVoxelDensityGraph::GetOpName(EVoxelDensityOp Op)
{
    static const TCHAR* const Names[] = {
        TEXT("constant"),
        TEXT("add"),
        TEXT("sub"),
        TEXT("mul"),
        TEXT("div"),
        TEXT("min"),
        TEXT("max"),
        TEXT("abs"),
        TEXT("negate"),
        TEXT("square"),
        TEXT("cube"),
        TEXT("half_negative"),
        TEXT("quarter_negative"),
        TEXT("squeeze"),
        TEXT("cache"),
        TEXT("blend_density"),
        TEXT("clamp"),
        TEXT("gradient"),
        TEXT("lerp"),
        TEXT("range_choice"),
        TEXT("interval_select"),
        TEXT("noise"),
        TEXT("shift_a"),
        TEXT("shift_b"),
        TEXT("old_blended_noise"),
        TEXT("spline"),
        TEXT("interpolated"),
        TEXT("find_top_surface"),
        TEXT("blend_alpha"),
        TEXT("blend_offset"),
        TEXT("beardifier"),
    };
    static_assert(UE_ARRAY_COUNT(Names) == int32(EVoxelDensityOp::Count), "one name per op");
    return Op < EVoxelDensityOp::Count ? Names[int32(Op)] : TEXT("?");
}
