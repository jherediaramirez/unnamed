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

    // The scratch's noise position buffers, sized for Count points.
    void GetNoisePositions(FVoxelDensityScratch& Scratch, int32 Count, double*& OutX, double*& OutY, double*& OutZ)
    {
        Scratch.NoiseX.SetNumUninitialized(Count, EAllowShrinking::No);
        Scratch.NoiseY.SetNumUninitialized(Count, EAllowShrinking::No);
        Scratch.NoiseZ.SetNumUninitialized(Count, EAllowShrinking::No);
        OutX = Scratch.NoiseX.GetData();
        OutY = Scratch.NoiseY.GetData();
        OutZ = Scratch.NoiseZ.GetData();
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
    LazyBounds.Reset();
    LazyChoices.Reset();
    Owners.Reset();
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

    FindLazyFunctions(Needed, Roots, WorldSeed);

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
        if (Data.Op == EVoxelDensityOp::IntervalSelect)
            for (int32 Entry = 0; Entry < Data.ListCount; ++Entry)
            {
                const int32 Function = Graph.ListNodes[Data.Ref + Entry];
                if (LazyBounds.Contains(Function)) bReadsY |= NestedEvaluators[Function]->ReadsY(Function);
            }
        NodeReadsY[Node] = bReadsY;
        (bReadsY ? VaryingOrder : FlatOrder).Add(Node);
    }
    FindLazyChoices(Roots);
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
            if (!LazyBounds.Contains(Graph.ListNodes[Data.Ref + Entry])) Out.Add(Graph.ListNodes[Data.Ref + Entry]);
}

void FVoxelDensityEvaluator::FindLazyFunctions(TArray<bool>& Needed, TConstArrayView<int32> Roots, uint64 WorldSeed)
{
    // Every read among the needed nodes, as the needed pass follows them; roots are read from outside.
    const int32 NodeCount = Graph.Nodes.Num();
    auto ForEachRead = [this](int32 Node, auto&& Visit)
    {
        const FVoxelDensityNode& Data = Graph.Nodes[Node];
        if (Data.Op == EVoxelDensityOp::FindTopSurface)
        {
            Visit(Data.Inputs[1]);
            return;
        }
        for (const int32 Child : Data.Inputs)
            if (Child != INDEX_NONE) Visit(Child);
        if (Data.Op == EVoxelDensityOp::Spline)
            for (const int32 Coordinate : Splines[Data.Ref].Coordinates)
                Visit(Coordinate);
        if (Data.Op == EVoxelDensityOp::IntervalSelect)
            for (int32 Entry = 0; Entry < Data.ListCount; ++Entry)
                Visit(Graph.ListNodes[Data.Ref + Entry]);
    };
    TArray<int32> Readers;
    Readers.Init(0, NodeCount);
    for (int32 Node = 0; Node < NodeCount; ++Node)
        if (Needed[Node]) ForEachRead(Node, [&Readers](int32 Child) { ++Readers[Child]; });
    for (const int32 Root : Roots)
        Readers[Root] += NodeCount;

    TArray<int32> Inner, Stack;
    Inner.Init(0, NodeCount);
    TArray<int32> Subtree;
    for (int32 Select = 0; Select < NodeCount; ++Select)
    {
        const FVoxelDensityNode& Data = Graph.Nodes[Select];
        if (!Needed[Select] || Data.Op != EVoxelDensityOp::IntervalSelect) continue;
        for (int32 Entry = 0; Entry < Data.ListCount; ++Entry)
        {
            const int32 Function = Graph.ListNodes[Data.Ref + Entry];
            if (Graph.Nodes[Function].Op == EVoxelDensityOp::Constant || LazyBounds.Contains(Function)) continue;
            // The function's subtree, and how often its own nodes and this select read each node in it.
            Subtree.Reset();
            Stack = {Function};
            bool bPlain = true;
            while (!Stack.IsEmpty() && bPlain)
            {
                const int32 Node = Stack.Pop(EAllowShrinking::No);
                if (Subtree.Contains(Node)) continue;
                Subtree.Add(Node);
                const EVoxelDensityOp Op = Graph.Nodes[Node].Op;
                bPlain = Op != EVoxelDensityOp::Interpolated && Op != EVoxelDensityOp::FindTopSurface;
                ForEachRead(Node, [&Stack](int32 Child) { Stack.Add(Child); });
            }
            if (!bPlain) continue;
            for (const int32 Node : Subtree)
                ForEachRead(Node, [&Inner](int32 Child) { ++Inner[Child]; });
            for (int32 Other = 0; Other < Data.ListCount; ++Other)
                Inner[Function] += Graph.ListNodes[Data.Ref + Other] == Function;
            bool bPrivate = true;
            for (const int32 Node : Subtree)
            {
                bPrivate &= Readers[Node] == Inner[Node];
                Inner[Node] = 0;
            }
            if (!bPrivate) continue;

            TSharedPtr<FVoxelDensityEvaluator> Nested = MakeShared<FVoxelDensityEvaluator>();
            if (!Nested->Compile(Graph, WorldSeed, {Function})) continue;
            TArray<FVoxelDensityBounds> Bounds;
            Bounds.Init(FVoxelDensityBounds(), NodeCount);
            Nested->EvaluateBounds(Nested->GetOrder(), -1e6, 1e6, Bounds);
            LazyBounds.Add(Function, Bounds[Function]);
            NestedIndices.Add(Function, NestedEvaluators.Num());
            NestedEvaluators.Add(Function, Nested);
            for (const int32 Node : Subtree)
                Needed[Node] = false;
        }
    }
}

