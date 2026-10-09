#include "Noise/VoxelGradientNoise.h"

namespace
{
    // 64 unit directions at (k + 1/2) * 360/64 degrees, so none lies on an axis.
    const FVector2f Gradients2[64] = {
        {0.99879545f, 0.0490676761f},
        {0.989176512f, 0.146730468f},
        {0.970031261f, 0.242980182f},
        {0.941544056f, 0.336889863f},
        {0.903989315f, 0.427555084f},
        {0.857728601f, 0.514102757f},
        {0.803207517f, 0.59569931f},
        {0.740951121f, 0.671558976f},
        {0.671558976f, 0.740951121f},
        {0.59569931f, 0.803207517f},
        {0.514102757f, 0.857728601f},
        {0.427555084f, 0.903989315f},
        {0.336889863f, 0.941544056f},
        {0.242980182f, 0.970031261f},
        {0.146730468f, 0.989176512f},
        {0.0490676761f, 0.99879545f},
        {-0.0490676761f, 0.99879545f},
        {-0.146730468f, 0.989176512f},
        {-0.242980182f, 0.970031261f},
        {-0.336889863f, 0.941544056f},
        {-0.427555084f, 0.903989315f},
        {-0.514102757f, 0.857728601f},
        {-0.59569931f, 0.803207517f},
        {-0.671558976f, 0.740951121f},
        {-0.740951121f, 0.671558976f},
        {-0.803207517f, 0.59569931f},
        {-0.857728601f, 0.514102757f},
        {-0.903989315f, 0.427555084f},
        {-0.941544056f, 0.336889863f},
        {-0.970031261f, 0.242980182f},
        {-0.989176512f, 0.146730468f},
        {-0.99879545f, 0.0490676761f},
        {-0.99879545f, -0.0490676761f},
        {-0.989176512f, -0.146730468f},
        {-0.970031261f, -0.242980182f},
        {-0.941544056f, -0.336889863f},
        {-0.903989315f, -0.427555084f},
        {-0.857728601f, -0.514102757f},
        {-0.803207517f, -0.59569931f},
        {-0.740951121f, -0.671558976f},
        {-0.671558976f, -0.740951121f},
        {-0.59569931f, -0.803207517f},
        {-0.514102757f, -0.857728601f},
        {-0.427555084f, -0.903989315f},
        {-0.336889863f, -0.941544056f},
        {-0.242980182f, -0.970031261f},
        {-0.146730468f, -0.989176512f},
        {-0.0490676761f, -0.99879545f},
        {0.0490676761f, -0.99879545f},
        {0.146730468f, -0.989176512f},
        {0.242980182f, -0.970031261f},
        {0.336889863f, -0.941544056f},
        {0.427555084f, -0.903989315f},
        {0.514102757f, -0.857728601f},
        {0.59569931f, -0.803207517f},
        {0.671558976f, -0.740951121f},
        {0.740951121f, -0.671558976f},
        {0.803207517f, -0.59569931f},
        {0.857728601f, -0.514102757f},
        {0.903989315f, -0.427555084f},
        {0.941544056f, -0.336889863f},
        {0.970031261f, -0.242980182f},
        {0.989176512f, -0.146730468f},
        {0.99879545f, -0.0490676761f},
    };

    // The 12 cube-edge directions of improved Perlin noise, normalized to unit length.
    constexpr float EdgeComponent = 0.707106781f;
    const FVector3f Gradients3[12] = {
        {EdgeComponent, EdgeComponent, 0},
        {-EdgeComponent, EdgeComponent, 0},
        {EdgeComponent, -EdgeComponent, 0},
        {-EdgeComponent, -EdgeComponent, 0},
        {EdgeComponent, 0, EdgeComponent},
        {-EdgeComponent, 0, EdgeComponent},
        {EdgeComponent, 0, -EdgeComponent},
        {-EdgeComponent, 0, -EdgeComponent},
        {0, EdgeComponent, EdgeComponent},
        {0, -EdgeComponent, EdgeComponent},
        {0, EdgeComponent, -EdgeComponent},
        {0, -EdgeComponent, -EdgeComponent},
    };

    // With unit gradients the noise peaks at sqrt(N) / 2 (a cell centre); these scale it to [-1, 1].
    constexpr float Scale2 = 1.41421356f;
    constexpr float Scale3 = 1.15470054f;

