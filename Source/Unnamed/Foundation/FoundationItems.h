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

    // Presentation only: "block" draws the block's 3D icon in the HUD (IconColor tints its fallback cube) and the
    // block in hand; otherwise the HUD draws Sprite and the held view the item's DA_Items model (items S4).
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FName Icon;

    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FLinearColor IconColor = FLinearColor::White;

    // The flat GUI sprite under ItemRoot/Gui ("item/iron_pickaxe"); None draws the generic token.
    UPROPERTY(Config, EditAnywhere, Category = "Item")
    FName Sprite;

    bool CanPlace() const { return !PlacesBlock.IsNone(); }
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

// The lab inventory: nine hotbar slots plus the offhand, the project's existing hand conventions.
// Infinite supply is its own rule: placement leaves the count unchanged and nothing else differs.
struct UNNAMED_API FFoundationInventory
{
    static constexpr int32 HotbarSlots = 9;

    TArray<FFoundationItemInstance> Hotbar;
    FFoundationItemInstance Offhand;
    int32 Selected = 0;
    bool bInfiniteSupply = false;
    int32 NextInstanceId = 1;
    // Bumps on any change, for snapshots.
    int32 Revision = 0;

    FFoundationInventory();
    FFoundationItemInstance& GetHand(EFoundationHand Hand);
    const FFoundationItemInstance& GetHand(EFoundationHand Hand) const;
    FFoundationItemInstance MakeStack(int32 Item, int32 Count);
    void SetSlot(int32 Slot, const FFoundationItemInstance& Stack);
    void Select(int32 Slot);
    void SwapHands();
    // Count that could be added (merge into same-item stacks, selected/offhand first, then free hotbar slots).
    int32 GetSpaceFor(int32 Item, int32 MaxStack) const;
    // Adds up to Count; returns how many fit. bTouchStacks false leaves the merged stacks' own revisions (their item
    // refs) as they are and bumps only the inventory's: an item entity's pickup changes a count, and a break in
    // progress with that stack goes on, as vanilla's compares the item, not the count.
    int32 Add(int32 Item, int32 Count, int32 MaxStack, bool bTouchStacks = true);
    // Removes one from a hand's stack (a placement); an emptied slot loses its identity.
    void ConsumeOne(EFoundationHand Hand);
    // Wears a hand's tool. True when this call used its last point and broke it.
    bool Wear(EFoundationHand Hand, int32 Amount, int32 MaxDurability);
    void Touch(FFoundationItemInstance& Stack);
};
