#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationTags.h"
#include "UnnamedLog.h"

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

    // Imported 26.3 blocks and their items (item id = block id) come from the block registry, the other 26.3 items
    // from DA_Items; Blocks and Items only add project blocks and items. Retired ids, never reused: dummy_block,
    // dummy_block_item, practice_pickaxe, practice_sword.
    EmptyHand = MakeItem(TEXT("empty_hand"), TEXT(""), 1, 4, TEXT(""));
    Loadout = {TEXT("stone"), TEXT("iron_pickaxe"), TEXT("iron_sword")};
}

const FFoundationPlacementPreset* UFoundationInteractionSettings::FindPreset(FName Name) const
{
    return Presets.FindByPredicate([Name](const FFoundationPlacementPreset& Preset) { return Preset.Name == Name; });
}

void UFoundationInteractionSettings::BuildRules(FFoundationRules& Rules) const
{
    const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
    Rules.Items.Reset();
    AddBlockItems(*FFoundationBlockRegistry::GetDefault(), Rules.Items);
    if (const UFoundationItemAssets* ItemAssets = UFoundationItemAssets::LoadDefault())
        MergeItems(Rules.Items, ItemAssets->MakeDefinitions(), TEXT("DA_Items"));
    MergeItems(Rules.Items, Items, TEXT("UFoundationInteractionSettings::Items"));
    Rules.Hand = EmptyHand;
    Rules.Loadout = Loadout;
    Rules.BlockReachCm = Profile.BlockReachCm;
    Rules.EntityReachCm = Profile.EntityReachCm;
    Rules.AirborneMiningMultiplier = AirborneMiningMultiplier;
    Rules.SubmergedMiningMultiplier = SubmergedMiningMultiplier;
    Rules.TargetMaxHealth = TrainingTargetMaxHealth;
}

int32 UFoundationInteractionSettings::MergeItems(
    TArray<FFoundationItemDefinition>& Into, TConstArrayView<FFoundationItemDefinition> Added, const TCHAR* Source)
{
    TSet<FName> Ids;
    for (const FFoundationItemDefinition& Item : Into)
        Ids.Add(Item.Id);
    int32 Clashes = 0;
    for (const FFoundationItemDefinition& Item : Added)
    {
        bool bClash = false;
        Ids.Add(Item.Id, &bClash);
        if (bClash)
        {
            UE_LOG(LogUnnamedFoundation, Error,
                TEXT("Item id %s from %s is already defined; the later item is skipped"), *Item.Id.ToString(), Source);
            ++Clashes;
            continue;
        }
        Into.Add(Item);
    }
    return Clashes;
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
