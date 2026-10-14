#include "Foundation/FoundationContainer.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarInventoryFull(TEXT("foundation.Inventory.Full"), 1,
        TEXT("1: the full player inventory, the hotbar, 27 main slots, 4 armor slots and the offhand (survival step "
             "5a). 0: the lab inventory, the hotbar and the offhand."));

    bool SameStack(const FFoundationItemInstance& A, const FFoundationItemInstance& B)
    {
        return A.InstanceId == B.InstanceId && A.Item == B.Item && A.Count == B.Count && A.Damage == B.Damage;
    }

    // Vanilla's isSameItemSameComponents for the stacks here: the item and its damage.
    bool SameItem(const FFoundationItemInstance& A, const FFoundationItemInstance& B)
    {
        return !A.IsEmpty() && !B.IsEmpty() && A.Item == B.Item && A.Damage == B.Damage;
    }
}

const TCHAR* LexToString(EFoundationClickType Type)
{
    static const TCHAR* Names[] = {TEXT("pickup"), TEXT("quick-move"), TEXT("swap"), TEXT("clone"), TEXT("throw"),
        TEXT("quick-craft"), TEXT("pickup-all")};
    return Names[FMath::Min(int32(Type), int32(UE_ARRAY_COUNT(Names)) - 1)];
}

bool FoundationInventory::IsFull()
{
    return CVarInventoryFull.GetValueOnAnyThread() != 0;
}

FFoundationContainerMenu::FFoundationContainerMenu(
    FFoundationInventory& InInventory, const FFoundationContainerRules& InRules, bool bInCreative)
    : Inventory(InInventory), Rules(InRules), bCreative(bInCreative)
{
}

int32 FFoundationContainerMenu::ToInventorySlot(int32 MenuSlot)
{
    if (MenuSlot >= FirstArmor && MenuSlot < FirstMain)
        return FFoundationInventory::FirstArmor + FFoundationInventory::ArmorSlots - 1 - (MenuSlot - FirstArmor);
    if (MenuSlot >= FirstMain && MenuSlot < FirstHotbar) return MenuSlot;
    if (MenuSlot >= FirstHotbar && MenuSlot < OffhandSlot) return MenuSlot - FirstHotbar;
    if (MenuSlot == OffhandSlot) return FFoundationInventory::OffhandSlot;
    return INDEX_NONE;
}

int32 FFoundationContainerMenu::ToMenuSlot(int32 InventorySlot)
{
    if (InventorySlot >= 0 && InventorySlot < FFoundationInventory::HotbarSlots) return FirstHotbar + InventorySlot;
    if (InventorySlot >= FFoundationInventory::FirstMain && InventorySlot < FFoundationInventory::FirstArmor)
        return InventorySlot;
    if (InventorySlot >= FFoundationInventory::FirstArmor && InventorySlot < FFoundationInventory::OffhandSlot)
        return FirstArmor + FFoundationInventory::ArmorSlots - 1 - (InventorySlot - FFoundationInventory::FirstArmor);
    if (InventorySlot == FFoundationInventory::OffhandSlot) return OffhandSlot;
    return INDEX_NONE;
}

FFoundationItemInstance* FFoundationContainerMenu::GetSlot(int32 MenuSlot) const
{
    const int32 Slot = ToInventorySlot(MenuSlot);
    return Slot == INDEX_NONE ? nullptr : Inventory.GetSlot(Slot);
}

bool FFoundationContainerMenu::MayPlace(int32 MenuSlot, const FFoundationItemInstance& Stack) const
{
    if (MenuSlot < FirstArmor || MenuSlot >= FirstMain) return true;
    // Head, chest, legs, feet; only an item equippable there (ArmorSlot.mayPlace).
    static const EFoundationEquipmentSlot Wanted[] = {EFoundationEquipmentSlot::Head, EFoundationEquipmentSlot::Chest,
        EFoundationEquipmentSlot::Legs, EFoundationEquipmentSlot::Feet};
    return !Stack.IsEmpty() && Rules.GetEquipment && Rules.GetEquipment(Stack.Item) == Wanted[MenuSlot - FirstArmor];
}

int32 FFoundationContainerMenu::GetMaxStack(const FFoundationItemInstance& Stack) const
{
    return Stack.IsEmpty() || !Rules.GetMaxStack ? 64 : FMath::Max(1, Rules.GetMaxStack(Stack.Item));
}

int32 FFoundationContainerMenu::GetSlotMax(int32 MenuSlot, const FFoundationItemInstance& Stack) const
{
    // An armor slot holds one item.
    const int32 SlotMax = MenuSlot >= FirstArmor && MenuSlot < FirstMain ? 1 : 64;
    return FMath::Min(SlotMax, GetMaxStack(Stack));
}

