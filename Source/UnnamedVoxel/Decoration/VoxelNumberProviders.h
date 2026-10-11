#pragma once

#include "CoreMinimal.h"
#include "Random/VoxelRandom.h"

class FVoxelFeatureGraph;
struct FVoxelFeatureValue;

// A compiled int provider, or a height provider with its anchors resolved to Z: a constant, a uniform range, or a
// trapezoid (the sum of two uniform parts, a triangle when the plateau is 0), the types the ore placements reach; and
// for plants (S8) biased_to_bottom, weighted_list and clamped. Providers.Distribution checks each against the jar's
// histograms.
struct UNNAMEDVOXEL_API FVoxelIntProvider
{
    enum class EType : uint8
    {
        Constant,
        Uniform,
        Trapezoid,
        // Min + a uniform draw below a uniform draw from 1 to Max - Min + 1 (nested uniforms; the jar's histograms).
        BiasedToBottom,
        // Weighted: Entries' values by their weights.
        WeightedList,
        // Inner[0]'s value clamped into [Min, Max].
        Clamped,
        // Min + three nested uniform draws, each below the last plus one (the carvers' count, S10; the jar's
        // histogram: P(Min) = 0.42 for 0..14).
        VeryBiasedToBottom
    };

    EType Type = EType::Constant;
    // Both included: the values the provider can give.
    int32 Min = 0;
    int32 Max = 0;
    // Trapezoid: the widths of the two uniform parts, SpanA + SpanB = Max - Min and SpanB - SpanA = the plateau.
    int32 SpanA = 0;
    int32 SpanB = 0;
    // WeightedList: each entry's weight (Y), and the weights' sum (in SpanA); the entries' providers are Inner.
    TArray<FIntPoint> Entries;
    // Clamped: the source; WeightedList: one provider per entry.
    TArray<FVoxelIntProvider> Inner;

    static FVoxelIntProvider MakeConstant(int32 Value);
    static FVoxelIntProvider MakeUniform(int32 Min, int32 Max);
    static FVoxelIntProvider MakeTrapezoid(int32 Min, int32 Max, int32 Plateau);

    int32 Sample(FVoxelXoroshiro& Random) const
    {
        switch (Type)
        {
        case EType::Uniform:
            return Random.NextRange(Min, Max);
        case EType::Trapezoid:
            return Min + Random.NextInt(SpanA + 1) + Random.NextInt(SpanB + 1);
        case EType::BiasedToBottom:
            return Min + Random.NextInt(1 + Random.NextInt(Max - Min + 1));
        case EType::WeightedList:
        {
            int32 Pick = Random.NextInt(SpanA);
            for (int32 Entry = 0; Entry < Entries.Num(); ++Entry)
                if ((Pick -= Entries[Entry].Y) < 0) return Inner[Entry].Sample(Random);
            return Inner.Last().Sample(Random);
        }
        case EType::Clamped:
            return FMath::Clamp(Inner[0].Sample(Random), Min, Max);
        case EType::VeryBiasedToBottom:
            return Min + Random.NextInt(1 + Random.NextInt(1 + Random.NextInt(Max - Min + 1)));
        default:
            return Min;
        }
    }

    // The exact probability of each value from Min to Max, for the distribution tests.
    void GetDistribution(TArray<double>& Out) const;
};

// A compiled float provider (the carvers', S10): a constant, a uniform range [Min, Max), or a trapezoid (the sum of
// two uniform parts, SpanA + SpanB = Max - Min and SpanB - SpanA = the plateau).
struct UNNAMEDVOXEL_API FVoxelFloatProvider
{
    enum class EType : uint8
    {
        Constant,
        Uniform,
        Trapezoid
    };

    EType Type = EType::Constant;
    float Min = 0;
    float Max = 0;
    float SpanA = 0;
    float SpanB = 0;

    static FVoxelFloatProvider MakeConstant(float Value);
    static FVoxelFloatProvider MakeUniform(float Min, float Max);
    static FVoxelFloatProvider MakeTrapezoid(float Min, float Max, float Plateau);

    float Sample(FVoxelXoroshiro& Random) const
    {
        switch (Type)
        {
        case EType::Uniform:
            return Min + (Max - Min) * Random.NextFloat();
        case EType::Trapezoid:
        {
            const float A = Random.NextFloat();
            return Min + SpanA * A + SpanB * Random.NextFloat();
        }
        default:
            return Min;
        }
    }

    // The exact probability of a value below X, for the distribution tests.
    double GetCdf(double X) const;
};

namespace VoxelNumberProviders
{
    // An int field's value: a bare number, or an int provider node of a compiled type.
    UNNAMEDVOXEL_API bool CompileInt(
        const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value, FVoxelIntProvider& Out, FString* OutError);
    // A height provider node (uniform or trapezoid) or a bare vertical anchor, with every anchor resolved against the
    // world's Z range: absolute, above_bottom (from MinZ) or below_top (from MaxZ).
    UNNAMEDVOXEL_API bool CompileHeight(
        const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value, FVoxelIntProvider& Out, FString* OutError);
    UNNAMEDVOXEL_API bool ResolveAnchor(const FVoxelFeatureGraph& Graph, int32 Node, int32& OutZ, FString* OutError);
    // A float field's value: a bare number, or a float provider node (constant, uniform or trapezoid).
    UNNAMEDVOXEL_API bool CompileFloat(
        const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value, FVoxelFloatProvider& Out, FString* OutError);
}
