#pragma once

#include "CoreMinimal.h"

// Mob pathfinding (survival T6 20b; scout C:\dev\sandbox\scout-mobs\facts.md section 6). A* over block cells for a mob
// of a given height, against a query that describes one cell at a time (so the same search runs on the authority's
// blocks, statics and a test's hand-built world). Cell Z is the cell's bottom in blocks; "provisional" values have a
// row in C:\dev\tools\shift-managers\survival\provisional-values.md.

// One cell as the path search sees it.
struct FFoundationPathCell
{
    // The top of the cell's collision boxes above the cell's bottom, in blocks: 0 none (air, plants), 1 a full block,
    // 0.5 a bottom slab, 1.5 a fence or wall (its box leaves the cell upward).
    float Solid = 0;
    // A cell a mob does not enter or stand in: water, lava. (A cell the world does not know has the collision mobs
    // move against there, e.g. an ungenerated column's solid.)
    bool bAvoid = false;
};

namespace FoundationMobPath
{
    // Scout facts section 6: the search expands at most this many nodes per call.
    constexpr int32 MaxNodes = 256;
    // [survival.r10.ai_defaults] default chosen fall 3: a mob steps off a ledge at most this many cells down.
    constexpr int32 MaxDropCells = 3;
    // Provisional (the 0.42 jump impulse and gravity 0.08 clear a little over a block): the highest rise in feet, in
    // blocks, a jump makes; above StepRise (the 0.6 step height) a move needs headroom for the jump.
    constexpr double JumpRise = 1.25, StepRise = .6;
    // Provisional (an estimate): the shortest block below a stand cell that counts as a floor; one taller than a block
    // (a fence, a wall) is not standable.
    constexpr float MinFloor = .6f;

    // Describes the cell at a block coordinate; a cell the caller has no block for is air.
    using FQuery = TFunctionRef<FFoundationPathCell(const FIntVector& Cell)>;

    // The feet height (in cells, fractional: Cell.Z + floor) a mob HeightCells tall stands at in Cell, or false. The
    // floor is a partial block in Cell itself (feet at its top) or a block of 0.6 to 1 below it with Cell empty;
    // every cell the mob's body overlaps must be empty, and neither Cell, the floor nor those cells may be avoided.
    UNNAMED_API bool GetStand(FQuery Query, const FIntVector& Cell, double HeightCells, double& OutFeet);
    // A path of stand cells from Start (excluded) toward Goal. True when it reaches Goal's cell (or a stand cell in
    // Goal's column within one cell vertically); otherwise OutPath leads to the expanded node closest to Goal
    // (horizontal distance first) and it returns false. OutPath empty: no move. OutExpanded: nodes expanded. Moves go
    // to the eight horizontal neighbours, up by a jump, down by a drop of MaxDropCells, never through a block; a
    // diagonal stays at the start's height and needs both cells beside its corner standable there. Start need not be
    // standable.
    UNNAMED_API bool FindPath(FQuery Query, const FIntVector& Start, const FIntVector& Goal, double HeightCells,
        TArray<FIntVector>& OutPath, int32* OutExpanded = nullptr);
}
