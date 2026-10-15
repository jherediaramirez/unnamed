#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

struct FFoundationItemDefinition;

// Block loot (items S5b, loot step 2a): 26.3's block loot tables (Reference/minecraft-items-26.3/loot_tables.json,
// through DA_Items). Each pool's single entry is flattened into ordered candidates; the first candidate whose
// conditions all pass drops (minecraft.wiki "Loot table": alternatives takes the first child that passes). No item
// carries enchantments yet, so Silk Touch never matches and Fortune is level 0; nothing explodes, so every item
// survives the explosion and explosion decay keeps every item.

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

// Condition kinds (minecraft.wiki "Predicate"), the closed set the imported blocks' tables use.
enum class EFoundationLootCondition : uint8
{
    // A named predicate file (tool/can_silk_touch, tool/can_shear).
    Predicate,
    // True: nothing explodes.
    SurvivesExplosion,
    // The broken block is Name and has every State value.
    MatchBlock,
    AnyOf,
    AllOf,
    Inverted,
    // A random fraction below Chances[enchantment level, capped at the last].
    TableBonus,
    // A random fraction below Chances[0].
    RandomChance,
    // The tool is one of Items.
    MatchTool,
    // With no State, an entity broke the block ("this"): the player. Entity loot (survival T6 20a): Name the entity
    // ("this", "killer", "direct_attacker") and State its predicate's facts, each of which must hold (FFoundationLootContext:
    // "is_on_fire", "sheep/color", "sheared"); a fact nothing has yet (an attacker's enchantments, a killer's type) fails.
    EntityProperties,
    // The block at the broken cell plus Offset is Name and has every State value (the other half of a double plant).
    LocationCheck,
    // Entity loot (survival T6 20a): a player killed the entity.
    KilledByPlayer,
};

// One node of a table's conditions; trees refer to their terms by index into the table's Conditions.
struct FFoundationLootCondition
{
    EFoundationLootCondition Type = EFoundationLootCondition::SurvivesExplosion;
    // Predicate: index into FFoundationRules::LootPredicates.
    int32 Predicate = INDEX_NONE;
    // AnyOf, AllOf, Inverted (one term).
    TArray<int32> Terms;
    // MatchBlock, LocationCheck: the block; TableBonus: the enchantment.
    FName Name;
    TArray<TPair<FName, FName>> State;
    TArray<float> Chances;
    TArray<FName> Items;
    // LocationCheck: in cells (the jar's x, y, z is X, Z, Y here: west -X, north -Y, up +Z).
    FIntVector Offset = FIntVector::ZeroValue;
};

// Item modifier kinds (minecraft.wiki "Item modifier").
enum class EFoundationLootModifier : uint8
{
    // The count becomes (or with bAdd, gains) a whole random in [Min, Max].
    SetCount,
    ApplyBonus,
    // The count is clamped to [Min, Max].
    LimitCount,
    // Kept as data until explosions exist.
    ExplosionDecay,
    // copy_components and copy_state: kept as data; stacks carry no components yet.
    Copy,
    // Entity loot (survival T6 20a): the item becomes Item, its furnace result (furnace_smelt).
    FurnaceSmelt,
    // enchanted_count_increase (Looting): neutral, since no item carries enchantments yet.
    EnchantedCountIncrease,
};

struct FFoundationLootModifier
{
    EFoundationLootModifier Type = EFoundationLootModifier::SetCount;
    // Indices into the table's Conditions; the modifier applies when every one passes.
    TArray<int32> Conditions;
    int32 Min = 1, Max = 1;
    bool bAdd = false;
    EFoundationLootBonus Bonus = EFoundationLootBonus::None;
    FName BonusEnchantment;
    float BonusMultiplier = 0;
    // FurnaceSmelt: the result, an index into FFoundationRules::Items.
    int32 Item = INDEX_NONE;
};

struct FFoundationLootCandidate
{
    // Index into FFoundationRules::Items.
    int32 Item = INDEX_NONE;
    // Indices into the table's Conditions; every one must pass.
    TArray<int32> Conditions;
    // Applied in order to a count of 1.
    TArray<FFoundationLootModifier> Modifiers;
};

struct FFoundationLootPool
{
    int32 Rolls = 1;
    // Checked once per pool, before its rolls.
    TArray<int32> Conditions;
    TArray<FFoundationLootCandidate> Candidates;
    // Applied after the candidate's own.
    TArray<FFoundationLootModifier> Modifiers;
};

struct FFoundationLootTable
{
    // The table's position in loot_tables.json (LegacyTables: items S5b's ores come first).
    int32 Index = INDEX_NONE;
    TArray<FFoundationLootCondition> Conditions;
    TArray<FFoundationLootPool> Pools;
    // Applied after the pool's.
    TArray<FFoundationLootModifier> Modifiers;
};

// What a roll knows of its break (the wiki's loot context): the tool, the broken block and its state, whether an
// entity broke it, and the world around it.
struct FFoundationLootContext
{
    const FFoundationItemDefinition& Tool;
    FName Block;
    TArray<TPair<FName, FName>> State;
    bool bEntity = true;
    // The block and state at the broken cell plus an offset, as the world holds them when the roll happens; false
    // where it is unknown. Unset (a roll with no world), LocationCheck fails.
    TFunction<bool(const FIntVector& Offset, FName& Block, TArray<TPair<FName, FName>>& State)> GetBlockAt;
    // Entity loot (survival T6 20a): a player killed it; the killed entity's own facts ("this").
    bool bKilledByPlayer = false;
    bool bOnFire = false;
    FName SheepColor;
    bool bSheared = false;
};

struct FFoundationLootDrop
{
    int32 Item = INDEX_NONE;
    int32 Count = 0;
};

namespace FoundationLoot
{
    // loot_tables.json's first tables, items S5b's 18 ores: with foundation.Loot.AllBlocks 0 only these roll.
    constexpr int32 LegacyTables = 18;
    // A roll's seed: a pure function of the rules' loot seed, the broken cell and the authority's break count.
    int32 MakeSeed(uint64 LootSeed, const FIntVector& Cell, int32 BreakCount);
    // The tool's level of an enchantment: 0, since no item carries enchantments yet.
    int32 GetEnchantmentLevel(const FFoundationItemDefinition& Tool, FName Enchantment);
    bool Passes(const FFoundationLootPredicate& Predicate, const FFoundationItemDefinition& Tool);
    int32 ApplyBonus(EFoundationLootBonus Bonus, float Multiplier, int32 Level, int32 Count, FRandomStream& Random);
    // Whether the table's condition Index passes; random conditions draw from Random.
    bool Test(const FFoundationLootTable& Table, int32 Index, TConstArrayView<FFoundationLootPredicate> Predicates,
        const FFoundationLootContext& Context, FRandomStream& Random);
    // Rolls every pool for a break and appends one drop per roll that yields at least one item.
    void Roll(const FFoundationLootTable& Table, TConstArrayView<FFoundationLootPredicate> Predicates,
        const FFoundationLootContext& Context, FRandomStream& Random, TArray<FFoundationLootDrop>& Out);
    // The same, for a break with Tool of no particular block.
    void Roll(const FFoundationLootTable& Table, TConstArrayView<FFoundationLootPredicate> Predicates,
        const FFoundationItemDefinition& Tool, FRandomStream& Random, TArray<FFoundationLootDrop>& Out);
}
