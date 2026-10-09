#include "Noise/VoxelNormalNoise.h"
#include "Density/VoxelDensityGraph.h"
#include "Random/VoxelRandom.h"

namespace
{
    // The second stack reads positions scaled by this, so the two lattices never coincide.
    constexpr double NormalNoiseSecondStackScale = 1.0183;

    // Each octave's lattice is shifted by up to this many cells, so no two octaves share the origin's zero.
    constexpr double NormalNoiseOffsetRange = 1024;

    uint64 NormalNoiseSalt(const FString& Name)
    {
        const FTCHARToUTF8 Utf8(*Name);
        return VoxelRandom::Salt(Utf8.Get());
    }
} // namespace

FVoxelNormalNoise::FVoxelNormalNoise(uint64 WorldSeed, const FVoxelDensityNoise& Description)
{
    const int32 Count = FMath::Min(Description.AmplitudeModifiers.Num(), MaxOctaves);
    // One stream per noise, drawn only here: octave seeds and offsets depend on (seed, name) alone.
    FVoxelXoroshiro Stream(VoxelRandom::Hash64(WorldSeed, NormalNoiseSalt(Description.Name), 0, 0, 0));

    // Lowest frequency first; each octave up has half the weight. Repeated multiplication rather than pow, which can
    // round differently between runtimes.
    double Frequency = 1;
    for (int32 Index = 0; Index < -Description.BaseOctave; ++Index)
        Frequency *= .5;
    for (int32 Index = 0; Index > -Description.BaseOctave; --Index)
        Frequency *= 2;
    // Octave weights halve upwards and are normalized to sum to 1 before the modifiers apply. The values then spread
    // with a standard deviation of base_amplitude / 3 times the summed weights (what vanilla's spreads show, within 2%
    // for every climate noise). Independent octaves add in quadrature, so the sum is rescaled by Linear / Quadrature.
    double Weight = 1, WeightSum = 0;
    for (int32 Index = 0; Index < Count; ++Index, Weight *= .5)
        WeightSum += Weight;
    double Linear = 0, Quadrature = 0;
    Weight = 1;
    for (int32 Index = 0; Index < Count; ++Index, Weight *= .5)
    {
        const double Scaled = Description.AmplitudeModifiers[Index] * Weight / WeightSum;
        Linear += FMath::Abs(Scaled);
        Quadrature += Scaled * Scaled;
    }
    const double Spread = Quadrature > 0 ? Linear / FMath::Sqrt(Quadrature) : 0;

    Weight = 1;
    for (int32 Index = 0; Index < Count; ++Index, Frequency *= 2, Weight *= .5)
    {
        // Every octave draws from the stream even when silent, so a modifier set to 0 never reseeds the others.
        for (int32 Stack = 0; Stack < 2; ++Stack)
        {
            FOctave Octave;
            Octave.Noise = FVoxelGradientNoise(Stream.Next());
            const double OffsetX = Stream.NextDouble() * NormalNoiseOffsetRange;
            const double OffsetY = Stream.NextDouble() * NormalNoiseOffsetRange;
            const double OffsetZ = Stream.NextDouble() * NormalNoiseOffsetRange;
            Octave.Offset = FVector3d(OffsetX, OffsetY, OffsetZ);
            Octave.Frequency = Stack == 0 ? Frequency : Frequency * NormalNoiseSecondStackScale;
            Octave.Amplitude = float(Description.BaseAmplitude * SpreadScale * Spread *
                Description.AmplitudeModifiers[Index] * Weight / WeightSum);
            if (Octave.Amplitude != 0) Octaves.Add(Octave);
            Bound += FMath::Abs(Octave.Amplitude);
        }
    }
}

void FVoxelNormalNoise::SampleBatch(const double* X, const double* Y, const double* Z, int32 Count, float* Out) const
{
    // Sample's sum starts at 0 and adds the octaves in order; so does each point here.
    for (int32 I = 0; I < Count; ++I)
        Out[I] = 0;
    for (const FOctave& Octave : Octaves)
        Octave.Noise.AccumulateOctave(
            X, Y, Z, nullptr, Count, Octave.Frequency, Octave.Frequency, Octave.Offset, Octave.Amplitude, Out);
}

float FVoxelNormalNoise::Sample(double X, double Y, double Z) const
{
    float Sum = 0;
    for (const FOctave& Octave : Octaves)
        Sum += Octave.Amplitude *
            Octave.Noise.Sample(X * Octave.Frequency + Octave.Offset.X, Y * Octave.Frequency + Octave.Offset.Y,
                Z * Octave.Frequency + Octave.Offset.Z);
    return Sum;
}
