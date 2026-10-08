#include "Noise/VoxelNoise.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Random/VoxelRandom.h"

namespace
{
    // Ridged octaves feed their sharpness into the next octave's weight at this rate (Musgrave's ridged multifractal).
    constexpr float RidgeWeightGain = 2;

    // Each octave's lattice is shifted by up to this many cells.
    constexpr double OffsetRange = 1024;
}

FVoxelNoise::FVoxelNoise(uint64 WorldSeed, uint64 Salt, const FVoxelNoiseSettings& InSettings) : Settings(InSettings)
{
    Settings.Octaves = FMath::Clamp(Settings.Octaves, 1, MaxOctaves);
    // One stream per noise, drawn only here: octave seeds and offsets depend on (seed, salt) alone.
    FVoxelXoroshiro Stream(VoxelRandom::Hash64(WorldSeed, Salt, 0, 0, 0));
    auto MakeOctave = [&Stream](double Frequency, float Amplitude)
    {
        FOctave Octave;
        Octave.Noise = FVoxelGradientNoise(Stream.Next());
        Octave.Frequency = Frequency;
        Octave.Amplitude = Amplitude;
        const double OffsetX = Stream.NextDouble() * OffsetRange;
        const double OffsetY = Stream.NextDouble() * OffsetRange;
        const double OffsetZ = Stream.NextDouble() * OffsetRange;
        Octave.Offset = FVector3d(OffsetX, OffsetY, OffsetZ);
        return Octave;
    };

    // Repeated multiplication rather than pow, which can round differently between runtimes.
    double Frequency = Settings.Frequency;
    float Amplitude = 1, AmplitudeSum = 0;
    for (int32 Index = 0; Index < Settings.Octaves; ++Index)
    {
        Octaves.Add(MakeOctave(Frequency, Amplitude));
        AmplitudeSum += Amplitude;
        Frequency *= Settings.Lacunarity;
        Amplitude *= float(Settings.Gain);
    }
    Normalizer = 1 / AmplitudeSum;

    if (Settings.WarpAmplitude > 0)
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            WarpOctaves.Add(MakeOctave(Settings.WarpFrequency, 2.f / 3));
            WarpOctaves.Add(MakeOctave(Settings.WarpFrequency * 2, 1.f / 3));
        }
}

float FVoxelNoise::SampleWarpAxis(const FOctave* Pair, double X, double Y)
{
    float Sum = 0;
    for (int32 Index = 0; Index < 2; ++Index)
        Sum += Pair[Index].Amplitude *
            Pair[Index].Noise.Sample(
                X * Pair[Index].Frequency + Pair[Index].Offset.X, Y * Pair[Index].Frequency + Pair[Index].Offset.Y);
    return Sum;
}

float FVoxelNoise::SampleWarpAxis(const FOctave* Pair, double X, double Y, double Z)
{
    float Sum = 0;
    for (int32 Index = 0; Index < 2; ++Index)
        Sum += Pair[Index].Amplitude *
            Pair[Index].Noise.Sample(X * Pair[Index].Frequency + Pair[Index].Offset.X,
                Y * Pair[Index].Frequency + Pair[Index].Offset.Y, Z * Pair[Index].Frequency + Pair[Index].Offset.Z);
    return Sum;
}

void FVoxelNoise::Warp(double& X, double& Y) const
{
    if (WarpOctaves.IsEmpty()) return;
    // Both axes read the unwarped position.
    const float DX = SampleWarpAxis(&WarpOctaves[0], X, Y), DY = SampleWarpAxis(&WarpOctaves[2], X, Y);
    X += Settings.WarpAmplitude * DX;
    Y += Settings.WarpAmplitude * DY;
}

void FVoxelNoise::Warp(double& X, double& Y, double& Z) const
{
    if (WarpOctaves.IsEmpty()) return;
    const float DX = SampleWarpAxis(&WarpOctaves[0], X, Y, Z), DY = SampleWarpAxis(&WarpOctaves[2], X, Y, Z),
                DZ = SampleWarpAxis(&WarpOctaves[4], X, Y, Z);
    X += Settings.WarpAmplitude * DX;
    Y += Settings.WarpAmplitude * DY;
    Z += Settings.WarpAmplitude * DZ;
}