void FVoxelDensityEvaluator::FindLazyChoices(TConstArrayView<int32> Roots)
{
    const int32 NodeCount = Graph.Nodes.Num();
    Owners.Init(INDEX_NONE, NodeCount);
    // Who reads each compiled node; roots are read from outside.
    TArray<TArray<int32>> Readers;
    Readers.SetNum(NodeCount);
    TArray<bool> External;
    External.Init(false, NodeCount);
    for (const int32 Root : Roots)
        External[Root] = true;
    TArray<int32> Reads;
    for (const int32 Node : Order)
    {
        GetReads(Node, Reads);
        for (const int32 Child : Reads)
            Readers[Child].Add(Node);
    }

    // Outer choices first, so a choice inside the branch of another takes its own private nodes from it.
    TArray<int32> Branches, Stamps, Subtree, Stack, Members;
    Branches.Init(INDEX_NONE, NodeCount);
    Stamps.Init(INDEX_NONE, NodeCount);
    int32 Stamp = 0;
    for (int32 Position = Order.Num() - 1; Position >= 0; --Position)
    {
        const int32 Choice = Order[Position];
        const FVoxelDensityNode& Data = Graph.Nodes[Choice];
        if (Data.Op != EVoxelDensityOp::RangeChoice) continue;
        for (int32 Branch = 0; Branch < 2; ++Branch)
        {
            const int32 Root = Data.Inputs[1 + Branch];
            if (Root == Data.Inputs[0] || Root == Data.Inputs[2 - Branch]) continue;
            Subtree.Reset();
            Stack = {Root};
            ++Stamp;
            while (!Stack.IsEmpty())
            {
                const int32 Node = Stack.Pop(EAllowShrinking::No);
                if (Stamps[Node] == Stamp) continue;
                Stamps[Node] = Stamp;
                Subtree.Add(Node);
                GetReads(Node, Reads);
                Stack.Append(Reads);
            }
            // Readers come after what they read, so from the end down each node's readers are decided before it.
            Subtree.Sort([](int32 Left, int32 Right) { return Left > Right; });
            ++Stamp;
            Members.Reset();
            bool bCostly = false;
            for (const int32 Node : Subtree)
            {
                const EVoxelDensityOp Op = Graph.Nodes[Node].Op;
                if (External[Node] || Op == EVoxelDensityOp::Interpolated || Op == EVoxelDensityOp::FindTopSurface)
                    continue;
                bool bPrivate = true;
                for (const int32 Reader : Readers[Node])
                    bPrivate &= Reader == Choice ? Node == Root : Stamps[Reader] == Stamp;
                if (!bPrivate) continue;
                Stamps[Node] = Stamp;
                Members.Add(Node);
                bCostly |= Op == EVoxelDensityOp::Noise || Op == EVoxelDensityOp::ShiftA ||
                    Op == EVoxelDensityOp::ShiftB || Op == EVoxelDensityOp::OldBlendedNoise ||
                    Op == EVoxelDensityOp::Spline || Op == EVoxelDensityOp::IntervalSelect;
            }
            if (!bCostly) continue;
            LazyChoices.FindOrAdd(Choice);
            for (const int32 Node : Members)
            {
                Owners[Node] = Choice;
                Branches[Node] = Branch;
            }
        }
    }
    if (LazyChoices.IsEmpty()) return;
    for (const int32 Node : Order)
        if (Owners[Node] != INDEX_NONE) LazyChoices[Owners[Node]].Nodes[Branches[Node]].Add(Node);

    // A branch's frontier: what the nodes evaluated in its scratch (its own, and those of choices nested inside it)
    // read from outside the choice. The branch of Choice a node is evaluated in, or INDEX_NONE.
    const auto BranchOf = [this, &Branches](int32 Node, int32 Choice)
    {
        for (int32 Current = Node; Owners[Current] != INDEX_NONE; Current = Owners[Current])
            if (Owners[Current] == Choice) return Branches[Current];
        return int32(INDEX_NONE);
    };
    for (TPair<int32, FLazyChoice>& Pair : LazyChoices)
        for (const int32 Node : Order)
        {
            const int32 Branch = BranchOf(Node, Pair.Key);
            if (Branch == INDEX_NONE) continue;
            GetReads(Node, Reads);
            for (const int32 Child : Reads)
                if (BranchOf(Child, Pair.Key) == INDEX_NONE) Pair.Value.Frontier[Branch].AddUnique(Child);
        }
    for (const TPair<int32, FLazyChoice>& Pair : LazyChoices)
        NestedIndices.Add(Pair.Key, NestedIndices.Num());
}

