#include "Terrain/VoxelAquifer.h"
#include "Density/VoxelDensityGraph.h"
#include "Random/VoxelRandom.h"

namespace
{
    constexpr uint64 AquiferCenterSalt = VoxelRandom::Salt("aquifer/center");
    // A dry cell's level: below the world, so it never holds fluid.
    constexpr int32 AquiferDryLevel = -4096;
    // Centres this close under the preliminary surface, or above it, take the global fluid.
    constexpr int32 AquiferSurfaceMargin = 12;
    // Floodedness thresholds deep underground and right under a sea floor (closeness 0 and 1): above Full the cell
    // takes the global fluid, above Partial a level of its own, else it is dry.
    constexpr float AquiferFullDeep = 0.8f, AquiferFullShallow = -0.3f;
    constexpr float AquiferPartialDeep = 0.4f, AquiferPartialShallow = -0.8f;
    // How far under a sea floor closeness falls to 0.
    constexpr float AquiferClosenessDepth = 64;
    constexpr int32 AquiferBand = 40;
    constexpr int32 AquiferLavaLevel = -10;
    constexpr float AquiferLavaThreshold = 0.3f;
    // Squared-distance difference at which a second centre stops mattering.
    constexpr float AquiferBlend = 25;

    int32 AquiferFloorDiv(int32 Value, int32 Divisor)
    {
        return FMath::FloorToInt32(Value / double(Divisor));
    }

    bool AquiferCompile(FVoxelDensityEvaluator& Evaluator, const FVoxelDensityGraph& Graph, uint64 Seed, int32 Node,
        const TCHAR* Name, FString* OutError)
    {
        FString Reason;
        if (Node == INDEX_NONE)
            Reason = TEXT("no input");
        else if (Evaluator.Compile(Graph, Seed, {Node}, &Reason))
            return true;
        if (OutError) *OutError = FString::Printf(TEXT("aquifer %s: %s"), Name, *Reason);
        return false;
    }

    // Evaluates Node at Count points already in Scratch's X, Y and Z.
    TConstArrayView<float> AquiferRun(
        const FVoxelDensityEvaluator& Evaluator, int32 Node, FVoxelAquiferScratch& Scratch, int32 Count, int32 Slot)
    {
        FVoxelDensityScratch& Points = Scratch.Points[Slot];
        Evaluator.Evaluate(Scratch.X.GetData(), Scratch.Y.GetData(), Scratch.Z.GetData(), Count, Points);
        return Evaluator.GetValues(Points, Node);
    }

    void AquiferPoints(FVoxelAquiferScratch& Scratch, int32 Count)
    {
        Scratch.X.SetNumUninitialized(Count, EAllowShrinking::No);
        Scratch.Y.SetNumUninitialized(Count, EAllowShrinking::No);
        Scratch.Z.SetNumUninitialized(Count, EAllowShrinking::No);
    }
} // namespace

bool FVoxelAquifer::Initialize(const FVoxelTerrainPreset& Preset, uint64 InSeed, FString* OutError)
{
    Seed = InSeed;
    SeaLevel = Preset.SeaLevel;
    MinY = Preset.MinY;
    Height = Preset.Height;
    FirstLayer = LayerOf(MinY) - 1;
    Layers = LayerOf(MinY + Height - 1) + 1 - FirstLayer + 1;
    const FVoxelTerrainPreset::FAquifers& In = Preset.Aquifers;
    const FVoxelDensityGraph& Graph = Preset.Graph;
    BarrierNode = In.Barrier;
    ExclusionNode = In.Exclusion;
    FloodednessNode = In.FluidLevelFloodedness;
    SpreadNode = In.FluidLevelSpread;
    LavaNode = In.Lava;
    SurfaceNode = In.SurfaceLevel;
    return AquiferCompile(Barrier, Graph, Seed, BarrierNode, TEXT("barrier"), OutError) &&
        AquiferCompile(Exclusion, Graph, Seed, ExclusionNode, TEXT("exclusion"), OutError) &&
        AquiferCompile(Floodedness, Graph, Seed, FloodednessNode, TEXT("fluid_level_floodedness"), OutError) &&
        AquiferCompile(Spread, Graph, Seed, SpreadNode, TEXT("fluid_level_spread"), OutError) &&
        AquiferCompile(Lava, Graph, Seed, LavaNode, TEXT("lava"), OutError) &&
        AquiferCompile(Surface, Graph, Seed, SurfaceNode, TEXT("surface_level"), OutError);
}

FVoxelAquiferStatus FVoxelAquifer::GetGlobal(int32 Y) const
{
    return Y < LavaLine ? FVoxelAquiferStatus{LavaLine, EVoxelAquiferFluid::Lava}
                        : FVoxelAquiferStatus{SeaLevel, EVoxelAquiferFluid::Water};
}

