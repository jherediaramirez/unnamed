#include "Foundation/FoundationTargeting.h"

bool FFoundationHit::SameTarget(const FFoundationHit& Other) const
{
    if (Kind != Other.Kind || Face != Other.Face) return false;
    switch (Kind)
    {
    case EFoundationHitKind::Block:
        return Cell == Other.Cell;
    case EFoundationHitKind::Static:
        return StaticIndex == Other.StaticIndex && Cell == Other.Cell;
    case EFoundationHitKind::Entity:
        return EntityId == Other.EntityId;
    default:
        return true;
    }
}

bool FoundationTargeting::IntersectBox(const FBox& Box, const FVector& Origin, const FVector& Direction,
    double MaxDistance, double& OutDistance, EFoundationFace& OutFace)
{
    double Enter = -TNumericLimits<double>::Max(), Exit = TNumericLimits<double>::Max();
    int32 EnterAxis = INDEX_NONE;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const double O = Origin[Axis], D = Direction[Axis];
        if (FMath::Abs(D) < 1e-12)
        {
            if (O < Box.Min[Axis] || O > Box.Max[Axis]) return false;
            continue;
        }
        double Near = (Box.Min[Axis] - O) / D, Far = (Box.Max[Axis] - O) / D;
        if (Near > Far) Swap(Near, Far);
        // Strictly greater: an exact tie keeps the earlier axis.
        if (Near > Enter)
        {
            Enter = Near;
            EnterAxis = Axis;
        }
        Exit = FMath::Min(Exit, Far);
        if (Enter > Exit) return false;
    }
    if (EnterAxis == INDEX_NONE || Enter < 0 || Enter > MaxDistance) return false;
    OutDistance = Enter;
    OutFace = FoundationGrid::FaceFromAxis(EnterAxis, Direction[EnterAxis] < 0);
    return true;
}

FFoundationHit FoundationTargeting::Resolve(const FFoundationRay& Ray, const FFoundationBlockWorld& World,
    TConstArrayView<FFoundationStatic> Statics, TConstArrayView<FFoundationEntityShape> Entities)
{
    const FVector Direction = Ray.Direction.GetSafeNormal();
    if (Direction.IsNearlyZero()) return {};
    const double Limit = FMath::Max(Ray.BlockReachCm, Ray.EntityReachCm);
    FFoundationHit Best;
    double BestDistance = TNumericLimits<double>::Max();
    double Distance = 0;
    EFoundationFace Face = EFoundationFace::None;

    // Grid traversal in block units from the grid origin. Direction is unit length, so the traversal
    // parameter times BlockCm is the distance in cm.
    const double Block = UFoundationProfile::BlockCm;
    const FVector Start = (Ray.Eye - World.OriginCm) / Block;
    FIntVector Cell(FMath::FloorToInt32(Start.X), FMath::FloorToInt32(Start.Y), FMath::FloorToInt32(Start.Z));
    int32 Step[3];
    double Next[3], Delta[3];
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const double D = Direction[Axis];
        Step[Axis] = D > 0 ? 1 : D < 0 ? -1 : 0;
        Delta[Axis] = Step[Axis] ? FMath::Abs(1. / D) : TNumericLimits<double>::Max();
        Next[Axis] = D > 0 ? (Cell[Axis] + 1 - Start[Axis]) / D
            : D < 0        ? (Cell[Axis] - Start[Axis]) / D
                           : TNumericLimits<double>::Max();
    }
    for (double Travelled = 0; Travelled * Block <= Limit;)
    {
        if (World.IsKnown(Cell))
        {
            const FFoundationCell Value = World.Get(Cell);
            FBox Selection;
            // Water has no selection shape and a slab only its half: the ray continues through the rest.
            if (!Value.State.IsAir() && World.GetSelectionBox(Cell, Value.State, Selection))
            {
                if (IntersectBox(Selection, Ray.Eye, Direction, Limit, Distance, Face))
                {
                    // Shapes stay inside their cells, so the first hit in traversal order is the nearest.
                    Best.Kind = EFoundationHitKind::Block;
                    Best.Cell = Cell;
                    Best.Face = Face;
                    Best.DistanceCm = BestDistance = Distance;
                    Best.Revision = Value.Revision;
                    break;
                }
            }
        }
        const int32 Axis = Next[0] <= Next[1] ? (Next[0] <= Next[2] ? 0 : 2) : (Next[1] <= Next[2] ? 1 : 2);
        Travelled = Next[Axis];
        Cell[Axis] += Step[Axis];
        Next[Axis] += Delta[Axis];
    }

    // Statics: strictly nearer than a grid block wins.
    for (int32 Index = 0; Index < Statics.Num(); ++Index)
        if (IntersectBox(Statics[Index].Box, Ray.Eye, Direction, Limit, Distance, Face) && Distance < BestDistance)
        {
            Best = {};
            Best.Kind = EFoundationHitKind::Static;
            Best.StaticIndex = Index;
            Best.Face = Face;
            Best.DistanceCm = BestDistance = Distance;
        }

    // Entities: strictly nearer than any block-like hit.
    for (const FFoundationEntityShape& Entity : Entities)
        if (IntersectBox(Entity.Box, Ray.Eye, Direction, Limit, Distance, Face) && Distance < BestDistance)
        {
            Best = {};
            Best.Kind = EFoundationHitKind::Entity;
            Best.EntityId = Entity.Id;
            Best.Face = Face;
            Best.DistanceCm = BestDistance = Distance;
        }

    if (Best.Kind == EFoundationHitKind::None) return Best;
    Best.Point = Ray.Eye + Direction * Best.DistanceCm;
    if (Best.Kind == EFoundationHitKind::Static)
        Best.Cell = FoundationGrid::CellAt(Best.Point - FoundationGrid::FaceNormal(Best.Face) * .5, World.OriginCm);
    if (Best.Kind == EFoundationHitKind::Entity) Best.Cell = FoundationGrid::CellAt(Best.Point, World.OriginCm);
    Best.ReachCm = Best.Kind == EFoundationHitKind::Entity ? Ray.EntityReachCm : Ray.BlockReachCm;
    Best.bInRange = Best.DistanceCm < Best.ReachCm;
    return Best;
}

FString FoundationTargeting::Describe(const FFoundationHit& Hit)
{
    const TCHAR* Kinds[] = {TEXT("none"), TEXT("block"), TEXT("static"), TEXT("entity")};
    if (Hit.Kind == EFoundationHitKind::None) return TEXT("none");
    FString Text = FString::Printf(TEXT("%s "), Kinds[int32(Hit.Kind)]);
    if (Hit.Kind == EFoundationHitKind::Entity)
        Text += FString::Printf(TEXT("#%d"), Hit.EntityId);
    else
        Text += FoundationGrid::DescribeCell(Hit.Cell);
    return Text +
        FString::Printf(TEXT(" face %s %.3f/%.2f blocks rev %lld%s"), FoundationGrid::FaceName(Hit.Face),
            Hit.DistanceCm / 100., Hit.ReachCm / 100., Hit.Revision, Hit.bInRange ? TEXT("") : TEXT(" OUT"));
}
