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
    // The player's secondary use (sneaking): a chest placed so stays single or joins the clicked chest (T1 18-chest).
    bool bSecondaryUse = false;
    // Set by ResolveDestination: whether the clicked block itself is replaced, and the placement cell.
    bool bReplaceClicked = false;
    FIntVector Destination = FIntVector::ZeroValue;
};

// Vanilla placement rules, as pure functions of the world and the context (26.3 bytecode of each block
// class's getStateForPlacement and canBeReplaced). The authority calls them inside its validated commands.
namespace FoundationBlockPlacement
{
    UNNAMED_API EFoundationFace GetOpposite(EFoundationFace Face);
    // Unreal aim vector -> Minecraft yaw and pitch (FoundationCoordinates conventions; Unreal X east, Y south, Z up).
    // Yaw 0 looks south (+Y), 90 west (-X), 180 north (-Y), -90 east (+X); pitch -90 looks up, 90 down. The aim is
    // (-sin yaw cos pitch, cos yaw cos pitch, -sin pitch). The yaw comes back in (-180, 180].
    UNNAMED_API void GetYawPitch(const FVector& Aim, double& OutYaw, double& OutPitch);
    // Direction.fromYRot: floor(yaw / 90 + 0.5) & 3 -> south, west, north, east.
    UNNAMED_API EFoundationFace GetHorizontalLook(double Yaw);
    // Direction.orderedByNearest(entity), with Mth.sin/cos's 65536-step table: nearest to the look first, then the
    // opposites in reverse (a b c -c -b -a). Exact ties follow the table (yaw 0's zero east-west part reads west, 90's
    // north-south part north, 180's east, 270's south), and at pitch +-90 the sides still follow the yaw. The yaw is
    // taken in [0, 360), the probe's range: the table splits yaw 315 and -45 at their ties. Probe P3
    // (Tests/Probes/torch-placement-26.3.json) measured all 312 looks of its 15 degree grid; this reproduces them.
    UNNAMED_API TStaticArray<EFoundationFace, 6> GetNearestLooks(double Yaw, double Pitch);
    // BlockPlaceContext.getNearestLookingDirections, as measured by probe P3: GetNearestLooks, with the direction into
    // the clicked block moved to the front (the rest keep their order) unless the click replaces that block.
    UNNAMED_API TStaticArray<EFoundationFace, 6> GetClickLooks(const FFoundationPlacementContext& Context);

    // Whether a placed block takes this state's cell: the block's replaceable flag (the facts'), or a single snow
    // layer. More layers are not replaced (minecraft.wiki, Snow; Jesus's answer to scout H's Q5), so snow keeps its
    // own flag false although the facts call every layer replaceable.
    UNNAMED_API bool IsReplaceable(const FFoundationBlockWorld& World, FFoundationBlockState State);
    // BlockState.canBeReplaced(context): replaceable states (IsReplaceable: air, water, one snow layer) always, except
    // a snow layer by snow (stacking layers is not built); a single slab only by its own slab item, and when it is the
    // clicked block only from the side that would make it double.
    UNNAMED_API bool CanBeReplaced(const FFoundationBlockWorld& World, FFoundationBlockState Existing,
        const FFoundationBlockType& Placing, const FFoundationPlacementContext& Context, bool bReplacingClicked);
    // BlockPlaceContext: the clicked cell when it accepts the block, otherwise the cell beyond the clicked face.
    // Static anchors are never replaced. False when the chosen cell does not accept the block.
    UNNAMED_API bool ResolveDestination(const FFoundationBlockWorld& World, const FFoundationBlockType& Placing,
        bool bClickedIsBlock, FFoundationPlacementContext& Context);
    // getStateForPlacement for the block's placement family. waterlogged always stays false here: water
    // behaviour (and waterlogging) arrives with the chunk loader. Model blocks (plants) place by their properties
    // instead (FoundationPlantRules), and take waterlogged from the destination.
    UNNAMED_API FFoundationBlockState GetStateForPlacement(const FFoundationBlockWorld& World,
        const FFoundationBlockType& Block, const FFoundationPlacementContext& Context);
    // A torch state at Cell is held (the reference's support table): a floor torch by the top face of the cell below,
    // a wall torch by the side of the cell behind it. OutSupport gets that cell; an unknown one reads as air.
    UNNAMED_API bool IsTorchSupported(const FFoundationBlockWorld& World, FFoundationBlockState State,
        const FIntVector& Cell, FIntVector* OutSupport = nullptr);
    // StandingAndWallBlockItem (the torch), as measured by probe P3 (Tests/Probes/torch-placement-26.3.json, 64,896
    // clicks) at the resolved destination. The wall is the first side in the click's looks (GetClickLooks) where
    // Holds accepts WallBlock facing away from that side. Walking the looks: up is skipped, down gives Block if Holds
    // accepts it, any side gives the wall if there is one; the first hit wins. False when nothing holds (nothing is
    // placed). A plain click on a side or top face that holds the block therefore always places it on the clicked
    // block. Both states start from their own placement (GetStateForPlacement: a coral fan's waterlogged).
    UNNAMED_API bool GetStandingOrWallState(const FFoundationBlockWorld& World, const FFoundationBlockType& Block,
        const FFoundationBlockType& WallBlock, const FFoundationPlacementContext& Context,
        TFunctionRef<bool(FFoundationBlockState)> Holds, FFoundationBlockState& OutState);
    // The torch's: GetStandingOrWallState held by the support table (IsTorchSupported).
    UNNAMED_API bool GetTorchState(const FFoundationBlockWorld& World, const FFoundationBlockType& Block,
        const FFoundationBlockType& WallBlock, const FFoundationPlacementContext& Context,
        FFoundationBlockState& OutState);
}
