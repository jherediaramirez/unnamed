#pragma once

#include "CoreMinimal.h"
#include "Density/VoxelDensityEvaluator.h"
#include "Random/VoxelRandom.h"
#include "Surface/VoxelSurfaceContext.h"

// One ore_vein rule (worldgen/material_rule, phase 6 S4): its blocks, its density functions' nodes in the vein graph
// and the heights where its density can be positive.
struct FVoxelOreVein
{
    // Nodes in the graph given to FVoxelOreVeins::Compile: the vein's density (a block where it is positive, with that
    // chance), its richness (the chance of ore) and its filler gap (filler instead of ore where it is positive).
    int32 Density = INDEX_NONE;
    int32 Richness = INDEX_NONE;
    int32 Gap = INDEX_NONE;
    uint16 Ore = 0;
    uint16 RawOre = 0;
    uint16 Filler = 0;
    float RawOreChance = 0;
    // Set by Compile: the heights (Z) where the density's bounds allow a positive value, MinZ > MaxZ for none.
    int32 MinZ = 0;
    int32 MaxZ = -1;
};

// What a column's veins give: per vein, block column and height, whether the vein places a block there and which.
// Filled by FVoxelOreVeins::Generate for one generated column; read by the surface evaluator.
struct FVoxelOreVeinField
{
    // Per vein: its first height and its words per block column (each word holds 64 heights).
    struct FSpan
    {
        int32 Low = 0;
        int32 Words = 0;
        int32 Offset = 0;
    };
    TArray<FSpan> Spans;
    // Per vein, block column (local Y * 16 + local X) and word: the heights it gives a block, of those the ore and of
    // those the raw ore (three words in a row).
    TArray<uint64> Bits;
    // The block columns where any vein gives a block (bit local Y * 16 + local X): the others skip the lookups.
    uint64 Columns[4] = {};
    // Whether Generate has filled it for the current column (PrepareChunk clears it).
    bool bFilled = false;
    // Counts from the last Generate: cells whose blocks were evaluated (shortcut on), corners evaluated per
    // interpolated node, blocks evaluated, gap evaluations and blocks given.
    int32 OpenCells = 0, Corners = 0, Blocks = 0, Gaps = 0, Given = 0;
};

// Working memory for FVoxelOreVeins, one per thread.
struct FVoxelOreVeinScratch
{
    FVoxelDensityScratch Corners, Blocks, Gaps;
    TArray<FVoxelDensityBounds> Bounds;
    TArray<double> X, Y, Z;
    TArray<double> GapX, GapY, GapZ;
    // Per interpolated node, its input at every corner of the column's span.
    TArray<float> CornerValues;
    TArray<uint8> CellOpen, CornerNeeded;
    TArray<int32> CornerPoints;
    // Per block evaluated: its block column and height; per gap evaluation: the block, vein and outcome.
    TArray<int32> BlockCells, BlockZ;
    TArray<int32> GapBlocks, GapVeins;
    TArray<uint8> GapOutcomes;
};

// The ore_vein rules of a surface rule tree, evaluated as 26.3's material rule does through our own noise and draws.
//
// The rule (black-boxed against facts/ore_vein_samples.json; Docs/VoxelWorld.md, Ore veins): where the vein's density
// is positive, a block is placed with the density's value as the chance (0.7 in the data); it is ore with the
// richness as the chance, unless the filler gap is positive, and filler otherwise; ore is raw ore with RawOreChance.
// Elsewhere the rule gives nothing and the tree runs on. The three draws are ours: one stream seeded by
// Hash64(seed, "ore_vein", x, y, z), the same for every vein at a block.
//
// The shortcut is exact: each vein's density is evaluated only at the heights where its bounds allow a positive
// value (copper 0 to 49 and iron -60 to -9 in 26.3's data). The interpolated inputs are evaluated at the corners of
// 4 x 8 x 4 cells one node at a time, and a cell whose bounds from the corners known so far prove every density <= 0
// is not evaluated further; the rest are evaluated per solid block, and the gap only where the richness draw passes.
// Interpolated values are VoxelDensity::Trilinear's, so every value equals direct evaluation's bit for bit. With the
// shortcut off, every solid block of the column is evaluated directly (FVoxelDensityEvaluator::Evaluate): the oracle.
//
// Immutable after Compile; any thread may generate with its own scratch and field.
class UNNAMEDVOXEL_API FVoxelOreVeins
{
public:
    // At most this many veins (the surface evaluator keeps them in a byte per height).
    static constexpr int32 MaxVeins = 8;
    static constexpr uint64 Salt = VoxelRandom::Salt("ore_vein");

