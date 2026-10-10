#include "Surface/VoxelOreVeins.h"
#include "VoxelConstants.h"

namespace
{
    constexpr int32 VeinColumnSize = VoxelConstants::ColumnSize;
    constexpr int32 VeinArea = VeinColumnSize * VeinColumnSize;

    int32 VeinFloorDiv(int32 Value, int32 Divisor)
    {
        return Value >= 0 ? Value / Divisor : -((-Value + Divisor - 1) / Divisor);
    }
} // namespace

bool FVoxelOreVeins::Compile(
    const FVoxelDensityGraph& Graph, uint64 WorldSeed, TConstArrayView<FVoxelOreVein> InVeins, FString* OutError)
{
    const auto Fail = [this, OutError](const FString& Message)
    {
        bCompiled = false;
        if (OutError) *OutError = Message;
        return false;
    };
    bCompiled = false;
    Veins = TArray<FVoxelOreVein>(InVeins.GetData(), InVeins.Num());
    Seed = WorldSeed;
    Evaluator = FVoxelDensityEvaluator();
    CellXZ = 4;
    CellY = 8;
    CornersXZ = 5;
    InterpolatedNodes.Reset();
    CornerNodes.Reset();
    BlockNodes.Reset();
    DensityNodes.Reset();
    GapNodes.Reset();
    InputBounds.Reset();
    SpanLow = 0;
    SpanHigh = -1;
    if (Veins.Num() > MaxVeins)
        return Fail(FString::Printf(TEXT("%d ore veins; at most %d fit"), Veins.Num(), MaxVeins));
    if (Veins.IsEmpty())
    {
        bCompiled = true;
        return true;
    }
    TArray<int32> Roots;
    for (const FVoxelOreVein& Vein : Veins)
        for (const int32 Node : {Vein.Density, Vein.Richness, Vein.Gap})
        {
            if (!Graph.Nodes.IsValidIndex(Node)) return Fail(TEXT("an ore vein's function is not in the graph"));
            Roots.AddUnique(Node);
        }
    FString Reason;
    if (!Evaluator.Compile(Graph, WorldSeed, Roots, &Reason)) return Fail(TEXT("ore veins: ") + Reason);

    // What the roots read, as FVoxelColumnDensity splits it: per block above the interpolated nodes, at corners below.
    const FVoxelDensityGraph& Nodes = Evaluator.GetGraph();
    const int32 Count = Nodes.Nodes.Num();
    TArray<int32> Reads;
    const auto Closure = [&](TArray<int32> Stack, TArray<bool>& Marked, bool bIntoInterpolated)
    {
        Marked.Init(false, Count);
        while (!Stack.IsEmpty())
        {
            const int32 Node = Stack.Pop();
            if (Marked[Node]) continue;
            Marked[Node] = true;
            if (Nodes.Nodes[Node].Op == EVoxelDensityOp::Interpolated)
            {
                if (bIntoInterpolated) Stack.Add(Nodes.Nodes[Node].Inputs[0]);
                continue;
            }
            Evaluator.GetReads(Node, Reads);
            Stack.Append(Reads);
        }
    };
    TArray<int32> Densities, Blocks, Gaps;
    for (const FVoxelOreVein& Vein : Veins)
    {
        Densities.AddUnique(Vein.Density);
        Blocks.AddUnique(Vein.Density);
        Blocks.AddUnique(Vein.Richness);
        Gaps.AddUnique(Vein.Gap);
    }
    TArray<bool> InBlocks, InDensities, InGaps;
    Closure(Blocks, InBlocks, false);
    Closure(Densities, InDensities, false);
    Closure(Gaps, InGaps, false);
    bool bHasCells = false;
    for (const int32 Node : Evaluator.GetOrder())
    {
        if (InGaps[Node]) GapNodes.Add(Node);
        if (!InBlocks[Node]) continue;
        if (Nodes.Nodes[Node].Op != EVoxelDensityOp::Interpolated)
        {
            BlockNodes.Add(Node);
            if (InDensities[Node]) DensityNodes.Add(Node);
            continue;
        }
        const FVoxelDensityNode& Data = Nodes.Nodes[Node];
        const int32 SizeXZ = int32(Data.Params[0]), SizeY = int32(Data.Params[1]);
        // Direct evaluation interpolates with the sizes as given: only whole sizes tile as the columns do.
        if (SizeXZ != Data.Params[0] || SizeY != Data.Params[1])
            return Fail(TEXT("ore veins: an interpolated node's cell size is not a whole number"));
        if (bHasCells && (SizeXZ != CellXZ || SizeY != CellY))
            return Fail(TEXT("ore veins: interpolated nodes use different cell sizes"));
        CellXZ = SizeXZ;
        CellY = SizeY;
        bHasCells = true;
        InterpolatedNodes.Add(Node);
        TArray<bool> InCorners;
        Closure({Data.Inputs[0]}, InCorners, true);
        TArray<int32>& Corner = CornerNodes.AddDefaulted_GetRef();
        for (const int32 Inner : Evaluator.GetOrder())
            if (InCorners[Inner])
            {
                if (Nodes.Nodes[Inner].Op == EVoxelDensityOp::Interpolated)
                    return Fail(TEXT("ore veins: an interpolated node reads another"));
                Corner.Add(Inner);
            }
    }
    if (CellXZ <= 0 || CellY <= 0 || VeinColumnSize % CellXZ != 0 || VoxelConstants::MinZ % CellY != 0)
        return Fail(FString::Printf(TEXT("ore veins: cells of %d x %d do not tile a column"), CellXZ, CellY));
    CornersXZ = VeinColumnSize / CellXZ + 1;

    // Each interpolated input's bounds over every height, and the heights where each density can be positive: per
    // block height Z, the corner nodes are bounded over Z's cell, the interpolated nodes take their inputs' bounds and
    // the per-block nodes are bounded at Z.
    TArray<FVoxelDensityBounds> Bounds;
    Bounds.Init(FVoxelDensityBounds(), Count);
    for (int32 Slot = 0; Slot < InterpolatedNodes.Num(); ++Slot)
    {
        Evaluator.EvaluateBounds(CornerNodes[Slot], VoxelConstants::MinZ, VoxelConstants::MaxZ + CellY, Bounds);
        InputBounds.Add(Bounds[Nodes.Nodes[InterpolatedNodes[Slot]].Inputs[0]]);
    }
    for (FVoxelOreVein& Vein : Veins)
    {
        Vein.MinZ = 0;
        Vein.MaxZ = -1;
    }
    for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
    {
        const int32 Bottom = VoxelConstants::MinZ + VeinFloorDiv(Z - VoxelConstants::MinZ, CellY) * CellY;
        for (int32 Slot = 0; Slot < InterpolatedNodes.Num(); ++Slot)
        {
            Evaluator.EvaluateBounds(CornerNodes[Slot], Bottom, Bottom + CellY, Bounds);
            // Widened as Generate's cell bounds are, for float rounding in the trilinear steps.
            const FVoxelDensityBounds Input = Bounds[Nodes.Nodes[InterpolatedNodes[Slot]].Inputs[0]];
            const float Margin = FMath::Max(FMath::Abs(Input.Min), FMath::Abs(Input.Max)) * 1e-5f + 1e-30f;
            Bounds[InterpolatedNodes[Slot]] = {Input.Min - Margin, Input.Max + Margin};
        }
        Evaluator.EvaluateBounds(DensityNodes, Z, Z, Bounds);
        for (FVoxelOreVein& Vein : Veins)
        {
            if (Bounds[Vein.Density].Max <= 0) continue;
            if (Vein.MinZ > Vein.MaxZ) Vein.MinZ = Z;
            Vein.MaxZ = Z;
        }
    }
    for (const FVoxelOreVein& Vein : Veins)
    {
        if (Vein.MinZ > Vein.MaxZ) continue;
        const bool bFirst = SpanLow > SpanHigh;
        SpanLow = bFirst ? Vein.MinZ : FMath::Min(SpanLow, Vein.MinZ);
        SpanHigh = bFirst ? Vein.MaxZ : FMath::Max(SpanHigh, Vein.MaxZ);
    }
    bCompiled = true;
    return true;
}

