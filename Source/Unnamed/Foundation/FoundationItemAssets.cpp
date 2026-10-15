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
    // A second layer (the leather pieces' overlay) draws over the first; the tint multiplies the first only.
    Item.SpriteOverlay = Asset.Textures.Num() > 1 ? Asset.Textures[1] : NAME_None;
    Item.SpriteTint = Asset.LayerTints.IsEmpty() ? FColor::White : Asset.LayerTints[0];
    Item.PlacesBlock = Asset.PlacesBlock;
    static const TMap<FName, EFoundationEquipmentSlot> Slots = {{TEXT("head"), EFoundationEquipmentSlot::Head},
        {TEXT("chest"), EFoundationEquipmentSlot::Chest}, {TEXT("legs"), EFoundationEquipmentSlot::Legs},
        {TEXT("feet"), EFoundationEquipmentSlot::Feet}};
    if (const EFoundationEquipmentSlot* Slot = Slots.Find(Asset.EquipmentSlot))
    {
        Item.Equipment = *Slot;
        Item.bSwappable = Asset.bSwappable;
        Item.EquipSound = Asset.EquipSound;
        // Whole armor points (every 26.3 piece's are); the toughness keeps its fraction.
        Item.Armor = FMath::RoundToInt(Asset.Armor);
        Item.ArmorToughness = Asset.ArmorToughness;
    }
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
    if (View.Model->Layers.Num() > 1)
    {
        const FName Second = View.Model->Layers[1];
        View.Overlay = Textures.FindByPredicate(
            [Second](const FFoundationItemTextureAsset& Texture) { return Texture.Name == Second; });
    }
    if (!Item->LayerTints.IsEmpty()) View.Tint = Item->LayerTints[0];
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

