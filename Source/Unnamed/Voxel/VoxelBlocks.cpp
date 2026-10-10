#include "Voxel/VoxelBlocks.h"
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Meshing/VoxelOreLooks.h"
#include "GameplayTagContainer.h"

// The core repeats the registry's face order and cull bit; they must agree.
static_assert(int32(EVoxelFace::West) == int32(EFoundationFace::West) &&
        int32(EVoxelFace::East) == int32(EFoundationFace::East) &&
        int32(EVoxelFace::North) == int32(EFoundationFace::North) &&
        int32(EVoxelFace::South) == int32(EFoundationFace::South) &&
        int32(EVoxelFace::Down) == int32(EFoundationFace::Down) && int32(EVoxelFace::Up) == int32(EFoundationFace::Up),
    "EVoxelFace must follow EFoundationFace");
static_assert(VoxelFaces::CullBit == FoundationBlockFaces::CullBit, "Cull bit");
static_assert(VoxelFaces::TintBit == FoundationBlockFaces::TintBit, "Tint bit");
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

    // The registry keeps every tint as its plains colour (the block reference's README): the grass, foliage and dry
    // foliage colormaps' plains colours mark the biome tints; any other colour is fixed (spruce and birch leaves, the
    // lily pad).
    constexpr uint32 PlainsGrass = 0x91BD59, PlainsFoliage = 0x77AB2F, PlainsDryFoliage = 0xA37546;

    EVoxelTintSource GetTintSource(FColor Tint)
    {
        if (!Tint.A) return EVoxelTintSource::None;
        const uint32 Packed = PackRgb(Tint);
        return Packed == PlainsGrass     ? EVoxelTintSource::Grass
            : Packed == PlainsFoliage    ? EVoxelTintSource::Foliage
            : Packed == PlainsDryFoliage ? EVoxelTintSource::DryFoliage
                                         : EVoxelTintSource::Constant;
    }

    bool IsSameQuad(const FVoxelModelQuad& A, const FVoxelModelQuad& B)
    {
        for (int32 Vertex = 0; Vertex < 4; ++Vertex)
            if (A.Positions[Vertex] != B.Positions[Vertex] || A.UVs[Vertex] != B.UVs[Vertex]) return false;
        return A.Code == B.Code && A.CullFace == B.CullFace && A.Shade == B.Shade && A.Light == B.Light;
    }

    // The registry's baked models, in its order, so model indices carry over. A quad repeating an earlier one of its
    // model exactly (some flowerbed segments list a plane twice) is dropped: it would draw the same pixels again.
    void CopyModels(const FFoundationBlockRegistry& Registry, FVoxelBlockTable& Table)
    {
        TArray<FVoxelModelQuad> Quads;
        for (int32 Model = 0; Model < Registry.GetModelCount(); ++Model)
        {
            Quads.Reset();
            for (const FFoundationModelQuad& Source : Registry.GetModelQuads(Model))
            {
                FVoxelModelQuad& Quad = Quads.AddDefaulted_GetRef();
                for (int32 Vertex = 0; Vertex < 4; ++Vertex)
                {
                    Quad.Positions[Vertex] = Source.Positions[Vertex];
                    Quad.UVs[Vertex] = Source.UVs[Vertex];
                }
                // The baked tintindex is -1 wherever the block has no tint source for it.
                Quad.Code = uint32(Source.Texture) & FoundationBlockFaces::TextureMask;
                if (Source.TintIndex >= 0) Quad.Code |= FoundationBlockFaces::TintBit;
                Quad.CullFace = int8(Source.CullFace == INDEX_NONE ? -1 : Source.CullFace);
                Quad.Shade = uint8(Source.Shade);
                Quad.Facing = uint8(Source.Facing);
                Quad.Light = uint8(FMath::Clamp(Source.Light, 0, 15));
                for (int32 Earlier = 0; Earlier + 1 < Quads.Num(); ++Earlier)
                    if (IsSameQuad(Quads[Earlier], Quad))
                    {
                        Quads.Pop();
                        break;
                    }
            }
            const int32 Index = Table.AddModel(Quads, Registry.IsModelAmbientOccluded(Model));
            check(Index == Model);
        }
    }
}

