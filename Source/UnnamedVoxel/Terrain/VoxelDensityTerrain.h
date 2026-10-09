#pragma once

#include "CoreMinimal.h"
#include "Biome/VoxelBiomeSource.h"
#include "Density/VoxelColumnDensity.h"
#include "Density/VoxelCornerCache.h"
#include "HAL/CriticalSection.h"
#include "Noise/VoxelNoise.h"
#include "Terrain/VoxelAquifer.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Terrain/VoxelTerrain.h"

class FVoxelBlockTable;
class FVoxelColumn;
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
};

// What one column's generation cost, by stage.
struct FVoxelDensityTerrainTimings
{
    double DensityMs = 0;
    double AquiferMs = 0;
    double BlocksMs = 0;
    double BiomesMs = 0;
    FVoxelColumnDensityStats Density;
};

// Generator 2: a world type's terrain from 26.3's density graph (final_density > 0 is solid), its open space filled by
// aquifers (FVoxelAquifer: sea water, cave water and still lava), bedrock and deepslate at the bottom, and a simple top
// layer (grass on dirt, sand at the shore, gravel deep under water) until phase 5's surface rules. With a biome table,
// each column also gets its biomes (FVoxelBiomeSource, multi-noise over the preset's climate). Axes: the
// reference's x is our X, its z our Y and its y our Z. Immutable after construction; any worker may generate any
// column, and the result depends only on the preset, the seed and the column.
class UNNAMEDVOXEL_API FVoxelDensityTerrain final : public IVoxelTerrain
{
public:
    // Table must outlive the terrain; BiomeTable (a parameter list, copied) may be null for no biomes. Check IsValid: a
    // preset that cannot compile, or biomes that cannot be built, leave the terrain empty.
    FVoxelDensityTerrain(const FVoxelTerrainPreset& Preset, uint64 InSeed, const FVoxelBlockTable& InTable,
        const FVoxelDensityTerrainSettings& InSettings = {}, const FVoxelBiomeTable* BiomeTable = nullptr);
    virtual ~FVoxelDensityTerrain() override;

    bool IsValid() const { return Error.IsEmpty(); }
    const FString& GetError() const { return Error; }

    // Exact: generates the column's density (about a millisecond).
    virtual int32 GetSurfaceHeight(int32 X, int32 Y) const override;
    // Cheap: the preliminary surface (EstimateSurface) at or above sea level. ConfirmDry checks the real column.
    virtual bool IsDry(int32 X, int32 Y) const override;
    virtual bool ConfirmDry(int32 X, int32 Y) const override;
    virtual void Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const override;
    virtual void ResetCaches() const override;

    // The preset's preliminary surface level (its PS view, a find_top_surface): the y of the top 8-block cell whose
    // smooth density is positive, ignoring the 3D noise.
    int32 EstimateSurface(int32 X, int32 Y) const;
    // Generates a column and reports its stage times. The overrides replace the settings' bSkip and bShareCorners
    // (sharing needs bShareCorners at construction) and the biome fill (Pruned by default; ignored without biomes).
    void Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense, FVoxelDensityTerrainTimings* Timings,
        const bool* bSkipOverride = nullptr, const bool* bShareOverride = nullptr,
        const EVoxelBiomeFill* BiomeOverride = nullptr) const;
    // The corner cache's size in bytes, 0 without sharing.
    int64 GetCornerCacheBytes() const;
    // The column's density, ((Z - MinZ) * 16 + local Y) * 16 + local X.
    void GenerateDensity(FIntPoint Position, TArray<float>& Out, bool bSkip, FVoxelColumnDensityStats* Stats) const;

    const FVoxelDensityEvaluator& GetEvaluator() const { return Evaluator; }
    virtual const FVoxelBiomeSource* GetBiomeSource() const override
    {
        return Biomes.IsInitialized() ? &Biomes : nullptr;
    }
    int32 GetFinalDensity() const { return FinalDensity; }

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
    };
    TUniquePtr<FScratch> TakeScratch() const;
    void ReturnScratch(TUniquePtr<FScratch> Scratch) const;

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
    FVoxelBiomeSource Biomes;

    mutable FCriticalSection PoolLock;
    mutable TArray<TUniquePtr<FScratch>> Pool;
};