    enum class EOutcome : uint8
    {
        Nothing,
        Filler,
        Ore,
        RawOre
    };

    // Compiles Veins' functions (nodes of Graph) and finds their heights. Fails when a node is missing, interpolated
    // nodes disagree on cell size or nest, or there are more than MaxVeins.
    bool Compile(const FVoxelDensityGraph& Graph, uint64 WorldSeed, TConstArrayView<FVoxelOreVein> Veins,
        FString* OutError = nullptr);
    bool IsCompiled() const { return bCompiled; }
    int32 Num() const { return Veins.Num(); }
    const FVoxelOreVein& GetVein(int32 Index) const { return Veins[Index]; }
    const FVoxelDensityEvaluator& GetEvaluator() const { return Evaluator; }

    // Fills Field for the generated column whose lowest corner is (X0, Y0): every vein's block at every solid block
    // (Kinds per block column, local Y * 16 + local X, bottom-up from VoxelConstants::MinZ, as FillWithRules lays them
    // out).
    void Generate(int32 X0, int32 Y0, TConstArrayView<EVoxelSurfaceKind> Kinds, bool bShortcut,
        FVoxelOreVeinScratch& Scratch, FVoxelOreVeinField& Field) const;

    // The vein's block at a block of the field's column: its state, or -1 for none.
    static int32 Get(const FVoxelOreVein& Vein, const FVoxelOreVeinField& Field, int32 VeinIndex, int32 Cell, int32 Z)
    {
        const FVoxelOreVeinField::FSpan& Span = Field.Spans[VeinIndex];
        const uint32 Bit = uint32(Z - Span.Low);
        if (Bit >= uint32(Span.Words * 64)) return -1;
        const uint64* Words = Field.Bits.GetData() + Span.Offset + (Cell * Span.Words + int32(Bit >> 6)) * 3;
        const uint64 Mask = 1ull << (Bit & 63);
        if (!(Words[0] & Mask)) return -1;
        return !(Words[1] & Mask) ? Vein.Filler : (Words[2] & Mask) ? Vein.RawOre : Vein.Ore;
    }

    // The rule's draws at a block (our axes) from its density and richness: nothing, filler, or ore or raw ore (which
    // become filler where the gap is positive). Public for the tests.
    static EOutcome Draw(
        const FVoxelOreVein& Vein, uint64 WorldSeed, int32 X, int32 Y, int32 Z, float Density, float Richness);
    // Ore and raw ore stay where the gap is not positive; otherwise filler.
    static EOutcome ApplyGap(EOutcome Outcome, float Gap)
    {
        return (Outcome == EOutcome::Ore || Outcome == EOutcome::RawOre) && Gap > 0 ? EOutcome::Filler : Outcome;
    }

private:
    int32 CornerIndex(int32 CX, int32 CY, int32 CZ) const { return (CY * CornersXZ + CZ) * CornersXZ + CX; }
    void Prepare(FVoxelOreVeinField& Field, bool bShortcut) const;
    void Write(FVoxelOreVeinField& Field, int32 VeinIndex, int32 Cell, int32 Z, EOutcome Outcome) const;
    void GenerateDirect(int32 X0, int32 Y0, TConstArrayView<EVoxelSurfaceKind> Kinds, FVoxelOreVeinScratch& Scratch,
        FVoxelOreVeinField& Field) const;
    // Evaluates the gaps queued in Scratch and writes the queued blocks.
    void FinishGaps(FVoxelOreVeinScratch& Scratch, FVoxelOreVeinField& Field) const;

    FVoxelDensityEvaluator Evaluator;
    TArray<FVoxelOreVein> Veins;
    uint64 Seed = 0;
    bool bCompiled = false;
    int32 CellXZ = 4, CellY = 8;
    int32 CornersXZ = 5;
    // Nodes in evaluation order: per interpolated node, what its input reads (evaluated at corners); above the
    // interpolated nodes, what the densities and richnesses read (per block) and what the densities alone read (bounded
    // per cell); everything the gaps read (per block, directly).
    TArray<int32> InterpolatedNodes;
    TArray<TArray<int32>> CornerNodes;
    TArray<int32> BlockNodes, DensityNodes, GapNodes;
    // Per interpolated node: its input's bounds over every height, for cells whose corners are not evaluated yet.
    TArray<FVoxelDensityBounds> InputBounds;
    // The heights any vein can give a block at.
    int32 SpanLow = 0, SpanHigh = -1;
};
