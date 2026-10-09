#pragma once

#include "CoreMinimal.h"

class FVoxelBiomeSource;

// Debug views of the biomes (phase 5): top-down biome maps, the readout at the player and the on-screen minimap.
namespace VoxelBiomeDebug
{
    // A preset's biome source (the overworld parameter list over the preset's climate) for a seed, read from the
    // reference and cached. Game thread only. Null, with the reason in OutError, when it cannot be built.
    TSharedPtr<const FVoxelBiomeSource> GetSource(const FString& Preset, uint64 Seed, FString& OutError);

    // A biome's colour from UVoxelWorldSettings::BiomeMapColors. A biome without one gets a colour hashed from its id,
    // and bOutListed false.
    FColor GetColor(const FString& Biome, bool* bOutListed = nullptr);

    struct FMapOptions
    {
        int32 Size = 1024;
        double BlocksPerPixel = 8;
        // The map's center in blocks.
        double CenterX = 0;
        double CenterY = 0;
        // The height the biomes are read at; unset reads them at depth 0, the climate's own surface (where the depth
        // gradient cancels the offset).
        TOptional<double> Height;
    };

    // Samples Size x Size points across every task worker, writes the PNG (north, -Y, up) and a CSV beside it with
    // each biome's share of the map and of the jar's depth-0 lookup samples. OutSummary gets the timing, the biome
    // count and the largest shares.
    bool WriteMap(const FVoxelBiomeSource& Source, const FMapOptions& Options, const FString& Path, FString& OutSummary,
        FString& OutError);

    // Whether an on-screen biome view (voxel.Debug.Biomes or voxel.Debug.Minimap) is on. Off, they register nothing:
    // no draw hook, no task, no texture. Voxel shots keep the UI while one is on, so they show it.
    bool IsOverlayOn();
} // namespace VoxelBiomeDebug
