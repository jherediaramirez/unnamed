#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlocks.h"

// Immutable lab geometry. Every static occludes; only designated, grid-aligned anchors seed placement.
struct FFoundationStatic
{
    FBox Box = FBox(ForceInit);
    FString Label;
    bool bAnchor = false;
    // A block-like use interaction that consumes the use action (e.g. "lab.reset"); None = none.
    FName Interaction;
};

// A targetable entity box (the training targets). Entities never come from the block grid.
struct FFoundationEntityShape
{
    int32 Id = 0;
    FBox Box = FBox(ForceInit);
    FString Label;
    bool bLiving = true;
    bool bAlive = true;
};

enum class EFoundationHitKind : uint8
{
    None,
    Block,
    Static,
    Entity
};

// What the view ray selects: kind, stable identity (cell, static index or entity ID), discrete face,
// intersection point and distance, the cell revision it observed and whether it lies inside its reach.
struct FFoundationHit
{
    EFoundationHitKind Kind = EFoundationHitKind::None;
    // Block: its cell. Static: the cell just behind the hit face (an anchor's clicked cell).
    FIntVector Cell = FIntVector::ZeroValue;
    EFoundationFace Face = EFoundationFace::None;
    FVector Point = FVector::ZeroVector;
    double DistanceCm = 0, ReachCm = 0;
    int64 Revision = 0;
    int32 StaticIndex = INDEX_NONE;
    int32 EntityId = 0;
    bool bInRange = false;

    bool IsBlockLike() const { return Kind == EFoundationHitKind::Block || Kind == EFoundationHitKind::Static; }
    // The same selected thing and face; revisions are compared separately.
    bool SameTarget(const FFoundationHit& Other) const;
};

struct FFoundationRay
{
    FVector Eye = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    double BlockReachCm = 450, EntityReachCm = 300;
    // An empty bucket's ray (step 17): fluid sources stop it too (water, lava and waterlogged cells; their fluid box,
    // the cell to the surface), entered from outside or holding the eye.
    bool bFluidSources = false;
};

// One shared query for targeting, previews and authority validation.
// - Blocks: an exact cell traversal from the eye. At an exact edge or corner crossing it steps X before Y
//   before Z, and a box entered exactly through an edge or corner reports the X face, then Y, then Z.
// - The nearest of grid block and static wins; a grid block wins an exact tie with a static.
// - An entity wins only when strictly nearer (as the reference does), then each kind's reach applies
//   with a strict boundary: distance < reach. Out-of-reach hits keep their kind with bInRange false.
// - A ray starting inside a box does not hit that box. Nothing extends reach: callers pass the gameplay eye.
namespace FoundationTargeting
{
    // Slab test from outside the box. Ties in entry distance keep the lower axis (X, Y, Z).
    UNNAMED_API bool IntersectBox(const FBox& Box, const FVector& Origin, const FVector& Direction, double MaxDistance,
        double& OutDistance, EFoundationFace& OutFace);
    UNNAMED_API FFoundationHit Resolve(const FFoundationRay& Ray, const FFoundationBlockWorld& World,
        TConstArrayView<FFoundationStatic> Statics, TConstArrayView<FFoundationEntityShape> Entities);
    UNNAMED_API FString Describe(const FFoundationHit& Hit);
}
