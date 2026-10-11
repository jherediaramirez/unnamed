#pragma once

#include "CoreMinimal.h"
#include "Biome/VoxelBiomeClimate.h"
#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTints.h"
#include "Density/VoxelColumnDensity.h"
#include "Density/VoxelCornerCache.h"
#include "HAL/CriticalSection.h"
#include "Noise/VoxelNoise.h"
#include "Surface/VoxelFreezePass.h"
#include "Surface/VoxelSurfaceEvaluator.h"
#include "Terrain/VoxelAquifer.h"
#include "Terrain/VoxelCarvers.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Terrain/VoxelSpawnFinder.h"
#include "Terrain/VoxelTerrain.h"

class FVoxelBlockTable;
class FVoxelColumn;
class FVoxelFeatureGraph;
struct FVoxelTerrainPreset;

struct FVoxelDensityTerrainSettings
{
    // The full final_density with aquifers (water and still lava underground), and only the first surface from the
    // sky dressed in grass, sand or gravel. Off: final_density's cave branch reads sloped_cheese alone, noodle caves
    // are neutral (RemoveCaves), and open space below sea level is water, exactly as before caves (session 3).
    bool bCaves = true;
    // Height-bound skipping (FVoxelColumnDensity); off only to compare against.
    bool bSkip = true;
    // Corners shared between neighbouring columns (FVoxelCornerCache, about 7 MB); off only to compare against.
    bool bShareCorners = true;
    // With surface rules and biomes: 26.3's surface rules (FVoxelSurfaceEvaluator), then the freeze pass. Off: the
    // earlier simple dressing (grass on dirt, sand at the shore, gravel deep under water, bedrock and deepslate mixed
    // in), kept to compare against.
    bool bSurfaceRules = true;
    // The surface rules' deep-stone shortcut (exact); off only to compare against.
    bool bSurfaceShortcut = true;
    // With surface rules: the ore_vein rules (copper and iron veins, phase 6 S4; FVoxelOreVeins). Off gives the blocks
    // from before S4 exactly.
    bool bOreVeins = true;
    // The veins' shortcut (exact: only the heights their densities reach, cells their bounds rule out skipped); off
    // evaluates every solid block directly, only to compare against.
    bool bOreVeinShortcut = true;
    // The carvers (phase 6, S10; voxel.Carvers): with caves, the rules and the decoration data, tunnels and canyons
    // carved between the surface rules and the freeze, their cells filled by the aquifer. Off (the default until S11)
    // gives the blocks from before S10 exactly.
    bool bCarvers = false;
    // Single Biome: this biome everywhere, by full id ("minecraft:desert"). Empty: the multi-noise source.
    FString FixedBiome;
    // The decoration data, held for the decoration stages (phase 6, S2 on); generation does not read it yet.
    TSharedPtr<const FVoxelFeatureGraph> Features;
};

// Per-call replacements for the settings, for benches and gates; unset keeps the setting. Sharing needs bShareCorners
// at construction; biomes and rules need their tables.
struct FVoxelDensityTerrainOverrides
{
    TOptional<bool> bSkip;
    TOptional<bool> bShareCorners;
    TOptional<EVoxelBiomeFill> BiomeFill;
    TOptional<bool> bSurfaceRules;
    TOptional<bool> bSurfaceShortcut;
    // Off for decoration's snapshot: the freeze pass then runs in FinishColumn (default on).
    TOptional<bool> bFreeze;
    TOptional<bool> bOreVeins;
    TOptional<bool> bOreVeinShortcut;
    // With the carvers compiled: carve (default bCarvers), through the shared path cache (default on; off builds
    // every path in the column, only to compare against), from starts within this many columns (default
    // FVoxelCarvers::Reach; the reach tests only).
    TOptional<bool> bCarvers;
    TOptional<bool> bCarveCache;
    TOptional<int32> CarveReach;
};

