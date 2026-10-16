#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FoundationBlockAssets.generated.h"

class UTexture2D;
class UTexture2DArray;

// The registry data written by Scripts/Editor/ImportBlocks.py from Reference/minecraft-blocks-26.3. Everything here
// is generated (properties are editable only so the import can write them); edit the reference or the import, never
// the asset. FFoundationBlockRegistry turns it into the runtime lookup tables.

// Which mesh and collision a state has. A fluid's height comes from its level. Layers1 to Layers7 are snow layers, a
// box k/8 high from the cell's floor (8 layers are Full). Model states are drawn from baked model quads
// (UFoundationBlockAssets::Models), with outline and collision boxes from the block's per-state facts.
UENUM()
enum class EFoundationBlockShape : uint8
{
    None,
    Full,
    SlabBottom,
    SlabTop,
    Fluid,
    Layers1,
    Layers2,
    Layers3,
    Layers4,
    Layers5,
    Layers6,
    Layers7,
    Model
};

// 26.3's position offset (minecraft.wiki "Block offset"): a function of the cell's X and Z only. XYZ also lowers the
// block, never raises it.
UENUM()
enum class EFoundationOffsetKind : uint8
{
    None,
    XZ,
    XYZ
};

// k for LayersK, else 0.
inline int32 GetFoundationShapeLayers(EFoundationBlockShape Shape)
{
    return Shape >= EFoundationBlockShape::Layers1 && Shape <= EFoundationBlockShape::Layers7
        ? int32(Shape) - int32(EFoundationBlockShape::Layers1) + 1
        : 0;
}

// The pass a block draws in: one material each. A grass block's side overlay is composited in the opaque pass.
UENUM()
enum class EFoundationRenderLayer : uint8
{
    Opaque,
    Cutout,
    Translucent
};

// Vanilla getStateForPlacement families (26.3 bytecode); everything else places its default state.
UENUM()
enum class EFoundationPlacementRule : uint8
{
    Default,
    // RotatedPillarBlock: axis from the clicked face.
    Axis,
    // AbstractFurnaceBlock and other horizontal blocks: facing the player (opposite the horizontal look).
    FacingPlayer,
    // DispenserBlock, BarrelBlock, CommandBlock: opposite the nearest look direction (six ways).
    FacingAwayNearest,
    // ObserverBlock: the nearest look direction itself.
    FacingNearest,
    // CrafterBlock: front opposite the nearest look, top from the horizontal look when vertical.
    CrafterOrientation,
    // JigsawBlock: front is the clicked face, top from the horizontal look when vertical.
    JigsawOrientation,
    Slab,
    // LeavesBlock: persistent, distance from neighbouring logs and leaves.
    Leaves,
    // SnowyBlock: snowy when a #snow block is above.
    Snowy,
    // WallTorchBlock: facing away from the wall it hangs on, which its standing item picks by look
    // (FoundationBlockPlacement::GetTorchState).
    WallAttached
};

USTRUCT()
struct FFoundationBlockAssetProperty
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Block")
    FName Name;

    // In 26.3 declaration order: state ids enumerate them as a cartesian product, last property fastest.
    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<FName> Values;
};

USTRUCT()
struct FFoundationBlockAsset
{
    GENERATED_BODY()

    // Saved id without namespace, e.g. "oak_slab".
    UPROPERTY(EditAnywhere, Category = "Block")
    FName Id;

    UPROPERTY(EditAnywhere, Category = "Block")
    FString DisplayName;

    // 26.3 runtime numbers, rebuilt from this asset every start and never saved.
    UPROPERTY(EditAnywhere, Category = "Block")
    int32 ProtocolId = 0;

    UPROPERTY(EditAnywhere, Category = "Block")
    int32 MinStateId = 0;

    UPROPERTY(EditAnywhere, Category = "Block")
    int32 DefaultStateId = 0;

    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<FFoundationBlockAssetProperty> Properties;

    UPROPERTY(EditAnywhere, Category = "Block")
    float Hardness = 0;

    UPROPERTY(EditAnywhere, Category = "Block")
    float BlastResistance = 0;

    UPROPERTY(EditAnywhere, Category = "Block")
    bool bRequiresCorrectToolForDrops = false;

    UPROPERTY(EditAnywhere, Category = "Block")
    int32 LightEmission = 0;

    UPROPERTY(EditAnywhere, Category = "Block")
    bool bReplaceable = false;

    UPROPERTY(EditAnywhere, Category = "Block")
    EFoundationRenderLayer Layer = EFoundationRenderLayer::Opaque;

    UPROPERTY(EditAnywhere, Category = "Block")
    EFoundationPlacementRule Placement = EFoundationPlacementRule::Default;

