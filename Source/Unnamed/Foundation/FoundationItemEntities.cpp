#include "Foundation/FoundationItemEntities.h"
#include "Foundation/FoundationSimulation.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarItemEntities(TEXT("foundation.Items.Entities"), 1,
        TEXT("1: a survival break drops its loot into the world as item entities, picked up by walking over them; a "
             "full "
             "inventory leaves them on the ground (items S8a); Q throws one item and Ctrl+Q the stack, nearby stacks "
             "merge "
             "and items despawn after 6,000 ticks (items S8b). 0: the loot goes straight to the inventory and a full "
             "inventory refuses the break, Q is refused, as before. Drops already in the world stay until picked up."));
}

bool FoundationItemEntities::IsOn()
{
    return CVarItemEntities.GetValueOnAnyThread() != 0;
}

FBox FoundationItemEntities::GetBox(const FVector& WorldCm)
{
    return FBox(WorldCm - FVector(BoxCm * .5, BoxCm * .5, 0), WorldCm + FVector(BoxCm * .5, BoxCm * .5, BoxCm));
}

FBox FoundationItemEntities::GetMergeBox(const FVector& WorldCm)
{
    return GetBox(WorldCm).ExpandBy(FVector(MergeGrowCm, MergeGrowCm, MergeGrowUpCm));
}

int32 FoundationItemEntities::GetCopies(int32 Count)
{
    int32 Copies = 1;
    for (const int32 From : CopyCounts)
        Copies += Count >= From ? 1 : 0;
    return Copies;
}

FBox FoundationItemEntities::GetPickupBox(const FBox& Body)
{
    return Body.ExpandBy(FVector(PickupGrowCm, PickupGrowCm, PickupGrowUpCm));
}

FBox FoundationItemEntities::GetStepRegion(const FFoundationItemEntity& Entity, const FVector& OriginCm)
{
    // This step's velocity after gravity, plus a centimetre for contact.
    const FVector Reach = (Entity.VelocityCm - FVector(0, 0, GravityCm)).GetAbs() + FVector(1);
    return GetBox(OriginCm + Entity.LocalCm).ExpandBy(Reach);
}

void FoundationItemEntities::Move(FFoundationItemEntity& Entity, const FVector& OriginCm, const TArray<FBox>& Boxes)
{
    MoveBox(Entity.LocalCm, Entity.PreviousLocalCm, Entity.VelocityCm, Entity.bOnGround,
        GetBox(OriginCm + Entity.LocalCm), Boxes, GravityCm, FVector::ZeroVector);
}

void FoundationItemEntities::MoveBox(FVector& LocalCm, FVector& PreviousLocalCm, FVector& Velocity, bool& bOnGround,
    const FBox& Box, const TArray<FBox>& Boxes, double Gravity, const FVector& PushCm)
{
    PreviousLocalCm = LocalCm;
    Velocity.Z -= Gravity;
    Velocity += PushCm;
    const FVector Moved = FFoundationSimulation::Clip(Box, Velocity, Boxes);
    LocalCm += Moved;
    bOnGround = Velocity.Z < 0 && Moved.Z != Velocity.Z;
    for (int32 Axis = 0; Axis < 3; ++Axis)
        if (Moved[Axis] != Velocity[Axis]) Velocity[Axis] = 0;
    const double Sideways = bOnGround ? GroundFriction * Drag : Drag;
    Velocity.X *= Sideways;
    Velocity.Y *= Sideways;
    Velocity.Z *= Drag;
}
