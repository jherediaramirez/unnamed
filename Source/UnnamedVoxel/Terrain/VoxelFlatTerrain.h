#pragma once

#include "CoreMinimal.h"
#include "Terrain/VoxelTerrain.h"

class FVoxelBlockTable;
class FVoxelColumn;

// One layer of a flat world: a state, Count blocks thick.
struct FVoxelFlatLayer
{
    uint16 State = 0;
    int32 Count = 1;
};

// A flat world (the reference's Superflat): every column is the same stack of layers, listed from the bottom up and
// laid from the world's floor (Z -64), cut off at its top. Immutable after construction; any worker may generate any
// column.
class UNNAMEDVOXEL_API FVoxelFlatTerrain final : public IVoxelTerrain
{
public:
    // Table must outlive the terrain.
    FVoxelFlatTerrain(TConstArrayView<FVoxelFlatLayer> Layers, const FVoxelBlockTable& InTable);

    // The same everywhere: the top solid layer's top block, or one below the floor when no layer is solid.
    virtual int32 GetSurfaceHeight(int32 X, int32 Y) const override;
    // Dry unless a fluid layer lies above the top solid one.
    virtual bool IsDry(int32 X, int32 Y) const override;
    virtual void Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const override;

    // The state at each Z from the floor up, ending at the top layer (above it is air).
    TConstArrayView<uint16> GetStack() const { return Stack; }

private:
    const FVoxelBlockTable& Table;
    TArray<uint16> Stack;
    int32 Surface;
    bool bDry;
};
