#include "Density/VoxelColumnDensity.h"
#include "HAL/PlatformTime.h"

bool FVoxelColumnDensity::Initialize(
    const FVoxelDensityEvaluator& InEvaluator, int32 InRoot, int32 InMinY, int32 InHeight, FString* OutError)
{
    auto Fail = [OutError](const FString& Reason)
    {
        if (OutError) *OutError = Reason;
        return false;
    };
    Evaluator = &InEvaluator;
    Root = InRoot;
    MinY = InMinY;
    Height = InHeight;
    FlatNodes.Reset();
    CornerNodes.Reset();
    InterpolatedNodes.Reset();
    BlockNodes.Reset();
    if (!InEvaluator.IsCompiledNode(Root)) return Fail(TEXT("the root is not compiled"));

    const FVoxelDensityGraph& Graph = InEvaluator.GetGraph();
    // Per-block nodes: what the root reads down to the interpolated nodes. Corner nodes: what those read.
    TArray<bool> InBlocks, InCorners;
    InBlocks.Init(false, Graph.Nodes.Num());
    InCorners.Init(false, Graph.Nodes.Num());
    TArray<int32> Stack = {Root}, Reads;
    CellXZ = 4;
    CellY = 8;
    bool bHasCells = false;
    while (!Stack.IsEmpty())
    {
        const int32 Node = Stack.Pop();
        if (InBlocks[Node]) continue;
        InBlocks[Node] = true;
        const FVoxelDensityNode& Data = Graph.Nodes[Node];
        if (Data.Op == EVoxelDensityOp::Interpolated)
        {
            const int32 SizeXZ = int32(Data.Params[0]), SizeY = int32(Data.Params[1]);
            if (bHasCells && (SizeXZ != CellXZ || SizeY != CellY))
                return Fail(TEXT("interpolated nodes use different cell sizes"));
            CellXZ = SizeXZ;
            CellY = SizeY;
            bHasCells = true;
            TArray<int32> Inner = {Data.Inputs[0]};
            while (!Inner.IsEmpty())
            {
                const int32 Child = Inner.Pop();
                if (InCorners[Child]) continue;
                InCorners[Child] = true;
                if (Graph.Nodes[Child].Op == EVoxelDensityOp::Interpolated)
                    return Fail(TEXT("an interpolated node reads another"));
                InEvaluator.GetReads(Child, Reads);
                Inner.Append(Reads);
            }
            continue;
        }
        InEvaluator.GetReads(Node, Reads);
        Stack.Append(Reads);
    }
    if (CellXZ <= 0 || CellY <= 0 || ColumnSize % CellXZ != 0 || Height % CellY != 0 || MinY % CellY != 0)
        return Fail(FString::Printf(TEXT("cells of %d x %d do not tile the column"), CellXZ, CellY));
    CornersXZ = ColumnSize / CellXZ + 1;
    CornersY = Height / CellY + 1;

    for (const int32 Node : InEvaluator.GetOrder())
    {
        if (InCorners[Node]) (InEvaluator.ReadsY(Node) ? CornerNodes : FlatNodes).Add(Node);
        if (InBlocks[Node])
            (Graph.Nodes[Node].Op == EVoxelDensityOp::Interpolated ? InterpolatedNodes : BlockNodes).Add(Node);
    }
    return true;
}