FName FoundationLootAssets::MakeTable(const FFoundationLootTableAsset& Asset, int32 TableIndex,
    const FFoundationRules& Rules, FFoundationLootTable& Table)
{
    Table.Index = TableIndex;
    // The first unknown item, predicate or kind skips the table.
    FName Missing;
    const auto FindPredicate = [&](FName Name)
    {
        const int32 Index = Rules.LootPredicates.IndexOfByPredicate(
            [Name](const FFoundationLootPredicate& Predicate) { return Predicate.Name == Name; });
        if (Index == INDEX_NONE && Missing.IsNone()) Missing = Name;
        return Index;
    };
    const auto FindBonus = [&](FName Formula)
    {
        if (Formula == TEXT("ore_drops")) return EFoundationLootBonus::OreDrops;
        if (Formula == TEXT("uniform_bonus_count")) return EFoundationLootBonus::UniformBonusCount;
        if (!Formula.IsNone() && Missing.IsNone()) Missing = Formula;
        return EFoundationLootBonus::None;
    };
    static const TMap<FName, EFoundationLootCondition> ConditionTypes = {
        {TEXT("predicate"), EFoundationLootCondition::Predicate},
        {TEXT("survives_explosion"), EFoundationLootCondition::SurvivesExplosion},
        {TEXT("match_block"), EFoundationLootCondition::MatchBlock}, {TEXT("any_of"), EFoundationLootCondition::AnyOf},
        {TEXT("all_of"), EFoundationLootCondition::AllOf}, {TEXT("inverted"), EFoundationLootCondition::Inverted},
        {TEXT("table_bonus"), EFoundationLootCondition::TableBonus},
        {TEXT("random_chance"), EFoundationLootCondition::RandomChance},
        {TEXT("match_tool"), EFoundationLootCondition::MatchTool},
        {TEXT("entity_properties"), EFoundationLootCondition::EntityProperties},
        {TEXT("location_check"), EFoundationLootCondition::LocationCheck},
        {TEXT("killed_by_player"), EFoundationLootCondition::KilledByPlayer}};
    for (const FFoundationLootConditionAsset& ConditionAsset : Asset.Conditions)
    {
        FFoundationLootCondition& Condition = Table.Conditions.AddDefaulted_GetRef();
        if (const EFoundationLootCondition* Type = ConditionTypes.Find(ConditionAsset.Type))
            Condition.Type = *Type;
        else if (Missing.IsNone())
            Missing = ConditionAsset.Type;
        if (Condition.Type == EFoundationLootCondition::Predicate)
            Condition.Predicate = FindPredicate(ConditionAsset.Name);
        Condition.Terms = ConditionAsset.Terms;
        Condition.Name = ConditionAsset.Name;
        // Parallel arrays (ImportItems.py writes both).
        for (int32 Index = 0; Index < FMath::Min(ConditionAsset.StateKeys.Num(), ConditionAsset.StateValues.Num());
            ++Index)
            Condition.State.Emplace(ConditionAsset.StateKeys[Index], ConditionAsset.StateValues[Index]);
        Condition.Chances = ConditionAsset.Chances;
        Condition.Items = ConditionAsset.Items;
        if (Condition.Type == EFoundationLootCondition::LocationCheck)
        {
            // The jar's x, y, z (y up) in cells: X, Z, Y.
            if (ConditionAsset.Offset.Num() == 3)
                Condition.Offset =
                    FIntVector(ConditionAsset.Offset[0], ConditionAsset.Offset[2], ConditionAsset.Offset[1]);
            else if (Missing.IsNone())
                Missing = ConditionAsset.Type;
        }
    }
    const auto ToModifiers = [&](TConstArrayView<FFoundationLootModifierAsset> Assets)
    {
        static const TMap<FName, EFoundationLootModifier> ModifierTypes = {
            {TEXT("set_count"), EFoundationLootModifier::SetCount},
            {TEXT("apply_bonus"), EFoundationLootModifier::ApplyBonus},
            {TEXT("limit_count"), EFoundationLootModifier::LimitCount},
            {TEXT("explosion_decay"), EFoundationLootModifier::ExplosionDecay},
            {TEXT("copy_components"), EFoundationLootModifier::Copy},
            {TEXT("copy_state"), EFoundationLootModifier::Copy},
            {TEXT("furnace_smelt"), EFoundationLootModifier::FurnaceSmelt},
            {TEXT("enchanted_count_increase"), EFoundationLootModifier::EnchantedCountIncrease}};
        TArray<FFoundationLootModifier> Modifiers;
        for (const FFoundationLootModifierAsset& ModifierAsset : Assets)
        {
            FFoundationLootModifier& Modifier = Modifiers.AddDefaulted_GetRef();
            if (const EFoundationLootModifier* Type = ModifierTypes.Find(ModifierAsset.Type))
                Modifier.Type = *Type;
            else if (Missing.IsNone())
                Missing = ModifierAsset.Type;
            Modifier.Conditions = ModifierAsset.Tests;
            Modifier.Min = Modifier.Type == EFoundationLootModifier::LimitCount && !ModifierAsset.bHasMin
                ? MIN_int32
                : ModifierAsset.Min;
            Modifier.Max = Modifier.Type == EFoundationLootModifier::LimitCount && !ModifierAsset.bHasMax
                ? MAX_int32
                : ModifierAsset.Max;
            Modifier.bAdd = ModifierAsset.bAdd;
            Modifier.Bonus = FindBonus(ModifierAsset.BonusFormula);
            Modifier.BonusEnchantment = ModifierAsset.BonusEnchantment;
            Modifier.BonusMultiplier = ModifierAsset.BonusMultiplier;
            if (Modifier.Type == EFoundationLootModifier::FurnaceSmelt)
            {
                Modifier.Item = Rules.FindItem(ModifierAsset.ResultItem);
                if (Modifier.Item == INDEX_NONE && Missing.IsNone()) Missing = ModifierAsset.ResultItem;
            }
        }
        return Modifiers;
    };
    for (const FFoundationLootPoolAsset& PoolAsset : Asset.Pools)
    {
        FFoundationLootPool& Pool = Table.Pools.AddDefaulted_GetRef();
        Pool.Rolls = PoolAsset.Rolls;
        Pool.Conditions = PoolAsset.Tests;
        Pool.Modifiers = ToModifiers(PoolAsset.Modifiers);
        for (const FFoundationLootCandidateAsset& CandidateAsset : PoolAsset.Candidates)
        {
            FFoundationLootCandidate& Candidate = Pool.Candidates.AddDefaulted_GetRef();
            Candidate.Item = Rules.FindItem(CandidateAsset.Item);
            if (Candidate.Item == INDEX_NONE && Missing.IsNone()) Missing = CandidateAsset.Item;
            // Predicate names become nodes after the asset's, then the condition objects follow them.
            for (const FName Condition : CandidateAsset.Conditions)
            {
                FFoundationLootCondition& Node = Table.Conditions.AddDefaulted_GetRef();
                Node.Type = EFoundationLootCondition::Predicate;
                Node.Name = Condition;
                Node.Predicate = FindPredicate(Condition);
                Candidate.Conditions.Add(Table.Conditions.Num() - 1);
            }
            Candidate.Conditions.Append(CandidateAsset.Tests);
            if (!CandidateAsset.Modifiers.IsEmpty())
            {
                Candidate.Modifiers = ToModifiers(CandidateAsset.Modifiers);
                continue;
            }
            // The ore form (items S5b): set_count's range, then the bonus and the decay. The range is always
            // drawn, as S5b drew it, so the ores' rolls stay the same.
            FFoundationLootModifier& Count = Candidate.Modifiers.AddDefaulted_GetRef();
            Count.Min = CandidateAsset.CountMin;
            Count.Max = CandidateAsset.CountMax;
            if (const EFoundationLootBonus Bonus = FindBonus(CandidateAsset.BonusFormula);
                Bonus != EFoundationLootBonus::None)
            {
                FFoundationLootModifier& Modifier = Candidate.Modifiers.AddDefaulted_GetRef();
                Modifier.Type = EFoundationLootModifier::ApplyBonus;
                Modifier.Bonus = Bonus;
                Modifier.BonusEnchantment = CandidateAsset.BonusEnchantment;
                Modifier.BonusMultiplier = CandidateAsset.BonusMultiplier;
            }
            if (CandidateAsset.bExplosionDecay)
                Candidate.Modifiers.AddDefaulted_GetRef().Type = EFoundationLootModifier::ExplosionDecay;
        }
    }
    Table.Modifiers = ToModifiers(Asset.Modifiers);
    return Missing;
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
    for (int32 TableIndex = 0; TableIndex < LootTables.Num(); ++TableIndex)
    {
        const FFoundationLootTableAsset& Asset = LootTables[TableIndex];
        FFoundationLootTable Table;
        const FName Missing = FoundationLootAssets::MakeTable(Asset, TableIndex, Rules, Table);
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
