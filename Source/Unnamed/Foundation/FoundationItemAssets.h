#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Foundation/FoundationItems.h"
#include "FoundationItemAssets.generated.h"

class UTexture2DArray;
struct FFoundationLootTable;
struct FFoundationRules;

// One ordered rule of a 26.3 tool component, as the data states it.
USTRUCT()
struct FFoundationItemRuleAsset
{
    GENERATED_BODY()

    // The selector verbatim: a block tag ("#minecraft:mineable/pickaxe") or one block ("minecraft:cobweb").
    UPROPERTY(EditAnywhere, Category = "Item")
    FString Blocks;

    // The Block.* tag a tag selector names (BlockImportData.tag_name); None for a single-block selector. A tag no
    // imported block carries is not registered, so the rule matches nothing until such a block exists.
    UPROPERTY(EditAnywhere, Category = "Item")
    FName BlockTag;

    // Zero states no speed.
    UPROPERTY(EditAnywhere, Category = "Item")
    float Speed = 0;

    UPROPERTY(EditAnywhere, Category = "Item")
    bool bStatesCorrectForDrops = false;

    UPROPERTY(EditAnywhere, Category = "Item")
    bool bCorrectForDrops = false;
};

// The plain entry's index in one creative tab's display list.
USTRUCT()
struct FFoundationItemCreativeAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Item")
    FName Tab;

    UPROPERTY(EditAnywhere, Category = "Item")
    int32 Index = 0;
};

// One case of an item model definition: the model it selects and the display contexts it covers.
USTRUCT()
struct FFoundationItemModelCaseAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Item")
    FName Model;

    // Empty: every context (a plain model) or the select's fallback.
    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FName> Contexts;
};

// One attribute modifier, verbatim from the item's attribute_modifiers ("attack_damage", "base_attack_damage",
// -2.799999952316284 stays as the data has it, "add_value", "mainhand").
USTRUCT()
struct FFoundationItemModifierAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Item")
    FName Attribute;
    UPROPERTY(EditAnywhere, Category = "Item")
    FName Id;
    UPROPERTY(EditAnywhere, Category = "Item")
    double Amount = 0;
    UPROPERTY(EditAnywhere, Category = "Item")
    FName Operation;
    UPROPERTY(EditAnywhere, Category = "Item")
    FName Slot;
};

// A 26.3 item: identity and numbers. The GUI sprite is Textures[0]; held meshes are built from Models later.
USTRUCT()
struct FFoundationItemAsset
{
    GENERATED_BODY()

    // Saved id without namespace, e.g. "iron_pickaxe".
    UPROPERTY(EditAnywhere, Category = "Item")
    FName Id;

    UPROPERTY(EditAnywhere, Category = "Item")
    FString DisplayName;

    // 26.3 runtime number, never saved.
    UPROPERTY(EditAnywhere, Category = "Item")
    int32 ProtocolId = 0;

    // tool, spear, material, stick, bucket or food; Material and Kind name tools and spears ("iron", "pickaxe").
    UPROPERTY(EditAnywhere, Category = "Item")
    FName Group;

    UPROPERTY(EditAnywhere, Category = "Item")
    FName Material;

    UPROPERTY(EditAnywhere, Category = "Item")
    FName Kind;

    UPROPERTY(EditAnywhere, Category = "Item")
    int32 MaxStack = 64;

    // Zero: the item never wears.
    UPROPERTY(EditAnywhere, Category = "Item")
    int32 MaxDamage = 0;

    // Totals: the player's base 1 and 4 plus the item's attribute modifiers.
    UPROPERTY(EditAnywhere, Category = "Item")
    float AttackDamage = 1;

    UPROPERTY(EditAnywhere, Category = "Item")
    float AttacksPerSecond = 4;

    // The attribute modifiers in data order (the tooltip's "When in Main Hand:" lines); empty for most items.
    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FFoundationItemModifierAsset> Modifiers;

    // Whether the item has a tool component; without one it mines like the hand.
    UPROPERTY(EditAnywhere, Category = "Item")
    bool bTool = false;

    UPROPERTY(EditAnywhere, Category = "Item")
    float DefaultMiningSpeed = 1;

    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FFoundationItemRuleAsset> Rules;

    UPROPERTY(EditAnywhere, Category = "Item")
    int32 DamagePerBlock = 0;

