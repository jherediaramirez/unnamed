#pragma once

#include "CoreMinimal.h"
#include "Density/VoxelDensityGraph.h"
#include "Noise/VoxelNormalNoise.h"
#include "Surface/VoxelOreVeins.h"
#include "Surface/VoxelSurfaceContext.h"
#include "Surface/VoxelSurfaceRules.h"

class FVoxelBiomeStorage;
class FVoxelBiomeTable;
class FVoxelBlockTable;

// A world type's surface rules and the noises they read, as read from the reference.
struct FVoxelSurfaceInputs
{
    FVoxelSurfaceRules Rules;
    // Every noise GetNoiseIds names.
    TArray<FVoxelDensityNoise> Noises;
    // The density functions the ore_vein rules name, with everything they read (GetVeinFunctionIds).
    FVoxelDensityGraph Functions;
};

// What one generated column (16 x 16) shares between its block columns: its biome storage and, with the shortcut, the
// biomes each quart layer can give, the results per height for deep and interior stone, and the corners' jitters.
struct FVoxelSurfaceChunk
{
    // Deep: the rules need the full tree at this height. NoState: no rule gives a block (the stone stays).
    static constexpr int32 Deep = -2;
    static constexpr int32 NotComputed = -3;
    // At or below ResumeBase: the tree is decided up to instruction ResumeBase - value; run it from there.
    static constexpr int32 ResumeBase = -16;

    const FVoxelBiomeStorage* Biomes = nullptr;
    bool bShortcut = false;
    // Per quart layer (Z - MinZ) / 4: the biomes its 5 x 5 quarts hold, BiomeWords words each.
    TArray<uint64> LayerBiomes;
    // Per height (Z - MinZ): the result for a block below its column's preliminary surface, or where to resume.
    TArray<int32> DeepStates;
    // The heights (Z - MinZ, highest first) whose deep result is not NoState, and the highest height at and below
    // which no deep result's remaining instructions read depths or water.
    TArray<int32> ActiveDeep;
    int32 ContextFreeTop = -1;
    // The stone_depth conditions the tree can reach with the biomes this column holds, by surface type.
    TArray<int32> FloorConditions, CeilingConditions;
    // Filled as blocks need them. Per height: the result for an interior block (deeper in its run than any stone_depth
    // condition reaches) at or above its column's preliminary surface, Deep or NotComputed.
    mutable TArray<int32> InteriorStates;
    // The storage's corner jitters (FVoxelBiomeStorage::Volume) and which quart layers are filled.
    mutable TArray<FVector3f> Jitters;
    mutable TArray<bool> JitterLayers;
    // Scratch for the partial evaluation.
    mutable TArray<int32> Outcomes;
    // Counts since PrepareChunk: solid blocks decided by the deep and interior tables and through the tree, the
    // instructions run for those, and the heights the interior table was built for.
    mutable int32 DeepBlocks = 0, InteriorBlocks = 0, FullBlocks = 0, InteriorRuns = 0;
    mutable int64 Steps = 0;

    // Ore veins (phase 6, S4): whether the ore_vein rules run, and whether only at the heights their densities reach
    // (the vein shortcut; off evaluates every solid block directly, only to compare against).
    bool bVeins = false;
    bool bVeinShortcut = true;
    // This column's vein blocks (FVoxelSurfaceEvaluator::PrepareVeins).
    FVoxelOreVeinField Veins;
    FVoxelOreVeinScratch VeinScratch;
    // Per height, with the shortcut: the veins (a bit per vein, in program order) the deep or interior table passes
    // before its result. A block there takes the first of them that gives a block, and the table's result otherwise.
    TArray<uint8> DeepVeins;
    mutable TArray<uint8> InteriorVeins;
};

// Evaluates a surface rule tree (FVoxelSurfaceRules) as 26.3's surface pass does, through our own noise and hashes:
// at every solid block of a column, top down, the first rule that gives a block wins; otherwise the block stays. The
// conditions follow the black-boxed behaviour in Docs/Records/2026-10-09-Voxel-Phase5-Scout-Facts.md (sections 1.2 to
// 1.4). Compiled once per terrain into a linear program: a rule that gives nothing falls through to the next.
//
// Our own parts: the per-column jitter of the surface depth (a hash of seed and column), the vertical gradients
// (Hash64(seed, random_name, x, y, z)), the badlands band pattern (192 blocks from a seeded stream) and the ore veins'
// draws (FVoxelOreVeins). Veins run when the chunk asks for them (PrepareChunk); otherwise they give nothing, as
// before phase 6's S4.
//
// The deep-stone shortcut is exact: below a column's preliminary surface the surface branch cannot run, so per height
// the rest of the tree is decided once per generated column (y-only conditions by height, biome conditions by the
// biomes the quart layers hold). Heights it cannot decide, and blocks above, run the full tree. With bShortcut off
// every block runs it and reads its biome from the storage; both give the same blocks (test Surface.Exact). An ore
// vein decides per block, so the tables pass over it and note it: a block at that height asks its veins first.
//
// Immutable after Compile; any thread may evaluate with its own chunk and column values.
class UNNAMEDVOXEL_API FVoxelSurfaceEvaluator
{
public:
    // No rule gave a block.
    static constexpr int32 NoState = -1;
    // The band pattern's period in blocks.
    static constexpr int32 BandPeriod = 192;