FVoxelOreVeins::EOutcome FVoxelOreVeins::Draw(
    const FVoxelOreVein& Vein, uint64 WorldSeed, int32 X, int32 Y, int32 Z, float Density, float Richness)
{
    if (!(Density > 0)) return EOutcome::Nothing;
    FVoxelXoroshiro Random(VoxelRandom::Hash64(WorldSeed, Salt, X, Y, Z));
    if (!(VoxelRandom::ToUnitFloat(Random.Next()) < Density)) return EOutcome::Nothing;
    if (!(VoxelRandom::ToUnitFloat(Random.Next()) < Richness)) return EOutcome::Filler;
    return VoxelRandom::ToUnitFloat(Random.Next()) < Vein.RawOreChance ? EOutcome::RawOre : EOutcome::Ore;
}

void FVoxelOreVeins::Prepare(FVoxelOreVeinField& Field, bool bShortcut) const
{
    Field.Spans.SetNum(Veins.Num());
    int32 Total = 0;
    for (int32 Index = 0; Index < Veins.Num(); ++Index)
    {
        const FVoxelOreVein& Vein = Veins[Index];
        FVoxelOreVeinField::FSpan& Span = Field.Spans[Index];
        Span.Low = bShortcut ? Vein.MinZ : VoxelConstants::MinZ;
        const int32 Heights = bShortcut ? FMath::Max(0, Vein.MaxZ - Vein.MinZ + 1) : VoxelConstants::Height;
        Span.Words = (Heights + 63) / 64;
        Span.Offset = Total;
        Total += VeinArea * Span.Words * 3;
    }
    Field.Bits.Init(0, Total);
    FMemory::Memzero(Field.Columns, sizeof(Field.Columns));
    Field.bFilled = true;
    Field.OpenCells = Field.Corners = Field.Blocks = Field.Gaps = Field.Given = 0;
}

