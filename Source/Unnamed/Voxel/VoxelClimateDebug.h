#pragma once

#include "CoreMinimal.h"
#include "Terrain/VoxelClimate.h"

// Debug views of the terrain's climate (phase 4): top-down map images and the values at the player. The climate is not
// in the terrain yet (session 3), so these read the reference presets directly.
namespace VoxelClimateDebug
{
    // A preset's climate sampler for a seed, read from the reference and cached. Game thread only. Null, with the
    // reason in OutError, when the preset cannot be read or compiled.
    TSharedPtr<const FVoxelClimateSampler> GetSampler(const FString& Preset, uint64 Seed, FString& OutError);

    struct FMapOptions
    {
        EVoxelClimate Parameter = EVoxelClimate::Continents;
        int32 Size = 1024;
        double BlocksPerPixel = 8;
        // The map's center in blocks.
        double CenterX = 0;
        double CenterY = 0;
    };

    // Samples Size x Size points across every task worker and writes a PNG. North (+Y) is up. Offset is drawn as the
    // surface height it implies (the depth gradient's zero), with water below sea level; the other parameters on a
    // ramp over their usual range. OutSummary gets the timing and the value spread.
    bool WriteMap(const FVoxelClimateSampler& Sampler, const FMapOptions& Options, const FString& Path,
        FString& OutSummary, FString& OutError);

    // The height in blocks where the depth gradient cancels Offset: a surface estimate before factor, jaggedness and
    // the 3D noise.
    double EstimateSurface(float Offset);
} // namespace VoxelClimateDebug
