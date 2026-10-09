#include "Density/VoxelDensityEvaluator.h"

namespace
{
    // Every op whose meaning the 26.3 jar confirmed by black-box sampling (see Docs/VoxelWorld.md, Worldgen data), plus
    // the blend ops and beardifier, which stay neutral until structures blend terrain in phase 7.
    bool DensityEvaluatorRuns(EVoxelDensityOp Op)
    {
        return Op < EVoxelDensityOp::Count;
    }

    // Bounds arithmetic. Infinite and NaN results widen to "anything".
    FVoxelDensityBounds DensityBoundsOf(float A, float B)
    {
        if (FMath::IsNaN(A) || FMath::IsNaN(B)) return {};
        return {FMath::Min(A, B), FMath::Max(A, B)};
    }

    FVoxelDensityBounds DensityBoundsHull(const FVoxelDensityBounds& A, const FVoxelDensityBounds& B)
    {
        return {FMath::Min(A.Min, B.Min), FMath::Max(A.Max, B.Max)};
    }

    FVoxelDensityBounds DensityBoundsMul(const FVoxelDensityBounds& A, const FVoxelDensityBounds& B)
    {
        const float P[4] = {A.Min * B.Min, A.Min * B.Max, A.Max * B.Min, A.Max * B.Max};
        FVoxelDensityBounds Out{P[0], P[0]};
        for (const float Value : P)
        {
            if (FMath::IsNaN(Value)) return {};
            Out.Min = FMath::Min(Out.Min, Value);
            Out.Max = FMath::Max(Out.Max, Value);
        }
        return Out;
    }

    // Applies a non-decreasing function to both ends.
    template <typename FunctionType>
    FVoxelDensityBounds DensityBoundsMonotone(const FVoxelDensityBounds& A, FunctionType&& Function)
    {
        return DensityBoundsOf(Function(A.Min), Function(A.Max));
    }

    // Rounding margin: float evaluation may round each op differently from the bounds' own float arithmetic.
    FVoxelDensityBounds DensityBoundsWiden(const FVoxelDensityBounds& A)
    {
        const auto Widen = [](float Value, float Sign)
        { return FMath::Abs(Value) >= UE_BIG_NUMBER ? Value : Value + Sign * (1e-5f + FMath::Abs(Value) * 1e-5f); };
        return {FMath::Max(Widen(A.Min, -1.f), -UE_BIG_NUMBER), FMath::Min(Widen(A.Max, 1.f), UE_BIG_NUMBER)};
    }

