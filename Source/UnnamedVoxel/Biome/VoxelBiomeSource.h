#pragma once

#include "CoreMinimal.h"
#include "Biome/VoxelBiomeTable.h"
#include "Terrain/VoxelClimate.h"

class FVoxelBiomeStorage;
struct FVoxelTerrainPreset;

// A climate target in the table's quantized units (FVoxelBiomeTable::Quantize), in EVoxelClimateAxis order.
struct FVoxelBiomeTarget
{
    int64 Values[int32(EVoxelClimateAxis::Count)] = {};

    int64& operator[](EVoxelClimateAxis Axis) { return Values[int32(Axis)]; }
    int64 operator[](EVoxelClimateAxis Axis) const { return Values[int32(Axis)]; }
};

// The parameter point that fits a target best, and its fitness.
struct FVoxelBiomeMatch
{
    int32 Point = INDEX_NONE;
    int64 Fitness = MAX_int64;

    // Lower fitness wins; of equal fitness, the earlier point in list order.
    bool IsBetterThan(int64 OtherFitness, int32 OtherPoint) const
    {
        return Fitness < OtherFitness || (Fitness == OtherFitness && Point < OtherPoint);
    }
};

// How a column's biomes are filled: the pruned search (the default), the brute-force scan it must equal, or not at all.
enum class EVoxelBiomeFill : uint8
{
    Off,
    Pruned,
    BruteForce
};

// A world type's biome source. Multi-noise picks, for each climate target, the parameter point with the smallest
// fitness: the sum over the six parameters of the squared distance outside each range, plus the point's offset
// squared. Ties go to the first point in list order (the jar's brute-force answer). A fixed source returns one biome
// everywhere (Single Biome). Immutable after Initialize; any thread may use it with its own scratch.
//
// Lookup: the points fall into a few groups that share one depth range (26.3's overworld list: 4), so inside a group
// the depth term is the same for every point. Per group, a bounding-box tree over the other five parameters finds the
// best point exactly; a target's answer is the best group after adding each group's depth term. A quart column
// searches each group once and then only compares the group winners at each of its 96 depths.
class UNNAMEDVOXEL_API FVoxelBiomeSource
{
public:
    // A multi-noise source over Preset's climate for one world seed. Fails, with a reason, when the table is invalid
    // or the preset's depth is not offset plus a y gradient.
    bool Initialize(const FVoxelBiomeTable& InTable, const FVoxelTerrainPreset& Preset, uint64 WorldSeed,
        FString* OutError = nullptr);
    // One biome everywhere.
    bool InitializeFixed(const FVoxelBiomeTable& InTable, int32 Biome, FString* OutError = nullptr);

    bool IsInitialized() const { return FixedBiome != INDEX_NONE || !Groups.IsEmpty(); }
    bool IsFixed() const { return FixedBiome != INDEX_NONE; }
    const FVoxelBiomeTable& GetTable() const { return Table; }
    uint64 GetSeed() const { return Seed; }

    // A point's fitness for a target: what both searches minimize.
    static int64 GetFitness(const FVoxelBiomeParameter& Point, const FVoxelBiomeTarget& Target);

    // The best point for a target. A fixed source answers no point (INDEX_NONE) and fitness 0; GetBiome gives its
    // biome.
    FVoxelBiomeMatch Find(const FVoxelBiomeTarget& Target) const;
    // The same by scanning every point; Find must equal it.
    FVoxelBiomeMatch FindBruteForce(const FVoxelBiomeTarget& Target) const;
    // The biome index of a match or of a point.
    int32 GetBiome(const FVoxelBiomeMatch& Match) const;

    // The quantized targets at Count block positions (X[I], Y[I]) and heights Z[I]; a null Z puts each target at depth
    // 0, the climate's own surface (where the depth gradient cancels the offset).
    void SampleTargets(const double* X, const double* Y, const double* Z, int32 Count, FVoxelDensityScratch& Scratch,
        TArrayView<FVoxelBiomeTarget> Out) const;

    // Fills Out for the column at column coordinates Column: its 5 x 5 quart columns (its own 4 x 4 plus the next
    // column's first row and column) at every quart height. Each quart's biome is the one at its minimum corner.
    void FillColumn(FIntPoint Column, FVoxelDensityScratch& Scratch, FVoxelBiomeStorage& Out,
        EVoxelBiomeFill Fill = EVoxelBiomeFill::Pruned) const;

    // The router's depth at block height Z for a quart column's offset, computed as the density evaluator computes
    // the depth function (its y gradient plus the offset), so it equals FVoxelClimateSampler::SampleDepth bit for bit.
    float GetDepth(float Offset, double Z) const;

    // How many depth groups the points form.
    int32 GetGroupCount() const { return Groups.Num(); }
    // The climate the source reads (initialized only for a multi-noise source).
    const FVoxelClimateSampler& GetClimate() const { return Climate; }

private:
    // A point as the group trees keep it: the five non-depth ranges (temperature, humidity, continentalness, erosion,
    // weirdness) and its offset squared.
    struct FFlatPoint
    {
        int64 Min[5];
        int64 Max[5];
        int64 OffsetSquared;
        int32 Index;
    };
    // A tree node: the box around its points' ranges, the smallest offset squared and the smallest list index below
    // it. A leaf (Left INDEX_NONE) holds Count points from First; an inner node's children are Left and Left + 1.
    struct FNode
    {
        int64 Min[5];
        int64 Max[5];
        int64 MinOffsetSquared;
        int32 MinIndex;
        int32 Left = INDEX_NONE;
        int32 First = 0;
        int32 Count = 0;
    };
    struct FGroup
    {
        int64 DepthMin = 0, DepthMax = 0;
        TArray<FFlatPoint> Points;
        TArray<FNode> Nodes;
    };

    bool InitializeTable(const FVoxelBiomeTable& InTable, FString* OutError);
    void BuildGroups();
    // Fills Group.Nodes[Node] over Count points from First, splitting it further when it holds more than a leaf.
    static void BuildNode(FGroup& Group, int32 Node, int32 First, int32 Count);
    // The best point of a group for a five-parameter target, starting from Hint (a point of the group, or INDEX_NONE).
    FVoxelBiomeMatch FindInGroup(const FGroup& Group, const int64 Flat[5], int32 Hint) const;
    // The best match over the group winners for a depth.
    FVoxelBiomeMatch PickGroup(const FVoxelBiomeMatch* Winners, int64 Depth) const;

    FVoxelBiomeTable Table;
    uint64 Seed = 0;
    int32 FixedBiome = INDEX_NONE;
    TArray<FGroup> Groups;
    FVoxelClimateSampler Climate;
    // The depth gradient's from y, from value, to y and to value.
    double DepthGradient[4] = {};
};
