#include "Foundation/FoundationBlockFamilies.h"

#include "Algo/Find.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationChests.h"
#include "Foundation/FoundationProfile.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"

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
        FGameplayTag Doors = FGameplayTag::RequestGameplayTag(TEXT("Block.Doors"), false);
        FGameplayTag Trapdoors = FGameplayTag::RequestGameplayTag(TEXT("Block.Trapdoors"), false);
        FGameplayTag Buttons = FGameplayTag::RequestGameplayTag(TEXT("Block.Buttons"), false);
        FGameplayTag WoodenButtons = FGameplayTag::RequestGameplayTag(TEXT("Block.WoodenButtons"), false);
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

    TAutoConsoleVariable<int32> CVarBlocksInteract(TEXT("foundation.Blocks.Interact"), 1,
        TEXT("1: doors, trapdoors and fence gates place by their own rules and open by hand (survival B3a); a door "
             "needs "
             "a sturdy block below. 0: no hand use, and doors and trapdoors place as before B3a."));

    const TCHAR* OpenValueName(bool bValue)
    {
        return bValue ? TEXT("true") : TEXT("false");
    }

    bool IsDoorHalf(const FFoundationBlockRegistry& Registry, FFoundationBlockState State, const TCHAR* Half)
    {
        return GetStateFamily(Registry, State) == EFoundationBlockFamily::Door &&
            Registry.GetValue(State, TEXT("half")) == Half;
    }

    // A full-block collision shape (isCollisionShapeFullBlock): what the door hinge counts beside a door. Measured by
    // Tests/Probes/door-hinge-26.3.json: stone, glass and leaves count; a bottom slab, a fence and air do not.
    bool HasFullBlockCollision(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        TArray<FBox> Boxes;
        Registry.GetCollisionBoxes(State, Boxes);
        constexpr double Epsilon = 1e-6;
        return Boxes.ContainsByPredicate(
            [](const FBox& Box) { return Box.Min.GetMax() < Epsilon && Box.Max.GetMin() > 1 - Epsilon; });
    }

    // The door hinge for a lower half at Cell facing Facing, as measured (Tests/Probes/door-hinge-26.3.json, 11,100
    // placements; VALUES "Door hinges"). Left and right are the player's as they look along Facing; the full-block
    // cells beside the door (its lower and its upper cell each count one) are counted per side. The hinge goes right
    // with a door's lower half on the left only, or with more full cells on the right; else left with a door's lower
    // half on the right only, or more full cells on the left; else on the half of the cell the click hit, the left on
    // the centre line (the probe's parts D and E: the rule is not symmetric).
    const TCHAR* GetDoorHingeSide(const FFoundationBlockWorld& World, const FIntVector& Cell, EFoundationFace Facing,
        const FFoundationPlacementContext& Context)
    {
        const FFoundationBlockRegistry& Registry = World.GetRegistry();
        const EFoundationFace Left = GetCounterClockWise(Facing), Right = GetClockWise(Facing);
        auto Read = [&World](const FIntVector& At)
        { return World.IsKnown(At) ? World.Get(At).State : FFoundationBlockState(); };
        auto CountFull = [&](EFoundationFace Side)
        {
            const FIntVector At = Cell + FoundationGrid::FaceOffset(Side);
            return int32(HasFullBlockCollision(Registry, Read(At))) +
                int32(HasFullBlockCollision(Registry, Read(At + FIntVector(0, 0, 1))));
        };
        const bool bDoorLeft = IsDoorHalf(Registry, Read(Cell + FoundationGrid::FaceOffset(Left)), TEXT("lower"));
        const bool bDoorRight = IsDoorHalf(Registry, Read(Cell + FoundationGrid::FaceOffset(Right)), TEXT("lower"));
        const int32 FullLeft = CountFull(Left), FullRight = CountFull(Right);
        if ((bDoorLeft && !bDoorRight) || FullRight > FullLeft) return TEXT("right");
        if ((bDoorRight && !bDoorLeft) || FullLeft > FullRight) return TEXT("left");
        const FVector Local =
            (Context.ClickPointCm - FoundationGrid::CellMin(Cell, World.OriginCm)) / UFoundationProfile::BlockCm -
            FVector(.5);
        return FVector::DotProduct(Local, FVector(FoundationGrid::FaceOffset(Left))) >= 0 ? TEXT("left")
                                                                                          : TEXT("right");
    }
}

