#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationItems.h"
#include "Foundation/FoundationRecipes.h"

// Vanilla's container click types, as its container click carries them (minecraft.wiki "Inventory", "Java Edition
// protocol"): PICKUP left/right, QUICK_MOVE shift, SWAP number keys and F, CLONE middle (creative), THROW Q over a
// slot, QUICK_CRAFT a drag, PICKUP_ALL a double-click.
enum class EFoundationClickType : uint8
{
    Pickup,
    QuickMove,
    Swap,
    Clone,
    Throw,
    QuickCraft,
    PickupAll
};

UNNAMED_API const TCHAR* LexToString(EFoundationClickType Type);

namespace FoundationInventory
{
    // foundation.Inventory.Full: 1 (the default) the full inventory, 36 + 4 armor + offhand; 0 the lab inventory,
    // the hotbar and the offhand.
    UNNAMED_API bool IsFull();
}

// What a click did.
struct FFoundationClickResult
{
    // A slot the menu does not have, or a click type the menu refuses (CLONE outside creative is a no-op instead).
    bool bValid = true;
    // Any slot, the cursor or the drag changed.
    bool bChanged = false;
    // A stack the click throws into the world (THROW, a PICKUP outside the window). The menu has already taken it.
    FFoundationItemInstance Thrown;
};

// The item facts a click needs, by item index.
struct FFoundationContainerRules
{
    TFunction<int32(int32 Item)> GetMaxStack;
    TFunction<EFoundationEquipmentSlot(int32 Item)> GetEquipment;
    // What a crafting grid makes (survival step 6): its item indices row-major, INDEX_NONE for an empty slot. Unset,
    // the result slot stays empty.
    TFunction<FFoundationRecipeResult(TConstArrayView<int32> Grid, int32 Width, int32 Height)> Craft;
    // foundation.Crafting: off, the crafting grid and its result are no slots (step 5b's menu); what a grid already
    // holds still comes back on close (Codex review 10, finding 1).
    bool bCrafting = true;
    // A furnace's (T1 step 8a): what burns (its fuel slot takes these and the empty bucket) and what it cooks (a
    // shift-click sends these to its input first). Unset, nothing.
    TFunction<bool(int32 Item)> IsFuel;
    TFunction<bool(int32 Item)> IsCookable;
    int32 Bucket = INDEX_NONE;
};

// The menus a container screen can show (survival step 7: the crafting table).
enum class EFoundationMenuKind : uint8
{
    // The player's own menu (vanilla's InventoryMenu): result, 2x2, armor, main, hotbar, offhand.
    Inventory,
    // A crafting table's (vanilla's CraftingMenu): result, 3x3, main, hotbar; no armor or offhand.
    Crafting,
    // A furnace's (vanilla's FurnaceMenu, T1 step 8a): input, fuel, result, main, hotbar.
    Furnace,
    // A smoker's and a blast furnace's (vanilla's SmokerMenu and BlastFurnaceMenu, T1 step 8b): the furnace's layout.
    Smoker,
    BlastFurnace,
    // A barrel's (vanilla's ChestMenu with 3 rows, T1 step 9): its 27 slots, main, hotbar.
    Barrel,
    // A chest's (T1 step 18-chest): the barrel's layout; a double chest's (ChestMenu with 6 rows): its 54 slots, the
    // type=right half's then the type=left half's, main, hotbar.
    Chest,
    LargeChest
};

// The furnace, smoker and blast furnace menus share one layout and one block entity kind.
inline bool IsFurnaceMenu(EFoundationMenuKind Kind)
{
    return Kind == EFoundationMenuKind::Furnace || Kind == EFoundationMenuKind::Smoker ||
        Kind == EFoundationMenuKind::BlastFurnace;
}

// The chest menus (T1 step 18-chest).
inline bool IsChestMenu(EFoundationMenuKind Kind)
{
    return Kind == EFoundationMenuKind::Chest || Kind == EFoundationMenuKind::LargeChest;
}

// The menus whose block shows its viewers (an open barrel, a chest's sounds; T1 steps 9, 18-chest).
inline bool HasViewers(EFoundationMenuKind Kind)
{
    return Kind == EFoundationMenuKind::Barrel || IsChestMenu(Kind);
}

// The menus whose container slots are a block entity's (the authority's store, T1 steps 8a, 9, 18-chest).
inline bool UsesBlockEntity(EFoundationMenuKind Kind)
{
    return IsFurnaceMenu(Kind) || HasViewers(Kind);
}

