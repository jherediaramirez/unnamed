#pragma once

#include "CoreMinimal.h"
#include "Meshing/VoxelMesher.h"

class FVoxelBlockTable;
class FVoxelColumn;

// A square of columns generated and meshed in one go: phase 2's fixed test area and the column bench. Phase 3's
// streaming replaces it with per-column jobs; the stages and their timings stay the same.
struct UNNAMEDVOXEL_API FVoxelArea
{
    struct FSectionMesh
    {
        // Index into Columns.
        int32 Column = 0;
        int32 Section = 0;
        FVoxelMeshData Mesh;
    };

    FIntPoint Center = FIntPoint::ZeroValue;
    int32 Radius = 0;
    // (2 * Radius + 1)^2 columns, row by row from the minimum corner.
    TArray<TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>> Columns;
    // Only sections that produced geometry.
    TArray<FSectionMesh> Meshes;

    // Per-stage timings in milliseconds, one entry per column or per section (all sections, meshed or skipped).
    TArray<double> GenerateMs, GatherMs, MeshMs;
    double GenerateWallMs = 0, MeshWallMs = 0;
    int32 Workers = 1;

    // Builds the area: every column, then every section with its neighbours (columns outside the area read as air,
    // so the edges show their cross-section). Parallel runs one task per column or section.
    static void Build(const FVoxelBlockTable& Table, int64 Seed, FIntPoint Center, int32 Radius,
        const FVoxelMeshOptions& Options, bool bParallel, FVoxelArea& Out);

    int32 GetColumnIndex(FIntPoint Column) const;
    int64 GetQuadCount() const;
    SIZE_T GetColumnBytes() const;
    // Several lines: per-stage mean and worst times, quads, memory per column.
    FString Describe() const;
};