    // The noise ids a tree needs: those its noise_threshold conditions name, plus the code side's minecraft:surface,
    // minecraft:surface_secondary and minecraft:clay_bands_offset.
    static TArray<FString> GetNoiseIds(const FVoxelSurfaceRules& Rules);
    // The density function ids its ore_vein rules name (density, richness and filler gap), each once.
    static TArray<FString> GetVeinFunctionIds(const FVoxelSurfaceRules& Rules);

    // Resolves blocks against Table (default states; minecraft:air is 0), biomes against Biomes (an id the table lacks
    // never matches), noises against Inputs.Noises and the veins' functions against Inputs.Functions. Fails on an
    // unknown block, a missing noise or function, or an invalid tree.
    bool Compile(const FVoxelSurfaceInputs& Inputs, const FVoxelBlockTable& Table, const FVoxelBiomeTable& Biomes,
        uint64 WorldSeed, FString* OutError = nullptr);
    bool IsCompiled() const { return bCompiled; }

    // Per generated column. Biomes may be null or empty (no biome matches). With bVeins, call PrepareVeins before the
    // first ApplyColumn.
    void PrepareChunk(FVoxelSurfaceChunk& Chunk, const FVoxelBiomeStorage* Biomes, bool bShortcut, bool bVeins = false,
        bool bVeinShortcut = true) const;
    // Per generated column, with veins: their blocks for the column whose lowest corner is (X0, Y0). Kinds are every
    // block column's, local Y * 16 + local X, bottom-up, as ApplyColumn reads them.
    void PrepareVeins(FVoxelSurfaceChunk& Chunk, int32 X0, int32 Y0, TConstArrayView<EVoxelSurfaceKind> Kinds) const;
    // Per block column (world X and Y): the surface depth from the surface noise, and the minimum surface level from P.
    // The chunk's reach for interior blocks is set when the column is applied.
    void PrepareColumn(FVoxelSurfaceColumn& Column, int32 X, int32 Y, double PreliminarySurface, bool bSteep) const;
    // Runs the rules down one block column. Kinds are bottom-up (index Z - MinZ); States hold the blocks before the
    // rules (stone where solid) at States[(Z - MinZ) * Stride] and get each solid block's rule result. LocalX and
    // LocalY place the column in its chunk for the biome lookup. Top, if known, is the highest block that is not air.
    void ApplyColumn(const FVoxelSurfaceChunk& Chunk, FVoxelSurfaceColumn& Column, int32 LocalX, int32 LocalY,
        TConstArrayView<EVoxelSurfaceKind> Kinds, TArrayView<uint16> States, int32 Stride = 1,
        int32 Top = VoxelConstants::MaxZ) const;

    // One solid block through the full tree: its state, or NoState.
    int32 Evaluate(const FVoxelSurfaceChunk& Chunk, FVoxelSurfaceColumn& Column, int32 LocalX, int32 LocalY,
        FVoxelSurfaceBlock& Block, int32 StartPc = 0) const;
    // One condition of the tree (an index into the rules' Conditions), for tests.
    bool TestCondition(int32 Condition, const FVoxelSurfaceChunk& Chunk, FVoxelSurfaceColumn& Column, int32 LocalX,
        int32 LocalY, FVoxelSurfaceBlock& Block) const;