void FVoxelOreVeins::Write(FVoxelOreVeinField& Field, int32 VeinIndex, int32 Cell, int32 Z, EOutcome Outcome) const
{
    if (Outcome == EOutcome::Nothing) return;
    const FVoxelOreVeinField::FSpan& Span = Field.Spans[VeinIndex];
    const int32 Bit = Z - Span.Low;
    check(Bit >= 0 && Bit < Span.Words * 64);
    uint64* Words = Field.Bits.GetData() + Span.Offset + (Cell * Span.Words + (Bit >> 6)) * 3;
    const uint64 Mask = 1ull << (Bit & 63);
    Words[0] |= Mask;
    Field.Columns[Cell >> 6] |= 1ull << (Cell & 63);
    if (Outcome != EOutcome::Filler) Words[1] |= Mask;
    if (Outcome == EOutcome::RawOre) Words[2] |= Mask;
    ++Field.Given;
}

void FVoxelOreVeins::FinishGaps(FVoxelOreVeinScratch& Scratch, FVoxelOreVeinField& Field) const
{
    const int32 Count = Scratch.GapBlocks.Num();
    if (Count == 0) return;
    Field.Gaps += Count;
    Evaluator.Prepare(Scratch.Gaps, Count);
    Evaluator.EvaluateNodes(
        GapNodes, Scratch.GapX.GetData(), Scratch.GapY.GetData(), Scratch.GapZ.GetData(), Scratch.Gaps);
    for (int32 K = 0; K < Count; ++K)
    {
        const int32 VeinIndex = Scratch.GapVeins[K], Block = Scratch.GapBlocks[K];
        const float Gap = Evaluator.GetValues(Scratch.Gaps, Veins[VeinIndex].Gap)[K];
        Write(Field, VeinIndex, Scratch.BlockCells[Block], Scratch.BlockZ[Block],
            ApplyGap(EOutcome(Scratch.GapOutcomes[K]), Gap));
    }
}

