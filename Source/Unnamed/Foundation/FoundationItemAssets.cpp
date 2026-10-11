#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationAuthority.h"
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
    Item.DisableBlockingSeconds = Asset.DisableBlockingSeconds;
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

int32 UFoundationItemAssets::AddLoot(FFoundationRules& Rules) const
{
    Rules.LootPredicates.Reset();
    for (const FFoundationLootPredicateAsset& Asset : LootPredicates)
    {
        FFoundationLootPredicate& Predicate = Rules.LootPredicates.AddDefaulted_GetRef();
        Predicate.Name = Asset.Name;
        Predicate.Items = Asset.Items;
        // Parallel arrays (ImportItems.py writes both).
        for (int32 Index = 0; Index < FMath::Min(Asset.Enchantments.Num(), Asset.EnchantmentMinLevels.Num()); ++Index)
            Predicate.Enchantments.Emplace(Asset.Enchantments[Index], Asset.EnchantmentMinLevels[Index]);
    }
    Rules.LootTables.Reset();
    int32 Skipped = 0;
    for (const FFoundationLootTableAsset& Asset : LootTables)
    {
        FFoundationLootTable Table;
        FName Missing;
        for (const FFoundationLootPoolAsset& PoolAsset : Asset.Pools)
        {
            FFoundationLootPool& Pool = Table.Pools.AddDefaulted_GetRef();
            Pool.Rolls = PoolAsset.Rolls;
            for (const FFoundationLootCandidateAsset& CandidateAsset : PoolAsset.Candidates)
            {
                FFoundationLootCandidate& Candidate = Pool.Candidates.AddDefaulted_GetRef();
                Candidate.Item = Rules.FindItem(CandidateAsset.Item);
                if (Candidate.Item == INDEX_NONE) Missing = CandidateAsset.Item;
                for (const FName Condition : CandidateAsset.Conditions)
                {
                    const int32 Index = Rules.LootPredicates.IndexOfByPredicate(
                        [Condition](const FFoundationLootPredicate& Predicate) { return Predicate.Name == Condition; });
                    if (Index == INDEX_NONE) Missing = Condition;
                    Candidate.Conditions.Add(Index);
                }
                Candidate.CountMin = CandidateAsset.CountMin;
                Candidate.CountMax = CandidateAsset.CountMax;
                if (CandidateAsset.BonusFormula == TEXT("ore_drops"))
                    Candidate.Bonus = EFoundationLootBonus::OreDrops;
                else if (CandidateAsset.BonusFormula == TEXT("uniform_bonus_count"))
                    Candidate.Bonus = EFoundationLootBonus::UniformBonusCount;
                else if (!CandidateAsset.BonusFormula.IsNone())
                    Missing = CandidateAsset.BonusFormula;
                Candidate.BonusEnchantment = CandidateAsset.BonusEnchantment;
                Candidate.BonusMultiplier = CandidateAsset.BonusMultiplier;
                Candidate.bExplosionDecay = CandidateAsset.bExplosionDecay;
            }
        }
        if (!Missing.IsNone())
        {
            UE_LOG(LogUnnamedFoundation, Log,
                TEXT("The loot table of %s is skipped: %s is not defined; it drops itself"), *Asset.Block.ToString(),
                *Missing.ToString());
            ++Skipped;
            continue;
        }
        Rules.LootTables.Add(Asset.Block, MoveTemp(Table));
    }
    return Skipped;
}
