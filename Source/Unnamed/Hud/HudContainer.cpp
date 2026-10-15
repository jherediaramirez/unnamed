#include "Hud/HudContainer.h"

namespace
{
    using FContainerModelMenu = FFoundationContainerMenu;
    const FHudCreativeStack EmptyContainerStack;
}

const FHudCreativeStack& FHudContainerView::Get(int32 Slot) const
{
    return Slots.IsValidIndex(Slot) ? Slots[Slot] : EmptyContainerStack;
}

FIntPoint FHudContainerModel::GetSlotPosition(int32 Slot)
{
    if (Slot == 0) return {154, 28};
    if (Slot < FContainerModelMenu::FirstArmor) return {98 + (Slot - 1) % 2 * 18, 18 + (Slot - 1) / 2 * 18};
    if (Slot < FContainerModelMenu::FirstMain) return {8, 8 + (Slot - FContainerModelMenu::FirstArmor) * 18};
    if (Slot < FContainerModelMenu::FirstHotbar)
        return {8 + (Slot - FContainerModelMenu::FirstMain) % 9 * 18,
            84 + (Slot - FContainerModelMenu::FirstMain) / 9 * 18};
    if (Slot < FContainerModelMenu::OffhandSlot) return {8 + (Slot - FContainerModelMenu::FirstHotbar) * 18, 142};
    return {77, 62};
}

FIntPoint FHudContainerModel::GetSlotPosition(EFoundationMenuKind Kind, int32 Slot)
{
    if (Kind == EFoundationMenuKind::Inventory) return GetSlotPosition(Slot);
    // The crafting table: result 124,35; the 3x3 from 30,17. The furnace: input 56,17, fuel 56,53, result 116,35. Both:
    // the main grid from 8,84; the hotbar at y 142.
    const FFoundationMenuLayout& Layout = FFoundationMenuLayout::Get(Kind);
    if (Layout.IsContainer(Slot))
    {
        static const FIntPoint Furnace[] = {{56, 17}, {56, 53}, {116, 35}};
        const int32 Index = Slot - Layout.FirstContainer;
        return Kind == EFoundationMenuKind::Furnace && Index < UE_ARRAY_COUNT(Furnace) ? Furnace[Index]
                                                                                       : FIntPoint(-1000, -1000);
    }
    if (Layout.HasCraftResult() && Slot == FContainerModelMenu::ResultSlot) return {124, 35};
    if (Layout.IsCraft(Slot))
        return {30 + (Slot - Layout.FirstCraft) % Layout.CraftWidth * 18,
            17 + (Slot - Layout.FirstCraft) / Layout.CraftWidth * 18};
    if (Layout.IsMain(Slot)) return {8 + (Slot - Layout.FirstMain) % 9 * 18, 84 + (Slot - Layout.FirstMain) / 9 * 18};
    if (Layout.IsHotbar(Slot)) return {8 + (Slot - Layout.FirstHotbar) * 18, 142};
    // Not a slot: far outside the window, so nothing hits it.
    return {-1000, -1000};
}

FIntPoint FHudContainerModel::GetRecipeButton(EFoundationMenuKind Kind)
{
    return Kind == EFoundationMenuKind::Crafting ? FIntPoint(CraftingRecipeButtonX, CraftingRecipeButtonY)
        : Kind == EFoundationMenuKind::Furnace   ? FIntPoint(FurnaceRecipeButtonX, FurnaceRecipeButtonY)
                                                 : FIntPoint(RecipeButtonX, RecipeButtonY);
}

int32 FHudContainerModel::GetFlameHeight(bool bLit, float LitFraction)
{
    return bLit && LitFraction > 0
        ? FMath::Min(FMath::CeilToInt(FMath::Clamp(LitFraction, 0.f, 1.f) * 13) + 1, FlameSize)
        : 0;
}

int32 FHudContainerModel::GetArrowWidth(float CookFraction)
{
    return FMath::CeilToInt(FMath::Clamp(CookFraction, 0.f, 1.f) * ArrowWidth);
}

int32 FHudContainerModel::GetResultSlot() const
{
    const FFoundationMenuLayout& Layout = FFoundationMenuLayout::Get(Kind);
    return Layout.HasCraftResult() ? FContainerModelMenu::ResultSlot : Layout.TakeOnly;
}

bool FHudContainerModel::IsShown(int32 Slot) const
{
    const FFoundationMenuLayout& Layout = FFoundationMenuLayout::Get(Kind);
    return Slot >= 0 && Slot < Layout.Slots && (bFullInventory || (!Layout.IsMain(Slot) && !Layout.IsArmor(Slot)));
}