void FVoxelColumnDensity::Generate(int32 X0, int32 Z0, bool bSkip, FVoxelColumnDensityScratch& Scratch,
    TArray<float>& OutDensity, FVoxelColumnDensityStats* OutStats) const
{
    check(Evaluator);
    const FVoxelDensityEvaluator& Eval = *Evaluator;
    const FVoxelDensityGraph& Graph = Eval.GetGraph();
    const int32 CellsXZ = CornersXZ - 1, CellsY = CornersY - 1;
    const int32 FlatCount = CornersXZ * CornersXZ, CornerCount = FlatCount * CornersY;
    FVoxelColumnDensityStats Stats;
    Stats.Cells = CellsXZ * CellsXZ * CellsY;
    OutDensity.SetNumUninitialized(ColumnSize * ColumnSize * Height, EAllowShrinking::No);

    uint64 Clock = FPlatformTime::Cycles64();
    const auto Lap = [&Clock](double& Into)
    {
        const uint64 Now = FPlatformTime::Cycles64();
        Into = FPlatformTime::ToMilliseconds64(Now - Clock);
        Clock = Now;
    };

    // 1. Everything under interpolated that never reads y, once per corner column.
    Scratch.X.SetNumUninitialized(FlatCount, EAllowShrinking::No);
    Scratch.Y.SetNumUninitialized(FlatCount, EAllowShrinking::No);
    Scratch.Z.SetNumUninitialized(FlatCount, EAllowShrinking::No);
    for (int32 CZ = 0; CZ < CornersXZ; ++CZ)
        for (int32 CX = 0; CX < CornersXZ; ++CX)
        {
            const int32 Index = CZ * CornersXZ + CX;
            Scratch.X[Index] = X0 + CX * CellXZ;
            Scratch.Y[Index] = MinY;
            Scratch.Z[Index] = Z0 + CZ * CellXZ;
        }
    Eval.Prepare(Scratch.Flat, FlatCount);
    Eval.EvaluateNodes(FlatNodes, Scratch.X.GetData(), Scratch.Y.GetData(), Scratch.Z.GetData(), Scratch.Flat);
    Lap(Stats.FlatMs);

    // 2. Each cell's state from bounds: exact flat values at its four corner columns, conservative bounds above them.
    const auto CellIndex = [CellsXZ](int32 CX, int32 CY, int32 CZ) { return (CY * CellsXZ + CZ) * CellsXZ + CX; };
    Scratch.CellStates.Init(Mixed, Stats.Cells);
    if (bSkip)
    {
        Scratch.CornerBounds.Init(FVoxelDensityBounds(), Graph.Nodes.Num());
        Scratch.BlockBounds.Init(FVoxelDensityBounds(), Graph.Nodes.Num());
        for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
            for (int32 CX = 0; CX < CellsXZ; ++CX)
            {
                const int32 Columns[4] = {CZ * CornersXZ + CX, CZ * CornersXZ + CX + 1, (CZ + 1) * CornersXZ + CX,
                    (CZ + 1) * CornersXZ + CX + 1};
                for (const int32 Node : FlatNodes)
                {
                    const TConstArrayView<float> Values = Eval.GetValues(Scratch.Flat, Node);
                    FVoxelDensityBounds& Bounds = Scratch.CornerBounds[Node];
                    Bounds.Min = Bounds.Max = Values[Columns[0]];
                    for (int32 K = 1; K < 4; ++K)
                    {
                        Bounds.Min = FMath::Min(Bounds.Min, Values[Columns[K]]);
                        Bounds.Max = FMath::Max(Bounds.Max, Values[Columns[K]]);
                    }
                }
                for (int32 CY = 0; CY < CellsY; ++CY)
                {
                    const double Bottom = MinY + CY * CellY;
                    Eval.EvaluateBounds(CornerNodes, Bottom, Bottom + CellY, Scratch.CornerBounds);
                    for (const int32 Node : InterpolatedNodes)
                        Scratch.BlockBounds[Node] = Scratch.CornerBounds[Graph.Nodes[Node].Inputs[0]];
                    Eval.EvaluateBounds(BlockNodes, Bottom, Bottom + CellY - 1, Scratch.BlockBounds);
                    const FVoxelDensityBounds& Density = Scratch.BlockBounds[Root];
                    uint8& State = Scratch.CellStates[CellIndex(CX, CY, CZ)];
                    if (Density.Min > 0)
                        State = Solid;
                    else if (Density.Max <= 0)
                        State = Empty;
                    Stats.SolidCells += State == Solid;
                    Stats.EmptyCells += State == Empty;
                }
            }
    }

    Lap(Stats.BoundsMs);

    // 3. The corners of every cell left to evaluate.
    Scratch.CornerNeeded.Init(0, CornerCount);
    for (int32 CY = 0; CY < CellsY; ++CY)
        for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
            for (int32 CX = 0; CX < CellsXZ; ++CX)
                if (Scratch.CellStates[CellIndex(CX, CY, CZ)] == Mixed)
                    for (int32 Corner = 0; Corner < 8; ++Corner)
                        Scratch.CornerNeeded[CornerIndex(
                            CX + (Corner & 1), CY + (Corner >> 1 & 1), CZ + (Corner >> 2 & 1))] = 1;
    Scratch.CornerPoints.Reset();
    Scratch.CornerFlat.Reset();
    for (int32 Index = 0; Index < CornerCount; ++Index)
        if (Scratch.CornerNeeded[Index])
        {
            Scratch.CornerPoints.Add(Index);
            Scratch.CornerFlat.Add(Index % FlatCount);
        }
    const int32 Needed = Scratch.CornerPoints.Num();
    Stats.CornersEvaluated = Needed;
    Scratch.CornerValues.SetNumUninitialized(InterpolatedNodes.Num() * CornerCount, EAllowShrinking::No);
    if (Needed > 0)
    {
        Scratch.X.SetNumUninitialized(Needed, EAllowShrinking::No);
        Scratch.Y.SetNumUninitialized(Needed, EAllowShrinking::No);
        Scratch.Z.SetNumUninitialized(Needed, EAllowShrinking::No);
        for (int32 K = 0; K < Needed; ++K)
        {
            const int32 Index = Scratch.CornerPoints[K];
            Scratch.X[K] = X0 + (Index % CornersXZ) * CellXZ;
            Scratch.Z[K] = Z0 + (Index / CornersXZ % CornersXZ) * CellXZ;
            Scratch.Y[K] = MinY + (Index / FlatCount) * CellY;
        }
        Eval.Prepare(Scratch.Corners, Needed);
        for (const int32 Node : FlatNodes)
        {
            const TConstArrayView<float> Source = Eval.GetValues(Scratch.Flat, Node);
            const TArrayView<float> Target = Eval.GetMutableValues(Scratch.Corners, Node);
            for (int32 K = 0; K < Needed; ++K)
                Target[K] = Source[Scratch.CornerFlat[K]];
        }
        Eval.EvaluateNodes(CornerNodes, Scratch.X.GetData(), Scratch.Y.GetData(), Scratch.Z.GetData(), Scratch.Corners);
        for (int32 Slot = 0; Slot < InterpolatedNodes.Num(); ++Slot)
        {
            const TConstArrayView<float> Values =
                Eval.GetValues(Scratch.Corners, Graph.Nodes[InterpolatedNodes[Slot]].Inputs[0]);
            float* Target = Scratch.CornerValues.GetData() + Slot * CornerCount;
            for (int32 K = 0; K < Needed; ++K)
                Target[Scratch.CornerPoints[K]] = Values[K];
        }
    }

    Lap(Stats.CornersMs);

    // 4. Blocks, one layer of cells at a time: interpolated values first, then the nodes above them.
    const int32 BlocksPerCell = CellXZ * CellY * CellXZ;
    for (int32 CY = 0; CY < CellsY; ++CY)
    {
        int32 Count = 0;
        for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
            for (int32 CX = 0; CX < CellsXZ; ++CX)
            {
                const uint8 State = Scratch.CellStates[CellIndex(CX, CY, CZ)];
                if (State == Mixed)
                {
                    Count += BlocksPerCell;
                    continue;
                }
                for (int32 DY = 0; DY < CellY; ++DY)
                    for (int32 DZ = 0; DZ < CellXZ; ++DZ)
                        for (int32 DX = 0; DX < CellXZ; ++DX)
                            OutDensity[((CY * CellY + DY) * ColumnSize + CZ * CellXZ + DZ) * ColumnSize + CX * CellXZ +
                                DX] = State == Solid ? 1.f : -1.f;
            }
        if (Count == 0) continue;
        Stats.BlocksEvaluated += Count;

        Scratch.X.SetNumUninitialized(Count, EAllowShrinking::No);
        Scratch.Y.SetNumUninitialized(Count, EAllowShrinking::No);
        Scratch.Z.SetNumUninitialized(Count, EAllowShrinking::No);
        Eval.Prepare(Scratch.Blocks, Count);
        int32 K = 0;
        for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
            for (int32 CX = 0; CX < CellsXZ; ++CX)
            {
                if (Scratch.CellStates[CellIndex(CX, CY, CZ)] != Mixed) continue;
                const int32 First = K;
                for (int32 DY = 0; DY < CellY; ++DY)
                    for (int32 DZ = 0; DZ < CellXZ; ++DZ)
                        for (int32 DX = 0; DX < CellXZ; ++DX, ++K)
                        {
                            Scratch.X[K] = X0 + CX * CellXZ + DX;
                            Scratch.Y[K] = MinY + CY * CellY + DY;
                            Scratch.Z[K] = Z0 + CZ * CellXZ + DZ;
                        }
                for (int32 Slot = 0; Slot < InterpolatedNodes.Num(); ++Slot)
                {
                    const float* Values = Scratch.CornerValues.GetData() + Slot * CornerCount;
                    float Corners[8];
                    for (int32 Corner = 0; Corner < 8; ++Corner)
                        Corners[Corner] =
                            Values[CornerIndex(CX + (Corner & 1), CY + (Corner >> 1 & 1), CZ + (Corner >> 2 & 1))];
                    const TArrayView<float> Target = Eval.GetMutableValues(Scratch.Blocks, InterpolatedNodes[Slot]);
                    int32 Point = First;
                    for (int32 DY = 0; DY < CellY; ++DY)
                        for (int32 DZ = 0; DZ < CellXZ; ++DZ)
                            for (int32 DX = 0; DX < CellXZ; ++DX, ++Point)
                                Target[Point] = VoxelDensity::Trilinear(Corners, float(double(DX) / CellXZ),
                                    float(double(DY) / CellY), float(double(DZ) / CellXZ));
                }
            }
        Eval.EvaluateNodes(BlockNodes, Scratch.X.GetData(), Scratch.Y.GetData(), Scratch.Z.GetData(), Scratch.Blocks);
        const TConstArrayView<float> Density = Eval.GetValues(Scratch.Blocks, Root);
        for (int32 Point = 0; Point < Count; ++Point)
        {
            const int32 X = int32(Scratch.X[Point]) - X0, Y = int32(Scratch.Y[Point]) - MinY,
                        Z = int32(Scratch.Z[Point]) - Z0;
            OutDensity[(Y * ColumnSize + Z) * ColumnSize + X] = Density[Point];
        }
    }
    Lap(Stats.BlocksMs);
    if (OutStats) *OutStats = Stats;
}
