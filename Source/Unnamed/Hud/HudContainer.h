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

// What the screen shows: the menu's slots by their menu number (FFoundationContainerMenu: the player's 0-45, or a
// crafting table's 0-45) and the cursor stack.
struct FHudContainerView
{
    TArray<FHudCreativeStack> Slots;
    FHudCreativeStack Carried;
    // A furnace's fuel left (litTime / litDuration) and cooking progress (cookingTimer / cookingTotalTime), 0-1.
    bool bLit = false;
    float LitFraction = 0;
    float CookFraction = 0;

    const FHudCreativeStack& Get(int32 Slot) const;
};

// Java 26.3's survival inventory screen (InventoryScreen over InventoryMenu) as layout and mouse gestures, in GUI
// pixels relative to the screen's top-left corner (176 x 166, textures/gui/container/inventory.png). UObject-free: it
// turns presses, moves and releases into the clicks vanilla's screen sends (minecraft.wiki "Inventory", Controls):
// left and right PICKUP, shift QUICK_MOVE, a drag with a stack QUICK_CRAFT, a double-click PICKUP_ALL (with shift, a
// QUICK_MOVE of every stack like the last one moved), number keys and F SWAP, Q THROW, a click outside PICKUP -999.
// The authority runs them (FFoundationContainerMenu); the widget only draws and forwards input. Kind Crafting is the
// crafting table's screen (survival step 7, CraftingScreen over CraftingMenu,
// textures/gui/container/crafting_table.png, the same 176 x 166): the same gestures over its own slots.
class UNNAMED_API FHudContainerModel
{
public:
    static constexpr int32 Width = 176;
    static constexpr int32 Height = 166;
    // The barrel (T1 step 9, ContainerScreen over a 3-row ChestMenu, textures/gui/container/generic_54.png): 176 x 168,
    // its slots from 8,18, the main grid from 8,85, the hotbar at y 143 (value-swaps.md "Barrel", inventory.layouts);
    // its title "Barrel" at 8,6 and "Inventory" at 8,74 (ContainerScreen: imageHeight - 94); no recipe-book button.
    static constexpr int32 BarrelHeight = 168;
    static constexpr int32 BarrelPlayerY = 85;
    static constexpr int32 BarrelHotbarY = 143;
    static constexpr int32 BarrelInventoryLabelY = 74;
    // generic_54.png: the top (the title and 3 rows, 71 high) and its player part (96 high from y 126).
    static constexpr int32 BarrelTopHeight = 71;
    static constexpr int32 BarrelPlayerTextureY = 126;
    static constexpr int32 BarrelPlayerHeight = 96;
    // A chest's screen is the barrel's (T1 step 18-chest); a double chest's (6 rows, value-swaps.md "generic_54"):
    // 176 x 222, its slots from 8,18, the main grid from 8,139, the hotbar at y 197, "Inventory" at 8,128; the
    // texture's top 125 high (the title and 6 rows), then the same player part.
    static constexpr int32 LargeChestHeight = 222;
    static constexpr int32 LargeChestPlayerY = 139;
    static constexpr int32 LargeChestHotbarY = 197;
    static constexpr int32 LargeChestInventoryLabelY = 128;
    static constexpr int32 LargeChestTopHeight = 125;
    // The screen's height: the barrel's and chest's 168, the double chest's 222, else 166.
    static int32 GetHeight(EFoundationMenuKind Kind)
    {
        return Kind == EFoundationMenuKind::LargeChest ? LargeChestHeight : HasViewers(Kind) ? BarrelHeight : Height;
    }
    // Whether the screen has the recipe-book button (vanilla's ContainerScreen has none).
    static bool HasRecipeButton(EFoundationMenuKind Kind) { return !HasViewers(Kind); }
    // The "Crafting" label (vanilla's titleLabelX/Y; provisional, minecraft.wiki screenshots, no capture yet).
    static constexpr int32 TitleX = 97;
    static constexpr int32 TitleY = 8;
    // The recipe-book button: leftPos + 104, height / 2 - 22, 20 x 18, which is 61 below topPos for any height
    // (topPos is (height - 166) / 2). Measured (value report SURVIVAL-FINDINGS R8, survival.r8.recipe_book: the book
    // open puts it at 413,158 with the menu at 309,97); inert until the recipe book exists.
    static constexpr int32 RecipeButtonX = 104;
    static constexpr int32 RecipeButtonY = 61;
    static constexpr int32 RecipeButtonWidth = 20;
    static constexpr int32 RecipeButtonHeight = 18;
    // Two presses of the left button on one slot this close together are a double-click (provisional: vanilla's
    // 250 ms, minecraft.wiki).
    static constexpr double DoubleClickSeconds = .25;

    // The crafting table's labels and recipe-book button (provisional: CraftingScreen's "Crafting" at 29,6, the
    // "Inventory" label at 8,72 and the button at leftPos + 5, height / 2 - 49, which is 34 below topPos;
    // minecraft.wiki screenshots, no capture yet).
    static constexpr int32 CraftingTitleX = 29;
    static constexpr int32 CraftingTitleY = 6;
    static constexpr int32 InventoryLabelX = 8;
    static constexpr int32 InventoryLabelY = 72;
    static constexpr int32 CraftingRecipeButtonX = 5;
    static constexpr int32 CraftingRecipeButtonY = 34;