float FVoxelAquifer::GetWall(const FVoxelAquiferStatus& A, const FVoxelAquiferStatus& B, int32 Y, float Noise) const
{
    const EVoxelAquiferFluid FA = A.At(Y), FB = B.At(Y);
    if ((FA == EVoxelAquiferFluid::Lava && FB == EVoxelAquiferFluid::Water) ||
        (FA == EVoxelAquiferFluid::Water && FB == EVoxelAquiferFluid::Lava))
        return 2;
    const int32 Gap = FMath::Abs(A.Level - B.Level);
    if (Gap == 0) return 0;
    // How far the block sits inside the band between the two levels: positive inside, negative outside, where the
    // wall fades three times slower.
    const float Inside = 0.5f * Gap - FMath::Abs(Y + 0.5f - 0.5f * (A.Level + B.Level));
    const float Wall = Inside > 0 ? Inside / 2 : Inside / 6;
    return 2 * (Wall + (FMath::Abs(Wall) <= 2 ? Noise : 0));
}

void FVoxelAquifer::Prepare(int32 X0, int32 Z0, FVoxelAquiferScratch& Scratch) const
{
    check(Surface.IsCompiled());
    const int32 CX0 = AquiferFloorDiv(X0, CellXZ) - 1, CZ0 = AquiferFloorDiv(Z0, CellXZ) - 1;
    const int32 Count = Layers * 9;

    // Each column of cells' preliminary surface, at its middle.
    AquiferPoints(Scratch, 9);
    for (int32 Index = 0; Index < 9; ++Index)
    {
        Scratch.X[Index] = (CX0 + Index % 3) * CellXZ + CellXZ / 2;
        Scratch.Y[Index] = 0;
        Scratch.Z[Index] = (CZ0 + Index / 3) * CellXZ + CellXZ / 2;
    }
    const TConstArrayView<float> SurfaceValues = AquiferRun(Surface, SurfaceNode, Scratch, 9, 0);
    Scratch.Surface.SetNumUninitialized(9, EAllowShrinking::No);
    for (int32 Index = 0; Index < 9; ++Index)
        Scratch.Surface[Index] = SurfaceValues[Index];

    // Centres; those near or above the surface take the global fluid, the rest wait for floodedness.
    Scratch.Centers.SetNumUninitialized(Count, EAllowShrinking::No);
    Scratch.Statuses.SetNumUninitialized(Count, EAllowShrinking::No);
    Scratch.Candidates.Reset();
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const int32 CX = CX0 + Index % 3, CZ = CZ0 + Index / 3 % 3, CL = FirstLayer + Index / 9;
        const uint64 Hash = VoxelRandom::Hash64(Seed, AquiferCenterSalt, CX, CL, CZ);
        const FIntVector Center(CX * CellXZ + 2 + VoxelRandom::ToIndex(Hash, CellXZ - 4),
            CL * CellY + 1 + VoxelRandom::ToIndex(Hash >> 21, CellY - 2),
            CZ * CellXZ + 2 + VoxelRandom::ToIndex(Hash >> 42, CellXZ - 4));
        Scratch.Centers[Index] = Center;
        if (Center.Y >= FMath::FloorToInt32(Scratch.Surface[Index % 9]) - AquiferSurfaceMargin)
            Scratch.Statuses[Index] = GetGlobal(Center.Y);
        else
            Scratch.Candidates.Add(Index);
    }

    // Floodedness: global fluid, a level of its own (kept as a candidate, Level 1 marks "own level") or dry.
    if (const int32 Candidates = Scratch.Candidates.Num())
    {
        AquiferPoints(Scratch, Candidates);
        for (int32 K = 0; K < Candidates; ++K)
        {
            const FIntVector& Center = Scratch.Centers[Scratch.Candidates[K]];
            Scratch.X[K] = Center.X;
            Scratch.Y[K] = Center.Y;
            Scratch.Z[K] = Center.Z;
        }
        const TConstArrayView<float> Flood = AquiferRun(Floodedness, FloodednessNode, Scratch, Candidates, 1);
        int32 Wet = 0;
        for (int32 K = 0; K < Candidates; ++K)
        {
            const int32 Index = Scratch.Candidates[K];
            const int32 Floor = FMath::FloorToInt32(Scratch.Surface[Index % 9]);
            const float Closeness = Floor < SeaLevel
                ? FMath::Clamp(1 - float(Floor - Scratch.Centers[Index].Y) / AquiferClosenessDepth, 0.f, 1.f)
                : 0.f;
            const float Value = FMath::Clamp(Flood[K], -1.f, 1.f);
            const float Full = FMath::Lerp(AquiferFullDeep, AquiferFullShallow, Closeness);
            const float Partial = FMath::Lerp(AquiferPartialDeep, AquiferPartialShallow, Closeness);
            FVoxelAquiferStatus& Status = Scratch.Statuses[Index];
            if (Value <= Partial)
            {
                Status = {AquiferDryLevel, EVoxelAquiferFluid::Air};
                continue;
            }
            Status = {Value > Full ? 0 : 1, EVoxelAquiferFluid::Air};
            Scratch.Candidates[Wet] = Index;
            Scratch.X[Wet] = Scratch.X[K];
            Scratch.Y[Wet] = Scratch.Y[K];
            Scratch.Z[Wet] = Scratch.Z[K];
            ++Wet;
        }
        Scratch.Candidates.SetNum(Wet, EAllowShrinking::No);
    }

    // Exclusion keeps a wet candidate dry; the rest take the global fluid or a level of their own.
    if (const int32 Candidates = Scratch.Candidates.Num())
    {
        const TConstArrayView<float> Excluded = AquiferRun(Exclusion, ExclusionNode, Scratch, Candidates, 2);
        int32 Own = 0;
        for (int32 K = 0; K < Candidates; ++K)
        {
            const int32 Index = Scratch.Candidates[K];
            FVoxelAquiferStatus& Status = Scratch.Statuses[Index];
            if (Excluded[K] > 0)
                Status = {AquiferDryLevel, EVoxelAquiferFluid::Air};
            else if (Status.Level == 0)
                Status = GetGlobal(Scratch.Centers[Index].Y);
            else
                Scratch.Candidates[Own++] = Index;
        }
        Scratch.Candidates.SetNum(Own, EAllowShrinking::No);
    }

    // A level of its own: spread per 16 x 40 x 16 blocks, lava per 64 x 40 x 64 where deep enough.
    if (const int32 Candidates = Scratch.Candidates.Num())
    {
        AquiferPoints(Scratch, Candidates);
        for (int32 K = 0; K < Candidates; ++K)
        {
            const FIntVector& Center = Scratch.Centers[Scratch.Candidates[K]];
            Scratch.X[K] = AquiferFloorDiv(Center.X, CellXZ);
            Scratch.Y[K] = AquiferFloorDiv(Center.Y, AquiferBand);
            Scratch.Z[K] = AquiferFloorDiv(Center.Z, CellXZ);
        }
        const TConstArrayView<float> SpreadValues = AquiferRun(Spread, SpreadNode, Scratch, Candidates, 3);
        for (int32 K = 0; K < Candidates; ++K)
        {
            const int32 Index = Scratch.Candidates[K];
            const int32 Band = AquiferFloorDiv(Scratch.Centers[Index].Y, AquiferBand);
            const int32 Level =
                Band * AquiferBand + AquiferBand / 2 + FMath::FloorToInt32(SpreadValues[K] * 10 / 3) * 3;
            Scratch.Statuses[Index] = {
                FMath::Min(Level, FMath::FloorToInt32(Scratch.Surface[Index % 9])), EVoxelAquiferFluid::Water};
        }
        for (int32 K = 0; K < Candidates; ++K)
        {
            const FIntVector& Center = Scratch.Centers[Scratch.Candidates[K]];
            Scratch.X[K] = AquiferFloorDiv(Center.X, 64);
            Scratch.Z[K] = AquiferFloorDiv(Center.Z, 64);
        }
        const TConstArrayView<float> LavaValues = AquiferRun(Lava, LavaNode, Scratch, Candidates, 4);
        for (int32 K = 0; K < Candidates; ++K)
        {
            FVoxelAquiferStatus& Status = Scratch.Statuses[Scratch.Candidates[K]];
            if (Status.Level <= AquiferLavaLevel && FMath::Abs(LavaValues[K]) > AquiferLavaThreshold)
                Status.Fluid = EVoxelAquiferFluid::Lava;
        }
    }

    // A layer is uniform when the 27 cells its blocks search all agree: no walls, no distances.
    Scratch.Uniform.Init(0, Layers);
    for (int32 Layer = 1; Layer + 1 < Layers; ++Layer)
    {
        const FVoxelAquiferStatus& First = Scratch.Statuses[(Layer - 1) * 9];
        bool bUniform = true;
        for (int32 Index = (Layer - 1) * 9; Index < (Layer + 2) * 9 && bUniform; ++Index)
            bUniform = Scratch.Statuses[Index] == First;
        Scratch.Uniform[Layer] = bUniform;
    }
    Scratch.KeepEmpty.SetNumUninitialized(Height, EAllowShrinking::No);
    for (int32 Y = MinY; Y < MinY + Height; ++Y)
        Scratch.KeepEmpty[Y - MinY] = Y >= LavaLine && !Scratch.Uniform[LayerOf(Y) - FirstLayer];
}