// A menu's slot numbering: one row per kind (furnaces and the barrel add theirs). Every range is [First, Next);
// INDEX_NONE marks a range the menu does not have.
struct FFoundationMenuLayout
{
    int32 Slots = 0;
    int32 FirstCraft = 1;
    int32 CraftWidth = 0;
    int32 CraftHeight = 0;
    int32 FirstArmor = INDEX_NONE;
    int32 FirstMain = INDEX_NONE;
    int32 FirstHotbar = INDEX_NONE;
    int32 Offhand = INDEX_NONE;
    // A container's own slots (a furnace's input, fuel and result; the caller's Grid): [FirstContainer, + Container
    // Slots). TakeOnly only gives (the furnace's result); FuelSlot takes only fuel or an empty bucket.
    int32 FirstContainer = INDEX_NONE;
    int32 ContainerSlots = 0;
    int32 TakeOnly = INDEX_NONE;
    int32 FuelSlot = INDEX_NONE;

    // Slot 0 is a crafting result (the inventory and the crafting table).
    bool HasCraftResult() const { return CraftWidth > 0; }
    bool IsContainer(int32 Slot) const
    {
        return FirstContainer != INDEX_NONE && Slot >= FirstContainer && Slot < FirstContainer + ContainerSlots;
    }
    int32 GetCraftEnd() const { return FirstCraft + CraftWidth * CraftHeight; }
    int32 GetMainEnd() const { return FirstHotbar; }
    int32 GetHotbarEnd() const { return FirstHotbar + FFoundationInventory::HotbarSlots; }
    bool IsCraft(int32 Slot) const { return Slot >= FirstCraft && Slot < GetCraftEnd(); }
    bool IsArmor(int32 Slot) const
    {
        return FirstArmor != INDEX_NONE && Slot >= FirstArmor && Slot < FirstArmor + FFoundationInventory::ArmorSlots;
    }
    bool IsMain(int32 Slot) const { return Slot >= FirstMain && Slot < GetMainEnd(); }
    bool IsHotbar(int32 Slot) const { return Slot >= FirstHotbar && Slot < GetHotbarEnd(); }
    bool IsOffhand(int32 Slot) const { return Offhand != INDEX_NONE && Slot == Offhand; }
    // A slot's own cap before the item's stack size: an armor slot holds one item, as a furnace's fuel slot holds one
    // empty bucket (bBucket: the stack is FuelBucketItem). The authority's GetSlotMax and the screen's drag preview
    // share it (Codex review 22, finding 3).
    static constexpr const TCHAR* FuelBucketItem = TEXT("bucket");
    int32 GetSlotLimit(int32 Slot, bool bBucket) const
    {
        return IsArmor(Slot) || (FuelSlot != INDEX_NONE && Slot == FuelSlot && bBucket) ? 1 : 64;
    }
    // A menu slot's Inventory number (0-40), or INDEX_NONE for the result, the grid or a slot the menu lacks.
    int32 ToInventorySlot(int32 MenuSlot) const;
    int32 ToMenuSlot(int32 InventorySlot) const;

    static const FFoundationMenuLayout& Get(EFoundationMenuKind Kind);
};

// One container screen's slots and click rules (vanilla's AbstractContainerMenu.clicked), UObject-free, over an
// inventory the caller owns. The cursor stack and a drag live on the inventory (Carried, QuickCraft). Slot numbers
// are the menu's own; container 0 is the player's (vanilla's InventoryMenu): 0 the crafting result and 1-4 its 2x2
// grid (survival step 6; FFoundationInventory::Craft), 5-8 the armor (head to feet), 9-35 the main grid, 36-44 the
// hotbar, 45 the offhand. A moved whole stack keeps its identity (instance id); a split part is a new instance. The
// result slot holds what the grid makes now (Rules.Craft); taking it crafts once, using one item of every grid slot.
// A crafting table's menu (EFoundationMenuKind::Crafting, survival step 7) is 0 the result, 1-9 its 3x3 grid (the
// caller's Grid, the screen's own), 10-36 the main grid, 37-45 the hotbar. A furnace's (EFoundationMenuKind::Furnace,
// T1 step 8a) is 0 the input, 1 the fuel, 2 the result (the caller's Grid: its block entity's slots), 3-29 the main
// grid, 30-38 the hotbar; its result only gives, and nothing is crafted. A barrel's (EFoundationMenuKind::Barrel, T1
// step 9) is 0-26 its block entity's slots, 27-53 the main grid, 54-62 the hotbar; a chest's (Chest, 18-chest) the
// same. A double chest's (LargeChest) is 0-53 its two halves' slots, 54-80 the main grid, 81-89 the hotbar.
class UNNAMED_API FFoundationContainerMenu
{
public:
    static constexpr int32 PlayerContainer = 0;
    static constexpr int32 PlayerMenuSlots = 46;
    // vanilla's slot number for a click outside the window.
    static constexpr int32 Outside = -999;
    static constexpr int32 ResultSlot = 0;
    static constexpr int32 FirstCraft = 1;
    static constexpr int32 CraftWidth = 2;
    static constexpr int32 CraftHeight = 2;
    static constexpr int32 FirstArmor = 5;
    static constexpr int32 FirstMain = 9;
    static constexpr int32 FirstHotbar = 36;
    static constexpr int32 OffhandSlot = 45;