TSharedRef<const FVoxelBlockTable> VoxelBlocks::MakeTable(const FFoundationBlockRegistry& Registry)
{
    TSharedRef<FVoxelBlockTable> Table = MakeShared<FVoxelBlockTable>();
    const UFoundationBlockAssets* Assets = Registry.GetAssets();
    const uint32 WaterStill = uint32(FMath::Max(0, FindBlockTexture(Assets, TEXT("water_still"))));
    const uint32 WaterFlow = uint32(FMath::Max(0, FindBlockTexture(Assets, TEXT("water_flow"))));
    const uint32 LavaStill = uint32(FMath::Max(0, FindBlockTexture(Assets, TEXT("lava_still"))));
    const FGameplayTag Leaves = FGameplayTag::RequestGameplayTag(TEXT("Block.Leaves"), false);
    const FGameplayTag BlocksMotion = FGameplayTag::RequestGameplayTag(TEXT("Block.BlocksMotion"), false);
    CopyModels(Registry, *Table);
    // The water a water-holding model state is drawn with: water's default state, a source.
    const FFoundationBlockType* WaterBlock = Registry.FindBlock(TEXT("water"));
    const uint16 WaterSource = WaterBlock ? uint16(WaterBlock->DefaultStateId) : 0;
    TArray<TConstArrayView<int32>> Parts;

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
        Info.TintSource = GetTintSource(Block.Tint);
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
            Info.TintSource = EVoxelTintSource::Water;
        }
        else if (Block.IsLava())
        {
            // Still lava: drawn like water by the fluid mesher, but in the opaque layer so the chunk material's
            // emission lights it, with its animated still texture on every face (vanilla's flow texture on the sides
            // waits for flowing lava). No damage and no flow.
            Info.Shape = EVoxelShape::Fluid;
            Info.Layer = EVoxelLayer::Opaque;
            Info.FluidAmount = uint8(Registry.GetFluid(State).Amount);
            for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
                Info.Faces[Face] = LavaStill;
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
                Info.Layers = uint8(GetFoundationShapeLayers(Registry.GetShape(State)));
                Info.Shape = Info.Layers ? EVoxelShape::Layers : EVoxelShape::Air;
                break;
            }
            Info.Layer = Block.Layer == EFoundationRenderLayer::Cutout ? EVoxelLayer::Cutout
                : Block.Layer == EFoundationRenderLayer::Translucent   ? EVoxelLayer::Translucent
                                                                       : EVoxelLayer::Opaque;
            if (const uint32* Codes = Registry.GetFaces(State))
                for (int32 Index = 0; Index < FoundationBlockFaces::CodesPerState; ++Index)
                    Info.Faces[Index] = Codes[Index];
            // A single snow layer has no collision box (26.3's collision is one layer below the outline).
            Info.bSolid = Info.Shape != EVoxelShape::Air && !(Info.Shape == EVoxelShape::Layers && Info.Layers == 1);
            // Model states (plants and other baked models) draw their baked quads in their own layer. They block
            // motion, and count for the motion-blocking heightmaps, as 26.3's blocks_motion tag says (cactus,
            // bamboo, mangrove roots, amethyst ...); those holding water count through it.
            if (Registry.GetShape(State) == EFoundationBlockShape::Model)
                Info.bSolid = BlocksMotion.IsValid() && Block.Tags.HasTag(BlocksMotion);
            if (Registry.GetShape(State) == EFoundationBlockShape::Model && Registry.GetModelParts(State, Parts))
            {
                Info.Shape = EVoxelShape::Model;
                Info.Layer = EVoxelLayer::Model;
                Info.LightEmission = uint8(FMath::Clamp(Registry.GetLightEmission(State), 0, 15));
                Info.ModelParts = Table->AddModelParts(Parts);
                Info.ModelPartCount = uint8(Parts.Num());
                if (Registry.GetFluid(State).Amount > 0) Info.HeldFluid = WaterSource;
                Info.Offset = Block.OffsetKind == EFoundationOffsetKind::XYZ ? EVoxelOffset::HorizontalAndDown
                    : Block.OffsetKind == EFoundationOffsetKind::XZ          ? EVoxelOffset::Horizontal
                                                                             : EVoxelOffset::None;
                Info.OffsetHorizontal = Block.OffsetMaxHorizontal;
                Info.OffsetDown = Block.OffsetKind == EFoundationOffsetKind::XYZ ? Block.OffsetMinVertical : 0.f;
            }
            Info.bCastsOcclusion = Info.Shape == EVoxelShape::Full && Info.Layer != EVoxelLayer::Translucent;
            // Only opaque surfaces hide a neighbour's face; a slab covers just its bottom or top square, snow layers
            // their bottom.
            if (Info.Layer == EVoxelLayer::Opaque)
                Info.OpaqueFaces = Info.Shape == EVoxelShape::Full ? 0x3F
                    : Info.Shape == EVoxelShape::SlabBottom || Info.Shape == EVoxelShape::Layers
                    ? uint8(1 << int32(EVoxelFace::Down))
                    : Info.Shape == EVoxelShape::SlabTop ? uint8(1 << int32(EVoxelFace::Up))
                                                         : 0;
        }
        Table->SetState(uint16(Id), Info);
    }
    for (int32 Index = 1; Index < Registry.GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry.GetBlockAt(Index);
        Table->SetDefaultState(Block.Id, uint16(Block.DefaultStateId));
        // Generation places default states; the freeze pass turns a default under snow into its snowy variant.
        const FFoundationBlockState Default = Block.GetDefaultState();
        if (Registry.GetValue(Default, TEXT("snowy")) == TEXT("false"))
            Table->SetSnowyState(Default.Id, Registry.WithValue(Default, TEXT("snowy"), TEXT("true")).Id);
    }
    // Render-only: the ore and rock classes the ore looks read (voxel.Look.OreHost, voxel.Look.OreGlint).
    VoxelOreLooks::Classify(*Table);
    return Table;
}

TSharedRef<const FVoxelBlockTable> VoxelBlocks::GetDefault()
{
    check(IsInGameThread());
    static TSharedPtr<const FVoxelBlockTable> Default;
    if (!Default) Default = MakeTable(*FFoundationBlockRegistry::GetDefault());
    return Default.ToSharedRef();
}