void FVoxelOreVeins::Generate(int32 X0, int32 Y0, TConstArrayView<EVoxelSurfaceKind> Kinds, bool bShortcut,
    FVoxelOreVeinScratch& Scratch, FVoxelOreVeinField& Field) const
{
    constexpr int32 Height = VoxelConstants::Height, MinZ = VoxelConstants::MinZ;
    check(bCompiled && Kinds.Num() == VeinArea * Height);
    Prepare(Field, bShortcut);
    Scratch.BlockCells.Reset();
    Scratch.BlockZ.Reset();
    Scratch.GapBlocks.Reset();
    Scratch.GapVeins.Reset();
    Scratch.GapOutcomes.Reset();
    Scratch.GapX.Reset();
    Scratch.GapY.Reset();
    Scratch.GapZ.Reset();
    if (!bShortcut)
    {
        GenerateDirect(X0, Y0, Kinds, Scratch, Field);
        return;
    }
    if (SpanLow > SpanHigh) return;
    const FVoxelDensityGraph& Graph = Evaluator.GetGraph();
    const EVoxelSurfaceKind* const KindData = Kinds.GetData();
    // The veins at each height of the span, by bit.
    const auto ActiveAt = [this](int32 Z)
    {
        uint32 Mask = 0;
        for (int32 Index = 0; Index < Veins.Num(); ++Index)
            if (Z >= Veins[Index].MinZ && Z <= Veins[Index].MaxZ) Mask |= 1u << Index;
        return Mask;
    };

    // Cells: CellsXZ x CellsXZ per layer, the layers holding the span.
    const int32 CellsXZ = CornersXZ - 1, FlatCount = CornersXZ * CornersXZ;
    const int32 Layer0 = VeinFloorDiv(SpanLow - MinZ, CellY),
                Layers = VeinFloorDiv(SpanHigh - MinZ, CellY) - Layer0 + 1;
    const int32 CellCount = CellsXZ * CellsXZ * Layers, CornerCount = FlatCount * (Layers + 1);
    const auto CellIndex = [CellsXZ](int32 CX, int32 L, int32 CZ) { return (L * CellsXZ + CZ) * CellsXZ + CX; };
    const auto Bottom = [this, Layer0](int32 L) { return MinZ + (Layer0 + L) * CellY; };
    Scratch.CellOpen.SetNumUninitialized(CellCount, EAllowShrinking::No);
    for (int32 L = 0; L < Layers; ++L)
    {
        uint32 Active = 0;
        for (int32 DY = 0; DY < CellY; ++DY)
            Active |= ActiveAt(Bottom(L) + DY);
        for (int32 Cell = 0; Cell < CellsXZ * CellsXZ; ++Cell)
            Scratch.CellOpen[L * CellsXZ * CellsXZ + Cell] = Active != 0;
    }

    // Interpolated inputs at the corners, one node at a time; before each and after the last, cells whose densities
    // are certainly <= 0 by the corners known so far close.
    const int32 Slots = InterpolatedNodes.Num();
    Scratch.CornerValues.SetNumUninitialized(FMath::Max(1, Slots * CornerCount), EAllowShrinking::No);
    Scratch.Bounds.Init(FVoxelDensityBounds(), Graph.Nodes.Num());
    // Whether any vein's density can be positive in layer L where each interpolated node lies within the corner
    // values of the open cells in [CX0, CX1] x [CZ0, CZ1] (nodes not evaluated yet within their inputs' bounds).
    const auto Possible = [&](int32 Known, int32 L, int32 CX0, int32 CX1, int32 CZ0, int32 CZ1)
    {
        for (int32 Slot = 0; Slot < Slots; ++Slot)
        {
            if (Slot >= Known)
            {
                Scratch.Bounds[InterpolatedNodes[Slot]] = InputBounds[Slot];
                continue;
            }
            const float* Values = Scratch.CornerValues.GetData() + Slot * CornerCount;
            float Min = UE_BIG_NUMBER, Max = -UE_BIG_NUMBER;
            for (int32 CZ = CZ0; CZ <= CZ1; ++CZ)
                for (int32 CX = CX0; CX <= CX1; ++CX)
                {
                    if (!Scratch.CellOpen[CellIndex(CX, L, CZ)]) continue;
                    for (int32 Corner = 0; Corner < 8; ++Corner)
                    {
                        const float Value =
                            Values[CornerIndex(CX + (Corner & 1), L + (Corner >> 1 & 1), CZ + (Corner >> 2 & 1))];
                        Min = FMath::Min(Min, Value);
                        Max = FMath::Max(Max, Value);
                    }
                }
            // Trilinear weights are convex; the margin covers float rounding (as FVoxelColumnDensity).
            const float Margin = FMath::Max(FMath::Abs(Min), FMath::Abs(Max)) * 1e-5f + 1e-30f;
            Scratch.Bounds[InterpolatedNodes[Slot]] = {Min - Margin, Max + Margin};
        }
        Evaluator.EvaluateBounds(DensityNodes, Bottom(L), Bottom(L) + CellY - 1, Scratch.Bounds);
        bool bPossible = false;
        for (const FVoxelOreVein& Vein : Veins)
            bPossible |= !(Scratch.Bounds[Vein.Density].Max <= 0);
        return bPossible;
    };
    // A layer's open cells together first (most layers hold no vein), then the cells of a layer that may.
    const auto BoundCells = [&](int32 Known)
    {
        for (int32 L = 0; L < Layers; ++L)
        {
            int32 Open = 0;
            for (int32 Cell = 0; Cell < CellsXZ * CellsXZ; ++Cell)
                Open += Scratch.CellOpen[L * CellsXZ * CellsXZ + Cell];
            if (Open == 0) continue;
            const bool bLayer = Possible(Known, L, 0, CellsXZ - 1, 0, CellsXZ - 1);
            for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
                for (int32 CX = 0; CX < CellsXZ; ++CX)
                {
                    uint8& State = Scratch.CellOpen[CellIndex(CX, L, CZ)];
                    if (State) State = bLayer && (Open == 1 || Possible(Known, L, CX, CX, CZ, CZ));
                }
        }
    };
    for (int32 Slot = 0; Slot < Slots; ++Slot)
    {
        if (Slot > 0) BoundCells(Slot);
        Scratch.CornerNeeded.Init(0, CornerCount);
        for (int32 L = 0; L < Layers; ++L)
            for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
                for (int32 CX = 0; CX < CellsXZ; ++CX)
                    if (Scratch.CellOpen[CellIndex(CX, L, CZ)])
                        for (int32 Corner = 0; Corner < 8; ++Corner)
                            Scratch.CornerNeeded[CornerIndex(
                                CX + (Corner & 1), L + (Corner >> 1 & 1), CZ + (Corner >> 2 & 1))] = 1;
        Scratch.CornerPoints.Reset();
        for (int32 Index = 0; Index < CornerCount; ++Index)
            if (Scratch.CornerNeeded[Index]) Scratch.CornerPoints.Add(Index);
        const int32 Needed = Scratch.CornerPoints.Num();
        Field.Corners += Needed;
        if (Needed == 0) break;
        Scratch.X.SetNumUninitialized(Needed, EAllowShrinking::No);
        Scratch.Y.SetNumUninitialized(Needed, EAllowShrinking::No);
        Scratch.Z.SetNumUninitialized(Needed, EAllowShrinking::No);
        for (int32 K = 0; K < Needed; ++K)
        {
            const int32 Index = Scratch.CornerPoints[K];
            Scratch.X[K] = X0 + (Index % CornersXZ) * CellXZ;
            Scratch.Z[K] = Y0 + (Index / CornersXZ % CornersXZ) * CellXZ;
            Scratch.Y[K] = MinZ + (Layer0 + Index / FlatCount) * CellY;
        }
        Evaluator.Prepare(Scratch.Corners, Needed);
        Evaluator.EvaluateNodes(
            CornerNodes[Slot], Scratch.X.GetData(), Scratch.Y.GetData(), Scratch.Z.GetData(), Scratch.Corners);
        const TConstArrayView<float> Values =
            Evaluator.GetValues(Scratch.Corners, Graph.Nodes[InterpolatedNodes[Slot]].Inputs[0]);
        float* Target = Scratch.CornerValues.GetData() + Slot * CornerCount;
        for (int32 K = 0; K < Needed; ++K)
            Target[Scratch.CornerPoints[K]] = Values[K];
    }
    BoundCells(Slots);

    // Blocks: the solid blocks of open cells at heights some vein reaches.
    for (int32 L = 0; L < Layers; ++L)
        for (int32 CZ = 0; CZ < CellsXZ; ++CZ)
            for (int32 CX = 0; CX < CellsXZ; ++CX)
            {
                if (!Scratch.CellOpen[CellIndex(CX, L, CZ)]) continue;
                ++Field.OpenCells;
                for (int32 DY = 0; DY < CellY; ++DY)
                {
                    const int32 Z = Bottom(L) + DY;
                    if (!ActiveAt(Z)) continue;
                    for (int32 DZ = 0; DZ < CellXZ; ++DZ)
                        for (int32 DX = 0; DX < CellXZ; ++DX)
                        {
                            const int32 Cell = (CZ * CellXZ + DZ) * VeinColumnSize + CX * CellXZ + DX;
                            if (KindData[Cell * Height + Z - MinZ] != EVoxelSurfaceKind::Solid) continue;
                            Scratch.BlockCells.Add(Cell);
                            Scratch.BlockZ.Add(Z);
                        }
                }
            }
    const int32 Count = Scratch.BlockCells.Num();
    Field.Blocks = Count;
    if (Count == 0) return;
    Scratch.X.SetNumUninitialized(Count, EAllowShrinking::No);
    Scratch.Y.SetNumUninitialized(Count, EAllowShrinking::No);
    Scratch.Z.SetNumUninitialized(Count, EAllowShrinking::No);
    Evaluator.Prepare(Scratch.Blocks, Count);
    for (int32 K = 0; K < Count; ++K)
    {
        const int32 Cell = Scratch.BlockCells[K], Z = Scratch.BlockZ[K];
        const int32 LocalX = Cell % VeinColumnSize, LocalY = Cell / VeinColumnSize;
        Scratch.X[K] = X0 + LocalX;
        Scratch.Y[K] = Z;
        Scratch.Z[K] = Y0 + LocalY;
    }
    // Interpolated values as direct evaluation computes them: the cell's corners and float(D / size) weights.
    for (int32 Slot = 0; Slot < Slots; ++Slot)
    {
        const float* Values = Scratch.CornerValues.GetData() + Slot * CornerCount;
        float* Target = Evaluator.GetMutableValues(Scratch.Blocks, InterpolatedNodes[Slot]).GetData();
        for (int32 K = 0; K < Count; ++K)
        {
            const int32 Cell = Scratch.BlockCells[K], Z = Scratch.BlockZ[K];
            const int32 LocalX = Cell % VeinColumnSize, LocalY = Cell / VeinColumnSize;
            const int32 Above = Z - MinZ - Layer0 * CellY;
            const int32 CX = LocalX / CellXZ, CZ = LocalY / CellXZ, L = Above / CellY;
            float Corners[8];
            for (int32 Corner = 0; Corner < 8; ++Corner)
                Corners[Corner] = Values[CornerIndex(CX + (Corner & 1), L + (Corner >> 1 & 1), CZ + (Corner >> 2 & 1))];
            Target[K] = VoxelDensity::Trilinear(Corners, float(double(LocalX % CellXZ) / CellXZ),
                float(double(Above % CellY) / CellY), float(double(LocalY % CellXZ) / CellXZ));
        }
    }
    Evaluator.EvaluateNodes(BlockNodes, Scratch.X.GetData(), Scratch.Y.GetData(), Scratch.Z.GetData(), Scratch.Blocks);

    // The draws, and the gap where the richness draw passed.
    for (int32 K = 0; K < Count; ++K)
    {
        const int32 Cell = Scratch.BlockCells[K], Z = Scratch.BlockZ[K];
        const int32 X = X0 + Cell % VeinColumnSize, Y = Y0 + Cell / VeinColumnSize;
        for (uint32 Active = ActiveAt(Z); Active != 0; Active &= Active - 1)
        {
            const int32 VeinIndex = int32(FMath::CountTrailingZeros(Active));
            const FVoxelOreVein& Vein = Veins[VeinIndex];
            const EOutcome Outcome = Draw(Vein, Seed, X, Y, Z, Evaluator.GetValues(Scratch.Blocks, Vein.Density)[K],
                Evaluator.GetValues(Scratch.Blocks, Vein.Richness)[K]);
            if (Outcome == EOutcome::Ore || Outcome == EOutcome::RawOre)
            {
                Scratch.GapBlocks.Add(K);
                Scratch.GapVeins.Add(VeinIndex);
                Scratch.GapOutcomes.Add(uint8(Outcome));
                Scratch.GapX.Add(X);
                Scratch.GapY.Add(Z);
                Scratch.GapZ.Add(Y);
            }
            else
                Write(Field, VeinIndex, Cell, Z, Outcome);
        }
    }
    FinishGaps(Scratch, Field);
}

