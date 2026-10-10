#pragma once

#include "CoreMinimal.h"
#include "Decoration/VoxelNumberProviders.h"
#include "Random/VoxelRandom.h"

class FVoxelBlockPredicates;
class FVoxelBlockTable;
class FVoxelBlockTags;
class FVoxelFeatureGraph;
class FVoxelStateProviders;
struct FVoxelPlantEnvironment;

// The tree feature of phase 6's S9a (minecraft.wiki "Tree definition"): trunk, foliage and root placers, feature sizes,
// the soil under the trunk and the leaves' distance. The placers' shapes are ours where the wiki is silent, fitted to
// scout J's probes (facts/feature_probes/tree_*.json) as S2 fitted the ore blob; Docs/VoxelWorld.md lists the rules.
// Decorators come in S9b: a tree's decorators are left out and counted. Coordinates are ours: X east, Y south, Z up
// (the reference's x, z and y).
enum class EVoxelTrunkPlacer : uint8
{
    Straight,
    Forking,
    Giant,
    MegaJungle,
    DarkOak,
    Fancy,
    Bending,
    UpwardsBranching,
    Cherry,
    Poplar,
    Count
};

enum class EVoxelFoliagePlacer : uint8
{
    Blob,
    Bush,
    Fancy,
    Jungle,
    Spruce,
    Pine,
    MegaPine,
    Acacia,
    DarkOak,
    RandomSpread,
    Cherry,
    Poplar,
    Count
};

// A placer's data fields, compiled by name: every int field (a bare whole number or an int provider), every number,
// and one block list (can_grow_through) as a block set. Each placer reads the fields it needs.
struct UNNAMEDVOXEL_API FVoxelTreePlacerConfig
{
    uint8 Type = 0;
    TArray<TPair<FName, FVoxelIntProvider>> Ints;
    TArray<TPair<FName, double>> Numbers;
    // A predicate block set (FVoxelBlockPredicates::GetSet), or INDEX_NONE.
    int32 BlockSet = INDEX_NONE;

    const FVoxelIntProvider* FindInt(FName Name) const;
    int32 SampleInt(FName Name, FVoxelXoroshiro& Random, int32 Default = 0) const;
    // The largest value the int field can take (Default when absent).
    int32 GetMaxInt(FName Name, int32 Default = 0) const;
    double GetNumber(FName Name, double Default = 0) const;
};

// minimum_size: two_layers_feature_size or three_layers_feature_size (wiki defaults).
struct FVoxelFeatureSize
{
    bool bThreeLayers = false;
    int32 Limit = 1, UpperLimit = 1, LowerSize = 0, MiddleSize = 1, UpperSize = 1;
    // min_clipped_height, or -1 when the data has none.
    int32 MinClippedHeight = -1;
};

// mangrove_root_placer's data.
struct FVoxelMangroveRoots
{
    bool bEnabled = false;
    // State providers (FVoxelStateProviders) for the roots, the muddy roots and what goes on top of roots.
    int32 RootProvider = INDEX_NONE, MuddyRootProvider = INDEX_NONE, AboveRootProvider = INDEX_NONE;
    FVoxelIntProvider TrunkOffsetY;
    float AboveRootChance = 0, RandomSkewChance = 0;
    int32 MaxRootWidth = 0, MaxRootLength = 0;
    // Predicate block sets: what roots grow through, and the blocks that turn roots muddy.
    int32 CanGrowThroughSet = INDEX_NONE, MuddyRootsInSet = INDEX_NONE;
};

