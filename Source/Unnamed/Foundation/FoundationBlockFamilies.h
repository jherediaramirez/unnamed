#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlockRegistry.h"

struct FFoundationBlockWorld;

// The block families with placement and shape rules of their own (survival track T5). B2: the connecting blocks and
// stairs; later steps add theirs (doors, trapdoors, buttons) here.
enum class EFoundationBlockFamily : uint8
{
    None,
    // #minecraft:stairs: facing, half, shape.
    Stairs,
    // #minecraft:fences: a side per horizontal direction.
    Fence,
    // #minecraft:walls: none, low or tall per side, and the post (up).
    Wall,
    // Glass panes and every bars block (the IronBarsBlock class): the fences' property set without the tag.
    Pane,
    // #minecraft:fence_gates: facing, in_wall (open and powered are B3's).
    FenceGate,
};

// Survival B2b's rules for those families, behind foundation.Blocks.Shapes (FoundationBlockFamilies::AreShapesEnabled,
// FoundationBlockRegistry.h): placement takes facing, half and connections, and an edit updates its neighbours' shapes.
// Behaviour is BlockState.updateShape as measured in Reference/minecraft-blocks-26.3/facts/shape_updates.json (the
// tests compare every row), and MINECRAFT-26.3-VALUES.md "Stair corners" and "Fence gates".
namespace FoundationBlockFamilies
{
    // Reads the state at a cell; cells nobody knows read as air.
    using FGetState = TFunctionRef<FFoundationBlockState(const FIntVector& Cell)>;

    UNNAMED_API EFoundationBlockFamily GetFamily(const FFoundationBlockType& Block);
    // The faces of a state that are sturdy (isFaceSturdy, full support), a bit per EFoundationFace: the faces its
    // collision boxes cover whole, clipped to the cell, apart from the measured exceptions (shape_updates.json).
    UNNAMED_API uint8 GetSturdyFaces(const FFoundationBlockRegistry& Registry, FFoundationBlockState State);
    // One face of GetSturdyFaces (Block.isFaceSturdy, full support).
    UNNAMED_API bool IsFaceSturdy(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, EFoundationFace Face);
    // BlockState.updateShape: State at Cell, told that its neighbour toward Toward is now Neighbour. Other cells (the
    // wall's above, the stair's sides, the gate's other side) come from Get. Non-family states come back unchanged.
    UNNAMED_API FFoundationBlockState UpdateShape(const FFoundationBlockRegistry& Registry, FFoundationBlockState State,
        const FIntVector& Cell, EFoundationFace Toward, FFoundationBlockState Neighbour, FGetState Get);
    // UpdateShape toward down, up, north, south, west, east with the states Get finds there, until a pass changes
    // nothing (at most 3 passes): the oracle's settle rule, and how placement takes its connections.
    UNNAMED_API FFoundationBlockState SettleShape(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, const FIntVector& Cell, FGetState Get);
    // getStateForPlacement for a family block at Context.Destination: stairs face the player's horizontal look with
    // the half from the click (bTopHalf: the slab rule), gates face the look (VALUES "Fence gates",
    // blocks.fence_gate_placement), waterlogged in a water source, then SettleShape. Others: the default state.
    UNNAMED_API FFoundationBlockState GetStateForPlacement(const FFoundationBlockWorld& World,
        const FFoundationBlockType& Block, const FFoundationPlacementContext& Context, bool bTopHalf);
    // After Cell changed: each family block among its 4 horizontal neighbours, and a wall below it, takes one
    // UpdateShape toward Cell. One ring, no cascade; unknown cells are skipped. Nothing while the switch is 0.
    UNNAMED_API void UpdateNeighbourShapes(FFoundationBlockWorld& World, const FIntVector& Cell);
}