    UPROPERTY(EditAnywhere, Category = "Item")
    bool bCanDestroyBlocksInCreative = true;

    UPROPERTY(EditAnywhere, Category = "Item")
    int32 DamagePerHit = 0;

    UPROPERTY(EditAnywhere, Category = "Item")
    float DisableBlockingSeconds = 0;

    // Zero: not enchantable.
    UPROPERTY(EditAnywhere, Category = "Item")
    int32 Enchantability = 0;

    // The repairable selector verbatim ("#minecraft:iron_tool_materials") and the item ids it resolves to.
    UPROPERTY(EditAnywhere, Category = "Item")
    FString RepairSelector;

    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FName> RepairItems;

    // Item.* gameplay tag names (Config/Tags/MinecraftItemTags.ini).
    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FName> ItemTags;

    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FFoundationItemCreativeAsset> Creative;

    // The item model definition: "model" (one case) or "select" on the display context.
    UPROPERTY(EditAnywhere, Category = "Item")
    FName ModelType;

    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FFoundationItemModelCaseAsset> Models;

    UPROPERTY(EditAnywhere, Category = "Item")
    float SwapAnimationScale = 1;

    UPROPERTY(EditAnywhere, Category = "Item")
    bool bHandAnimationOnSwap = true;

    // Sprite names ("item/iron_pickaxe"); the first is the GUI sprite.
    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FName> Textures;

    // The block it places (item id = block id), or None: the saplings once DA_Blocks holds them (survival B1).
    UPROPERTY(EditAnywhere, Category = "Item")
    FName PlacesBlock;
};

// One display context's transform, verbatim in Mojang model units (rotation degrees, translation pixels).
USTRUCT()
struct FFoundationItemDisplayAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Item")
    FName Context;

    UPROPERTY(EditAnywhere, Category = "Item")
    FVector Rotation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Item")
    FVector Translation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Item")
    FVector Scale = FVector::OneVector;
};

// A resolved item model: its layers and each context's transform from the nearest model in its parent chain.
USTRUCT()
struct FFoundationItemModelAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Item")
    FName Name;

    UPROPERTY(EditAnywhere, Category = "Item")
    FName Parent;

    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FName> Layers;

    UPROPERTY(EditAnywhere, Category = "Item")
    FName GuiLight;

    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FFoundationItemDisplayAsset> Display;
};

// A sprite's size and opacity mask (one hex string per row, top first, leftmost pixel in the highest bit).
USTRUCT()
struct FFoundationItemTextureAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Item")
    FName Name;

    UPROPERTY(EditAnywhere, Category = "Item")
    int32 Width = 16;

    UPROPERTY(EditAnywhere, Category = "Item")
    int32 Height = 16;

    UPROPERTY(EditAnywhere, Category = "Item")
    int32 OpaquePixels = 0;

    UPROPERTY(EditAnywhere, Category = "Item")
    FString RgbaSha256;

    UPROPERTY(EditAnywhere, Category = "Item")
    TArray<FString> Mask;

    // The sprite's layer in UFoundationItemAssets::ItemTextures (16 x 16) or ItemTextures32 (32 x 32).
    UPROPERTY(EditAnywhere, Category = "Item")
    int32 Layer = 0;
};

// What an item draws in one display context: its model definition's case for that context, the model's first layer
// and the context's transform.
struct FFoundationItemModelView
{
    const FFoundationItemModelAsset* Model = nullptr;
    const FFoundationItemTextureAsset* Texture = nullptr;
    // Null when the model has no transform for the context (DA_Items holds each model's transforms already resolved
    // through its parent chain): the identity transform.
    const FFoundationItemDisplayAsset* Display = nullptr;
};

// A named tool predicate a loot table's conditions use (loot_tables.json's "predicates"; items S5b).
USTRUCT()
struct FFoundationLootPredicateAsset
{
    GENERATED_BODY()

    // "tool/can_silk_touch".
    UPROPERTY(EditAnywhere, Category = "Loot")
    FName Name;

    // The tool must be one of these, when any are listed.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FName> Items;

    // Enchantments the tool must carry, each at its minimum level or above.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FName> Enchantments;

    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<int32> EnchantmentMinLevels;
};

