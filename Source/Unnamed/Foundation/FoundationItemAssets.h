#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Foundation/FoundationItems.h"
#include "FoundationItemAssets.generated.h"

class UTexture2DArray;

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

    // DA_Items under ItemRoot, loaded once; null (with one warning) before ImportItems.py has run.
    static const UFoundationItemAssets* LoadDefault();
    // The authority's definition: tags resolved to registered gameplay tags, durability from the tool and weapon
    // components, the GUI sprite. The held view draws the item's model from here (FindModelView), so Icon stays None.
    static FFoundationItemDefinition MakeDefinition(const FFoundationItemAsset& Asset);
    TArray<FFoundationItemDefinition> MakeDefinitions() const;
    const FFoundationItemAsset* FindItem(FName Id) const;
    // The item's model in a display context ("firstperson_righthand"): a select's case that lists the context, else
    // its fallback (the case with no contexts). False when the item, its model or the model's first layer is missing.
    bool FindModelView(FName Id, FName Context, FFoundationItemModelView& View) const;
};
