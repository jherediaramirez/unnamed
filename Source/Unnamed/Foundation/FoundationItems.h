#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Foundation/FoundationActions.h"
#include "FoundationItems.generated.h"

// One ordered tool rule, as in 26.3's tool component: the first rule matching the block that states a
// speed supplies the speed; the first matching rule that states drop correctness supplies that.
USTRUCT()
struct FFoundationMiningRule
{
    GENERATED_BODY()

    // Matches a block carrying any of these tags.
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FGameplayTagContainer BlockTags;

    // Zero states no speed.
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    float Speed = 0;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    bool bStatesCorrectForDrops = false;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    bool bCorrectForDrops = false;
};

// 26.3's equippable component slot: where an item may be worn. The inventory's armor slots take only their own
// slot's items, the offhand any item (minecraft.wiki "Inventory").
UENUM()
enum class EFoundationEquipmentSlot : uint8
{
    None,
    Feet,
    Legs,
    Chest,
    Head,
    Offhand
};

// What eating an item gives (26.3's food component) and how long it takes (its consumable component's
// consume_seconds, in reference ticks; `consumable: {}` is the default 1.6 s, 32 ticks).
struct FFoundationConsumable
{
    int32 Nutrition = 0;
    float Saturation = 0;
    int32 ConsumeTicks = 0;

    bool IsValid() const { return ConsumeTicks > 0; }
};

namespace FoundationConsumables
{
    // The default consume time, 1.6 s (wiki "Food"; items.json's `consumable: {}`).
    constexpr int32 DefaultTicks = 32;
    // The 26.3 reference's food for an item id, copied from Reference/minecraft-items-26.3/items.json (food and
    // consumable) for the foods this project has; a test pins every row against items.json. Null for others. A native
    // table until DA_Items carries the food fields (a later importer migration; known issue).
    UNNAMED_API const FFoundationConsumable* FindReference(FName Id);
    UNNAMED_API TConstArrayView<TPair<FName, FFoundationConsumable>> GetReferenceTable();
}

// An authorable item type. Capabilities are independent optional fields, so one item can place, mine and
// fight; rules read capabilities and tags, never names.
USTRUCT()
struct FFoundationItemDefinition
{
    GENERATED_BODY()

    // Stable saved ID, e.g. "iron_pickaxe".
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FName Id;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FString DisplayName;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FGameplayTagContainer Tags;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    int32 MaxStack = 1;

    // Place capability: the block this item places, or None.
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FName PlacesBlock;

    // A standing-and-wall item (the torch): the block it places against a side face instead, or None.
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FName PlacesWallBlock;

    // Melee capability: total damage and attacks per second (not additive modifiers).
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    float AttackDamage = 1;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    float AttacksPerSecond = 4;

    // Mining capability: speed where no rule states one, then ordered rules.
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    float DefaultMiningSpeed = 1;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    TArray<FFoundationMiningRule> MiningRules;

    // 26.3's can_destroy_blocks_in_creative: false (swords) makes a creative player's click break nothing.
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    bool bCanDestroyBlocksInCreative = true;

    // Durability: zero means the item never wears. Costs apply only when lab tool wear is on.
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    int32 MaxDurability = 0;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    int32 DurabilityPerBlock = 0;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    int32 DurabilityPerHit = 0;

    // 26.3's weapon.disable_blocking_for_seconds (the axes' 5): how long a hit disables a blocking shield. Data only
    // until shields exist (items S7).
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    float DisableBlockingSeconds = 0;

    // 26.3's equippable slot; None for every item today (armor arrives with survival step 19a).
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    EFoundationEquipmentSlot Equipment = EFoundationEquipmentSlot::None;

    // Presentation only: "block" draws the block's 3D icon in the HUD (IconColor tints its fallback cube) and the
    // block in hand; otherwise the HUD draws Sprite and the held view the item's DA_Items model (items S4).
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FName Icon;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FLinearColor IconColor = FLinearColor::White;

    // The flat GUI sprite under ItemRoot/Gui ("item/iron_pickaxe"); None draws the generic token.
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FName Sprite;

    // Consumable capability (step 10, eating). Unset (0 ticks) here, the 26.3 reference's food for this Id applies
    // (FoundationConsumables::FindReference); tests author their own.
    FFoundationConsumable Consumable;

    bool CanPlace() const { return !PlacesBlock.IsNone(); }
    // The definition's own consumable, else the reference's for its Id, else none (0 ticks).
    FFoundationConsumable GetConsumable() const;
    // Speed from the first rule that matches and states one, else the default.
    float GetMiningSpeed(const FGameplayTagContainer& BlockTags) const;
    // Drop correctness from the first matching rule that states it, else false.
    bool IsCorrectForDrops(const FGameplayTagContainer& BlockTags) const;
};

// What a command says the hand held: an instance and its revision.
struct FFoundationItemRef
{
    int32 InstanceId = 0;
    int32 Revision = 0;

