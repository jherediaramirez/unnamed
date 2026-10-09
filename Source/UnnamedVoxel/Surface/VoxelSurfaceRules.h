#pragma once

#include "CoreMinimal.h"

// The reference's surface rule types (worldgen/material_rule). A rule gives a block or nothing; a sequence runs its
// rules in order and the first block wins.
enum class EVoxelSurfaceRuleOp : uint8
{
    Sequence, // Rules RuleList[First .. First + Count).
    Condition, // When Condition holds, the result of rule Then; otherwise nothing.
    Block, // The block state Strings[Block].
    OreVein, // Blocks Strings[Block] (ore), [RawOreBlock], [FillerBlock]; density functions Strings[Density],
             // [Richness], [FillerGap]; RawOreChance.
    Bandlands, // No fields: the badlands terracotta bands.
    Count
};

// The reference's condition types (inline in rules, or named in worldgen/material_condition).
enum class EVoxelSurfaceConditionOp : uint8
{
    Biome, // The biome is one of Strings[First .. First + Count).
    NoiseThreshold, // The noise Strings[First] is within [MinThreshold, MaxThreshold]; bIs3D samples it at the block's
                    // y.
    VerticalGradient, // Random name Strings[First]: true at and below Anchor, false at and above FalseAnchor.
    YAbove, // Above Anchor, moved by SurfaceDepthMultiplier and, with bAddDepth, the stone depth.
    Water, // Above the water level plus Offset, moved by SurfaceDepthMultiplier and, with bAddDepth, the stone depth.
    StoneDepth, // Within Offset (plus, with bAddDepth, the surface depth and SecondaryDepthRange) of the floor, or with
                // bCeiling of the ceiling.
    Not, // Condition Input does not hold.
    Steep, // No fields.
    Hole, // No fields.
    AbovePreliminarySurface, // No fields.
    Count
};

// A height: absolute, above the world's bottom, or below its top.
enum class EVoxelAnchorKind : uint8
{
    Absolute,
    AboveBottom,
    BelowTop
};

struct FVoxelVerticalAnchor
{
    EVoxelAnchorKind Kind = EVoxelAnchorKind::Absolute;
    int32 Value = 0;
};

struct FVoxelSurfaceRule
{
    EVoxelSurfaceRuleOp Op = EVoxelSurfaceRuleOp::Block;
    int32 First = INDEX_NONE;
    int32 Count = 0;
    int32 Condition = INDEX_NONE;
    int32 Then = INDEX_NONE;
    int32 Block = INDEX_NONE;
    int32 RawOreBlock = INDEX_NONE;
    int32 FillerBlock = INDEX_NONE;
    int32 Density = INDEX_NONE;
    int32 Richness = INDEX_NONE;
    int32 FillerGap = INDEX_NONE;
    double RawOreChance = 0;
};

struct FVoxelSurfaceCondition
{
    EVoxelSurfaceConditionOp Op = EVoxelSurfaceConditionOp::Steep;
    int32 Input = INDEX_NONE;
    int32 First = INDEX_NONE;
    int32 Count = 0;
    double MinThreshold = 0;
    double MaxThreshold = 0;
    bool bIs3D = false;
    bool bAddDepth = false;
    bool bCeiling = false;
    FVoxelVerticalAnchor Anchor;
    FVoxelVerticalAnchor FalseAnchor;
    int32 SurfaceDepthMultiplier = 0;
    int32 Offset = 0;
    int32 SecondaryDepthRange = 0;
};

// A named rule or condition (its own file) and its node.
struct FVoxelSurfaceName
{
    FString Name;
    int32 Index = INDEX_NONE;
};

// A surface rule tree as plain data: rules and conditions in two flat arrays with children by index, as the density
// graph keeps its nodes. A named rule or condition is one node however many others reference it. Every node comes
// after the nodes it reads, so the tree has no cycles. Ids (blocks, biomes, noises, random names, density functions)
// stay strings here; the evaluator resolves them when it compiles the tree.
class UNNAMEDVOXEL_API FVoxelSurfaceRules
{
public:
    TArray<FVoxelSurfaceRule> Rules;
    TArray<FVoxelSurfaceCondition> Conditions;
    // Sequence entries, by rule index.
    TArray<int32> RuleList;
    // Every id the nodes name, each once.
    TArray<FString> Strings;
    TArray<FVoxelSurfaceName> NamedRules;
    TArray<FVoxelSurfaceName> NamedConditions;
    // The tree's root rule.
    int32 Root = INDEX_NONE;

    int32 FindRule(FStringView Name) const;
    int32 FindCondition(FStringView Name) const;
    // A string's index, added if new.
    int32 AddString(const FString& Value);

    // Checks every index and count, each op's fields, the order above, non-empty ids and unique names.
    bool Validate(FString* OutError = nullptr) const;

    // The reference's type names without their namespace ("sequence", "stone_depth").
    static const TCHAR* GetRuleOpName(EVoxelSurfaceRuleOp Op);
    static const TCHAR* GetConditionOpName(EVoxelSurfaceConditionOp Op);
};
