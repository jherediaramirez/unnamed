#pragma once

#include "CoreMinimal.h"
#include "Async/ParallelFor.h"
#include "Decoration/VoxelDecorator.h"
#include "Meshing/VoxelMesher.h"

class FVoxelBlockTable;
class FVoxelColumn;
class IVoxelTerrain;

// The decoration stages over a square of columns (phase 6, S2): Generate on the radius + 2 square (the decorator's
// snapshot), Decorate on radius + 1, Finalize on the radius, then the decorator's finishing pass (the freeze). Each
// stage runs on one thread or across every worker, its columns in row order or reversed; every write log is kept
// until the last Finalize. Streaming (S3) gives the same final columns.
struct UNNAMEDVOXEL_API FVoxelDecoratedArea
{
    FIntPoint Center = FIntPoint::ZeroValue;
    int32 Radius = 0;
    // (2 Radius + 1)^2 final columns, row by row from the minimum corner.
    TArray<TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>> Columns;
    // Per column of each stage's square, row by row. FinalizeMs includes FinishMs, the finishing pass.
    TArray<double> GenerateMs, DecorateMs, FinalizeMs, FinishMs;
    TArray<FVoxelDecorateStats> Decorates;
    TArray<FVoxelFinalizeStats> Finalizes;
    double GenerateWallMs = 0, DecorateWallMs = 0, FinalizeWallMs = 0;

    static void Build(const FVoxelDecorator& Decorator, const IVoxelTerrain& Terrain, FIntPoint Center, int32 Radius,
        bool bParallel, bool bReverse, FVoxelDecoratedArea& Out);
};

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
    // With decoration: the Decorate and Finalize stages (GenerateMs then covers the radius + 2 square).
    TArray<double> DecorateMs, FinalizeMs;
    double DecorateWallMs = 0, FinalizeWallMs = 0;
    int32 Workers = 1;

    // Builds the area: every column from Terrain (decorated when Decorator is set), then every section with its
    // neighbours (columns outside the area read as air, so the edges show their cross-section). Parallel runs one task
    // per column or section.
    static void Build(const FVoxelBlockTable& Table, const IVoxelTerrain& Terrain, FIntPoint Center, int32 Radius,
        const FVoxelMeshOptions& Options, bool bParallel, FVoxelArea& Out, const FVoxelDecorator* Decorator = nullptr);

    int32 GetColumnIndex(FIntPoint Column) const;
    int64 GetQuadCount() const;
    SIZE_T GetColumnBytes() const;
    // Several lines: per-stage mean and worst times, quads, memory per column.
    FString Describe() const;

private:
    static void BuildColumns(
        const IVoxelTerrain& Terrain, FIntPoint Center, int32 Radius, EParallelForFlags Flags, FVoxelArea& Out);
    static void BuildMeshes(const FVoxelBlockTable& Table, const IVoxelTerrain& Terrain,
        const FVoxelMeshOptions& Options, EParallelForFlags Flags, FVoxelArea& Out);
};
