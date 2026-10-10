#include "Foundation/FoundationItemAssets.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"

namespace
{
    // Held-item and hotbar tints by tool material, until icons and held meshes come from the sprites.
    FLinearColor MaterialTint(FName Material)
    {
        if (Material == TEXT("wooden")) return FLinearColor(.55f, .4f, .22f);
        if (Material == TEXT("stone")) return FLinearColor(.5f, .5f, .5f);
        if (Material == TEXT("copper")) return FLinearColor(.85f, .5f, .3f);
        if (Material == TEXT("iron")) return FLinearColor(.78f, .78f, .8f);
        if (Material == TEXT("golden")) return FLinearColor(.95f, .8f, .25f);
        if (Material == TEXT("diamond")) return FLinearColor(.4f, .85f, .85f);
        if (Material == TEXT("netherite")) return FLinearColor(.3f, .27f, .28f);
        return FLinearColor::White;
    }
}

const UFoundationItemAssets* UFoundationItemAssets::LoadDefault()
{
    static const UFoundationItemAssets* Default = nullptr;
    static bool bTried = false;
    if (!bTried)
    {
        // Loaded once and kept for the process, like DA_Blocks: a reimport needs an editor restart.
        bTried = true;
        const FSoftObjectPath Path = UUnnamedProjectSettings::GetItemAssetPath(TEXT("DA_Items"));
        Default = Cast<UFoundationItemAssets>(Path.TryLoad());
        if (Default)
            const_cast<UFoundationItemAssets*>(Default)->AddToRoot();
        else
            UE_LOG(LogUnnamedFoundation, Warning,
                TEXT("Item registry asset %s is missing: only block and project items exist. Run ImportItems.py."),
                *Path.ToString());
    }
    return Default;
}

FFoundationItemDefinition UFoundationItemAssets::MakeDefinition(const FFoundationItemAsset& Asset)
{
    FFoundationItemDefinition Item;
    Item.Id = Asset.Id;
    Item.DisplayName = Asset.DisplayName;
    for (const FName Tag : Asset.ItemTags)
        if (const FGameplayTag Found = FGameplayTag::RequestGameplayTag(Tag, false); Found.IsValid())
            Item.Tags.AddTag(Found);
    Item.MaxStack = Asset.MaxStack;
    Item.AttackDamage = Asset.AttackDamage;
    Item.AttacksPerSecond = Asset.AttacksPerSecond;
    Item.DefaultMiningSpeed = Asset.DefaultMiningSpeed;
    for (const FFoundationItemRuleAsset& Source : Asset.Rules)
    {
        // An unregistered tag or a single-block selector leaves the rule's tags empty: it matches nothing.
        FFoundationMiningRule& Rule = Item.MiningRules.AddDefaulted_GetRef();
        if (const FGameplayTag Found = FGameplayTag::RequestGameplayTag(Source.BlockTag, false); Found.IsValid())
            Rule.BlockTags.AddTag(Found);
        Rule.Speed = Source.Speed;
        Rule.bStatesCorrectForDrops = Source.bStatesCorrectForDrops;
        Rule.bCorrectForDrops = Source.bCorrectForDrops;
    }
    Item.MaxDurability = Asset.MaxDamage;
    Item.DurabilityPerBlock = Asset.DamagePerBlock;
    Item.DurabilityPerHit = Asset.DamagePerHit;
    Item.Icon = Asset.Kind == TEXT("pickaxe") || Asset.Kind == TEXT("sword") ? Asset.Kind : NAME_None;
    Item.IconColor = MaterialTint(Asset.Material);
    return Item;
}

TArray<FFoundationItemDefinition> UFoundationItemAssets::MakeDefinitions() const
{
    TArray<FFoundationItemDefinition> Definitions;
    Definitions.Reserve(Items.Num());
    for (const FFoundationItemAsset& Asset : Items)
        Definitions.Add(MakeDefinition(Asset));
    return Definitions;
}
