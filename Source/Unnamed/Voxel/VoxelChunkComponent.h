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
// The see-through blocks (the Translucent layer: ice, glass, slime, honey) draw from a child of the same class, the
// column's see-through part, made when the column first has any (voxel.SeeThroughPart). Two translucent sections of
// one primitive tie in the engine's translucent sort (one bounds, one priority, and RealtimeMesh gives every section
// mesh id 0), and its unstable sort then picks their order column by column, anew as the view changes: ice over a
// frozen lake flickered between ice over the water and water over the ice (water and sky W3c). The part sorts a
// centimetre nearer than its column, so it draws after the column's water; between columns the distance decides, as
// before.
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
    // a bit per section with geometry; no layers removes the mesh. The see-through layer goes to the see-through part.
    void SetMesh(RealtimeMesh::FRealtimeMeshStreamSet&& Streams, FMeshCardsBuildData&& Cards, uint32 LayerMask,
        uint32 SectionMask);
    int32 GetSectionMeshCount() const { return FMath::CountBits(MeshedSections); }
    // Far LOD (phase 9, S1 and S2): a far mesh casts no shadow unless bShadows (voxel.Far.Shadows) and stays out of ray
    // tracing, and each layer's slot draws in LayerMaterials' entry where it has a material (the far fade's, or
    // voxel.Debug.FarTint's in every slot), else in its own. A full mesh sets all three back. Each changes the render
    // state only when it changes.
    void SetFarLook(bool bFar, bool bShadows, TConstArrayView<TObjectPtr<UMaterialInterface>> LayerMaterials);
    bool IsFarLook() const { return bFarLook; }
    // The see-through part, or null before the column first had see-through blocks; it follows this component's
    // visibility, far look and destruction, and holds no geometry while the column has none.
    const UVoxelChunkComponent* GetSeeThroughPart() const { return SeeThroughPart; }
    bool IsSeeThroughPart() const { return bIsSeeThroughPart; }
    // See-through parts alive, and those holding geometry (a flight's leak check).
    static int32 GetSeeThroughPartCount() { return SeeThroughParts; }
    static int32 GetSeeThroughPartsMeshed() { return SeeThroughPartsMeshed; }
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

    // Sections' geometry as one RealtimeMesh stream set: positions in centimetres, each face's normal and tangent,
    // the three UV channels the chunk materials read (block UV, codes, tint and face), the vertex color (occlusion,
    // light) and a poly group per layer, layer by layer with the see-through layer last. Plain data: any thread.
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

    // Moves the see-through layer's vertices and triangles, the tail of a BuildStreams set, into Out (rebased to its
    // first vertex); Streams keeps the other layers. False, with both untouched, when the set has no such triangles.
    static bool SplitSeeThrough(
        RealtimeMesh::FRealtimeMeshStreamSet& Streams, RealtimeMesh::FRealtimeMeshStreamSet& Out);

protected:
    virtual void OnVisibilityChanged() override;

private:
    UVoxelChunkComponent* MakeSeeThroughPart();

    // Bit per section with geometry in the current mesh.
    uint32 MeshedSections = 0;
    bool bHasBufferSet = false;
    bool bFarLook = false;
    bool bFarShadows = false;
    // This component is a column's see-through part.
    bool bIsSeeThroughPart = false;
    // Each slot's override (a far material, the debug tint), or null; only compared, the subsystem holds them.
    UMaterialInterface* LookMaterials[5] = {};
    // Setup's materials, for the see-through part's slots.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> SlotMaterials;
    UPROPERTY(Transient)
    TObjectPtr<UVoxelChunkComponent> SeeThroughPart;
    static int32 SeeThroughParts;
    static int32 SeeThroughPartsMeshed;
};