// What one column's generation cost, by stage.
struct FVoxelDensityTerrainTimings
{
    double DensityMs = 0;
    double AquiferMs = 0;
    double BlocksMs = 0;
    double BiomesMs = 0;
    // Inside BlocksMs: the preliminary surface, the surface rules and the freeze pass; the first and last also apart.
    double SurfaceMs = 0;
    double PreliminaryMs = 0;
    double FreezeMs = 0;
    // Inside SurfaceMs: the ore veins' field (FVoxelOreVeins::Generate), and its counts.
    double VeinMs = 0;
    int32 VeinCells = 0, VeinCorners = 0, VeinBlocks = 0, VeinGaps = 0, VeinGiven = 0;
    // Solid blocks the rules decided from the deep and interior tables and through the full tree, and the full tree's
    // instructions run.
    int32 SurfaceDeep = 0, SurfaceInterior = 0, SurfaceFull = 0, SurfaceInteriorRuns = 0;
    int64 SurfaceSteps = 0;
    // Inside BlocksMs, apart from SurfaceMs: the carvers (S10). Carve holds the mark's split; Candidates are marked
    // blocks that could be carved (not air, fluid or bedrock), the next three those carved, by what fills them, and
    // Barriers those the aquifer kept solid.
    double CarversMs = 0;
    FVoxelCarveStats Carve;
    int32 CarveCandidates = 0, CarveAir = 0, CarveWater = 0, CarveLava = 0, CarveBarriers = 0;
    FVoxelColumnDensityStats Density;
};

// Generator 2: a world type's terrain from 26.3's density graph (final_density > 0 is solid), its open space filled by
// aquifers (FVoxelAquifer: sea water, cave water and still lava). With a biome table, each column gets its biomes
// (FVoxelBiomeSource, multi-noise over the preset's climate, or one fixed biome); with surface rules as well, the
// rules choose every solid block (FVoxelSurfaceEvaluator: bedrock, deepslate, the surface by biome) and the freeze pass
// adds ice and snow (FVoxelFreezePass). Without rules, a simple dressing stands in. Axes: the reference's x is our X,
// its z our Y and its y our Z. Immutable after construction; any worker may generate any column, and the result
// depends only on the preset, the seed and the column.
class UNNAMEDVOXEL_API FVoxelDensityTerrain final : public IVoxelTerrain
{
public:
    // Table must outlive the terrain; BiomeTable (a parameter list, copied) may be null for no biomes, and
    // SurfaceInputs (read once) null for no rules. Rules need biomes. Check IsValid: a preset that cannot compile, or
    // biomes or rules that cannot be built, leave the terrain empty.
    FVoxelDensityTerrain(const FVoxelTerrainPreset& Preset, uint64 InSeed, const FVoxelBlockTable& InTable,
        const FVoxelDensityTerrainSettings& InSettings = {}, const FVoxelBiomeTable* BiomeTable = nullptr,
        const FVoxelSurfaceInputs* SurfaceInputs = nullptr);
    virtual ~FVoxelDensityTerrain() override;

    bool IsValid() const { return Error.IsEmpty(); }
    const FString& GetError() const { return Error; }

    // Exact: generates the column's density (about a millisecond).
    virtual int32 GetSurfaceHeight(int32 X, int32 Y) const override;
    // Cheap: the preliminary surface (EstimateSurface) at or above sea level. ConfirmDry checks the real column.
    virtual bool IsDry(int32 X, int32 Y) const override;
    virtual bool ConfirmDry(int32 X, int32 Y) const override;
    // The climate's pick, then the 11 x 11 columns in square rings around its column, each column's 16 x 16 block
    // columns from its minimum corner: the first top solid block at or above SeaLevel - 1 (not under water) wins.
    virtual bool FindSpawn(FIntVector& OutCell, bool& bOutDry) const override;
    virtual void Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const override;
    // Without the freeze pass, which FinishColumn runs (FVoxelFreezePass::ApplyToColumn) where Generate would.
    virtual void GenerateSnapshot(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const override;
    virtual void FinishColumn(FVoxelColumn& Column) const override;
    virtual void ResetCaches() const override;

    // The preset's preliminary surface level (its PS view, a find_top_surface): the y of the top 8-block cell whose
    // smooth density is positive, ignoring the 3D noise.
    int32 EstimateSurface(int32 X, int32 Y) const;
    // Generates a column and reports its stage times, with the settings replaced as Overrides says (the biome fill is
    // Pruned by default).
    void Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense, FVoxelDensityTerrainTimings* Timings,
        const FVoxelDensityTerrainOverrides& Overrides = {}) const;
    // The corner cache's size in bytes, 0 without sharing.
    int64 GetCornerCacheBytes() const;
    // GetSurfaceHeight for each block column of the column at Position (local Y * 16 + local X) from one density.
    void GetSurfaceHeights(FIntPoint Position, TArrayView<int32> Out) const;
    const FVoxelSpawnFinder& GetSpawnFinder() const { return SpawnFinder; }
    // The column's density, ((Z - MinZ) * 16 + local Y) * 16 + local X.
    void GenerateDensity(FIntPoint Position, TArray<float>& Out, bool bSkip, FVoxelColumnDensityStats* Stats) const;

