#pragma once

#include "CoreMinimal.h"
#include "Density/VoxelDensityEvaluator.h"

// Working memory for FVoxelColumnDensity, one per thread.
struct FVoxelColumnDensityScratch
{
    FVoxelDensityScratch Flat, Corners, Blocks;
    TArray<FVoxelDensityBounds> CornerBounds, BlockBounds;
    TArray<double> X, Y, Z;
    // Per interpolated node, its input at every corner of the column's grid.
    TArray<float> CornerValues;
    TArray<uint8> CellStates, CornerNeeded;
    TArray<int32> CornerPoints, CornerFlat;
};

// What one column's density cost.
struct FVoxelColumnDensityStats
{
    int32 Cells = 0;
    int32 SolidCells = 0;
    int32 EmptyCells = 0;
    int32 CornersEvaluated = 0;
    int32 BlocksEvaluated = 0;
    // Stage times: y-independent corner values, cell bounds, corner evaluation and blocks.
    double FlatMs = 0, BoundsMs = 0, CornersMs = 0, BlocksMs = 0;
};

// A density function evaluated over one 16 x 16 column the way 26.3 builds terrain: the parts under `interpolated`
// at the corners of cells (4 x 8 x 4 blocks for the overworld), trilinearly interpolated inside each cell, and the
// parts above it per block. Nodes that never read y run once per corner column (5 x 5), not once per corner.
// Height-bound skipping: before any corner is evaluated, each cell's density is bounded from the column's exact
// y-independent values and conservative bounds of the rest (noise amplitudes, the cell's y range). A cell whose density
// is certainly positive is solid and one whose density is certainly not is empty, so neither is evaluated.
// Immutable after Initialize; any thread may generate with its own scratch, and results equal direct evaluation of the
// function at each block (FVoxelDensityEvaluator::Evaluate) bit for bit, skipped cells aside.
class UNNAMEDVOXEL_API FVoxelColumnDensity
{
public:
    static constexpr int32 ColumnSize = 16;

    // Evaluator must outlive this and have Root compiled. Fails when interpolated nodes disagree on cell size, nest, or
    // do not tile a column.
    bool Initialize(const FVoxelDensityEvaluator& InEvaluator, int32 InRoot, int32 InMinY, int32 InHeight,
        FString* OutError = nullptr);

    // Density at every block of the column whose lowest corner is (X0, Z0) in the reference's axes, indexed
    // ((y - MinY) * 16 + z) * 16 + x. Skipped cells read +1 (solid) or -1 (empty). KeepEmpty (empty, or one flag per
    // block y from MinY) keeps cells touching a flagged y from being skipped as empty, for callers that read the exact
    // value of non-positive density there (aquifer barriers).
    void Generate(int32 X0, int32 Z0, bool bSkip, FVoxelColumnDensityScratch& Scratch, TArray<float>& OutDensity,
        FVoxelColumnDensityStats* OutStats = nullptr, TConstArrayView<uint8> KeepEmpty = {}) const;

    int32 GetMinY() const { return MinY; }
    int32 GetHeight() const { return Height; }

private:
    enum ECellState : uint8
    {
        Mixed,
        Solid,
        Empty
    };

    bool IsKeptEmpty(TConstArrayView<uint8> KeepEmpty, int32 CY) const;
    int32 CornerIndex(int32 CX, int32 CY, int32 CZ) const { return (CY * CornersXZ + CZ) * CornersXZ + CX; }

    const FVoxelDensityEvaluator* Evaluator = nullptr;
    int32 Root = INDEX_NONE;
    int32 MinY = 0, Height = 0;
    int32 CellXZ = ColumnSize, CellY = 0;
    int32 CornersXZ = 2, CornersY = 2;
    // Nodes under interpolated that never read y, those that may, the interpolated nodes, and the per-block nodes
    // above them (interpolated excluded), each in evaluation order.
    TArray<int32> FlatNodes, CornerNodes, InterpolatedNodes, BlockNodes;
};
