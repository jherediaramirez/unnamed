#pragma once

#include "CoreMinimal.h"

class FFoundationBlockRegistry;
class FVoxelBlockTable;
class FVoxelColumn;
struct FVoxelNeighborhood;

// The torch light system's plain core (session T3): which states get a point light, where in their cell it sits, the
// light sources a column holds, and the pool of K lights picked around the camera. No UObjects: the scan runs on a
// worker inside the column mesh job, and the pick on whatever thread owns the pool.
//
// Coordinates are block units in world cell space, Unreal axes: X east, Y south, Z up (Minecraft north is -Y, west
// -X). Cell (X, Y, Z) spans [X, X + 1) x [Y, Y + 1) x [Z, Z + 1) from its minimum corner, as the registry's shapes do;
// a column at position (CX, CY) holds cells X = CX * 16 + 0..15, Y = CY * 16 + 0..15 and Z = VoxelConstants::MinZ to
// MaxZ. One block is UFoundationProfile::BlockCm (100 cm).

// Where a state's point light sits.
enum class EVoxelLightShape : uint8
{
    None,
    // The standing torch-rule block: 1 pixel above its 2 x 10 x 2 pixel stick.
    FloorTorch,
    // The torch-rule pair's wall block: 1 pixel past the top of the stick leaning out from its wall.
    WallTorch,
    // A full opaque cube (glowstone, sea lantern, froglights, crying obsidian): just outside its open faces, per axis.
    Cube,
    // Any other emitting state (cave vines with berries, sea pickles, lanterns): the cell centre.
    Model
};

// One state's entry in the light table.
struct FVoxelLightInfo
{
    // The light level, 0 when the state gets no point light. A light's attenuation radius is Level - 1 blocks.
    uint8 Level = 0;
    EVoxelLightShape Shape = EVoxelLightShape::None;
    // Wall torches: the EVoxelFace the torch faces, away from its wall (facing east hangs on the west neighbour).
    uint8 Facing = 0;
    // An opaque full cube: it closes the face of a cube emitter next to it.
    bool bOpaqueCube = false;
};

// The light table: one FVoxelLightInfo per state id (VoxelLightSources::MakeTable), shared read-only once built.
// Unknown ids read as state 0's entry (air).
class FVoxelLightTable
{
public:
    explicit FVoxelLightTable(int32 StateCount = 1, int32 InMinLevel = 0) : MinLevel(InMinLevel)
    {
        States.SetNum(FMath::Max(1, StateCount));
    }

    const FVoxelLightInfo& Get(uint16 State) const { return State < States.Num() ? States[State] : States[0]; }
    int32 GetStateCount() const { return States.Num(); }
    void SetState(uint16 State, const FVoxelLightInfo& Info) { States[State] = Info; }
    // The level a state's emission must reach for a light.
    int32 GetMinLevel() const { return MinLevel; }

private:
    TArray<FVoxelLightInfo> States;
    int32 MinLevel = 0;
};

// One light-emitting cell of a column, as the scan finds it.
struct FVoxelLightSource
{
    // The world cell.
    FIntVector Cell = FIntVector::ZeroValue;
    // The light's point: the cell's minimum corner plus the shape's point within it.
    FVector Point = FVector::ZeroVector;
    uint8 Level = 0;
    // The cell's key (VoxelLightSources::MakeKey): stable for as long as the block stays.
    uint64 Key = 0;
};

// One light of the pool: a seed source and the sources merged into it.
struct FVoxelLightPick
{
    // The members' points weighted by their intensity weights (VoxelLightSources::GetWeight).
    FVector Centroid = FVector::ZeroVector;
    // The seed's point: later sources merge by their distance to it, so a pick never drifts toward more sources.
    FVector SeedPoint = FVector::ZeroVector;
    FIntVector SeedCell = FIntVector::ZeroValue;
    // The members' summed weights: a torch is 1.
    float Weight = 0;
    // The brightest member's level; the light's attenuation radius is Level - 1 blocks.
    uint8 Level = 0;
    int32 Members = 0;
    // The seed cell's key: a light component keeps its pick from frame to frame by it.
    uint64 Key = 0;
};

