#pragma once

#include "CoreMinimal.h"

class FVoxelBiomeClimate;
class FVoxelBiomeStorage;
class FVoxelBiomeTable;
class FVoxelBlockTable;

// 26.3's snow-and-freeze step (black-boxed: Docs/Records/2026-10-09-Voxel-Phase5-Scout-Facts.md, section 4), run
// once per generated column after the surface rules. Per block column it finds the top: the position above the highest
// block that blocks movement or holds fluid. With the biome at the top (FVoxelBiomeStorage::GetBlock) and the
// temperature of each position (FVoxelBiomeClimate):
// - still water just below the top turns to ice when cold;
// - the top takes one snow layer when cold, if the block below can hold snow; a grass block, podzol or mycelium
//   below turns snowy.
// Block light is taken as 0 (generation has no light yet). Snow blocks and powder snow come only from the rules.
// When trees arrive, run it after them, so snow lands on leaves. Immutable after Initialize; any thread.
class UNNAMEDVOXEL_API FVoxelFreezePass
{
public:
    // Resolves ice, snow and the support exceptions in Table (which must outlive the pass); fails when the table has
    // no ice or snow.
    bool Initialize(const FVoxelBlockTable& InTable, FString* OutError = nullptr);
    bool IsInitialized() const { return Table != nullptr; }

    // Freezes and snows one generated column's blocks in Dense (FVoxelColumn::GetDenseIndex order). Position is the
    // column; Biomes its storage (empty: nothing changes). Heights, if given, hold each block column's highest block
    // that is not air (index LocalY * 16 + LocalX; nothing above it is anything but air), where the search for the top
    // starts.
    void Apply(FIntPoint Position, TArrayView<uint16> Dense, const FVoxelBiomeStorage& Biomes,
        const FVoxelBiomeTable& BiomeTable, const FVoxelBiomeClimate& Climate, int32 SeaLevel,
        TConstArrayView<int32> Heights = {}) const;

    // Whether a snow layer can rest on State: a full top face, except ice, packed ice, powder snow and barriers; honey,
    // soul sand and mud always.
    bool CanHoldSnow(uint16 State) const;

private:
    const FVoxelBlockTable* Table = nullptr;
    uint16 Ice = 0, Snow = 0;
    // Block indices (FVoxelStateInfo::Block).
    uint16 WaterBlock = 0;
    TArray<uint16> NeverHolds, AlwaysHolds;
};