    float DensitySqueeze(float Value)
    {
        const float V = FMath::Clamp(Value, -1.f, 1.f);
        return V * .5f - V * V * V / 24.f;
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
    NodeReadsY.Reset();
    FlatOrder.Reset();
    VaryingOrder.Reset();
    Noises.Reset();
    Splines.Reset();
    BlendedNoises.Reset();
    NestedEvaluators.Reset();
    NestedIndices.Reset();
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
    TArray<int32> Reads;
    for (int32 Node = Graph.Nodes.Num() - 1; Node >= 0; --Node)
    {
        if (!Needed[Node]) continue;
        const FVoxelDensityNode& Data = Graph.Nodes[Node];
        if (!DensityEvaluatorRuns(Data.Op))
            return Fail(FString::Printf(
                TEXT("node %d: op %s is not evaluated yet"), Node, FVoxelDensityGraph::GetOpName(Data.Op)));
        if (Data.Op == EVoxelDensityOp::Spline)
        {
            FCompiledSpline& Compiled = Splines.FindOrAdd(Data.Ref);
            if (!Compiled.Spline.IsValid())
            {
                FString SplineError;
                if (!Graph.BuildSpline(Data.Ref, Compiled.Spline, Compiled.Coordinates, &SplineError))
                    return Fail(FString::Printf(TEXT("node %d: %s"), Node, *SplineError));
            }
        }
        if (Data.Op == EVoxelDensityOp::FindTopSurface && !(Data.Params[1] >= 1))
            return Fail(FString::Printf(TEXT("node %d: find_top_surface needs a cell height of at least 1"), Node));
        if (Data.Op == EVoxelDensityOp::Interpolated && !(Data.Params[0] >= 1 && Data.Params[1] >= 1))
            return Fail(FString::Printf(TEXT("node %d: interpolated needs cell sizes of at least 1"), Node));
        if (Data.Op == EVoxelDensityOp::Interpolated || Data.Op == EVoxelDensityOp::FindTopSurface)
        {
            // The inner function runs at other positions than the batch's, so it gets an evaluator of its own.
            TSharedPtr<FVoxelDensityEvaluator> Nested = MakeShared<FVoxelDensityEvaluator>();
            FString NestedError;
            if (!Nested->Compile(Graph, WorldSeed, {Data.Inputs[0]}, &NestedError))
                return Fail(FString::Printf(TEXT("node %d: %s"), Node, *NestedError));
            NestedIndices.Add(Node, NestedEvaluators.Num());
            NestedEvaluators.Add(Node, Nested);
        }
        // find_top_surface reads its density only through the nested evaluator; an interpolated input stays compiled
        // here too, so a column can evaluate it at the cell corners.
        Reads.Reset();
        if (Data.Op == EVoxelDensityOp::FindTopSurface)
            Reads.Add(Data.Inputs[1]);
        else
        {
            for (const int32 Child : Data.Inputs)
                if (Child != INDEX_NONE) Reads.Add(Child);
            if (Data.Op == EVoxelDensityOp::Spline) Reads.Append(Splines[Data.Ref].Coordinates);
            if (Data.Op == EVoxelDensityOp::IntervalSelect)
                for (int32 Entry = 0; Entry < Data.ListCount; ++Entry)
                    Reads.Add(Graph.ListNodes[Data.Ref + Entry]);
        }
        for (const int32 Child : Reads)
            Needed[Child] = true;
    }

    Noises.SetNum(Graph.Noises.Num());
    Slots.Init(INDEX_NONE, Graph.Nodes.Num());
    NodeReadsY.Init(false, Graph.Nodes.Num());
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
        if (Data.Op == EVoxelDensityOp::OldBlendedNoise)
            BlendedNoises.Add(
                Node, FVoxelBlendedNoise(WorldSeed, Data.Params[0], Data.Params[1], Data.Params[2], Data.Params[3]));

        // Whether the value can change with y: the sources are a y gradient, a noise read with a y scale and the
        // blended noise. find_top_surface ignores the sample's y; shift_a and shift_b read y = 0.
        bool bReadsY = false;
        switch (Data.Op)
        {
        case EVoxelDensityOp::Gradient:
            bReadsY = Data.Axis == 1;
            break;
        case EVoxelDensityOp::Noise:
            bReadsY = Data.Params[1] != 0;
            break;
        case EVoxelDensityOp::OldBlendedNoise:
            bReadsY = true;
            break;
        case EVoxelDensityOp::ShiftA:
        case EVoxelDensityOp::ShiftB:
            break;
        default:
            break;
        }
        GetReads(Node, Reads);
        if (Data.Op == EVoxelDensityOp::FindTopSurface) Reads = {Data.Inputs[1]};
        for (const int32 Child : Reads)
            bReadsY |= NodeReadsY[Child];
        NodeReadsY[Node] = bReadsY;
        (bReadsY ? VaryingOrder : FlatOrder).Add(Node);
    }
    return true;
}

void FVoxelDensityEvaluator::GetReads(int32 Node, TArray<int32>& Out) const
{
    Out.Reset();
    const FVoxelDensityNode& Data = Graph.Nodes[Node];
    // find_top_surface reads its density only inside its nested evaluator.
    if (Data.Op == EVoxelDensityOp::FindTopSurface)
    {
        Out.Add(Data.Inputs[1]);
        return;
    }
    for (const int32 Child : Data.Inputs)
        if (Child != INDEX_NONE) Out.Add(Child);
    if (Data.Op == EVoxelDensityOp::Spline)
        if (const FCompiledSpline* Compiled = Splines.Find(Data.Ref)) Out.Append(Compiled->Coordinates);
    if (Data.Op == EVoxelDensityOp::IntervalSelect)
        for (int32 Entry = 0; Entry < Data.ListCount; ++Entry)
            Out.Add(Graph.ListNodes[Data.Ref + Entry]);
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
    check(IsCompiledNode(Node));
    return TConstArrayView<float>(Scratch.Values.GetData() + Slots[Node] * Scratch.Count, Scratch.Count);
}

TArrayView<float> FVoxelDensityEvaluator::GetMutableValues(FVoxelDensityScratch& Scratch, int32 Node) const
{
    check(IsCompiledNode(Node));
    return TArrayView<float>(Buffer(Scratch, Node), Scratch.Count);
}

void FVoxelDensityEvaluator::Prepare(FVoxelDensityScratch& Scratch, int32 Count) const
{
    check(IsCompiled());
    Scratch.Count = Count;
    // Values are written before they are read (by Evaluate, or by the caller for EvaluateNodes' outside inputs).
    Scratch.Values.SetNumUninitialized(Order.Num() * Count, EAllowShrinking::No);
}

