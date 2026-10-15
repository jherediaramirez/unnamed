#include "Foundation/FoundationBlockFamilies.h"

#include "Algo/Find.h"
#include "Foundation/FoundationBlocks.h"
#include "GameplayTagContainer.h"

namespace
{
    using namespace FoundationBlockFamilies;

    // The vanilla block tags the families read (Config/Tags/MinecraftBlockTags.ini).
    struct FFamilyTags
    {
        FGameplayTag Stairs = FGameplayTag::RequestGameplayTag(TEXT("Block.Stairs"), false);
        FGameplayTag Fences = FGameplayTag::RequestGameplayTag(TEXT("Block.Fences"), false);
        FGameplayTag WoodenFences = FGameplayTag::RequestGameplayTag(TEXT("Block.WoodenFences"), false);
        FGameplayTag Walls = FGameplayTag::RequestGameplayTag(TEXT("Block.Walls"), false);
        FGameplayTag FenceGates = FGameplayTag::RequestGameplayTag(TEXT("Block.FenceGates"), false);
        FGameplayTag WallPostOverride = FGameplayTag::RequestGameplayTag(TEXT("Block.WallPostOverride"), false);
        FGameplayTag Leaves = FGameplayTag::RequestGameplayTag(TEXT("Block.Leaves"), false);
    };

    const FFamilyTags& GetTags()
    {
        // Requested again while invalid, so a lookup before the tag table loads does not stick.
        static FFamilyTags Tags;
        if (!Tags.Stairs.IsValid()) Tags = FFamilyTags();
        return Tags;
    }

    bool HasTag(const FFoundationBlockType& Block, const FGameplayTag& Tag)
    {
        return Tag.IsValid() && Block.Tags.HasTag(Tag);
    }

    bool IsSide(EFoundationFace Face)
    {
        return Face <= EFoundationFace::South;
    }

    // Seen from above: north, east, south, west.
    EFoundationFace GetClockWise(EFoundationFace Face)
    {
        switch (Face)
        {
        case EFoundationFace::North:
            return EFoundationFace::East;
        case EFoundationFace::East:
            return EFoundationFace::South;
        case EFoundationFace::South:
            return EFoundationFace::West;
        default:
            return EFoundationFace::North;
        }
    }

    EFoundationFace GetCounterClockWise(EFoundationFace Face)
    {
        return FoundationBlockPlacement::GetOpposite(GetClockWise(Face));
    }

    bool IsSameAxis(EFoundationFace A, EFoundationFace B)
    {
        return int32(A) / 2 == int32(B) / 2;
    }

    FName SideName(EFoundationFace Face)
    {
        return FName(FoundationGrid::FaceName(Face));
    }

    EFoundationFace GetFacing(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        const FName Value = Registry.GetValue(State, TEXT("facing"));
        for (int32 Face = 0; Face < 4; ++Face)
            if (Value == FoundationGrid::FaceName(EFoundationFace(Face))) return EFoundationFace(Face);
        return EFoundationFace::None;
    }

    EFoundationBlockFamily GetStateFamily(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        return GetFamily(Registry.GetBlock(State));
    }

    // A face's covered area: rectangles in the face's two other axes, in Unreal axis order, block units.
    struct FRect
    {
        double U0, V0, U1, V1;
    };

    using FRects = TArray<FRect, TInlineAllocator<8>>;

    // A wall side's value for no connection. FName("none") is NAME_None (FName ignores case), which a missing property
    // also reads as; walls always have their four sides.
    const TCHAR* const NoSide = TEXT("none");

    // The parts of a state's collision boxes, clipped to its cell, that lie on one face of the cell.
    void GetFaceRects(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, EFoundationFace Face, FRects& Out)
    {
        TArray<FBox> Boxes;
        Registry.GetCollisionBoxes(State, Boxes);
        const int32 Axis = int32(Face) / 2;
        const bool bPositive = int32(Face) % 2 == 1;
        const int32 U = Axis == 0 ? 1 : 0, V = Axis == 2 ? 1 : 2;
        constexpr double Epsilon = 1e-6;
        for (const FBox& Box : Boxes)
        {
            const FVector Min = Box.Min.ComponentMax(FVector::ZeroVector);
            const FVector Max = Box.Max.ComponentMin(FVector::OneVector);
            if (Min[U] >= Max[U] || Min[V] >= Max[V] || Min[Axis] >= Max[Axis]) continue;
            if (bPositive ? Max[Axis] < 1 - Epsilon : Min[Axis] > Epsilon) continue;
            Out.Add({Min[U], Min[V], Max[U], Max[V]});
        }
    }

