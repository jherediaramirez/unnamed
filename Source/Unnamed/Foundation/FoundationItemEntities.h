#pragma once

#include "CoreMinimal.h"

// A dropped item stack in the world (items S8a; scout AC's plan step 3). The authority owns the list and steps it once
// per reference step; the view only draws it. Positions are cm from the block world's origin (FFoundationBlockWorld::
// OriginCm), so world rebasing never moves them.
struct FFoundationItemEntity
{
    int32 Id = 0;
    // An index into FFoundationRules::Items, and how many.
    int32 Item = INDEX_NONE;
    int32 Count = 0;
    // The stack's wear (a thrown tool keeps it; items S8b). Stacks merge only with equal damage.
    int32 Damage = 0;
    // The bottom centre of its box now and one step ago (the view draws between the two).
    FVector LocalCm = FVector::ZeroVector, PreviousLocalCm = FVector::ZeroVector;
    // Cm per reference step.
    FVector VelocityCm = FVector::ZeroVector;
    // Steps lived, and steps until it can be picked up; neither counts while frozen.
    int32 Age = 0, PickupDelay = 0;
    // The look's phase in radians (vanilla's per-entity bob offset), fixed at the spawn.
    float BobOffset = 0;
    bool bOnGround = false;
    // Its column is not loaded: it neither steps nor is picked up nor drawn until the column is back (vanilla's
    // entities do not tick in an unloaded chunk).
    bool bFrozen = false;
};

// The item entities' rules. Sourced: minecraft.wiki "Item (entity)" (the box, the block-drop pickup delay, the pickup
// box, vanishing below the world) and "Entity", Motion table (items: gravity 0.04 blocks per tick squared, drag 0.98
// on both axes, terminal velocity 1.96, which puts the acceleration before the drag). Provisional until probe P3
// measures a 26.3 client: the tick order beyond that, the ground friction, the spawn spread and speed, and the look.
namespace FoundationItemEntities
{
    // foundation.Items.Entities: 1 (the default) breaks drop into the world; 0 returns them straight to the inventory.
    UNNAMED_API bool IsOn();

    // Sourced.
    constexpr double BoxCm = 25;
    constexpr int32 BlockDropPickupDelay = 10;
    // The player's box grown by 1 block sideways and 0.5 up and down.
    constexpr double PickupGrowCm = 100, PickupGrowUpCm = 50;
    constexpr double GravityCm = 4;
    constexpr double Drag = .98;
    // Below the world's lowest cell minus this many, an item vanishes at once.
    constexpr int32 VoidDepthCells = 64;
    // Items S8b, sourced (minecraft.wiki "Item (entity)"): a thrown item's pickup delay, the age at which an item
    // despawns (5 minutes), the merge box (an item's box grown 0.5 blocks sideways and 0.25 up and down) and the
    // 40-tick merge period, and the stack sizes from which 2, 3, 4 and 5 copies are drawn. Provisional (P3): the period
    // counted on the item's own age for moving and resting items alike (vanilla checks moving items more often).
    constexpr int32 ThrowPickupDelay = 40;
    constexpr int32 DespawnAge = 6000;
    constexpr double MergeGrowCm = 50, MergeGrowUpCm = 25;
    constexpr int32 MergeTicks = 40;
    constexpr int32 CopyCounts[] = {2, 17, 33, 49};

    // Provisional (code-only values, not on the wiki; P3): a thrown item leaves 0.3 blocks below the eye at 0.3 blocks
    // per tick along the look, 0.1 up, plus up to 0.02 sideways in a random direction and up to 0.1 up or down.
    constexpr double ThrowBelowEyeCm = 30, ThrowSpeedCm = 30, ThrowUpCm = 10, ThrowSpreadCm = 2, ThrowSpreadUpCm = 10;

    // Provisional: on the ground the horizontal drag is also scaled by the block's friction, the wiki's default 0.6
    // ("Entity", the living-entity footnote; items are not stated, and ice is not modelled).
    constexpr double GroundFriction = .6;
    // Provisional (facts section 2, "commonly cited"): a block drop spawns at the cell's centre, lowered by half its
    // box, moved up to 0.25 blocks on each axis, at up to 0.1 blocks per tick sideways and 0.2 up.
    constexpr double SpawnSpreadCm = 25, SpawnSidewaysCm = 10, SpawnUpCm = 20;

    // The look, provisional (P3: spin period, bob height): the bob is BobCm x sin(age / BobTicks + phase) + BobCm, the
    // spin age / SpinTicks + phase radians; a whole stack picked up flies to the player's centre over FlyTicks.
    constexpr double BobCm = 10, BobTicks = 10, SpinTicks = 20, FlyTicks = 3;
    // Items S8b, provisional (P3): each copy after the first is moved up to this far on each axis (half as far for a
    // flat sprite).
    constexpr double CopySpreadCm = 15;

    // The box of an item standing at a world position (its bottom centre).
    UNNAMED_API FBox GetBox(const FVector& WorldCm);
    // What an item merges from: its box grown by the merge box.
    UNNAMED_API FBox GetMergeBox(const FVector& WorldCm);
    // Copies drawn for a stack of Count (1 to 5).
    UNNAMED_API int32 GetCopies(int32 Count);
    // What a player picks up from: the body grown by the pickup box.
    UNNAMED_API FBox GetPickupBox(const FBox& Body);
    // The world region one step's motion can touch (for the collision query).
    UNNAMED_API FBox GetStepRegion(const FFoundationItemEntity& Entity, const FVector& OriginCm);
    // One step of motion against world boxes (cm): gravity, the move clipped as the player's is (vertical first), a
    // blocked axis stops, then the drag (and the ground friction sideways on the ground).
    UNNAMED_API void Move(FFoundationItemEntity& Entity, const FVector& OriginCm, const TArray<FBox>& Boxes);
}
