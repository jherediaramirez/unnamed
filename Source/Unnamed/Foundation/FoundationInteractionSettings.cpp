#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationTags.h"

namespace
{
    FFoundationPlacementPreset MakePreset(
        const TCHAR* Name, float Repeat, float Retry, bool bCommitCadence, const TCHAR* Status)
    {
        FFoundationPlacementPreset Preset;
        Preset.Name = Name;
        Preset.HeldRepeatSeconds = Repeat;
        Preset.SelfClearanceRetrySeconds = Retry;
        Preset.bCommitCadence = bCommitCadence;
        Preset.Status = Status;
        return Preset;
    }

    FFoundationItemDefinition MakeItem(const TCHAR* Id, const TCHAR* Name, float Damage, float Speed, const TCHAR* Icon)
    {
        FFoundationItemDefinition Item;
        Item.Id = Id;
        Item.DisplayName = Name;
        Item.AttackDamage = Damage;
        Item.AttacksPerSecond = Speed;
        Item.Icon = Icon;
        return Item;
    }
}

UFoundationInteractionSettings::UFoundationInteractionSettings()
{
    CategoryName = TEXT("Game");
    // Reference values within the common Unreal policies; LucidFluid is a proposed A/B experiment.
    Presets = {
        MakePreset(TEXT("VanillaReference"), .2f, 0, false,
            TEXT("Initial default: 4-tick held repeat, no self-clearance retry; not a full Java input emulator")),
        MakePreset(TEXT("LucidFluid"), .1f, .1f, true,
            TEXT("Experimental: 0.1 s commit cadence and one 0.1 s self-clearance retry; needs hands-on acceptance")),
    };
    DefaultPreset = TEXT("VanillaReference");

    // Imported 26.3 blocks and their items (item id = block id) come from the block registry; Blocks only adds
    // project blocks. Retired ids, never reused: dummy_block, dummy_block_item.

    // Iron-like practice tools: total damage and speed, 250 durability. The pickaxe has the iron pickaxe's
    // rules: it cannot harvest #incorrect_for_iron_tool (diamond-tier blocks), then mines pickaxe blocks at 6.
    FFoundationMiningRule IronTier;
    IronTier.BlockTags = FGameplayTagContainer(FoundationTags::BlockIncorrectForIronTool);
    IronTier.bStatesCorrectForDrops = true;
    IronTier.bCorrectForDrops = false;
    FFoundationMiningRule Pickaxe;
    Pickaxe.BlockTags = FGameplayTagContainer(FoundationTags::BlockMineablePickaxe);
    Pickaxe.Speed = 6;
    Pickaxe.bStatesCorrectForDrops = Pickaxe.bCorrectForDrops = true;
    FFoundationItemDefinition PickaxeItem =
        MakeItem(TEXT("practice_pickaxe"), TEXT("Practice Pickaxe"), 4, 1.2f, TEXT("pickaxe"));
    PickaxeItem.Tags = FGameplayTagContainer(FoundationTags::ItemToolPickaxe);
    PickaxeItem.MiningRules = {IronTier, Pickaxe};
    PickaxeItem.MaxDurability = 250;
    PickaxeItem.DurabilityPerBlock = 1;
    PickaxeItem.DurabilityPerHit = 2;
    PickaxeItem.IconColor = FLinearColor(.78f, .78f, .8f);

    // Sword-specific mining (cobwebs, instant plants) is deferred: default speed 1 everywhere.
    FFoundationItemDefinition Sword = MakeItem(TEXT("practice_sword"), TEXT("Practice Sword"), 6, 1.6f, TEXT("sword"));
    Sword.Tags = FGameplayTagContainer(FoundationTags::ItemWeaponSword);
    Sword.MaxDurability = 250;
    Sword.DurabilityPerBlock = 2;
    Sword.DurabilityPerHit = 1;
    Sword.IconColor = FLinearColor(.8f, .82f, .86f);
    Items = {PickaxeItem, Sword};

    EmptyHand = MakeItem(TEXT("empty_hand"), TEXT(""), 1, 4, TEXT(""));
    Loadout = {TEXT("stone"), TEXT("practice_pickaxe"), TEXT("practice_sword")};
}

const FFoundationPlacementPreset* UFoundationInteractionSettings::FindPreset(FName Name) const
{
    return Presets.FindByPredicate([Name](const FFoundationPlacementPreset& Preset) { return Preset.Name == Name; });
}

void UFoundationInteractionSettings::BuildRules(FFoundationRules& Rules) const
{
    const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
    Rules.Items = Items;
    AddBlockItems(*FFoundationBlockRegistry::GetDefault(), Rules.Items);
    Rules.Hand = EmptyHand;
    Rules.Loadout = Loadout;
    Rules.BlockReachCm = Profile.BlockReachCm;
    Rules.EntityReachCm = Profile.EntityReachCm;
    Rules.AirborneMiningMultiplier = AirborneMiningMultiplier;
    Rules.SubmergedMiningMultiplier = SubmergedMiningMultiplier;
    Rules.TargetMaxHealth = TrainingTargetMaxHealth;
}

void UFoundationInteractionSettings::AddBlockItems(
    const FFoundationBlockRegistry& Registry, TArray<FFoundationItemDefinition>& Items)
{
    for (int32 Index = 0; Index < Registry.GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry.GetBlockAt(Index);
        const FName Id = Block.Item.IsNone() && !Block.DropItem.IsNone() ? Block.DropItem : Block.Item;
        if (Id.IsNone() ||
            Items.ContainsByPredicate([Id](const FFoundationItemDefinition& Item) { return Item.Id == Id; }))
            continue;
        FFoundationItemDefinition Item = MakeItem(*Id.ToString(), *Block.DisplayName, 1, 4, TEXT("block"));
        Item.Tags = FGameplayTagContainer(FoundationTags::ItemBlock);
        Item.MaxStack = 64;
        Item.PlacesBlock = Block.Id;
        Item.IconColor = Block.Color;
        Items.Add(Item);
    }
}
