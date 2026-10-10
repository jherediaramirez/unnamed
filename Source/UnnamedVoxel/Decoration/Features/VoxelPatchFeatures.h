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
    // The selectors, sequence and no_op (FVoxelSelectorConfig).
    Selector
};

struct FVoxelPlantFeature
{
    EVoxelPlantFeature Kind = EVoxelPlantFeature::Selector;
    // simple_block's to_place, disk's state_provider, vegetation_patch's ground_state.
    int32 Provider = INDEX_NONE;
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
}
