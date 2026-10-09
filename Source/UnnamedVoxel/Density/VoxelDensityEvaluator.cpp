#include "Density/VoxelDensityEvaluator.h"

namespace
{
    // The ops whose meaning the 26.3 jar confirmed by black-box sampling (see Docs/VoxelWorld.md, Worldgen data), plus
    // the blend ops and beardifier, which stay neutral until structures blend terrain in phase 7.
    bool DensityEvaluatorRuns(EVoxelDensityOp Op)
    {
        switch (Op)
        {
        case EVoxelDensityOp::Interpolated:
        case EVoxelDensityOp::FindTopSurface:
        case EVoxelDensityOp::OldBlendedNoise:
        case EVoxelDensityOp::IntervalSelect:
            return false;
        default:
            return Op < EVoxelDensityOp::Count;
        }
    }
} // namespace

bool FVoxelDensityEvaluator::Compile(
    const FVoxelDensityGraph& InGraph, uint64 WorldSeed, TConstArrayView<int32> Roots, FString* OutError)
{
    auto Fail = [OutError](const FString& Reason)
    {
        if (OutError) *OutError = Reason;
        return false;
    };

    Graph = InGraph;
    Slots.Reset();
    Order.Reset();
    Noises.Reset();
    Splines.Reset();
    FString GraphError;
    if (!Graph.Validate(&GraphError)) return Fail(TEXT("invalid graph: ") + GraphError);

    TArray<bool> Needed;
    Needed.Init(false, Graph.Nodes.Num());
    for (const int32 Root : Roots)
    {
        if (!Graph.Nodes.IsValidIndex(Root)) return Fail(FString::Printf(TEXT("root %d is not a node"), Root));
        Needed[Root] = true;
    }

    // Inputs always come before the node that reads them, so one pass from the end marks everything the roots read.
    for (int32 Node = Graph.Nodes.Num() - 1; Node >= 0; --Node)
    {
        if (!Needed[Node]) continue;
        const FVoxelDensityNode& Data = Graph.Nodes[Node];
        if (!DensityEvaluatorRuns(Data.Op))
            return Fail(FString::Printf(
                TEXT("node %d: op %s is not evaluated yet"), Node, FVoxelDensityGraph::GetOpName(Data.Op)));
        for (const int32 Child : Data.Inputs)
            if (Child != INDEX_NONE) Needed[Child] = true;
        if (Data.Op == EVoxelDensityOp::Spline)
        {
            FCompiledSpline& Compiled = Splines.FindOrAdd(Data.Ref);
            if (!Compiled.Spline.IsValid())
            {
                FString SplineError;
                if (!Graph.BuildSpline(Data.Ref, Compiled.Spline, Compiled.Coordinates, &SplineError))
                    return Fail(FString::Printf(TEXT("node %d: %s"), Node, *SplineError));
            }
            for (const int32 Coordinate : Compiled.Coordinates)
                Needed[Coordinate] = true;
        }
    }

    Noises.SetNum(Graph.Noises.Num());
    Slots.Init(INDEX_NONE, Graph.Nodes.Num());
    for (int32 Node = 0; Node < Graph.Nodes.Num(); ++Node)
    {
        if (!Needed[Node]) continue;
        Slots[Node] = Order.Num();
        Order.Add(Node);
        const FVoxelDensityNode& Data = Graph.Nodes[Node];
        const bool bReadsNoise = Data.Op == EVoxelDensityOp::Noise || Data.Op == EVoxelDensityOp::ShiftA ||
            Data.Op == EVoxelDensityOp::ShiftB;
        if (bReadsNoise && Noises[Data.Ref].GetOctaveCount() == 0)
            Noises[Data.Ref] = FVoxelNormalNoise(WorldSeed, Graph.Noises[Data.Ref]);
    }
    return true;
}

float* FVoxelDensityEvaluator::Buffer(FVoxelDensityScratch& Scratch, int32 Node) const
{
    return Scratch.Values.GetData() + Slots[Node] * Scratch.Count;
}

const float* FVoxelDensityEvaluator::Input(const FVoxelDensityScratch& Scratch, int32 Node, int32 InputIndex) const
{
    const int32 Child = Graph.Nodes[Node].Inputs[InputIndex];
    return Child == INDEX_NONE ? nullptr : Scratch.Values.GetData() + Slots[Child] * Scratch.Count;
}

TConstArrayView<float> FVoxelDensityEvaluator::GetValues(const FVoxelDensityScratch& Scratch, int32 Node) const
{
    check(Slots.IsValidIndex(Node) && Slots[Node] != INDEX_NONE);
    return TConstArrayView<float>(Scratch.Values.GetData() + Slots[Node] * Scratch.Count, Scratch.Count);
}

void FVoxelDensityEvaluator::Evaluate(
    const double* X, const double* Y, const double* Z, int32 Count, FVoxelDensityScratch& Scratch) const
{
    check(IsCompiled());
    Scratch.Count = Count;
    // Every compiled node's buffer is written before any node reads it, so a reused scratch's old values never show.
    if (Scratch.Values.Num() != Order.Num() * Count) Scratch.Values.Init(0, Order.Num() * Count);
    for (const int32 Node : Order)
        EvaluateNode(Node, X, Y, Z, Scratch);
}

