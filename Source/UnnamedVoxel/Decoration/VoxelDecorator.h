#pragma once

#include "CoreMinimal.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Decoration/Features/VoxelOreFeature.h"
#include "Decoration/Features/VoxelPatchFeatures.h"
#include "Decoration/Features/VoxelTreeFeature.h"
#include "Decoration/VoxelBlockPredicates.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelNumberProviders.h"
#include "Decoration/VoxelRuleTests.h"
#include "Decoration/VoxelStateProviders.h"
#include "Decoration/VoxelWriteLog.h"
#include "Noise/VoxelGradientNoise.h"

class FVoxelBiomeTable;
class FVoxelBlockTable;
class FVoxelColumn;
class FVoxelFeatureGraph;
class IVoxelTerrain;

// Test-only changes that must break the checks named beside them (scout C, recommendation 3). The other two of the
// four, a merge in arrival order and a Decorate that reads a neighbour's decorated state, belong to whoever schedules
// the stages: Finalize's ArrivalOrder, and the columns a Decorate is handed.
enum class EVoxelDecorationMutation : uint8
{
    None,
    // One random stream runs through every feature of a column: Decoration.SaltStability fails.
    SharedStream,
    // The first two features of the ore step trade order indices: Decoration.Golden and the vanilla order fail.
    SwappedOrder,
    // The survival table leaves supports_vegetation empty: the plant survival tests fail (Nyx's condition 3).
    DroppedSupportTag,
    // Writes stop raising the live heightmaps: Plants.LiveHeightmaps fails.
    FrozenHeightmaps
};

struct FVoxelDecorationOptions
{
    // Scout E's early-out: an ore try whose blob lies wholly above the highest block of every column it overlaps is
    // skipped. Exact, because ores whose targets cannot match air replace nothing there, and every try has its own
    // stream.
    bool bEarlyOut = true;
    // Discard on air exposure; off only for the ore histogram test.
    bool bDiscard = true;
    // voxel.Decoration.FreezeLast: the freeze pass runs at Finalize, after the merge (S3, plan 3.4), so snow lands on
    // decoration. Off: S2's order, the freeze inside the snapshot (a control that reproduces S2's hashes).
    bool bFreezeLast = true;
    // voxel.Decoration.Plants: the vegetation feature types (S8) compile and run. Off: only S2's ore features, so the
    // goldens and hashes from before S8 reproduce exactly.
    bool bPlants = true;
    // The seven placed features S8's types complete that belong to S11 and S12 (lush caves x3, cave vines, spore
    // blossom, sculk vein, ice patch): off until those sessions (scout I's question 1, recommendation b).
    bool bLaterPlants = false;
    // voxel.Decoration.Trees: the tree feature (S9a) compiles with its modifiers (surface_water_depth_filter,
    // environment_scan); inside a selector, an entry of a type S9b builds (fallen trees, huge mushrooms) places
    // nothing, and a tree's decorators are left out, all counted by Describe. Off: trees are skipped as before S9a, so
    // S8's goldens and hashes reproduce. Needs bPlants.
    bool bTrees = true;
    // When set, only the placed features it accepts (by full id) run.
    TFunction<bool(FStringView)> Filter;
    EVoxelDecorationMutation Mutation = EVoxelDecorationMutation::None;
};

struct FVoxelDecorateStats
{
    // Placed features run, ore tries reaching the feature, tries the early-out skipped, blocks placed.
    int32 Features = 0, Tries = 0, Skipped = 0, Placed = 0;
    // Writes logged, and cells dropped outside the 3x3.
    int32 Writes = 0, Dropped = 0;
    // Plants (S8): positions reaching a vegetation feature, and the blocks they placed.
    int32 PlantTries = 0, PlantBlocks = 0;
    // Cells in the working copy, its memory, and the log's.
    int32 WorkingCells = 0;
    SIZE_T WorkingBytes = 0, LogBytes = 0;
};

struct FVoxelFinalizeStats
{
    int32 Writes = 0, Applied = 0, Sections = 0, HeightCells = 0;
    // Plant writes the replay refused: the cell changed since its Decorate, or the support did.
    int32 Refused = 0;
};

// The decoration core (phase 6, S2): the placed features of a world's biomes compiled from the feature graph, run as
// Decorate (one column's features against its 3x3 snapshots, giving a write log in 9 buckets) and Finalize (a
// column's snapshot plus the 9 buckets aimed at it, merged in a fixed order). Both are pure functions of the seed,
// the data and the columns, so any worker can run any stage in any order. Built: ores (S2) and the vegetation types
// (S8); placed features of other types are skipped and listed by Describe. Immutable after Initialize, and never
// moved after it (its parts point at each other).
class UNNAMEDVOXEL_API FVoxelDecorator
{
public:
    FVoxelDecorator();
    ~FVoxelDecorator();
    FVoxelDecorator(const FVoxelDecorator&) = delete;
    FVoxelDecorator& operator=(const FVoxelDecorator&) = delete;

