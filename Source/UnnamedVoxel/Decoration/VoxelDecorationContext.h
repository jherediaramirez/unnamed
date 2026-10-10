#pragma once

#include "CoreMinimal.h"
#include "Storage/VoxelColumn.h"
#include "Storage/VoxelSection.h"
#include "VoxelConstants.h"

class FVoxelBlockTable;

// The heightmaps a placement reads (26.3's names). The _WG maps are the snapshot's, as Generate left the columns; the
// others are live: the snapshot's raised by this Decorate's own writes, so a patch sees the pumpkin or cactus an
// earlier try placed (vanilla's maps update as blocks go in).
enum class EVoxelDecorationHeightmap : uint8
{
    WorldSurfaceWG,
    OceanFloorWG,
    WorldSurface,
    OceanFloor,
    MotionBlocking,
    MotionBlockingNoLeaves
};

// What one Decorate reads (phase 6 plan 3.1): the 3x3 columns around its own, as Generate left them (the snapshots),
// under a working copy that holds this Decorate's own writes. A feature therefore sees the column's earlier features
// wherever they landed, and never a neighbour's decoration, which is what makes the result independent of load order.
// The working copy is sparse: the cells written so far, in an open-addressing table over the 48 x 48 window, so a
// write costs no section copy. Coordinates are world blocks.
class UNNAMEDVOXEL_API FVoxelDecorationContext
{
public:
    static constexpr int32 Window = 3 * VoxelConstants::ColumnSize;

    // Snapshots: the 9 columns around Center, Snapshots[FVoxelWriteLog::GetBucket(DX, DY)] at Center + (DX, DY);
    // none may be null. With a Table, writes raise the live heightmaps (plants read them; ores never do).
    FVoxelDecorationContext(
        FIntPoint Center, TConstArrayView<const FVoxelColumn*> Snapshots, const FVoxelBlockTable* Table = nullptr);
    // Leaves the thread's written bitmap clear for the next context.
    ~FVoxelDecorationContext();
    FVoxelDecorationContext(const FVoxelDecorationContext&) = delete;
    FVoxelDecorationContext& operator=(const FVoxelDecorationContext&) = delete;

    FIntPoint GetCenter() const { return Center; }
    bool Contains(int32 X, int32 Y) const
    {
        return uint32(X - MinX) < uint32(Window) && uint32(Y - MinY) < uint32(Window);
    }

    // The working copy's state; air outside the window or the world.
    uint16 GetState(int32 X, int32 Y, int32 Z) const
    {
        const int32 LX = X - MinX, LY = Y - MinY, Local = Z - VoxelConstants::MinZ;
        if (uint32(LX) >= uint32(Window) || uint32(LY) >= uint32(Window) ||
            uint32(Local) >= uint32(VoxelConstants::Height))
            return 0;
        const uint32 Key = uint32((Local * Window + LY) * Window + LX);
        if (Written[Key >> 6] >> (Key & 63) & 1)
            for (uint32 Slot = Hash(Key);; Slot = (Slot + 1) & SlotMask)
                if (Keys.GetData()[Slot] == Key) return Values.GetData()[Slot];
        return Columns[(LY >> 4) * 3 + (LX >> 4)]
            ->GetSection(Local >> 4)
            .Get(FVoxelSection::GetIndex(LX & 15, LY & 15, Local & 15));
    }

    // Sets a cell of the working copy; a cell outside the window or the world changes nothing.
    void SetState(int32 X, int32 Y, int32 Z, uint16 State);

    const FVoxelColumn& GetSnapshot(int32 Bucket) const { return *Columns[Bucket]; }
    // The biome index of the block at world X, Y and Z (clamped into the world), or INDEX_NONE outside the window or
    // without biomes.
    int32 GetBiome(int32 X, int32 Y, int32 Z) const;
    // The highest non-air Z of column Bucket in the working copy (MinZ - 1 when empty): nothing above it is anything
    // but air. Writes of air leave it high, so it is a bound, never too low.
    int32 GetTopZ(int32 Bucket) const { return TopZ[Bucket]; }
    // The top Z a heightmap counts at world X, Y (MinZ - 1 for none, or outside the window). Vanilla's heightmap value
    // is one above it.
    int32 GetHeight(EVoxelDecorationHeightmap Map, int32 X, int32 Y) const;
    // Test-only (the mutation check): writes stop raising the live heightmaps.
    void FreezeHeightsForTest() { bFrozenHeights = true; }

    // Cells written so far, and the working copy's memory.
    int32 GetWrittenCells() const { return Count; }
    SIZE_T GetAllocatedSize() const
    {
        return Keys.GetAllocatedSize() + Values.GetAllocatedSize() + WrittenWords * sizeof(uint64);
    }

private:
    static constexpr uint32 EmptyKey = MAX_uint32;

    uint32 Hash(uint32 Key) const { return (Key * 0x9E3779B1u) >> HashShift; }
    void Grow();

    FIntPoint Center;
    int32 MinX = 0, MinY = 0;
    const FVoxelColumn* Columns[9] = {};
    int32 TopZ[9] = {};
    // One bit per window cell (the table's key), set once the cell is written: reads of other cells skip the table.
    // The bitmap is the worker thread's, reused by its next context and cleared by this one's destructor.
    static constexpr int32 WrittenWords = Window * Window * VoxelConstants::Height / 64;
    uint64* Written = nullptr;
    // The written cells: key (Z - MinZ) * 48^2 + window Y * 48 + window X, or EmptyKey; at most half full.
    TArray<uint32> Keys;
    TArray<uint16> Values;
    uint32 SlotMask = 0;
    int32 HashShift = 32;
    int32 Count = 0;
    // Live heightmaps: per map (WorldSurface, OceanFloor, MotionBlocking, MotionBlockingNoLeaves) and window cell, the
    // highest Z a write raised it to; empty without a table.
    const FVoxelBlockTable* Table = nullptr;
    TArray<int16> Raised;
    bool bFrozenHeights = false;
};
