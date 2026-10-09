#pragma once

#include "CoreMinimal.h"
#include "VoxelConstants.h"

// A column's biomes: one biome index (into the source's FVoxelBiomeTable) per quart, the 4 x 4 x 4 block cell whose
// minimum corner the source sampled. It holds the column's own 4 x 4 quart columns plus the next column's first row
// and column (5 x 5), so every block's lookup is answered inside the column, whatever is loaded around it. Built on a
// worker, then read-only like the column's blocks.
//
// Borders are jittered: each quart corner's site moves by up to 0.45 quart on each axis, from a hash of the seed and
// the quart, and a block takes the biome of the nearest site among its cell's 8 corners. Borders come out ragged
// instead of following the 4-block grid (the effect of 26.3's biome zoom, through our own hash).
class UNNAMEDVOXEL_API FVoxelBiomeStorage
{
public:
    static constexpr int32 QuartSize = 4;
    static constexpr int32 Width = VoxelConstants::ColumnSize / QuartSize + 1;
    static constexpr int32 Layers = VoxelConstants::Height / QuartSize;
    static constexpr int32 Volume = Width * Width * Layers;
    // No biome sampled.
    static constexpr uint8 None = 0xFF;

    // Quart X and Y from the column's minimum corner (0-4), Z from the world's bottom (0-95): X fastest, then Y.
    static constexpr int32 GetIndex(int32 X, int32 Y, int32 Z) { return (Z * Width + Y) * Width + X; }

    // Sizes the storage for column Column of a world seed, every quart None.
    void Init(FIntPoint InColumn, uint64 WorldSeed);
    // Empty: the terrain has no biomes (Superflat, the stand-in).
    void Reset();
    bool IsEmpty() const { return Quarts.IsEmpty(); }

    FIntPoint GetColumn() const { return Column; }
    TConstArrayView<uint8> GetQuarts() const { return Quarts; }
    uint8 GetQuart(int32 X, int32 Y, int32 Z) const { return Quarts[GetIndex(X, Y, Z)]; }
    void SetQuart(int32 X, int32 Y, int32 Z, uint8 Biome) { Quarts[GetIndex(X, Y, Z)] = Biome; }

    // The quart (as GetQuart's X, Y, Z) whose biome the block at local X and Y (0-15) and world Z takes.
    FIntVector GetBlockQuart(int32 LocalX, int32 LocalY, int32 Z) const;
    // That quart's biome index, or INDEX_NONE when the storage is empty or Z is outside the world.
    int32 GetBlock(int32 LocalX, int32 LocalY, int32 Z) const;

    // A quart corner's offset from its grid position, in quarts (each axis in [-0.45, 0.45)), for world quart
    // coordinates (block coordinates divided by 4, rounded down).
    static FVector3f GetJitter(uint64 WorldSeed, int32 QuartX, int32 QuartY, int32 QuartZ);

    SIZE_T GetAllocatedSize() const { return Quarts.GetAllocatedSize(); }

private:
    TArray<uint8> Quarts;
    FIntPoint Column = FIntPoint::ZeroValue;
    uint64 Seed = 0;
};