struct UNNAMEDVOXEL_API FVoxelTreeConfig
{
    FString Id;
    FVoxelTreePlacerConfig Trunk;
    FVoxelTreePlacerConfig Foliage;
    FVoxelFeatureSize Size;
    FVoxelMangroveRoots Roots;
    // State providers: the trunk's (axis y) and the foliage's (distance 7) states, and the soil under the trunk.
    int32 TrunkProvider = INDEX_NONE, FoliageProvider = INDEX_NONE, BelowTrunkProvider = INDEX_NONE;
    // The trunk state per axis (0 X, 1 Y, 2 Z: the data's x, z, y), and per foliage state its states by distance 1..6
    // and waterlogging (index (Distance - 1) * 2 + Wet).
    uint16 LogStates[3] = {};
    TMap<uint16, TArray<uint16>> LeafStates;
    // Air plus replaceable_by_trees: where logs, leaves and roots may go (a predicate block set, IfReplaceable's
    // argument).
    int32 FreeSet = INDEX_NONE;
    bool bIgnoreVines = false;
    // How far a tree's cells can lie from its origin horizontally (Chebyshev), asserted at most 16 at compile.
    int32 Reach = 0;
    // The data's decorators, left out until S9c, by type.
    TArray<FString> SkippedDecorators;
};

// Where a trunk wants foliage: the cell above its top log (or a branch end), a radius bonus, and whether the trunk is
// 2 x 2 there (the foliage then centres on the four columns). A poplar trunk also says how far below Position its
// branch layer lies, where its foliage ends (0 for every other trunk). A mega jungle branch also says whether its
// canopy gets its bottom layer, drawn in the trunk's stream between the branch's angle and the next branch (ours,
// fitted (S9b refit); false for every other attachment).
struct FVoxelFoliageAttachment
{
    FIntVector Position = FIntVector::ZeroValue;
    int32 RadiusBonus = 0;
    bool bDoubleTrunk = false;
    int32 BranchDepth = 0;
    bool bBottomLayer = false;
};

// One tree under construction: its own cells over the working copy, in the order they were set. A cell holds one kind;
// a log may replace the tree's own leaf, a leaf never replaces the tree's log or root.
class UNNAMEDVOXEL_API FVoxelTreeBuilder
{
public:
    enum class ECell : uint8
    {
        Log,
        Leaf,
        Root,
        // The soil under the trunk (below_trunk_provider).
        Ground
    };

    FVoxelTreeBuilder(const FVoxelTreeConfig& InConfig, const FVoxelPlantEnvironment& InEnv, FVoxelXoroshiro& InRandom);

    // The state at Cell: the tree's own write, else the working copy's.
    uint16 GetState(FIntVector Cell) const;
    // Air or replaceable_by_trees in the view above (the tree's own leaves included), inside the world.
    bool IsFree(FIntVector Cell) const;
    // A log along Axis (0 X, 1 Y, 2 Z) where the cell is free or holds this tree's log or leaf.
    bool SetLog(FIntVector Cell, int32 Axis = 2);
    // A leaf where the cell is free and holds none of this tree's logs or roots.
    bool SetLeaf(FIntVector Cell);
    // A root state (mangrove roots, muddy roots, moss carpet) anywhere but this tree's logs.
    bool SetRoot(FIntVector Cell, uint16 State);
    // The soil under a bottom trunk cell: below_trunk_provider's state at Cell, when it differs from the cell's.
    void SetGroundBelow(FIntVector Cell);

    bool IsLog(FIntVector Cell) const;
    bool IsLeaf(FIntVector Cell) const;
    int32 GetCellCount() const { return Cells.Num(); }

    const FVoxelTreeConfig& Config;
    const FVoxelPlantEnvironment& Env;
    FVoxelXoroshiro& Random;
    // The trunk's height after the feature size, and the origin after the root placer's offset.
    int32 Height = 0;
    FIntVector Origin = FIntVector::ZeroValue;

    struct FCell
    {
        FIntVector Position;
        uint16 State = 0;
        ECell Kind = ECell::Log;
    };
    TArray<FCell> Cells;

private:
    int32* FindCell(FIntVector Cell) { return IndexOf.Find(Cell); }
    const int32* FindCell(FIntVector Cell) const { return IndexOf.Find(Cell); }
    void Set(FIntVector Cell, uint16 State, ECell Kind);
    TMap<FIntVector, int32> IndexOf;
};

