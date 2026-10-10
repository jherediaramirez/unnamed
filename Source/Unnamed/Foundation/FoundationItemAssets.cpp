#include "Foundation/FoundationItemAssets.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"

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
    Item.bCanDestroyBlocksInCreative = Asset.bCanDestroyBlocksInCreative;
    Item.MaxDurability = Asset.MaxDamage;
    Item.DurabilityPerBlock = Asset.DamagePerBlock;
    Item.DurabilityPerHit = Asset.DamagePerHit;
    Item.Sprite = Asset.Textures.IsEmpty() ? NAME_None : Asset.Textures[0];
    return Item;
}

const FFoundationItemAsset* UFoundationItemAssets::FindItem(FName Id) const
{
    return Items.FindByPredicate([Id](const FFoundationItemAsset& Item) { return Item.Id == Id; });
}

bool UFoundationItemAssets::FindModelView(FName Id, FName Context, FFoundationItemModelView& View) const
{
    View = {};
    const FFoundationItemAsset* Item = FindItem(Id);
    if (!Item) return false;
    const FFoundationItemModelCaseAsset* Case = Item->Models.FindByPredicate(
        [Context](const FFoundationItemModelCaseAsset& Candidate) { return Candidate.Contexts.Contains(Context); });
    if (!Case)
        Case = Item->Models.FindByPredicate(
            [](const FFoundationItemModelCaseAsset& Candidate) { return Candidate.Contexts.IsEmpty(); });
    if (!Case) return false;
    View.Model =
        Models.FindByPredicate([Case](const FFoundationItemModelAsset& Model) { return Model.Name == Case->Model; });
    if (!View.Model || View.Model->Layers.IsEmpty()) return false;
    const FName Layer = View.Model->Layers[0];
    View.Texture =
        Textures.FindByPredicate([Layer](const FFoundationItemTextureAsset& Texture) { return Texture.Name == Layer; });
    View.Display = View.Model->Display.FindByPredicate(
        [Context](const FFoundationItemDisplayAsset& Display) { return Display.Context == Context; });
    return View.Texture != nullptr;
}

TArray<FFoundationItemDefinition> UFoundationItemAssets::MakeDefinitions() const
{
    TArray<FFoundationItemDefinition> Definitions;
    Definitions.Reserve(Items.Num());
    for (const FFoundationItemAsset& Asset : Items)
        Definitions.Add(MakeDefinition(Asset));
    return Definitions;
}