    // Plains-biome tint for faces whose code carries the tint bit; alpha 0 means untinted.
    UPROPERTY(EditAnywhere, Category = "Block")
    FColor Tint = FColor(0, 0, 0, 0);

    // Average color of the default state's top face (tinted), for break particles.
    UPROPERTY(EditAnywhere, Category = "Block")
    FColor ParticleColor = FColor::White;

    // Vanilla block tags mapped to Block.* gameplay tags (Config/Tags/MinecraftBlockTags.ini).
    UPROPERTY(EditAnywhere, Category = "Block")
    FGameplayTagContainer Tags;

    // Per state, in state-id order: the shape, then FoundationBlockFaces::CodesPerState face codes.
    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<EFoundationBlockShape> StateShapes;

    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<int32> StateFaces;

    // The block item (item id = block id) or None; its GUI tint (alpha 0 = untinted) and stack size.
    UPROPERTY(EditAnywhere, Category = "Block")
    FName Item;

    UPROPERTY(EditAnywhere, Category = "Block")
    FString ItemName;

    UPROPERTY(EditAnywhere, Category = "Block")
    int32 ItemMaxStack = 64;

    UPROPERTY(EditAnywhere, Category = "Block")
    FColor ItemTint = FColor(0, 0, 0, 0);

    // A standing-and-wall item's wall block, which it places against a side face (torch: wall_torch), or None.
    UPROPERTY(EditAnywhere, Category = "Block")
    FName ItemWallBlock;

    // Per state, in state-id order: the faces that hold a torch, a bit per EFoundationFace (the reference's
    // facts/block_support.json). Up holds a floor torch, a side a wall torch facing away from it; Down never.
    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<uint8> StateSupport;

    // Model blocks only (empty or default for the rest). Per state, in state-id order: the part count, then per part
    // the alternative count and that many indices into UFoundationBlockAssets::Models (a random variant list has
    // several alternatives; the mesher picks one by position).
    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<int32> StateModels;

    // Per state, two counts (outline boxes, collision boxes); then StateBoxes holds each state's outline boxes and its
    // collision boxes as (min X, Y, Z, max X, Y, Z) in block units, Unreal axes, without the position offset.
    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<int32> StateBoxCounts;

    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<float> StateBoxes;

    // Per state: light emission, and the water amount it holds (8 for a source; 0 none).
    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<int32> StateLight;

    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<int32> StateWater;

    // Per state: the tint, for a block whose tint follows its age (the pumpkin and melon stems; survival B6a). Empty:
    // every state takes the block's Tint.
    UPROPERTY(EditAnywhere, Category = "Block")
    TArray<FColor> StateTint;

    UPROPERTY(EditAnywhere, Category = "Block")
    EFoundationOffsetKind OffsetKind = EFoundationOffsetKind::None;

    // The largest horizontal offset (blocks) and the lowest vertical one (XYZ: negative).
    UPROPERTY(EditAnywhere, Category = "Block")
    float OffsetMaxHorizontal = 0;

    UPROPERTY(EditAnywhere, Category = "Block")
    float OffsetMinVertical = 0;

    UPROPERTY(EditAnywhere, Category = "Block")
    bool bOutlineMovesWithOffset = false;

    UPROPERTY(EditAnywhere, Category = "Block")
    bool bCollisionMovesWithOffset = false;

    // The item icon of a model block: a flat sprite (an IconAtlas tile, tinted by ItemTint) or a baked model drawn
    // with its gui display transform (degrees, sixteenths, scale); -1 when unused. Other items draw their cube.
    UPROPERTY(EditAnywhere, Category = "Block")
    int32 ItemIconTile = -1;

    UPROPERTY(EditAnywhere, Category = "Block")
    int32 ItemIconModel = -1;

    UPROPERTY(EditAnywhere, Category = "Block")
    FVector3f ItemIconRotation = FVector3f::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Block")
    FVector3f ItemIconTranslation = FVector3f::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Block")
    FVector3f ItemIconScale = FVector3f::OneVector;

    // One of survival B1's family blocks (the reference manifest's added: "survival-B1"), appended after the blocks
    // before them; foundation.Blocks.Families 0 hides it (FoundationBlockFamilies).
    UPROPERTY(EditAnywhere, Category = "Block")
    bool bAppendedFamily = false;
};

// A baked block model (Reference/minecraft-blocks-26.3/block_models.json): a range of quads.
USTRUCT()
struct FFoundationBlockModelAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Model")
    int32 FirstQuad = 0;

    UPROPERTY(EditAnywhere, Category = "Model")
    int32 QuadCount = 0;

    UPROPERTY(EditAnywhere, Category = "Model")
    bool bAmbientOcclusion = true;
};

