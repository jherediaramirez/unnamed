#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlocks.h"

struct FFoundationPlacementContext;

// Placement for the model blocks (plants, multiface blocks, amethyst, dripstone; phase 6, S7): their state from the
// click, and the two halves of double plants. Behaviour comes from the blocks reference (properties, facts), scout H's
// measured support table (Docs/Records/2026-10-10-Voxel-Phase6-Scout-H-Plant-Interaction.md, T1) and minecraft.wiki;
// rows marked (inferred) follow from those, not from a stated rule.
namespace FoundationPlantRules
{
    // getStateForPlacement for a model block, from its properties:
    // - half (tall grass, large fern, the tall flowers, small dripleaf): the lower half; the authority writes the
    //   upper half above it (GetOtherHalf);
    // - facing: amethyst cluster and buds, shelf mushroom and coral wall fans point away from the clicked block (wiki:
    //   Amethyst Bud, "facing equals the anchoring face"), cocoa toward it (T1); the other horizontal ones (petals,
    //   leaf litter, dripleaves) face the player (inferred);
    // - vine, glow lichen and sculk vein: the one face toward the clicked block (wiki: Vines, Glow Lichen). A vine is
    //   never placed on an underside until Jesus decides its up face (scout H, Q8): that state has no face;
    // - vertical_direction (pointed dripstone, sulfur spike): up on a top face, down on a bottom face, and on a side
    //   against the vertical look (wiki: Pointed Dripstone; the side case inferred);
    // - waterlogged: when the destination holds a water source (wiki: Waterlogging).
    UNNAMED_API FFoundationBlockState GetStateForPlacement(const FFoundationBlockWorld& World,
        const FFoundationBlockType& Block, const FFoundationPlacementContext& Context);

    // A double plant's half (a model state with half=lower or upper): the cell of its other half, and the state that
    // half takes there (waterlogged from that cell). False for every other state.
    UNNAMED_API bool GetOtherHalf(const FFoundationBlockWorld& World, const FIntVector& Cell,
        FFoundationBlockState State, FIntVector& OutCell, FFoundationBlockState& OutState);
    // Whether Other is State's other half: the same double plant with the other half value.
    UNNAMED_API bool IsOtherHalf(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, FFoundationBlockState Other);
    // A vine, glow lichen or sculk vein state without a face: never placed (the click found no face to hold).
    UNNAMED_API bool HasNoFace(const FFoundationBlockRegistry& Registry, FFoundationBlockState State);
}
