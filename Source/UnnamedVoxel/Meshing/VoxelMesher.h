#pragma once

#include "CoreMinimal.h"
#include "Meshing/VoxelMeshData.h"

class FVoxelBlockTable;
class FVoxelColumn;
class FVoxelTintTable;

// The states around one section: the section plus a one-cell border copied from its 26 neighbours, X fastest, then
// Y, then Z (padded coordinates 0 and 17 are the border). Faces and ambient occlusion at section and column borders
// read the real neighbours from here, so a section meshes alone, on any thread.
struct FVoxelPaddedSection
{
    static constexpr int32 Size = 18;
    static constexpr int32 Volume = Size * Size * Size;

    static constexpr int32 GetIndex(int32 X, int32 Y, int32 Z) { return (Z * Size + Y) * Size + X; }

    uint16 States[Volume];
};

// The 3 x 3 columns around the one being meshed. A missing column reads as air, which draws the edge of a finite
// area; streaming meshes a column only once its neighbours exist.
struct FVoxelNeighborhood
{
    const FVoxelColumn* Columns[3][3] = {};

    const FVoxelColumn* Get(int32 DX, int32 DY) const { return Columns[DY + 1][DX + 1]; }
    void Set(int32 DX, int32 DY, const FVoxelColumn* Column) { Columns[DY + 1][DX + 1] = Column; }
};

// One section of one column, as an edit marks it dirty.
struct FVoxelSectionKey
{
    FIntPoint Column = FIntPoint::ZeroValue;
    int32 Section = 0;

    bool operator==(const FVoxelSectionKey& Other) const { return Column == Other.Column && Section == Other.Section; }
};

struct FVoxelMeshOptions
{
    // Merge equal neighbouring faces into larger quads; off gives one quad per face (debugging, tests).
    bool bGreedy = true;
    // Full-cube faces merge by look (FVoxelBlockTable::GetLookState), so states that draw alike, such as leaves of
    // every distance, merge with each other; off, they merge only within one state (the bench's comparison).
    bool bMergeLooks = true;
    bool bAmbientOcclusion = true;
    // Biome blend radius for grass, foliage and water tints (26.3's biome blend; its default is 2): a face takes the
    // mean tint of the (2r + 1)^2 blocks around its block. 0 gives each block its own biome's colour; below 0 every
    // block keeps its plains tint.
    int32 TintBlend = 2;
    // Debug and tests: off, every tinted face averages block by block, never taking the uniform-biome shortcut.
    bool bTintShortcut = true;
    // Model states' quads (voxel.Models); off, they draw nothing, though the water they hold still does.
    bool bModels = true;
    // Look-call A/B: model quads whose shade direction differs from their facing are shaded as up, which is what an
    // override left unturned by the blockstate rotation would give the multiface blocks (glow lichen, vine, sculk
    // vein).
    bool bModelShadeUnturned = false;
    // Render-only ore looks (Meshing/VoxelOreLooks.h): voxel.Look.OreHost (0 vanilla, 1 ores on their host rock, 2
    // the palette swap) and voxel.Look.OreGlint (0 vanilla, 1 ore-aware mips and gloss, 3 the debug mask). At 0 and 0
    // the mesh is vanilla, vertex for vertex.
    int32 OreHost = 0;
    int32 OreGlint = 0;
    static constexpr int32 MaxTintBlend = 8;
};

// Section meshing. Full cubes, water surfaces and water floors merge greedily within each 16 x 16 slice, keyed by
// state, ambient-occlusion corners and tint, so a merged quad never blurs different shading or colours. Slabs and water
// sides, which are partial faces, emit one quad each. A face is hidden when its code carries the cull bit and the
// neighbour covers that whole square with an opaque surface, or when the neighbour is the same see-through block (glass
// beside glass); water never draws against water, nor against a model state holding water. Model states append their
// baked quads, unmerged, to the Model layer: one alternative per part (VoxelModels::PickAlternative), moved by the
// block's offset, each hidden when its cull face's neighbour covers that side; their water is drawn around them.
namespace VoxelMesher
{
    UNNAMEDVOXEL_API void GatherPadded(
        const FVoxelNeighborhood& Neighborhood, int32 SectionIndex, FVoxelPaddedSection& Out);
    // Tints and Neighborhood (the columns Padded was gathered from) give grass, foliage and water their biomes'
    // colours (FVoxelMeshOptions::TintBlend); without them every block keeps its plains tint.
    UNNAMEDVOXEL_API void MeshSection(const FVoxelBlockTable& Table, const FVoxelPaddedSection& Padded,
        int32 SectionIndex, const FVoxelMeshOptions& Options, FVoxelMeshData& Out,
        const FVoxelTintTable* Tints = nullptr, const FVoxelNeighborhood* Neighborhood = nullptr);

    // A face's block UV at point P (blocks from the face's first cell): the block registry's canonical UVs
    // (FoundationBlockFaces::GetCanonicalUV), which follow the reference's FaceBakery.
    UNNAMEDVOXEL_API FVector2f GetCanonicalUV(int32 Face, const FVector3f& P);

    // The sections whose padded copy holds a world cell, so whose mesh a change there can alter: its own section and,
    // when it lies on a section or column border, the neighbours reading it as their border. Up to 8 sections in up
    // to 4 columns; none for a cell outside the world's height.
    UNNAMEDVOXEL_API void GetSectionsTouching(
        const FIntVector& Cell, TArray<FVoxelSectionKey, TInlineAllocator<8>>& Out);
}
