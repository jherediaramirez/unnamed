#include "Foundation/FoundationItemEntities.h"
#include "Foundation/FoundationProfile.h"
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
             "inventory refuses the break, Q is refused, as before. Drops already in the world stay until picked up, "
             "moving as before (out of a block placed over them too)."));
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
    // The push-out's speed draw is a pure function of the item and its age, so a replay moves the same.
    MoveBox(Entity.LocalCm, Entity.PreviousLocalCm, Entity.VelocityCm, Entity.bOnGround,
        GetBox(OriginCm + Entity.LocalCm), Boxes, GravityCm, FVector::ZeroVector,
        FRandomStream(int32(HashCombine(GetTypeHash(Entity.Id), GetTypeHash(Entity.Age)))).GetFraction());
}

bool FoundationItemEntities::IsInside(const FBox& Box, const TArray<FBox>& Boxes)
{
    const FBox Inner = Box.ExpandBy(-.01);
    for (const FBox& Solid : Boxes)
        if (Solid.Intersect(Inner)) return true;
    return false;
}

namespace
{
    // Survival step 19a (SURVIVAL-RUN2-FINDINGS, R23, survival2.r23.item_rules, traced by survival2.r23.item_pushout):
    // a body overlapping a block heads out of the cell its centre is in, by the nearest of north (-Y), south (+Y),
    // west (-X), east (+X) and up, in that order and strictly nearer to win, never down. A side whose neighbouring cell
    // one box fills whole (a full block) is no way out, so a slab or fence beside it is one; up when none is. That
    // axis gets 0.1 to 0.3 blocks per tick (Random), the other two keep 0.75 of theirs. This replaces the items fixes'
    // provisional push-out, which went back and forth between two boxes (Codex review 11, finding 1; FX-a).
    void PushOut(FVector& Velocity, const FBox& Box, const FVector& LocalCm, const TArray<FBox>& Boxes, double Random)
    {
        using namespace FoundationItemEntities;
        const double Block = UFoundationProfile::BlockCm;
        const FVector Center = Box.GetCenter();
        // Cells are counted from the block world's origin, where the box stands on LocalCm.
        const FVector Origin = FVector(Center.X, Center.Y, Box.Min.Z) - LocalCm;
        const FVector Local = Center - Origin;
        const FVector CellMin(FMath::FloorToDouble(Local.X / Block) * Block,
            FMath::FloorToDouble(Local.Y / Block) * Block, FMath::FloorToDouble(Local.Z / Block) * Block);
        const auto IsFull = [&](const FIntVector& Side)
        {
            const FVector Min = Origin + CellMin + FVector(Side) * Block;
            const FBox Cell(Min + FVector(.01), Min + FVector(Block - .01));
            for (const FBox& Solid : Boxes)
                if (Solid.IsInsideOrOn(Cell.Min) && Solid.IsInsideOrOn(Cell.Max)) return true;
            return false;
        };
        const FIntVector Sides[] = {{0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 1}};
        FIntVector Best(0, 0, 1);
        double BestDistance = TNumericLimits<double>::Max();
        for (const FIntVector& Side : Sides)
        {
            const int32 Axis = Side.X != 0 ? 0 : Side.Y != 0 ? 1 : 2;
            const double Distance = Side[Axis] < 0 ? Local[Axis] - CellMin[Axis] : CellMin[Axis] + Block - Local[Axis];
            if (Distance < BestDistance && !IsFull(Side))
            {
                BestDistance = Distance;
                Best = Side;
            }
        }
        const int32 Axis = Best.X != 0 ? 0 : Best.Y != 0 ? 1 : 2;
        Velocity *= PushOutKeep;
        Velocity[Axis] = Best[Axis] * (PushOutMinCm + Random * PushOutRangeCm);
    }
}

void FoundationItemEntities::MoveBox(FVector& LocalCm, FVector& PreviousLocalCm, FVector& Velocity, bool& bOnGround,
    const FBox& Box, const TArray<FBox>& Boxes, double Gravity, const FVector& PushCm, double PushOutRandom,
    bool bGravityInside)
{
    PreviousLocalCm = LocalCm;
    if (IsInside(Box, Boxes))
    {
        // R23: the step still has gravity (an item's; an orb starting inside skips it) and drag around the push, and
        // moves through blocks.
        if (bGravityInside) Velocity.Z -= Gravity;
        Velocity += PushCm;
        PushOut(Velocity, Box, LocalCm, Boxes, PushOutRandom);
        LocalCm += Velocity;
        bOnGround = false;
        Velocity *= Drag;
        return;
    }
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
