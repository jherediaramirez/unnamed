#pragma once

#include "CoreMinimal.h"

class AVoxelWorld;
class FVoxelBlockTable;
class FVoxelColumnPipeline;

// The torch light probe (session T1, Docs/Records/2026-10-10-Torch-Light-Probe.md), development only: a pool of
// movable point lights at deterministic cave-floor cells near a view, to measure what shadowed local lights cost and
// how they look in caves before torches get real lights (T3). In L_Generated, voxel.LightProbe <N>
// [megalights|vsm|none] [intensity cd] [radius cm] shows N of them around the player's view, and voxel.LightProbe 0
// removes them. The megalights mode needs MegaLights on for the run (r.MegaLights.EnableForProject 1, by -ExecCmds or
// the console); the project default stays off. FVoxelLightShots (-VoxelShotLights) drives the same pool for its
// measurements.
namespace VoxelLightProbe
{
    enum class EMode : uint8
    {
        // Shadowed through MegaLights (ray-traced shadows by default), when MegaLights is on.
        MegaLights,
        // Shadowed by virtual shadow maps, outside MegaLights.
        Vsm,
        // Unshadowed, outside MegaLights.
        None
    };

    struct FSettings
    {
        EMode Mode = EMode::MegaLights;
        double IntensityCd = 8000;
        // A torch's reach: light 14 fades to 1 over 13 blocks.
        double RadiusCm = 1300;
        double SourceRadiusCm = 5;
    };

    bool ParseMode(const FString& Text, EMode& OutMode);
    const TCHAR* GetModeName(EMode Mode);

    // The streamed columns, read in cells. Cells of columns not generated read as stone, so nothing looks or fills
    // into a missing column.
    struct FCells
    {
        explicit FCells(const AVoxelWorld& World);
        bool IsValid() const { return Pipeline && Table; }
        uint16 GetState(const FIntVector& Cell) const;
        bool IsAir(const FIntVector& Cell) const;
        // Has a collision box (FVoxelStateInfo::bSolid).
        bool IsSolid(const FIntVector& Cell) const;
        // A full cube that is not see-through.
        bool IsOpaqueCube(const FIntVector& Cell) const;
        // The top non-air block of the column at X, Y; below the world when the column is not generated.
        int32 GetSurface(int32 X, int32 Y) const;
        // At least three blocks under its column's surface, as the -VoxelShotCaves views count a cave.
        bool IsCovered(const FIntVector& Cell) const;
        // Blocks (cell units from cell (0, 0, 0)'s minimum corner) to world cm, and back.
        FVector ToCm(const FVector& Blocks) const;
        FVector ToBlocks(const FVector& Cm) const;
        static FIntVector ToCell(const FVector& Blocks);
        // A straight line from From to To (blocks) passes only through air after From's cell and before To's; Ignore
        // counts as air and Blocked as solid.
        bool IsClear(const FVector& From, const FVector& To, const FIntVector* Ignore = nullptr,
            const FIntVector* Blocked = nullptr) const;

        const FVoxelColumnPipeline* Pipeline = nullptr;
        TSharedPtr<const FVoxelBlockTable> Table;
        FVector OriginCm = FVector::ZeroVector;
    };

    // The floor cells of the cave around Start, in fill order: a flood fill through covered air from Start (up to 24
    // blocks across and 16 up or down from it), keeping air cells with a solid cell below and air above. Seen, when
    // given, gets every cell filled.
    TArray<FIntVector> FindFloorCells(const FCells& Cells, const FIntVector& Start, TSet<FIntVector>* Seen = nullptr);
    // Light positions (cm) for up to Count lights near a view: the floor cells around the eye's cell, those in front
    // of the view first, then in fill order, each at least 2 blocks from those kept before (then any cell left, when
    // that leaves too few). A light sits 1 m above its cell's floor. The same view gives the same cells, and the
    // first N of a longer list are the N-light case.
    TArray<FVector> FindFloorLights(const FCells& Cells, const FVector& EyeCm, const FVector& Forward, int32 Count);
    // The floor cells FindFloorLights puts its lights over, in the same order.
    TArray<FIntVector> FindFloorLightCells(
        const FCells& Cells, const FVector& EyeCm, const FVector& Forward, int32 Count);

    // `voxel.LightProbe <N> torches` (session T3): a floor torch on each cell, written through the authority's block
    // world, so their lights come from the world's light pool (FVoxelLights) rather than the probe's. Takes back the
    // torches placed before. Returns how many were placed.
    int32 PlaceTorches(AVoxelWorld& World, const TArray<FIntVector>& FloorCells);
    // Puts back what each placed torch replaced, where the torch still stands; returns how many. A torch whose column
    // is unloaded is kept and tried again by the next call (PlaceTorches makes one first).
    int32 RemoveTorches();

    // Shows one light at each position with these settings, owned by the world's actor; the pool grows as needed
    // and its other lights are hidden. Returns how many are shown.
    int32 Show(AVoxelWorld& World, const TArray<FVector>& Positions, const FSettings& Settings);
    // Destroys the pool.
    void Clear();
    // The pool's lights that are registered and visible.
    int32 CountShown();
}
