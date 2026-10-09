#pragma once

#include "CoreMinimal.h"
#include "Noise/VoxelGradientNoise.h"

struct FVoxelDensityNoise;

// The terrain's climate and density noise, described by a 26.3 noise file (FVoxelDensityNoise): octaves whose
// frequencies double from 2^base_octave cycles per unit, each weighted by its amplitude modifier, summed over two
// stacks of our own gradient noise read at slightly different scales so their lattices never line up. The values spread
// like vanilla's (facts/noise_samples.json in the worldgen reference): a standard deviation of base_amplitude / 3 times
// the octaves' summed weights, times SpreadScale. They do not match vanilla seed for seed. Octaves with a zero modifier
// cost nothing.
// Immutable after construction; any thread may sample, and equal inputs give bit-identical values.
class UNNAMEDVOXEL_API FVoxelNormalNoise
{
public:
    static constexpr int32 MaxOctaves = 24;

    // Scales every noise's sum. Calibrated once against the vanilla spreads; it decides saved worlds like the golden
    // checksums do, so never change it to pass a test.
    static constexpr double SpreadScale = 1.06;

    FVoxelNormalNoise() = default;
    // Seeded from (WorldSeed, VoxelRandom::Salt of the noise's name).
    FVoxelNormalNoise(uint64 WorldSeed, const FVoxelDensityNoise& Description);

    // Positions are in the noise's own units (the density graph has already applied its scales and shifts). The
    // reference's y is the vertical axis.
    float Sample(double X, double Y, double Z) const;

    int32 GetOctaveCount() const { return Octaves.Num(); }

private:
    struct FOctave
    {
        FVoxelGradientNoise Noise;
        double Frequency = 1;
        float Amplitude = 1;
        FVector3d Offset = FVector3d::ZeroVector;
    };

    // Both stacks' octaves, interleaved: the first stack's octave, then the second's at the same frequency.
    TArray<FOctave, TInlineAllocator<16>> Octaves;
};