void FVoxelAquifer::Fill(int32 X0, int32 Z0, TConstArrayView<float> Density, FVoxelAquiferScratch& Scratch,
    TArray<EVoxelAquiferFluid>& Out) const
{
    constexpr int32 Size = 16;
    check(Density.Num() == Size * Size * Height && Scratch.Statuses.Num() == Layers * 9);
    Out.SetNumUninitialized(Density.Num(), EAllowShrinking::No);
    Scratch.Pending.Reset();
    for (int32 Z = 0; Z < Size; ++Z)
        for (int32 X = 0; X < Size; ++X)
        {
            // Open to the sky until the first solid block from the top.
            bool bSky = true;
            for (int32 Y = MinY + Height - 1; Y >= MinY; --Y)
            {
                const int32 Block = ((Y - MinY) * Size + Z) * Size + X;
                EVoxelAquiferFluid& Result = Out[Block];
                if (Density[Block] > 0)
                {
                    Result = EVoxelAquiferFluid::Air;
                    bSky = false;
                    continue;
                }
                if (bSky || Y < LavaLine)
                {
                    Result = GetGlobal(Y).At(Y);
                    continue;
                }
                const int32 Layer = LayerOf(Y) - FirstLayer;
                if (Scratch.Uniform[Layer])
                {
                    Result = Scratch.Statuses[Layer * 9].At(Y);
                    continue;
                }
                // The three nearest centres among the 27 around the block.
                int32 Cells[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
                int32 Distances[3] = {MAX_int32, MAX_int32, MAX_int32};
                const FIntVector Position(X0 + X, Y, Z0 + Z);
                for (int32 Index = (Layer - 1) * 9; Index < (Layer + 2) * 9; ++Index)
                {
                    const FIntVector Delta = Scratch.Centers[Index] - Position;
                    const int32 Distance = Delta.X * Delta.X + Delta.Y * Delta.Y + Delta.Z * Delta.Z;
                    if (Distance >= Distances[2]) continue;
                    int32 Slot = 2;
                    for (; Slot > 0 && Distance < Distances[Slot - 1]; --Slot)
                    {
                        Distances[Slot] = Distances[Slot - 1];
                        Cells[Slot] = Cells[Slot - 1];
                    }
                    Distances[Slot] = Distance;
                    Cells[Slot] = Index;
                }
                const FVoxelAquiferStatus& Nearest = Scratch.Statuses[Cells[0]];
                Result = Nearest.At(Y);
                const float Near2 = 1 - (Distances[1] - Distances[0]) / AquiferBlend;
                const float Near3 = 1 - (Distances[2] - Distances[0]) / AquiferBlend;
                const bool bDiffer2 = Near2 > 0 && !(Scratch.Statuses[Cells[1]] == Nearest);
                const bool bDiffer3 = Near3 > 0 && !(Scratch.Statuses[Cells[2]] == Nearest);
                if (bDiffer2 || bDiffer3) Scratch.Pending.Add({Block, {Cells[0], Cells[1], Cells[2]}, {Near2, Near3}});
            }
        }

    // Walls: the barrier noise only where two or three different statuses meet.
    const int32 Count = Scratch.Pending.Num();
    if (Count == 0) return;
    AquiferPoints(Scratch, Count);
    for (int32 K = 0; K < Count; ++K)
    {
        const int32 Block = Scratch.Pending[K].Block;
        Scratch.X[K] = X0 + Block % Size;
        Scratch.Z[K] = Z0 + Block / Size % Size;
        Scratch.Y[K] = MinY + Block / (Size * Size);
    }
    const TConstArrayView<float> Noise = AquiferRun(Barrier, BarrierNode, Scratch, Count, 5);
    for (int32 K = 0; K < Count; ++K)
    {
        const FVoxelAquiferScratch::FPending& Item = Scratch.Pending[K];
        const int32 Y = int32(Scratch.Y[K]);
        const float Value = Density[Item.Block];
        const FVoxelAquiferStatus& Nearest = Scratch.Statuses[Item.Cells[0]];
        const bool bWall =
            (Item.Weights[0] > 0 &&
                Value + Item.Weights[0] * GetWall(Nearest, Scratch.Statuses[Item.Cells[1]], Y, Noise[K]) > 0) ||
            (Item.Weights[1] > 0 &&
                Value + Item.Weights[1] * GetWall(Nearest, Scratch.Statuses[Item.Cells[2]], Y, Noise[K]) > 0);
        if (bWall) Out[Item.Block] = EVoxelAquiferFluid::Barrier;
    }
}
