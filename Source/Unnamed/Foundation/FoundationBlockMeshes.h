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

    // The unit box a mesh kind covers.
    UNNAMED_API FBox GetUnitBox(int32 Kind);
    // The mesh kind for a state; a fluid picks its height from the level and whether the same fluid is directly above.
    UNNAMED_API int32 GetKind(const FFoundationBlockRegistry& Registry, FFoundationBlockState State, bool bWaterAbove);
    UNNAMED_API UStaticMesh* Build(UObject* Outer, int32 Kind);
}