    // A noise's slot in FVoxelSurfaceColumn::Noise, or INDEX_NONE.
    int32 FindNoiseSlot(FStringView Id) const;
    int32 GetSurfaceSlot() const { return SurfaceSlot; }
    int32 GetSecondarySlot() const { return SecondarySlot; }
    int32 GetBandsSlot() const { return BandsSlot; }
    // The badlands band at Z for a column's band offset; empty pattern (no bandlands rule) gives 0.
    uint16 GetBand(int32 Z, int32 BandOffset) const;
    TConstArrayView<uint16> GetBands() const { return Bands; }
    // The compiled program's length, for the docs and tests.
    int32 GetProgramSize() const { return Program.Num(); }
    const FVoxelOreVeins& GetOreVeins() const { return OreVeins; }

private:
    enum class EOp : uint8
    {
        // Condition holds == bExpect: next instruction; otherwise jump to Jump.
        Test,
        // The result is State.
        Give,
        // The result is the band at the block's height.
        Bands,
        // Ore vein State (its index in OreVeins): its block, if it gives one; otherwise the next instruction.
        Vein
    };
    struct FInstruction
    {
        EOp Op = EOp::Test;
        bool bExpect = true;
        uint16 State = 0;
        int32 Condition = INDEX_NONE;
        int32 Jump = INDEX_NONE;
    };
    struct FCondition
    {
        EVoxelSurfaceConditionOp Op = EVoxelSurfaceConditionOp::Steep;
        int32 Input = INDEX_NONE;
        int32 Noise = INDEX_NONE;
        bool bIs3D = false;
        bool bAddDepth = false;
        bool bCeiling = false;
        double Min = 0, Max = 0;
        uint64 Salt = 0;
        // Absolute heights: y_above's anchor, vertical_gradient's true-at-and-below and false-at-and-above.
        int32 Anchor = 0, FalseAnchor = 0;
        int32 Multiplier = 0;
        int32 Offset = 0;
        int32 SecondaryRange = 0;
        // Biome: the first of BiomeWords words in BiomeMasks.
        int32 Mask = INDEX_NONE;
    };

    double GetNoise(FVoxelSurfaceColumn& Column, int32 Slot) const;
    int32 GetBlockBiome(const FVoxelSurfaceChunk& Chunk, int32 LocalX, int32 LocalY, FVoxelSurfaceBlock& Block) const;
    // The biomes a block at Z can take: its quart layer's and the one above's.
    void GetLayerMask(const FVoxelSurfaceChunk& Chunk, int32 Z, uint64* Out) const;
    bool TestBiome(const FCondition& Data, const FVoxelSurfaceChunk& Chunk, int32 LocalX, int32 LocalY,
        FVoxelSurfaceBlock& Block) const;
    // 1 true, 0 false, -1 not decided by height and the layer's biomes alone. bAbove: at or above the preliminary
    // surface; bInterior: deeper than every stone_depth condition reaches.
    int32 TestPartial(int32 Condition, int32 Z, const uint64* LayerMask, bool bAbove, bool bInterior) const;
    // Below the preliminary surface: the tree walked forward to its result or to the first undecided instruction, and
    // the veins it passes.
    int32 RunDeep(const FVoxelSurfaceChunk& Chunk, int32 Z, const uint64* LayerMask, uint8& OutVeins) const;
    // Interior blocks at height Z: each instruction's outcome from the end back; an undecided test whose two ways end
    // alike is decided (jumps only go forward). Veins pass, noted in OutVeins; one past an undecided test whose ways
    // end alike leaves the height to the full tree.
    int32 RunInterior(const FVoxelSurfaceChunk& Chunk, int32 Z, uint8& OutVeins) const;
    // The veins (bits) the chunk runs at height Z.
    uint32 GetActiveVeins(const FVoxelSurfaceChunk& Chunk, int32 Z) const;
    // The first of Veins (bits, in program order) that gives a block at the block column Cell and height Z, or NoState.
    int32 GiveVein(const FVoxelSurfaceChunk& Chunk, uint32 Veins, int32 Cell, int32 Z) const;
    int32 GetStoneDepthLimit(const FCondition& Data, FVoxelSurfaceColumn& Column) const;
    // A biome condition against a set of biomes a block may take: 1 all in it, 0 none, -1 some.
    int32 DecideBiome(const FCondition& Data, const uint64* Mask) const;
    void BuildBands(const FVoxelBlockTable& Table, uint64 WorldSeed, FString& OutError);

    TArray<FInstruction> Program;
    TArray<FCondition> Conditions;
    // Per instruction: whether running the tree from it below the preliminary surface may read a block's depths or
    // water height.
    TArray<bool> NeedsContext;
    // Per instruction: the veins at or after it.
    TArray<uint8> VeinsFrom;
    FVoxelOreVeins OreVeins;
    TArray<uint64> BiomeMasks;
    int32 BiomeWords = 1;
    TArray<FVoxelNormalNoise> Noises;
    TArray<FString> NoiseIds;
    int32 SurfaceSlot = INDEX_NONE, SecondarySlot = INDEX_NONE, BandsSlot = INDEX_NONE;
    TArray<uint16> Bands;
    uint64 Seed = 0;
    bool bCompiled = false;
    bool bHasBands = false;
};
