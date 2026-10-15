#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlockFamilies.h"
#include "Foundation/FoundationCoordinates.h"
#include "Foundation/FoundationPlantRules.h"
#include "Foundation/FoundationTags.h"

namespace
{
    // Mth.sin and Mth.cos: a 65536-entry table indexed by (long)(radians * 65536 / 2pi) & 65535.
    float TableSin(double Radians, double Offset = 0)
    {
        const int64 Index = int64(Radians * 10430.378350470453 + Offset) & 65535;
        return float(FMath::Sin(double(Index) * UE_DOUBLE_PI * 2.0 / 65536.0));
    }

    float TableCos(double Radians)
    {
        return TableSin(Radians, 16384.0);
    }

    TStaticArray<EFoundationFace, 6> MakeDirectionArray(EFoundationFace A, EFoundationFace B, EFoundationFace C)
    {
        TStaticArray<EFoundationFace, 6> Result;
        Result[0] = A;
        Result[1] = B;
        Result[2] = C;
        Result[3] = FoundationBlockPlacement::GetOpposite(C);
        Result[4] = FoundationBlockPlacement::GetOpposite(B);
        Result[5] = FoundationBlockPlacement::GetOpposite(A);
        return Result;
    }

    FName FaceValue(EFoundationFace Face)
    {
        return FName(FoundationGrid::FaceName(Face));
    }

    bool IsHorizontal(EFoundationFace Face)
    {
        return Face != EFoundationFace::Up && Face != EFoundationFace::Down;
    }

    // BlockPlaceContext.getClickLocation().y - getClickedPos().getY(): height of the hit in the destination cell.
    double GetClickHeight(
        const FFoundationBlockWorld& World, const FFoundationPlacementContext& Context, const FIntVector& Cell)
    {
        return (Context.ClickPointCm.Z - FoundationGrid::CellMin(Cell, World.OriginCm).Z) / UFoundationProfile::BlockCm;
    }

    // LeavesBlock.getDistanceAt: logs (and other #prevents_nearby_leaf_decay) 0, leaves their distance, else 7.
    int32 GetLeafDistance(const FFoundationBlockWorld& World, FFoundationBlockState State)
    {
        const FFoundationBlockType& Block = World.GetBlock(State);
        if (Block.Tags.HasTag(FoundationTags::BlockPreventsNearbyLeafDecay)) return 0;
        const FName Distance = World.GetRegistry().GetValue(State, TEXT("distance"));
        return Distance.IsNone() ? 7 : FCString::Atoi(*Distance.ToString());
    }
}

EFoundationFace FoundationBlockPlacement::GetOpposite(EFoundationFace Face)
{
    // West/East, North/South and Down/Up are adjacent pairs in EFoundationFace.
    return Face == EFoundationFace::None ? Face : EFoundationFace(int32(Face) ^ 1);
}

void FoundationBlockPlacement::GetYawPitch(const FVector& Aim, double& OutYaw, double& OutPitch)
{
    const FVector Direction = Aim.GetSafeNormal();
    OutYaw = FoundationCoordinates::Yaw(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)));
    OutPitch = FoundationCoordinates::Pitch(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Direction.Z, -1., 1.))));
}

EFoundationFace FoundationBlockPlacement::GetHorizontalLook(double Yaw)
{
    static const EFoundationFace By2DData[4] = {
        EFoundationFace::South, EFoundationFace::West, EFoundationFace::North, EFoundationFace::East};
    return By2DData[FMath::FloorToInt64(Yaw / 90.0 + 0.5) & 3];
}