FFoundationItemInstance FFoundationContainerMenu::Split(FFoundationItemInstance& Stack, int32 Count)
{
    Count = FMath::Min(Count, Stack.Count);
    if (Count <= 0) return {};
    if (Count == Stack.Count)
    {
        // The whole stack moves with its identity.
        const FFoundationItemInstance Whole = Stack;
        Stack = {};
        return Whole;
    }
    FFoundationItemInstance Part = Inventory.MakeStack(Stack.Item, Count);
    Part.Damage = Stack.Damage;
    Stack.Count -= Count;
    return Part;
}

bool FFoundationContainerMenu::CanQuickReplace(int32 MenuSlot, const FFoundationItemInstance& Stack) const
{
    const FFoundationItemInstance* Target = GetSlot(MenuSlot);
    return Target && (Target->IsEmpty() || (SameItem(*Target, Stack) && Target->Count <= GetMaxStack(Stack)));
}

void FFoundationContainerMenu::MoveInto(FFoundationItemInstance& Source, int32 Begin, int32 End)
{
    // Merges first, then empty slots, each in slot order.
    for (int32 Slot = Begin; Slot < End && !Source.IsEmpty(); ++Slot)
        if (FFoundationItemInstance* Target = GetSlot(Slot);
            Target && SameItem(*Target, Source) && MayPlace(Slot, Source))
        {
            const int32 Move = FMath::Min(Source.Count, GetSlotMax(Slot, Source) - Target->Count);
            if (Move <= 0) continue;
            Target->Count += Move;
            Source.Count -= Move;
            if (Source.Count <= 0) Source = {};
        }
    for (int32 Slot = Begin; Slot < End && !Source.IsEmpty(); ++Slot)
        if (FFoundationItemInstance* Target = GetSlot(Slot); Target && Target->IsEmpty() && MayPlace(Slot, Source))
            *Target = Split(Source, GetSlotMax(Slot, Source));
}

void FFoundationContainerMenu::Pickup(int32 Slot, int32 Button, FFoundationClickResult& Result)
{
    FFoundationItemInstance& Carried = Inventory.Carried;
    if (Slot == Outside)
    {
        // A click outside the window throws the cursor stack (left) or one of it (right).
        if (!Carried.IsEmpty()) Result.Thrown = Split(Carried, Button == 0 ? Carried.Count : 1);
        return;
    }
    FFoundationItemInstance& Stack = *GetSlot(Slot);
    if (Stack.IsEmpty())
    {
        // Left places all that fits, right one.
        if (!Carried.IsEmpty() && MayPlace(Slot, Carried))
            Stack = Split(Carried, FMath::Min(Button == 0 ? Carried.Count : 1, GetSlotMax(Slot, Carried)));
    }
    else if (Carried.IsEmpty())
        // Left takes the stack, right the larger half.
        Carried = Split(Stack, Button == 0 ? Stack.Count : (Stack.Count + 1) / 2);
    else if (MayPlace(Slot, Carried))
    {
        if (SameItem(Stack, Carried))
        {
            const int32 Move = FMath::Min(Button == 0 ? Carried.Count : 1, GetSlotMax(Slot, Carried) - Stack.Count);
            if (Move > 0)
            {
                Stack.Count += Move;
                Carried.Count -= Move;
                if (Carried.Count <= 0) Carried = {};
            }
        }
        else if (Carried.Count <= GetSlotMax(Slot, Carried))
            Swap(Stack, Carried);
    }
    else if (SameItem(Stack, Carried))
    {
        // A slot that refuses the cursor's item still gives its own to the cursor.
        const int32 Move = FMath::Min(Stack.Count, GetMaxStack(Carried) - Carried.Count);
        if (Move > 0)
        {
            Carried.Count += Move;
            Stack.Count -= Move;
            if (Stack.Count <= 0) Stack = {};
        }
    }
}

