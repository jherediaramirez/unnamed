#pragma once

#include "CoreMinimal.h"
#include "Decoration/Features/VoxelOreFeature.h"
#include "Decoration/VoxelNumberProviders.h"
#include "Decoration/VoxelRuleTests.h"
#include "Decoration/VoxelWriteLog.h"

class FVoxelBiomeTable;
class FVoxelBlockTable;
class FVoxelColumn;
class FVoxelFeatureGraph;

// Test-only changes that must break the checks named beside them (scout C, recommendation 3). The other two of the
// four, a merge in arrival order and a Decorate that reads a neighbour's decorated state, belong to whoever schedules
// the stages: Finalize's ArrivalOrder, and the columns a Decorate is handed.
enum class EVoxelDecorationMutation : uint8
{
    None,
    // One random stream runs through every feature of a column: Decoration.SaltStability fails.
    SharedStream,
    // The first two features of the ore step trade order indices: Decoration.Golden and the vanilla order fail.
    SwappedOrder
};

struct FVoxelDecorationOptions
{
    // Scout E's early-out: an ore try whose blob lies wholly above the highest block of every column it overlaps is
    // skipped. Exact, because ores whose targets cannot match air replace nothing there, and every try has its own
    // stream.
    bool bEarlyOut = true;
    // Discard on air exposure; off only for the ore histogram test.
    bool bDiscard = true;
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
    // Cells in the working copy, its memory, and the log's.
    int32 WorkingCells = 0;
    SIZE_T WorkingBytes = 0, LogBytes = 0;
};

struct FVoxelFinalizeStats
{
    int32 Writes = 0, Applied = 0, Sections = 0, HeightCells = 0;
};

// The decoration core (phase 6, S2): the placed features of a world's biomes compiled from the feature graph, run as
// Decorate (one column's features against its 3x3 snapshots, giving a write log in 9 buckets) and Finalize (a
// column's snapshot plus the 9 buckets aimed at it, merged in a fixed order). Both are pure functions of the seed,
// the data and the columns, so any worker can run any stage in any order. Only the ore feature type is built so far;
// placed features of other types are skipped and listed by Describe. Immutable after Initialize.
class UNNAMEDVOXEL_API FVoxelDecorator
{
public:
    FVoxelDecorator();
    ~FVoxelDecorator();

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

    // One column decorated alone (design B, the oracle): its 5x5 snapshots from GetSnapshot, its 3x3 decorated, then
    // finalized. Equal to the column any schedule of the stages gives.
    using FGetSnapshot = TFunctionRef<TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>(FIntPoint)>;
    void DecorateAlone(FIntPoint Column, FGetSnapshot GetSnapshot, FVoxelColumn& Out) const;

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

private:
    struct FModifier
    {
        enum class EType : uint8
        {
            Count,
            InSquare,
            HeightRange,
            RarityFilter,
            Biome
        };
        EType Type = EType::Count;
        FVoxelIntProvider Provider;
        int32 Chance = 1;
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
    // Prune(Position) runs where the biome modifier would look the biome up, and drops the try when it returns true.
    template <typename FPrune, typename FVisit>
    void RunModifiers(const FFeature& Feature, int32 FeatureIndex, int32 Modifier, FIntVector Position, uint64 Seed,
        const class FVoxelDecorationContext& Context, FPrune&& Prune, FVisit&& Visit) const;
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
};