void FVoxelDensityEvaluator::EvaluateLazyChoice(
    int32 Node, const double* X, const double* Y, const double* Z, FVoxelDensityScratch& Scratch) const
{
    // As the plain range_choice, but a branch with private nodes evaluates them only at the points that pick it.
    const FVoxelDensityNode& Data = Graph.Nodes[Node];
    const FLazyChoice& Choice = LazyChoices.FindChecked(Node);
    const int32 Count = Scratch.Count;
    float* Out = Buffer(Scratch, Node);
    const float* A = Input(Scratch, Node, 0);
    const float Min = float(Data.Params[0]), Max = float(Data.Params[1]);
    for (int32 Branch = 0; Branch < 2; ++Branch)
    {
        const bool bInRange = Branch == 0;
        const int32 Root = Data.Inputs[1 + Branch];
        if (Choice.Nodes[Branch].IsEmpty())
        {
            const float* Values = Buffer(Scratch, Root);
            for (int32 I = 0; I < Count; ++I)
                if ((A[I] >= Min && A[I] < Max) == bInRange) Out[I] = Values[I];
            continue;
        }
        TArray<int32>& Points = Scratch.ChoicePoints;
        Points.Reset();
        for (int32 I = 0; I < Count; ++I)
            if ((A[I] >= Min && A[I] < Max) == bInRange) Points.Add(I);
        const int32 Picked = Points.Num();
        if (Picked == 0) continue;
        Scratch.ChoiceX.SetNumUninitialized(Picked, EAllowShrinking::No);
        Scratch.ChoiceY.SetNumUninitialized(Picked, EAllowShrinking::No);
        Scratch.ChoiceZ.SetNumUninitialized(Picked, EAllowShrinking::No);
        for (int32 K = 0; K < Picked; ++K)
        {
            Scratch.ChoiceX[K] = X[Points[K]];
            Scratch.ChoiceY[K] = Y[Points[K]];
            Scratch.ChoiceZ[K] = Z[Points[K]];
        }
        FVoxelDensityScratch& Inner = GetNested(Scratch, Node);
        Prepare(Inner, Picked);
        for (const int32 Read : Choice.Frontier[Branch])
        {
            const float* Source = Buffer(Scratch, Read);
            float* Target = Buffer(Inner, Read);
            for (int32 K = 0; K < Picked; ++K)
                Target[K] = Source[Points[K]];
        }
        for (const int32 Private : Choice.Nodes[Branch])
            EvaluateNode(
                Private, Scratch.ChoiceX.GetData(), Scratch.ChoiceY.GetData(), Scratch.ChoiceZ.GetData(), Inner);
        const float* Values = Buffer(Inner, Root);
        for (int32 K = 0; K < Picked; ++K)
            Out[Points[K]] = Values[K];
    }
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
    checkSlow(!IsDeferred(Node));
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
        if (Owners[Node] == INDEX_NONE) EvaluateNode(Node, X, Y, Z, Scratch);
}

