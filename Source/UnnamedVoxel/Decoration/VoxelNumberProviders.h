#pragma once

#include "CoreMinimal.h"
#include "Random/VoxelRandom.h"

class FVoxelFeatureGraph;
struct FVoxelFeatureValue;

// A compiled int provider, or a height provider with its anchors resolved to Z: a constant, a uniform range, or a
// trapezoid (the sum of two uniform parts, a triangle when the plateau is 0). These are the types the ore placements
// reach; Providers.Distribution checks each against the jar's histograms.
struct UNNAMEDVOXEL_API FVoxelIntProvider
{
    enum class EType : uint8
    {
        Constant,
        Uniform,
        Trapezoid
    };

    EType Type = EType::Constant;
    // Both included.
    int32 Min = 0;
    int32 Max = 0;
    // Trapezoid: the widths of the two uniform parts, SpanA + SpanB = Max - Min and SpanB - SpanA = the plateau.
    int32 SpanA = 0;
    int32 SpanB = 0;

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
        default:
            return Min;
        }
    }

    // The exact probability of each value from Min to Max, for the distribution tests.
    void GetDistribution(TArray<double>& Out) const;
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
}