    // Seeded lattice hash: odd multipliers spread each axis, then MurmurHash3's 32-bit finalizer mixes the result.
    FORCEINLINE uint32 HashLattice(uint32 Seed, uint32 X, uint32 Y, uint32 Z)
    {
        uint32 Hash = Seed ^ (X * 0x9E3779B1u) ^ (Y * 0x85EBCA77u) ^ (Z * 0xC2B2AE3Du);
        Hash ^= Hash >> 16;
        Hash *= 0x85EBCA6Bu;
        Hash ^= Hash >> 13;
        Hash *= 0xC2B2AE35u;
        return Hash ^ (Hash >> 16);
    }

    FORCEINLINE const FVector2f& Gradient2(uint32 Seed, uint32 X, uint32 Y)
    {
        return Gradients2[HashLattice(Seed, X, Y, 0) >> 26];
    }

    FORCEINLINE const FVector3f& Gradient3(uint32 Seed, uint32 X, uint32 Y, uint32 Z)
    {
        return Gradients3[(uint64(HashLattice(Seed, X, Y, Z)) * 12) >> 32];
    }

    // 6t^5 - 15t^4 + 10t^3: continuous first and second derivatives across cells.
    FORCEINLINE float Fade(float T)
    {
        return T * T * T * (T * (T * 6 - 15) + 10);
    }

    FORCEINLINE float FadeDerivative(float T)
    {
        return 30 * T * T * (T * (T - 2) + 1);
    }

    // The lattice cell (wrapped to 32 bits) and the fraction inside it.
    FORCEINLINE void Split(double Position, uint32& OutCell, float& OutFraction)
    {
        const double Floor = FMath::FloorToDouble(Position);
        OutCell = uint32(int64(Floor));
        OutFraction = float(Position - Floor);
    }

    template <bool bGradient> float Evaluate2(uint32 Seed, double X, double Y, FVector2f* OutGradient)
    {
        uint32 X0, Y0;
        float FX, FY;
        Split(X, X0, FX);
        Split(Y, Y0, FY);
        const FVector2f& G00 = Gradient2(Seed, X0, Y0);
        const FVector2f& G10 = Gradient2(Seed, X0 + 1, Y0);
        const FVector2f& G01 = Gradient2(Seed, X0, Y0 + 1);
        const FVector2f& G11 = Gradient2(Seed, X0 + 1, Y0 + 1);
        const float N00 = G00.X * FX + G00.Y * FY;
        const float N10 = G10.X * (FX - 1) + G10.Y * FY;
        const float N01 = G01.X * FX + G01.Y * (FY - 1);
        const float N11 = G11.X * (FX - 1) + G11.Y * (FY - 1);
        const float U = Fade(FX), V = Fade(FY);
        // The bilinear blend as a polynomial in U and V, so the derivative follows directly (Inigo Quilez's form).
        const float K1 = N10 - N00, K2 = N01 - N00, K3 = N00 - N10 - N01 + N11;
        if constexpr (bGradient)
        {
            const FVector2f Blend = G00 + (G10 - G00) * U + (G01 - G00) * V + (G00 - G10 - G01 + G11) * (U * V);
            const FVector2f Fading(FadeDerivative(FX) * (K1 + K3 * V), FadeDerivative(FY) * (K2 + K3 * U));
            *OutGradient = (Blend + Fading) * Scale2;
        }
        return (N00 + K1 * U + K2 * V + K3 * U * V) * Scale2;
    }

