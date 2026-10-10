#pragma once

#include "CoreMinimal.h"
#include "Async/Mutex.h"
#include "Decoration/VoxelNumberProviders.h"

class FVoxelFeatureGraph;

enum class EVoxelCarverKind : uint8
{
    Cave,
    Canyon
};

// One worldgen/carver entry as plain numbers (phase 6, S10), compiled from the decoration data. 26.3's carvers only
// mark positions; whoever writes the blocks decides what a marked cell becomes (FVoxelDensityTerrain: the aquifer).
struct UNNAMEDVOXEL_API FVoxelCarverConfig
{
    FString Id;
    EVoxelCarverKind Kind = EVoxelCarverKind::Cave;
    // A column starts this carver with this chance.
    double Probability = 0;
    // The start's height (both kinds).
    FVoxelIntProvider Y;
    // Caves: tunnel systems per start, and each system's shape.
    FVoxelIntProvider Count;
    FVoxelFloatProvider Thickness, FloorLevel, HorizontalRadius, VerticalRadius, RoomVerticalRadius;
    // Not in the 26.3 files; the codec's default is 1 (scout K's decoded records).
    FVoxelFloatProvider StartVerticalRadius = FVoxelFloatProvider::MakeConstant(1);
    bool bWeirdThicknessBias = false;
    // Canyons (Thickness is shape.thickness).
    FVoxelFloatProvider VerticalRotation, DistanceFactor, HorizontalRadiusFactor;
    int32 WidthSmoothness = 1;
    float VerticalRadiusDefault = 1, VerticalRadiusCenter = 0, YScale = 1;

    // Entry is an index into Graph's carver registry.
    static bool Compile(const FVoxelFeatureGraph& Graph, int32 Entry, FVoxelCarverConfig& Out, FString* OutError);
};

// One ellipsoid of a carver's path, in blocks on our axes (X, Y across, Z up): X and Y from the start column's lowest
// corner (exact floats within the reach), Z absolute.
struct FVoxelCarveStep
{
    float X = 0, Y = 0, Z = 0;
    float Horizontal = 1, Vertical = 1;
    // Caves: cells whose relative height is at or below this stay (the flat floor); -1 keeps every cell.
    float Floor = -1;
};

// Everything one start carves: its steps, and per column of its box the steps that reach that column.
struct UNNAMEDVOXEL_API FVoxelCarvePath
{
    EVoxelCarverKind Kind = EVoxelCarverKind::Cave;
    FIntPoint Start = FIntPoint::ZeroValue;
    TArray<FVoxelCarveStep> Steps;
    // Canyons: each Z's width factor (index Z - MinZ); empty for caves.
    TArray<float> Widths;
    // The columns the steps reach (inclusive), and per column (row-major from Min) a range of StepIndex.
    FIntPoint Min = FIntPoint::ZeroValue, Max = FIntPoint(-1, -1);
    TArray<int32> ColumnFirst;
    TArray<int32> StepIndex;
    // Steps left out because they would reach past FVoxelCarvers::Reach.
    int32 Dropped = 0;

    TConstArrayView<int32> GetSteps(FIntPoint Column) const;
    int64 GetAllocatedSize() const;
};

// Paths by (carver, start column), shared by every worker. Direct-mapped: a key takes its slot over from an older
// one, and a lost path is just built again, so results never depend on the cache (Carvers.CacheOff).
class UNNAMEDVOXEL_API FVoxelCarvePathCache
{
public:
    static constexpr int32 DefaultSlots = 4096;

    // Slots is rounded up to a power of two; tests use a few to force collisions.
    explicit FVoxelCarvePathCache(int32 Slots = DefaultSlots);
    TSharedPtr<const FVoxelCarvePath, ESPMode::ThreadSafe> Find(int32 Carver, FIntPoint Start) const;
    void Store(int32 Carver, FIntPoint Start, TSharedPtr<const FVoxelCarvePath, ESPMode::ThreadSafe> Path) const;
    void Reset() const;
    int64 GetAllocatedSize() const;

private:
    struct FSlot
    {
        mutable UE::FMutex Lock;
        int32 Carver = INDEX_NONE;
        FIntPoint Start = FIntPoint::ZeroValue;
        TSharedPtr<const FVoxelCarvePath, ESPMode::ThreadSafe> Path;
    };
    int32 GetSlot(int32 Carver, FIntPoint Start) const;
    int32 SlotCount = DefaultSlots;
    TUniquePtr<FSlot[]> Table;
};