TStaticArray<EFoundationFace, 6> FoundationBlockPlacement::GetNearestLooks(double Yaw, double Pitch)
{
    const float PitchRadians = float(Pitch) * 0.017453292f;
    const float YawRadians = -float(Yaw - 360. * FMath::Floor(Yaw / 360.)) * 0.017453292f;
    const float H = TableSin(PitchRadians), I = TableCos(PitchRadians);
    const float J = TableSin(YawRadians), K = TableCos(YawRadians);
    const bool bEast = J > 0, bUp = H < 0, bSouth = K > 0;
    const float L = bEast ? J : -J, M = bUp ? -H : H, N = bSouth ? K : -K;
    const float O = L * I, P = N * I;
    const EFoundationFace X = bEast ? EFoundationFace::East : EFoundationFace::West;
    const EFoundationFace Y = bUp ? EFoundationFace::Up : EFoundationFace::Down;
    const EFoundationFace Z = bSouth ? EFoundationFace::South : EFoundationFace::North;
    if (L > N)
    {
        if (M > O) return MakeDirectionArray(Y, X, Z);
        return P > M ? MakeDirectionArray(X, Z, Y) : MakeDirectionArray(X, Y, Z);
    }
    if (M > P) return MakeDirectionArray(Y, Z, X);
    return O > M ? MakeDirectionArray(Z, X, Y) : MakeDirectionArray(Z, Y, X);
}

TStaticArray<EFoundationFace, 6> FoundationBlockPlacement::GetClickLooks(const FFoundationPlacementContext& Context)
{
    TStaticArray<EFoundationFace, 6> Looks = GetNearestLooks(Context.Yaw, Context.Pitch);
    if (Context.bReplaceClicked || Context.ClickedFace == EFoundationFace::None) return Looks;
    const EFoundationFace Into = GetOpposite(Context.ClickedFace);
    for (int32 Index = 5; Index > 0; --Index)
        if (Looks[Index] == Into) Swap(Looks[Index], Looks[Index - 1]);
    return Looks;
}

bool FoundationBlockPlacement::IsReplaceable(const FFoundationBlockWorld& World, FFoundationBlockState State)
{
    return World.GetBlock(State).bReplaceable || World.GetRegistry().GetShape(State) == EFoundationBlockShape::Layers1;
}

bool FoundationBlockPlacement::CanBeReplaced(const FFoundationBlockWorld& World, FFoundationBlockState Existing,
    const FFoundationBlockType& Placing, const FFoundationPlacementContext& Context, bool bReplacingClicked)
{
    const FFoundationBlockType& Block = World.GetBlock(Existing);
    if (IsReplaceable(World, Existing) && (Block.bReplaceable || Block.Id != Placing.Id)) return true;
    if (Block.Placement != EFoundationPlacementRule::Slab) return false;
    // SlabBlock.canBeReplaced.
    const FName Type = World.GetRegistry().GetValue(Existing, TEXT("type"));
    if (Type == TEXT("double") || Block.Id != Placing.Id) return false;
    if (!bReplacingClicked) return true;
    const bool bUpper = GetClickHeight(World, Context, Context.ClickedCell) > 0.5;
    const EFoundationFace Face = Context.ClickedFace;
    if (Type == TEXT("bottom")) return Face == EFoundationFace::Up || (bUpper && IsHorizontal(Face));
    return Face == EFoundationFace::Down || (!bUpper && IsHorizontal(Face));
}

bool FoundationBlockPlacement::ResolveDestination(const FFoundationBlockWorld& World,
    const FFoundationBlockType& Placing, bool bClickedIsBlock, FFoundationPlacementContext& Context)
{
    if (bClickedIsBlock &&
        CanBeReplaced(World, World.Get(Context.ClickedCell).State, Placing, Context, /*bReplacingClicked*/ true))
    {
        Context.bReplaceClicked = true;
        Context.Destination = Context.ClickedCell;
        return true;
    }
    Context.bReplaceClicked = false;
    Context.Destination = Context.ClickedCell + FoundationGrid::FaceOffset(Context.ClickedFace);
    return CanBeReplaced(World, World.Get(Context.Destination).State, Placing, Context, false);
}

