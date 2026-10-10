#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockRegistry.h"

class UStaticMesh;

// The meshes block instances use: a cube, the two slab halves, the nine fluid heights and the seven snow layer
// heights. Each is a box in
// block units built at runtime (no mesh assets to keep in sync) with the canonical face UVs in channel 0
// (FoundationBlockFaces::GetCanonicalUV, so half shapes sample half the texture) and the face index
// (EFoundationFace) in channel 1, which the block materials use to pick that face's code. The pivot is the
// cell's minimum corner.
//
// Baked models (FFoundationBlockRegistry::GetModelQuads) follow the kinds, one mesh each (mesh Count + model index).
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
    // A model's mesh: its quads but the repeats, in order, four vertices each. Null when nothing is left.
    UNNAMED_API UStaticMesh* BuildModel(UObject* Outer, TConstArrayView<FFoundationModelQuad> Quads);
}
