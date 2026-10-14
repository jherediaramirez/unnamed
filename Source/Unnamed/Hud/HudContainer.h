#pragma once

#include "CoreMinimal.h"
#include "Hud/HudCreative.h"

// One click the survival inventory screen sends as ContainerClick (container 0, FFoundationContainerMenu's slots).
struct FHudContainerClick
{
    int32 Slot = 0;
    int32 Button = 0;
    EFoundationClickType Type = EFoundationClickType::Pickup;
    bool operator==(const FHudContainerClick& Other) const = default;
};

// What the screen shows: the player's menu slots by their menu number (0-45, FFoundationContainerMenu) and the
// cursor stack.
struct FHudContainerView
{
    TArray<FHudCreativeStack> Slots;
    FHudCreativeStack Carried;

    const FHudCreativeStack& Get(int32 Slot) const;
};

// Java 26.3's survival inventory screen (InventoryScreen over InventoryMenu) as layout and mouse gestures, in GUI
// pixels relative to the screen's top-left corner (176 x 166, textures/gui/container/inventory.png). UObject-free: it
// turns presses, moves and releases into the clicks vanilla's screen sends (minecraft.wiki "Inventory", Controls):
// left and right PICKUP, shift QUICK_MOVE, a drag with a stack QUICK_CRAFT, a double-click PICKUP_ALL (with shift, a
// QUICK_MOVE of every stack like the last one moved), number keys and F SWAP, Q THROW, a click outside PICKUP -999.
// The authority runs them (FFoundationContainerMenu); the widget only draws and forwards input.
class UNNAMED_API FHudContainerModel
{
public:
    static constexpr int32 Width = 176;
    static constexpr int32 Height = 166;
    // The "Crafting" label (vanilla's titleLabelX/Y; provisional, minecraft.wiki screenshots, no capture yet).
    static constexpr int32 TitleX = 97;
    static constexpr int32 TitleY = 8;
    // The recipe-book button: leftPos + 104, height / 2 - 22, 20 x 18, which is 61 below topPos for any height
    // (topPos is (height - 166) / 2). Provisional (minecraft.wiki); inert until the recipe book exists.
    static constexpr int32 RecipeButtonX = 104;
    static constexpr int32 RecipeButtonY = 61;
    static constexpr int32 RecipeButtonWidth = 20;
    static constexpr int32 RecipeButtonHeight = 18;
    // Two presses of the left button on one slot this close together are a double-click (provisional: vanilla's
    // 250 ms, minecraft.wiki).
    static constexpr double DoubleClickSeconds = .25;

    // A menu slot's 16 x 16 item area (top-left), measured from inventory.png's slot frames.
    static FIntPoint GetSlotPosition(int32 Slot);
    // The menu slot under Point; FFoundationContainerMenu::Outside outside the window; INDEX_NONE inside it but on no
    // slot. Without the full inventory (bFullInventory off) the main and armor slots are not slots.
    int32 HitTest(FIntPoint Point) const;
    // The crafting slots (0-4) highlight but do nothing until survival step 6.
    static bool IsClickable(int32 Slot) { return Slot == FFoundationContainerMenu::Outside || Slot >= 5; }
    static bool IsInRecipeButton(FIntPoint Point);

    // Button 0 left, 1 right, 2 middle; Time in seconds from any fixed clock.
    TArray<FHudContainerClick> MouseDown(
        FIntPoint Point, int32 Button, bool bShift, const FHudContainerView& View, double Time);
    TArray<FHudContainerClick> MouseMove(FIntPoint Point, const FHudContainerView& View);
    TArray<FHudContainerClick> MouseUp(FIntPoint Point, int32 Button, const FHudContainerView& View);
    // A number key (Button 0-8) or F (40) over a slot with an empty cursor: SWAP with that hotbar slot or the offhand.
    TArray<FHudContainerClick> PressSwap(FIntPoint Point, int32 Button, const FHudContainerView& View) const;
    // Q over a slot with an empty cursor: THROW one, with control the whole stack.
    TArray<FHudContainerClick> PressDrop(FIntPoint Point, bool bControl, const FHudContainerView& View) const;
    // Forgets a drag and the last click (the screen closed).
    void Reset();

    bool IsDragging() const { return DragButton != INDEX_NONE; }
    const TArray<int32>& GetDragSlots() const { return DragSlots; }
    // A drag over more than one slot shows its outcome before the release: each slot's stack after the split and
    // what stays on the cursor. Slots holds the dragged slots only.
    void GetDragPreview(
        const FHudContainerView& View, TMap<int32, FHudCreativeStack>& Slots, FHudCreativeStack& Carried) const;

    // The items' stack sizes and equipment slots (the drag only adds slots that can take the stack).
    FHudCreativeItems Items;
    bool bFullInventory = true;

private:
    int32 GetMaxStack(FName Item) const;
    // The menu's rules for a drag slot: the slot takes the item (armor by equipment) and is empty or holds the same
    // item with room.
    bool CanDragTo(int32 Slot, const FHudContainerView& View) const;

    int32 DragButton = INDEX_NONE;
    int32 PressSlot = INDEX_NONE;
    TArray<int32> DragSlots;
    bool bDoubleClick = false;
    bool bShiftDoubleClick = false;
    int32 LastSlot = INDEX_NONE;
    double LastTime = -1000;
    // The stack (item and damage) of the last shift-click, for a shift double-click; a plain press forgets it.
    FHudCreativeStack LastQuickMoved;
};
