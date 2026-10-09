#include "Biome/VoxelBiomeTable.h"

namespace
{
    bool BiomeTableFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    bool IsBiomeColor(int32 Color, bool bOptional)
    {
        return (bOptional && Color == INDEX_NONE) || (Color >= 0 && Color <= 0xFFFFFF);
    }
}

int32 FVoxelBiomeTable::FindBiome(FStringView Name) const
{
    return Biomes.IndexOfByPredicate(
        [Name](const FVoxelBiome& Biome) { return FStringView(Biome.Name).Equals(Name, ESearchCase::CaseSensitive); });
}

bool FVoxelBiomeTable::Validate(FString* OutError) const
{
    for (int32 Index = 0; Index < Biomes.Num(); ++Index)
    {
        const FVoxelBiome& Biome = Biomes[Index];
        if (Biome.Name.IsEmpty()) return BiomeTableFail(OutError, FString::Printf(TEXT("biome %d has no name"), Index));
        // Ids are case-sensitive; TSet<FString> would compare them ignoring case.
        if (FindBiome(Biome.Name) != Index)
            return BiomeTableFail(OutError, FString::Printf(TEXT("%s is listed twice"), *Biome.Name));
        if (!FMath::IsFinite(Biome.Temperature) || !FMath::IsFinite(Biome.Downfall))
            return BiomeTableFail(
                OutError, FString::Printf(TEXT("%s: temperature or downfall is not finite"), *Biome.Name));
        if (!IsBiomeColor(Biome.WaterColor, false) || !IsBiomeColor(Biome.GrassColor, true) ||
            !IsBiomeColor(Biome.FoliageColor, true) || !IsBiomeColor(Biome.DryFoliageColor, true))
            return BiomeTableFail(OutError, FString::Printf(TEXT("%s: a colour is out of range"), *Biome.Name));
    }
    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
        const FVoxelBiomeParameter& Point = Points[Index];
        if (!Biomes.IsValidIndex(Point.Biome))
            return BiomeTableFail(OutError, FString::Printf(TEXT("point %d: biome index %d"), Index, Point.Biome));
        for (int32 Axis = 0; Axis < int32(EVoxelClimateAxis::Count); ++Axis)
            if (Point.Min[Axis] > Point.Max[Axis])
                return BiomeTableFail(
                    OutError, FString::Printf(TEXT("point %d: parameter %d has min above max"), Index, Axis));
    }
    return true;
}
