#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockAssets.h"
#include "Foundation/FoundationGrid.h"

class UFoundationBlockAssets;
struct FFoundationBlockDefinition;

// A cell's block state: the 26.3 block-state id (air is 0, stone 1, water 89-104). Runtime-only: saves use
// FFoundationSavedState. Blocks this project adds itself get ids after the vanilla range.
struct FFoundationBlockState
{
    uint16 Id = 0;

    bool IsAir() const { return Id == 0; }
    bool operator==(const FFoundationBlockState& Other) const = default;
    friend uint32 GetTypeHash(FFoundationBlockState State) { return State.Id; }
};

// The saved form of a state, as in Minecraft's chunk palette: {Name: "oak_slab", Properties: {type: "bottom",
// waterlogged: "false"}}. Properties are in the block's declaration order.
struct FFoundationSavedState
{
    FName Id;
    TArray<TPair<FName, FName>> Properties;

    bool operator==(const FFoundationSavedState& Other) const = default;
};

// What a fluid state means (26.3 FlowingFluid, water and lava alike): level 0 is a source (amount 8); 1-7 flow with
// amount 8 - level; 8-15 fall with amount 8. The surface is amount / 9 of the cell, full height when the same fluid
// is directly above.
struct FFoundationFluid
{
    bool bWater = false;
    bool bLava = false;
    bool bSource = false;
    bool bFalling = false;
    int32 Amount = 0;
    int32 Level = 0;

    double GetHeight(bool bWaterAbove) const { return bWaterAbove ? 1.0 : Amount / 9.0; }
};

// One block type: identity, mining facts, placement family and presentation data shared by its states.
struct FFoundationBlockType
{
    FName Id;
    FString DisplayName;
    int32 ProtocolId = 0;
    int32 MinStateId = 0;
    int32 StateCount = 1;
    int32 DefaultStateId = 0;
    TArray<FFoundationBlockAssetProperty> Properties;

    // Java-style hardness: negative never breaks, zero breaks at once, positive accumulates progress.
    float Hardness = 0;
    float BlastResistance = 0;
    bool bRequiresCorrectToolForDrops = false;
    // A replaceable block is itself the placement destination (air, water).
    bool bReplaceable = false;
    // Survival rule hook; no imported block needs support.
    bool bNeedsSupportBelow = false;
    int32 LightEmission = 0;
    FGameplayTagContainer Tags;
    EFoundationRenderLayer Layer = EFoundationRenderLayer::Opaque;
    EFoundationPlacementRule Placement = EFoundationPlacementRule::Default;
    // Plains tint for tinted faces (alpha 0: none) and the flat color project blocks and particles use.
    FColor Tint = FColor(0, 0, 0, 0);
    FLinearColor Color = FLinearColor::White;

    // Provisional loot rule: a block drops its own item. Loot tables are not imported yet.
    FName DropItem;
    int32 DropCount = 1;
    FName Item;
    // The item's GUI tint (items/<id>.json; alpha 0: untinted) and stack size.
    FColor ItemTint = FColor(0, 0, 0, 0);
    int32 ItemMaxStack = 64;

    bool IsAir() const { return ProtocolId == 0; }
    bool IsWater() const { return Id == TEXT("water"); }
    // Still lava: no flow and no damage yet.
    bool IsLava() const { return Id == TEXT("lava"); }
    bool IsFluid() const { return IsWater() || IsLava(); }
    FFoundationBlockState GetDefaultState() const { return {uint16(DefaultStateId)}; }
    bool Contains(FFoundationBlockState State) const
    {
        return State.Id >= MinStateId && State.Id < MinStateId + StateCount;
    }
    int32 FindProperty(FName Name) const
    {
        return Properties.IndexOfByPredicate([Name](const FFoundationBlockAssetProperty& P) { return P.Name == Name; });
    }

private:
    friend class FFoundationBlockRegistry;
    // First entry of this block's states in the registry's shape and face tables.
    int32 TableOffset = 0;
};

// Face codes: what one face of a state shows. Six per state in EFoundationFace order, then an overlay code.
// The same codes feed the instanced cubes now and a chunk mesher later, so they never depend on the mesh.
namespace FoundationBlockFaces
{
    constexpr int32 CodesPerState = 7;
    constexpr int32 OverlayIndex = 6;
    // Face: bits 0-9 texture (index into UFoundationBlockAssets::Textures), 10-12 UV transform, 13 tinted,
    // 14 culled by a full neighbour. Overlay: bits 0-9 texture, 13 tinted, 15 present, 16-21 face mask.
    constexpr uint32 TextureMask = 0x3FF;
    constexpr int32 TransformShift = 10;
    constexpr uint32 TintBit = 1u << 13;
    constexpr uint32 CullBit = 1u << 14;
    constexpr uint32 OverlayBit = 1u << 15;
    constexpr int32 OverlayFaceShift = 16;

    inline int32 GetTexture(uint32 Code)
    {
        return int32(Code & TextureMask);
    }
    inline int32 GetTransform(uint32 Code)
    {
        return int32((Code >> TransformShift) & 7);
    }
    inline bool IsTinted(uint32 Code)
    {
        return (Code & TintBit) != 0;
    }
    inline bool IsCulled(uint32 Code)
    {
        return (Code & CullBit) != 0;
    }
    inline bool HasOverlay(uint32 Overlay, EFoundationFace Face)
    {
        return (Overlay & OverlayBit) && (Overlay >> (OverlayFaceShift + int32(Face)) & 1);
    }

