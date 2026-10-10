#include "Foundation/FoundationPlantRules.h"
#include "Foundation/FoundationBlockPlacement.h"

namespace
{
    const TCHAR* const PlantHalf = TEXT("half");
    const TCHAR* const PlantWaterlogged = TEXT("waterlogged");

    bool IsModelState(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        return Registry.GetShape(State) == EFoundationBlockShape::Model;
    }

    bool HoldsWaterSource(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        const FFoundationFluid Fluid = Registry.GetFluid(State);
        return Fluid.bWater && Fluid.bSource;
    }

    // Vine, glow lichen and sculk vein: a true/false property per face they can hold.
    bool IsMultiface(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        const FName North = Registry.GetValue(State, TEXT("north"));
        return IsModelState(Registry, State) && (North == TEXT("true") || North == TEXT("false")) &&
            Registry.GetBlock(State).FindProperty(TEXT("up")) != INDEX_NONE;
    }

    // Blocks held by the block behind their facing (T1: the support is opposite the facing).
    bool FacesAwayFromSupport(FName Id)
    {
        static const TSet<FName> Ids = {TEXT("amethyst_cluster"), TEXT("large_amethyst_bud"),
            TEXT("medium_amethyst_bud"), TEXT("small_amethyst_bud"), TEXT("shelf_mushroom"),
            TEXT("tube_coral_wall_fan"), TEXT("brain_coral_wall_fan"), TEXT("bubble_coral_wall_fan"),
            TEXT("fire_coral_wall_fan"), TEXT("horn_coral_wall_fan")};
        return Ids.Contains(Id);
    }
}

FFoundationBlockState FoundationPlantRules::GetStateForPlacement(
    const FFoundationBlockWorld& World, const FFoundationBlockType& Block, const FFoundationPlacementContext& Context)
{
    using namespace FoundationBlockPlacement;
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    FFoundationBlockState State = Block.GetDefaultState();
    auto Has = [&Block](const TCHAR* Property) { return Block.FindProperty(Property) != INDEX_NONE; };
    auto Set = [&Registry, &State](const TCHAR* Property, const TCHAR* Value)
    { State = Registry.WithValue(State, Property, Value); };
    const EFoundationFace Clicked = Context.ClickedFace;
    const EFoundationFace TowardClicked = GetOpposite(Clicked);
    if (Has(PlantHalf)) Set(PlantHalf, TEXT("lower"));
    if (Has(TEXT("facing")))
        Set(TEXT("facing"),
            FacesAwayFromSupport(Block.Id)  ? FoundationGrid::FaceName(Clicked)
                : Block.Id == TEXT("cocoa") ? FoundationGrid::FaceName(TowardClicked)
                                            : FoundationGrid::FaceName(GetOpposite(GetHorizontalLook(Context.Yaw))));
    if (IsMultiface(Registry, State))
    {
        // The default state holds no face; set the one the click names (a vine has no down face).
        const bool bVineUnderside = Block.Id == TEXT("vine") && TowardClicked == EFoundationFace::Up;
        if (!bVineUnderside) Set(FoundationGrid::FaceName(TowardClicked), TEXT("true"));
    }
    if (Has(TEXT("vertical_direction")))
    {
        const bool bUp = Clicked == EFoundationFace::Up || (Clicked != EFoundationFace::Down && Context.Pitch >= 0);
        Set(TEXT("vertical_direction"), bUp ? TEXT("up") : TEXT("down"));
    }
    if (Has(PlantWaterlogged))
        Set(PlantWaterlogged,
            HoldsWaterSource(Registry, World.Get(Context.Destination).State) ? TEXT("true") : TEXT("false"));
    return State;
}

bool FoundationPlantRules::GetOtherHalf(const FFoundationBlockWorld& World, const FIntVector& Cell,
    FFoundationBlockState State, FIntVector& OutCell, FFoundationBlockState& OutState)
{
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    if (!IsModelState(Registry, State)) return false;
    const FName Half = Registry.GetValue(State, PlantHalf);
    const bool bLower = Half == TEXT("lower");
    if (!bLower && Half != TEXT("upper")) return false;
    OutCell = Cell + FIntVector(0, 0, bLower ? 1 : -1);
    OutState = Registry.WithValue(State, PlantHalf, bLower ? TEXT("upper") : TEXT("lower"));
    if (Registry.GetBlock(State).FindProperty(PlantWaterlogged) != INDEX_NONE)
        OutState = Registry.WithValue(OutState, PlantWaterlogged,
            HoldsWaterSource(Registry, World.Get(OutCell).State) ? TEXT("true") : TEXT("false"));
    return true;
}

bool FoundationPlantRules::IsOtherHalf(
    const FFoundationBlockRegistry& Registry, FFoundationBlockState State, FFoundationBlockState Other)
{
    if (!IsModelState(Registry, State) || !Registry.GetBlock(State).Contains(Other)) return false;
    const FName Half = Registry.GetValue(State, PlantHalf), OtherHalf = Registry.GetValue(Other, PlantHalf);
    return (Half == TEXT("lower") && OtherHalf == TEXT("upper")) ||
        (Half == TEXT("upper") && OtherHalf == TEXT("lower"));
}

bool FoundationPlantRules::HasNoFace(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
{
    if (!IsMultiface(Registry, State)) return false;
    for (int32 Face = 0; Face < 6; ++Face)
        if (Registry.GetValue(State, FoundationGrid::FaceName(EFoundationFace(Face))) == TEXT("true")) return false;
    return true;
}
