#pragma once

#include "CoreMinimal.h"
#include "Noise/VoxelNoise.h"

struct FVoxelBiome;

// Per-block temperature and the snow line: 26.3's height-adjusted temperature (black-boxed in
// Docs/Records/2026-10-09-Voxel-Phase5-Scout-Facts.md, section 3) through our own noises. Up to sea level + 17 a block
// has its biome's temperature; above, it loses 0.05 per 40 blocks, moved up or down by a wobble of about 8 blocks.
// The frozen modifier gives the frozen oceans open-water patches at 0.2. The climate noises take no world seed, as in
// 26.3: every world shares the same snow-line wobble and frozen patches. Immutable; any thread may use it.
class UNNAMEDVOXEL_API FVoxelBiomeClimate
{
public:
    // Snow falls and still water freezes below this temperature (strictly: 0.15 itself is not cold).
    static constexpr float SnowTemperature = 0.15f;
    // The height adjustment starts above sea level plus this.
    static constexpr int32 SnowLineOffset = 17;
    // The wobble's amplitude in blocks.
    static constexpr double WobbleBlocks = 8;
    // The temperature a frozen biome takes in its open-water patches.
    static constexpr float FrozenPatchTemperature = 0.2f;

    FVoxelBiomeClimate();

    // The biome's temperature at a column: its own, or with the frozen modifier FrozenPatchTemperature where
    // Frozen * 7 + Info(0.2) < 0.3 and Info(0.09) < 0.8.
    float GetBaseTemperature(const FVoxelBiome& Biome, int32 X, int32 Y) const;
    // At a block (our axes: Z up), with the world type's sea level.
    float GetTemperature(const FVoxelBiome& Biome, int32 X, int32 Y, int32 Z, int32 SeaLevel) const;
    // The height rule alone: Base above SeaLevel + SnowLineOffset minus (Wobble + Z - (SeaLevel + 17)) * 0.05 / 40, in
    // float as the jar computes it.
    static float AdjustForHeight(float Base, int32 Z, int32 SeaLevel, float Wobble);
    static bool IsColdEnoughToSnow(float Temperature) { return Temperature < SnowTemperature; }

    // The wobble in blocks at a column, within +-WobbleBlocks.
    float GetWobble(int32 X, int32 Y) const;
    // The frozen modifier's noise (one octave, features about 20 blocks across), in [-1, 1].
    double GetFrozenNoise(int32 X, int32 Y) const;
    // The biome info noise in [-1, 1] at a position already scaled by the caller (the modifier reads it at 0.2 and 0.09
    // per block; session 6's swamp grass reads it too).
    double GetInfoNoise(double X, double Y) const;

private:
    FVoxelNoise Wobble, Frozen, Info;
};