void FFoundationContainerMenu::QuickMove(int32 Slot)
{
    FFoundationItemInstance& Stack = *GetSlot(Slot);
    if (Stack.IsEmpty()) return;
    const EFoundationEquipmentSlot Equipment =
        Rules.GetEquipment ? Rules.GetEquipment(Stack.Item) : EFoundationEquipmentSlot::None;
    // vanilla's InventoryMenu routing (minecraft.wiki "Inventory"): armor goes back to the main grid and hotbar; a
    // wearable item goes to its empty armor slot, an offhand item to an empty offhand; otherwise the hotbar and the
    // main grid trade, and the offhand goes to the main grid and hotbar.
    const int32 ArmorSlot = Equipment == EFoundationEquipmentSlot::Head ? FirstArmor
        : Equipment == EFoundationEquipmentSlot::Chest                  ? FirstArmor + 1
        : Equipment == EFoundationEquipmentSlot::Legs                   ? FirstArmor + 2
        : Equipment == EFoundationEquipmentSlot::Feet                   ? FirstArmor + 3
                                                                        : INDEX_NONE;
    if (Slot >= FirstArmor && Slot < FirstMain)
        MoveInto(Stack, FirstMain, OffhandSlot);
    else if (ArmorSlot != INDEX_NONE && GetSlot(ArmorSlot) && GetSlot(ArmorSlot)->IsEmpty())
        MoveInto(Stack, ArmorSlot, ArmorSlot + 1);
    else if (Equipment == EFoundationEquipmentSlot::Offhand && GetSlot(OffhandSlot)->IsEmpty())
        MoveInto(Stack, OffhandSlot, OffhandSlot + 1);
    else if (Slot >= FirstMain && Slot < FirstHotbar)
        MoveInto(Stack, FirstHotbar, OffhandSlot);
    else if (Slot >= FirstHotbar && Slot < OffhandSlot)
        MoveInto(Stack, FirstMain, FirstHotbar);
    else if (Slot == OffhandSlot)
        MoveInto(Stack, FirstMain, OffhandSlot);
}

void FFoundationContainerMenu::SwapWith(int32 Slot, int32 Button)
{
    // Number keys 1-9 are buttons 0-8 (the hotbar), F is 40 (the offhand).
    FFoundationItemInstance& Stack = *GetSlot(Slot);
    FFoundationItemInstance& Other = *Inventory.GetSlot(Button);
    if (&Stack == &Other || (Stack.IsEmpty() && Other.IsEmpty())) return;
    if (Other.IsEmpty())
        Swap(Stack, Other);
    else if (MayPlace(Slot, Other))
    {
        if (Stack.IsEmpty())
            Stack = Split(Other, GetSlotMax(Slot, Other));
        else if (Other.Count <= GetSlotMax(Slot, Other))
            Swap(Stack, Other);
    }
}

void FFoundationContainerMenu::QuickCraft(int32 Slot, int32 Button)
{
    // A drag (minecraft.wiki "Inventory"): a start, a slot at a time, an end. Left splits the cursor stack evenly
    // and keeps the remainder, right places one in each, middle (creative) a full stack in each.
    FFoundationQuickCraft& Drag = Inventory.QuickCraft;
    FFoundationItemInstance& Carried = Inventory.Carried;
    const int32 Phase = Button & 3, Type = Button >> 2 & 3;
    if (!(Drag.Status == Phase || (Drag.Status == 1 && Phase == 2)) || Carried.IsEmpty())
    {
        Drag = {};
        return;
    }
    if (Phase == 0)
    {
        Drag = {};
        if (Type == 0 || Type == 1 || (Type == 2 && bCreative))
        {
            Drag.Status = 1;
            Drag.Type = Type;
        }
        return;
    }
    if (Phase == 1)
    {
        if (Slot != Outside && CanQuickReplace(Slot, Carried) && MayPlace(Slot, Carried) &&
            (Drag.Type == 2 || Carried.Count > Drag.Slots.Num()))
            Drag.Slots.AddUnique(Slot);
        return;
    }
    const FFoundationQuickCraft Done = Drag;
    Drag = {};
    if (Done.Slots.Num() == 1)
    {
        // One slot is a plain click.
        FFoundationClickResult Ignored;
        Pickup(Done.Slots[0], Done.Type == 0 ? 0 : 1, Ignored);
        return;
    }
    if (Done.Slots.IsEmpty()) return;
    const int32 Each = Done.Type == 0 ? Carried.Count / Done.Slots.Num() : Done.Type == 1 ? 1 : GetMaxStack(Carried);
    int32 Remaining = Carried.Count;
    for (const int32 DragSlot : Done.Slots)
    {
        FFoundationItemInstance* Target = GetSlot(DragSlot);
        if (!Target || !CanQuickReplace(DragSlot, Carried) || !MayPlace(DragSlot, Carried) ||
            (Done.Type != 2 && Carried.Count < Done.Slots.Num()))
            continue;
        const int32 Existing = Target->IsEmpty() ? 0 : Target->Count;
        const int32 Place = FMath::Min(Each + Existing, GetSlotMax(DragSlot, Carried));
        if (Place <= Existing) continue;
        Remaining -= Place - Existing;
        if (Target->IsEmpty())
        {
            *Target = Inventory.MakeStack(Carried.Item, Place);
            Target->Damage = Carried.Damage;
        }
        else
            Target->Count = Place;
    }
    // A creative middle drag copies; the others spend the cursor stack.
    if (Done.Type != 2)
    {
        Carried.Count = Remaining;
        if (Carried.Count <= 0) Carried = {};
    }
}

