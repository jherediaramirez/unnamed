#include "Foundation/FoundationItems.h"

float FFoundationItemDefinition::GetMiningSpeed(const FGameplayTagContainer& BlockTags) const
{
    // Rules match exact tags, so results never depend on whether parent tags were filled in.
    for (const FFoundationMiningRule& Rule : MiningRules)
        if (Rule.Speed > 0 && BlockTags.HasAnyExact(Rule.BlockTags)) return Rule.Speed;
    return DefaultMiningSpeed;
}

bool FFoundationItemDefinition::IsCorrectForDrops(const FGameplayTagContainer& BlockTags) const
{
    for (const FFoundationMiningRule& Rule : MiningRules)
        if (Rule.bStatesCorrectForDrops && BlockTags.HasAnyExact(Rule.BlockTags)) return Rule.bCorrectForDrops;
    return false;
}

FFoundationInventory::FFoundationInventory()
{
    Hotbar.SetNum(HotbarSlots);
}

FFoundationItemInstance& FFoundationInventory::GetHand(EFoundationHand Hand)
{
    return Hand == EFoundationHand::Off ? Offhand : Hotbar[Selected];
}

const FFoundationItemInstance& FFoundationInventory::GetHand(EFoundationHand Hand) const
{
    return Hand == EFoundationHand::Off ? Offhand : Hotbar[Selected];
}

FFoundationItemInstance FFoundationInventory::MakeStack(int32 Item, int32 Count)
{
    FFoundationItemInstance Stack;
    if (Item == INDEX_NONE || Count <= 0) return Stack;
    Stack.InstanceId = NextInstanceId++;
    Stack.Item = Item;
    Stack.Count = Count;
    Stack.Revision = 1;
    return Stack;
}

void FFoundationInventory::SetSlot(int32 Slot, const FFoundationItemInstance& Stack)
{
    if (!Hotbar.IsValidIndex(Slot)) return;
    Hotbar[Slot] = Stack;
    ++Revision;
}

void FFoundationInventory::Select(int32 Slot)
{
    Selected = FMath::Clamp(Slot, 0, HotbarSlots - 1);
    ++Revision;
}

void FFoundationInventory::SwapHands()
{
    Swap(Hotbar[Selected], Offhand);
    ++Revision;
}

int32 FFoundationInventory::GetSpaceFor(int32 Item, int32 MaxStack) const
{
    int32 Space = 0;
    auto Count = [&](const FFoundationItemInstance& Stack)
    {
        if (Stack.IsEmpty())
            Space += MaxStack;
        else if (Stack.Item == Item)
            Space += FMath::Max(0, MaxStack - Stack.Count);
    };
    for (const FFoundationItemInstance& Stack : Hotbar)
        Count(Stack);
    // The offhand only takes merges, never a new stack.
    if (!Offhand.IsEmpty() && Offhand.Item == Item) Space += FMath::Max(0, MaxStack - Offhand.Count);
    return Space;
}

int32 FFoundationInventory::Add(int32 Item, int32 Count, int32 MaxStack)
{
    int32 Added = 0;
    auto Merge = [&](FFoundationItemInstance& Stack)
    {
        if (Added >= Count || Stack.IsEmpty() || Stack.Item != Item || Stack.Count >= MaxStack) return;
        const int32 Amount = FMath::Min(Count - Added, MaxStack - Stack.Count);
        Stack.Count += Amount;
        Added += Amount;
        Touch(Stack);
    };
    // Same order as the reference inventory: selected stack, offhand, then the hotbar; then free slots.
    Merge(Hotbar[Selected]);
    Merge(Offhand);
    for (FFoundationItemInstance& Stack : Hotbar)
        Merge(Stack);
    for (FFoundationItemInstance& Stack : Hotbar)
        if (Added < Count && Stack.IsEmpty())
        {
            const int32 Amount = FMath::Min(Count - Added, MaxStack);
            Stack = MakeStack(Item, Amount);
            Added += Amount;
            ++Revision;
        }
    return Added;
}

void FFoundationInventory::ConsumeOne(EFoundationHand Hand)
{
    FFoundationItemInstance& Stack = GetHand(Hand);
    if (Stack.IsEmpty()) return;
    if (--Stack.Count <= 0)
    {
        Stack = {};
        ++Revision;
        return;
    }
    Touch(Stack);
}

bool FFoundationInventory::Wear(EFoundationHand Hand, int32 Amount, int32 MaxDurability)
{
    FFoundationItemInstance& Stack = GetHand(Hand);
    if (Stack.IsEmpty() || MaxDurability <= 0 || Amount <= 0) return false;
    Stack.Damage += Amount;
    if (Stack.Damage >= MaxDurability)
    {
        // The last point breaks the tool once; its identity ends with it.
        Stack = {};
        ++Revision;
        return true;
    }
    Touch(Stack);
    return false;
}

void FFoundationInventory::Touch(FFoundationItemInstance& Stack)
{
    ++Stack.Revision;
    ++Revision;
}
