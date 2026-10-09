#include "Biome/VoxelBiomeClimate.h"
#include "Biome/VoxelBiomeTable.h"
#include "Random/VoxelRandom.h"

namespace
{
    // Seedless, as 26.3's climate noises are: a fixed seed for every world.
    constexpr uint64 BiomeClimateSeed = 0;

    FVoxelNoiseSettings BiomeClimateNoise(double Frequency)
    {
        FVoxelNoiseSettings Result;
        Result.Fractal = EVoxelFractal::Fbm;
        Result.Frequency = Frequency;
        Result.Octaves = 1;
        return Result;
    }
} // namespace

FVoxelBiomeClimate::FVoxelBiomeClimate()
    : Wobble(BiomeClimateSeed, VoxelRandom::Salt("biome-climate/wobble"), BiomeClimateNoise(1. / 8)),
      Frozen(BiomeClimateSeed, VoxelRandom::Salt("biome-climate/frozen"), BiomeClimateNoise(.05)),
      Info(BiomeClimateSeed, VoxelRandom::Salt("biome-climate/info"), BiomeClimateNoise(1))
{
}

float FVoxelBiomeClimate::GetWobble(int32 X, int32 Y) const
{
    return float(Wobble.Sample(X, Y) * WobbleBlocks);
}

double FVoxelBiomeClimate::GetFrozenNoise(int32 X, int32 Y) const
{
    return Frozen.Sample(X, Y);
}

double FVoxelBiomeClimate::GetInfoNoise(double X, double Y) const
{
    return Info.Sample(X, Y);
}

float FVoxelBiomeClimate::GetBaseTemperature(const FVoxelBiome& Biome, int32 X, int32 Y) const
{
    if (Biome.TemperatureModifier == EVoxelTemperatureModifier::Frozen &&
        GetFrozenNoise(X, Y) * 7 + GetInfoNoise(X * .2, Y * .2) < .3 && GetInfoNoise(X * .09, Y * .09) < .8)
        return FrozenPatchTemperature;
    return Biome.Temperature;
}

float FVoxelBiomeClimate::AdjustForHeight(float Base, int32 Z, int32 SeaLevel, float WobbleValue)
{
    const int32 Line = SeaLevel + SnowLineOffset;
    if (Z <= Line) return Base;
    return Base - (WobbleValue + float(Z) - float(Line)) * .05f / 40.f;
}

float FVoxelBiomeClimate::GetTemperature(const FVoxelBiome& Biome, int32 X, int32 Y, int32 Z, int32 SeaLevel) const
{
    const float Base = GetBaseTemperature(Biome, X, Y);
    return Z <= SeaLevel + SnowLineOffset ? Base : AdjustForHeight(Base, Z, SeaLevel, GetWobble(X, Y));
}