EFoundationBlockFamily FoundationBlockFamilies::GetFamily(const FFoundationBlockType& Block)
{
    const FFamilyTags& Tags = GetTags();
    if (HasTag(Block, Tags.Stairs)) return EFoundationBlockFamily::Stairs;
    if (HasTag(Block, Tags.Fences)) return EFoundationBlockFamily::Fence;
    if (HasTag(Block, Tags.Walls)) return EFoundationBlockFamily::Wall;
    if (HasTag(Block, Tags.FenceGates)) return EFoundationBlockFamily::FenceGate;
    if (HasTag(Block, Tags.Doors)) return EFoundationBlockFamily::Door;
    if (HasTag(Block, Tags.Trapdoors)) return EFoundationBlockFamily::Trapdoor;
    if (HasTag(Block, Tags.Buttons)) return EFoundationBlockFamily::Button;
    if (Block.Id == TEXT("lever")) return EFoundationBlockFamily::Lever;
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
    // Every snow layer's underside holds a ceiling button or lever, one layer too, which has no collision
    // (family_survival.json's up probe, survival B3b).
    if (Block.Id == TEXT("snow") && Face == EFoundationFace::Down) return true;
    FRects Rects;
    GetFaceRects(Registry, State, Face, Rects);
    return !Rects.IsEmpty() && Covers(Rects, {0, 0, 1, 1});
}

