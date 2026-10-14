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

bool FoundationItemEntities::IsInside(const FBox& Box, const TArray<FBox>& Boxes)
{
    const FBox Inner = Box.ExpandBy(-.01);
    for (const FBox& Solid : Boxes)
        if (Solid.Intersect(Inner)) return true;
    return false;
}

namespace
{
    // Items fixes (S8a-b): one step out of a block, moving through blocks. While the body's centre is in a box, it
    // heads for the nearest unobstructed side of the centre's cell (up when every side is blocked); once the centre is
    // clear it keeps that heading until the body is.
    void PushOut(FVector& LocalCm, FVector& Velocity, bool& bOnGround, const FBox& Box, const TArray<FBox>& Boxes)
    {
        using namespace FoundationItemEntities;
        const double Block = UFoundationProfile::BlockCm;
        const FVector Center = Box.GetCenter();
        const auto IsSolidAt = [&Boxes](const FVector& Point)
        {
            for (const FBox& Solid : Boxes)
                if (Solid.IsInside(Point)) return true;
            return false;
        };
        if (IsSolidAt(Center))
        {
            // Cells are counted from the block world's origin, where the box stands on LocalCm.
            const FVector Origin = FVector(Center.X, Center.Y, Box.Min.Z) - LocalCm;
            const FVector Local = Center - Origin;
            const FVector CellMin(FMath::FloorToDouble(Local.X / Block) * Block,
                FMath::FloorToDouble(Local.Y / Block) * Block, FMath::FloorToDouble(Local.Z / Block) * Block);
            const auto IsFree = [&](const FIntVector& Side)
            {
                const FVector Min = Origin + CellMin + FVector(Side) * Block;
                return !IsInside(FBox(Min, Min + FVector(Block)), Boxes);
            };
            // The sides in a fixed order; the nearest free one wins (the first on a tie).
            const FIntVector Sides[] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
            FIntVector Best(0, 0, 1);
            double BestDistance = TNumericLimits<double>::Max();
            for (const FIntVector& Side : Sides)
            {
                const int32 Axis = Side.X != 0 ? 0 : 1;
                const double Distance =
                    Side[Axis] < 0 ? Local[Axis] - CellMin[Axis] : CellMin[Axis] + Block - Local[Axis];
                if (Distance < BestDistance && IsFree(Side))
                {
                    BestDistance = Distance;
                    Best = Side;
                }
            }
            Velocity = FVector(Best) * PushOutCm;
        }
        else
        {
            // Clear at the centre but still overlapping (leaving after a push, or a block placed over its edge): out of
            // the first box it is in by the shortest way, sideways or up (never down, as above).
            for (const FBox& Solid : Boxes)
                if (Solid.Intersect(Box.ExpandBy(-.01)))
                {
                    const double Depths[] = {Box.Max.X - Solid.Min.X, Solid.Max.X - Box.Min.X, Box.Max.Y - Solid.Min.Y,
                        Solid.Max.Y - Box.Min.Y, Solid.Max.Z - Box.Min.Z};
                    const FVector Ways[] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 1}};
                    int32 Shortest = 0;
                    for (int32 Way = 1; Way < 5; ++Way)
                        Shortest = Depths[Way] < Depths[Shortest] ? Way : Shortest;
                    Velocity = Ways[Shortest] * PushOutCm;
                    break;
                }
        }
        LocalCm += Velocity;
        bOnGround = false;
    }
}

void FoundationItemEntities::MoveBox(FVector& LocalCm, FVector& PreviousLocalCm, FVector& Velocity, bool& bOnGround,
    const FBox& Box, const TArray<FBox>& Boxes, double Gravity, const FVector& PushCm)
{
    PreviousLocalCm = LocalCm;
    if (IsInside(Box, Boxes))
    {
        PushOut(LocalCm, Velocity, bOnGround, Box, Boxes);
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
