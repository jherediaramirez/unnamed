#include "Foundation/FoundationLoot.h"
#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Foundation/FoundationItems.h"

namespace
{
    // SplitMix64's finalizer: every input bit reaches every output bit.
    uint64 Mix(uint64 Value)
    {
        Value += 0x9E3779B97F4A7C15ull;
        Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
        Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
        return Value ^ (Value >> 31);
    }
}

int32 FoundationLoot::MakeSeed(uint64 LootSeed, const FIntVector& Cell, int32 BreakCount)
{
    uint64 Hash = Mix(LootSeed);
    Hash = Mix(Hash ^ uint32(Cell.X));
    Hash = Mix(Hash ^ uint32(Cell.Y));
    Hash = Mix(Hash ^ uint32(Cell.Z));
    Hash = Mix(Hash ^ uint32(BreakCount));
    return int32(uint32(Hash));
}

int32 FoundationLoot::GetEnchantmentLevel(const FFoundationItemDefinition& Tool, FName Enchantment)
{
    return 0;
}

bool FoundationLoot::Passes(const FFoundationLootPredicate& Predicate, const FFoundationItemDefinition& Tool)
{
    if (!Predicate.Items.IsEmpty() && !Predicate.Items.Contains(Tool.Id)) return false;
    for (const TPair<FName, int32>& Enchantment : Predicate.Enchantments)
        if (GetEnchantmentLevel(Tool, Enchantment.Key) < Enchantment.Value) return false;
    return true;
}

int32 FoundationLoot::ApplyBonus(
    EFoundationLootBonus Bonus, float Multiplier, int32 Level, int32 Count, FRandomStream& Random)
{
    switch (Bonus)
    {
    case EFoundationLootBonus::OreDrops:
        return Count * FMath::Max(1, Random.RandRange(0, Level + 1));
    case EFoundationLootBonus::UniformBonusCount:
        return Count + Random.RandRange(0, FMath::RoundToInt32(Level * Multiplier));
    default:
        return Count;
    }
}

bool FoundationLoot::Test(const FFoundationLootTable& Table, int32 Index,
    TConstArrayView<FFoundationLootPredicate> Predicates, const FFoundationLootContext& Context, FRandomStream& Random)
{
    if (!Table.Conditions.IsValidIndex(Index)) return false;
    const FFoundationLootCondition& Condition = Table.Conditions[Index];
    const auto TermPasses = [&](int32 Term) { return Test(Table, Term, Predicates, Context, Random); };
    switch (Condition.Type)
    {
    case EFoundationLootCondition::Predicate:
        return Predicates.IsValidIndex(Condition.Predicate) && Passes(Predicates[Condition.Predicate], Context.Tool);
    case EFoundationLootCondition::SurvivesExplosion:
        return true;
    case EFoundationLootCondition::MatchBlock:
        return Context.Block == Condition.Name &&
            Algo::AllOf(
                Condition.State, [&](const TPair<FName, FName>& Value) { return Context.State.Contains(Value); });
    case EFoundationLootCondition::AnyOf:
        return Algo::AnyOf(Condition.Terms, TermPasses);
    case EFoundationLootCondition::AllOf:
        return Algo::AllOf(Condition.Terms, TermPasses);
    case EFoundationLootCondition::Inverted:
        return Condition.Terms.Num() == 1 && !TermPasses(Condition.Terms[0]);
    case EFoundationLootCondition::TableBonus:
    {
        if (Condition.Chances.IsEmpty()) return false;
        const int32 Level = GetEnchantmentLevel(Context.Tool, Condition.Name);
        return Random.GetFraction() < Condition.Chances[FMath::Clamp(Level, 0, Condition.Chances.Num() - 1)];
    }
    case EFoundationLootCondition::RandomChance:
        return !Condition.Chances.IsEmpty() && Random.GetFraction() < Condition.Chances[0];
    case EFoundationLootCondition::MatchTool:
        return Condition.Items.Contains(Context.Tool.Id);
    case EFoundationLootCondition::EntityProperties:
        return Context.bEntity;
    }
    return false;
}

namespace
{
    bool TestAll(const FFoundationLootTable& Table, TConstArrayView<int32> Conditions,
        TConstArrayView<FFoundationLootPredicate> Predicates, const FFoundationLootContext& Context,
        FRandomStream& Random)
    {
        return Algo::AllOf(
            Conditions, [&](int32 Index) { return FoundationLoot::Test(Table, Index, Predicates, Context, Random); });
    }

    int32 Modify(const FFoundationLootTable& Table, TConstArrayView<FFoundationLootModifier> Modifiers, int32 Count,
        TConstArrayView<FFoundationLootPredicate> Predicates, const FFoundationLootContext& Context,
        FRandomStream& Random)
    {
        for (const FFoundationLootModifier& Modifier : Modifiers)
        {
            if (!TestAll(Table, Modifier.Conditions, Predicates, Context, Random)) continue;
            switch (Modifier.Type)
            {
            case EFoundationLootModifier::SetCount:
            {
                const int32 Value = Random.RandRange(Modifier.Min, Modifier.Max);
                Count = Modifier.bAdd ? Count + Value : Value;
                break;
            }
            case EFoundationLootModifier::ApplyBonus:
                Count = FoundationLoot::ApplyBonus(Modifier.Bonus, Modifier.BonusMultiplier,
                    FoundationLoot::GetEnchantmentLevel(Context.Tool, Modifier.BonusEnchantment), Count, Random);
                break;
            case EFoundationLootModifier::LimitCount:
                Count = FMath::Clamp(Count, Modifier.Min, Modifier.Max);
                break;
            default:
                break;
            }
        }
        return Count;
    }
}

void FoundationLoot::Roll(const FFoundationLootTable& Table, TConstArrayView<FFoundationLootPredicate> Predicates,
    const FFoundationLootContext& Context, FRandomStream& Random, TArray<FFoundationLootDrop>& Out)
{
    for (const FFoundationLootPool& Pool : Table.Pools)
    {
        if (!TestAll(Table, Pool.Conditions, Predicates, Context, Random)) continue;
        for (int32 Roll = 0; Roll < Pool.Rolls; ++Roll)
            for (const FFoundationLootCandidate& Candidate : Pool.Candidates)
            {
                if (!TestAll(Table, Candidate.Conditions, Predicates, Context, Random)) continue;
                int32 Count = Modify(Table, Candidate.Modifiers, 1, Predicates, Context, Random);
                Count = Modify(Table, Pool.Modifiers, Count, Predicates, Context, Random);
                Count = Modify(Table, Table.Modifiers, Count, Predicates, Context, Random);
                if (Count > 0) Out.Add({Candidate.Item, Count});
                break;
            }
    }
}

void FoundationLoot::Roll(const FFoundationLootTable& Table, TConstArrayView<FFoundationLootPredicate> Predicates,
    const FFoundationItemDefinition& Tool, FRandomStream& Random, TArray<FFoundationLootDrop>& Out)
{
    Roll(Table, Predicates, FFoundationLootContext{Tool}, Random, Out);
}
