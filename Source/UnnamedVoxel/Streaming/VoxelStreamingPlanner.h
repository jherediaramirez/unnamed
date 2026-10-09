#pragma once

#include "CoreMinimal.h"

// Which columns stream around the camera's column (phase 3). Pure functions of their inputs, so they are tested
// without the engine and give the same answer everywhere.
//
// Distances are gaps between whole columns: from the center column's square to another column's square, in columns.
// Wherever the camera stands inside its column, every block within ViewRadius columns of it lies in a column in view,
// so a fog that ends ViewRadius * 16 blocks away never shows a missing column.
//
// - In view (gap <= ViewRadius): meshed and drawn.
// - Needed (in view, or beside a column in view): generated, because a section meshes from its 3 x 3 neighbourhood.
// - Unloaded (gap > ViewRadius + 1 + UnloadMargin): beyond the generated ring plus the margin, as the prompt's
//   "unload radius = load radius + 2". Columns between needed and unloaded stay as they are, so moving back and
//   forth across a column border loads and unloads nothing (hysteresis).
struct UNNAMEDVOXEL_API FVoxelStreamingRings
{
    // A needed column's gap is at most ViewRadius + sqrt(2), and a diagonal move adds up to sqrt(2): with less than 2
    // past the ring, moving back and forth across a border would unload and reload columns at the edge.
    static constexpr int32 MinUnloadMargin = 2;

    FIntPoint Center = FIntPoint::ZeroValue;
    int32 ViewRadius = 0;
    int32 UnloadMargin = MinUnloadMargin;

    FVoxelStreamingRings() = default;
    // Clamps the radius to at least 0 and the margin to at least MinUnloadMargin.
    FVoxelStreamingRings(FIntPoint InCenter, int32 InViewRadius, int32 InUnloadMargin);

    // The squared gap in columns between the center column and Column: 0 for the center's 3 x 3.
    int64 GetGapSquared(FIntPoint Column) const;
    bool IsInView(FIntPoint Column) const;
    bool IsNeeded(FIntPoint Column) const;
    bool IsBeyondUnload(FIntPoint Column) const;
    // Columns farther than this (as a gap) unload.
    int32 GetUnloadDistance() const { return ViewRadius + 1 + UnloadMargin; }
    // Every needed column lies within this many columns of the center on both axes.
    int32 GetNeededExtent() const { return ViewRadius + 2; }

    bool operator==(const FVoxelStreamingRings& Other) const
    {
        return Center == Other.Center && ViewRadius == Other.ViewRadius && UnloadMargin == Other.UnloadMargin;
    }
};

// What changes when the rings move: the needed columns not yet present, in priority order, and the present columns
// beyond the unload distance.
struct UNNAMEDVOXEL_API FVoxelStreamingPlan
{
    TArray<FIntPoint> Loads;
    TArray<FIntPoint> Unloads;
};

namespace VoxelStreaming
{
    // The camera's yaw in eighths of a turn: sector 0 faces +X (east), and sectors count toward +Y (south), as Unreal's
    // yaw does. Plans change only when the sector does, not every frame.
    constexpr int32 ViewSectorCount = 8;
    UNNAMEDVOXEL_API int32 GetViewSector(double YawDegrees);
    // The sector's direction in whole columns: (1, 0), (1, 1), (0, 1) and so on.
    UNNAMEDVOXEL_API FIntPoint GetViewSectorDirection(int32 ViewSector);

    // Every needed column, in priority order: nearest first (squared distance between column centers), then most
    // ahead of the view sector, then by Y and X. The same rings and sector always give the same order.
    UNNAMEDVOXEL_API void GetNeededColumns(const FVoxelStreamingRings& Rings, int32 ViewSector, TArray<FIntPoint>& Out);

    // The columns of Present beyond the unload distance, sorted by Y and X.
    UNNAMEDVOXEL_API void GetUnloads(
        const FVoxelStreamingRings& Rings, TConstArrayView<FIntPoint> Present, TArray<FIntPoint>& Out);

    // Both lists against a set of present columns.
    UNNAMEDVOXEL_API void Plan(
        const FVoxelStreamingRings& Rings, int32 ViewSector, const TSet<FIntPoint>& Present, FVoxelStreamingPlan& Out);
}
