#pragma once

#include "CoreMinimal.h"

class FVoxelBiomeSource;
class FVoxelColumn;
class FVoxelFeatureGraph;
class FVoxelTintTable;

// A world's column generator: the stand-in height terrain, a flat world, and later phase 4's density terrain. A
// column is a pure function of the generator's settings and the column's position, so any worker may generate any
// column in any order. Implementations are immutable after construction.
class UNNAMEDVOXEL_API IVoxelTerrain
{
public:
    virtual ~IVoxelTerrain() = default;

    // The top solid block's Z at a world column, known before any column is built (spawning, framing).
    virtual int32 GetSurfaceHeight(int32 X, int32 Y) const = 0;
    // Whether that top block is dry land rather than under water.
    virtual bool IsDry(int32 X, int32 Y) const = 0;
    // A second, exact look where IsDry is an estimate (the density terrain's). Spawning checks it before standing on
    // land.
    virtual bool ConfirmDry(int32 X, int32 Y) const { return IsDry(X, Y); }
    // A new world's spawn by 26.3's climate search (FVoxelSpawnFinder), or false when the terrain has none (the flat
    // and stand-in terrains, which take the nearest-land search). OutCell is the feet cell on the first dry top block
    // near the climate's pick (bOutDry), or, when none is dry, the pick at the water's surface.
    virtual bool FindSpawn(FIntVector& OutCell, bool& bOutDry) const { return false; }
    // Fills Out with column Position; Dense is scratch, resized to FVoxelColumn::DenseVolume.
    virtual void Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const = 0;
    // Decoration's snapshot (phase 6, S3): Generate without its last column-wide pass (the freeze pass), which
    // FinishColumn then runs on the decorated column, so snow lands on what decoration placed (plan 3.4).
    // FinishColumn(GenerateSnapshot) equals Generate. Terrains without such a pass generate as Generate does.
    virtual void GenerateSnapshot(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const
    {
        Generate(Position, Out, Dense);
    }
    virtual void FinishColumn(FVoxelColumn& Column) const {}
    // Forgets work shared between columns (results never depend on it), so a measurement or a load-order check starts
    // cold.
    virtual void ResetCaches() const {}
    // The biomes the terrain's columns carry, or null when it has none (Superflat, the stand-in).
    virtual const FVoxelBiomeSource* GetBiomeSource() const { return nullptr; }
    // The colours its biomes give grass, foliage and water, indexed like the columns' biomes; null without biomes or
    // colormaps (blocks keep their plains tints).
    virtual const FVoxelTintTable* GetTints() const { return nullptr; }
    // The decoration data its columns are decorated with (phase 6), or null when it has none; and the world seed the
    // decoration's random streams start from.
    virtual TSharedPtr<const FVoxelFeatureGraph> GetFeatures() const { return nullptr; }
    virtual uint64 GetSeed() const { return 0; }
};