FFoundationBlockState FoundationBlockPlacement::GetStateForPlacement(
    const FFoundationBlockWorld& World, const FFoundationBlockType& Block, const FFoundationPlacementContext& Context)
{
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    // Stairs, fences, walls, panes, bars and gates (survival B2b), doors and trapdoors (B3a); their half is the slab's.
    if (FoundationBlockFamilies::AreShapesEnabled() && FoundationBlockFamilies::UsesFamilyPlacement(Block))
        return FoundationBlockFamilies::GetStateForPlacement(World, Block, Context,
            Context.ClickedFace == EFoundationFace::Down ||
                (Context.ClickedFace != EFoundationFace::Up &&
                    GetClickHeight(World, Context, Context.Destination) > 0.5));
    // Plants and the other model blocks without a rule of their own (the torches have one) place by their properties.
    if (Block.Placement == EFoundationPlacementRule::Default &&
        Registry.GetShape(Block.GetDefaultState()) == EFoundationBlockShape::Model)
        return FoundationPlantRules::GetStateForPlacement(World, Block, Context);
    FFoundationBlockState State = Block.GetDefaultState();
    auto Set = [&Registry, &State](const TCHAR* Property, FName Value)
    { State = Registry.WithValue(State, Property, Value); };
    switch (Block.Placement)
    {
    case EFoundationPlacementRule::Axis:
    {
        // Minecraft axes: x east-west, y up-down, z north-south.
        const EFoundationFace Face = Context.ClickedFace;
        Set(TEXT("axis"),
            Face == EFoundationFace::West || Face == EFoundationFace::East ? TEXT("x")
                : IsHorizontal(Face)                                       ? TEXT("z")
                                                                           : TEXT("y"));
        break;
    }
    case EFoundationPlacementRule::FacingPlayer:
        Set(TEXT("facing"), FaceValue(GetOpposite(GetHorizontalLook(Context.Yaw))));
        break;
    case EFoundationPlacementRule::FacingAwayNearest:
        Set(TEXT("facing"), FaceValue(GetOpposite(GetNearestLooks(Context.Yaw, Context.Pitch)[0])));
        break;
    case EFoundationPlacementRule::FacingNearest:
        Set(TEXT("facing"), FaceValue(GetNearestLooks(Context.Yaw, Context.Pitch)[0]));
        break;
    case EFoundationPlacementRule::CrafterOrientation:
    case EFoundationPlacementRule::JigsawOrientation:
    {
        const EFoundationFace Horizontal = GetHorizontalLook(Context.Yaw);
        EFoundationFace Front, Top;
        if (Block.Placement == EFoundationPlacementRule::CrafterOrientation)
        {
            Front = GetOpposite(GetNearestLooks(Context.Yaw, Context.Pitch)[0]);
            Top = Front == EFoundationFace::Down ? GetOpposite(Horizontal)
                : Front == EFoundationFace::Up   ? Horizontal
                                                 : EFoundationFace::Up;
        }
        else
        {
            Front = Context.ClickedFace;
            Top = IsHorizontal(Front) ? EFoundationFace::Up : GetOpposite(Horizontal);
        }
        Set(TEXT("orientation"),
            FName(FString::Printf(TEXT("%s_%s"), FoundationGrid::FaceName(Front), FoundationGrid::FaceName(Top))));
        break;
    }
    case EFoundationPlacementRule::Slab:
    {
        // A matching single slab in the destination becomes double; otherwise the hit picks the half.
        const FFoundationBlockState Existing = World.Get(Context.Destination).State;
        if (Block.Contains(Existing))
        {
            State = Registry.WithValue(Existing, TEXT("type"), TEXT("double"));
            Set(TEXT("waterlogged"), TEXT("false"));
            break;
        }
        const EFoundationFace Face = Context.ClickedFace;
        const bool bBottom = Face != EFoundationFace::Down &&
            (Face == EFoundationFace::Up || !(GetClickHeight(World, Context, Context.Destination) > 0.5));
        Set(TEXT("type"), bBottom ? TEXT("bottom") : TEXT("top"));
        Set(TEXT("waterlogged"), TEXT("false"));
        break;
    }
    case EFoundationPlacementRule::Leaves:
    {
        // Player-placed leaves never decay; distance is computed once (neighbour updates come later).
        Set(TEXT("persistent"), TEXT("true"));
        Set(TEXT("waterlogged"), TEXT("false"));
        int32 Distance = 7;
        for (int32 Face = 0; Face < 6 && Distance > 1; ++Face)
            Distance = FMath::Min(Distance,
                GetLeafDistance(
                    World, World.Get(Context.Destination + FoundationGrid::FaceOffset(EFoundationFace(Face))).State) +
                    1);
        Set(TEXT("distance"), FName(*FString::FromInt(Distance)));
        break;
    }
    case EFoundationPlacementRule::Snowy:
    {
        const FFoundationBlockState Above = World.Get(Context.Destination + FIntVector(0, 0, 1)).State;
        Set(TEXT("snowy"), World.GetBlock(Above).Tags.HasTag(FoundationTags::BlockSnow) ? TEXT("true") : TEXT("false"));
        break;
    }
    default:
        break;
    }
    return State;
}

