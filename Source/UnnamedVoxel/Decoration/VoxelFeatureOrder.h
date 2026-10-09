#pragma once

#include "CoreMinimal.h"
#include "Random/VoxelRandom.h"

class FVoxelFeatureGraph;

// One global order of the placed features per generation step. Vanilla sorts the biomes' lists in code; ours is a
// topological sort of the lists every biome gives for the step (a feature before those that follow it in any biome),
// taking the smallest id among the features that are free to go next. It decides the order features run in a column
// and their priority when writes meet.
struct UNNAMEDVOXEL_API FVoxelFeatureOrder
{
    // Placed feature entries, per step in order.
    TArray<TArray<int32>> Steps;
    // Per placed feature entry: its step and its index in that step, or INDEX_NONE for entries no biome lists.
    TArray<int32> StepOf;
    TArray<int32> IndexInStep;

    // Fails, naming the step and the features left, if the lists of one step contradict each other.
    bool Build(const FVoxelFeatureGraph& Graph, FString* OutError = nullptr);
};

// Feature seeds (Jesus's answer 4, phase 6 plan 3.2). Every constant here decides decorated worlds, so tests pin them.
namespace VoxelFeatureSeeds
{
    constexpr uint64 DecorationSalt = VoxelRandom::Salt("decoration");
    constexpr uint64 PatternSalt = VoxelRandom::Salt("feature_pattern");
    // The fixed seed vanilla gives the noise of noise_threshold_count and noise_based_count; the flower providers
    // carry theirs in the data ("seed").
    constexpr int64 VanillaPatternSeed = 2345;

    // A placed feature's salt: a stable hash of its full id ("minecraft:ore_iron_upper"), never of an index or the
    // read order. 64-bit FNV-1a over the id's UTF-8 bytes, finished with VoxelRandom::Mix64.
    UNNAMEDVOXEL_API uint64 GetFeatureSalt(FStringView Id);

    // The seed of one placed feature's random stream in one column and step.
    constexpr uint64 GetStreamSeed(uint64 WorldSeed, uint64 FeatureSalt, int32 ColumnX, int32 ColumnZ, int32 Step)
    {
        return VoxelRandom::Hash64(WorldSeed, DecorationSalt ^ FeatureSalt, ColumnX, ColumnZ, Step);
    }

    // The seed of a flower band or noise-count pattern noise. Vanilla seeds these from fixed numbers, so they sit in
    // the same place in every world; ours mix in the world seed. DataSeed is the provider's "seed" field, or
    // VanillaPatternSeed for the count modifiers.
    constexpr uint64 GetPatternSeed(uint64 WorldSeed, int64 DataSeed)
    {
        return VoxelRandom::Hash64(WorldSeed, PatternSalt, int32(uint64(DataSeed)), int32(uint64(DataSeed) >> 32));
    }
}
