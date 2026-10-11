#pragma once

#include "CoreMinimal.h"
#include "Decoration/Features/VoxelTreeFeature.h"

// Phase 6's S9d part 2: the fallen trees and the huge mushrooms (minecraft.wiki "Fallen tree", "Huge mushroom"). Where
// the wiki is silent the shapes are ours, fitted to scout J's probes (facts/feature_probes/tree_fallen_*.json and
// tree_huge_*.json; Docs/VoxelWorld.md lists the rules). A fallen tree is built on an FVoxelTreeBuilder, so the tree
// decorators run on it unchanged.

// fallen_tree's data.
struct UNNAMEDVOXEL_API FVoxelFallenTreeConfig
{
    // The builder's tree: trunk_provider's states per axis, the free set, and stump_decorators as its decorators; no
    // trunk or foliage placer (both Count).
    FVoxelTreeConfig Tree;
    // log_decorators, in the data's order.
    TArray<FVoxelTreeDecoratorConfig> LogDecorators;
    FVoxelIntProvider LogLength;
};

// huge_red_mushroom's and huge_brown_mushroom's data.
struct UNNAMEDVOXEL_API FVoxelHugeMushroomConfig
{
    bool bRed = false;
    // foliage_radius (the wiki's default 2).
    int32 Radius = 2;
    // stem_provider's state, and the cap's cells (cap_provider's state with its faces set) relative to the cap's top
    // layer above the origin.
    uint16 Stem = 0;
    TArray<TPair<FIntVector, uint16>> Cap;
    // can_place_on (a predicate, tested at the block under the origin), and where the stem and cap go (a block set: air
    // and replaceable_by_trees; the reference has no replaceable_by_mushrooms tag).
    int32 CanPlaceOn = INDEX_NONE;
    int32 FreeSet = INDEX_NONE;
};

namespace VoxelBigVegetation
{
    // A fallen_tree feature node compiled. A trunk provider type not built leaves OutSkip naming it (the feature is
    // then skipped); a decorator not built (or one that reads more than its logs) is left out and named in
    // Out.Tree.SkippedDecorators; false on malformed data or a reach above 16.
    UNNAMEDVOXEL_API bool CompileFallenTree(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags,
        const FVoxelBlockTable& Table, FVoxelBlockPredicates& Predicates, FVoxelStateProviders& Providers,
        FVoxelFallenTreeConfig& Out, FString& OutSkip, FString* OutError);
    // Places one fallen tree at Origin: the stump, the log beside it where it fits, the stump's and the log's
    // decorators, then every cell. Returns whether it placed (the stump stands). Its time goes to Times when given
    // (the decorators' to theirs).
    UNNAMEDVOXEL_API bool PlaceFallenTree(const FVoxelFallenTreeConfig& Config, FIntVector Origin,
        FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env, FVoxelTreeTimes* Times = nullptr);

    // A huge_red_mushroom (bRed) or huge_brown_mushroom node compiled. A provider or predicate type not built leaves
    // OutSkip naming it; false on malformed data.
    UNNAMEDVOXEL_API bool CompileHugeMushroom(const FVoxelFeatureGraph& Graph, int32 Node, bool bRed,
        FVoxelBlockTags& Tags, const FVoxelBlockTable& Table, FVoxelBlockPredicates& Predicates,
        FVoxelStateProviders& Providers, FVoxelHugeMushroomConfig& Out, FString& OutSkip, FString* OutError);
    // Places one huge mushroom at Origin (the stem's bottom cell). Returns whether it placed. Its time goes to Times
    // when given.
    UNNAMEDVOXEL_API bool PlaceHugeMushroom(const FVoxelHugeMushroomConfig& Config, FIntVector Origin,
        FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env, FVoxelTreeTimes* Times = nullptr);
}