int32 FHudContainerModel::HitTest(FIntPoint Point) const
{
    if (Point.X < 0 || Point.Y < 0 || Point.X >= Width || Point.Y >= Height) return FContainerModelMenu::Outside;
    for (int32 Slot = 0; Slot < GetSlots(); ++Slot)
    {
        // A slot's hover area is its 16 x 16 item area and the pixel around it.
        const FIntPoint At = GetPosition(Slot);
        if (Point.X >= At.X - 1 && Point.X < At.X + 17 && Point.Y >= At.Y - 1 && Point.Y < At.Y + 17)
            return IsShown(Slot) ? Slot : INDEX_NONE;
    }
    return INDEX_NONE;
}

bool FHudContainerModel::IsInRecipeButton(EFoundationMenuKind Kind, FIntPoint Point)
{
    const FIntPoint At = GetRecipeButton(Kind);
    return Point.X >= At.X && Point.X < At.X + RecipeButtonWidth && Point.Y >= At.Y &&
        Point.Y < At.Y + RecipeButtonHeight;
}

int32 FHudContainerModel::GetMaxStack(FName Item) const
{
    const FHudCreativeItem* Info = Items.Find(Item);
    return Info ? FMath::Max(Info->MaxStack, 1) : 64;
}

bool FHudContainerModel::CanDragTo(int32 Slot, const FHudContainerView& View) const
{
    // Never the result slot (it only gives); the crafting grid takes a drag like any slot.
    const FFoundationMenuLayout& Layout = FFoundationMenuLayout::Get(Kind);
    if (Slot < Layout.FirstCraft || Slot >= Layout.Slots || View.Carried.IsEmpty() || Slot == GetResultSlot())
        return false;
    // A furnace's fuel slot takes only fuel and the empty bucket.
    if (Layout.FuelSlot != INDEX_NONE && Slot == Layout.FuelSlot && !FuelItems.Contains(View.Carried.Item))
        return false;
    if (Layout.IsArmor(Slot))
    {
        // The armor slots, head to feet, take only their own slot's items.
        static const EFoundationEquipmentSlot Order[] = {EFoundationEquipmentSlot::Head,
            EFoundationEquipmentSlot::Chest, EFoundationEquipmentSlot::Legs, EFoundationEquipmentSlot::Feet};
        const FHudCreativeItem* Info = Items.Find(View.Carried.Item);
        if (!Info || Info->Equipment != Order[Slot - Layout.FirstArmor]) return false;
    }
    const FHudCreativeStack& Stack = View.Get(Slot);
    return Stack.IsEmpty() ||
        (Stack.Item == View.Carried.Item && Stack.Damage == View.Carried.Damage &&
            Stack.Count < GetMaxStack(Stack.Item));
}

TArray<FHudContainerClick> FHudContainerModel::MouseDown(
    FIntPoint Point, int32 Button, bool bShift, const FHudContainerView& View, double Time)
{
    // Middle clicks clone only in creative; a press during a drag does nothing.
    if (Button < 0 || Button > 1 || IsDragging() || bDoubleClick) return {};
    const int32 Slot = HitTest(Point);
    if (Slot == INDEX_NONE || !IsClickable(Slot)) return {};
    // The second left press on a slot: with a stack on the cursor it collects (PICKUP_ALL), after a shift-click it
    // moves every stack like the one moved; either happens on the release. Never on the result slot: each click there
    // crafts (provisional, vanilla play: spam-clicking an output crafts each time; the value report has no double-click
    // row; Codex review 10, finding 4).
    const bool bDouble = Button == 0 && Slot != FContainerModelMenu::Outside && Slot != GetResultSlot() &&
        Slot == LastSlot && Time - LastTime < DoubleClickSeconds &&
        (bShift ? !LastQuickMoved.IsEmpty() : !View.Carried.IsEmpty());
    LastSlot = Slot;
    LastTime = bDouble ? -1000 : Time;
    if (bDouble)
    {
        bDoubleClick = true;
        bShiftDoubleClick = bShift;
        return {};
    }
    if (Slot == FContainerModelMenu::Outside)
        return View.Carried.IsEmpty()
            ? TArray<FHudContainerClick>()
            : TArray<FHudContainerClick>{{FContainerModelMenu::Outside, Button, EFoundationClickType::Pickup}};
    if (bShift)
    {
        LastQuickMoved = View.Get(Slot);
        return {{Slot, Button, EFoundationClickType::QuickMove}};
    }
    LastQuickMoved = {};
    if (!View.Carried.IsEmpty())
    {
        // With a stack on the cursor the press starts a drag; the release decides between a click and a spread.
        DragButton = Button;
        PressSlot = Slot;
        DragSlots.Reset();
        if (CanDragTo(Slot, View)) DragSlots.Add(Slot);
        return {};
    }
    return {{Slot, Button, EFoundationClickType::Pickup}};
}

