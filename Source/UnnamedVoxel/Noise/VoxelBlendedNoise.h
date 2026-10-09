#pragma once

#include "CoreMinimal.h"
#include "Noise/VoxelGradientNoise.h"

// The terrain's base 3D noise (the reference's old_blended_noise): two independent "limit" fBm noises, blended by a
// coarser selector fBm, so the ground alternates between two characters of roughness. Our own design on our gradient
// noise. It takes 26.3's numbers: block positions scale by 684.412 times the node's xz/y scale, the selector reads
// those positions divided by the xz/y factors, and octaves start 15 (limits) and 7 (selector) below one cycle per unit.
// Limits keep 8 of their octaves: with the overworld's numbers the next would have a period under a block, where it
// would only alias at the density cell corners. OutputScale is calibrated once so values spread like the jar's
// (standard deviation about 0.2 for the overworld's numbers, Tests/VoxelBlendedNoiseTests.cpp); it decides saved worlds
// like the golden checksums, so never change it to pass a test. Immutable after construction; any thread may sample,
// and equal inputs give bit-identical values.
class UNNAMEDVOXEL_API FVoxelBlendedNoise
{
public:
    static constexpr int32 LimitOctaves = 8;
    static constexpr int32 SelectorOctaves = 8;
    static constexpr double OutputScale = 1.89;

    FVoxelBlendedNoise() = default;
    // Seeded from (WorldSeed, VoxelRandom::Salt("old_blended_noise")): every node with these numbers is the same noise.
    FVoxelBlendedNoise(uint64 WorldSeed, double XZScale, double YScale, double XZFactor, double YFactor);

    // Block positions with the reference's axes (y up).
    float Sample(double X, double Y, double Z) const;

    // No sample's magnitude exceeds this (each octave of our gradient noise lies in [-1, 1]).
    float GetBound() const { return float(OutputScale); }

    bool IsValid() const { return Limits[0].Num() > 0; }

private:
    struct FOctave
    {
        FVoxelGradientNoise Noise;
        float Amplitude = 0;
        // Cycles per block along xz and y, and a lattice offset in cells.
        double FrequencyXZ = 0, FrequencyY = 0;
        FVector3d Offset = FVector3d::ZeroVector;
    };
    using FOctaves = TArray<FOctave, TInlineAllocator<8>>;

    static float SampleOctaves(const FOctaves& Octaves, double X, double Y, double Z);

    FOctaves Limits[2];
    FOctaves Selector;
};