    bool operator==(const FFoundationItemRef& Other) const = default;
};

// A stack in a slot. InstanceId is its identity for the session (0 = empty); Revision bumps on every
// change (count, durability), so a command made against an older state is detectably stale.
struct FFoundationItemInstance
{
    int32 InstanceId = 0;
    int32 Item = INDEX_NONE;
    int32 Count = 0;
    int32 Damage = 0;
    int32 Revision = 0;

    bool IsEmpty() const { return InstanceId == 0 || Count <= 0 || Item == INDEX_NONE; }
    FFoundationItemRef GetRef() const
    {
        return IsEmpty() ? FFoundationItemRef{} : FFoundationItemRef{InstanceId, Revision};
    }
};

// A container drag in progress (vanilla's QUICK_CRAFT): its kind and the slots it covered so far.
struct FFoundationQuickCraft
{
    // 0 none, 1 collecting slots (vanilla's quick-craft status).
    int32 Status = 0;
    // 0 an even split (left), 1 one each (right), 2 full stacks (middle, creative).
    int32 Type = 0;
    TArray<int32> Slots;
};

// The player's inventory, in vanilla's Inventory numbering: 0-8 the hotbar, 9-35 the main grid, 36-39 the armor
// (feet, legs, chest, head), 40 the offhand. bFull off (foundation.Inventory.Full 0) is the lab inventory: only the
// hotbar and the offhand exist for every rule. Infinite supply is its own rule: placement leaves the count unchanged
// and nothing else differs.
struct UNNAMED_API FFoundationInventory
{
    static constexpr int32 HotbarSlots = 9;
    static constexpr int32 MainSlots = 27;
    static constexpr int32 ArmorSlots = 4;
    static constexpr int32 FirstMain = 9;
    static constexpr int32 FirstArmor = 36;
    static constexpr int32 OffhandSlot = 40;
    static constexpr int32 Slots = 41;

    TArray<FFoundationItemInstance> Hotbar;
    TArray<FFoundationItemInstance> Main;
    // Feet, legs, chest, head.
    TArray<FFoundationItemInstance> Armor;
    FFoundationItemInstance Offhand;
    // The stack on the cursor while a container is open (vanilla's carried item), and a drag over its slots.
    FFoundationItemInstance Carried;
    FFoundationQuickCraft QuickCraft;
    int32 Selected = 0;
    bool bInfiniteSupply = false;
    // The full inventory (36 + 4 + offhand); the authority sets it from FFoundationLabOptions::bFullInventory.
    bool bFull = false;
    int32 NextInstanceId = 1;
    // Bumps on any change, for snapshots.
    int32 Revision = 0;

    FFoundationInventory();
    FFoundationItemInstance& GetHand(EFoundationHand Hand);
    const FFoundationItemInstance& GetHand(EFoundationHand Hand) const;
    // A slot by its Inventory number (0-40); null outside that range or, with bFull off, outside the lab inventory.
    FFoundationItemInstance* GetSlot(int32 Slot);
    const FFoundationItemInstance* GetSlot(int32 Slot) const;
    FFoundationItemInstance MakeStack(int32 Item, int32 Count);
    // Writes a slot by its Inventory number (0-40); a slot GetSlot refuses is left alone.
    void SetSlot(int32 Slot, const FFoundationItemInstance& Stack);
    void Select(int32 Slot);
    void SwapHands();
    // Count that could be added (merge into same-item stacks, selected/offhand first, then free slots: the hotbar, then
    // the main grid with bFull).
    int32 GetSpaceFor(int32 Item, int32 MaxStack) const;
    // Adds up to Count in vanilla's order (minecraft.wiki "Inventory", provisional until probe P3): the selected
    // stack, the offhand, then the hotbar and the main grid merge; then the first empty slot, hotbar before main.
    // Returns how many fit. bTouchStacks false leaves the merged stacks' own revisions (their item
    // refs) as they are and bumps only the inventory's: an item entity's pickup changes a count, and a break in
    // progress with that stack goes on, as vanilla's compares the item, not the count. Damage is the added stack's wear
    // (a thrown tool picked up again); it merges only into stacks with the same damage.
    int32 Add(int32 Item, int32 Count, int32 MaxStack, bool bTouchStacks = true, int32 Damage = 0);
    // Puts a whole stack back (the cursor when a container closes) in Add's order, keeping its damage: merges only
    // into stacks of the same item and damage, and the rest keeps its identity in the first free slot. Returns what
    // did not fit (empty when it all did).
    FFoundationItemInstance Return(FFoundationItemInstance Stack, int32 MaxStack);
    // Removes one from a hand's stack (a placement); an emptied slot loses its identity.
    void ConsumeOne(EFoundationHand Hand);
    // Wears a hand's tool. True when this call used its last point and broke it.
    bool Wear(EFoundationHand Hand, int32 Amount, int32 MaxDurability);
    void Touch(FFoundationItemInstance& Stack);
};
