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

int32 FHudContainerModel::HitTest(FIntPoint Point) const
{
    if (Point.X < 0 || Point.Y < 0 || Point.X >= Width || Point.Y >= Height) return FContainerModelMenu::Outside;
    for (int32 Slot = 0; Slot < FContainerModelMenu::PlayerMenuSlots; ++Slot)
    {
        // A slot's hover area is its 16 x 16 item area and the pixel around it.
        const FIntPoint At = GetSlotPosition(Slot);
        if (Point.X >= At.X - 1 && Point.X < At.X + 17 && Point.Y >= At.Y - 1 && Point.Y < At.Y + 17)
            return bFullInventory || Slot < FContainerModelMenu::FirstArmor || Slot >= FContainerModelMenu::FirstHotbar
                ? Slot
                : INDEX_NONE;
    }
    return INDEX_NONE;
}

bool FHudContainerModel::IsInRecipeButton(FIntPoint Point)
{
    return Point.X >= RecipeButtonX && Point.X < RecipeButtonX + RecipeButtonWidth && Point.Y >= RecipeButtonY &&
        Point.Y < RecipeButtonY + RecipeButtonHeight;
}

int32 FHudContainerModel::GetMaxStack(FName Item) const
{
    const FHudCreativeItem* Info = Items.Find(Item);
    return Info ? FMath::Max(Info->MaxStack, 1) : 64;
}

bool FHudContainerModel::CanDragTo(int32 Slot, const FHudContainerView& View) const
{
    if (Slot < FContainerModelMenu::FirstArmor || Slot >= FContainerModelMenu::PlayerMenuSlots ||
        View.Carried.IsEmpty())
        return false;
    if (Slot < FContainerModelMenu::FirstMain)
    {
        // The armor slots, head to feet, take only their own slot's items.
        static const EFoundationEquipmentSlot Order[] = {EFoundationEquipmentSlot::Head,
            EFoundationEquipmentSlot::Chest, EFoundationEquipmentSlot::Legs, EFoundationEquipmentSlot::Feet};
        const FHudCreativeItem* Info = Items.Find(View.Carried.Item);
        if (!Info || Info->Equipment != Order[Slot - FContainerModelMenu::FirstArmor]) return false;
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
    // moves every stack like the one moved; either happens on the release.
    const bool bDouble = Button == 0 && Slot != FContainerModelMenu::Outside && Slot == LastSlot &&
        Time - LastTime < DoubleClickSeconds && (bShift ? !LastQuickMoved.IsEmpty() : !View.Carried.IsEmpty());
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
            for (int32 Slot = FContainerModelMenu::FirstArmor; Slot < FContainerModelMenu::PlayerMenuSlots; ++Slot)
                if (HitTest(GetSlotPosition(Slot)) == Slot && !View.Get(Slot).IsEmpty() &&
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
        Clicks.Add({PressSlot, DragButton, EFoundationClickType::Pickup});
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

void FHudContainerModel::Reset()
{
    DragButton = PressSlot = LastSlot = INDEX_NONE;
    DragSlots.Reset();
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
