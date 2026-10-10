#pragma once

#include "CoreMinimal.h"

// The six faces of a cell in the block world's order (EFoundationFace). The voxel core cannot see the game module, so
// it repeats the order; VoxelBlocks.cpp checks the two agree.
enum class EVoxelFace : uint8
{
    West, // -X
    East, // +X
    North, // -Y
    South, // +Y
    Down, // -Z
    Up // +Z
};

namespace VoxelFaces
{
    constexpr int32 Count = 6;
    // The axis a face looks along (0 X, 1 Y, 2 Z) and whether it looks toward +.
    constexpr int32 GetAxis(int32 Face)
    {
        return Face >> 1;
    }

    constexpr bool IsPositive(int32 Face)
    {
        return (Face & 1) != 0;
    }

    constexpr int32 GetOpposite(int32 Face)
    {
        return Face ^ 1;
    }

    // From the block registry's face codes (FoundationBlockFaces): the face's texture takes the state's tint.
    constexpr uint32 TintBit = 1u << 13;
    // The face is hidden by a full neighbour.
    constexpr uint32 CullBit = 1u << 14;
    // Index of the overlay code after the six face codes.
    constexpr int32 OverlayIndex = 6;
}

// How a state fills its cell; mirrors the registry's EFoundationBlockShape.
enum class EVoxelShape : uint8
{
    Air,
    Full,
    SlabBottom,
    SlabTop,
    Fluid,
    // Snow layers: a box Layers/8 high from the cell's floor.
    Layers,
    // Baked model quads (plants, multiface, custom elements): one alternative of each of its parts
    // (FVoxelBlockTable::GetModelParts), moved by the block's offset.
    Model
};

// The mesh pass, and material slot, a state's faces draw in.
enum class EVoxelLayer : uint8
{
    Opaque,
    Cutout,
    Translucent,
    Water,
    // Model quads: their own section, drawn with the cutout material, never in Lumen's cards.
    Model,
    Count
};

// Where a state's tint comes from (26.3's BlockColors): the grass, foliage, water or dry foliage colour of the block's
// biome; or one colour everywhere (spruce and birch leaves, the lily pad, and every tint without biomes).
enum class EVoxelTintSource : uint8
{
    None,
    Constant,
    Grass,
    Foliage,
    Water,
    // Leaf litter: the dry_foliage colormap, or the biome's dry_foliage_color.
    DryFoliage
};

// 26.3's position offset (minecraft.wiki "Block offset"): a function of the cell's horizontal position only.
enum class EVoxelOffset : uint8
{
    None,
    // Along X and Y (horizontal), within the block's range.
    Horizontal,
    // Also down, within [OffsetDown, 0].
    HorizontalAndDown
};

// One baked model quad, copied from the block registry (FFoundationModelQuad) so the core stays UObject-free.
struct FVoxelModelQuad
{
    // Block units from the cell's minimum corner, in FaceInfo order. The Mojang-to-Unreal axis swap mirrors it, so it
    // turns against the quad's normal, as the mesher's cube faces do.
    FVector3f Positions[4];
    // The texture UV at each vertex (the face's UV rotation included); the material applies no transform.
    FVector2f UVs[4];
    // A face code (FoundationBlockFaces): the texture index, with VoxelFaces::TintBit when the quad takes the tint.
    uint32 Code = 0;
    // The EVoxelFace whose covering neighbour hides the quad; -1 when nothing hides it.
    int8 CullFace = -1;
    // The EVoxelFace the quad is shaded as (shade_direction_override, else its facing): its normal.
    uint8 Shade = 0;
    // The EVoxelFace nearest the quad's normal.
    uint8 Facing = 0;
    // Ambient occlusion, in a model that takes it, for a quad lying flat within 1/16 of a cell side: it takes the
    // corner occlusion a full face there would. Near the side it faces (a cactus side) that is the face of the cell
    // beyond it, an EVoxelFace; near the side behind it (a carpet top) the face of its own cell, OwnCell. -1 stays
    // open.
    static constexpr int8 OwnCell = 6;
    int8 OcclusionFront = -1;
    // The element's light_emission, 0-15.
    uint8 Light = 0;
};

