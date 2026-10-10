#pragma once

#include "CoreMinimal.h"
#include "Density/VoxelDensityEvaluator.h"

struct FVoxelTerrainPreset;

// What fills a non-solid block.
enum class EVoxelAquiferFluid : uint8
{
    Air,
    Water,
    Lava,
    // Stone the aquifer adds between two different fluid levels.
    Barrier
};

// A cell's fluid: Fluid fills every y below Level.
struct FVoxelAquiferStatus
{
    int32 Level = 0;
    EVoxelAquiferFluid Fluid = EVoxelAquiferFluid::Air;

    EVoxelAquiferFluid At(int32 Y) const { return Y < Level ? Fluid : EVoxelAquiferFluid::Air; }
    bool operator==(const FVoxelAquiferStatus& Other) const { return Level == Other.Level && Fluid == Other.Fluid; }
};

// Working memory for FVoxelAquifer, one per thread.
struct FVoxelAquiferScratch
{
    // One per input evaluator (surface, floodedness, exclusion, spread, lava, barrier).
    FVoxelDensityScratch Points[6];
    TArray<double> X, Y, Z;
    TArray<FIntVector> Centers;
    TArray<FVoxelAquiferStatus> Statuses;
    TArray<float> Surface;
    TArray<int32> Candidates;
    TArray<uint8> Uniform, KeepEmpty;
    // Blocks waiting for the barrier noise: index into the column, then the three nearest cells and their weights.
    struct FPending
    {
        int32 Block;
        int32 Cells[3];
        float Weights[2];
    };
    TArray<FPending> Pending;
};

// Aquifers: what fills open space underground. Our own design on 26.3's aquifer inputs (a preset's `aquifers`
// object) and numbers:
// - Space is cut into cells of 16 x 12 x 16 blocks, each with a centre jittered inside it by Hash64(seed, salt, cell).
// - Each centre gets a status. Centres near or above the preliminary surface (surface_level) take the global fluid:
//   sea water below sea level, lava below y -54. Below it, floodedness decides between the global fluid, a fluid
//   level of the cell's own (spread, in 40-block bands, quantized to 3 blocks) and dry. Floodedness floods more
//   readily close under a sea floor. exclusion > 0 (deep, low-erosion regions) keeps a cell dry. A level of its own
//   at or below y -10 is lava where the lava noise is strong.
// - A non-solid block takes the status of the nearest of the 27 centres around it. Where the nearest two or three
//   centres are almost equally near and their statuses differ, a wall term (strongest between the two levels, with
//   the barrier noise added) can turn the block into stone; lava next to water always gets a wall.
// - Open space below y -54 is lava, and open space with no solid block above it takes the global fluid, so oceans
//   and open sky never hold aquifer pockets.
// Immutable after Initialize; results depend only on the seed, the preset and the block's position.
class UNNAMEDVOXEL_API FVoxelAquifer
{
public:
    static constexpr int32 CellXZ = 16;
    static constexpr int32 CellY = 12;
    static constexpr int32 LavaLine = -54;

    bool Initialize(const FVoxelTerrainPreset& Preset, uint64 InSeed, FString* OutError = nullptr);

    // Computes the statuses around the column whose lowest corner is (X0, Z0) (reference axes), for y MinY to
    // MinY + Height - 1. After this, GetKeepEmpty gives one flag per y: where Fill reads exact non-positive density.
    void Prepare(int32 X0, int32 Z0, FVoxelAquiferScratch& Scratch) const;
    TConstArrayView<uint8> GetKeepEmpty(const FVoxelAquiferScratch& Scratch) const { return Scratch.KeepEmpty; }

    // Fills Out (indexed like Density: ((y - MinY) * 16 + z) * 16 + x) with what fills each non-positive block;
    // solid blocks read Air. Call after Prepare for the same column.
    void Fill(int32 X0, int32 Z0, TConstArrayView<float> Density, FVoxelAquiferScratch& Scratch,
        TArray<EVoxelAquiferFluid>& Out) const;

    // The carvers' query (phase 6, S10): what Fill gives each listed block if that block were open, from the statuses
    // Prepare built for the same column, so a carved cell gets what a noise-cave cell there gets. Blocks index the
    // column as Density does; the wall test reads min(Density, 0) (0 for a carved solid block, as an open one would
    // read). SkyTop holds each block column's highest block with positive density (MinY - 1 for none; index z * 16 +
    // x): blocks at or above it take the global fluid, as Fill's sky rule gives them. Out[K] is Blocks[K]'s fluid;
    // Barrier keeps the block solid.
    void FillCarved(int32 X0, int32 Z0, TConstArrayView<int32> Blocks, TConstArrayView<float> Density,
        TConstArrayView<int32> SkyTop, FVoxelAquiferScratch& Scratch, TArray<EVoxelAquiferFluid>& Out) const;

    // A status's global fluid at a centre's y.
    FVoxelAquiferStatus GetGlobal(int32 Y) const;

private:
    int32 LayerOf(int32 Y) const { return FMath::FloorToInt32(Y / double(CellY)); }
    float GetWall(const FVoxelAquiferStatus& A, const FVoxelAquiferStatus& B, int32 Y, float Barrier) const;

    uint64 Seed = 0;
    int32 SeaLevel = 63;
    int32 MinY = 0, Height = 0;
    // The column's layers of cells, one more on each side.
    int32 FirstLayer = 0, Layers = 0;
    FVoxelDensityEvaluator Barrier, Exclusion, Floodedness, Spread, Lava, Surface;
    int32 BarrierNode = INDEX_NONE, ExclusionNode = INDEX_NONE, FloodednessNode = INDEX_NONE, SpreadNode = INDEX_NONE,
          LavaNode = INDEX_NONE, SurfaceNode = INDEX_NONE;
};
