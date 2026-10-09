#pragma once

#include "CoreMinimal.h"
#include "Noise/VoxelNoise.h"
#include "Terrain/VoxelTerrain.h"

class FVoxelBlockTable;
class FVoxelColumn;

// The states the stand-in terrain places, looked up once by block id.
struct FVoxelTerrainBlocks
{
    uint16 Stone = 0, Deepslate = 0, Dirt = 0, Grass = 0, Sand = 0, Gravel = 0, Water = 0, Bedrock = 0, Snow = 0;
    // Still lava (aquifers); 0 when the table has none.
    uint16 Lava = 0;

    static FVoxelTerrainBlocks Find(const FVoxelBlockTable& Table);
};

// Phase 2's stand-in terrain: a seeded 2D heightmap with no caves or overhangs, dressed in the reference's layers
// (bedrock thinning out by Z -60, deepslate below 0 mixing into stone up to 8, sea at 63). It gives storage, meshing
// and streaming varied ground to run on until phase 4's density terrain replaces it. Immutable after construction;
// any worker may generate any column, and the result depends only on the seed and the column.
class UNNAMEDVOXEL_API FVoxelHeightTerrain final : public IVoxelTerrain
{
public:
    // Table must outlive the terrain.
    FVoxelHeightTerrain(int64 InSeed, const FVoxelBlockTable& InTable);

    virtual int32 GetSurfaceHeight(int32 X, int32 Y) const override;
    // Dry from sea level up.
    virtual bool IsDry(int32 X, int32 Y) const override;
    virtual void Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const override;

private:
    int64 Seed;
    const FVoxelBlockTable& Table;
    FVoxelTerrainBlocks Blocks;
    FVoxelNoise Continents, Hills, Detail, Soil;
};
