#include "Biome/VoxelBiomeTints.h"

bool FVoxelTintTable::Initialize(const FVoxelBiomeTable& Table, FString* OutError)
{
    Biomes.Reset();
    constexpr int32 Pixels = FVoxelBiomeTable::ColormapSize * FVoxelBiomeTable::ColormapSize;
    if (Table.GrassColormap.Num() != Pixels || Table.FoliageColormap.Num() != Pixels)
    {
        if (OutError) *OutError = TEXT("biome tints need the grass and foliage colormaps");
        return false;
    }
    // The mesher caches biome indices in a byte, with 0xFE and 0xFF as markers.
    if (Table.Biomes.Num() >= 0xFE)
    {
        if (OutError)
            *OutError = FString::Printf(TEXT("biome tints take at most 253 biomes, not %d"), Table.Biomes.Num());
        return false;
    }
    Biomes.Reserve(Table.Biomes.Num());
    for (const FVoxelBiome& Biome : Table.Biomes)
    {
        // Biome.getGrassColorFromTexture: the base temperature and downfall clamped as floats, then read as doubles.
        const double Temperature = FMath::Clamp(Biome.Temperature, 0.f, 1.f);
        const double Downfall = FMath::Clamp(Biome.Downfall, 0.f, 1.f);
        FVoxelBiomeTint& Tint = Biomes.AddDefaulted_GetRef();
        Tint.Water = uint32(Biome.WaterColor);
        Tint.Grass = Biome.GrassColor != INDEX_NONE ? uint32(Biome.GrassColor)
                                                    : LookupColormap(Table.GrassColormap, Temperature, Downfall);
        // The dark forest modifier depends on the colour alone, so it is applied here; swamp's reads the position.
        if (Biome.GrassColorModifier == EVoxelGrassColorModifier::DarkForest) Tint.Grass = ApplyDarkForest(Tint.Grass);
        Tint.Foliage = Biome.FoliageColor != INDEX_NONE ? uint32(Biome.FoliageColor)
                                                        : LookupColormap(Table.FoliageColormap, Temperature, Downfall);
        Tint.DryFoliage = Biome.DryFoliageColor != INDEX_NONE ? uint32(Biome.DryFoliageColor)
            : Table.DryFoliageColormap.Num() == Pixels ? LookupColormap(Table.DryFoliageColormap, Temperature, Downfall)
                                                       : 0;
        Tint.GrassModifier = Biome.GrassColorModifier;
    }
    return true;
}

uint32 FVoxelTintTable::LookupColormap(TConstArrayView<uint32> Colormap, double Temperature, double Downfall)
{
    // The downfall is scaled by the temperature, which keeps the lookup inside the colormap's lower-left triangle.
    const double Scaled = Downfall * Temperature;
    const int32 X = int32((1.0 - Temperature) * 255.0);
    const int32 Y = int32((1.0 - Scaled) * 255.0);
    const int32 Index = Y * FVoxelBiomeTable::ColormapSize + X;
    return Colormap.IsValidIndex(Index) ? Colormap[Index] : 0xFF00FF;
}

uint32 FVoxelTintTable::GetSwampGrass(int32 X, int32 Y) const
{
    return Climate.GetInfoNoise(X * SwampScale, Y * SwampScale) < SwampThreshold ? SwampDark : SwampLight;
}

uint32 FVoxelTintTable::GetColor(EVoxelTintSource Source, int32 Biome, int32 X, int32 Y, uint32 Fallback) const
{
    if (!Biomes.IsValidIndex(Biome)) return Fallback;
    const FVoxelBiomeTint& Tint = Biomes[Biome];
    switch (Source)
    {
    case EVoxelTintSource::Water:
        return Tint.Water;
    case EVoxelTintSource::Grass:
        return Tint.GrassModifier == EVoxelGrassColorModifier::Swamp ? GetSwampGrass(X, Y) : Tint.Grass;
    case EVoxelTintSource::Foliage:
        return Tint.Foliage;
    case EVoxelTintSource::DryFoliage:
        return Tint.DryFoliage ? Tint.DryFoliage : Fallback;
    default:
        return Fallback;
    }
}
