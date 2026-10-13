#pragma once

#include "CoreMinimal.h"

// The reference's temperature_modifier: frozen biomes (the frozen oceans) vary their temperature with position.
enum class EVoxelTemperatureModifier : uint8
{
    None,
    Frozen
};

// The reference's effects.grass_color_modifier.
enum class EVoxelGrassColorModifier : uint8
{
    None,
    DarkForest,
    Swamp
};

// The six climate parameters, in the reference's order.
enum class EVoxelClimateAxis : uint8
{
    Temperature,
    Humidity,
    Continentalness,
    Erosion,
    Depth,
    Weirdness,
    Count
};

// One biome from the reference's biome files: the numbers and colours generation and tinting read, and the water fog.
// Spawns, sounds, the other attributes, features and carvers are not read.
struct FVoxelBiome
{
    // The full id, "minecraft:plains".
    FString Name;
    float Temperature = 0;
    float Downfall = 0;
    bool bHasPrecipitation = false;
    EVoxelTemperatureModifier TemperatureModifier = EVoxelTemperatureModifier::None;
    // Colours as 0xRRGGBB. The optional ones are INDEX_NONE when the biome takes them from the colormaps.
    int32 WaterColor = 0;
    int32 GrassColor = INDEX_NONE;
    int32 FoliageColor = INDEX_NONE;
    int32 DryFoliageColor = INDEX_NONE;
    EVoxelGrassColorModifier GrassColorModifier = EVoxelGrassColorModifier::None;
    // From the file's attributes: minecraft:visual/water_fog_color as 0xRRGGBB, INDEX_NONE when unset (the default
    // applies, VoxelWaterFog.h), and water_fog_end_distance's multiplier (swamps 0.85), 1 when unset.
    int32 WaterFogColor = INDEX_NONE;
    float WaterFogEndScale = 1;
};

// One point of a biome parameter list: a box over the six climate parameters plus an offset, in the jar's quantized
// units (x Quantization). A target inside a range is 0 away from it; outside, the distance to the nearer end.
struct FVoxelBiomeParameter
{
    int64 Min[int32(EVoxelClimateAxis::Count)] = {};
    int64 Max[int32(EVoxelClimateAxis::Count)] = {};
    int64 Offset = 0;
    // Index into FVoxelBiomeTable::Biomes.
    int32 Biome = INDEX_NONE;
};

// A world type's biomes and its parameter list as plain data. Points keep the list's order, because the order decides
// ties: of equally fitting points, the first wins.
class UNNAMEDVOXEL_API FVoxelBiomeTable
{
public:
    // Climate values are stored as whole numbers: value x 10,000, truncated toward zero, as the jar quantizes them.
    static constexpr int64 Quantization = 10000;

    // The parameter list's preset, "minecraft:overworld".
    FString Preset;
    TArray<FVoxelBiome> Biomes;
    TArray<FVoxelBiomeParameter> Points;
    // The block reference's grass and foliage colormaps, ColormapSize x ColormapSize colours as 0xRRGGBB, row by row
    // from the top left; empty when not read (no biome tints). The dry foliage colormap (leaf litter) is optional:
    // without it, dry foliage keeps its plains tint except where a biome sets dry_foliage_color.
    static constexpr int32 ColormapSize = 256;
    TArray<uint32> GrassColormap;
    TArray<uint32> FoliageColormap;
    TArray<uint32> DryFoliageColormap;

    // A biome's index by full id, or INDEX_NONE.
    int32 FindBiome(FStringView Name) const;

    // Checks unique non-empty names, finite numbers, colours in 0..0xFFFFFF, every range's min <= max, every point's
    // biome index, and colormaps that are both empty or both full.
    bool Validate(FString* OutError = nullptr) const;

    static int64 Quantize(float Value) { return int64(Value * float(Quantization)); }
};