    template <bool bGradient>
    FORCEINLINE float Evaluate3(uint32 Seed, double X, double Y, double Z, FVector3f* OutGradient)
    {
        uint32 X0, Y0, Z0;
        float FX, FY, FZ;
        Split(X, X0, FX);
        Split(Y, Y0, FY);
        Split(Z, Z0, FZ);
        const FVector3f& G000 = Gradient3(Seed, X0, Y0, Z0);
        const FVector3f& G100 = Gradient3(Seed, X0 + 1, Y0, Z0);
        const FVector3f& G010 = Gradient3(Seed, X0, Y0 + 1, Z0);
        const FVector3f& G110 = Gradient3(Seed, X0 + 1, Y0 + 1, Z0);
        const FVector3f& G001 = Gradient3(Seed, X0, Y0, Z0 + 1);
        const FVector3f& G101 = Gradient3(Seed, X0 + 1, Y0, Z0 + 1);
        const FVector3f& G011 = Gradient3(Seed, X0, Y0 + 1, Z0 + 1);
        const FVector3f& G111 = Gradient3(Seed, X0 + 1, Y0 + 1, Z0 + 1);
        const float N000 = G000.X * FX + G000.Y * FY + G000.Z * FZ;
        const float N100 = G100.X * (FX - 1) + G100.Y * FY + G100.Z * FZ;
        const float N010 = G010.X * FX + G010.Y * (FY - 1) + G010.Z * FZ;
        const float N110 = G110.X * (FX - 1) + G110.Y * (FY - 1) + G110.Z * FZ;
        const float N001 = G001.X * FX + G001.Y * FY + G001.Z * (FZ - 1);
        const float N101 = G101.X * (FX - 1) + G101.Y * FY + G101.Z * (FZ - 1);
        const float N011 = G011.X * FX + G011.Y * (FY - 1) + G011.Z * (FZ - 1);
        const float N111 = G111.X * (FX - 1) + G111.Y * (FY - 1) + G111.Z * (FZ - 1);
        const float U = Fade(FX), V = Fade(FY), W = Fade(FZ);
        const float K1 = N100 - N000, K2 = N010 - N000, K3 = N001 - N000;
        const float K4 = N000 - N100 - N010 + N110, K5 = N000 - N010 - N001 + N011, K6 = N000 - N100 - N001 + N101;
        const float K7 = -N000 + N100 + N010 - N110 + N001 - N101 - N011 + N111;
        if constexpr (bGradient)
        {
            const FVector3f Blend = G000 + (G100 - G000) * U + (G010 - G000) * V + (G001 - G000) * W +
                (G000 - G100 - G010 + G110) * (U * V) + (G000 - G010 - G001 + G011) * (V * W) +
                (G000 - G100 - G001 + G101) * (W * U) +
                (-G000 + G100 + G010 - G110 + G001 - G101 - G011 + G111) * (U * V * W);
            const FVector3f Fading(FadeDerivative(FX) * (K1 + K4 * V + K6 * W + K7 * V * W),
                FadeDerivative(FY) * (K2 + K5 * W + K4 * U + K7 * W * U),
                FadeDerivative(FZ) * (K3 + K6 * U + K5 * V + K7 * U * V));
            *OutGradient = (Blend + Fading) * Scale3;
        }
        return (N000 + K1 * U + K2 * V + K3 * W + K4 * U * V + K5 * V * W + K6 * W * U + K7 * U * V * W) * Scale3;
    }
}

float FVoxelGradientNoise::Sample(double X, double Y) const
{
    return Evaluate2<false>(Seed32, X, Y, nullptr);
}

float FVoxelGradientNoise::Sample(double X, double Y, double Z) const
{
    return Evaluate3<false>(Seed32, X, Y, Z, nullptr);
}

void FVoxelGradientNoise::AccumulateOctave(const double* X, const double* Y, const double* Z, const int32* Points,
    int32 Count, double FrequencyXZ, double FrequencyY, const FVector3d& Offset, float Amplitude, float* Sums) const
{
    const double OffsetX = Offset.X, OffsetY = Offset.Y, OffsetZ = Offset.Z;
    for (int32 K = 0; K < Count; ++K)
    {
        const int32 P = Points ? Points[K] : K;
        Sums[P] += Amplitude *
            Evaluate3<false>(Seed32, X[P] * FrequencyXZ + OffsetX, Y[P] * FrequencyY + OffsetY,
                Z[P] * FrequencyXZ + OffsetZ, nullptr);
    }
}

FVoxelNoiseSample2 FVoxelGradientNoise::SampleWithGradient(double X, double Y) const
{
    FVoxelNoiseSample2 Out;
    Out.Value = Evaluate2<true>(Seed32, X, Y, &Out.Gradient);
    return Out;
}

FVoxelNoiseSample3 FVoxelGradientNoise::SampleWithGradient(double X, double Y, double Z) const
{
    FVoxelNoiseSample3 Out;
    Out.Value = Evaluate3<true>(Seed32, X, Y, Z, &Out.Gradient);
    return Out;
}
