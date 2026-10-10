#pragma once

#include "CoreMinimal.h"
#include "VoxelConstants.h"

class FVoxelBlockTable;
class FVoxelFeatureGraph;

// A set of blocks (FVoxelStateInfo::Block), one bit each.
struct UNNAMEDVOXEL_API FVoxelBlockSet
{
    TArray<uint64> Words;

    void Init(int32 BlockLimit, bool bAll);
    bool Contains(int32 Block) const
    {
        const int32 Word = Block >> 6;
        return uint32(Word) < uint32(Words.Num()) && (Words.GetData()[Word] >> (Block & 63) & 1) != 0;
    }
    void Add(int32 Block) { Words[Block >> 6] |= uint64(1) << (Block & 63); }
    bool operator==(const FVoxelBlockSet& Other) const { return Words == Other.Words; }
};

// The block tags of a feature graph, expanded against a block table and cached per tag: tag_match's sets, and later
// the replaceable tags. Block ids the table lacks (decoration blocks not imported yet) are left out and counted.
class UNNAMEDVOXEL_API FVoxelBlockTags
{
public:
    FVoxelBlockTags(const FVoxelFeatureGraph& InGraph, const FVoxelBlockTable& InTable);

    // The tag's blocks by full id ("minecraft:base_stone_overworld"), or null when the graph has no such tag.
    const FVoxelBlockSet* Find(FStringView Tag, FString* OutError = nullptr);
    // The block a block id names, or INDEX_NONE (counted as missing).
    int32 FindBlock(FStringView Id);
    int32 GetBlockLimit() const { return BlockLimit; }
    // Distinct block ids the tags and rules named that the table lacks.
    const TSet<FString>& GetMissing() const { return Missing; }

private:
    const FVoxelFeatureGraph& Graph;
    const FVoxelBlockTable& Table;
    int32 BlockLimit = 1;
    TMap<int32, FVoxelBlockSet> Sets;
    TSet<FString> Missing;
};

// One rule test (an ore target's test), compiled to bands of Z: within a band the answer depends on the block alone,
// so a test is one table lookup and one bit. The seven rule tests the ores use: always_true, block_match, tag_match,
// height_match, not, all_of and any_of.
class UNNAMEDVOXEL_API FVoxelRuleTest
{
public:
    // Fails, naming the type, on a rule test outside the seven or a tag the graph lacks.
    bool Compile(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags, FString* OutError = nullptr);

    bool Test(int32 Block, int32 Z) const
    {
        if (Z < VoxelConstants::MinZ || Z > VoxelConstants::MaxZ) return false;
        return Sets.GetData()[BandOfZ[Z - VoxelConstants::MinZ]].Contains(Block);
    }
    // Whether any band holds Block (air: whether the test can ever pass on air).
    bool CanMatch(int32 Block) const;
    int32 GetBandCount() const { return Sets.Num(); }

private:
    TArray<FVoxelBlockSet> Sets;
    uint8 BandOfZ[VoxelConstants::Height] = {};
};
