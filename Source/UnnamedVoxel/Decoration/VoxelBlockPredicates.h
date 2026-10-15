#pragma once

#include "CoreMinimal.h"
#include "Decoration/VoxelRuleTests.h"

class FVoxelBlockTable;
class FVoxelDecorationContext;
class FVoxelFeatureGraph;
class FVoxelPlantSurvival;
struct FVoxelFeatureValue;

// 26.3's block predicates (minecraft.wiki "Block predicate"), the placement filters and feature checks of plants and
// patches, compiled to one flat node array. A leaf tests the cell at its offset from the position; offsets are the
// reference's [x, y, z] turned to our axes (X, Z, Y). Out-of-world cells read as air, as the working copy gives them.
// Built: true, matching_blocks, matching_block_tag, matching_fluids, solid, replaceable, has_sturdy_face,
// would_survive, inside_world_bounds, all_of, any_of, not. A placed feature reaching another type is skipped.
class UNNAMEDVOXEL_API FVoxelBlockPredicates
{
public:
    enum class EOp : uint8
    {
        True,
        // The block is in Sets[Argument] (matching_blocks, matching_block_tag, replaceable).
        InSet,
        // The fluid is one of Argument's bits (FluidBits).
        Fluids,
        // The block blocks motion (26.3's "solid", read as blocks_motion).
        Solid,
        // The face Argument (EVoxelFace) is sturdy.
        Sturdy,
        // State Argument would survive there (FVoxelPlantSurvival).
        WouldSurvive,
        InsideWorld,
        AllOf,
        AnyOf,
        Not
    };

    // matching_fluids' fluids, one bit each.
    enum EFluidBits : uint8
    {
        WaterSource = 1,
        WaterFlowing = 2,
        LavaSource = 4,
        LavaFlowing = 8
    };

    struct FNode
    {
        EOp Op = EOp::True;
        FIntVector Offset = FIntVector::ZeroValue;
        int32 Argument = 0;
        // Combinators: their children, Children[First .. First + Count).
        int32 First = 0;
        int32 Count = 0;
    };

    void Initialize(const FVoxelBlockTable& Table, const FVoxelPlantSurvival& Survival);

    // Compiles predicate Node to OutRoot. A type not built leaves OutSkip naming it; false only on malformed data.
    bool Compile(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags, int32& OutRoot, FString& OutSkip,
        FString* OutError);
    bool Test(int32 Root, const FVoxelDecorationContext& Context, FIntVector Position) const;

    // Block sets, shared with the writes that re-check one at replay (EVoxelWriteMode::IfReplaceable's argument).
    int32 AddSet(const FVoxelBlockSet& Set) { return Sets.Add(Set); }
    const FVoxelBlockSet& GetSet(int32 Set) const { return Sets[Set]; }
    // The set Root tests when it is a plain block test at offset 0 (a disk's target), else INDEX_NONE.
    int32 GetPlainSet(int32 Root) const;
    uint8 GetFluidBits(uint16 State) const;
    int32 GetNodeCount() const { return Nodes.Num(); }
    // Every would_survive test of From tests To instead; how many it swapped.
    int32 ReplaceState(uint16 From, uint16 To);

private:
    TArray<FNode> Nodes;
    TArray<int32> Children;
    TArray<FVoxelBlockSet> Sets;
    const FVoxelBlockTable* Table = nullptr;
    const FVoxelPlantSurvival* Survival = nullptr;
    int32 WaterBlock = INDEX_NONE, LavaBlock = INDEX_NONE;
    int32 ReplaceableSet = INDEX_NONE;
};

namespace VoxelPredicateReading
{
    // A block state value (a bare id, or {"id", "properties"}) to a state id, or INDEX_NONE when the table lacks it.
    UNNAMEDVOXEL_API int32 ReadState(
        const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value, const FVoxelBlockTable& Table);
    // An offset field ([x, y, z] in the reference's axes) in ours; zero when absent.
    UNNAMEDVOXEL_API FIntVector ReadOffset(const FVoxelFeatureGraph& Graph, int32 Node, const TCHAR* Key);
    // A direction name ("up", "north", ...) as an EVoxelFace index, or INDEX_NONE.
    UNNAMEDVOXEL_API int32 ReadFace(FStringView Name);
    // A blocks field (one id, a "#tag", or a list of ids) as a set over Tags' blocks; ids the table lacks are left
    // out. False on an unknown tag.
    UNNAMEDVOXEL_API bool ReadBlockSet(const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue* Value,
        FVoxelBlockTags& Tags, FVoxelBlockSet& Out, FString* OutError);
}
