#pragma once

#include "CoreMinimal.h"
#include "Noise/VoxelGradientNoise.h"

// How octaves combine.
enum class EVoxelFractal : uint8
{
    // Octaves summed with falling amplitude: rolling detail.
    Fbm,
    // Each octave 1 - |noise|, squared and weighted by the octave before it: sharp crests for ranges and ridgelines.
    Ridged,
    // fBm whose octaves fade where the accumulated slope is steep (Inigo Quilez): smooth valleys, detailed crests,
    // an eroded look without simulation.
    Damped
};

// A noise as data: what an asset or a test describes and FVoxelNoise compiles. Frequencies are cycles per block.
struct FVoxelNoiseSettings
{
    EVoxelFractal Fractal = EVoxelFractal::Fbm;
    double Frequency = 1. / 256;
    int32 Octaves = 4;
    double Lacunarity = 2;
    double Gain = .5;
    // Domain warp: positions move by up to this many blocks along a smooth two-octave noise at WarpFrequency. 0 is off.
    double WarpAmplitude = 0;
    double WarpFrequency = 1. / 512;
};

// A seeded fractal noise. Each octave and warp axis is its own FVoxelGradientNoise, seeded from (world seed, salt) and
// offset so octaves never share a lattice origin. Values lie in [-1, 1]. Immutable after construction; any thread may
// sample, and two noises built from the same seed, salt and settings return bit-identical values.
class UNNAMEDVOXEL_API FVoxelNoise
{
public:
    static constexpr int32 MaxOctaves = 16;

    FVoxelNoise() = default;
    FVoxelNoise(uint64 WorldSeed, uint64 Salt, const FVoxelNoiseSettings& InSettings);

    float Sample(double X, double Y) const;
    float Sample(double X, double Y, double Z) const;

    // Regular grids with X fastest, then Y, then Z: Out[(Iz * CountY + Iy) * CountX + Ix]. One call per column or cell
    // keeps call overhead and the trace scope off the inner loop, and is where a SIMD backend can replace the loop.
    void SampleGrid(double X0, double Y0, double Step, int32 CountX, int32 CountY, TArrayView<float> Out) const;
    void SampleGrid(double X0, double Y0, double Z0, double StepXY, double StepZ, int32 CountX, int32 CountY,
        int32 CountZ, TArrayView<float> Out) const;

    const FVoxelNoiseSettings& GetSettings() const { return Settings; }

private:
    struct FOctave
    {
        FVoxelGradientNoise Noise;
        double Frequency = 1;
        float Amplitude = 1;
        // In lattice units, so no two octaves share the origin's zero.
        FVector3d Offset = FVector3d::ZeroVector;
    };

    void Warp(double& X, double& Y) const;
    void Warp(double& X, double& Y, double& Z) const;
    static float SampleWarpAxis(const FOctave* Pair, double X, double Y);
    static float SampleWarpAxis(const FOctave* Pair, double X, double Y, double Z);

    FVoxelNoiseSettings Settings;
    TArray<FOctave, TInlineAllocator<8>> Octaves;
    // Two octaves per warp axis (X, Y, Z), present only when WarpAmplitude > 0.
    TArray<FOctave, TInlineAllocator<6>> WarpOctaves;
    // 1 / the sum of octave amplitudes.
    float Normalizer = 1;
};