// One node of a table's condition objects (loot step 2a), in the table's Conditions array, since a struct cannot hold
// itself: trees name their terms by index there. A predicate name inside a tree, or at the top of a pool's or a
// modifier's conditions, is a "predicate" node.
USTRUCT()
struct FFoundationLootConditionAsset
{
    GENERATED_BODY()

    // "predicate", "survives_explosion", "match_block", "any_of", "all_of", "inverted", "table_bonus",
    // "random_chance", "match_tool" or "entity_properties".
    UPROPERTY(EditAnywhere, Category = "Loot")
    FName Type;

    // predicate: its name; match_block: the block; table_bonus: the enchantment; match_tool: the tag (or None);
    // entity_properties: the entity ("this").
    UPROPERTY(EditAnywhere, Category = "Loot")
    FName Name;

    // any_of and all_of: the terms; inverted: its one term.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<int32> Terms;

    // match_block: the state, as parallel arrays.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FName> StateKeys;

    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FName> StateValues;

    // table_bonus: the chance per enchantment level; random_chance: its one chance.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<float> Chances;

    // match_tool: the items the tool must be one of.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FName> Items;

    // location_check (tall grass, large fern): the jar's offset x, y, z (y up) of the block that must be Name with the
    // state above.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<int32> Offset;
};

// An item modifier of loot_tables.json's "modifiers" lists (loot step 2a).
USTRUCT()
struct FFoundationLootModifierAsset
{
    GENERATED_BODY()

    // "set_count", "apply_bonus", "limit_count", "explosion_decay", "copy_components" or "copy_state".
    UPROPERTY(EditAnywhere, Category = "Loot")
    FName Type;

    // Indices into the table's Conditions; the modifier applies when every one passes.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<int32> Tests;

    // set_count: the inclusive range and whether it adds; limit_count: the bounds, each when it has one.
    UPROPERTY(EditAnywhere, Category = "Loot")
    int32 Min = 0;

    UPROPERTY(EditAnywhere, Category = "Loot")
    int32 Max = 0;

    UPROPERTY(EditAnywhere, Category = "Loot")
    bool bAdd = false;

    UPROPERTY(EditAnywhere, Category = "Loot")
    bool bHasMin = false;

    UPROPERTY(EditAnywhere, Category = "Loot")
    bool bHasMax = false;

    // apply_bonus, as on the candidate.
    UPROPERTY(EditAnywhere, Category = "Loot")
    FName BonusFormula;

    UPROPERTY(EditAnywhere, Category = "Loot")
    FName BonusEnchantment;

    UPROPERTY(EditAnywhere, Category = "Loot")
    float BonusMultiplier = 0;

    // furnace_smelt (entity loot, survival T6 20a): the item's furnace result, resolved by ImportMobs.py.
    UPROPERTY(EditAnywhere, Category = "Loot")
    FName ResultItem;
};

// One candidate of a flattened pool: the first whose conditions all pass drops.
USTRUCT()
struct FFoundationLootCandidateAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Loot")
    FName Item;

    // Predicate names; every one must pass.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FName> Conditions;

    // set_count's inclusive range.
    UPROPERTY(EditAnywhere, Category = "Loot")
    int32 CountMin = 1;

    UPROPERTY(EditAnywhere, Category = "Loot")
    int32 CountMax = 1;

    // apply_bonus: "ore_drops", "uniform_bonus_count" or None, with its enchantment and multiplier.
    UPROPERTY(EditAnywhere, Category = "Loot")
    FName BonusFormula;

    UPROPERTY(EditAnywhere, Category = "Loot")
    FName BonusEnchantment;

    UPROPERTY(EditAnywhere, Category = "Loot")
    float BonusMultiplier = 0;

    UPROPERTY(EditAnywhere, Category = "Loot")
    bool bExplosionDecay = false;

    // Loot step 2a: the condition objects (indices into the table's Conditions), checked after the names.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<int32> Tests;

    // Loot step 2a: when any, the candidate's ordered modifiers, in place of the count, bonus and decay above.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FFoundationLootModifierAsset> Modifiers;
};

USTRUCT()
struct FFoundationLootPoolAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Loot")
    int32 Rolls = 1;

    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FFoundationLootCandidateAsset> Candidates;

    // Loot step 2a: the pool's conditions (indices into the table's Conditions) and modifiers.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<int32> Tests;

    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FFoundationLootModifierAsset> Modifiers;
};

