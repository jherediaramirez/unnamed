#include "Biome/VoxelBiomeStorage.h"
#include "Random/VoxelRandom.h"

namespace
{
    constexpr uint64 BiomeJitterSalt = VoxelRandom::Salt("biome/jitter");
    // The world's bottom in quarts.
    constexpr int32 BiomeStorageMinQuartZ = VoxelConstants::MinZ / FVoxelBiomeStorage::QuartSize;
    static_assert(VoxelConstants::MinZ % FVoxelBiomeStorage::QuartSize == 0, "Quarts must tile the world range");

    // A 10-bit field of a hash as a jitter in [-0.45, 0.45) quarts.
    FORCEINLINE float BiomeJitterAxis(uint64 Hash, int32 Shift)
    {
        return (float((Hash >> Shift) & 1023) / 1024.f - .5f) * .9f;
    }
} // namespace

void FVoxelBiomeStorage::Init(FIntPoint InColumn, uint64 WorldSeed)
{
    Column = InColumn;
    Seed = WorldSeed;
    Quarts.Init(None, Volume);
}

void FVoxelBiomeStorage::Reset()
{
    Quarts.Empty();
    Column = FIntPoint::ZeroValue;
    Seed = 0;
}

FVector3f FVoxelBiomeStorage::GetJitter(uint64 WorldSeed, int32 QuartX, int32 QuartY, int32 QuartZ)
{
    const uint64 Hash = VoxelRandom::Hash64(WorldSeed, BiomeJitterSalt, QuartX, QuartY, QuartZ);
    return FVector3f(BiomeJitterAxis(Hash, 54), BiomeJitterAxis(Hash, 44), BiomeJitterAxis(Hash, 34));
}

FIntVector FVoxelBiomeStorage::GetBlockQuart(int32 LocalX, int32 LocalY, int32 Z) const
{
    check(LocalX >= 0 && LocalX < VoxelConstants::ColumnSize && LocalY >= 0 && LocalY < VoxelConstants::ColumnSize);
    check(Z >= VoxelConstants::MinZ && Z <= VoxelConstants::MaxZ);
    // The block's centre in quarts: its cell's minimum corner and the fraction across the cell.
    const int32 Cell[3] = {LocalX / QuartSize, LocalY / QuartSize, (Z - VoxelConstants::MinZ) / QuartSize};
    const float Fraction[3] = {(float(LocalX % QuartSize) + .5f) / QuartSize,
        (float(LocalY % QuartSize) + .5f) / QuartSize,
        (float((Z - VoxelConstants::MinZ) % QuartSize) + .5f) / QuartSize};
    const int32 Base[3] = {Column.X * (Width - 1), Column.Y * (Width - 1), BiomeStorageMinQuartZ};

    // The nearest jittered site among the cell's 8 corners; ties go to the first in X, Y, Z order. The top layer has
    // no corner above it.
    FIntVector Best(Cell[0], Cell[1], Cell[2]);
    float BestDistance = MAX_flt;
    for (int32 Corner = 0; Corner < 8; ++Corner)
    {
        const int32 Step[3] = {Corner & 1, (Corner >> 1) & 1, Corner >> 2};
        if (Cell[2] + Step[2] >= Layers) continue;
        const FVector3f Jitter =
            GetJitter(Seed, Base[0] + Cell[0] + Step[0], Base[1] + Cell[1] + Step[1], Base[2] + Cell[2] + Step[2]);
        const float DX = Fraction[0] - (float(Step[0]) + Jitter.X);
        const float DY = Fraction[1] - (float(Step[1]) + Jitter.Y);
        const float DZ = Fraction[2] - (float(Step[2]) + Jitter.Z);
        const float Distance = DX * DX + DY * DY + DZ * DZ;
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = FIntVector(Cell[0] + Step[0], Cell[1] + Step[1], Cell[2] + Step[2]);
        }
    }
    return Best;
}

int32 FVoxelBiomeStorage::GetBlock(int32 LocalX, int32 LocalY, int32 Z) const
{
    if (IsEmpty() || Z < VoxelConstants::MinZ || Z > VoxelConstants::MaxZ) return INDEX_NONE;
    const FIntVector Quart = GetBlockQuart(LocalX, LocalY, Z);
    const uint8 Biome = GetQuart(Quart.X, Quart.Y, Quart.Z);
    return Biome == None ? INDEX_NONE : int32(Biome);
}