    const FVoxelDensityEvaluator& GetEvaluator() const { return Evaluator; }
    virtual const FVoxelBiomeSource* GetBiomeSource() const override
    {
        return Biomes.IsInitialized() ? &Biomes : nullptr;
    }
    virtual const FVoxelTintTable* GetTints() const override
    {
        return Biomes.IsInitialized() && Tints.IsInitialized() ? &Tints : nullptr;
    }
    virtual TSharedPtr<const FVoxelFeatureGraph> GetFeatures() const override { return Settings.Features; }
    virtual uint64 GetSeed() const override { return Seed; }
    // The carvers, compiled when the terrain has caves, the rules and the decoration data (S10), and why not.
    const FVoxelCarvers& GetCarvers() const { return Carvers; }
    const FString& GetCarverError() const { return CarverError; }
    // The path cache's size in bytes (0 without carvers).
    int64 GetCarveCacheBytes() const { return CarveCache ? CarveCache->GetAllocatedSize() : 0; }
    int32 GetFinalDensity() const { return FinalDensity; }
    // The compiled surface rules; not compiled without rules.
    const FVoxelSurfaceEvaluator& GetSurfaceRules() const { return SurfaceRules; }
    const FVoxelBiomeClimate& GetClimate() const { return Climate; }
    int32 GetSeaLevel() const { return SeaLevel; }

    // Rewrites Graph so final_density ignores caves: range choices on a function named */sloped_cheese read it alone,
    // and */caves/noodle becomes a large constant (neutral under final_density's min). Returns the nodes rewritten.
    static int32 RemoveCaves(FVoxelDensityGraph& Graph);

private:
    struct FScratch
    {
        FVoxelColumnDensityScratch Column;
        FVoxelDensityScratch Point;
        FVoxelAquiferScratch Aquifer;
        FVoxelDensityScratch Biome;
        TArray<float> Density;
        TArray<EVoxelAquiferFluid> Fluids;
        // Per block column (local Y * 16 + X), bottom-up: what each block holds before the rules.
        TArray<EVoxelSurfaceKind> Kinds;
        FVoxelSurfaceChunk SurfaceChunk;
        // The carvers: the column's mark, the marked blocks that can be carved, and what fills them.
        TArray<uint64> CarveMask;
        TArray<int32> CarveBlocks;
        TArray<EVoxelAquiferFluid> CarveFluids;
    };
    TUniquePtr<FScratch> TakeScratch() const;
    void ReturnScratch(TUniquePtr<FScratch> Scratch) const;
    bool InitializeBiomes(const FVoxelBiomeTable& BiomeTable, const FVoxelTerrainPreset& Preset, FString& OutError);
    // The simple dressing without rules.
    void FillDressed(int32 X0, int32 Z0, TArray<uint16>& Dense, const FScratch& Scratch) const;
    // The rules and the freeze pass over the column's biomes, with their times.
    void FillWithRules(FIntPoint Position, const FVoxelColumn& Out, TArray<uint16>& Dense, FScratch& Scratch,
        const FVoxelDensityTerrainOverrides& Overrides, FVoxelDensityTerrainTimings& Timings) const;
    // The carvers' pass over the ruled blocks; Heights stay each block column's highest non-air block.
    void Carve(FIntPoint Position, TArray<uint16>& Dense, FScratch& Scratch, TArrayView<int32> Heights,
        const FVoxelDensityTerrainOverrides& Overrides, FVoxelDensityTerrainTimings& Timings) const;

    uint64 Seed;
    const FVoxelBlockTable& Table;
    FVoxelDensityTerrainSettings Settings;
    FVoxelTerrainBlocks Blocks;
    FVoxelNoise Soil;
    FString Error;
    int32 SeaLevel = 63;
    FVoxelDensityEvaluator Evaluator, SurfaceEvaluator;
    int32 FinalDensity = INDEX_NONE, Surface = INDEX_NONE;
    FVoxelColumnDensity Column;
    // Shared by every worker (it locks itself); null without bShareCorners.
    TUniquePtr<FVoxelCornerCache> CornerCache;
    FVoxelAquifer Aquifer;
    FVoxelSpawnFinder SpawnFinder;
    FVoxelBiomeSource Biomes;
    // Built when the biome table carries the colormaps.
    FVoxelTintTable Tints;
    FVoxelSurfaceEvaluator SurfaceRules;
    FVoxelFreezePass Freeze;
    FVoxelBiomeClimate Climate;
    FVoxelCarvers Carvers;
    FString CarverError;
    // Shared by every worker (it locks itself); null without carvers.
    TUniquePtr<FVoxelCarvePathCache> CarveCache;

    mutable FCriticalSection PoolLock;
    mutable TArray<TUniquePtr<FScratch>> Pool;
};