namespace VoxelLightSources
{
    // States below this light level get no point light (a torch is 14, crying obsidian 10, lit redstone ore 9).
    constexpr int32 DefaultMinLevel = 10;
    // The pool's cap: K is clamped to [0, MaxLights].
    constexpr int32 MaxLights = 100;
    // Sources closer than this (strictly) to a pick's seed join it.
    constexpr double DefaultMergeDistance = 2;
    // A source's weight is its level over a torch's.
    constexpr int32 TorchLevel = 14;
    // Light points, in blocks. A standing torch: 1 pixel above its 10 pixel stick. A wall torch: the same stick from
    // 3.5 to 13.5 pixels up, its base at the wall face, turned 22.5 degrees away from the wall about the base, and the
    // point 1 pixel past its top (11 pixels from the base): out from the wall and up from the cell's floor. A cube: its
    // offset from the cell centre on each axis with a net open direction (GetLocalPoint).
    constexpr double FloorTorchHeight = 11 / 16.;
    constexpr double WallTorchSin = 0.38268343236508978; // sin 22.5 degrees
    constexpr double WallTorchCos = 0.92387953251128674; // cos 22.5 degrees
    constexpr double WallTorchOut = 11 * WallTorchSin / 16;
    constexpr double WallTorchHeight = (3.5 + 11 * WallTorchCos) / 16;
    constexpr double CubeOffset = 0.6;

    // The light table for a registry and the voxel block table built from it (VoxelBlocks::MakeTable): a state gets a
    // light when the light emission the mesher glows it with (FVoxelStateInfo::LightEmission) is at least MinLevel and
    // it is not lava. The shape comes from the registry's torch rule (bTorchRule; the pair's wall block, the one named
    // by ItemWallBlock, takes its facing from "facing") and the voxel table (an opaque full cube is a Cube; the rest a
    // Model). Game thread, like the tables it reads.
    UNNAMED_API TSharedRef<const FVoxelLightTable> MakeTable(
        const FFoundationBlockRegistry& Registry, const FVoxelBlockTable& Blocks, int32 MinLevel = DefaultMinLevel);

    // The voxel table the world meshes with for voxel.TorchGlow: a copy of Blocks in which only the registry's
    // torch-rule states (bTorchRule: torch, wall_torch) glow at Glow (FVoxelStateInfo::LightEmission, clamped 0-15),
    // so the stick's texture is not blown out to white; every other state (glowstone, glow lichen, lava, ...) and the
    // models are as in Blocks. Blocks itself when no state would change (Glow 14, the torches' own level). The light
    // table is still made from the unlowered Blocks (MakeTable), so the torch lights stay at 14. Game thread.
    UNNAMED_API TSharedRef<const FVoxelBlockTable> MakeTorchGlowTable(
        const FFoundationBlockRegistry& Registry, const TSharedRef<const FVoxelBlockTable>& Blocks, int32 Glow);

    // The point of a state's light within its cell, from the cell's minimum corner. OpenFaces, for cubes, holds a bit
    // per EVoxelFace whose neighbour is not an opaque full cube. A cube's point is CubeOffset from the centre along
    // every axis where exactly one of the two faces is open (toward it); an axis with both or neither open stays
    // centred. One open face gives centre +-0.6 on its axis, east and up (0.5 + 0.6, 0.5, 0.5 + 0.6), three such axes
    // 0.6 on each. When every axis nets zero, the point is above the top if it is open, else past the first open
    // face. False when the state gets no light, or when a cube has no open face (buried).
    UNNAMED_API bool GetLocalPoint(const FVoxelLightInfo& Info, uint8 OpenFaces, FVector& OutPoint);

    // A cell's key: unique for |X|, |Y| < 2^26 and Z within the world's height.
    UNNAMED_API uint64 MakeKey(const FIntVector& Cell);

    // A source's intensity weight: Level / TorchLevel.
    inline float GetWeight(int32 Level)
    {
        return float(Level) / float(TorchLevel);
    }

    // Appends the light sources of one column (Position: its column coordinate) to Out, sections bottom to top and
    // cells in section index order (X fastest, then Y, then Z), so the order is deterministic. Neighborhood supplies
    // the columns around it for the faces of cubes at the column's edges; a missing column, like the cells above and
    // below the world, reads as air. An empty or uniform section costs one lookup, and a paletted one whose palette
    // holds no light one lookup per entry. Any thread.
    UNNAMED_API void GatherSources(const FVoxelColumn& Column, FIntPoint Position,
        const FVoxelNeighborhood& Neighborhood, const FVoxelLightTable& Table, TArray<FVoxelLightSource>& Out);

    // The pool: the sources whose points lie within Radius of Camera, ordered by (distance squared, cell X, Y, Z);
    // in that order each joins the nearest pick whose seed point is closer than MergeDistance (the first of equals),
    // else seeds a new pick while there are fewer than Count (clamped to [0, MaxLights]), else is dropped. Out holds
    // the picks in seed order, the same for any order of LightSources.
    UNNAMED_API void PickLights(TConstArrayView<FVoxelLightSource> LightSources, const FVector& Camera, int32 Count,
        double Radius, double MergeDistance, TArray<FVoxelLightPick>& Out);
}
