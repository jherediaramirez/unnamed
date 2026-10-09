#include "Voxel/VoxelBlocks.h"
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "GameplayTagContainer.h"

// The core repeats the registry's face order and cull bit; they must agree.
static_assert(int32(EVoxelFace::West) == int32(EFoundationFace::West) &&
        int32(EVoxelFace::East) == int32(EFoundationFace::East) &&
        int32(EVoxelFace::North) == int32(EFoundationFace::North) &&
        int32(EVoxelFace::South) == int32(EFoundationFace::South) &&
        int32(EVoxelFace::Down) == int32(EFoundationFace::Down) && int32(EVoxelFace::Up) == int32(EFoundationFace::Up),
    "EVoxelFace must follow EFoundationFace");
static_assert(VoxelFaces::CullBit == FoundationBlockFaces::CullBit, "Cull bit");
static_assert(VoxelFaces::OverlayIndex == FoundationBlockFaces::OverlayIndex, "Overlay code index");

namespace
{
    int32 FindBlockTexture(const UFoundationBlockAssets* Assets, FName Name)
    {
        return Assets ? Assets->Textures.IndexOfByPredicate(
                            [Name](const FFoundationBlockTextureAsset& Texture) { return Texture.Name == Name; })
                      : INDEX_NONE;
    }

    uint32 PackRgb(FColor Color)
    {
        return uint32(Color.R) << 16 | uint32(Color.G) << 8 | Color.B;
    }
}

TSharedRef<const FVoxelBlockTable> VoxelBlocks::MakeTable(const FFoundationBlockRegistry& Registry)
{
    TSharedRef<FVoxelBlockTable> Table = MakeShared<FVoxelBlockTable>();
    const UFoundationBlockAssets* Assets = Registry.GetAssets();
    const uint32 WaterStill = uint32(FMath::Max(0, FindBlockTexture(Assets, TEXT("water_still"))));
    const uint32 WaterFlow = uint32(FMath::Max(0, FindBlockTexture(Assets, TEXT("water_flow"))));
    const FGameplayTag Leaves = FGameplayTag::RequestGameplayTag(TEXT("Block.Leaves"), false);

    for (int32 Id = 1; Id < Registry.GetStateLimit(); ++Id)
    {
        const FFoundationBlockState State{uint16(Id)};
        if (!Registry.IsKnown(State)) continue;
        const FFoundationBlockType& Block = Registry.GetBlock(State);
        FVoxelStateInfo Info;
        Info.Block = uint16(Block.ProtocolId);
        Info.LightEmission = uint8(FMath::Clamp(Block.LightEmission, 0, 15));
        Info.bLeaves = Leaves.IsValid() && Block.Tags.HasTag(Leaves);
        Info.Tint = PackRgb(Block.Tint);
        if (Block.IsWater())
        {
            // Still texture on top and bottom, the flow texture (its own array) on the sides; tinted, never culled
            // by code (the mesher hides water against water and opaque faces itself).
            Info.Shape = EVoxelShape::Fluid;
            Info.Layer = EVoxelLayer::Water;
            Info.FluidAmount = uint8(Registry.GetFluid(State).Amount);
            for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
                Info.Faces[Face] =
                    (Face >= int32(EVoxelFace::Down) ? WaterStill : WaterFlow) | FoundationBlockFaces::TintBit;
            Info.Tint = PackRgb(Assets ? Assets->WaterColor : FColor(0x3F, 0x76, 0xE4));
        }
        else
        {
            switch (Registry.GetShape(State))
            {
            case EFoundationBlockShape::Full:
                Info.Shape = EVoxelShape::Full;
                break;
            case EFoundationBlockShape::SlabBottom:
                Info.Shape = EVoxelShape::SlabBottom;
                break;
            case EFoundationBlockShape::SlabTop:
                Info.Shape = EVoxelShape::SlabTop;
                break;
            default:
                Info.Shape = EVoxelShape::Air;
                break;
            }
            Info.Layer = Block.Layer == EFoundationRenderLayer::Cutout ? EVoxelLayer::Cutout
                : Block.Layer == EFoundationRenderLayer::Translucent   ? EVoxelLayer::Translucent
                                                                       : EVoxelLayer::Opaque;
            if (const uint32* Codes = Registry.GetFaces(State))
                for (int32 Index = 0; Index < FoundationBlockFaces::CodesPerState; ++Index)
                    Info.Faces[Index] = Codes[Index];
            Info.bSolid = Info.Shape != EVoxelShape::Air;
            Info.bCastsOcclusion = Info.Shape == EVoxelShape::Full && Info.Layer != EVoxelLayer::Translucent;
            // Only opaque surfaces hide a neighbour's face; a slab covers just its bottom or top square.
            if (Info.Layer == EVoxelLayer::Opaque)
                Info.OpaqueFaces = Info.Shape == EVoxelShape::Full ? 0x3F
                    : Info.Shape == EVoxelShape::SlabBottom        ? uint8(1 << int32(EVoxelFace::Down))
                    : Info.Shape == EVoxelShape::SlabTop           ? uint8(1 << int32(EVoxelFace::Up))
                                                                   : 0;
        }
        Table->SetState(uint16(Id), Info);
    }
    for (int32 Index = 1; Index < Registry.GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry.GetBlockAt(Index);
        Table->SetDefaultState(Block.Id, uint16(Block.DefaultStateId));
    }
    // Still lava for aquifers. The block reference has no lava yet, so only the voxel table knows it, at 26.3's own
    // ids (state 105 = lava level 0, a source; block 38; light 15); the registry and the authority read it as air.
    // Drawn like water by the fluid mesher, but in the opaque layer so the chunk material's emission lights it: the
    // still water texture tinted orange stands in for lava's own animated texture.
    constexpr uint16 LavaState = 105, LavaBlock = 38;
    if (!Registry.IsKnown(FFoundationBlockState{LavaState}) && Table->FindState(TEXT("lava")) == 0)
    {
        FVoxelStateInfo Lava;
        Lava.Shape = EVoxelShape::Fluid;
        Lava.Layer = EVoxelLayer::Opaque;
        Lava.FluidAmount = 8;
        Lava.LightEmission = 15;
        Lava.Block = LavaBlock;
        Lava.Tint = PackRgb(FColor(0xFF, 0x6A, 0x10));
        for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
            Lava.Faces[Face] = WaterStill | FoundationBlockFaces::TintBit;
        Table->SetState(LavaState, Lava);
        Table->SetDefaultState(TEXT("lava"), LavaState);
    }
    return Table;
}

TSharedRef<const FVoxelBlockTable> VoxelBlocks::GetDefault()
{
    check(IsInGameThread());
    static TSharedPtr<const FVoxelBlockTable> Default;
    if (!Default) Default = MakeTable(*FFoundationBlockRegistry::GetDefault());
    return Default.ToSharedRef();
}