float FVoxelNoise::Sample(double X, double Y) const
{
    Warp(X, Y);
    float Sum = 0;
    switch (Settings.Fractal)
    {
    case EVoxelFractal::Fbm:
        for (const FOctave& Octave : Octaves)
            Sum += Octave.Amplitude *
                Octave.Noise.Sample(X * Octave.Frequency + Octave.Offset.X, Y * Octave.Frequency + Octave.Offset.Y);
        return Sum * Normalizer;
    case EVoxelFractal::Ridged:
    {
        float Weight = 1;
        for (const FOctave& Octave : Octaves)
        {
            float Ridge = 1 -
                FMath::Abs(Octave.Noise.Sample(
                    X * Octave.Frequency + Octave.Offset.X, Y * Octave.Frequency + Octave.Offset.Y));
            Ridge *= Ridge * Weight;
            Weight = FMath::Clamp(Ridge * RidgeWeightGain, 0.f, 1.f);
            Sum += Octave.Amplitude * Ridge;
        }
        // [0, 1] to [-1, 1].
        return Sum * Normalizer * 2 - 1;
    }
    case EVoxelFractal::Damped:
    {
        FVector2f Slope = FVector2f::ZeroVector;
        for (const FOctave& Octave : Octaves)
        {
            const FVoxelNoiseSample2 Noise = Octave.Noise.SampleWithGradient(
                X * Octave.Frequency + Octave.Offset.X, Y * Octave.Frequency + Octave.Offset.Y);
            Slope += Noise.Gradient;
            Sum += Octave.Amplitude * Noise.Value / (1 + Slope.SizeSquared());
        }
        return Sum * Normalizer;
    }
    }
    return 0;
}

float FVoxelNoise::Sample(double X, double Y, double Z) const
{
    Warp(X, Y, Z);
    float Sum = 0;
    switch (Settings.Fractal)
    {
    case EVoxelFractal::Fbm:
        for (const FOctave& Octave : Octaves)
            Sum += Octave.Amplitude *
                Octave.Noise.Sample(X * Octave.Frequency + Octave.Offset.X, Y * Octave.Frequency + Octave.Offset.Y,
                    Z * Octave.Frequency + Octave.Offset.Z);
        return Sum * Normalizer;
    case EVoxelFractal::Ridged:
    {
        float Weight = 1;
        for (const FOctave& Octave : Octaves)
        {
            float Ridge = 1 -
                FMath::Abs(Octave.Noise.Sample(X * Octave.Frequency + Octave.Offset.X,
                    Y * Octave.Frequency + Octave.Offset.Y, Z * Octave.Frequency + Octave.Offset.Z));
            Ridge *= Ridge * Weight;
            Weight = FMath::Clamp(Ridge * RidgeWeightGain, 0.f, 1.f);
            Sum += Octave.Amplitude * Ridge;
        }
        return Sum * Normalizer * 2 - 1;
    }
    case EVoxelFractal::Damped:
    {
        FVector3f Slope = FVector3f::ZeroVector;
        for (const FOctave& Octave : Octaves)
        {
            const FVoxelNoiseSample3 Noise = Octave.Noise.SampleWithGradient(X * Octave.Frequency + Octave.Offset.X,
                Y * Octave.Frequency + Octave.Offset.Y, Z * Octave.Frequency + Octave.Offset.Z);
            Slope += Noise.Gradient;
            Sum += Octave.Amplitude * Noise.Value / (1 + Slope.SizeSquared());
        }
        return Sum * Normalizer;
    }
    }
    return 0;
}

void FVoxelNoise::SampleGrid(double X0, double Y0, double Step, int32 CountX, int32 CountY, TArrayView<float> Out) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelNoise_SampleGrid2D);
    check(Out.Num() >= CountX * CountY);
    int32 Index = 0;
    for (int32 IY = 0; IY < CountY; ++IY)
        for (int32 IX = 0; IX < CountX; ++IX)
            Out[Index++] = Sample(X0 + IX * Step, Y0 + IY * Step);
}

void FVoxelNoise::SampleGrid(double X0, double Y0, double Z0, double StepXY, double StepZ, int32 CountX, int32 CountY,
    int32 CountZ, TArrayView<float> Out) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelNoise_SampleGrid3D);
    check(Out.Num() >= CountX * CountY * CountZ);
    int32 Index = 0;
    for (int32 IZ = 0; IZ < CountZ; ++IZ)
        for (int32 IY = 0; IY < CountY; ++IY)
            for (int32 IX = 0; IX < CountX; ++IX)
                Out[Index++] = Sample(X0 + IX * StepXY, Y0 + IY * StepXY, Z0 + IZ * StepZ);
}