    // Compiles Graph for the biomes of Biomes (matched by full id) with the block states of Table, which must outlive
    // the decorator. Fails on data a compiled type cannot read; a placed feature of a type not built yet is skipped.
    bool Initialize(const FVoxelFeatureGraph& Graph, const FVoxelBlockTable& Table, const FVoxelBiomeTable& Biomes,
        uint64 WorldSeed, const FVoxelDecorationOptions& Options = {}, FString* OutError = nullptr);
    bool IsInitialized() const { return Table != nullptr; }

    // Decorate (plan 3.1): runs every placed feature some biome of the 3x3 lists, in the global order, against the
    // snapshots (Snapshots[FVoxelWriteLog::GetBucket(DX, DY)] is the column at Column + (DX, DY)).
    void Decorate(FIntPoint Column, TConstArrayView<const FVoxelColumn*> Snapshots, FVoxelWriteLog& Out,
        FVoxelDecorateStats* Stats = nullptr) const;
    // Finalize: Snapshot plus every write aimed at it, Incoming[FVoxelWriteLog::GetBucket(DX, DY)] being the bucket
    // from the source at the snapshot's position + (DX, DY) (null for none), replayed in merge order. ArrivalOrder,
    // for the mutation check only, replays whole buckets one after another in that order instead.
    void Finalize(const FVoxelColumn& Snapshot, TConstArrayView<const FVoxelWriteBucket*> Incoming, FVoxelColumn& Out,
        FVoxelFinalizeStats* Stats = nullptr, TConstArrayView<int32> ArrivalOrder = {}) const;

    // The snapshot a column's Decorate reads and the pass that finishes a final column after Finalize, in the order
    // bFreezeLast picks: Terrain's GenerateSnapshot and FinishColumn, or (S2's order) Generate and nothing.
    void GenerateSnapshot(
        const IVoxelTerrain& Terrain, FIntPoint Column, FVoxelColumn& Out, TArray<uint16>& Dense) const;
    void FinishColumn(const IVoxelTerrain& Terrain, FVoxelColumn& Column) const;
    bool IsFreezeLast() const { return Options.bFreezeLast; }

    // One column decorated alone (design B, the oracle): its 5x5 snapshots generated, its 3x3 decorated, then
    // finalized and finished. Equal to the column any schedule of the stages gives.
    void DecorateAlone(const IVoxelTerrain& Terrain, FIntPoint Column, FVoxelColumn& Out) const;

    // The placed features compiled, per step, and those skipped by type.
    FString Describe() const;
    const FVoxelBlockTable& GetTable() const { return *Table; }
    // Every state a compiled ore can place with its block id, in first-seen order (the ore histogram's columns).
    TArray<TPair<uint16, FString>> GetOreStates() const;
    // The placed features compiled, in run order, with their merge keys (FVoxelWriteBucket::MakeKey).
    struct FFeatureInfo
    {
        FString Id;
        int32 Step = 0;
        int32 IndexInStep = 0;
        uint16 Key = 0;
    };
    TArray<FFeatureInfo> GetFeatureInfos() const;
    // The plant survival table, the predicates and the providers the plants compiled to (for the tests).
    const FVoxelPlantSurvival& GetSurvival() const { return Survival; }
    const FVoxelStateProviders& GetProviders() const { return Providers; }
    // The skipped placed features' reasons with their counts.
    const TMap<FString, int32>& GetSkipped() const { return Skipped; }
    // Trees (S9a): the configurations compiled, the selector entries of S9b's types placing nothing, and the tree
    // decorators left out, by type.
    TConstArrayView<FVoxelTreeConfig> GetTrees() const { return Trees; }
    const TMap<FString, int32>& GetPlaceholders() const { return Placeholders; }
    const TMap<FString, int32>& GetSkippedDecorators() const { return SkippedDecorators; }
    // The noise the count modifiers read (noise_threshold_count at X / 200, Y / 200).
    float SampleCountNoise(double X, double Y) const { return CountNoise.Sample(X, Y); }

private:
    struct FModifier
    {
        enum class EType : uint8
        {
            Count,
            InSquare,
            HeightRange,
            RarityFilter,
            Biome,
            // S8:
            Heightmap,
            Offset,
            Filter,
            NoiseThresholdCount,
            NoiseBasedCount,
            SurfaceRelative,
            // S9a:
            SurfaceWaterDepth,
            EnvironmentScan
        };
        EType Type = EType::Count;
        FVoxelIntProvider Provider;
        int32 Chance = 1;
        // Heightmap, SurfaceRelative: the map.
        EVoxelDecorationHeightmap Map = EVoxelDecorationHeightmap::WorldSurfaceWG;
        // Offset: X, Y and Z (the reference's x, z and y); Provider is X.
        FVoxelIntProvider OffsetY, OffsetZ;
        // Filter: a predicate root.
        int32 Predicate = INDEX_NONE;
        // NoiseThresholdCount: level, below and above (in Chance and Below); NoiseBasedCount: ratio, factor, offset;
        // SurfaceRelative: Below and Above as the inclusive bounds.
        double Level = 0, Ratio = 0, Factor = 1, NoiseOffset = 0;
        int32 Below = 0, Above = 0;
        // SurfaceWaterDepth: Above is the largest depth. EnvironmentScan: the Z step (1 up, -1 down), the steps, the
        // target (Predicate) and the optional condition each cell walked onto must meet unless it is the target.
        int32 ScanStep = 0, ScanSteps = 0, ScanAllowed = INDEX_NONE;
    };

