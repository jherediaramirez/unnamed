#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationMobAssets.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationRecipeAssets.h"
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
    const UFoundationItemAssets* ItemAssets = UFoundationItemAssets::LoadDefault();
    if (ItemAssets) MergeItems(Rules.Items, ItemAssets->MakeDefinitions(), TEXT("DA_Items"));
    MergeItems(Rules.Items, Items, TEXT("UFoundationInteractionSettings::Items"));
    // Block loot (items S5b), resolved against every item above.
    Rules.LootTables.Reset();
    Rules.LootPredicates.Reset();
    if (ItemAssets) ItemAssets->AddLoot(Rules);
    // Entity loot (survival T6 20a), from DA_Mobs, resolved the same way.
    Rules.EntityLootTables.Reset();
    if (const UFoundationMobAssets* MobAssets = UFoundationMobAssets::LoadDefault()) MobAssets->AddLoot(Rules);
    // Crafting recipes (survival step 6), resolved against every item above; those naming an item that does not exist
    // yet wait for it.
    Rules.Recipes.Recipes.Reset();
    if (const UFoundationRecipeAssets* RecipeAssets = UFoundationRecipeAssets::LoadDefault())
        RecipeAssets->AddRecipes(Rules);
    Rules.Hand = EmptyHand;
    Rules.Loadout = Loadout;
    Rules.BlockReachCm = Profile.BlockReachCm;
    Rules.EntityReachCm = Profile.EntityReachCm;
    Rules.AirborneMiningMultiplier = AirborneMiningMultiplier;
    Rules.SubmergedMiningMultiplier = SubmergedMiningMultiplier;
    Rules.TargetMaxHealth = TrainingTargetMaxHealth;
    // The wiki ("Breaking", "Creative"): spears break no blocks in creative either, though 26.3's data gives them no
    // tool component. Wiki only, not yet black-box verified (items S5); remove this to follow the data alone.
    Rules.CreativeNoBreakTags.Reset();
    if (const FGameplayTag Spears = FGameplayTag::RequestGameplayTag(TEXT("Item.Spears"), false); Spears.IsValid())
        Rules.CreativeNoBreakTags.AddTag(Spears);
    else
        UE_LOG(LogUnnamedFoundation, Warning, TEXT("Item.Spears is not registered; spears break blocks in creative"));
    // The wiki ("Sweep attack"): only a sword in the main hand sweeps (items S7).
    Rules.SweepItemTags.Reset();
    if (const FGameplayTag Swords = FGameplayTag::RequestGameplayTag(TEXT("Item.Swords"), false); Swords.IsValid())
        Rules.SweepItemTags.AddTag(Swords);
    else
        UE_LOG(LogUnnamedFoundation, Warning, TEXT("Item.Swords is not registered; nothing sweeps"));
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
    TSet<FName> WallBlocks;
    for (int32 Index = 0; Index < Registry.GetBlockCount(); ++Index)
        WallBlocks.Add(Registry.GetBlockAt(Index).ItemWallBlock);
    for (int32 Index = 0; Index < Registry.GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry.GetBlockAt(Index);
        // A wall block (wall_torch, the coral wall fans) has no item: it drops its standing block's, which that block
        // makes.
        if (WallBlocks.Contains(Block.Id) && Block.Item.IsNone()) continue;
        // A block that drops its DA_Items item (the saplings) leaves that item to DA_Items.
        if (Block.bDropsPlacingItem) continue;
        const FName Id = Block.Item.IsNone() && !Block.DropItem.IsNone() ? Block.DropItem : Block.Item;
        if (Id.IsNone() ||
            Items.ContainsByPredicate([Id](const FFoundationItemDefinition& Item) { return Item.Id == Id; }))
            continue;
        FFoundationItemDefinition Item = MakeItem(*Id.ToString(), *Block.DisplayName, 1, 4, TEXT("block"));
        Item.Tags = FGameplayTagContainer(FoundationTags::ItemBlock);
        Item.MaxStack = 64;
        Item.PlacesBlock = Block.Id;
        Item.PlacesWallBlock = Block.ItemWallBlock;
        Item.IconColor = Block.Color;
        // 26.3's data gives one block item an equippable: carved_pumpkin, head, not swappable, no equip_sound (so
        // the component's default, item.armor.equip_generic; minecraft.wiki "Data component format"). Set here by id
        // until DA_Blocks carries equippables (survival step 19a; Codex review 5a, finding 2).
        if (Id == TEXT("carved_pumpkin"))
        {
            Item.Equipment = EFoundationEquipmentSlot::Head;
            Item.bSwappable = false;
            Item.EquipSound = TEXT("item.armor.equip_generic");
        }
        Items.Add(Item);
    }
}
