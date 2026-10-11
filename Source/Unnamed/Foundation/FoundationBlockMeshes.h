#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Meshing/VoxelFluidSurface.h"

class UStaticMesh;

// The meshes block instances use: a cube, the two slab halves, the nine fluid heights and the seven snow layer
// heights. Each is a box in
// block units built at runtime (no mesh assets to keep in sync) with the canonical face UVs in channel 0
// (FoundationBlockFaces::GetCanonicalUV, so half shapes sample half the texture) and the face index
// (EFoundationFace) in channel 1, which the block materials use to pick that face's code. The pivot is the
// cell's minimum corner.
//
// Baked models (GetDrawnModelQuads) follow the kinds, one mesh each (mesh Count + model index).
// Their UV 0 is the quad's texture UV, and UV 1 is (shade face, the quad's own code): its texture, tint bit and element
// light, marked with ModelCodeBit, which the block materials read instead of the instance's face code. Normals and
// tangents follow the shade face, as the voxel world's do (crosses are lit as up). The cube kinds keep UV 1's second
// channel at 0.
namespace FoundationBlockMeshes
{
    enum EKind : int32
    {
        Cube,
        SlabBottom,
        SlabTop,
        // Fluid surfaces at amount / 9 for amounts 1-8, then a full-height column (the same fluid above).
        WaterFirst,
        WaterFull = WaterFirst + 8,
        // Snow layers 1-7, k/8 high.
        LayersFirst,
        Count = LayersFirst + 7
    };

    // A model quad's own code: face code bits (texture 0-9, tint 13), the element light in bits 16-19 and the marker.
    // Under 2^21, so the full-precision UV holds it to a quarter, and interpolation across the quad cannot move it
    // past the rounding the materials apply.
    constexpr int32 ModelLightShift = 16;
    constexpr uint32 ModelCodeBit = 1u << 20;

    // The unit box a mesh kind covers.
    UNNAMED_API FBox GetUnitBox(int32 Kind);
    // The mesh kind for a state; a fluid picks its height from the level and whether the same fluid is directly above.
    UNNAMED_API int32 GetKind(const FFoundationBlockRegistry& Registry, FFoundationBlockState State, bool bWaterAbove);
    UNNAMED_API UStaticMesh* Build(UObject* Outer, int32 Kind);

    // Sloped fluid cells (water and sky W3b): the Playground draws a fluid cell whose surface slopes
    // (VoxelFluidSurface::Make, under voxel.Fluids.SlopedTops) with the voxel mesher's geometry, one mesh per distinct
    // surface. Every other fluid cell keeps its box kind.
    // A state holds fluid as the voxel mesher reads "fluid above": a fluid, or a model holding water.
    UNNAMED_API bool HoldsFluid(const FFoundationBlockRegistry& Registry, FFoundationBlockState State);
    // A state packed as the voxel mesher packs it (VoxelFluidSurface::Pack): a fluid, the water source a model holds,
    // or 0.
    UNNAMED_API uint8 PackFluid(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, bool bFluidAbove);
    // The surface of the fluid cell at Cell from the states around it, as the voxel mesher makes it; false when the
    // cell draws as its box kind.
    UNNAMED_API bool MakeFluidSurface(const FFoundationBlockRegistry& Registry,
        TFunctionRef<FFoundationBlockState(const FIntVector&)> GetState, const FIntVector& Cell, int32 Mode,
        bool bOpenCorners, VoxelFluidSurface::FSurface& Out);
    // Whether a sloped top runs downhill, so it takes the flow texture along its flow.
    UNNAMED_API bool IsFlowTop(const VoxelFluidSurface::FSurface& Surface);
    // One face of a sloped fluid cell in block units from the cell's minimum corner, in the voxel mesher's corner
    // order (VoxelMesher's FluidFace; triangles 0-1-2 and 0-2-3), with the top's vertices on the surface's corners.
    struct FFluidFace
    {
        FVector3f Positions[4];
        FVector2f UVs[4];
    };
    // The six faces (EFoundationFace order). The top takes the flow UVs when IsFlowTop, every other face the
    // canonical UVs of its vertices.
    UNNAMED_API void GetFluidFaces(const VoxelFluidSurface::FSurface& Surface, FFluidFace Out[6]);
    // A surface's four corners in 108ths of a block (every corner the rule makes is a whole number of them: ninths,
    // and means of up to four ninths), 7 bits each: the mesh a surface shares with every surface of the same corners.
    UNNAMED_API uint32 GetFluidSurfaceKey(const VoxelFluidSurface::FSurface& Surface);
    UNNAMED_API UStaticMesh* BuildFluidSurface(UObject* Outer, const VoxelFluidSurface::FSurface& Surface);

    UNNAMED_API uint32 MakeModelCode(const FFoundationModelQuad& Quad);
    // Whether a model's quad repeats an earlier one of the same model exactly (flowerbed segments list a plane twice).
    // The voxel world drops these; so does the model mesh.
    UNNAMED_API bool IsRepeatedQuad(TConstArrayView<FFoundationModelQuad> Quads, int32 Index);
    // A quad lying on an earlier quad's four corners (the firefly bush's emissive overlay on its cross) is lifted this
    // far along its facing, in blocks. Masked materials clip only in the depth prepass
    // (r.EarlyZPassOnlyMaterialMasking, on by default), so a coplanar overlay would pass the base pass's depth test
    // across the whole quad beneath it and draw its transparent texels there.
    constexpr float OverlayLift = 1.f / 512;
    UNNAMED_API FVector3f GetOverlayLift(TConstArrayView<FFoundationModelQuad> Quads, int32 Index);

    // The torch's flame (torch session T3b; Jesus: only the flame glows): torch.png's rows 6 and 7, above the stick's
    // rows 8-15. A quad textured `torch` that crosses UV v = TorchFlameV is cut there into the flame above and the
    // stick below. The flame, and a torch quad wholly above the line (the top face), carries TorchFlameLight as its
    // element light; the stick keeps the baked 0. Torch-rule states themselves glow at voxel.TorchGlow (GetDrawnLight).
    constexpr float TorchFlameV = 8.f / 16;
    constexpr int32 TorchFlameLight = 1;
    // A model's quads as both worlds draw them (these model meshes, and the voxel world's table): the registry's, in
    // order, with each torch quad cut at the flame (its flame piece first). The registry keeps the baked quads.
    UNNAMED_API TArray<FFoundationModelQuad> GetDrawnModelQuads(const FFoundationBlockRegistry& Registry, int32 Model);
    // The light a state's instances glow at: torch-rule states (torch, wall_torch) at voxel.TorchGlow, read when the
    // instance is made; every other state at its own light emission.
    UNNAMED_API int32 GetDrawnLight(const FFoundationBlockRegistry& Registry, FFoundationBlockState State);

    // A model's mesh: its quads but the repeats, in order, four vertices each. Null when nothing is left.
    UNNAMED_API UStaticMesh* BuildModel(UObject* Outer, TConstArrayView<FFoundationModelQuad> Quads);
}
