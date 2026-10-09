#pragma once

#include "CoreMinimal.h"
#include "Async/Mutex.h"

// Corner values shared between neighbouring columns. Cells have corners on their column's four edges, and the column
// across an edge needs the same corners, so a corner column (one x and z) on a column edge is kept here once evaluated,
// for whichever neighbour comes next. Only edge corner columns are kept: per column, the ColumnSize / CellXZ on its
// west line and ColumnSize / CellXZ - 1 more on its north line (7 for 4-block cells). The table is direct-mapped over a
// window of Window x Window columns, so corner columns within one window never collide; one a window away takes the
// slot over (the newest wins). Corners are pure functions of the evaluator's seed and graph and of the position, so a
// value found here equals a fresh evaluation bit for bit, and a race or an eviction changes only how often a corner is
// found. Keep one cache per FVoxelColumnDensity: the key is only the position. Any thread may use it (each slot has its
// own lock).
class UNNAMEDVOXEL_API FVoxelCornerCache
{
public:
    // Columns per side of the window the table covers.
    static constexpr int32 Window = 32;
    // The most memory the table may take.
    static constexpr int64 MaxBytes = 16 << 20;

    // Values per corner (one per interpolated node), corners per corner column and the cell width, for columns of
    // ColumnSize blocks; Owner is what made the cache (FVoxelColumnDensity::MakeCornerCache), so another column density
    // never reads it. Fails when the corners do not fit a 64-bit mask or the table would take more than MaxBytes.
    bool Initialize(
        int32 InValues, int32 InCornersY, int32 InCellXZ, int32 InColumnSize, const void* InOwner = nullptr);
    bool IsValid() const { return SlotCount > 0; }
    // Whether InOwner made this cache for corners of this shape: FVoxelColumnDensity::Generate reads no other.
    bool BelongsTo(const void* InOwner, int32 InValues, int32 InCornersY, int32 InCellXZ) const
    {
        return IsValid() && InOwner != nullptr && InOwner == Owner && InValues == Values && InCornersY == CornersY &&
            InCellXZ == CellXZ;
    }
    int64 GetBytes() const;

    // Copies the corners in Wanted (bit Y for corner Y, from the bottom) that are kept for the corner column at (X, Z)
    // into Out, Values floats per corner with corner Y at Y * Values, and returns the corners copied.
    uint64 Find(int32 X, int32 Z, uint64 Wanted, float* Out) const;
    // Keeps the corners in Mask for the corner column at (X, Z), read from In with Find's layout. Positions inside a
    // column are never kept.
    void Store(int32 X, int32 Z, uint64 Mask, const float* In);
    // Forgets every corner, so a measurement or a load-order check starts cold.
    void Reset();

private:
    struct FSlot
    {
        mutable UE::FMutex Lock;
        bool bUsed = false;
        int32 X = 0, Z = 0;
        uint64 Mask = 0;
    };

    // The slot of an edge corner column, or INDEX_NONE inside a column.
    int32 SlotIndex(int32 X, int32 Z) const;

    int32 Values = 0, CornersY = 0, CellXZ = 0, ColumnSize = 0, PerLine = 0, SlotsPerColumn = 0, SlotCount = 0;
    // Bits 0 to CornersY - 1: masks are clipped to them, so no corner lands in the next slot's data.
    uint64 AllCorners = 0;
    const void* Owner = nullptr;
    TUniquePtr<FSlot[]> Slots;
    TArray<float> Data;
};