void FFoundationContainerMenu::PickupAll(int32 Slot, int32 Button)
{
    // A double-click collects matching stacks into the cursor up to a full stack: non-full stacks first, then full
    // ones, from the first slot (left) or the last.
    FFoundationItemInstance& Carried = Inventory.Carried;
    const FFoundationItemInstance& Clicked = *GetSlot(Slot);
    if (Carried.IsEmpty() || !Clicked.IsEmpty()) return;
    const int32 Max = GetMaxStack(Carried);
    for (int32 Pass = 0; Pass < 2; ++Pass)
        for (int32 Index = 0; Index < PlayerMenuSlots && Carried.Count < Max; ++Index)
        {
            const int32 Menu = Button == 0 ? Index : PlayerMenuSlots - 1 - Index;
            FFoundationItemInstance* Stack = GetSlot(Menu);
            if (!Stack || !SameItem(*Stack, Carried) || (Pass == 0 && Stack->Count >= GetMaxStack(*Stack))) continue;
            const int32 Move = FMath::Min(Stack->Count, Max - Carried.Count);
            Carried.Count += Move;
            Stack->Count -= Move;
            if (Stack->Count <= 0) *Stack = {};
        }
}

FFoundationClickResult FFoundationContainerMenu::Click(int32 Slot, int32 Button, EFoundationClickType Type)
{
    FFoundationClickResult Result;
    if (Slot != Outside && !GetSlot(Slot))
    {
        Result.bValid = false;
        return Result;
    }
    // Before, to tell what changed.
    TArray<FFoundationItemInstance, TInlineAllocator<PlayerMenuSlots>> Before;
    for (int32 Index = 0; Index < PlayerMenuSlots; ++Index)
        Before.Add(GetSlot(Index) ? *GetSlot(Index) : FFoundationItemInstance());
    const FFoundationItemInstance CarriedBefore = Inventory.Carried;
    const FFoundationQuickCraft DragBefore = Inventory.QuickCraft;

    if (Type == EFoundationClickType::QuickCraft)
        QuickCraft(Slot, Button);
    else if (Inventory.QuickCraft.Status != 0)
        // Any other click ends a drag and does nothing else.
        Inventory.QuickCraft = {};
    else if (Type == EFoundationClickType::Pickup)
        Pickup(Slot, Button, Result);
    else if (Slot != Outside)
        switch (Type)
        {
        case EFoundationClickType::QuickMove:
            QuickMove(Slot);
            break;
        case EFoundationClickType::Swap:
            if (Button == FFoundationInventory::OffhandSlot ||
                (Button >= 0 && Button < FFoundationInventory::HotbarSlots))
                SwapWith(Slot, Button);
            else
                Result.bValid = false;
            break;
        case EFoundationClickType::Clone:
            if (bCreative && Inventory.Carried.IsEmpty() && !GetSlot(Slot)->IsEmpty())
            {
                const FFoundationItemInstance& Stack = *GetSlot(Slot);
                Inventory.Carried = Inventory.MakeStack(Stack.Item, GetMaxStack(Stack));
                Inventory.Carried.Damage = Stack.Damage;
            }
            break;
        case EFoundationClickType::Throw:
            // Q over a slot throws one, Ctrl+Q the stack; only with an empty cursor.
            if (Inventory.Carried.IsEmpty() && !GetSlot(Slot)->IsEmpty())
                Result.Thrown = Split(*GetSlot(Slot), Button == 0 ? 1 : GetSlot(Slot)->Count);
            break;
        case EFoundationClickType::PickupAll:
            PickupAll(Slot, Button);
            break;
        default:
            break;
        }

    // Stacks that kept their identity but changed count bump their own revision (their item refs).
    auto Touched = [](FFoundationItemInstance& Now, const FFoundationItemInstance& Then)
    {
        if (Now.IsEmpty() || SameStack(Now, Then)) return false;
        if (Now.InstanceId == Then.InstanceId) ++Now.Revision;
        return true;
    };
    for (int32 Index = 0; Index < PlayerMenuSlots; ++Index)
        if (FFoundationItemInstance* Stack = GetSlot(Index))
            Result.bChanged |= Touched(*Stack, Before[Index]) || (Stack->IsEmpty() && !Before[Index].IsEmpty());
    Result.bChanged |= Touched(Inventory.Carried, CarriedBefore) ||
        (Inventory.Carried.IsEmpty() && !CarriedBefore.IsEmpty()) || !Result.Thrown.IsEmpty() ||
        Inventory.QuickCraft.Status != DragBefore.Status || Inventory.QuickCraft.Slots != DragBefore.Slots;
    if (Result.bChanged) ++Inventory.Revision;
    return Result;
}