    // Whether the rectangles together cover Target (VoxelShape's isCovered / getFaceShape full test).
    bool Covers(TConstArrayView<FRect> Rects, const FRect& Target)
    {
        TArray<double, TInlineAllocator<16>> Us = {Target.U0, Target.U1}, Vs = {Target.V0, Target.V1};
        for (const FRect& Rect : Rects)
        {
            for (const double U : {Rect.U0, Rect.U1})
                if (U > Target.U0 && U < Target.U1) Us.AddUnique(U);
            for (const double V : {Rect.V0, Rect.V1})
                if (V > Target.V0 && V < Target.V1) Vs.AddUnique(V);
        }
        Us.Sort();
        Vs.Sort();
        for (int32 I = 0; I + 1 < Us.Num(); ++I)
            for (int32 J = 0; J + 1 < Vs.Num(); ++J)
            {
                const double U = (Us[I] + Us[I + 1]) / 2, V = (Vs[J] + Vs[J + 1]) / 2;
                if (!Rects.ContainsByPredicate(
                        [U, V](const FRect& R) { return U > R.U0 && U < R.U1 && V > R.V0 && V < R.V1; }))
                    return false;
            }
        return true;
    }

    // Blocks that never connect by their sturdy faces (measured: shape_updates.json, single_neighbour): the pumpkins
    // and the melon.
    bool IsConnectionException(const FFoundationBlockType& Block)
    {
        static const FName Ids[] = {TEXT("pumpkin"), TEXT("carved_pumpkin"), TEXT("jack_o_lantern"), TEXT("melon")};
        return Algo::Find(Ids, Block.Id) != nullptr;
    }

    // FenceGateBlock.connectsToDirection: a gate joins the side its closed bar points to.
    bool GateJoins(const FFoundationBlockRegistry& Registry, FFoundationBlockState Gate, EFoundationFace Toward)
    {
        return IsSameAxis(GetClockWise(GetFacing(Registry, Gate)), Toward);
    }

    bool IsSturdyToward(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState Neighbour, EFoundationFace Toward)
    {
        return !IsConnectionException(Registry.GetBlock(Neighbour)) &&
            IsFaceSturdy(Registry, Neighbour, FoundationBlockPlacement::GetOpposite(Toward));
    }

    // Whether Self (of Family) connects to Neighbour, the state toward Toward.
    bool Connects(const FFoundationBlockRegistry& Registry, const FFoundationBlockType& Self,
        EFoundationBlockFamily Family, FFoundationBlockState Neighbour, EFoundationFace Toward)
    {
        const FFoundationBlockType& Other = Registry.GetBlock(Neighbour);
        const EFoundationBlockFamily OtherFamily = GetFamily(Other);
        if (IsSturdyToward(Registry, Neighbour, Toward)) return true;
        switch (Family)
        {
        case EFoundationBlockFamily::Fence:
            // Wooden fences join wooden fences; the others only their own kind.
            if (OtherFamily == EFoundationBlockFamily::Fence)
                return HasTag(Self, GetTags().WoodenFences) == HasTag(Other, GetTags().WoodenFences) &&
                    (HasTag(Self, GetTags().WoodenFences) || Self.Id == Other.Id);
            return OtherFamily == EFoundationBlockFamily::FenceGate && GateJoins(Registry, Neighbour, Toward);
        case EFoundationBlockFamily::Wall:
            return OtherFamily == EFoundationBlockFamily::Wall || OtherFamily == EFoundationBlockFamily::Pane ||
                (OtherFamily == EFoundationBlockFamily::FenceGate && GateJoins(Registry, Neighbour, Toward));
        case EFoundationBlockFamily::Pane:
            return OtherFamily == EFoundationBlockFamily::Pane || OtherFamily == EFoundationBlockFamily::Wall;
        default:
            return false;
        }
    }