FFoundationBlockState FoundationBlockFamilies::UpdateShape(const FFoundationBlockRegistry& Registry,
    FFoundationBlockState State, const FIntVector& Cell, EFoundationFace Toward, FFoundationBlockState Neighbour,
    FGetState Get)
{
    if (FoundationChests::IsChest(Registry.GetBlock(State).Id))
        return FoundationChests::UpdateShape(Registry, State, Toward, Neighbour);
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
    if (FoundationChests::IsChest(Block.Id)) return FoundationChests::GetStateForPlacement(World, Block, Context);
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    FFoundationBlockState State = Block.GetDefaultState();
    const EFoundationBlockFamily Family = GetFamily(Block);
    if (Family == EFoundationBlockFamily::None) return State;
    const EFoundationFace Look = FoundationBlockPlacement::GetHorizontalLook(Context.Yaw);
    if (Family == EFoundationBlockFamily::Stairs || Family == EFoundationBlockFamily::FenceGate)
        State = Registry.WithValue(State, TEXT("facing"), SideName(Look));
    if (Family == EFoundationBlockFamily::Stairs || Family == EFoundationBlockFamily::Trapdoor)
        State = Registry.WithValue(State, TEXT("half"), bTopHalf ? TEXT("top") : TEXT("bottom"));
    if (Family == EFoundationBlockFamily::Door)
    {
        State = Registry.WithValue(State, TEXT("facing"), SideName(Look));
        State = Registry.WithValue(State, TEXT("half"), TEXT("lower"));
        State = Registry.WithValue(State, TEXT("hinge"), GetDoorHingeSide(World, Context.Destination, Look, Context));
    }
    // A trapdoor faces the clicked side face with the half from the click height, or away from the player on a top or
    // bottom face (minecraft.wiki "Trapdoor"); a click that replaces the clicked block (short grass) counts as the
    // latter, the half top unless the click was on a top face.
    if (Family == EFoundationBlockFamily::Trapdoor)
    {
        const bool bOnSide = IsSide(Context.ClickedFace) && !Context.bReplaceClicked;
        State = Registry.WithValue(State, TEXT("facing"),
            SideName(bOnSide ? Context.ClickedFace : FoundationBlockPlacement::GetOpposite(Look)));
        if (Context.bReplaceClicked)
            State = Registry.WithValue(
                State, TEXT("half"), Context.ClickedFace == EFoundationFace::Up ? TEXT("bottom") : TEXT("top"));
    }
    // A button or lever on the first of the click's looks where it survives (the probe's parts A and B).
    if (Family == EFoundationBlockFamily::Button || Family == EFoundationBlockFamily::Lever)
    {
        const FFoundationBlockState Standing =
            Registry.WithValue(Registry.WithValue(State, TEXT("face"), TEXT("floor")), TEXT("facing"), SideName(Look));
        for (const EFoundationFace Toward : FoundationBlockPlacement::GetClickLooks(Context))
        {
            const FFoundationBlockState Attached = Toward == EFoundationFace::Down ? Standing
                : Toward == EFoundationFace::Up
                ? Registry.WithValue(Standing, TEXT("face"), TEXT("ceiling"))
                : Registry.WithValue(Registry.WithValue(State, TEXT("face"), TEXT("wall")), TEXT("facing"),
                      SideName(FoundationBlockPlacement::GetOpposite(Toward)));
            if (GetSurvival(World, Context.Destination, Attached) == EFoundationFamilySurvival::Survives)
                return Attached;
        }
        return Standing;
    }
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
        if ((Family == EFoundationBlockFamily::None && !FoundationChests::IsChest(Registry.GetBlock(State).Id)) ||
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

bool FoundationBlockFamilies::IsInteractOn()
{
    return CVarBlocksInteract.GetValueOnAnyThread() != 0;
}

bool FoundationBlockFamilies::UsesFamilyPlacement(const FFoundationBlockType& Block)
{
    if (FoundationChests::IsChest(Block.Id)) return FoundationChests::IsOn();
    const EFoundationBlockFamily Family = GetFamily(Block);
    if (Family == EFoundationBlockFamily::Door || Family == EFoundationBlockFamily::Trapdoor ||
        Family == EFoundationBlockFamily::Button || Family == EFoundationBlockFamily::Lever)
        return IsInteractOn();
    return Family != EFoundationBlockFamily::None;
}

bool FoundationBlockFamilies::IsUsable(const FFoundationBlockType& Block)
{
    switch (GetFamily(Block))
    {
    case EFoundationBlockFamily::Door:
    case EFoundationBlockFamily::Trapdoor:
        return Block.Id != TEXT("iron_door") && Block.Id != TEXT("iron_trapdoor");
    case EFoundationBlockFamily::FenceGate:
    case EFoundationBlockFamily::Button:
    case EFoundationBlockFamily::Lever:
        return true;
    default:
        return false;
    }
}

bool FoundationBlockFamilies::Use(
    FFoundationAuthority& Authority, FFoundationPlayer& Player, const FFoundationHit& Hit, FFoundationResult& Result)
{
    const FFoundationBlockWorld& World = Authority.World;
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    const FFoundationBlockState State = World.Get(Hit.Cell).State;
    const FFoundationBlockType& Block = Registry.GetBlock(State);
    if (!IsUsable(Block)) return false;
    const EFoundationBlockFamily Family = GetFamily(Block);
    if (Family == EFoundationBlockFamily::Button || Family == EFoundationBlockFamily::Lever)
    {
        const bool bPowered = Registry.GetValue(State, TEXT("powered")) != TEXT("true");
        // A pressed button takes the click and changes nothing; its release stays where it was (the probe's part C).
        if (Family == EFoundationBlockFamily::Button && !bPowered)
        {
            Result.Outcome = EFoundationOutcome::Committed;
            Result.bInteracted = true;
            Result.Cell = Hit.Cell;
            Result.State = State;
            return true;
        }
        const TPair<FIntVector, FFoundationBlockState> Write(
            Hit.Cell, Registry.WithValue(State, TEXT("powered"), OpenValueName(bPowered)));
        // The lever's play call is volume 0.3, pitch 0.6 on and 0.5 off; a button's is 1 and 1, its asset holding 0.3
        // and the pitch (F 3561-3567, 3639-3729).
        const bool bLever = Family == EFoundationBlockFamily::Lever;
        Authority.CommitBlockUse(Player, Hit.Cell, MakeArrayView(&Write, 1), GetUseSound(Block, bPowered), Result,
            bLever ? .3f : 1.f, bLever ? (bPowered ? .6f : .5f) : 1.f);
        if (!bLever && Result.IsCommitted())
            Authority.ScheduleBlockTick(
                Hit.Cell, Write.Value, GetButtonTicks(Block), EFoundationBlockTick::ButtonRelease);
        return true;
    }
    const bool bOpen = Registry.GetValue(State, TEXT("open")) != TEXT("true");
    FFoundationBlockState Used = Registry.WithValue(State, TEXT("open"), OpenValueName(bOpen));
    TArray<TPair<FIntVector, FFoundationBlockState>, TInlineAllocator<2>> Writes;
    if (GetFamily(Block) == EFoundationBlockFamily::FenceGate && bOpen)
    {
        // An opening gate that faces the player first turns to the player's look, so it swings away
        // (survival.r9.switches; VALUES "Fence gates").
        double Yaw, Pitch;
        FoundationBlockPlacement::GetYawPitch(Player.Facts.Aim, Yaw, Pitch);
        const EFoundationFace Look = FoundationBlockPlacement::GetHorizontalLook(Yaw);
        if (GetFacing(Registry, State) == FoundationBlockPlacement::GetOpposite(Look))
            Used = Registry.WithValue(Used, TEXT("facing"), SideName(Look));
    }
    Writes.Emplace(Hit.Cell, Used);
    // Both door halves open in the one use (survival.r9.switches); a lone half (a world saved before B3a) alone.
    if (GetFamily(Block) == EFoundationBlockFamily::Door)
    {
        const bool bLower = Registry.GetValue(State, TEXT("half")) == TEXT("lower");
        const FIntVector Other = Hit.Cell + FIntVector(0, 0, bLower ? 1 : -1);
        // Its other half unknown (the edge of the streamed world): the click is consumed, nothing written.
        if (!World.IsKnown(Other))
        {
            Result.Outcome = EFoundationOutcome::Rejected;
            Result.Reason = EFoundationReason::OutsideBounds;
            return true;
        }
        if (IsDoorHalf(Registry, World.Get(Other).State, bLower ? TEXT("upper") : TEXT("lower")))
            Writes.Emplace(Other, Registry.WithValue(World.Get(Other).State, TEXT("open"), OpenValueName(bOpen)));
    }
    Authority.CommitBlockUse(Player, Hit.Cell, Writes, GetUseSound(Block, bOpen), Result);
    return true;
}

void FoundationBlockFamilies::ReleaseButton(FFoundationAuthority& Authority, const FIntVector& Cell)
{
    const FFoundationBlockRegistry& Registry = Authority.World.GetRegistry();
    const FFoundationBlockState State = Authority.World.Get(Cell).State;
    const TPair<FIntVector, FFoundationBlockState> Write(
        Cell, Registry.WithValue(State, TEXT("powered"), TEXT("false")));
    // No player releases it: its event (and any pop it causes) carries the default player, id 0, step 0.
    static const FFoundationPlayer Nobody;
    FFoundationResult Released;
    Authority.CommitBlockUse(
        Nobody, Cell, MakeArrayView(&Write, 1), GetUseSound(Registry.GetBlock(State), false), Released, 1.f, 1.f);
}

int32 FoundationBlockFamilies::GetButtonTicks(const FFoundationBlockType& Block)
{
    if (GetFamily(Block) != EFoundationBlockFamily::Button) return 0;
    return HasTag(Block, GetTags().WoodenButtons) ? 30 : 20;
}

FName FoundationBlockFamilies::GetUseSound(const FFoundationBlockType& Block, bool bOpen)
{
    const EFoundationBlockFamily Family = GetFamily(Block);
    if (Family == EFoundationBlockFamily::Lever) return TEXT("block.lever.click");
    const FString Id = Block.Id.ToString();
    if (Family == EFoundationBlockFamily::Button)
    {
        const TCHAR* Set = !HasTag(Block, GetTags().WoodenButtons)              ? TEXT("stone")
            : Id.StartsWith(TEXT("cherry_"))                                    ? TEXT("cherry_wood")
            : Id.StartsWith(TEXT("bamboo_"))                                    ? TEXT("bamboo_wood")
            : Id.StartsWith(TEXT("crimson_")) || Id.StartsWith(TEXT("warped_")) ? TEXT("nether_wood")
                                                                                : TEXT("wooden");
        return FName(FString::Printf(TEXT("block.%s_button.%s"), Set, bOpen ? TEXT("click_on") : TEXT("click_off")));
    }
    const TCHAR* Kind = Family == EFoundationBlockFamily::Door ? TEXT("door")
        : Family == EFoundationBlockFamily::Trapdoor           ? TEXT("trapdoor")
        : Family == EFoundationBlockFamily::FenceGate          ? TEXT("fence_gate")
                                                               : nullptr;
    if (!Kind) return NAME_None;
    // The set types' sounds; the other woods share wooden_door, wooden_trapdoor and fence_gate.
    const TCHAR* Set = Id.Contains(TEXT("copper"))                          ? TEXT("copper_")
        : Id.StartsWith(TEXT("iron_"))                                      ? TEXT("iron_")
        : Id.StartsWith(TEXT("cherry_"))                                    ? TEXT("cherry_wood_")
        : Id.StartsWith(TEXT("bamboo_"))                                    ? TEXT("bamboo_wood_")
        : Id.StartsWith(TEXT("crimson_")) || Id.StartsWith(TEXT("warped_")) ? TEXT("nether_wood_")
        : Family == EFoundationBlockFamily::FenceGate                       ? TEXT("")
                                                                            : TEXT("wooden_");
    return FName(FString::Printf(TEXT("block.%s%s.%s"), Set, Kind, bOpen ? TEXT("open") : TEXT("close")));
}

EFoundationFamilySurvival FoundationBlockFamilies::GetSurvival(
    const FFoundationBlockWorld& World, const FIntVector& Cell, FFoundationBlockState State)
{
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    const EFoundationBlockFamily Family = GetStateFamily(Registry, State);
    if (!IsInteractOn()) return EFoundationFamilySurvival::NoRule;
    // A button or lever: the face of the block it hangs on, sturdy (family_survival.json).
    if (Family == EFoundationBlockFamily::Button || Family == EFoundationBlockFamily::Lever)
    {
        const FName Face = Registry.GetValue(State, TEXT("face"));
        const EFoundationFace Toward = Face == TEXT("floor") ? EFoundationFace::Down
            : Face == TEXT("ceiling")                        ? EFoundationFace::Up
                                      : FoundationBlockPlacement::GetOpposite(GetFacing(Registry, State));
        const FIntVector Support = Cell + FoundationGrid::FaceOffset(Toward);
        if (!World.IsKnown(Support)) return EFoundationFamilySurvival::Unknown;
        return IsFaceSturdy(Registry, World.Get(Support).State, FoundationBlockPlacement::GetOpposite(Toward))
            ? EFoundationFamilySurvival::Survives
            : EFoundationFamilySurvival::Fails;
    }
    if (Family != EFoundationBlockFamily::Door) return EFoundationFamilySurvival::NoRule;
    const FIntVector Below = Cell - FIntVector(0, 0, 1);
    if (!World.IsKnown(Below)) return EFoundationFamilySurvival::Unknown;
    const FFoundationBlockState Support = World.Get(Below).State;
    const bool bSurvives = Registry.GetValue(State, TEXT("half")) == TEXT("upper")
        ? Registry.GetBlock(State).Contains(Support)
        : IsFaceSturdy(Registry, Support, EFoundationFace::Up);
    return bSurvives ? EFoundationFamilySurvival::Survives : EFoundationFamilySurvival::Fails;
}
