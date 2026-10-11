#pragma once

#include "CoreMinimal.h"
#include "Density/VoxelDensityEvaluator.h"
#include "Density/VoxelDensityGraph.h"

class IVoxelTerrain;

// What the world asks of the spawn search: today's nearest-land search (captures pass -VoxelSpawnAt or
// -VoxelSpawnExact) or the climate search.
struct FVoxelSpawnRequest
{
    // The nearest-land search from Center, out to ViewReach, as before the climate search; the climate search
    // otherwise.
    bool bNearest = false;
    FIntPoint Center = FIntPoint::ZeroValue;
    // Every candidate checked against its real column instead of the estimate first.
    bool bExact = false;
    // The nearest-land search's reach in blocks: max(8, voxel.ViewRadius x 16).
    int32 ViewReach = 192;
};

struct FVoxelSpawn
{
    // The feet cell.
    FIntVector Cell = FIntVector::ZeroValue;
    // The climate search found dry land: once the columns around it exist, the player stands on the highest block that
    // collides there (GetStandingZ), a treetop included.
    bool bStandOnBlocks = false;
};

// A new world's spawn the way 26.3 picks it (Docs/VoxelWorld.md, Spawn): the climate search scores 66 points out to
// 2048 blocks, then 862 around the best, by the preset's spawn_target plus a distance penalty, and the terrain checks
// the columns within SpawnRadius of that pick's for dry land (IVoxelTerrain::FindSpawn). Pure and deterministic: a
// function of the preset and the seed. Immutable after Initialize; any thread may search.
class UNNAMEDVOXEL_API FVoxelSpawnFinder
{
public:
    using FTarget = TArray<FVoxelTerrainPreset::FSpawnRange>;

    // Compiles the targets' functions over Preset's graph. A preset without targets searches by distance alone, so
    // the origin wins.
    bool Initialize(const FVoxelTerrainPreset& Preset, uint64 WorldSeed, FString* OutError = nullptr);
    bool IsInitialized() const { return bInitialized; }

    // The climate's pick (the interim spawn), sampled at y 0.
    FIntPoint FindClimateSpot() const;

    // Stage 1's candidates: the origin, then rings of radius 512, 1024, 1536 and 2048 with ceil(2 pi r / 512) points
    // each, at angles 2 pi k / n from +X, rounded to the nearest block.
    static void GetFirstCandidates(TArray<FIntPoint>& Out);
    // Stage 2's: rings of radius 32 to 512 in steps of 32 around Center, with ceil(2 pi r / 32) points each.
    static void GetSecondCandidates(FIntPoint Center, TArray<FIntPoint>& Out);
    // (x^2 + y^2)^2 / 390625, rounded down: a full quantized climate unit squared at 2500 blocks from the origin.
    static int64 GetDistancePenalty(FIntPoint Point);
    // The smallest, over the targets, sum of squared distances outside each range; Value gives a node's quantized
    // value. 0 without targets.
    static int64 GetClimateDistance(TConstArrayView<FTarget> Targets, TFunctionRef<int64(int32 Node)> Value);

    // The spawn for a request. The climate search asks Terrain->FindSpawn; when nothing near its pick is dry, the
    // square rings of columns beyond that (SpawnRadius + 1 to FallbackReach) are searched with real heights
    // (IVoxelTerrain::FindDryRing), and failing that the player starts at the water's surface over the pick. Terrains
    // without a climate search (flat, the stand-in) and the bNearest request take the nearest-land search, whose
    // fallback is the top solid block at the centre (the sea floor).
    static FVoxelSpawn Find(const IVoxelTerrain& Terrain, const FVoxelSpawnRequest& Request);
    // The nearest-land search: square rings outward from Center to Reach; the first column whose top solid block is
    // dry land wins. Unless bExact, IsDry's estimate picks the columns and at most 64 are confirmed against the real
    // column (with bExact, every column within 32 blocks).
    static TOptional<FIntVector> FindNearestDry(
        const IVoxelTerrain& Terrain, FIntPoint Center, int32 Reach, bool bExact);
    // The feet Z on a block column: one above the highest Z from Top down to Bottom where Collides holds; Fallback when
    // none does.
    static int32 GetStandingZ(TFunctionRef<bool(int32 Z)> Collides, int32 Bottom, int32 Top, int32 Fallback);

    // The square rings of columns around Center from FirstRing to LastRing; Look gives a column's dry spot, if any.
    // Per ring, Look runs on every column (in parallel when bParallel, so it must be thread-safe), then the first set
    // result in the ring's order (IVoxelTerrain::FindDryRing) wins, so the answer does not depend on threads. Stops at
    // the first ring with a hit.
    static TOptional<FIntVector> SearchRings(FIntPoint Center, int32 FirstRing, int32 LastRing,
        TFunctionRef<TOptional<FIntVector>(FIntPoint Column)> Look, bool bParallel);

    // 26.3 checks the chunks within 5 of the pick's.
    static constexpr int32 SpawnRadius = 5;
    // The fallback's last ring, in columns (512 blocks) around the pick's column.
    static constexpr int32 FallbackReach = 32;

private:
    // Keeps the candidate with the smallest fitness; ties keep the earlier one (Best first).
    void PickBest(TConstArrayView<FIntPoint> Candidates, FIntPoint& Best, int64& BestFitness) const;

    TArray<FTarget> Targets;
    FVoxelDensityEvaluator Evaluator;
    bool bInitialized = false;
};