    // The wall's post and side heights from its connections and the block above: a side is tall when the above's
    // underside covers a 2-pixel strip from that edge past the centre, the post (up) as minecraft.wiki "Wall" gives it.
    FFoundationBlockState MakeWall(const FFoundationBlockRegistry& Registry, FFoundationBlockState State,
        const bool (&Connected)[4], FFoundationBlockState Above)
    {
        FRects Under;
        GetFaceRects(Registry, Above, EFoundationFace::Down, Under);
        constexpr double P7 = 7 / 16., P9 = 9 / 16.;
        // West, east, north, south; U is east (X), V south (Y).
        static const FRect SideTests[4] = {{0, P7, P9, P9}, {P7, P7, 1, P9}, {P7, 0, P9, P9}, {P7, P7, P9, 1}};
        bool bTall[4];
        for (int32 Side = 0; Side < 4; ++Side)
        {
            bTall[Side] = Connected[Side] && Covers(Under, SideTests[Side]);
            State = Registry.WithValue(State, SideName(EFoundationFace(Side)),
                !Connected[Side]  ? NoSide
                    : bTall[Side] ? TEXT("tall")
                                  : TEXT("low"));
        }
        const FFoundationBlockType& AboveBlock = Registry.GetBlock(Above);
        bool bUp;
        if ((GetFamily(AboveBlock) == EFoundationBlockFamily::Wall &&
                Registry.GetValue(Above, TEXT("up")) == TEXT("true")) ||
            HasTag(AboveBlock, GetTags().WallPostOverride))
            bUp = true;
        // An end, a corner or a lone post shows its post; a straight run or a cross does not...
        else if (Connected[0] != Connected[1] || Connected[2] != Connected[3] ||
            !(Connected[0] || Connected[1] || Connected[2] || Connected[3]))
            bUp = true;
        // ...unless a block above covers the centre, while no two opposite sides are tall.
        else if ((bTall[0] && bTall[1]) || (bTall[2] && bTall[3]))
            bUp = false;
        else
            bUp = Covers(Under, {P7, P7, P9, P9});
        return Registry.WithValue(State, TEXT("up"), bUp ? TEXT("true") : TEXT("false"));
    }

    // StairBlock's shape: a perpendicular same-half stair in front makes an outer corner, one behind an inner corner,
    // unless the side it turns toward holds a stair of the same facing and half (VALUES "Stair corners").
    FFoundationBlockState MakeStairShape(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, const FIntVector& Cell, FGetState Get)
    {
        const EFoundationFace Facing = GetFacing(Registry, State);
        const FName Half = Registry.GetValue(State, TEXT("half"));
        auto SameHalfStair = [&Registry, &Half](FFoundationBlockState Other)
        {
            return GetStateFamily(Registry, Other) == EFoundationBlockFamily::Stairs &&
                Registry.GetValue(Other, TEXT("half")) == Half;
        };
        auto CanTakeShape = [&](EFoundationFace Side)
        {
            const FFoundationBlockState Other = Get(Cell + FoundationGrid::FaceOffset(Side));
            return !SameHalfStair(Other) || GetFacing(Registry, Other) != Facing;
        };
        const TCHAR* Shape = TEXT("straight");
        const FFoundationBlockState Front = Get(Cell + FoundationGrid::FaceOffset(Facing));
        const FFoundationBlockState Back =
            Get(Cell + FoundationGrid::FaceOffset(FoundationBlockPlacement::GetOpposite(Facing)));
        if (const EFoundationFace FrontFacing = GetFacing(Registry, Front); SameHalfStair(Front) &&
            !IsSameAxis(FrontFacing, Facing) && CanTakeShape(FoundationBlockPlacement::GetOpposite(FrontFacing)))
            Shape = FrontFacing == GetCounterClockWise(Facing) ? TEXT("outer_left") : TEXT("outer_right");
        else if (const EFoundationFace BackFacing = GetFacing(Registry, Back);
            SameHalfStair(Back) && !IsSameAxis(BackFacing, Facing) && CanTakeShape(BackFacing))
            Shape = BackFacing == GetCounterClockWise(Facing) ? TEXT("inner_left") : TEXT("inner_right");
        return Registry.WithValue(State, TEXT("shape"), Shape);
    }

