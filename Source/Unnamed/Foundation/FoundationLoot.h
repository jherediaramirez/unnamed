#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

struct FFoundationItemDefinition;

// Block loot (items S5b): 26.3's block loot tables (Reference/minecraft-items-26.3/loot_tables.json, through DA_Items).
// Each pool's single entry is flattened into ordered candidates; the first candidate whose conditions all pass drops
// (minecraft.wiki "Loot table": alternatives takes the first child that passes). No item carries enchantments yet, so
// Silk Touch never matches and Fortune is level 0; nothing explodes, so explosion decay keeps every item.

// A named tool predicate (data/minecraft/predicate/tool/*.json, match_tool): the tool is one of Items (when any are
// listed) and carries each enchantment at its minimum level or above.
struct FFoundationLootPredicate
{
    FName Name;
    TArray<FName> Items;
    TArray<TPair<FName, int32>> Enchantments;
};

// apply_bonus formulas (minecraft.wiki "Item modifier"); both are neutral at level 0.
enum class EFoundationLootBonus : uint8
{
    None,
    // "ore_drops": the count times max(1, a whole random in [0, level + 2)).
    OreDrops,
    // "uniform_bonus_count": the count plus a whole random in [0, level x multiplier].
    UniformBonusCount,
};

struct FFoundationLootCandidate
{
    // Index into FFoundationRules::Items.
    int32 Item = INDEX_NONE;
    // Indices into FFoundationRules::LootPredicates; every one must pass.
    TArray<int32> Conditions;
    // set_count's inclusive range (no set_count: 1).
    int32 CountMin = 1, CountMax = 1;
    EFoundationLootBonus Bonus = EFoundationLootBonus::None;
    FName BonusEnchantment;
    float BonusMultiplier = 0;
    // Kept as data until explosions exist.
    bool bExplosionDecay = false;
};

struct FFoundationLootPool
{
    int32 Rolls = 1;
    TArray<FFoundationLootCandidate> Candidates;
};

struct FFoundationLootTable
{
    TArray<FFoundationLootPool> Pools;
};

struct FFoundationLootDrop
{
    int32 Item = INDEX_NONE;
    int32 Count = 0;
};

namespace FoundationLoot
{
    // A roll's seed: a pure function of the rules' loot seed, the broken cell and the authority's break count.
    int32 MakeSeed(uint64 LootSeed, const FIntVector& Cell, int32 BreakCount);
    // The tool's level of an enchantment: 0, since no item carries enchantments yet.
    int32 GetEnchantmentLevel(const FFoundationItemDefinition& Tool, FName Enchantment);
    bool Passes(const FFoundationLootPredicate& Predicate, const FFoundationItemDefinition& Tool);
    int32 ApplyBonus(EFoundationLootBonus Bonus, float Multiplier, int32 Level, int32 Count, FRandomStream& Random);
    // Rolls every pool for a break with Tool and appends one drop per roll that yields at least one item.
    void Roll(const FFoundationLootTable& Table, TConstArrayView<FFoundationLootPredicate> Predicates,
        const FFoundationItemDefinition& Tool, FRandomStream& Random, TArray<FFoundationLootDrop>& Out);
}
