#pragma once

#include "CoreMinimal.h"

// Which columns stream around the camera's column (phase 3). Pure functions of their inputs, so they are tested
// without the engine and give the same answer everywhere.
//
// Distances are gaps between whole columns: from the center column's square to another column's square, in columns.
// Wherever the camera stands inside its column, every block within ViewRadius columns of it lies in a column in view,
// so a fog that ends ViewRadius * 16 blocks away never shows a missing column.
//
// - In view (gap <= ViewRadius): meshed in full and drawn.
// - Far (far LOD, phase 9 S1; ViewRadius < gap <= FarRadius): drawn as a far mesh (Meshing/VoxelFarMesher.h). The drawn
//   radius is D = max(ViewRadius, FarRadius); every drawn column is final, and the stage rings and the unload distance
//   grow from D. With FarRadius <= ViewRadius every set is exactly what it was without far LOD.
// - Needed (drawn, or beside a column drawn): generated, because a section meshes from its 3 x 3 neighbourhood.
// - Unloaded (gap > D + Stages + UnloadMargin): beyond the generated ring plus the margin, as the prompt's
//   "unload radius = load radius + 2". Columns between needed and unloaded stay as they are, so moving back and
//   forth across a column border loads and unloads nothing (hysteresis).
//
// With decoration (phase 6, S3) each stage needs the 3 x 3 of the one before it, so the needed set grows by a ring per
// stage (Stages 3): ring 0 is drawn and meshed, ring 1 final, ring 2 decorated and ring 3 generated. Each ring is
// the one inside it grown by a column on both axes, a dilation, not a circle (scout C: 545, 657, 777 and 905 columns
// at radius 12). Stages 1 is the needed set without decoration.
struct UNNAMEDVOXEL_API FVoxelStreamingRings
{
    // A needed column's gap is at most ViewRadius + Stages sqrt(2) (a ring is a dilation on both axes), and a diagonal
    // move adds up to sqrt(2): with less past the rings, moving back and forth across a border would unload and reload
    // columns at the corners. Without decoration that is 2 (GetMinUnloadMargin(1)); with it, 3.
    static constexpr int32 MinUnloadMargin = 2;
    static int32 GetMinUnloadMargin(int32 InStages)
    {
        return FMath::CeilToInt32((InStages + 1) * UE_SQRT_2) - InStages;
    }
    // Rings past the view: 1 without decoration (generated), 3 with it (final, decorated, generated).
    static constexpr int32 MaxStages = 3;

    FIntPoint Center = FIntPoint::ZeroValue;
    int32 ViewRadius = 0;
    int32 UnloadMargin = MinUnloadMargin;
    int32 Stages = 1;
    // Far LOD: columns past ViewRadius and up to FarRadius are drawn as far meshes; 0 (or anything up to ViewRadius)
    // draws none.
    int32 FarRadius = 0;

    FVoxelStreamingRings() = default;
    // Clamps the radii to at least 0, the stages to 1..MaxStages and the margin to at least GetMinUnloadMargin.
    FVoxelStreamingRings(
        FIntPoint InCenter, int32 InViewRadius, int32 InUnloadMargin, int32 InStages = 1, int32 InFarRadius = 0);

    // D: every column within it is drawn, in full or as a far mesh.
    int32 GetDrawnRadius() const { return FMath::Max(ViewRadius, FarRadius); }

    // The squared gap in columns between the center column and Column: 0 for the center's 3 x 3.
    int64 GetGapSquared(FIntPoint Column) const;
    // Meshed in full (gap <= ViewRadius).
    bool IsInView(FIntPoint Column) const;
    // Drawn (gap <= D), and drawn as a far mesh (ViewRadius < gap <= D).
    bool IsDrawn(FIntPoint Column) const;
    bool IsFar(FIntPoint Column) const { return IsDrawn(Column) && !IsInView(Column); }
    // Within Stages rings of the drawn columns.
    bool IsNeeded(FIntPoint Column) const;
    // 0 drawn, then the number of columns (on both axes) to the nearest column drawn; INDEX_NONE beyond the needed
    // rings.
    int32 GetRing(FIntPoint Column) const;
    bool IsBeyondUnload(FIntPoint Column) const;
    // Columns farther than this (as a gap) unload.
    int32 GetUnloadDistance() const { return GetDrawnRadius() + Stages + UnloadMargin; }
    // Every needed column lies within this many columns of the center on both axes.
    int32 GetNeededExtent() const { return GetDrawnRadius() + 1 + Stages; }

    bool operator==(const FVoxelStreamingRings& Other) const
    {
        return Center == Other.Center && ViewRadius == Other.ViewRadius && UnloadMargin == Other.UnloadMargin &&
            Stages == Other.Stages && FarRadius == Other.FarRadius;
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

    // Every column within Radius of From (a gap, as IsDrawn and IsBeyondUnload measure it) but not within Radius of To,
    // by Y, then X: what a move from From to To leaves behind. It visits only those columns, not the whole disk.
    UNNAMEDVOXEL_API void ForEachLeft(FIntPoint From, FIntPoint To, int32 Radius, TFunctionRef<void(FIntPoint)> Visit);
}

// GetNeededColumns' order as offsets from the center, with each offset's ring (far LOD step 1b). The order and the
// rings depend only on the offsets, so one built at center 0 serves every center: shifting it gives exactly what
// GetNeededColumns gives there, without its scan, its sort or a GetRing per column.
struct UNNAMEDVOXEL_API FVoxelNeededOrder
{
    // What it was built for; ViewRadius -1 is none.
    int32 ViewRadius = -1;
    int32 FarRadius = 0;
    int32 Stages = 1;
    int32 ViewSector = 0;
    // The rings' GetNeededExtent: every offset lies within it on both axes.
    int32 Extent = 0;
    // The needed columns around center 0, in priority order, and the ring of each.
    TArray<FIntPoint> Offsets;
    TArray<uint8> OffsetRings;

    FVoxelNeededOrder() = default;
    FVoxelNeededOrder(const FVoxelStreamingRings& Rings, int32 InViewSector);
    // Built for these rings' radii and stages (any center) and this sector.
    bool Matches(const FVoxelStreamingRings& Rings, int32 InViewSector) const;
    // An offset's index in Offsets, or INDEX_NONE when that column is not needed.
    int32 FindIndex(FIntPoint Offset) const;
    // The needed columns around Center, in priority order.
    void GetColumns(FIntPoint Center, TArray<FIntPoint>& Out) const;

private:
    // Per offset within Extent on both axes, row by row: its index in Offsets, or INDEX_NONE.
    TArray<int32> Indices;
};