    // A nested placed feature (selectors, a patch's vegetation): its modifiers (never biome) and its feature.
    struct FPlaced
    {
        TArray<FModifier> Modifiers;
        int32 Plant = INDEX_NONE;
    };

    struct FFeature
    {
        FString Id;
        int32 Step = 0;
        int32 IndexInStep = 0;
        uint16 Key = 0;
        uint64 Salt = 0;
        TArray<FModifier> Modifiers;
        // The biome modifier comes last, so the early-out can run there on the try's final position.
        bool bPruneAtBiome = false;
        int32 Ore = INDEX_NONE;
        // A vegetation feature (S8), an index into Plants.
        int32 Plant = INDEX_NONE;
    };

    struct FOre
    {
        FVoxelOreConfig Config;
        VoxelOreFeature::FReach Reach;
        // Some target matches air somewhere, so the early-out would not be exact.
        bool bTargetsAir = false;
    };

    bool CompileFeature(const FVoxelFeatureGraph& Graph, int32 Entry, FVoxelBlockTags& Tags, FFeature& Out,
        FString& OutSkip, FString* OutError);
    // The placement list of placed feature Node; bNested refuses biome. A type not built leaves OutSkip set.
    bool CompileModifiers(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags, bool bNested,
        TArray<FModifier>& Out, FString& OutSkip, FString* OutError);
    // A vegetation feature node, and a nested placed feature value, compiled once each.
    bool CompilePlant(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags, int32& OutPlant,
        FString& OutSkip, FString* OutError);
    bool CompilePlaced(const FVoxelFeatureGraph& Graph, const struct FVoxelFeatureValue& Value, FVoxelBlockTags& Tags,
        int32& OutPlaced, FString& OutSkip, FString* OutError);
    // Prune(Position) runs where the biome modifier would look the biome up, and drops the try when it returns true.
    template <typename FPrune, typename FVisit>
    void RunModifiers(TConstArrayView<FModifier> Modifiers, bool bPruneAtBiome, int32 FeatureIndex, int32 Modifier,
        FIntVector Position, uint64 Seed, const FVoxelDecorationContext& Context, FPrune&& Prune, FVisit&& Visit) const;
    // Runs vegetation feature Plant at Position with a stream from Seed, and a nested placed feature.
    void PlacePlant(int32 Plant, FIntVector Position, uint64 Seed, const FVoxelPlantEnvironment& Env,
        FVoxelDecorateStats& Stats) const;
    void RunPlaced(int32 Placed, FIntVector Position, uint64 Seed, const FVoxelPlantEnvironment& Env,
        FVoxelDecorateStats& Stats) const;
    // Scout E's early-out for one try of an ore.
    bool IsAboveEverything(
        const FOre& Ore, FIntPoint Column, FIntVector Position, const class FVoxelDecorationContext& Context) const;

    const FVoxelBlockTable* Table = nullptr;
    uint64 WorldSeed = 0;
    FVoxelDecorationOptions Options;
    // In run order: by step, then index in the step.
    TArray<FFeature> Features;
    TArray<FOre> Ores;
    TArray<TArray<FVoxelOreTarget>> TargetLists;
    // Per biome of the terrain's table: one bit per feature that biome lists.
    TArray<TBitArray<>> FeaturesOfBiome;
    // Skipped placed features: why, and how many.
    TMap<FString, int32> Skipped;
    TSet<FString> MissingBlocks;

    // Plants (S8).
    FVoxelPlantSurvival Survival;
    FVoxelBlockPredicates Predicates;
    FVoxelStateProviders Providers;
    TArray<FVoxelPlantFeature> Plants;
    TArray<FPlaced> Placed;
    TMap<uint16, uint16> UpperOf;
    // noise_threshold_count's and noise_based_count's noise: ours, seeded from the world seed (Jesus's answer 4).
    FVoxelGradientNoise CountNoise{0};
    // Compile memos: graph node to plant or nested placed feature.
    TMap<int32, int32> PlantOfNode, PlacedOfNode;

    // Trees (S9a).
    TArray<FVoxelTreeConfig> Trees;
    TMap<FString, int32> Placeholders, SkippedDecorators;
};
