#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlocks.h"

// Where and how a block item is being placed: 26.3's BlockPlaceContext. Directions use EFoundationFace
// (Minecraft north = -Y, south = +Y, east = +X, west = -X).
struct FFoundationPlacementContext
{
    // The targeted cell: a block's own cell, or for a lab anchor the cell just behind the hit face.
    FIntVector ClickedCell = FIntVector::ZeroValue;
    EFoundationFace ClickedFace = EFoundationFace::Up;
    // The ray's hit point, world cm.
    FVector ClickPointCm = FVector::ZeroVector;
    // The player's view in Minecraft degrees: yaw 0 looks south, 90 west; positive pitch looks down.
    double Yaw = 0, Pitch = 0;
    // Set by ResolveDestination: whether the clicked block itself is replaced, and the placement cell.
    bool bReplaceClicked = false;
    FIntVector Destination = FIntVector::ZeroValue;
};

// Vanilla placement rules, as pure functions of the world and the context (26.3 bytecode of each block
// class's getStateForPlacement and canBeReplaced). The authority calls them inside its validated commands.
namespace FoundationBlockPlacement
{
    UNNAMED_API EFoundationFace GetOpposite(EFoundationFace Face);
    // Unreal aim vector -> Minecraft yaw and pitch (FoundationCoordinates conventions).
    UNNAMED_API void GetYawPitch(const FVector& Aim, double& OutYaw, double& OutPitch);
    // Direction.fromYRot: floor(yaw / 90 + 0.5) & 3 -> south, west, north, east.
    UNNAMED_API EFoundationFace GetHorizontalLook(double Yaw);
    // Direction.orderedByNearest(entity), with Mth.sin/cos's 65536-step table: nearest to the look first.
    UNNAMED_API TStaticArray<EFoundationFace, 6> GetNearestLooks(double Yaw, double Pitch);

    // BlockState.canBeReplaced(context): replaceable blocks (air, water) always; a single slab only by its own
    // slab item, and when it is the clicked block only from the side that would make it double.
    UNNAMED_API bool CanBeReplaced(const FFoundationBlockWorld& World, FFoundationBlockState Existing,
        const FFoundationBlockType& Placing, const FFoundationPlacementContext& Context, bool bReplacingClicked);
    // BlockPlaceContext: the clicked cell when it accepts the block, otherwise the cell beyond the clicked face.
    // Static anchors are never replaced. False when the chosen cell does not accept the block.
    UNNAMED_API bool ResolveDestination(const FFoundationBlockWorld& World, const FFoundationBlockType& Placing,
        bool bClickedIsBlock, FFoundationPlacementContext& Context);
    // getStateForPlacement for the block's placement family. waterlogged always stays false here: water
    // behaviour (and waterlogging) arrives with the chunk loader.
    UNNAMED_API FFoundationBlockState GetStateForPlacement(const FFoundationBlockWorld& World,
        const FFoundationBlockType& Block, const FFoundationPlacementContext& Context);
    // StandingAndWallBlockItem (the torch): its wall block on a side face, its own block on the top face and the
    // underside. Items without a wall block always place their block.
    UNNAMED_API FName GetItemBlock(FName Block, FName WallBlock, EFoundationFace ClickedFace);
    // A placed torch survives (the reference's support table): a floor torch on a top face that holds one, a wall
    // torch against a side that holds one behind it. An underside click places nothing until torch session T3 brings
    // vanilla's choice among the other faces.
    UNNAMED_API bool HasTorchSupport(const FFoundationBlockWorld& World, FFoundationBlockState Placed,
        const FIntVector& Destination, EFoundationFace ClickedFace);
}