    // The player's menu over Inventory. bCreative allows CLONE and middle drags.
    FFoundationContainerMenu(FFoundationInventory& Inventory, const FFoundationContainerRules& Rules, bool bCreative);
    // Another menu over Inventory; Grid is its crafting grid (CraftWidth x CraftHeight slots) or its container's slots
    // (ContainerSlots), the caller's.
    FFoundationContainerMenu(FFoundationInventory& Inventory, const FFoundationContainerRules& Rules, bool bCreative,
        EFoundationMenuKind Kind, TArrayView<FFoundationItemInstance> Grid);

    // The player's menu: a menu slot's Inventory number (0-40), or INDEX_NONE for a slot that is not one (the
    // crafting slots 0-4, or a slot this menu does not have).
    static int32 ToInventorySlot(int32 MenuSlot);
    static int32 ToMenuSlot(int32 InventorySlot);

    const FFoundationMenuLayout& GetLayout() const { return Layout; }

    // Vanilla's QUICK_CRAFT button: the phase (0 start, 1 add a slot, 2 end) and the drag type (0 left, 1 right,
    // 2 middle).
    static int32 QuickCraftButton(int32 Phase, int32 Type) { return Phase | Type << 2; }

    FFoundationClickResult Click(int32 Slot, int32 Button, EFoundationClickType Type);
    // A menu slot's stack (the result slot: what the grid makes now), or null for a slot this menu does not have.
    const FFoundationItemInstance* GetMenuStack(int32 MenuSlot) const { return GetSlot(MenuSlot); }

private:
    FFoundationItemInstance* GetSlot(int32 MenuSlot) const;
    // Recomputes the result slot from the grid (Rules.Craft); a new instance each time it changes.
    void UpdateResult();
    // Takes the result once: uses one item of every grid slot, then recomputes the result. Returns the crafted stack.
    FFoundationItemInstance TakeResult();
    // The result slot's clicks (vanilla's ResultSlot: it takes and never places).
    void ClickResult(int32 Button, EFoundationClickType Type, FFoundationClickResult& Result);
    bool MayPlace(int32 MenuSlot, const FFoundationItemInstance& Stack) const;
    int32 GetSlotMax(int32 MenuSlot, const FFoundationItemInstance& Stack) const;
    int32 GetMaxStack(const FFoundationItemInstance& Stack) const;
    // A part of a stack as a new instance with its damage (vanilla's ItemStack.split).
    FFoundationItemInstance Split(FFoundationItemInstance& Stack, int32 Count);
    // Adds Source into the slots in [Begin, End), merging first, then empty slots (vanilla's moveItemStackTo); bReverse
    // walks from the last slot.
    void MoveInto(FFoundationItemInstance& Source, int32 Begin, int32 End, bool bReverse = false);
    // How many of Stack the slots in [Begin, End) could take (MoveInto's room).
    int32 GetRoom(const FFoundationItemInstance& Stack, int32 Begin, int32 End) const;
    void Pickup(int32 Slot, int32 Button, FFoundationClickResult& Result);
    void QuickMove(int32 Slot);
    void SwapWith(int32 Slot, int32 Button);
    void QuickCraft(int32 Slot, int32 Button);
    void PickupAll(int32 Slot, int32 Button);
    bool CanQuickReplace(int32 MenuSlot, const FFoundationItemInstance& Stack) const;

    FFoundationInventory& Inventory;
    const FFoundationContainerRules& Rules;
    bool bCreative = false;
    const FFoundationMenuLayout& Layout;
    // The crafting grid's slots, row-major (the inventory's 2x2 or the caller's).
    TArrayView<FFoundationItemInstance> Grid;
    // The result slot's stack; the menu's own, never the inventory's.
    mutable FFoundationItemInstance ResultStack;
};
