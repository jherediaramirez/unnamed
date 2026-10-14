#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationItems.h"

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
};

// One container screen's slots and click rules (vanilla's AbstractContainerMenu.clicked), UObject-free, over an
// inventory the caller owns. The cursor stack and a drag live on the inventory (Carried, QuickCraft). Slot numbers
// are the menu's own; container 0 is the player's (vanilla's InventoryMenu): 0 the crafting result and 1-4 its grid
// (survival step 6; refused until then), 5-8 the armor (head to feet), 9-35 the main grid, 36-44 the hotbar,
// 45 the offhand. A moved whole stack keeps its identity (instance id); a split part is a new instance.
class UNNAMED_API FFoundationContainerMenu
{
public:
    static constexpr int32 PlayerContainer = 0;
    static constexpr int32 PlayerMenuSlots = 46;
    // vanilla's slot number for a click outside the window.
    static constexpr int32 Outside = -999;
    static constexpr int32 FirstArmor = 5;
    static constexpr int32 FirstMain = 9;
    static constexpr int32 FirstHotbar = 36;
    static constexpr int32 OffhandSlot = 45;

    // The player's menu over Inventory. bCreative allows CLONE and middle drags.
    FFoundationContainerMenu(FFoundationInventory& Inventory, const FFoundationContainerRules& Rules, bool bCreative);

    // A menu slot's Inventory number (0-40), or INDEX_NONE for a slot this menu does not have.
    static int32 ToInventorySlot(int32 MenuSlot);
    static int32 ToMenuSlot(int32 InventorySlot);

    // Vanilla's QUICK_CRAFT button: the phase (0 start, 1 add a slot, 2 end) and the drag type (0 left, 1 right,
    // 2 middle).
    static int32 QuickCraftButton(int32 Phase, int32 Type) { return Phase | Type << 2; }

    FFoundationClickResult Click(int32 Slot, int32 Button, EFoundationClickType Type);

private:
    FFoundationItemInstance* GetSlot(int32 MenuSlot) const;
    bool MayPlace(int32 MenuSlot, const FFoundationItemInstance& Stack) const;
    int32 GetSlotMax(int32 MenuSlot, const FFoundationItemInstance& Stack) const;
    int32 GetMaxStack(const FFoundationItemInstance& Stack) const;
    // A part of a stack as a new instance with its damage (vanilla's ItemStack.split).
    FFoundationItemInstance Split(FFoundationItemInstance& Stack, int32 Count);
    // Adds Source into the slots in [Begin, End), merging first, then empty slots (vanilla's moveItemStackTo).
    void MoveInto(FFoundationItemInstance& Source, int32 Begin, int32 End);
    void Pickup(int32 Slot, int32 Button, FFoundationClickResult& Result);
    void QuickMove(int32 Slot);
    void SwapWith(int32 Slot, int32 Button);
    void QuickCraft(int32 Slot, int32 Button);
    void PickupAll(int32 Slot, int32 Button);
    bool CanQuickReplace(int32 MenuSlot, const FFoundationItemInstance& Stack) const;

    FFoundationInventory& Inventory;
    const FFoundationContainerRules& Rules;
    bool bCreative = false;
};