void FVoxelDensityEvaluator::EvaluateNode(
    int32 Node, const double* X, const double* Y, const double* Z, FVoxelDensityScratch& Scratch) const
{
    const FVoxelDensityNode& Data = Graph.Nodes[Node];
    const int32 Count = Scratch.Count;
    float* Out = Buffer(Scratch, Node);
    const float* A = Input(Scratch, Node, 0);
    const float* B = Input(Scratch, Node, 1);
    const float* C = Input(Scratch, Node, 2);

    switch (Data.Op)
    {
    case EVoxelDensityOp::Constant:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = float(Data.Params[0]);
        break;
    case EVoxelDensityOp::Add:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = A[I] + B[I];
        break;
    case EVoxelDensityOp::Sub:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = A[I] - B[I];
        break;
    case EVoxelDensityOp::Mul:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = A[I] * B[I];
        break;
    case EVoxelDensityOp::Div:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = A[I] / B[I];
        break;
    case EVoxelDensityOp::Min:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = FMath::Min(A[I], B[I]);
        break;
    case EVoxelDensityOp::Max:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = FMath::Max(A[I], B[I]);
        break;
    case EVoxelDensityOp::Abs:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = FMath::Abs(A[I]);
        break;
    case EVoxelDensityOp::Negate:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = -A[I];
        break;
    case EVoxelDensityOp::Square:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = A[I] * A[I];
        break;
    case EVoxelDensityOp::Cube:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = A[I] * A[I] * A[I];
        break;
    case EVoxelDensityOp::HalfNegative:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = A[I] > 0 ? A[I] : A[I] * .5f;
        break;
    case EVoxelDensityOp::QuarterNegative:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = A[I] > 0 ? A[I] : A[I] * .25f;
        break;
    case EVoxelDensityOp::Squeeze:
        // A soft clamp: odd, flat at +-1 and steepest at 0 (squeeze(2) = 11/24 in the jar).
        for (int32 I = 0; I < Count; ++I)
        {
            const float V = FMath::Clamp(A[I], -1.f, 1.f);
            Out[I] = V * .5f - V * V * V / 24.f;
        }
        break;
    case EVoxelDensityOp::Cache:
    case EVoxelDensityOp::BlendDensity:
        FMemory::Memcpy(Out, A, Count * sizeof(float));
        break;
    case EVoxelDensityOp::Clamp:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = FMath::Clamp(A[I], float(Data.Params[0]), float(Data.Params[1]));
        break;
    case EVoxelDensityOp::Gradient:
    {
        // Linear between the two coordinates along the axis and flat beyond them.
        const double* Axis = Data.Axis == 0 ? X : (Data.Axis == 1 ? Y : Z);
        const double From = Data.Params[0], To = Data.Params[2];
        for (int32 I = 0; I < Count; ++I)
        {
            // Equal coordinates make a step at them rather than a division by zero.
            const double Alpha =
                To == From ? (Axis[I] >= To ? 1. : 0.) : FMath::Clamp((Axis[I] - From) / (To - From), 0., 1.);
            Out[I] = float(Data.Params[1] + Alpha * (Data.Params[3] - Data.Params[1]));
        }
        break;
    }
    case EVoxelDensityOp::Lerp:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = B[I] + A[I] * (C[I] - B[I]);
        break;
    case EVoxelDensityOp::RangeChoice:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = A[I] >= float(Data.Params[0]) && A[I] < float(Data.Params[1]) ? B[I] : C[I];
        break;
    case EVoxelDensityOp::Noise:
    {
        const FVoxelNormalNoise& Noise = Noises[Data.Ref];
        const double ScaleXZ = Data.Params[0], ScaleY = Data.Params[1];
        for (int32 I = 0; I < Count; ++I)
            Out[I] = Noise.Sample(X[I] * ScaleXZ + (A ? A[I] : 0.f), Y[I] * ScaleY, Z[I] * ScaleXZ + (B ? B[I] : 0.f));
        break;
    }
    case EVoxelDensityOp::ShiftA:
    {
        const FVoxelNormalNoise& Noise = Noises[Data.Ref];
        for (int32 I = 0; I < Count; ++I)
            Out[I] = 4 * Noise.Sample(X[I] * .25, 0, Z[I] * .25);
        break;
    }
    case EVoxelDensityOp::ShiftB:
    {
        const FVoxelNormalNoise& Noise = Noises[Data.Ref];
        for (int32 I = 0; I < Count; ++I)
            Out[I] = 4 * Noise.Sample(Z[I] * .25, X[I] * .25, 0);
        break;
    }
    case EVoxelDensityOp::Spline:
    {
        const FCompiledSpline& Compiled = Splines.FindChecked(Data.Ref);
        const int32 Inputs = Compiled.Coordinates.Num();
        Scratch.SplineInputs.SetNumUninitialized(Inputs);
        for (int32 I = 0; I < Count; ++I)
        {
            for (int32 Coordinate = 0; Coordinate < Inputs; ++Coordinate)
                Scratch.SplineInputs[Coordinate] = Scratch.Values[Slots[Compiled.Coordinates[Coordinate]] * Count + I];
            Out[I] = Compiled.Spline.Evaluate(Scratch.SplineInputs);
        }
        break;
    }
    case EVoxelDensityOp::BlendAlpha:
        for (int32 I = 0; I < Count; ++I)
            Out[I] = 1;
        break;
    case EVoxelDensityOp::BlendOffset:
    case EVoxelDensityOp::Beardifier:
        FMemory::Memzero(Out, Count * sizeof(float));
        break;
    default:
        checkNoEntry();
    }
}