void FVoxelDensityEvaluator::Evaluate(
    const double* X, const double* Y, const double* Z, int32 Count, FVoxelDensityScratch& Scratch) const
{
    Prepare(Scratch, Count);
    for (const int32 Node : Order)
        EvaluateNode(Node, X, Y, Z, Scratch);
}

void FVoxelDensityEvaluator::EvaluateNodes(TConstArrayView<int32> Nodes, const double* X, const double* Y,
    const double* Z, FVoxelDensityScratch& Scratch) const
{
    for (const int32 Node : Nodes)
        EvaluateNode(Node, X, Y, Z, Scratch);
}

FVoxelDensityScratch& FVoxelDensityEvaluator::GetNested(FVoxelDensityScratch& Scratch, int32 Node) const
{
    const int32 Index = NestedIndices.FindChecked(Node);
    while (Scratch.Nested.Num() <= Index)
        Scratch.Nested.Add(new FVoxelDensityScratch());
    return Scratch.Nested[Index];
}

void FVoxelDensityEvaluator::EvaluateInterpolated(
    int32 Node, const double* X, const double* Y, const double* Z, FVoxelDensityScratch& Scratch) const
{
    // The input at the 8 corners of each point's cell, corners on multiples of the cell size, then trilinear weights.
    const FVoxelDensityNode& Data = Graph.Nodes[Node];
    const FVoxelDensityEvaluator& Nested = *NestedEvaluators.FindChecked(Node);
    const int32 Count = Scratch.Count;
    const double SizeXZ = Data.Params[0], SizeY = Data.Params[1];
    Scratch.NestedX.SetNumUninitialized(Count);
    Scratch.NestedY.SetNumUninitialized(Count);
    Scratch.NestedZ.SetNumUninitialized(Count);
    Scratch.NestedSums.SetNumUninitialized(Count * 8);
    FVoxelDensityScratch& Inner = GetNested(Scratch, Node);
    for (int32 Corner = 0; Corner < 8; ++Corner)
    {
        for (int32 I = 0; I < Count; ++I)
        {
            Scratch.NestedX[I] = (FMath::FloorToDouble(X[I] / SizeXZ) + (Corner & 1)) * SizeXZ;
            Scratch.NestedY[I] = (FMath::FloorToDouble(Y[I] / SizeY) + (Corner >> 1 & 1)) * SizeY;
            Scratch.NestedZ[I] = (FMath::FloorToDouble(Z[I] / SizeXZ) + (Corner >> 2 & 1)) * SizeXZ;
        }
        Nested.Evaluate(Scratch.NestedX.GetData(), Scratch.NestedY.GetData(), Scratch.NestedZ.GetData(), Count, Inner);
        const TConstArrayView<float> Values = Nested.GetValues(Inner, Data.Inputs[0]);
        for (int32 I = 0; I < Count; ++I)
            Scratch.NestedSums[I * 8 + Corner] = Values[I];
    }
    float* Out = Buffer(Scratch, Node);
    for (int32 I = 0; I < Count; ++I)
    {
        const float TX = float((X[I] - FMath::FloorToDouble(X[I] / SizeXZ) * SizeXZ) / SizeXZ);
        const float TY = float((Y[I] - FMath::FloorToDouble(Y[I] / SizeY) * SizeY) / SizeY);
        const float TZ = float((Z[I] - FMath::FloorToDouble(Z[I] / SizeXZ) * SizeXZ) / SizeXZ);
        Out[I] = VoxelDensity::Trilinear(&Scratch.NestedSums[I * 8], TX, TY, TZ);
    }
}

