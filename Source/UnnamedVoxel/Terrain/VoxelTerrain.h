#pragma once

#include "CoreMinimal.h"

class FVoxelColumn;

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
    // Fills Out with column Position; Dense is scratch, resized to FVoxelColumn::DenseVolume.
    virtual void Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const = 0;
};
