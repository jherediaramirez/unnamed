#pragma once

#include "CoreMinimal.h"
#include "Biome/VoxelBiomeStorage.h"
#include "Storage/VoxelSection.h"
#include "VoxelConstants.h"

class FVoxelBlockTable;

// Per-column surfaces, as the reference keeps them so nothing has to scan a column for its top.
enum class EVoxelHeightmap : uint8
{
    // Highest non-air block.
    WorldSurface,
    // Highest block that blocks movement, ignoring fluids: the sea floor under water.
    OceanFloor,
    // Highest block that blocks movement or holds fluid.
    MotionBlocking,
    // The same, ignoring leaves.
    MotionBlockingNoLeaves,
    Count
};

// One 16 x 16 column of the world at full height: 24 sections and the heightmaps. A column is built on a worker and
// shared read-only from then on; an edit copies the whole column (about 10 KB) and changes the copy.
class UNNAMEDVOXEL_API FVoxelColumn
{
public:
    static constexpr int32 Area = VoxelConstants::ColumnSize * VoxelConstants::ColumnSize;
    // A dense column buffer: X fastest, then Y, then Z from MinZ. Section s is the s-th run of FVoxelSection::Volume.
    static constexpr int32 DenseVolume = Area * VoxelConstants::Height;

    static constexpr int32 GetDenseIndex(int32 X, int32 Y, int32 Z)
    {
        return ((Z - VoxelConstants::MinZ) * VoxelConstants::ColumnSize + Y) * VoxelConstants::ColumnSize + X;
    }

    explicit FVoxelColumn(FIntPoint InPosition = FIntPoint::ZeroValue) : Position(InPosition) {}

    // Column coordinates: world X and Y divided by 16, rounded down.
    FIntPoint GetPosition() const { return Position; }
    const FVoxelSection& GetSection(int32 Index) const { return Sections[Index]; }
    // The state at local X and Y (0-15) and world Z; air above and below the world.
    uint16 GetState(int32 X, int32 Y, int32 Z) const;
    // The highest Z matching the heightmap's rule, or MinZ - 1 when nothing in the column matches.
    int32 GetHeight(EVoxelHeightmap Map, int32 X, int32 Y) const;

    // Builds the sections and heightmaps from a dense buffer of DenseVolume states.
    void Build(TConstArrayView<uint16> Dense, const FVoxelBlockTable& Table);
    // The column's biomes, empty when its terrain has none. Edits leave them as they are.
    const FVoxelBiomeStorage& GetBiomes() const { return Biomes; }
    void SetBiomes(FVoxelBiomeStorage&& InBiomes) { Biomes = MoveTemp(InBiomes); }
    // The biome index (into the terrain's biome table) of the block at local X and Y and world Z, or INDEX_NONE.
    int32 GetBiome(int32 X, int32 Y, int32 Z) const { return Biomes.GetBlock(X, Y, Z); }

    // Sets one cell (local X and Y, world Z) and that X, Y's heightmaps; a Z outside the world changes nothing.
    void SetState(int32 X, int32 Y, int32 Z, uint16 State, const FVoxelBlockTable& Table);
    // Replaces one section's states (FVoxelSection::Volume, in FVoxelSection::GetIndex order) and leaves the
    // heightmaps alone: Finalize calls UpdateHeights for each X, Y where a write changed what the heightmaps count.
    void SetSectionStates(int32 Section, TConstArrayView<uint16> States);
    void UpdateHeights(int32 X, int32 Y, const FVoxelBlockTable& Table);
    SIZE_T GetAllocatedSize() const;

private:
    FIntPoint Position;
    FVoxelSection Sections[VoxelConstants::SectionCount];
    // Z - MinZ + 1 of the top matching block, 0 for none: the reference's encoding.
    uint16 Heightmaps[int32(EVoxelHeightmap::Count)][Area] = {};
    FVoxelBiomeStorage Biomes;
};
