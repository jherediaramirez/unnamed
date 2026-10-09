#pragma once

#include "CoreMinimal.h"
#include "VoxelConstants.h"

// The surface rules' inputs, as 26.3's surface pass builds them (black-boxed: Docs/Records/
// 2026-10-09-Voxel-Phase5-Scout-Facts.md, section 1.2). Plain values, so tests can build them by hand.

// What a block holds before the rules run. The rules replace only solid blocks; their results do not feed back.
enum class EVoxelSurfaceKind : uint8
{
    Air,
    Solid,
    Water,
    Lava
};

// One block column's values (the reference's x and z are our X and Y).
struct FVoxelSurfaceColumn
{
    static constexpr int32 MaxNoises = 16;

    int32 X = 0, Y = 0;
    // trunc(surface * 2.75 + 3 + r * 0.25), r in [0, 1) a hash of (seed, X, Y).
    int32 SurfaceDepth = 0;
    // floor(P) + SurfaceDepth - 8, P the preliminary surface lerped across the column's 16-block cell.
    int32 MinSurfaceLevel = 0;
    // Steep (VoxelSurfaceContext::IsSteep).
    bool bSteep = false;
    // With the shortcut: the largest depth below a run's top (above its bottom) at which a floor (ceiling) stone_depth
    // condition the chunk can reach holds; deeper blocks are interior.
    int32 FloorReach = MIN_int32, CeilingReach = MIN_int32;
    // 2D noise values at (X, 0, Y) by the evaluator's noise slot; NoiseKnown has a bit per slot already filled. The
    // evaluator samples a missing one on first use.
    double Noise[MaxNoises] = {};
    uint32 NoiseKnown = 0;
    // round(clay_bands_offset * 4), once BandOffsetKnown.
    int32 BandOffset = 0;
    bool bBandOffsetKnown = false;

    void SetNoise(int32 Slot, double Value)
    {
        Noise[Slot] = Value;
        NoiseKnown |= 1u << Slot;
    }
};

// One block's values.
struct FVoxelSurfaceBlock
{
    static constexpr int32 NoWater = MIN_int32;
    static constexpr int32 UnknownBiome = -2;

    int32 Z = 0;
    // Solid blocks from the top of the current run down to this one, counting it. Air resets it; fluid is neither
    // counted nor resets it.
    int32 DepthAbove = 0;
    // Solid blocks from this one down to the next air or fluid, counting it; the world's bottom ends the run.
    int32 DepthBelow = 0;
    // Going down, set at the first fluid after the last air to that fluid's Z + 1; air resets it, solid does not.
    int32 WaterHeight = NoWater;
    // The block's biome index (INDEX_NONE for none); UnknownBiome until read from the storage, once.
    int32 Biome = UnknownBiome;
};

namespace VoxelSurfaceContext
{
    // Kinds bottom-up (index Z - MinZ) into DepthBelow (0 where not solid): counted up from each run's bottom, which
    // is the count from the block down.
    inline void ComputeDepthBelow(TConstArrayView<EVoxelSurfaceKind> Kinds, TArrayView<int32> Out)
    {
        int32 Run = 0;
        for (int32 Index = 0; Index < Kinds.Num(); ++Index)
        {
            Run = Kinds[Index] == EVoxelSurfaceKind::Solid ? Run + 1 : 0;
            Out[Index] = Run;
        }
    }

    // 26.3's steep test at (LocalX, LocalY) of a Size x Size grid of heights (Heights[LocalY * Size + LocalX], the top
    // non-air block, fluids included): the height 1 south minus 1 north is 4 or more, or 1 west minus 1 east is 4 or
    // more. Neighbours clamp to the grid, so a column never reads another column's blocks.
    inline bool IsSteep(TConstArrayView<int32> Heights, int32 Size, int32 LocalX, int32 LocalY)
    {
        const auto At = [&](int32 X, int32 Y)
        { return Heights[FMath::Clamp(Y, 0, Size - 1) * Size + FMath::Clamp(X, 0, Size - 1)]; };
        return At(LocalX, LocalY + 1) >= At(LocalX, LocalY - 1) + 4 ||
            At(LocalX - 1, LocalY) >= At(LocalX + 1, LocalY) + 4;
    }

    // The preliminary surface at a block of a column cell from its four corners (C00 at the minimum corner, C10 at +X,
    // C01 at +Y), lerped by Fraction = local / CellSize as the jar does.
    inline double LerpPreliminary(double FractionX, double FractionY, double C00, double C10, double C01, double C11)
    {
        const double North = C00 + (C10 - C00) * FractionX;
        const double South = C01 + (C11 - C01) * FractionX;
        return North + (South - North) * FractionY;
    }
} // namespace VoxelSurfaceContext
