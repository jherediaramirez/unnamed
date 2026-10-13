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
// they colour the near land. Optional tree runs retain wood and every above-ground crown tier. It has no plants,
// underground faces, ambient occlusion or Lumen cards.

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
    // Full woody cubes only: crop stems are model states and must not become tree boxes. Kept separately so Get and
    // the legacy surface summary remain unchanged.
    bool IsLog(uint16 State) const { return State < Logs.Num() && Logs[State]; }
    // Logs and stems, by block id (FVoxelBlockTable::GetDefaultStates; the table carries no tags): the suffixes _log,
    // _wood, _stem and _hyphae. They cover 26.3's #minecraft:logs (logs_that_burn, crimson_stems and warped_stems, the
    // stripped variants included) and the mushroom stem. This name-only filter also matches crop stems; IsLog
    // additionally requires the state's full-cube shape.
    static bool IsLogId(FStringView Id);

private:
    TArray<EVoxelFarClass> Classes;
    TBitArray<> Logs;
};

// An actual vertical run of wood or leaves above the heightfield ground, inclusive world Z. Runs are stored from
// top to bottom, split at air gaps and changes of draw state. No fixed tier limit; a column's height bounds the count.
struct FVoxelFarTreeRun
{
    int16 Bottom = 0, Top = 0;
    uint16 State = 0, TopState = 0;
    uint32 Tint = 0, TopTint = 0, BottomTint = 0;
    bool bLeaves = false;
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
    // Optional runs in FVoxelFarSummary::TreeRuns. Empty for the legacy summary.
    int32 TreeRunStart = 0, TreeRunCount = 0;

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
    bool bTrees = false;
    TArray<FVoxelFarTreeRun> TreeRuns;

    const FVoxelFarCell& Get(int32 X, int32 Y) const { return Cells[Y * VoxelConstants::ColumnSize + X]; }
    FVoxelFarCell& Get(int32 X, int32 Y) { return Cells[Y * VoxelConstants::ColumnSize + X]; }
    TConstArrayView<FVoxelFarTreeRun> GetTreeRuns(int32 X, int32 Y) const
    {
        const FVoxelFarCell& Cell = Get(X, Y);
        return MakeArrayView(TreeRuns).Slice(Cell.TreeRunStart, Cell.TreeRunCount);
    }
};

// A neighbour drawn in full, along the edge it shares with a far column (review A3): which of its edge cells leave the
// far column's side open, by height. The near mesher draws only its own cells' faces, so where such a cell lies below
// the neighbour's ground (under an overhang or a roof, in a cave), the far column's wall must show.
struct FVoxelFarBorder
{
    static constexpr int32 Words = VoxelConstants::Height / 64;
    static_assert(VoxelConstants::Height % 64 == 0, "Whole words of heights");

    // Along the edge (Y for West and East, X for North and South), then (Z - MinZ): bit set where the cell does not
    // cover the face it shows the far column.
    uint64 Open[VoxelConstants::ColumnSize][Words];

    bool IsOpen(int32 Along, int32 Z) const
    {
        const int32 Local = Z - VoxelConstants::MinZ;
        return (Open[Along][Local >> 6] >> (Local & 63)) & 1;
    }
};

namespace VoxelFarMesher
{
    struct FOptions
    {
        // The canopy boxes' material layer: Cutout, as near leaves draw, or Opaque (voxel.Far.CanopyLayer).
        EVoxelLayer CanopyLayer = EVoxelLayer::Cutout;
        // voxel.Far.Trees: actual wood and all crown tiers. False preserves the original mesh byte for byte.
        bool bTrees = false;
    };

    // Summarizes a final column, scanning each cell down from its WorldSurface height. With Tints and Neighborhood (the
    // column at its center, as MeshSection reads them), every tint is the full mesher's for that face
    // (VoxelMesher::GetFaceTints with MeshOptions): the top state's up face, the side and fill states' side faces, the
    // canopy's side and top faces and the fluid's up face. Without them every tint is its state's plains tint
    // (neighbours' summaries only need heights and states). Pure: the same inputs always give the same summary.
    UNNAMEDVOXEL_API void Summarize(const FVoxelFarClasses& Classes, const FVoxelBlockTable& Table,
        const FVoxelColumn& Column, FVoxelFarSummary& Out, const FVoxelTintTable* Tints = nullptr,
        const FVoxelNeighborhood* Neighborhood = nullptr, const FVoxelMeshOptions& MeshOptions = FVoxelMeshOptions(),
        bool bTrees = false);

    // The edge cells of Neighbor, the column across Center's Face (EVoxelFace: West, East, North or South), as a far
    // border: a cell is open unless its state's OpaqueFaces covers the face toward Center, as the full mesher culls.
    UNNAMEDVOXEL_API void SummarizeBorder(
        const FVoxelBlockTable& Table, const FVoxelColumn& Neighbor, int32 Face, FVoxelFarBorder& Out);

    // Center's far mesh, appended to Out: positions in blocks from the column's minimum corner, Z the world height, in
    // the full mesher's layout.
    // - Ground tops (TopState's up face, one block above GroundZ), merged greedily by height, state and tint.
    // - Ground walls toward every neighbour cell whose ground is lower, down to it: the wall's top block shows
    //   SideState's side face, the rest FillState's, merged vertically. Across the column's edges the neighbour cell is
    //   in Neighbors[Face] (EVoxelFace order: West, East, North, South); a null neighbour counts as ground at
    //   Center.LowestGround - 1, a skirt. With Borders[Face] (a neighbour drawn in full), the wall also shows below
    //   that wherever the border is open, cell by cell, as SideState at GroundZ and FillState below it.
    // - Canopy boxes: the top (CanopyTopState), the bottom unless ground lies right under it, and the sides, each
    //   minus what a neighbour cell's canopy or ground covers, on Options.CanopyLayer.
    // - Fluid tops at FluidZ plus the fluid's surface height, merged greedily, on the fluid state's own layer.
    // With Options.bTrees, tree runs replace canopy boxes: wood on opaque, leaves on CanopyLayer, with exposed faces
    // merged. Across column edges wood and lower tiers stay capped against vegetation the legacy mode omits; the
    // extra opposing internal caps avoid open tree portions during asynchronous mode changes and near handoffs.
    // Ground tops and walls go on the opaque layer whatever the state's own layer. No ambient occlusion: every corner
    // is open. Light is the state's emission, as the full mesher writes it.
    UNNAMEDVOXEL_API void Mesh(const FVoxelBlockTable& Table, const FVoxelFarSummary& Center,
        const FVoxelFarSummary* const Neighbors[4], const FOptions& Options, FVoxelMeshData& Out,
        const FVoxelFarBorder* const Borders[4] = nullptr);
}
