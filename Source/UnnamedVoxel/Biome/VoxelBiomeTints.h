#pragma once

#include "CoreMinimal.h"
#include "Biome/VoxelBiomeClimate.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"

// One biome's tint colours as 0xRRGGBB, before the grass modifier (which reads the block's position).
struct FVoxelBiomeTint
{
    uint32 Water = 0;
    uint32 Grass = 0;
    uint32 Foliage = 0;
    EVoxelGrassColorModifier GrassModifier = EVoxelGrassColorModifier::None;
};

// 26.3's biome colours (the scout's facts, section 5), per biome of a table, from its data and the block reference's
// colormaps:
// - water: the biome's water_color;
// - grass: grass_color, or the grass colormap at the biome's clamped temperature and downfall; then the modifier:
//   dark forest blends halfway toward #28340A, swamp gives #4C763C or #6A7039 by the info noise at the block;
// - foliage: foliage_color, or the foliage colormap.
// The base temperature is the biome's own: height, the snow line and the frozen modifier play no part. Immutable; any
// thread may use it.
class UNNAMEDVOXEL_API FVoxelTintTable
{
public:
    static constexpr uint32 SwampDark = 0x4C763C;
    static constexpr uint32 SwampLight = 0x6A7039;
    // Swamp grass is dark where the info noise at (x, y) * SwampScale is below SwampThreshold.
    static constexpr double SwampScale = 0.0225;
    static constexpr double SwampThreshold = -0.1;

    // Needs both colormaps (FVoxelBiomeTable::ColormapSize squared colours each); false leaves the table empty.
    bool Initialize(const FVoxelBiomeTable& Table, FString* OutError = nullptr);
    bool IsInitialized() const { return !Biomes.IsEmpty(); }
    int32 Num() const { return Biomes.Num(); }
    const FVoxelBiomeTint& Get(int32 Biome) const { return Biomes[Biome]; }

    // The colour a block at (X, Y) of the biome takes for Source (grass, foliage or water); INDEX_NONE or another
    // source gives Fallback.
    uint32 GetColor(EVoxelTintSource Source, int32 Biome, int32 X, int32 Y, uint32 Fallback) const;
    // Whether a source's colour in the biome depends on the block's position (swamp grass).
    bool VariesWithPosition(EVoxelTintSource Source, int32 Biome) const
    {
        return Source == EVoxelTintSource::Grass && Biomes.IsValidIndex(Biome) &&
            Biomes[Biome].GrassModifier == EVoxelGrassColorModifier::Swamp;
    }
    // The swamp's grass at a block.
    uint32 GetSwampGrass(int32 X, int32 Y) const;

    // GrassColor.get / FoliageColor.get: the colormap pixel for a temperature and downfall already clamped to [0, 1].
    static uint32 LookupColormap(TConstArrayView<uint32> Colormap, double Temperature, double Downfall);
    // The dark forest modifier: ((Color & 0xFEFEFE) + 0x28340A) >> 1.
    static uint32 ApplyDarkForest(uint32 Color) { return ((Color & 0xFEFEFE) + 0x28340A) >> 1; }

private:
    TArray<FVoxelBiomeTint> Biomes;
    // Its info noise colours swamp grass; seedless, as in 26.3.
    FVoxelBiomeClimate Climate;
};
