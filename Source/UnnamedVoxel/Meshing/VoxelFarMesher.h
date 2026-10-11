#pragma once

#include "CoreMinimal.h"
#include "Meshing/VoxelMesher.h"
#include "VoxelConstants.h"

class FVoxelBlockTable;
class FVoxelColumn;
class FVoxelTintTable;

// Far LOD, step 1 (phase 9, S1): columns past voxel.ViewRadius and up to voxel.FarRadius are drawn as cheap surface
// meshes. A far mesh is built from the column's surface summary, one block per cell: blocky ground tops merged
// greedily, side walls down to each lower neighbour cell, leaf-canopy boxes and fluid tops. It uses the full mesher's
// vertex layout (VoxelMesher::AppendQuad) and tints (VoxelMesher::GetFaceTints), so the chunk materials colour it as
// they colour the near land. It has no plants, logs, underground faces, ambient occlusion or Lumen cards.

// What a state is to the surface summary.
enum class EVoxelFarClass : uint8
{
    // Air, model states (plants, multiface blocks, custom elements), logs and stems: the summary looks through them.
    Skip,
    // Every other solid or full state.
    Ground,
    // The block table's leaves flag.
    Leaves,
    Fluid,
    // A snow layer: lying on the ground or on a canopy, it makes that top snow.
    SnowLayer
};

// Every state's far class, built once from the block table.
class UNNAMEDVOXEL_API FVoxelFarClasses
{
public:
    explicit FVoxelFarClasses(const FVoxelBlockTable& Table);
    EVoxelFarClass Get(uint16 State) const { return State < Classes.Num() ? Classes[State] : EVoxelFarClass::Skip; }
    // Logs and stems, by block id (FVoxelBlockTable::GetDefaultStates; the table carries no tags): the suffixes _log,
    // _wood, _stem and _hyphae. They cover 26.3's #minecraft:logs (logs_that_burn, crimson_stems and warped_stems, the
    // stripped variants included) and the mushroom stem.
    static bool IsLogId(FStringView Id);

private:
    TArray<EVoxelFarClass> Classes;
};

// One cell (local X, Y) of a column's surface. Z values are world Z; NoZ marks a part the cell lacks.
struct FVoxelFarCell
{
    static constexpr int16 NoZ = MIN_int16;

    // Ground: the first ground cell scanning down from the column's WorldSurface height, through skipped states,
    // leaves, snow layers and fluid.
    int16 GroundZ = NoZ;
    // The ground's top face: a snow layer's state when one lies directly on the ground cell, else the ground cell's.
    uint16 TopState = 0;
    // A wall's top block shows SideState's side faces (the ground cell's state); the rest of the wall shows FillState's
    // (the cell under the ground cell when that is ground, else SideState).
    uint16 SideState = 0;
    uint16 FillState = 0;
    uint32 TopTint = 0, SideTint = 0, FillTint = 0;
    // Canopy: the topmost run of leaves above the ground, its top and bottom Z (both inclusive) and its leaves state.
    // Its top face shows CanopyTopState: a snow layer's when one lies directly on the canopy, else the leaves'.
    int16 CanopyTop = NoZ, CanopyBottom = NoZ;
    uint16 CanopyState = 0, CanopyTopState = 0;
    uint32 CanopyTint = 0, CanopyTopTint = 0;
    // Fluid: the topmost fluid cell above the ground (the ground is its floor), and its state.
    int16 FluidZ = NoZ;
    uint16 FluidState = 0;
    uint32 FluidTint = 0;

    bool HasGround() const { return GroundZ != NoZ; }
    bool HasCanopy() const { return CanopyTop != NoZ; }
    bool HasFluid() const { return FluidZ != NoZ; }
};

// A column's surface: what a far mesh reads. A later step fills it from the density graph instead of final blocks.
struct FVoxelFarSummary
{
    FIntPoint Position = FIntPoint::ZeroValue;
    // X fastest, then Y.
    FVoxelFarCell Cells[VoxelConstants::ColumnSize * VoxelConstants::ColumnSize];
    // The lowest GroundZ of any cell: a missing neighbour's skirt reaches down to it. NoZ without ground.
    int16 LowestGround = FVoxelFarCell::NoZ;

    const FVoxelFarCell& Get(int32 X, int32 Y) const { return Cells[Y * VoxelConstants::ColumnSize + X]; }
    FVoxelFarCell& Get(int32 X, int32 Y) { return Cells[Y * VoxelConstants::ColumnSize + X]; }
};

namespace VoxelFarMesher
{
    struct FOptions
    {
        // The canopy boxes' material layer: Cutout, as near leaves draw, or Opaque (voxel.Far.CanopyLayer).
        EVoxelLayer CanopyLayer = EVoxelLayer::Cutout;
    };

    // Summarizes a final column, scanning each cell down from its WorldSurface height. With Tints and Neighborhood (the
    // column at its center, as MeshSection reads them), every tint is the full mesher's for that face
    // (VoxelMesher::GetFaceTints with MeshOptions): the top state's up face, the side and fill states' side faces, the
    // canopy's side and top faces and the fluid's up face. Without them every tint is its state's plains tint
    // (neighbours' summaries only need heights and states). Pure: the same inputs always give the same summary.
    UNNAMEDVOXEL_API void Summarize(const FVoxelFarClasses& Classes, const FVoxelBlockTable& Table,
        const FVoxelColumn& Column, FVoxelFarSummary& Out, const FVoxelTintTable* Tints = nullptr,
        const FVoxelNeighborhood* Neighborhood = nullptr, const FVoxelMeshOptions& MeshOptions = FVoxelMeshOptions());

    // Center's far mesh, appended to Out: positions in blocks from the column's minimum corner, Z the world height, in
    // the full mesher's layout.
    // - Ground tops (TopState's up face, one block above GroundZ), merged greedily by height, state and tint.
    // - Ground walls toward every neighbour cell whose ground is lower, down to it: the wall's top block shows
    //   SideState's side face, the rest FillState's, merged vertically. Across the column's edges the neighbour cell is
    //   in Neighbors[Face] (EVoxelFace order: West, East, North, South); a null neighbour counts as ground at
    //   Center.LowestGround - 1, a skirt.
    // - Canopy boxes: the top (CanopyTopState), the bottom unless ground lies right under it, and the sides, each
    //   minus what a neighbour cell's canopy or ground covers, on Options.CanopyLayer.
    // - Fluid tops at FluidZ plus the fluid's surface height, merged greedily, on the fluid state's own layer.
    // Ground tops and walls go on the opaque layer whatever the state's own layer. No ambient occlusion: every corner
    // is open. Light is the state's emission, as the full mesher writes it.
    UNNAMEDVOXEL_API void Mesh(const FVoxelBlockTable& Table, const FVoxelFarSummary& Center,
        const FVoxelFarSummary* const Neighbors[4], const FOptions& Options, FVoxelMeshData& Out);
}