void FVoxelDensityEvaluator::EvaluateNodes(TConstArrayView<int32> Nodes, const double* X, const double* Y,
    const double* Z, FVoxelDensityScratch& Scratch) const
{
    for (const int32 Node : Nodes)
        if (Owners[Node] == INDEX_NONE) EvaluateNode(Node, X, Y, Z, Scratch);
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
void FVoxelDensityEvaluator::EvaluateLazySelect(
    int32 Node, const double* X, const double* Y, const double* Z, FVoxelDensityScratch& Scratch) const
{
    // As the plain interval_select, but lazy functions run only at the points that pick them, by their own evaluator.
    const FVoxelDensityNode& Data = Graph.Nodes[Node];
    const int32 Count = Scratch.Count;
    float* Out = Buffer(Scratch, Node);
    const float* A = Input(Scratch, Node, 0);
    const int32 Last = Data.ListCount - 1;
    Scratch.Choices.SetNumUninitialized(Count, EAllowShrinking::No);
    for (int32 I = 0; I < Count; ++I)
    {
        int32 Entry = 0;
        while (Entry < Last && A[I] >= float(Graph.ListValues[Data.Ref + Entry]))
            ++Entry;
        Scratch.Choices[I] = Entry;
        const int32 Function = Graph.ListNodes[Data.Ref + Entry];
        if (Slots[Function] != INDEX_NONE) Out[I] = Scratch.Values[Slots[Function] * Count + I];
    }
    for (int32 Entry = 0; Entry <= Last; ++Entry)
    {
        const int32 Function = Graph.ListNodes[Data.Ref + Entry];
        if (Slots[Function] != INDEX_NONE) continue;
        Scratch.NestedPoints.Reset();
        for (int32 I = 0; I < Count; ++I)
            if (Scratch.Choices[I] == Entry) Scratch.NestedPoints.Add(I);
        const int32 Picked = Scratch.NestedPoints.Num();
        if (Picked == 0) continue;
        Scratch.NestedX.SetNumUninitialized(Picked, EAllowShrinking::No);
        Scratch.NestedY.SetNumUninitialized(Picked, EAllowShrinking::No);
        Scratch.NestedZ.SetNumUninitialized(Picked, EAllowShrinking::No);
        for (int32 K = 0; K < Picked; ++K)
        {
            const int32 Point = Scratch.NestedPoints[K];
            Scratch.NestedX[K] = X[Point];
            Scratch.NestedY[K] = Y[Point];
            Scratch.NestedZ[K] = Z[Point];
        }
        const FVoxelDensityEvaluator& Nested = *NestedEvaluators.FindChecked(Function);
        FVoxelDensityScratch& Inner = GetNested(Scratch, Function);
        Nested.Evaluate(Scratch.NestedX.GetData(), Scratch.NestedY.GetData(), Scratch.NestedZ.GetData(), Picked, Inner);
        const TConstArrayView<float> Values = Nested.GetValues(Inner, Function);
        for (int32 K = 0; K < Picked; ++K)
            Out[Scratch.NestedPoints[K]] = Values[K];
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
        if (LazyChoices.Contains(Node))
        {
            EvaluateLazyChoice(Node, X, Y, Z, Scratch);
            break;
        }
        for (int32 I = 0; I < Count; ++I)
            Out[I] = A[I] >= float(Data.Params[0]) && A[I] < float(Data.Params[1]) ? B[I] : C[I];
        break;
    case EVoxelDensityOp::IntervalSelect:
    {
        if (!LazyBounds.IsEmpty())
        {
            EvaluateLazySelect(Node, X, Y, Z, Scratch);
            break;
        }
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
        double *NX, *NY, *NZ;
        GetNoisePositions(Scratch, Count, NX, NY, NZ);
        for (int32 I = 0; I < Count; ++I)
        {
            NX[I] = X[I] * ScaleXZ + (A ? A[I] : 0.f);
            NY[I] = Y[I] * ScaleY;
            NZ[I] = Z[I] * ScaleXZ + (B ? B[I] : 0.f);
        }
        Noise.SampleBatch(NX, NY, NZ, Count, Out);
        break;
    }
    case EVoxelDensityOp::ShiftA:
    {
        const FVoxelNormalNoise& Noise = Noises[Data.Ref];
        double *NX, *NY, *NZ;
        GetNoisePositions(Scratch, Count, NX, NY, NZ);
        for (int32 I = 0; I < Count; ++I)
        {
            NX[I] = X[I] * .25;
            NY[I] = 0;
            NZ[I] = Z[I] * .25;
        }
        Noise.SampleBatch(NX, NY, NZ, Count, Out);
        for (int32 I = 0; I < Count; ++I)
            Out[I] = 4 * Out[I];
        break;
    }
    case EVoxelDensityOp::ShiftB:
    {
        const FVoxelNormalNoise& Noise = Noises[Data.Ref];
        double *NX, *NY, *NZ;
        GetNoisePositions(Scratch, Count, NX, NY, NZ);
        for (int32 I = 0; I < Count; ++I)
        {
            NX[I] = Z[I] * .25;
            NY[I] = X[I] * .25;
            NZ[I] = 0;
        }
        Noise.SampleBatch(NX, NY, NZ, Count, Out);
        for (int32 I = 0; I < Count; ++I)
            Out[I] = 4 * Out[I];
        break;
    }
    case EVoxelDensityOp::OldBlendedNoise:
    {
        BlendedNoises.FindChecked(Node).Sample(X, Y, Z, Count, Out, Scratch.Blended);
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
    {
        const FVoxelDensityNode& Data = Graph.Nodes[Node];
        if (Data.Op == EVoxelDensityOp::IntervalSelect && !LazyBounds.IsEmpty())
            for (int32 Entry = 0; Entry < Data.ListCount; ++Entry)
                if (const FVoxelDensityBounds* Lazy = LazyBounds.Find(Graph.ListNodes[Data.Ref + Entry]))
                    Bounds[Graph.ListNodes[Data.Ref + Entry]] = *Lazy;
        Bounds[Node] = DensityBoundsWiden(GetBounds(Node, MinY, MaxY, Bounds));
    }
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
