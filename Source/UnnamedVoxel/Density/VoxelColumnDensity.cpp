#include "Density/VoxelColumnDensity.h"
#include "Density/VoxelCornerCache.h"
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
        // Deferred nodes (lazy range_choice branches) hold no values to bound from, so they are bounded with the
        // nodes that read y.
        if (InCorners[Node])
            (InEvaluator.ReadsY(Node) || InEvaluator.IsDeferred(Node) ? CornerNodes : FlatNodes).Add(Node);
        if (InBlocks[Node])
            (Graph.Nodes[Node].Op == EVoxelDensityOp::Interpolated ? InterpolatedNodes : BlockNodes).Add(Node);
    }
    return true;
}

bool FVoxelColumnDensity::MakeCornerCache(FVoxelCornerCache& Out) const
{
    return Out.Initialize(InterpolatedNodes.Num(), CornersY, CellXZ, ColumnSize, this);
}

bool FVoxelColumnDensity::IsKeptEmpty(TConstArrayView<uint8> KeepEmpty, int32 CY) const
{
    if (KeepEmpty.IsEmpty()) return false;
    for (int32 DY = 0; DY < CellY; ++DY)
        if (KeepEmpty[CY * CellY + DY]) return true;
    return false;
}

void FVoxelColumnDensity::Generate(int32 X0, int32 Z0, bool bSkip, FVoxelColumnDensityScratch& Scratch,
    TArray<float>& OutDensity, FVoxelColumnDensityStats* OutStats, TConstArrayView<uint8> KeepEmpty,
    FVoxelCornerCache* Cache) const
{
    check(KeepEmpty.IsEmpty() || KeepEmpty.Num() == Height);
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
                // The density's bound over cells Low to High, as Solid, Empty or Mixed.
                const auto BoundCells = [&](int32 Low, int32 High)
                {
                    const double Bottom = MinY + Low * CellY, Top = MinY + (High + 1) * CellY;
                    Eval.EvaluateBounds(CornerNodes, Bottom, Top, Scratch.CornerBounds);
                    for (const int32 Node : InterpolatedNodes)
                        Scratch.BlockBounds[Node] = Scratch.CornerBounds[Graph.Nodes[Node].Inputs[0]];
                    Eval.EvaluateBounds(BlockNodes, Bottom, Top - 1, Scratch.BlockBounds);
                    const FVoxelDensityBounds& Density = Scratch.BlockBounds[Root];
                    if (Density.Min > 0) return Solid;
                    if (!(Density.Max <= 0)) return Mixed;
                    for (int32 CY = Low; CY <= High; ++CY)
                        if (IsKeptEmpty(KeepEmpty, CY)) return Mixed;
                    return Empty;
                };
                // From the top down: stacks of cells while a whole stack is certain (the open air above the
                // terrain), then one cell at a time down to the first mixed cell. Cells below it stay mixed: with caves
                // their bounds are too loose to prove anything, and stage 3b settles them from their corners.
                int32 CY = CellsY - 1;
                for (; CY >= BoundStack - 1; CY -= BoundStack)
                {
                    const uint8 State = BoundCells(CY - BoundStack + 1, CY);
                    if (State == Mixed) break;
                    for (int32 Cell = CY - BoundStack + 1; Cell <= CY; ++Cell)
                        Scratch.CellStates[CellIndex(CX, Cell, CZ)] = State;
                    (State == Solid ? Stats.SolidCells : Stats.EmptyCells) += BoundStack;
                }
                for (; CY >= 0; --CY)
                {
                    const uint8 State = BoundCells(CY, CY);
                    Scratch.CellStates[CellIndex(CX, CY, CZ)] = State;
                    Stats.SolidCells += State == Solid;
                    Stats.EmptyCells += State == Empty;
                    if (State == Mixed) break;
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
    Scratch.CornerValues.SetNumUninitialized(InterpolatedNodes.Num() * CornerCount, EAllowShrinking::No);
    // Edge corners a neighbour has already evaluated come from the cache (marked 2); the rest (1) are evaluated here.
    const int32 ValueCount = InterpolatedNodes.Num();
    const auto ForEachEdge = [this](auto&& Visit)
    {
        const int32 Last = CornersXZ - 1;
        for (int32 CZ = 0; CZ < CornersXZ; ++CZ)
            for (int32 CX = 0; CX < CornersXZ; ++CX)
                if (CX == 0 || CX == Last || CZ == 0 || CZ == Last) Visit(CX, CZ);
    };
    // A cache made by another column density holds other corners: never read it.
    if (Cache &&
        !ensureMsgf(
            Cache->BelongsTo(this, ValueCount, CornersY, CellXZ), TEXT("A corner cache from another column density")))
        Cache = nullptr;
    if (Cache)
    {
        Scratch.CacheValues.SetNumUninitialized(CornersY * ValueCount, EAllowShrinking::No);
        ForEachEdge(
            [&](int32 CX, int32 CZ)
            {
                uint64 Wanted = 0;
                for (int32 CY = 0; CY < CornersY; ++CY)
                    Wanted |= uint64(Scratch.CornerNeeded[CornerIndex(CX, CY, CZ)] != 0) << CY;
                const uint64 Found =
                    Cache->Find(X0 + CX * CellXZ, Z0 + CZ * CellXZ, Wanted, Scratch.CacheValues.GetData());
                for (uint64 Bits = Found; Bits != 0; Bits &= Bits - 1)
                {
                    const int32 CY = int32(FMath::CountTrailingZeros64(Bits));
                    const int32 Index = CornerIndex(CX, CY, CZ);
                    for (int32 Slot = 0; Slot < ValueCount; ++Slot)
                        Scratch.CornerValues[Slot * CornerCount + Index] = Scratch.CacheValues[CY * ValueCount + Slot];
                    Scratch.CornerNeeded[Index] = 2;
                    ++Stats.CornersShared;
                }
            });
    }
    Scratch.CornerPoints.Reset();
    Scratch.CornerFlat.Reset();
    for (int32 Index = 0; Index < CornerCount; ++Index)
        if (Scratch.CornerNeeded[Index] == 1)
        {
            Scratch.CornerPoints.Add(Index);
            Scratch.CornerFlat.Add(Index % FlatCount);
        }
    const int32 Needed = Scratch.CornerPoints.Num();
    Stats.CornersEvaluated = Needed;
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
    if (Cache && Needed > 0)
        ForEachEdge(
            [&](int32 CX, int32 CZ)
            {
                uint64 Mask = 0;
                for (int32 CY = 0; CY < CornersY; ++CY)
                {
                    const int32 Index = CornerIndex(CX, CY, CZ);
                    if (Scratch.CornerNeeded[Index] != 1) continue;
                    Mask |= uint64(1) << CY;
                    for (int32 Slot = 0; Slot < ValueCount; ++Slot)
                        Scratch.CacheValues[CY * ValueCount + Slot] = Scratch.CornerValues[Slot * CornerCount + Index];
                }
                Cache->Store(X0 + CX * CellXZ, Z0 + CZ * CellXZ, Mask, Scratch.CacheValues.GetData());
            });

    Lap(Stats.CornersMs);

    // 3b. Mixed cells again, now from their corners: inside a cell an interpolated value lies between its 8 corner
    // values (trilinear weights are convex; the margin covers float rounding), and the nodes above it are bounded over
    // the cell's blocks as in stage 2. Cells that turn out certain skip the per-block work.
    if (bSkip && Needed + Stats.CornersShared > 0)
    {
        for (int32 CY = 0; CY < CellsY; ++CY)
        {
            const double Bottom = MinY + CY * CellY;
            const bool bKeptEmpty = IsKeptEmpty(KeepEmpty, CY);
            for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
                for (int32 CX = 0; CX < CellsXZ; ++CX)
                {
                    uint8& State = Scratch.CellStates[CellIndex(CX, CY, CZ)];
                    if (State != Mixed) continue;
                    for (int32 Slot = 0; Slot < InterpolatedNodes.Num(); ++Slot)
                    {
                        const float* Values = Scratch.CornerValues.GetData() + Slot * CornerCount;
                        float Min = UE_BIG_NUMBER, Max = -UE_BIG_NUMBER;
                        for (int32 Corner = 0; Corner < 8; ++Corner)
                        {
                            const float Value =
                                Values[CornerIndex(CX + (Corner & 1), CY + (Corner >> 1 & 1), CZ + (Corner >> 2 & 1))];
                            Min = FMath::Min(Min, Value);
                            Max = FMath::Max(Max, Value);
                        }
                        const float Margin = FMath::Max(FMath::Abs(Min), FMath::Abs(Max)) * 1e-5f + 1e-30f;
                        Scratch.BlockBounds[InterpolatedNodes[Slot]] = {Min - Margin, Max + Margin};
                    }
                    Eval.EvaluateBounds(BlockNodes, Bottom, Bottom + CellY - 1, Scratch.BlockBounds);
                    const FVoxelDensityBounds& Density = Scratch.BlockBounds[Root];
                    if (Density.Min > 0)
                        State = Solid;
                    else if (Density.Max <= 0 && !bKeptEmpty)
                        State = Empty;
                    Stats.RefinedCells += State != Mixed;
                }
        }
    }

    Lap(Stats.RefineMs);

    // 4. Blocks, one layer of cells at a time: interpolated values first, then the nodes above them.
    const int32 BlocksPerCell = CellXZ * CellY * CellXZ;
    // Trilinear weights by offset in the cell, computed as direct evaluation computes them.
    Scratch.WeightsXZ.SetNumUninitialized(CellXZ, EAllowShrinking::No);
    Scratch.WeightsY.SetNumUninitialized(CellY, EAllowShrinking::No);
    for (int32 D = 0; D < CellXZ; ++D)
        Scratch.WeightsXZ[D] = float(double(D) / CellXZ);
    for (int32 D = 0; D < CellY; ++D)
        Scratch.WeightsY[D] = float(double(D) / CellY);
    const float* WeightsXZ = Scratch.WeightsXZ.GetData();
    const float* WeightsY = Scratch.WeightsY.GetData();
    // Raw pointers keep range checks out of the per-block loops. Blocks of a layer of cells run Y, then Z, then X.
    float* const Densities = OutDensity.GetData();
    const int32 LayerBlocks = CellY * ColumnSize * ColumnSize;
    const auto RowOf = [this](int32 CX, int32 DY, int32 CZ, int32 DZ)
    { return (DY * ColumnSize + CZ * CellXZ + DZ) * ColumnSize + CX * CellXZ; };
    for (int32 CY = 0; CY < CellsY; ++CY)
    {
        const uint8* const States = Scratch.CellStates.GetData() + CellIndex(0, CY, 0);
        float* const Layer = Densities + CY * LayerBlocks;
        int32 Count = 0;
        bool bOneState = true;
        for (int32 Cell = 0; Cell < CellsXZ * CellsXZ; ++Cell)
        {
            Count += States[Cell] == Mixed ? BlocksPerCell : 0;
            bOneState &= States[Cell] == States[0];
        }
        // Certain cells read +1 or -1; a layer of one certain state is one run.
        if (bOneState && States[0] != Mixed)
        {
            const float Value = States[0] == Solid ? 1.f : -1.f;
            for (int32 Block = 0; Block < LayerBlocks; ++Block)
                Layer[Block] = Value;
        }
        else
            for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
                for (int32 CX = 0; CX < CellsXZ; ++CX)
                {
                    const uint8 State = States[CZ * CellsXZ + CX];
                    if (State == Mixed) continue;
                    const float Value = State == Solid ? 1.f : -1.f;
                    for (int32 DY = 0; DY < CellY; ++DY)
                        for (int32 DZ = 0; DZ < CellXZ; ++DZ)
                        {
                            float* const Row = Layer + RowOf(CX, DY, CZ, DZ);
                            for (int32 DX = 0; DX < CellXZ; ++DX)
                                Row[DX] = Value;
                        }
                }
        if (Count == 0) continue;
        Stats.BlocksEvaluated += Count;

        Scratch.X.SetNumUninitialized(Count, EAllowShrinking::No);
        Scratch.Y.SetNumUninitialized(Count, EAllowShrinking::No);
        Scratch.Z.SetNumUninitialized(Count, EAllowShrinking::No);
        double* const PX = Scratch.X.GetData();
        double* const PY = Scratch.Y.GetData();
        double* const PZ = Scratch.Z.GetData();
        Eval.Prepare(Scratch.Blocks, Count);
        int32 K = 0;
        for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
            for (int32 CX = 0; CX < CellsXZ; ++CX)
            {
                if (States[CZ * CellsXZ + CX] != Mixed) continue;
                const int32 First = K;
                for (int32 DY = 0; DY < CellY; ++DY)
                    for (int32 DZ = 0; DZ < CellXZ; ++DZ)
                        for (int32 DX = 0; DX < CellXZ; ++DX, ++K)
                        {
                            PX[K] = X0 + CX * CellXZ + DX;
                            PY[K] = MinY + CY * CellY + DY;
                            PZ[K] = Z0 + CZ * CellXZ + DZ;
                        }
                for (int32 Slot = 0; Slot < InterpolatedNodes.Num(); ++Slot)
                {
                    const float* Values = Scratch.CornerValues.GetData() + Slot * CornerCount;
                    float Corners[8];
                    for (int32 Corner = 0; Corner < 8; ++Corner)
                        Corners[Corner] =
                            Values[CornerIndex(CX + (Corner & 1), CY + (Corner >> 1 & 1), CZ + (Corner >> 2 & 1))];
                    float* Target = Eval.GetMutableValues(Scratch.Blocks, InterpolatedNodes[Slot]).GetData() + First;
                    // VoxelDensity::Trilinear's steps, each computed once for the blocks that share it: the same
                    // expressions on the same values, so every block gets Trilinear's value bit for bit.
                    float X00[ColumnSize], X10[ColumnSize], X01[ColumnSize], X11[ColumnSize];
                    for (int32 DX = 0; DX < CellXZ; ++DX)
                    {
                        const float TX = WeightsXZ[DX];
                        X00[DX] = Corners[0] + TX * (Corners[1] - Corners[0]);
                        X10[DX] = Corners[2] + TX * (Corners[3] - Corners[2]);
                        X01[DX] = Corners[4] + TX * (Corners[5] - Corners[4]);
                        X11[DX] = Corners[6] + TX * (Corners[7] - Corners[6]);
                    }
                    for (int32 DY = 0; DY < CellY; ++DY)
                    {
                        const float TY = WeightsY[DY];
                        float Y0[ColumnSize], Y1[ColumnSize];
                        for (int32 DX = 0; DX < CellXZ; ++DX)
                        {
                            Y0[DX] = X00[DX] + TY * (X10[DX] - X00[DX]);
                            Y1[DX] = X01[DX] + TY * (X11[DX] - X01[DX]);
                        }
                        for (int32 DZ = 0; DZ < CellXZ; ++DZ)
                        {
                            const float TZ = WeightsXZ[DZ];
                            for (int32 DX = 0; DX < CellXZ; ++DX)
                                *Target++ = Y0[DX] + TZ * (Y1[DX] - Y0[DX]);
                        }
                    }
                }
            }
        Eval.EvaluateNodes(BlockNodes, Scratch.X.GetData(), Scratch.Y.GetData(), Scratch.Z.GetData(), Scratch.Blocks);
        // Back in the order the points were laid out.
        const float* const Density = Eval.GetValues(Scratch.Blocks, Root).GetData();
        K = 0;
        for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
            for (int32 CX = 0; CX < CellsXZ; ++CX)
            {
                if (States[CZ * CellsXZ + CX] != Mixed) continue;
                for (int32 DY = 0; DY < CellY; ++DY)
                    for (int32 DZ = 0; DZ < CellXZ; ++DZ)
                    {
                        float* const Row = Layer + RowOf(CX, DY, CZ, DZ);
                        for (int32 DX = 0; DX < CellXZ; ++DX)
                            Row[DX] = Density[K++];
                    }
            }
    }
    Lap(Stats.BlocksMs);
    if (OutStats) *OutStats = Stats;
}
