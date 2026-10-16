#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"

struct FFoundationBlockWorld;
struct FFoundationPlacementContext;

// The chest and the trapped chest (T1 step 18-chest; the trapped chest's redstone is not modelled): single or paired
// into a double chest by their `type` (single, left, right). Pairing is vanilla's ChestBlock as measured: a type=left
// half's partner is on its facing's clockwise side seen from above (N->E, E->S, S->W, W->N), a type=right half's on
// the anticlockwise side (the black-box run C:\dev\sandbox\inv18\chest_probe.txt; VALUES "Double chests",
// blocks.chest_neighbor_pairing). FoundationBlockFamilies calls in here from its placement and shape entry points.
namespace FoundationChests
{
    enum class EType : uint8
    {
        Single,
        Left,
        Right
    };

    // foundation.Chest: 1 a chest opens its menu (27 slots, or 54 over a double chest) and pairs on placement; 0 as
    // before: no block use (a right-click places against it) and every new chest is single.
    UNNAMED_API bool IsOn();
    UNNAMED_API bool IsChest(FName Block);
    UNNAMED_API EType GetType(const FFoundationBlockRegistry& Registry, FFoundationBlockState State);
    // The side a paired half's partner is on; None for a single chest or another block.
    UNNAMED_API EFoundationFace GetPartnerSide(const FFoundationBlockRegistry& Registry, FFoundationBlockState State);
    // A paired half's partner cell, when the partner is the same chest, faces the same way and points back.
    UNNAMED_API bool GetPartner(const FFoundationBlockWorld& World, const FIntVector& Cell, FIntVector& OutPartner);
    // A double chest's halves in menu order: the type=right half (menu slots 0-26) then the type=left (27-53; the
    // hopper probe in chest_probe.txt). False when Cell is no paired half.
    UNNAMED_API bool GetHalves(
        const FFoundationBlockWorld& World, const FIntVector& Cell, FIntVector& OutRight, FIntVector& OutLeft);
    // getStateForPlacement: facing opposite the horizontal look and waterlogged in a water source (the plant rules'),
    // then the type: with secondary use on a single chest's side face, join the clicked chest (taking its facing) when
    // its facing is across the clicked face; without, pair with a single same-id chest of the same facing on the
    // clockwise side (type=left), else the anticlockwise side (type=right; which wins with both is provisional);
    // else single.
    UNNAMED_API FFoundationBlockState GetStateForPlacement(const FFoundationBlockWorld& World,
        const FFoundationBlockType& Block, const FFoundationPlacementContext& Context);
    // updateShape: a single chest whose side neighbour is a half of the same id and facing pointing at it takes the
    // opposite type; a half whose partner side no longer holds a half pointing back turns single (a same-id chest
    // there that points elsewhere unpairs it too, which keeps GetPartner symmetric).
    UNNAMED_API FFoundationBlockState UpdateShape(const FFoundationBlockRegistry& Registry, FFoundationBlockState State,
        EFoundationFace Toward, FFoundationBlockState Neighbour);
    // A chest does not open under a redstone conductor (minecraft.wiki "Chest"), approximated as a full opaque cube
    // (provisional: isRedstoneConductor is code only) less the opaque cubes vanilla leaves non-conductive: glowstone,
    // redstone_block and observer (isRedstoneConductor never), soul_sand and mud (collision lower than a full block)
    // (QC R37-1, minecraft.wiki "Conductivity"). Unknown cells do not block.
    UNNAMED_API bool IsBlockedAbove(const FFoundationBlockWorld& World, const FIntVector& Cell);

    // The open and close sounds' volume (value-swaps.md l.261, survival.r5.play_calls): 0.5. The view adds the pitch
    // 0.9 + 0.1U and the event's attenuation (12, DA_Sounds).
    constexpr float SoundVolume = .5f;
}
