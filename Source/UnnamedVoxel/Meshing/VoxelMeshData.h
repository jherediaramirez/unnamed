#pragma once

#include "CoreMinimal.h"
#include "Blocks/VoxelBlockTable.h"

// One material layer's geometry: four vertices per quad, two triangles each. Positions are in blocks from the column's
// minimum corner, with Z the world height, so the chunk component only scales them by the block size.
struct FVoxelMeshLayer
{
    TArray<FVector3f> Positions;
    // The face's block UV, continuous across a merged quad; the material takes each block's fraction after applying
    // the face's UV transform.
    TArray<FVector2f> BlockUVs;
    // (face code, overlay code): integers below 2^24, exact in a float.
    TArray<FVector2f> Codes;
    // (packed sRGB tint, face index in EVoxelFace order).
    TArray<FVector2f> TintFaces;
    // R: ambient occlusion, 0 (darkest) to 255 (open) in thirds. G: light emission x 17.
    TArray<FColor> Colors;
    TArray<uint32> Indices;

    int32 GetQuadCount() const { return Positions.Num() / 4; }
    bool IsEmpty() const { return Positions.IsEmpty(); }

    void Reset()
    {
        Positions.Reset();
        BlockUVs.Reset();
        Codes.Reset();
        TintFaces.Reset();
        Colors.Reset();
        Indices.Reset();
    }

    SIZE_T GetAllocatedSize() const
    {
        return Positions.GetAllocatedSize() + BlockUVs.GetAllocatedSize() + Codes.GetAllocatedSize() +
            TintFaces.GetAllocatedSize() + Colors.GetAllocatedSize() + Indices.GetAllocatedSize();
    }
};

// One section's mesh, a layer per EVoxelLayer (one material slot each).
struct FVoxelMeshData
{
    FVoxelMeshLayer Layers[int32(EVoxelLayer::Count)];

    int32 GetQuadCount() const
    {
        int32 Count = 0;
        for (const FVoxelMeshLayer& Layer : Layers)
            Count += Layer.GetQuadCount();
        return Count;
    }

    bool IsEmpty() const { return GetQuadCount() == 0; }

    void Reset()
    {
        for (FVoxelMeshLayer& Layer : Layers)
            Layer.Reset();
    }
};