    // The UV an unrotated full cube shows at block-relative point P (Unreal axes, 0..1), per face. Every mesh
    // carries these UVs, so half-height shapes sample half the texture.
    UNNAMED_API FVector2f GetCanonicalUV(EFoundationFace Face, const FVector3f& P);
    // Transform 0-7 from canonical to texture UV: bit 2 mirrors U first, bits 0-1 turn (u, v) -> (v, 1 - u).
    // The block materials implement the same formula.
    UNNAMED_API FVector2f TransformUV(int32 Transform, FVector2f UV);

    // Ticks per second of block animation (the client tick).
    constexpr int32 TicksPerSecond = 20;
    // A texture's animation at a tick since start, as 26.3's SpriteContents.AnimatedTexture ticks it: the shown
    // layer, the next frame's layer and the blend toward it (subFrame / frametime when interpolating, else 0).
    UNNAMED_API void GetFrame(
        const FFoundationBlockTextureAsset& Texture, int64 Tick, int32& OutLayer, int32& OutNextLayer, float& OutBlend);
}

// The block vocabulary: every imported 26.3 block plus project blocks, with lookups by id, protocol id and
// state id, the state <-> properties mapping, per-state shapes and faces, and the saved-form codec. Immutable
// once built; the block world and authority share one instance.
class UNNAMED_API FFoundationBlockRegistry
{
public:
    // The project registry: the asset at UUnnamedProjectSettings::BlockAssets, then the project blocks from
    // UFoundationInteractionSettings. Built on first use; without the asset only air and project blocks exist.
    static TSharedRef<const FFoundationBlockRegistry> GetDefault();
    static TSharedRef<FFoundationBlockRegistry> Build(
        const UFoundationBlockAssets* Assets, TArrayView<const FFoundationBlockDefinition> ProjectBlocks);

    int32 GetBlockCount() const { return Blocks.Num(); }
    const FFoundationBlockType& GetBlockAt(int32 Index) const { return Blocks[Index]; }
    // The highest state id in use plus one.
    int32 GetStateLimit() const { return StateBlocks.Num(); }
    const UFoundationBlockAssets* GetAssets() const { return Assets.Get(); }

    const FFoundationBlockType* FindBlock(FName Id) const;
    const FFoundationBlockType* FindByProtocolId(int32 ProtocolId) const;
    // The block a state belongs to; unknown ids resolve to air.
    const FFoundationBlockType& GetBlock(FFoundationBlockState State) const;
    bool IsKnown(FFoundationBlockState State) const;

    // Properties.
    FName GetValue(FFoundationBlockState State, FName Property) const;
    // The same block with one property changed; unchanged when the property or value does not exist.
    FFoundationBlockState WithValue(FFoundationBlockState State, FName Property, FName Value) const;
    TArray<TPair<FName, FName>> GetProperties(FFoundationBlockState State) const;
    // Every state id, from properties in any order; missing properties take the default state's values.
    bool TryMakeState(const FFoundationBlockType& Block, TConstArrayView<TPair<FName, FName>> Properties,
        FFoundationBlockState& OutState, TArray<FString>* OutWarnings = nullptr) const;

    // Shapes in block units from the cell's minimum corner (Unreal axes). Selection is what targeting and the
    // outline see; collision is what movement and placement obstruction see. Water has neither.
    EFoundationBlockShape GetShape(FFoundationBlockState State) const;
    bool GetSelectionShape(FFoundationBlockState State, FBox& OutBox) const;
    bool GetCollisionShape(FFoundationBlockState State, FBox& OutBox) const;
    FFoundationFluid GetFluid(FFoundationBlockState State) const;

    // Seven face codes (see FoundationBlockFaces); null for air.
    const uint32* GetFaces(FFoundationBlockState State) const;

    // Saved form: never numbers. Unknown blocks fail; unknown properties or values keep the default and warn,
    // as Minecraft's NbtUtils.readBlockState does.
    FFoundationSavedState Save(FFoundationBlockState State) const;
    bool Load(const FFoundationSavedState& Saved, FFoundationBlockState& OutState,
        TArray<FString>* OutWarnings = nullptr) const;
    // "oak_slab[type=bottom,waterlogged=false]", Minecraft's block-state syntax without the namespace.
    FString ToString(FFoundationBlockState State) const;
    bool Parse(const FString& Text, FFoundationBlockState& OutState) const;

private:
    void AddBlock(
        FFoundationBlockType&& Block, TConstArrayView<EFoundationBlockShape> Shapes, TConstArrayView<int32> Faces);
    int32 GetLocalIndex(FFoundationBlockState State, const FFoundationBlockType*& OutBlock) const;

    TArray<FFoundationBlockType> Blocks;
    TMap<FName, int32> ById;
    TMap<int32, int32> ByProtocol;
    // Per state id: the owning block index, or INDEX_NONE for ids no imported block uses.
    TArray<int32> StateBlocks;
    TArray<EFoundationBlockShape> Shapes;
    TArray<uint32> Faces;
    // The default registry roots its asset (GetDefault); a registry built in a test is kept alive by its caller.
    TWeakObjectPtr<const UFoundationBlockAssets> Assets;
};
