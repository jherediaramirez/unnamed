#pragma once

#include "CoreMinimal.h"
#include "Random/VoxelRandom.h"

// 26.3's selector features (minecraft.wiki "Configured feature"): each picks nested placed features to run, which the
// decorator runs from the same position. How a pick spends the stream is ours where the wiki is silent.
enum class EVoxelSelectorOp : uint8
{
    // Entries in order, each taken with its chance; else the default.
    Random,
    // One entry, uniformly.
    SimpleRandom,
    // One entry by weight.
    WeightedRandom,
    // feature_true or feature_false, a fair coin.
    RandomBoolean,
    // Every entry, in order.
    Sequence,
    NoOp
};

struct FVoxelSelectorConfig
{
    EVoxelSelectorOp Op = EVoxelSelectorOp::NoOp;
    // The decorator's nested placed features (random_boolean_selector: true, then false).
    TArray<int32> Placed;
    TArray<float> Chances;
    TArray<int32> Weights;
    int32 TotalWeight = 0;
    int32 Default = INDEX_NONE;
};

namespace VoxelSelectorFeatures
{
    // The nested placed feature a selector runs (not for Sequence or NoOp), or INDEX_NONE for none.
    inline int32 Pick(const FVoxelSelectorConfig& Config, FVoxelXoroshiro& Random)
    {
        switch (Config.Op)
        {
        case EVoxelSelectorOp::Random:
            for (int32 Entry = 0; Entry < Config.Placed.Num(); ++Entry)
                if (Random.NextFloat() < Config.Chances[Entry]) return Config.Placed[Entry];
            return Config.Default;
        case EVoxelSelectorOp::SimpleRandom:
        case EVoxelSelectorOp::RandomBoolean:
            return Config.Placed.IsEmpty() ? INDEX_NONE : Config.Placed[Random.NextInt(Config.Placed.Num())];
        case EVoxelSelectorOp::WeightedRandom:
        {
            int32 Weight = Random.NextInt(Config.TotalWeight);
            for (int32 Entry = 0; Entry < Config.Placed.Num(); ++Entry)
                if ((Weight -= Config.Weights[Entry]) < 0) return Config.Placed[Entry];
            return Config.Placed.Last();
        }
        default:
            return INDEX_NONE;
        }
    }
}