// One texture of the block arrays: its layers and, when animated, the 26.3 .mcmeta frame table.
USTRUCT()
struct FFoundationBlockTextureAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Texture")
    FName Name;

    // False: the 16x16 block array; true: the 32x32 water flow array.
    UPROPERTY(EditAnywhere, Category = "Texture")
    bool bWaterFlow = false;

    UPROPERTY(EditAnywhere, Category = "Texture")
    int32 FirstLayer = 0;

    UPROPERTY(EditAnywhere, Category = "Texture")
    int32 FrameCount = 1;

    // Shown frames (indices into the strip) and their durations in ticks; empty when not animated.
    UPROPERTY(EditAnywhere, Category = "Texture")
    TArray<int32> FrameIndices;

    UPROPERTY(EditAnywhere, Category = "Texture")
    TArray<int32> FrameTicks;

    UPROPERTY(EditAnywhere, Category = "Texture")
    bool bInterpolate = false;

    bool IsAnimated() const { return FrameIndices.Num() > 1; }
};

// A vanilla creative tab, with its full 26.3 display order (items that do not exist yet are skipped on screen).
USTRUCT()
struct FFoundationCreativeTabAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Creative")
    FName Id;

    UPROPERTY(EditAnywhere, Category = "Creative")
    FString Title;

    // 0 top, 1 bottom; column 0-6.
    UPROPERTY(EditAnywhere, Category = "Creative")
    int32 Row = 0;

    UPROPERTY(EditAnywhere, Category = "Creative")
    int32 Column = 0;

    // CATEGORY, HOTBAR, SEARCH or INVENTORY.
    UPROPERTY(EditAnywhere, Category = "Creative")
    FName Type;

    UPROPERTY(EditAnywhere, Category = "Creative")
    bool bAlignedRight = false;

    UPROPERTY(EditAnywhere, Category = "Creative")
    bool bShowTitle = true;

    UPROPERTY(EditAnywhere, Category = "Creative")
    bool bCanScroll = true;

    UPROPERTY(EditAnywhere, Category = "Creative")
    FName Icon;

    UPROPERTY(EditAnywhere, Category = "Creative")
    TArray<FName> Items;
};

UCLASS(BlueprintType)
class UNNAMED_API UFoundationBlockAssets : public UDataAsset
{
    GENERATED_BODY()
public:
    // SHA-256 of the reference files the import read; a re-import with different input refuses to overwrite.
    UPROPERTY(EditAnywhere, Category = "Blocks")
    FString SourceSha256;

    UPROPERTY(EditAnywhere, Category = "Blocks")
    TArray<FFoundationBlockAsset> Blocks;

    // Index 0 is the plain white texture project blocks use; vanilla textures follow in reference order.
    UPROPERTY(EditAnywhere, Category = "Blocks")
    TArray<FFoundationBlockTextureAsset> Textures;

    UPROPERTY(EditAnywhere, Category = "Blocks")
    TArray<FFoundationCreativeTabAsset> CreativeTabs;

    UPROPERTY(EditAnywhere, Category = "Blocks")
    TArray<FFoundationBlockModelAsset> Models;

    // Per quad, four vertices of (X, Y, Z, U, V): block units on Unreal axes (X east, Y south, Z up) and the
    // texture UV, in FaceInfo order. ModelQuadCodes packs each quad's texture and flags (FoundationBlockModels).
    UPROPERTY(EditAnywhere, Category = "Blocks")
    TArray<float> ModelQuadVertices;

    UPROPERTY(EditAnywhere, Category = "Blocks")
    TArray<int32> ModelQuadCodes;

    UPROPERTY(EditAnywhere, Category = "Blocks")
    TObjectPtr<UTexture2DArray> BlockTextures;

    UPROPERTY(EditAnywhere, Category = "Blocks")
    TObjectPtr<UTexture2DArray> WaterFlowTextures;

    // Every frame of BlockTextures, mip 0, in a grid of 16x16 tiles for Slate item icons, then the item sprites
    // model blocks' icons use (textures/item).
    UPROPERTY(EditAnywhere, Category = "Blocks")
    TObjectPtr<UTexture2D> IconAtlas;

    UPROPERTY(EditAnywhere, Category = "Blocks")
    int32 IconAtlasColumns = 32;

    // Plains-biome water color (the water block's tint) at import.
    UPROPERTY(EditAnywhere, Category = "Blocks")
    FColor WaterColor = FColor(0x3F, 0x76, 0xE4);
};
