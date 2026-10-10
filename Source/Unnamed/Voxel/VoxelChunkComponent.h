#pragma once

#include "CoreMinimal.h"
#include "RealtimeMeshComponent.h"
#include "VoxelChunkComponent.generated.h"

struct FVoxelMeshData;
class FMeshCardsBuildData;
class UMaterialInterface;
namespace RealtimeMesh
{
    struct FRealtimeMeshStreamSet;
}

// One column's mesh: a RealtimeMesh with a single buffer set holding every section's geometry and one poly group per
// layer, drawn with that layer's chunk material (M_ChunkOpaque, M_ChunkCutout, M_ChunkTranslucent, M_ChunkWater, and
// M_ChunkCutout again for the model quads, which are never carded).
// One buffer set rather than one per section: at the pinned RealtimeMesh commit a mesh with several buffer sets draws
// only the last one created correctly (Docs/VoxelWorld.md), and one set per column is also fewer draws. A changed
// section rebuilds its column's set from the sections' mesh data. The mesh also carries Lumen cards (BuildCards).
// Collision stays off: the Foundation pawn collides against block data, not meshes. Game thread only; workers build
// the streams and cards with BuildStreams and BuildCards.
UCLASS(ClassGroup = Voxel)
class UNNAMED_API UVoxelChunkComponent : public URealtimeMeshComponent
{
    GENERATED_BODY()
public:
    UVoxelChunkComponent();

    // The column's own bounds: its 16 x 16 footprint from the lowest to the highest section with geometry, set with
    // the mesh rather than read back from RealtimeMesh's cache.
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

    // Creates the mesh with one material slot per layer.
    void Setup(TConstArrayView<UMaterialInterface*> LayerMaterials);
    // Replaces the column's geometry and its Lumen cards. LayerMask has a bit per layer with triangles and SectionMask
    // a bit per section with geometry; no layers removes the mesh.
    void SetMesh(RealtimeMesh::FRealtimeMeshStreamSet&& Streams, FMeshCardsBuildData&& Cards, uint32 LayerMask,
        uint32 SectionMask);
    int32 GetSectionMeshCount() const { return FMath::CountBits(MeshedSections); }

    // Sections' geometry as one RealtimeMesh stream set: positions in centimetres, each face's normal and tangent,
    // the three UV channels the chunk materials read (block UV, codes, tint and face), the vertex color (occlusion,
    // light) and a poly group per layer. Plain data: any thread.
    static void BuildStreams(
        TConstArrayView<const FVoxelMeshData*> Sections, RealtimeMesh::FRealtimeMeshStreamSet& Out);
    static uint32 GetLayerMask(TConstArrayView<const FVoxelMeshData*> Sections);

    // The column's Lumen cards: for each face direction, a card per section fitted to the faces Lumen can capture
    // (opaque and cutout), with a direction's vertically closest cards merged until the column has at most
    // MaxCards. Lumen captures each card into its surface cache. Without cards its rays find the terrain black,
    // so no light bounces off the land and shaded slopes go dark. Plain data: any thread.
    static void BuildCards(TConstArrayView<const FVoxelMeshData*> Sections, FMeshCardsBuildData& Out);
    // Lumen looks a mesh's cards up through a 32-bit mask and ignores cards past the 32nd.
    static constexpr int32 MaxCards = 32;

private:
    // Bit per section with geometry in the current mesh.
    uint32 MeshedSections = 0;
    bool bHasBufferSet = false;
};