    // The furnace (T1 step 8a2, AbstractFurnaceScreen, textures/gui/container/furnace.png, the same 176 x 166): input
    // 56,17, fuel 56,53, result 116,35 (value-swaps.md, "FurnaceScreen"). Its title centred at y 6 and the recipe-book
    // button at leftPos + 20, 34 below topPos are provisional (minecraft.wiki screenshots, no capture yet).
    static constexpr int32 FurnaceTitleY = 6;
    static constexpr int32 FurnaceRecipeButtonX = 20;
    static constexpr int32 FurnaceRecipeButtonY = 34;
    // The flame and the arrow (value report survival.r3.sprites, SURVIVAL-FINDINGS F 3238-3256): the flame 14 wide at
    // x 56 with its lower edge at y 50, hidden while unlit; the arrow at 79,34, 16 high.
    static constexpr int32 FlameX = 56;
    static constexpr int32 FlameBottom = 50;
    static constexpr int32 FlameSize = 14;
    static constexpr int32 ArrowX = 79;
    static constexpr int32 ArrowY = 34;
    static constexpr int32 ArrowWidth = 24;
    static constexpr int32 ArrowHeight = 16;
    // The flame's height: 0 unlit or at f 0, else ceil(f x 13) + 1 (5/8/11/14 at 25/50/75/100 %); the arrow's width
    // ceil(f x 24) (6/12/18/24).
    static int32 GetFlameHeight(bool bLit, float LitFraction);
    static int32 GetArrowWidth(float CookFraction);

    // A menu slot's 16 x 16 item area (top-left), measured from inventory.png's slot frames; for the crafting table
    // the value report's slot positions (value-swaps.md, "Crafting table").
    static FIntPoint GetSlotPosition(int32 Slot);
    static FIntPoint GetSlotPosition(EFoundationMenuKind Kind, int32 Slot);
    static FIntPoint GetRecipeButton(EFoundationMenuKind Kind);
    FIntPoint GetPosition(int32 Slot) const { return GetSlotPosition(Kind, Slot); }
    // The menu's slot count.
    int32 GetSlots() const { return FFoundationMenuLayout::Get(Kind).Slots; }
    // Whether a slot is drawn and hit: without the full inventory the main and armor slots are not slots.
    bool IsShown(int32 Slot) const;
    // The menu slot under Point; FFoundationContainerMenu::Outside outside the window; INDEX_NONE inside it but on no
    // slot. Without the full inventory (bFullInventory off) the main and armor slots are not slots.
    int32 HitTest(FIntPoint Point) const;
    // Every slot, the crafting grid and its result included (survival step 6), and outside the window.
    static bool IsClickable(int32 Slot) { return Slot == FFoundationContainerMenu::Outside || Slot >= 0; }
    static bool IsInRecipeButton(FIntPoint Point) { return IsInRecipeButton(EFoundationMenuKind::Inventory, Point); }
    static bool IsInRecipeButton(EFoundationMenuKind Kind, FIntPoint Point);

    // Button 0 left, 1 right, 2 middle; Time in seconds from any fixed clock.
    TArray<FHudContainerClick> MouseDown(
        FIntPoint Point, int32 Button, bool bShift, const FHudContainerView& View, double Time);
    TArray<FHudContainerClick> MouseMove(FIntPoint Point, const FHudContainerView& View);
    TArray<FHudContainerClick> MouseUp(FIntPoint Point, int32 Button, const FHudContainerView& View);
    // A number key (Button 0-8) or F (40) over a slot with an empty cursor: SWAP with that hotbar slot or the offhand.
    TArray<FHudContainerClick> PressSwap(FIntPoint Point, int32 Button, const FHudContainerView& View) const;
    // Q over a slot with an empty cursor: THROW one, with control the whole stack.
    TArray<FHudContainerClick> PressDrop(FIntPoint Point, bool bControl, const FHudContainerView& View) const;
    // Forgets a drag and the last click (the screen closed, or the mouse capture lost to another window).
    void Reset();
    // Forgets a drag only, keeping the last click for a double-click (the screen let the capture go itself).
    void ResetDrag();

    bool IsDragging() const { return DragButton != INDEX_NONE; }
    const TArray<int32>& GetDragSlots() const { return DragSlots; }
    // A drag over more than one slot shows its outcome before the release: each slot's stack after the split and
    // what stays on the cursor. Slots holds the dragged slots only.
    void GetDragPreview(
        const FHudContainerView& View, TMap<int32, FHudCreativeStack>& Slots, FHudCreativeStack& Carried) const;

    // The items' stack sizes and equipment slots (the drag only adds slots that can take the stack).
    FHudCreativeItems Items;
    // The items a furnace's fuel slot takes (fuel and the empty bucket, FFoundationContainerMenu::MayPlace).
    TSet<FName> FuelItems;
    bool bFullInventory = true;
    // The player is in creative: a middle press with a stack starts a clone drag (a full stack in each slot).
    bool bCreative = false;
    EFoundationMenuKind Kind = EFoundationMenuKind::Inventory;

private:
    // The slot that only gives (the crafting result, a furnace's result), or INDEX_NONE.
    int32 GetResultSlot() const;
    int32 GetMaxStack(FName Item) const;
    // What Slot holds of Item: the slot's own cap (FFoundationMenuLayout::GetSlotLimit), then the stack size.
    int32 GetSlotMax(int32 Slot, FName Item) const;
    // The menu's rules for a drag slot: the slot takes the item (armor by equipment) and is empty or holds the same
    // item with room.
    bool CanDragTo(int32 Slot, const FHudContainerView& View) const;

    int32 DragButton = INDEX_NONE;
    int32 PressSlot = INDEX_NONE;
    TArray<int32> DragSlots;
    bool bDoubleClick = false;
    bool bShiftDoubleClick = false;
    int32 LastSlot = INDEX_NONE;
    // The last press's button: a double-click is two presses of the same one (Codex review 29, finding 1).
    int32 LastButton = INDEX_NONE;
    double LastTime = -1000;
    // The stack (item and damage) of the last shift-click, for a shift double-click; a plain press forgets it.
    FHudCreativeStack LastQuickMoved;
};