bool FoundationBlockPlacement::IsTorchSupported(
    const FFoundationBlockWorld& World, FFoundationBlockState State, const FIntVector& Cell, FIntVector* OutSupport)
{
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    // A wall torch hangs on the face its facing points away from; a floor torch stands on the top face below it.
    const FName Facing = Registry.GetValue(State, TEXT("facing"));
    EFoundationFace Face = EFoundationFace::Up;
    for (int32 Side = 0; Side < 4; ++Side)
        if (FaceValue(EFoundationFace(Side)) == Facing) Face = EFoundationFace(Side);
    const FIntVector Support = Cell - FoundationGrid::FaceOffset(Face);
    if (OutSupport) *OutSupport = Support;
    return (Registry.GetTorchSupport(World.Get(Support).State) >> int32(Face) & 1) != 0;
}

bool FoundationBlockPlacement::GetStandingOrWallState(const FFoundationBlockWorld& World,
    const FFoundationBlockType& Block, const FFoundationBlockType& WallBlock,
    const FFoundationPlacementContext& Context, TFunctionRef<bool(FFoundationBlockState)> Holds,
    FFoundationBlockState& OutState)
{
    const TStaticArray<EFoundationFace, 6> Looks = GetClickLooks(Context);
    // The wall: the first side in the looks where the wall block facing away from it holds.
    const FFoundationBlockState WallPlaced = GetStateForPlacement(World, WallBlock, Context);
    FFoundationBlockState Wall;
    bool bWall = false;
    for (int32 Index = 0; Index < 6 && !bWall; ++Index)
        if (IsHorizontal(Looks[Index]))
        {
            Wall = World.GetRegistry().WithValue(WallPlaced, TEXT("facing"), FaceValue(GetOpposite(Looks[Index])));
            bWall = Holds(Wall);
        }
    // Walking the looks: up is skipped, down stands on the cell below if it holds, a side takes the wall.
    for (const EFoundationFace Look : Looks)
        if (Look == EFoundationFace::Down)
        {
            const FFoundationBlockState Floor = GetStateForPlacement(World, Block, Context);
            if (Holds(Floor))
            {
                OutState = Floor;
                return true;
            }
        }
        else if (Look != EFoundationFace::Up && bWall)
        {
            OutState = Wall;
            return true;
        }
    return false;
}

bool FoundationBlockPlacement::GetTorchState(const FFoundationBlockWorld& World, const FFoundationBlockType& Block,
    const FFoundationBlockType& WallBlock, const FFoundationPlacementContext& Context, FFoundationBlockState& OutState)
{
    return GetStandingOrWallState(
        World, Block, WallBlock, Context, [&World, &Context](FFoundationBlockState State)
        { return IsTorchSupported(World, State, Context.Destination); }, OutState);
}