void FVoxelOreVeins::GenerateDirect(int32 X0, int32 Y0, TConstArrayView<EVoxelSurfaceKind> Kinds,
    FVoxelOreVeinScratch& Scratch, FVoxelOreVeinField& Field) const
{
    // Every solid block, a layer at a time, through the evaluator's own interpolation; every vein at every height.
    constexpr int32 Height = VoxelConstants::Height, MinZ = VoxelConstants::MinZ;
    if (Veins.IsEmpty()) return;
    for (int32 Z = MinZ; Z <= VoxelConstants::MaxZ; ++Z)
    {
        Scratch.BlockCells.Reset();
        for (int32 Cell = 0; Cell < VeinArea; ++Cell)
            if (Kinds[Cell * Height + Z - MinZ] == EVoxelSurfaceKind::Solid) Scratch.BlockCells.Add(Cell);
        const int32 Count = Scratch.BlockCells.Num();
        if (Count == 0) continue;
        Field.Blocks += Count;
        Scratch.X.SetNumUninitialized(Count, EAllowShrinking::No);
        Scratch.Y.SetNumUninitialized(Count, EAllowShrinking::No);
        Scratch.Z.SetNumUninitialized(Count, EAllowShrinking::No);
        for (int32 K = 0; K < Count; ++K)
        {
            Scratch.X[K] = X0 + Scratch.BlockCells[K] % VeinColumnSize;
            Scratch.Y[K] = Z;
            Scratch.Z[K] = Y0 + Scratch.BlockCells[K] / VeinColumnSize;
        }
        Evaluator.Evaluate(Scratch.X.GetData(), Scratch.Y.GetData(), Scratch.Z.GetData(), Count, Scratch.Blocks);
        for (int32 K = 0; K < Count; ++K)
            for (int32 VeinIndex = 0; VeinIndex < Veins.Num(); ++VeinIndex)
            {
                const FVoxelOreVein& Vein = Veins[VeinIndex];
                const int32 Cell = Scratch.BlockCells[K];
                const EOutcome Outcome = Draw(Vein, Seed, X0 + Cell % VeinColumnSize, Y0 + Cell / VeinColumnSize, Z,
                    Evaluator.GetValues(Scratch.Blocks, Vein.Density)[K],
                    Evaluator.GetValues(Scratch.Blocks, Vein.Richness)[K]);
                Write(Field, VeinIndex, Cell, Z, ApplyGap(Outcome, Evaluator.GetValues(Scratch.Blocks, Vein.Gap)[K]));
            }
    }
}
