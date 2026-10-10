#pragma once

#include "CoreMinimal.h"
#include "Decoration/Features/VoxelSelectorFeatures.h"
#include "Decoration/VoxelNumberProviders.h"
#include "Decoration/VoxelWriteLog.h"
#include "Random/VoxelRandom.h"

class FVoxelBlockPredicates;
class FVoxelBlockTable;
class FVoxelDecorationContext;
class FVoxelPlantSurvival;
class FVoxelStateProviders;

// One write a vegetation feature makes: logged into the target column's bucket and applied to the working copy.
struct FVoxelPlantWrite
{
    FIntVector Cell = FIntVector::ZeroValue;
    uint16 State = 0;
    EVoxelWriteMode Mode = EVoxelWriteMode::IfUnchanged;
    // IfUnchanged: the state the cell held; IfReplaceable: a block set (FVoxelBlockPredicates::GetSet).
    uint16 Argument = 0;
    EVoxelWriteSupport Support = EVoxelWriteSupport::None;
};

// What a vegetation feature reads and writes.
struct FVoxelPlantEnvironment
{
    const FVoxelBlockTable& Table;
    const FVoxelPlantSurvival& Survival;
    const FVoxelBlockPredicates& Predicates;
    const FVoxelStateProviders& Providers;
    FVoxelDecorationContext& Context;
    // A two-cell plant's lower-half states to their upper halves.
    const TMap<uint16, uint16>& UpperOf;
    TFunctionRef<void(const FVoxelPlantWrite&)> Emit;
};

struct FVoxelBlockColumnLayer
{
    FVoxelIntProvider Height;
    int32 Provider = INDEX_NONE;
};

// The vegetation feature types of phase 6's S8 (minecraft.wiki "Configured feature"), compiled.
enum class EVoxelPlantFeature : uint8
{
    SimpleBlock,
    BlockColumn,
    Disk,
    VegetationPatch,
    // multiface_growth (glow lichen).
    Multiface,
    // tree (S9a): Tree indexes the decorator's tree configurations.
    Tree,
    // The selectors, sequence and no_op (FVoxelSelectorConfig).
    Selector
};

struct FVoxelPlantFeature
{
    EVoxelPlantFeature Kind = EVoxelPlantFeature::Selector;
    // simple_block's to_place, disk's state_provider, vegetation_patch's ground_state.
    int32 Provider = INDEX_NONE;
    // tree: an index into the decorator's FVoxelTreeConfig list.
    int32 Tree = INDEX_NONE;
    // block_column: layers in order along Direction (an EVoxelFace), each cell passing Allowed (a predicate).
    TArray<FVoxelBlockColumnLayer> Layers;
    int32 Direction = 5;
    int32 Allowed = INDEX_NONE;
    bool bPrioritizeTip = false;
    // disk: cells within Radius (dx^2 + dz^2 <= r^2) and HalfHeight of the origin that pass Target (a predicate);
    // TargetSet re-checks it at replay when Target is a plain block test.
    int32 Target = INDEX_NONE;
    int32 TargetSet = INDEX_NONE;
    FVoxelIntProvider Radius;
    int32 HalfHeight = 0;
    // vegetation_patch (floor): Radius per axis, ground of ReplaceableSet (a block set) replaced Depth deep, the
    // vegetation (a nested placed feature) on each new ground cell with VegetationChance.
    int32 ReplaceableSet = INDEX_NONE;
    int32 Vegetation = INDEX_NONE;
    FVoxelIntProvider Depth;
    float ExtraBottomChance = 0, VegetationChance = 0, ExtraEdgeChance = 0;
    int32 VerticalRange = 0;
    // multiface_growth: the faces it may attach on (one bit per EVoxelFace), the base blocks (a block set), the
    // block's state attached on each face (dry, then waterlogged), the spreading chance, and how many cells the search
    // walks from the origin (ours: 1; see PlaceMultiface).
    uint8 Faces = 0;
    int32 BaseSet = INDEX_NONE;
    uint16 FaceStates[6][2] = {};
    float SpreadChance = 0;
    int32 SearchSteps = 1;
    FVoxelSelectorConfig Selector;
};

namespace VoxelPatchFeatures
{
    // Places State at Cell if it survives there; a two-cell plant also needs its upper cell free (air, or water for a
    // water plant) and places both. Logs each with its support (EVoxelWriteSupport). Returns whether it placed.
    UNNAMEDVOXEL_API bool PlacePlant(const FVoxelPlantEnvironment& Env, FIntVector Cell, uint16 State);

    // simple_block: the provider's state at Position, if it survives.
    UNNAMEDVOXEL_API bool PlaceSimpleBlock(const FVoxelPlantFeature& Feature, FIntVector Position,
        FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env);
    // block_column: every layer's height drawn first; the free run along Direction (cells passing Allowed, from
    // Position) bounds the total. A shorter run trims the base layers first with prioritize_tip, else the top.
    UNNAMEDVOXEL_API bool PlaceBlockColumn(const FVoxelPlantFeature& Feature, FIntVector Position,
        FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env);
    // The trim alone, for the tests: each layer's cells given the drawn heights and the free run.
    UNNAMEDVOXEL_API void TrimColumn(TArray<int32>& Heights, int32 FreeRun, bool bPrioritizeTip);
    // disk: a flat cylinder, columns x then y, each from its top cell down.
    UNNAMEDVOXEL_API bool PlaceDisk(const FVoxelPlantFeature& Feature, FIntVector Position, FVoxelXoroshiro& Random,
        const FVoxelPlantEnvironment& Env);
    // vegetation_patch's ground (ours where the wiki is silent: a rectangle of the two radii plus one, corners left
    // out, edge columns kept with their chance; each column's floor is the highest replaceable ground under air within
    // vertical_range of Position). Adds the cells above new ground that take vegetation.
    UNNAMEDVOXEL_API bool PlaceVegetationPatch(const FVoxelPlantFeature& Feature, FIntVector Position,
        FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env, TArray<FIntVector>& OutVegetation);
    // multiface_growth (ours, fitted to scout I's room probe, where an origin 4 cells from every wall never placed and
    // one a cell from a wall always placed on the cell toward it, 31 of 50 times with a second block beside it on the
    // same wall): the origin, then a walk of SearchSteps cells along each of the six directions in a shuffled order,
    // stopping at a cell that is neither air nor a water source (an origin inside stone still walks: the probe only
    // tried air origins). The first such cell with an allowed face toward a
    // base block takes the block on that face (its faces tried in a shuffled order). Then, with SpreadChance, one of
    // the four cells beside it in the face's plane takes the same face if it can. The data's search_range (20) would
    // let the centre origin place, so the walk is not that long; it never leaves the 3x3 window (Jesus's answer to
    // scout I's question 3). Returns whether it placed.
    UNNAMEDVOXEL_API bool PlaceMultiface(const FVoxelPlantFeature& Feature, FIntVector Position,
        FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env);
}
