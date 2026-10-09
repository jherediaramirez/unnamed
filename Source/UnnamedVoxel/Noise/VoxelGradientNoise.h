#pragma once

#include "CoreMinimal.h"

// A noise value and its gradient with respect to the coordinates passed in.
struct FVoxelNoiseSample2
{
    float Value = 0;
    FVector2f Gradient = FVector2f::ZeroVector;
};

struct FVoxelNoiseSample3
{
    float Value = 0;
    FVector3f Gradient = FVector3f::ZeroVector;
};

// One octave of gradient (Perlin-style) noise in 2D and 3D, with analytic derivatives and a quintic fade.
// - Lattice gradients come from a hash of the lattice point and this octave's seed, not a permutation table, so the
//   pattern never repeats within the int32 lattice and nothing is shared between noises.
// - Gradient directions are literal tables (no runtime trigonometry, which can round differently between runtimes).
// - Positions split into lattice cell and fraction in double precision, so worlds tens of millions of blocks out keep
//   full detail; the fraction is then evaluated in float, as 26.3 evaluates density.
// Values lie in [-1, 1] and are 0 on every lattice point. Immutable after construction, so any thread may sample.
class UNNAMEDVOXEL_API FVoxelGradientNoise
{
public:
    FVoxelGradientNoise() = default;
    explicit FVoxelGradientNoise(uint64 Seed) : Seed32(uint32(Seed ^ (Seed >> 32))) {}

    float Sample(double X, double Y) const;
    float Sample(double X, double Y, double Z) const;
    FVoxelNoiseSample2 SampleWithGradient(double X, double Y) const;
    FVoxelNoiseSample3 SampleWithGradient(double X, double Y, double Z) const;

    // One octave of an fBm over a batch: adds Amplitude * Sample(X[P] * FrequencyXZ + Offset.X, Y[P] * FrequencyY +
    // Offset.Y, Z[P] * FrequencyXZ + Offset.Z) to Sums[P] for each P = Points[K] (P = K when Points is null), K <
    // Count. The arithmetic is Sample's, so every sum gets the bits it would get point by point.
    void AccumulateOctave(const double* X, const double* Y, const double* Z, const int32* Points, int32 Count,
        double FrequencyXZ, double FrequencyY, const FVector3d& Offset, float Amplitude, float* Sums) const;

private:
    uint32 Seed32 = 0;
};
