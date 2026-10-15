#pragma once

#include "CoreMinimal.h"
#include "Decoration/VoxelNumberProviders.h"
#include "Noise/VoxelNormalNoise.h"
#include "Random/VoxelRandom.h"

class FVoxelBlockPredicates;
class FVoxelBlockTable;
class FVoxelBlockTags;
class FVoxelDecorationContext;
class FVoxelFeatureGraph;
struct FVoxelFeatureValue;

// 26.3's block state providers (minecraft.wiki "Block state provider"), compiled to one flat array: a bare state
// (simple), weighted, rule_based, randomized_int, and the three noise providers (noise, noise_threshold, dual_noise),
// named or inline. The noise providers' noises are ours (FVoxelNormalNoise), seeded from the world seed and the data's
// "seed" (VoxelFeatureSeeds::GetPatternSeed, Jesus's answer 4), so the flower bands differ per world; the mapping from
// noise to state is ours where the wiki leaves it open (noise: linear over [-1, 1); dual_noise below). rotated and
// random_block (coral, S12) are not built: a placed feature reaching them is skipped.
class UNNAMEDVOXEL_API FVoxelStateProviders
{
public:
    enum class EOp : uint8
    {
        Simple,
        Weighted,
        RuleBased,
        RandomizedInt,
        Noise,
        NoiseThreshold,
        DualNoise
    };

    struct FNode
    {
        EOp Op = EOp::Simple;
        // Simple: the state; NoiseThreshold: the default state.
        uint16 State = 0;
        // Weighted, Noise, DualNoise, RandomizedInt: States[First .. First + Count); RuleBased: Rules[...];
        // NoiseThreshold: the low states.
        int32 First = 0, Count = 0;
        // NoiseThreshold: the high states, States[Second .. Second + SecondCount).
        int32 Second = 0, SecondCount = 0;
        // Weighted: the weights' sum (Weights run beside States).
        int32 Total = 0;
        // RuleBased: the provider when no rule matches; INDEX_NONE keeps the cell's state.
        int32 Fallback = INDEX_NONE;
        int32 Noise = INDEX_NONE, SlowNoise = INDEX_NONE;
        double Scale = 1, SlowScale = 1, Threshold = 0;
        float Chance = 0;
        // RandomizedInt: the values; DualNoise: the variety (Min to Max states).
        FVoxelIntProvider Values;
    };

    void Initialize(const FVoxelBlockTable& Table, const FVoxelBlockPredicates& Predicates, uint64 WorldSeed);

    // Compiles a provider value (a state, a provider node or a named provider) to OutProvider. A type not built leaves
    // OutSkip naming it; false only on malformed data. Predicates compiles rule_based's tests.
    bool Compile(const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value, FVoxelBlockTags& Tags,
        FVoxelBlockPredicates& Predicates, int32& OutProvider, FString& OutSkip, FString* OutError);

    uint16 Sample(
        int32 Provider, FVoxelXoroshiro& Random, FIntVector Position, const FVoxelDecorationContext& Context) const;
    // noise_threshold's rule for a given noise value (the distribution test injects it).
    uint16 SampleThreshold(int32 Provider, double NoiseValue, FVoxelXoroshiro& Random) const;
    // The noise value a noise provider reads at a position (its first noise).
    double GetNoise(int32 Provider, FIntVector Position) const;
    // Every state a provider can give, without repeats.
    void GetStates(int32 Provider, TArray<uint16>& Out) const;
    const FNode& GetNode(int32 Provider) const { return Nodes[Provider]; }
    // Every provider that gives From gives To instead; how many states it swapped.
    int32 ReplaceState(uint16 From, uint16 To);
    TConstArrayView<uint16> GetStateList() const { return States; }
    TConstArrayView<int32> GetWeights() const { return Weights; }

private:
    int32 AddNoise(const FVoxelFeatureGraph& Graph, int32 Node, uint64 DataSeed, const FString& Name);

    TArray<FNode> Nodes;
    TArray<uint16> States;
    TArray<int32> Weights;
    // RuleBased: (predicate root, provider).
    TArray<FIntPoint> Rules;
    TArray<FVoxelNormalNoise> Noises;
    const FVoxelBlockTable* Table = nullptr;
    const FVoxelBlockPredicates* Tests = nullptr;
    uint64 WorldSeed = 0;
};