// What generation and meshing need to know about one block state.
struct FVoxelStateInfo
{
    EVoxelShape Shape = EVoxelShape::Air;
    EVoxelLayer Layer = EVoxelLayer::Opaque;
    // One bit per EVoxelFace: faces that cover their whole square with an opaque surface. A neighbour's face against
    // one of them is hidden.
    uint8 OpaqueFaces = 0;
    // Has a collision box: blocks movement and counts for the motion-blocking heightmaps.
    bool bSolid = false;
    // Darkens neighbouring vertices (ambient occlusion): full cubes that are not see-through.
    bool bCastsOcclusion = false;
    bool bLeaves = false;
    uint8 LightEmission = 0;
    // Fluids: the amount 1-8 (a source holds 8); 0 for anything else.
    uint8 FluidAmount = 0;
    // Layers: how many eighths of the cell the box fills, 1-7.
    uint8 Layers = 0;
    // The block this state belongs to. Faces between two states of one see-through block are hidden (glass, ice).
    uint16 Block = 0;
    // Packed sRGB 0xRRGGBB, for faces whose code carries the tint bit: the plains colour for biome tints, which
    // the mesher uses where a block has no biome.
    uint32 Tint = 0;
    EVoxelTintSource TintSource = EVoxelTintSource::None;
    // Six face codes in EVoxelFace order, then the overlay code (FoundationBlockFaces).
    uint32 Faces[7] = {};
    // Model states: the first of their parts in the table (FVoxelBlockTable::GetModelParts) and how many.
    int32 ModelParts = 0;
    uint8 ModelPartCount = 0;
    // Model states holding water (kelp, seagrass, corals, waterlogged ones): the fluid state drawn around them, which
    // also counts for the motion-blocking heightmaps; 0 for none.
    uint16 HeldFluid = 0;
    // The block's position offset, in blocks: up to OffsetHorizontal either way along X and Y, and for
    // HorizontalAndDown down to OffsetDown (negative).
    EVoxelOffset Offset = EVoxelOffset::None;
    float OffsetHorizontal = 0;
    float OffsetDown = 0;

    bool IsAir() const { return Shape == EVoxelShape::Air; }
    bool IsFluid() const { return Shape == EVoxelShape::Fluid; }
    // A fluid, or a model state holding one.
    bool HoldsFluid() const { return Shape == EVoxelShape::Fluid || HeldFluid != 0; }
    // The height of a fluid's surface within its cell when no fluid is above (a source fills 8/9).
    float GetFluidHeight() const { return FluidAmount / 9.f; }
};

// Baked models' offsets and alternatives: pure functions of the cell, as every random value in the core is. The offset
// reads only the horizontal position (26.3's offsets ignore the seed and Y, which keeps two-cell plants and dripstone
// columns aligned); the pick reads the whole cell and the part.
namespace VoxelModels
{
    // The offset a state's model takes at a world cell (X and Y horizontal), in blocks.
    UNNAMEDVOXEL_API FVector3f GetOffset(const FVoxelStateInfo& Info, int32 X, int32 Y);
    // Which of Count alternatives a state's part draws at a world cell.
    UNNAMEDVOXEL_API int32 PickAlternative(int32 X, int32 Y, int32 Z, int32 Part, int32 Count);
}

// Every block state the world can hold, indexed by state id; state 0 is air. Built once on the game thread from the
// block registry (VoxelBlocks::MakeTable), then shared read-only by every worker.
class UNNAMEDVOXEL_API FVoxelBlockTable
{
public:
    FVoxelBlockTable();

    // Unknown ids read as air.
    const FVoxelStateInfo& Get(uint16 State) const { return State < States.Num() ? States[State] : States[0]; }
    int32 GetStateCount() const { return States.Num(); }
    void SetState(uint16 State, const FVoxelStateInfo& Info);

    // A block's default state by id ("stone"); 0 (air) when the block is unknown.
    uint16 FindState(FName Block) const;
    // A block by id, with or without the "minecraft:" prefix, as its states' Block; INDEX_NONE when the table has no
    // such block. Air is block 0.
    int32 FindBlock(FStringView Id) const;
    // One more than the highest Block of any state: the size of a set over blocks.
    int32 GetBlockLimit() const { return BlockLimit; }
    void SetDefaultState(FName Block, uint16 State);
    // A state's snowy=true variant (grass block, podzol, mycelium under snow); State itself when it has none.
    uint16 GetSnowyState(uint16 State) const;
    void SetSnowyState(uint16 State, uint16 Snowy);

    // Baked models, in the block registry's order: their quads and whether they take ambient occlusion. Adding one
    // finds each quad's OcclusionFront. Returns the model's index.
    int32 AddModel(TConstArrayView<FVoxelModelQuad> Quads, bool bAmbientOcclusion);
    int32 GetModelCount() const { return ModelRanges.Num(); }
    TConstArrayView<FVoxelModelQuad> GetModelQuads(int32 Model) const
    {
        return TConstArrayView<FVoxelModelQuad>(ModelQuads.GetData() + ModelRanges[Model].X, ModelRanges[Model].Y);
    }
    // A model state's parts, each a list of alternative models; returns the first, for FVoxelStateInfo::ModelParts.
    int32 AddModelParts(TConstArrayView<TConstArrayView<int32>> Parts);
    // A model state's parts from Info.ModelParts: for each of Info.ModelPartCount parts, its alternative count, then
    // that many model indices.
    const int32* GetModelParts(const FVoxelStateInfo& Info) const { return PartData.GetData() + Info.ModelParts; }

private:
    TArray<FVoxelStateInfo> States;
    TArray<FVoxelModelQuad> ModelQuads;
    // Per model: its first quad and quad count.
    TArray<FIntPoint> ModelRanges;
    TArray<int32> PartData;
    int32 BlockLimit = 1;
    TMap<FName, uint16> DefaultStates;
    TMap<uint16, uint16> SnowyStates;
};
