#pragma once

#include "CoreMinimal.h"

class FVoxelBiomeTable;
class FVoxelBlockTable;
class IVoxelTerrain;
struct FUnnamedSavedWorld;
struct FVoxelSurfaceInputs;
struct FVoxelTerrainPreset;

// Builds the terrain a world generates. The voxel world uses it to stream, the menu to check a world before
// travelling to it (Create and Play), so both decide alike.
namespace VoxelTerrainFactory
{
    // Superflat's layers, generator 1's stand-in, or generator 2's 26.3 preset for the world type with the overworld
    // biome parameter list and the preset's surface rules (Single Biome: the world's one biome), holding the decoration
    // data (VoxelWorldgen::ReadFeatures) for the decoration stages. A saved world never falls back: when its preset,
    // the parameter list, the rules or the decoration data cannot be read or compiled, or its single biome is not an
    // overworld biome, this returns null with the reason, because another terrain under a saved world would
    // later carry its edits on the wrong land. Only the dev seed (World null) falls back to the stand-in, with an error
    // in the log.
    UNNAMED_API TSharedPtr<const IVoxelTerrain> Make(const FUnnamedSavedWorld* World, int64 Seed,
        const FVoxelBlockTable& Table, const FString& ReferenceRoot, FString& OutError);

    // Whether World's terrain can be built from the worldgen reference at ReferenceRoot. Game thread only.
    UNNAMED_API bool Check(const FUnnamedSavedWorld& World, const FString& ReferenceRoot, FString& OutError);

    // The 26.3 preset a generator-2 world type uses: "large_biomes", "amplified", else "overworld" (Single Biome too).
    UNNAMED_API const TCHAR* GetPresetName(const FString& Type);

    // Reads what a generator-2 terrain needs: the preset, the overworld biome parameter list, the preset's surface
    // rules with their noises, and the block reference's grass and foliage colormaps (into OutBiomes) for the tints.
    // Game thread only (it loads the image module).
    UNNAMED_API bool ReadInputs(const FString& ReferenceRoot, const FString& PresetName, FVoxelTerrainPreset& OutPreset,
        FVoxelBiomeTable& OutBiomes, FVoxelSurfaceInputs& OutSurface, FString& OutError);

    // The block reference's colormaps folder (grass.png, foliage.png, dry_foliage.png), staged with packages.
    UNNAMED_API FString GetColormapRoot();
} // namespace VoxelTerrainFactory
