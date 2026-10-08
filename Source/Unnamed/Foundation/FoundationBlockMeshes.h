#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockRegistry.h"

class UStaticMesh;

// The meshes block instances use: a cube, the two slab halves and the nine water heights. Each is a box in
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
        // Water surfaces at amount / 9 for amounts 1-8, then a full-height column (water above).
        WaterFirst,
        WaterFull = WaterFirst + 8,
        Count
    };

    // The unit box a mesh kind covers.
    UNNAMED_API FBox GetUnitBox(int32 Kind);
    // The mesh kind for a state; water picks its height from the level and whether water is directly above.
    UNNAMED_API int32 GetKind(const FFoundationBlockRegistry& Registry, FFoundationBlockState State, bool bWaterAbove);
    UNNAMED_API UStaticMesh* Build(UObject* Outer, int32 Kind);
}
