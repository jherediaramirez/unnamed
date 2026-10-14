#include "Foundation/FoundationItemEntities.h"
#include "Foundation/FoundationSimulation.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarItemEntities(TEXT("foundation.Items.Entities"), 1,
        TEXT("1: a survival break drops its loot into the world as item entities, picked up by walking over them; a "
             "full "
             "inventory leaves them on the ground (items S8a). 0: the loot goes straight to the inventory and a full "
             "inventory refuses the break, as before. Drops already in the world stay until picked up."));
}

bool FoundationItemEntities::IsOn()
{
    return CVarItemEntities.GetValueOnAnyThread() != 0;
}

FBox FoundationItemEntities::GetBox(const FVector& WorldCm)
{
    return FBox(WorldCm - FVector(BoxCm * .5, BoxCm * .5, 0), WorldCm + FVector(BoxCm * .5, BoxCm * .5, BoxCm));
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
    Entity.PreviousLocalCm = Entity.LocalCm;
    FVector& Velocity = Entity.VelocityCm;
    Velocity.Z -= GravityCm;
    const FVector Moved = FFoundationSimulation::Clip(GetBox(OriginCm + Entity.LocalCm), Velocity, Boxes);
    Entity.LocalCm += Moved;
    Entity.bOnGround = Velocity.Z < 0 && Moved.Z != Velocity.Z;
    for (int32 Axis = 0; Axis < 3; ++Axis)
        if (Moved[Axis] != Velocity[Axis]) Velocity[Axis] = 0;
    const double Sideways = Entity.bOnGround ? GroundFriction * Drag : Drag;
    Velocity.X *= Sideways;
    Velocity.Y *= Sideways;
    Velocity.Z *= Drag;
}