namespace VoxelTrunkPlacers
{
    // The trunk placer of a data type id ("minecraft:straight_trunk_placer"), or Count when not built.
    UNNAMEDVOXEL_API EVoxelTrunkPlacer FindType(FStringView Type);
    // Places the trunk of Builder.Height from Builder.Origin (with the soil under its bottom cells) and adds the
    // foliage attachments.
    UNNAMEDVOXEL_API void Place(
        const FVoxelTreePlacerConfig& Trunk, FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out);
    // The columns the feature size checks around (one, or four for a 2 x 2 trunk), relative to the origin.
    UNNAMEDVOXEL_API TConstArrayView<FIntPoint> GetFootprint(EVoxelTrunkPlacer Type);
    // The farthest a log or attachment lies from the origin horizontally (Chebyshev).
    UNNAMEDVOXEL_API int32 GetReach(const FVoxelTreePlacerConfig& Trunk);
}

namespace VoxelFoliagePlacers
{
    UNNAMEDVOXEL_API EVoxelFoliagePlacer FindType(FStringView Type);
    // Places the foliage at every attachment.
    UNNAMEDVOXEL_API void Place(const FVoxelTreePlacerConfig& Foliage, FVoxelTreeBuilder& Builder,
        TConstArrayView<FVoxelFoliageAttachment> Attachments);
    // The farthest a leaf lies from its attachment horizontally (Chebyshev), radius bonus included.
    UNNAMEDVOXEL_API int32 GetReach(const FVoxelTreePlacerConfig& Foliage);
}

namespace VoxelRootPlacers
{
    // Whether mangrove_root_placer is built (a tree with a root placer that is not is skipped).
    UNNAMEDVOXEL_API bool IsMangroveBuilt();
    // mangrove_root_placer: raises Builder.Origin by trunk_offset_y and grows the roots under it. False when the tree
    // fails (nothing is emitted).
    UNNAMEDVOXEL_API bool PlaceMangrove(const FVoxelMangroveRoots& Roots, FVoxelTreeBuilder& Builder);
    UNNAMEDVOXEL_API int32 GetReach(const FVoxelMangroveRoots& Roots);
}

namespace VoxelTreeFeature
{
    // A tree feature node compiled. A placer type not built yet leaves OutSkip naming it; false on malformed data or a
    // reach above 16 (the 3 x 3 window's margin).
    UNNAMEDVOXEL_API bool Compile(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags,
        const FVoxelBlockTable& Table, FVoxelBlockPredicates& Predicates, FVoxelStateProviders& Providers,
        FVoxelTreeConfig& Out, FString& OutSkip, FString* OutError);

    // The trunk height the feature size allows (Height when every layer is free, a clipped height, or INDEX_NONE when
    // the tree fails): layers 0 to Height + 1 above the origin, each a square of its layer's size around every
    // footprint column, must be free. Ours, fitted to scout J's ceiling probes.
    UNNAMEDVOXEL_API int32 GetFreeHeight(
        const FVoxelTreeConfig& Config, const FVoxelTreeBuilder& Builder, int32 Height);

    // Places one tree at Origin: trunk height, roots, feature size, trunk, foliage, then the leaves' distance (a BFS
    // from the tree's own logs through its own leaves; leaves 7 or more away are dropped, Jesus's answer to scout J's
    // question 4), and emits every cell. Returns whether it placed.
    UNNAMEDVOXEL_API bool Place(
        const FVoxelTreeConfig& Config, FIntVector Origin, FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env);

    // The leaves' distances for a builder's cells (1..6, 0 for a leaf to drop), in cell order; for the tests.
    UNNAMEDVOXEL_API void GetLeafDistances(const FVoxelTreeBuilder& Builder, TArray<uint8>& Out);
}