// A block's loot table, as loot_tables.json flattens it.
USTRUCT()
struct FFoundationLootTableAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Loot")
    FName Block;

    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FFoundationLootPoolAsset> Pools;

    // Loot step 2a: the condition nodes its candidates, pools and modifiers name, and the table's own modifiers.
    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FFoundationLootConditionAsset> Conditions;

    UPROPERTY(EditAnywhere, Category = "Loot")
    TArray<FFoundationLootModifierAsset> Modifiers;
};

// DA_Items: the 26.3 items that are not a block's item, generated by Scripts/Editor/ImportItems.py from
// Reference/minecraft-items-26.3 (UUnnamedProjectSettings::ItemRoot). Never edited by hand.
UCLASS(BlueprintType)
class UNNAMED_API UFoundationItemAssets : public UDataAsset
{
    GENERATED_BODY()
public:
    // SHA-256 of the reference's SHA256SUMS.txt; a re-import from different data refuses to overwrite.
    UPROPERTY(EditAnywhere, Category = "Items")
    FString SourceSha256;

    // Registry order.
    UPROPERTY(EditAnywhere, Category = "Items")
    TArray<FFoundationItemAsset> Items;

    UPROPERTY(EditAnywhere, Category = "Items")
    TArray<FFoundationItemModelAsset> Models;

    UPROPERTY(EditAnywhere, Category = "Items")
    TArray<FFoundationItemTextureAsset> Textures;

    // T_ItemTextures and T_ItemTextures32: every sprite as an array layer (FFoundationItemTextureAsset::Layer),
    // mip 0 only, for held items (items S4). Soft, so loading DA_Items does not load them before anything draws
    // with them; the GUI draws the loose sprites under ItemRoot/Gui instead.
    UPROPERTY(EditAnywhere, Category = "Items")
    TSoftObjectPtr<UTexture2DArray> ItemTextures;

    UPROPERTY(EditAnywhere, Category = "Items")
    TSoftObjectPtr<UTexture2DArray> ItemTextures32;

    // The tooltip's text, verbatim from en_us.json (the reference's lang.json): "item.modifiers.mainhand" and so on.
    UPROPERTY(EditAnywhere, Category = "Items")
    TMap<FString, FString> Lang;

    // Block loot tables (items S5b), in loot_tables.json's order, and the tool predicates their conditions name.
    UPROPERTY(EditAnywhere, Category = "Items")
    TArray<FFoundationLootTableAsset> LootTables;

    UPROPERTY(EditAnywhere, Category = "Items")
    TArray<FFoundationLootPredicateAsset> LootPredicates;

    // DA_Items under ItemRoot, loaded once; null (with one warning) before ImportItems.py has run.
    static const UFoundationItemAssets* LoadDefault();
    // The authority's definition: tags resolved to registered gameplay tags, durability from the tool and weapon
    // components, the GUI sprite. The held view draws the item's model from here (FindModelView), so Icon stays None.
    static FFoundationItemDefinition MakeDefinition(const FFoundationItemAsset& Asset);
    TArray<FFoundationItemDefinition> MakeDefinitions() const;
    // Replaces Rules' loot predicates and tables with these, resolving items against Rules.Items (BuildRules calls it
    // after the item merge). A table naming an item or predicate that does not exist is skipped, so its block keeps
    // "drops itself" until the item does; returns how many were skipped (none since loot step 2b).
    int32 AddLoot(FFoundationRules& Rules) const;
    const FFoundationItemAsset* FindItem(FName Id) const;
    // The item's model in a display context ("firstperson_righthand"): a select's case that lists the context, else
    // its fallback (the case with no contexts). False when the item, its model or the model's first layer is missing.
    bool FindModelView(FName Id, FName Context, FFoundationItemModelView& View) const;
};

namespace FoundationLootAssets
{
    // One table asset as the authority's table, items resolved against Rules.Items and predicates against
    // Rules.LootPredicates; returns the first unknown item, predicate or kind (None when the table is whole). DA_Items'
    // block tables (AddLoot) and DA_Mobs' entity tables share it.
    UNNAMED_API FName MakeTable(
        const FFoundationLootTableAsset& Asset, int32 TableIndex, const FFoundationRules& Rules, FFoundationLootTable& Table);
}
