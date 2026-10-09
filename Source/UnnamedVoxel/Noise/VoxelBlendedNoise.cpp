#include "Noise/VoxelBlendedNoise.h"
#include "Random/VoxelRandom.h"

namespace
{
    // 26.3's numbers for this noise: units per block at scale 1, and the first octave of the limit and selector noises.
    constexpr double BlendedNoiseBaseScale = 684.412;
    constexpr int32 BlendedNoiseLimitFirstOctave = -15;
    constexpr int32 BlendedNoiseSelectorFirstOctave = -7;
    // The selector's normalized fBm spreads with a deviation near 0.2; this gain lets it reach both limits often, with
    // blended ground in between.
    constexpr float BlendedNoiseSelectorGain = 2.5f;
    constexpr double BlendedNoiseOffsetRange = 1024;

    double BlendedNoisePowerOfTwo(int32 Exponent)
    {
        // Repeated multiplication rather than pow, which can round differently between runtimes.
        double Result = 1;
        for (int32 Index = 0; Index < -Exponent; ++Index)
            Result *= .5;
        for (int32 Index = 0; Index < Exponent; ++Index)
            Result *= 2;
        return Result;
    }
} // namespace

FVoxelBlendedNoise::FVoxelBlendedNoise(uint64 WorldSeed, double XZScale, double YScale, double XZFactor, double YFactor)
{
    FVoxelXoroshiro Stream(VoxelRandom::Hash64(WorldSeed, VoxelRandom::Salt("old_blended_noise"), 0, 0, 0));
    auto Build = [&Stream](FOctaves& Out, int32 Count, int32 FirstOctave, double UnitsXZ, double UnitsY)
    {
        double WeightSum = 0, Weight = 1;
        for (int32 Index = 0; Index < Count; ++Index, Weight *= .5)
            WeightSum += Weight;
        Weight = 1;
        for (int32 Index = 0; Index < Count; ++Index, Weight *= .5)
        {
            FOctave Octave;
            Octave.Noise = FVoxelGradientNoise(Stream.Next());
            const double OffsetX = Stream.NextDouble() * BlendedNoiseOffsetRange;
            const double OffsetY = Stream.NextDouble() * BlendedNoiseOffsetRange;
            const double OffsetZ = Stream.NextDouble() * BlendedNoiseOffsetRange;
            Octave.Offset = FVector3d(OffsetX, OffsetY, OffsetZ);
            const double Cycles = BlendedNoisePowerOfTwo(FirstOctave + Index);
            Octave.FrequencyXZ = UnitsXZ * Cycles;
            Octave.FrequencyY = UnitsY * Cycles;
            Octave.Amplitude = float(Weight / WeightSum);
            Out.Add(Octave);
        }
    };
    const double UnitsXZ = BlendedNoiseBaseScale * XZScale, UnitsY = BlendedNoiseBaseScale * YScale;
    Build(Limits[0], LimitOctaves, BlendedNoiseLimitFirstOctave, UnitsXZ, UnitsY);
    Build(Limits[1], LimitOctaves, BlendedNoiseLimitFirstOctave, UnitsXZ, UnitsY);
    Build(Selector, SelectorOctaves, BlendedNoiseSelectorFirstOctave, UnitsXZ / XZFactor, UnitsY / YFactor);
}

float FVoxelBlendedNoise::SampleOctaves(const FOctaves& Octaves, double X, double Y, double Z)
{
    float Sum = 0;
    for (const FOctave& Octave : Octaves)
        Sum += Octave.Amplitude *
            Octave.Noise.Sample(X * Octave.FrequencyXZ + Octave.Offset.X, Y * Octave.FrequencyY + Octave.Offset.Y,
                Z * Octave.FrequencyXZ + Octave.Offset.Z);
    return Sum;
}

float FVoxelBlendedNoise::Sample(double X, double Y, double Z) const
{
    const float Alpha = FMath::Clamp(.5f + BlendedNoiseSelectorGain * SampleOctaves(Selector, X, Y, Z), 0.f, 1.f);
    // Where the selector settles on one limit, the other is never sampled.
    const float First = Alpha < 1 ? SampleOctaves(Limits[0], X, Y, Z) : 0.f;
    const float Second = Alpha > 0 ? SampleOctaves(Limits[1], X, Y, Z) : 0.f;
    return float(OutputScale) * (First + Alpha * (Second - First));
}

void FVoxelBlendedNoise::Sample(const double* X, const double* Y, const double* Z, int32 Count, float* Out,
    FVoxelBlendedNoiseScratch& Scratch) const
{
    // Sums start at 0 as in SampleOctaves; a limit a point does not sample stays 0, as in Sample.
    for (TArray<float>* Sums : {&Scratch.Selector, &Scratch.First, &Scratch.Second})
    {
        Sums->SetNumUninitialized(Count, EAllowShrinking::No);
        FMemory::Memzero(Sums->GetData(), Count * sizeof(float));
    }
    const auto Accumulate = [&](const FOctaves& Octaves, const TArray<int32>* Points, TArray<float>& Sums)
    {
        for (const FOctave& Octave : Octaves)
            Octave.Noise.AccumulateOctave(X, Y, Z, Points ? Points->GetData() : nullptr, Points ? Points->Num() : Count,
                Octave.FrequencyXZ, Octave.FrequencyY, Octave.Offset, Octave.Amplitude, Sums.GetData());
    };
    Accumulate(Selector, nullptr, Scratch.Selector);
    Scratch.FirstPoints.Reset();
    Scratch.SecondPoints.Reset();
    for (int32 Point = 0; Point < Count; ++Point)
    {
        const float Alpha = FMath::Clamp(.5f + BlendedNoiseSelectorGain * Scratch.Selector[Point], 0.f, 1.f);
        Scratch.Selector[Point] = Alpha;
        if (Alpha < 1) Scratch.FirstPoints.Add(Point);
        if (Alpha > 0) Scratch.SecondPoints.Add(Point);
    }
    Accumulate(Limits[0], &Scratch.FirstPoints, Scratch.First);
    Accumulate(Limits[1], &Scratch.SecondPoints, Scratch.Second);
    for (int32 Point = 0; Point < Count; ++Point)
    {
        const float Alpha = Scratch.Selector[Point], First = Scratch.First[Point], Second = Scratch.Second[Point];
        Out[Point] = float(OutputScale) * (First + Alpha * (Second - First));
    }
}
