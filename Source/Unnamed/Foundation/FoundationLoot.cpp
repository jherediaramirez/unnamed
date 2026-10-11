#include "Foundation/FoundationLoot.h"
#include "Algo/AllOf.h"
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

void FoundationLoot::Roll(const FFoundationLootTable& Table, TConstArrayView<FFoundationLootPredicate> Predicates,
    const FFoundationItemDefinition& Tool, FRandomStream& Random, TArray<FFoundationLootDrop>& Out)
{
    for (const FFoundationLootPool& Pool : Table.Pools)
        for (int32 Roll = 0; Roll < Pool.Rolls; ++Roll)
            for (const FFoundationLootCandidate& Candidate : Pool.Candidates)
            {
                if (!Algo::AllOf(Candidate.Conditions,
                        [&](int32 Index) { return Predicates.IsValidIndex(Index) && Passes(Predicates[Index], Tool); }))
                    continue;
                int32 Count = Random.RandRange(Candidate.CountMin, Candidate.CountMax);
                Count = ApplyBonus(Candidate.Bonus, Candidate.BonusMultiplier,
                    GetEnchantmentLevel(Tool, Candidate.BonusEnchantment), Count, Random);
                if (Count > 0) Out.Add({Candidate.Item, Count});
                break;
            }
}