    bool IsWall(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        return HasTag(Registry.GetBlock(State), GetTags().Walls);
    }
}

EFoundationBlockFamily FoundationBlockFamilies::GetFamily(const FFoundationBlockType& Block)
{
    const FFamilyTags& Tags = GetTags();
    if (HasTag(Block, Tags.Stairs)) return EFoundationBlockFamily::Stairs;
    if (HasTag(Block, Tags.Fences)) return EFoundationBlockFamily::Fence;
    if (HasTag(Block, Tags.Walls)) return EFoundationBlockFamily::Wall;
    if (HasTag(Block, Tags.FenceGates)) return EFoundationBlockFamily::FenceGate;
    // Panes and bars: exactly north, east, south, west and waterlogged, as a fence without the tag.
    if (Block.Properties.Num() == 5)
    {
        for (const TCHAR* Name : {TEXT("north"), TEXT("east"), TEXT("south"), TEXT("west"), TEXT("waterlogged")})
            if (Block.FindProperty(Name) == INDEX_NONE) return EFoundationBlockFamily::None;
        return EFoundationBlockFamily::Pane;
    }
    return EFoundationBlockFamily::None;
}

uint8 FoundationBlockFamilies::GetSturdyFaces(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
{
    uint8 Faces = 0;
    for (int32 Face = 0; Face < 6; ++Face)
        if (IsFaceSturdy(Registry, State, EFoundationFace(Face))) Faces |= uint8(1 << Face);
    return Faces;
}

bool FoundationBlockFamilies::IsFaceSturdy(
    const FFoundationBlockRegistry& Registry, FFoundationBlockState State, EFoundationFace Face)
{
    // Full cubes that support nothing (measured: shape_updates.json, single_neighbour): leaves and powder snow.
    const FFoundationBlockType& Block = Registry.GetBlock(State);
    if (State.IsAir() || HasTag(Block, GetTags().Leaves) || Block.Id == TEXT("powder_snow")) return false;
    FRects Rects;
    GetFaceRects(Registry, State, Face, Rects);
    return !Rects.IsEmpty() && Covers(Rects, {0, 0, 1, 1});
}

FFoundationBlockState FoundationBlockFamilies::UpdateShape(const FFoundationBlockRegistry& Registry,
    FFoundationBlockState State, const FIntVector& Cell, EFoundationFace Toward, FFoundationBlockState Neighbour,
    FGetState Get)
{
    const FFoundationBlockType& Block = Registry.GetBlock(State);
    const EFoundationBlockFamily Family = GetFamily(Block);
    switch (Family)
    {
    case EFoundationBlockFamily::Stairs:
        return IsSide(Toward) ? MakeStairShape(Registry, State, Cell, Get) : State;
    case EFoundationBlockFamily::Fence:
    case EFoundationBlockFamily::Pane:
        if (!IsSide(Toward)) return State;
        return Registry.WithValue(State, SideName(Toward),
            Connects(Registry, Block, Family, Neighbour, Toward) ? TEXT("true") : TEXT("false"));
    case EFoundationBlockFamily::Wall:
    {
        if (Toward == EFoundationFace::Down) return State;
        bool Connected[4];
        for (int32 Side = 0; Side < 4; ++Side)
            Connected[Side] = Registry.GetValue(State, SideName(EFoundationFace(Side))) != NoSide;
        if (IsSide(Toward)) Connected[int32(Toward)] = Connects(Registry, Block, Family, Neighbour, Toward);
        const FFoundationBlockState Above =
            Toward == EFoundationFace::Up ? Neighbour : Get(Cell + FoundationGrid::FaceOffset(EFoundationFace::Up));
        return MakeWall(Registry, State, Connected, Above);
    }
    case EFoundationBlockFamily::FenceGate:
    {
        // Walls on either side across the gate's facing lower it into the wall (VALUES "Fence gates").
        if (!IsSide(Toward) || !IsSameAxis(GetClockWise(GetFacing(Registry, State)), Toward)) return State;
        const bool bInWall = IsWall(Registry, Neighbour) ||
            IsWall(Registry, Get(Cell + FoundationGrid::FaceOffset(FoundationBlockPlacement::GetOpposite(Toward))));
        return Registry.WithValue(State, TEXT("in_wall"), bInWall ? TEXT("true") : TEXT("false"));
    }
    default:
        return State;
    }
}

FFoundationBlockState FoundationBlockFamilies::SettleShape(
    const FFoundationBlockRegistry& Registry, FFoundationBlockState State, const FIntVector& Cell, FGetState Get)
{
    static constexpr EFoundationFace Order[] = {EFoundationFace::Down, EFoundationFace::Up, EFoundationFace::North,
        EFoundationFace::South, EFoundationFace::West, EFoundationFace::East};
    for (int32 Pass = 0; Pass < 3; ++Pass)
    {
        const FFoundationBlockState Before = State;
        for (const EFoundationFace Toward : Order)
            State = UpdateShape(Registry, State, Cell, Toward, Get(Cell + FoundationGrid::FaceOffset(Toward)), Get);
        if (State == Before) break;
    }
    return State;
}

FFoundationBlockState FoundationBlockFamilies::GetStateForPlacement(const FFoundationBlockWorld& World,
    const FFoundationBlockType& Block, const FFoundationPlacementContext& Context, bool bTopHalf)
{
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    FFoundationBlockState State = Block.GetDefaultState();
    const EFoundationBlockFamily Family = GetFamily(Block);
    if (Family == EFoundationBlockFamily::None) return State;
    const EFoundationFace Look = FoundationBlockPlacement::GetHorizontalLook(Context.Yaw);
    if (Family == EFoundationBlockFamily::Stairs || Family == EFoundationBlockFamily::FenceGate)
        State = Registry.WithValue(State, TEXT("facing"), SideName(Look));
    if (Family == EFoundationBlockFamily::Stairs)
        State = Registry.WithValue(State, TEXT("half"), bTopHalf ? TEXT("top") : TEXT("bottom"));
    if (Block.FindProperty(TEXT("waterlogged")) != INDEX_NONE)
    {
        const FFoundationFluid Fluid = Registry.GetFluid(World.Get(Context.Destination).State);
        State = Registry.WithValue(
            State, TEXT("waterlogged"), Fluid.bWater && Fluid.bSource ? TEXT("true") : TEXT("false"));
    }
    return SettleShape(Registry, State, Context.Destination, [&World](const FIntVector& Cell)
        { return World.IsKnown(Cell) ? World.Get(Cell).State : FFoundationBlockState(); });
}

void FoundationBlockFamilies::UpdateNeighbourShapes(FFoundationBlockWorld& World, const FIntVector& Cell)
{
    if (!AreShapesEnabled() || !World.IsKnown(Cell)) return;
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    const FFoundationBlockState Changed = World.Get(Cell).State;
    auto Get = [&World](const FIntVector& At)
    { return World.IsKnown(At) ? World.Get(At).State : FFoundationBlockState(); };
    auto Update = [&](EFoundationFace Side)
    {
        const FIntVector At = Cell + FoundationGrid::FaceOffset(Side);
        if (!World.IsKnown(At)) return;
        const FFoundationBlockState State = World.Get(At).State;
        const EFoundationBlockFamily Family = GetStateFamily(Registry, State);
        if (Family == EFoundationBlockFamily::None ||
            (Side == EFoundationFace::Down && Family != EFoundationBlockFamily::Wall))
            return;
        const FFoundationBlockState Updated =
            UpdateShape(Registry, State, At, FoundationBlockPlacement::GetOpposite(Side), Changed, Get);
        if (Updated != State) World.Set(At, Updated);
    };
    for (const EFoundationFace Side : {EFoundationFace::North, EFoundationFace::South, EFoundationFace::West,
             EFoundationFace::East, EFoundationFace::Down})
        Update(Side);
}