void FVoxelDensityEvaluator::EvaluateTopSurface(
    int32 Node, const double* X, const double* Z, FVoxelDensityScratch& Scratch) const
{
    // From the upper bound rounded down to a multiple of the cell height, step down a cell at a time; the first y whose
    // density is positive is the surface, and the lower bound when none is. The sample's own y is not used.
    const FVoxelDensityNode& Data = Graph.Nodes[Node];
    const FVoxelDensityEvaluator& Nested = *NestedEvaluators.FindChecked(Node);
    const int32 Count = Scratch.Count;
    const double Lower = Data.Params[0], CellHeight = Data.Params[1];
    const float* Upper = Input(Scratch, Node, 1);
    float* Out = Buffer(Scratch, Node);
    FVoxelDensityScratch& Inner = GetNested(Scratch, Node);

    // The parts of the density that never read y run once; the rest once per step, for every point, until every point
    // has found its surface or passed the lower bound.
    Scratch.NestedSums.SetNumUninitialized(Count);
    Scratch.NestedY.SetNumUninitialized(Count);
    TArray<int32>& Active = Scratch.NestedPoints;
    Active.Reset();
    for (int32 I = 0; I < Count; ++I)
    {
        Out[I] = float(Lower);
        Scratch.NestedSums[I] = float(FMath::FloorToDouble(double(Upper[I]) / CellHeight) * CellHeight);
        Scratch.NestedY[I] = Scratch.NestedSums[I];
        if (Scratch.NestedSums[I] >= Lower) Active.Add(I);
    }
    if (Active.IsEmpty()) return;
    Nested.Prepare(Inner, Count);
    Nested.EvaluateNodes(Nested.GetFlatOrder(), X, Scratch.NestedY.GetData(), Z, Inner);
    while (!Active.IsEmpty())
    {
        Nested.EvaluateNodes(Nested.GetVaryingOrder(), X, Scratch.NestedY.GetData(), Z, Inner);
        const TConstArrayView<float> Density = Nested.GetValues(Inner, Data.Inputs[0]);
        int32 Kept = 0;
        for (const int32 I : Active)
        {
            if (Density[I] > 0)
                Out[I] = Scratch.NestedSums[I];
            else if ((Scratch.NestedSums[I] -= float(CellHeight)) >= Lower)
            {
                Scratch.NestedY[I] = Scratch.NestedSums[I];
                Active[Kept++] = I;
            }
        }
        Active.SetNum(Kept, EAllowShrinking::No);
    }
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
            Out[I] = DensitySqueeze(A[I]);
        break;
    case EVoxelDensityOp::Cache:
    case EVoxelDensityOp::BlendDensity:
        // The jar's cache refuses to run outside its deduplicated graph, so its value is taken as its input's: a cache
        // only shares work.
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
    case EVoxelDensityOp::IntervalSelect:
    {
        // Function K covers inputs from threshold K - 1 (inclusive) up to threshold K (exclusive); the first function
        // takes everything below the first threshold and the last everything from the last threshold up.
        const int32 Last = Data.ListCount - 1;
        for (int32 I = 0; I < Count; ++I)
        {
            int32 Entry = 0;
            while (Entry < Last && A[I] >= float(Graph.ListValues[Data.Ref + Entry]))
                ++Entry;
            Out[I] = Scratch.Values[Slots[Graph.ListNodes[Data.Ref + Entry]] * Count + I];
        }
        break;
    }
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
    case EVoxelDensityOp::OldBlendedNoise:
    {
        const FVoxelBlendedNoise& Noise = BlendedNoises.FindChecked(Node);
        for (int32 I = 0; I < Count; ++I)
            Out[I] = Noise.Sample(X[I], Y[I], Z[I]);
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
    case EVoxelDensityOp::Interpolated:
        EvaluateInterpolated(Node, X, Y, Z, Scratch);
        break;
    case EVoxelDensityOp::FindTopSurface:
        EvaluateTopSurface(Node, X, Z, Scratch);
        break;
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

void FVoxelDensityEvaluator::EvaluateBounds(
    TConstArrayView<int32> Nodes, double MinY, double MaxY, TArray<FVoxelDensityBounds>& Bounds) const
{
    check(Bounds.Num() == Graph.Nodes.Num());
    for (const int32 Node : Nodes)
        Bounds[Node] = DensityBoundsWiden(GetBounds(Node, MinY, MaxY, Bounds));
}

FVoxelDensityBounds FVoxelDensityEvaluator::GetBounds(
    int32 Node, double MinY, double MaxY, const TArray<FVoxelDensityBounds>& Bounds) const
{
    const FVoxelDensityNode& Data = Graph.Nodes[Node];
    const FVoxelDensityBounds Unbounded;
    const FVoxelDensityBounds& A = Data.Inputs[0] != INDEX_NONE ? Bounds[Data.Inputs[0]] : Unbounded;
    const FVoxelDensityBounds& B = Data.Inputs[1] != INDEX_NONE ? Bounds[Data.Inputs[1]] : Unbounded;
    const FVoxelDensityBounds& C = Data.Inputs[2] != INDEX_NONE ? Bounds[Data.Inputs[2]] : Unbounded;

    switch (Data.Op)
    {
    case EVoxelDensityOp::Constant:
        return {float(Data.Params[0]), float(Data.Params[0])};
    case EVoxelDensityOp::Add:
        return DensityBoundsOf(A.Min + B.Min, A.Max + B.Max);
    case EVoxelDensityOp::Sub:
        return DensityBoundsOf(A.Min - B.Max, A.Max - B.Min);
    case EVoxelDensityOp::Mul:
        return DensityBoundsMul(A, B);
    case EVoxelDensityOp::Div:
        if (B.Min > 0 || B.Max < 0) return DensityBoundsMul(A, DensityBoundsOf(1.f / B.Min, 1.f / B.Max));
        return Unbounded;
    case EVoxelDensityOp::Min:
        return {FMath::Min(A.Min, B.Min), FMath::Min(A.Max, B.Max)};
    case EVoxelDensityOp::Max:
        return {FMath::Max(A.Min, B.Min), FMath::Max(A.Max, B.Max)};
    case EVoxelDensityOp::Abs:
    case EVoxelDensityOp::Square:
    {
        FVoxelDensityBounds Magnitude = A.Min >= 0 ? A
            : A.Max <= 0                           ? FVoxelDensityBounds{-A.Max, -A.Min}
                                                   : FVoxelDensityBounds{0, FMath::Max(-A.Min, A.Max)};
        if (Data.Op == EVoxelDensityOp::Square) Magnitude = DensityBoundsMul(Magnitude, Magnitude);
        return Magnitude;
    }
    case EVoxelDensityOp::Negate:
        return {-A.Max, -A.Min};
    case EVoxelDensityOp::Cube:
        return DensityBoundsMonotone(A, [](float V) { return V * V * V; });
    case EVoxelDensityOp::HalfNegative:
        return DensityBoundsMonotone(A, [](float V) { return V > 0 ? V : V * .5f; });
    case EVoxelDensityOp::QuarterNegative:
        return DensityBoundsMonotone(A, [](float V) { return V > 0 ? V : V * .25f; });
    case EVoxelDensityOp::Squeeze:
        return DensityBoundsMonotone(A, DensitySqueeze);
    case EVoxelDensityOp::Cache:
    case EVoxelDensityOp::BlendDensity:
    case EVoxelDensityOp::Interpolated:
        // An interpolated value lies between its cell's corner values, so the input's bounds over the corners hold.
        return A;
    case EVoxelDensityOp::Clamp:
        return DensityBoundsMonotone(
            A, [&Data](float V) { return FMath::Clamp(V, float(Data.Params[0]), float(Data.Params[1])); });
    case EVoxelDensityOp::Gradient:
    {
        if (Data.Axis != 1) return DensityBoundsOf(float(Data.Params[1]), float(Data.Params[3]));
        // Monotone in y: its ends bound it.
        const double From = Data.Params[0], To = Data.Params[2];
        const auto At = [&](double Y)
        {
            const double Alpha = To == From ? (Y >= To ? 1. : 0.) : FMath::Clamp((Y - From) / (To - From), 0., 1.);
            return float(Data.Params[1] + Alpha * (Data.Params[3] - Data.Params[1]));
        };
        return DensityBoundsOf(At(MinY), At(MaxY));
    }
    case EVoxelDensityOp::Lerp:
    {
        const FVoxelDensityBounds Difference = DensityBoundsOf(C.Min - B.Max, C.Max - B.Min);
        const FVoxelDensityBounds Step = DensityBoundsMul(A, Difference);
        return DensityBoundsOf(B.Min + Step.Min, B.Max + Step.Max);
    }
    case EVoxelDensityOp::RangeChoice:
        if (A.Min >= float(Data.Params[0]) && A.Max < float(Data.Params[1])) return B;
        if (A.Max < float(Data.Params[0]) || A.Min >= float(Data.Params[1])) return C;
        return DensityBoundsHull(B, C);
    case EVoxelDensityOp::IntervalSelect:
    {
        FVoxelDensityBounds Out = Bounds[Graph.ListNodes[Data.Ref]];
        for (int32 Entry = 1; Entry < Data.ListCount; ++Entry)
            Out = DensityBoundsHull(Out, Bounds[Graph.ListNodes[Data.Ref + Entry]]);
        return Out;
    }
    case EVoxelDensityOp::Noise:
        return {-Noises[Data.Ref].GetBound(), Noises[Data.Ref].GetBound()};
    case EVoxelDensityOp::ShiftA:
    case EVoxelDensityOp::ShiftB:
        return {-4 * Noises[Data.Ref].GetBound(), 4 * Noises[Data.Ref].GetBound()};
    case EVoxelDensityOp::OldBlendedNoise:
    {
        const float Bound = BlendedNoises.FindChecked(Node).GetBound();
        return {-Bound, Bound};
    }
    case EVoxelDensityOp::BlendAlpha:
        return {1, 1};
    case EVoxelDensityOp::BlendOffset:
    case EVoxelDensityOp::Beardifier:
        return {0, 0};
    default:
        // Splines (Hermite segments overshoot their points) and find_top_surface.
        return Unbounded;
    }
}