// What one column's carving did.
struct FVoxelCarveStats
{
    int32 Starts = 0, PathsBuilt = 0, PathsFound = 0, Steps = 0, Marked = 0;
    double StartsMs = 0, PathsMs = 0, MarkMs = 0;
};

// The three carvers as a pull (phase 6, S10; plan 3.6): a column carves the parts of every path that starts within
// Reach columns of it. A start is Hash64(seed, "carver/<id>", start) < probability; its path is a pure function of
// the seed, the carver and the start column (our own walker, shaped after the wiki and fitted to scout K's black-box
// statistics; vanilla's random is not reproduced), so load order and the cache cannot change a block.
class UNNAMEDVOXEL_API FVoxelCarvers
{
public:
    // 2 x the jar's getRange() (4); the farthest chunk a start carved in 3,000 black-box starts was 7.
    static constexpr int32 Reach = 8;
    // The column's mark: one bit per block, in FVoxelColumn::GetDenseIndex order.
    static constexpr int32 MaskWords = 16 * 16 * 384 / 64;

    // The carvers every biome lists (the per-biome rule cannot change a block while all lists are equal, so a data
    // change that makes them differ fails here). Seed is the world seed.
    bool Initialize(const FVoxelFeatureGraph& Graph, uint64 InSeed, FString* OutError = nullptr);
    // From configs directly (tests).
    bool Initialize(TConstArrayView<FVoxelCarverConfig> InConfigs, uint64 InSeed, FString* OutError = nullptr);
    bool IsInitialized() const { return !Configs.IsEmpty(); }
    TConstArrayView<FVoxelCarverConfig> GetConfigs() const { return Configs; }

    bool IsStart(int32 Carver, FIntPoint Start) const;
    // Builds the path of Carver's start at Start (whether or not it is one).
    void BuildPath(int32 Carver, FIntPoint Start, FVoxelCarvePath& Out) const;
    // Sets Mask's bits (MaskWords, cleared first) for every cell of Column the starts within InReach carve. Cache null:
    // every path is built here.
    void Mark(FIntPoint Column, TArrayView<uint64> Mask, const FVoxelCarvePathCache* Cache,
        FVoxelCarveStats* Stats = nullptr, int32 InReach = Reach) const;

    // One step's cells in Column (the scout's ellipsoid rule): a block column is in when its centre's (dx^2 + dy^2) <
    // 1, Z runs from floor(Z - Vertical) to floor(Z + Vertical) + 1 but never below MinZ + 1, and the relative height
    // (Z - 0.5 - centre) / Vertical goes to the skip test. Start is the column the step's X and Y count from;
    // bSkipTest false keeps every candidate (the oracle cases).
    static void Voxelize(const FVoxelCarveStep& Step, EVoxelCarverKind Kind, TConstArrayView<float> Widths,
        FIntPoint Column, TArrayView<uint64> Mask, FIntPoint Start = FIntPoint::ZeroValue, bool bSkipTest = true);

private:
    void BuildCave(
        const FVoxelCarverConfig& Config, FVoxelXoroshiro& Random, FIntPoint Start, TArray<FVoxelCarveStep>& Out) const;
    void BuildCanyon(const FVoxelCarverConfig& Config, FVoxelXoroshiro& Random, FIntPoint Start,
        TArray<FVoxelCarveStep>& Out, TArray<float>& Widths) const;

    uint64 Seed = 0;
    TArray<FVoxelCarverConfig> Configs;
    TArray<uint64> StartSalts, PathSalts;
};
