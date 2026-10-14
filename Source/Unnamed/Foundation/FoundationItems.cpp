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
    Main.SetNum(MainSlots);
    Armor.SetNum(ArmorSlots);
}

FFoundationItemInstance& FFoundationInventory::GetHand(EFoundationHand Hand)
{
    return Hand == EFoundationHand::Off ? Offhand : Hotbar[Selected];
}

const FFoundationItemInstance& FFoundationInventory::GetHand(EFoundationHand Hand) const
{
    return Hand == EFoundationHand::Off ? Offhand : Hotbar[Selected];
}

FFoundationItemInstance* FFoundationInventory::GetSlot(int32 Slot)
{
    return const_cast<FFoundationItemInstance*>(static_cast<const FFoundationInventory*>(this)->GetSlot(Slot));
}

const FFoundationItemInstance* FFoundationInventory::GetSlot(int32 Slot) const
{
    if (Slot >= 0 && Slot < HotbarSlots) return &Hotbar[Slot];
    if (Slot == OffhandSlot) return &Offhand;
    if (!bFull) return nullptr;
    if (Slot >= FirstMain && Slot < FirstMain + MainSlots) return &Main[Slot - FirstMain];
    if (Slot >= FirstArmor && Slot < FirstArmor + ArmorSlots) return &Armor[Slot - FirstArmor];
    return nullptr;
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
    FFoundationItemInstance* Target = GetSlot(Slot);
    if (!Target) return;
    *Target = Stack;
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
    if (bFull)
        for (const FFoundationItemInstance& Stack : Main)
            Count(Stack);
    // The offhand only takes merges, never a new stack.
    if (!Offhand.IsEmpty() && Offhand.Item == Item) Space += FMath::Max(0, MaxStack - Offhand.Count);
    return Space;
}

int32 FFoundationInventory::Add(int32 Item, int32 Count, int32 MaxStack, bool bTouchStacks)
{
    int32 Added = 0;
    auto Merge = [&](FFoundationItemInstance& Stack)
    {
        if (Added >= Count || Stack.IsEmpty() || Stack.Item != Item || Stack.Count >= MaxStack) return;
        const int32 Amount = FMath::Min(Count - Added, MaxStack - Stack.Count);
        Stack.Count += Amount;
        Added += Amount;
        if (bTouchStacks)
            Touch(Stack);
        else
            ++Revision;
    };
    auto Fill = [&](FFoundationItemInstance& Stack)
    {
        if (Added >= Count || !Stack.IsEmpty()) return;
        const int32 Amount = FMath::Min(Count - Added, MaxStack);
        Stack = MakeStack(Item, Amount);
        Added += Amount;
        ++Revision;
    };
    // Vanilla's order: the selected stack, the offhand, then slots 0-35 merge; then the first free slot of 0-35.
    Merge(Hotbar[Selected]);
    Merge(Offhand);
    for (FFoundationItemInstance& Stack : Hotbar)
        Merge(Stack);
    if (bFull)
        for (FFoundationItemInstance& Stack : Main)
            Merge(Stack);
    for (FFoundationItemInstance& Stack : Hotbar)
        Fill(Stack);
    if (bFull)
        for (FFoundationItemInstance& Stack : Main)
            Fill(Stack);
    return Added;
}
FFoundationItemInstance FFoundationInventory::Return(FFoundationItemInstance Stack, int32 MaxStack)
{
    auto Merge = [&](FFoundationItemInstance& Into)
    {
        if (Stack.IsEmpty() || Into.IsEmpty() || Into.Item != Stack.Item || Into.Damage != Stack.Damage) return;
        const int32 Amount = FMath::Min(Stack.Count, MaxStack - Into.Count);
        if (Amount <= 0) return;
        Into.Count += Amount;
        Stack.Count -= Amount;
        if (Stack.Count <= 0) Stack = {};
        Touch(Into);
    };
    auto Place = [&](FFoundationItemInstance& Into)
    {
        if (Stack.IsEmpty() || !Into.IsEmpty()) return;
        Into = Stack;
        Stack = {};
        ++Revision;
    };
    Merge(Hotbar[Selected]);
    Merge(Offhand);
    for (FFoundationItemInstance& Into : Hotbar)
        Merge(Into);
    if (bFull)
        for (FFoundationItemInstance& Into : Main)
            Merge(Into);
    for (FFoundationItemInstance& Into : Hotbar)
        Place(Into);
    if (bFull)
        for (FFoundationItemInstance& Into : Main)
            Place(Into);
    return Stack;
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