TArray<FHudContainerClick> FHudContainerModel::MouseMove(FIntPoint Point, const FHudContainerView& View)
{
    if (!IsDragging()) return {};
    // Each dragged slot takes at least one item.
    const int32 Slot = HitTest(Point);
    if (Slot != INDEX_NONE && Slot != FContainerModelMenu::Outside && !DragSlots.Contains(Slot) &&
        CanDragTo(Slot, View) && DragSlots.Num() < View.Carried.Count)
        DragSlots.Add(Slot);
    return {};
}

TArray<FHudContainerClick> FHudContainerModel::MouseUp(FIntPoint Point, int32 Button, const FHudContainerView& View)
{
    TArray<FHudContainerClick> Clicks;
    if (bDoubleClick && Button == 0)
    {
        bDoubleClick = false;
        if (!bShiftDoubleClick)
            Clicks.Add({LastSlot, 0, EFoundationClickType::PickupAll});
        else
            // Every slot past the crafting grid (the armor on, for the inventory; the main grid on, for a table).
            for (int32 Slot = FFoundationMenuLayout::Get(Kind).GetCraftEnd(); Slot < GetSlots(); ++Slot)
                if (HitTest(GetPosition(Slot)) == Slot && !View.Get(Slot).IsEmpty() &&
                    View.Get(Slot).Item == LastQuickMoved.Item && View.Get(Slot).Damage == LastQuickMoved.Damage)
                    Clicks.Add({Slot, 0, EFoundationClickType::QuickMove});
        return Clicks;
    }
    if (!IsDragging() || Button != DragButton) return Clicks;
    if (DragSlots.Num() > 1)
    {
        Clicks.Add({FContainerModelMenu::Outside, FContainerModelMenu::QuickCraftButton(0, DragButton),
            EFoundationClickType::QuickCraft});
        for (const int32 Slot : DragSlots)
            Clicks.Add({Slot, FContainerModelMenu::QuickCraftButton(1, DragButton), EFoundationClickType::QuickCraft});
        Clicks.Add({FContainerModelMenu::Outside, FContainerModelMenu::QuickCraftButton(2, DragButton),
            EFoundationClickType::QuickCraft});
    }
    else
        // One collected slot is a plain click there (vanilla turns a one-slot QUICK_CRAFT into a PICKUP of it); with
        // none, the press slot (Codex 5b finding 2).
        Clicks.Add({DragSlots.Num() == 1 ? DragSlots[0] : PressSlot, DragButton, EFoundationClickType::Pickup});
    DragButton = PressSlot = INDEX_NONE;
    DragSlots.Reset();
    return Clicks;
}

TArray<FHudContainerClick> FHudContainerModel::PressSwap(
    FIntPoint Point, int32 Button, const FHudContainerView& View) const
{
    const int32 Slot = HitTest(Point);
    if (!View.Carried.IsEmpty() || IsDragging() || Slot == INDEX_NONE || Slot == FContainerModelMenu::Outside ||
        !IsClickable(Slot))
        return {};
    return {{Slot, Button, EFoundationClickType::Swap}};
}

TArray<FHudContainerClick> FHudContainerModel::PressDrop(
    FIntPoint Point, bool bControl, const FHudContainerView& View) const
{
    const int32 Slot = HitTest(Point);
    if (!View.Carried.IsEmpty() || IsDragging() || Slot == INDEX_NONE || Slot == FContainerModelMenu::Outside ||
        !IsClickable(Slot))
        return {};
    return {{Slot, bControl ? 1 : 0, EFoundationClickType::Throw}};
}

void FHudContainerModel::ResetDrag()
{
    DragButton = PressSlot = INDEX_NONE;
    DragSlots.Reset();
}

void FHudContainerModel::Reset()
{
    ResetDrag();
    LastSlot = INDEX_NONE;
    bDoubleClick = bShiftDoubleClick = false;
    LastTime = -1000;
    LastQuickMoved = {};
}

void FHudContainerModel::GetDragPreview(
    const FHudContainerView& View, TMap<int32, FHudCreativeStack>& Slots, FHudCreativeStack& Carried) const
{
    Slots.Reset();
    Carried = View.Carried;
    if (DragSlots.Num() < 2 || View.Carried.IsEmpty()) return;
    const int32 Each = DragButton == 0 ? View.Carried.Count / DragSlots.Num() : 1;
    int32 Remaining = View.Carried.Count;
    for (const int32 Slot : DragSlots)
    {
        const FHudCreativeStack& Stack = View.Get(Slot);
        const int32 Existing = Stack.IsEmpty() ? 0 : Stack.Count;
        const int32 Place = FMath::Min(Each + Existing, GetMaxStack(View.Carried.Item));
        Remaining -= FMath::Max(Place - Existing, 0);
        Slots.Add(Slot, {View.Carried.Item, FMath::Max(Place, Existing), View.Carried.Damage});
    }
    Carried.Count = Remaining;
    if (Carried.Count <= 0) Carried = {};
}
