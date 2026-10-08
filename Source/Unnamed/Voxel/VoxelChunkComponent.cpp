#include "Voxel/VoxelChunkComponent.h"
#include "Core/RealtimeMeshBuilder.h"
#include "Foundation/FoundationProfile.h"
#include "Meshing/VoxelMeshData.h"
#include "RealtimeMeshSimple.h"
#include "VoxelConstants.h"

using namespace RealtimeMesh;

namespace
{
    // Normal and tangent (the U axis) per EVoxelFace.
    const FVector3f FaceNormals[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const FVector3f FaceTangents[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    const TCHAR* const ChunkSlotNames[] = {TEXT("Opaque"), TEXT("Cutout"), TEXT("Translucent"), TEXT("Water")};
    constexpr int32 ChunkLayerCount = UE_ARRAY_COUNT(ChunkSlotNames);

    FRealtimeMeshBufferSetKey GetColumnKey()
    {
        return FRealtimeMeshBufferSetKey::Create(0, 0);
    }
}

UVoxelChunkComponent::UVoxelChunkComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCanEverAffectNavigation(false);
    SetGenerateOverlapEvents(false);
}

void UVoxelChunkComponent::Setup(TConstArrayView<UMaterialInterface*> LayerMaterials)
{
    URealtimeMeshSimple* Mesh = InitializeRealtimeMesh<URealtimeMeshSimple>();
    for (int32 Layer = 0; Layer < ChunkLayerCount; ++Layer)
        Mesh->SetupMaterialSlot(
            Layer, ChunkSlotNames[Layer], LayerMaterials.IsValidIndex(Layer) ? LayerMaterials[Layer] : nullptr);
    MeshedSections = 0;
    bHasBufferSet = false;
}

FBoxSphereBounds UVoxelChunkComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    constexpr double BlockCm = UFoundationProfile::BlockCm;
    constexpr double SectionCm = VoxelConstants::SectionSize * BlockCm;
    if (!MeshedSections)
        return FBoxSphereBounds(FBox(FVector::ZeroVector, FVector::ZeroVector)).TransformBy(LocalToWorld);
    const int32 Lowest = FMath::CountTrailingZeros(MeshedSections),
                Highest = 31 - FMath::CountLeadingZeros(MeshedSections);
    const double Bottom = VoxelConstants::MinZ * BlockCm + Lowest * SectionCm;
    const double Top = VoxelConstants::MinZ * BlockCm + (Highest + 1) * SectionCm;
    const double Side = VoxelConstants::ColumnSize * BlockCm;
    return FBoxSphereBounds(FBox(FVector(0, 0, Bottom), FVector(Side, Side, Top))).TransformBy(LocalToWorld);
}

uint32 UVoxelChunkComponent::GetLayerMask(TConstArrayView<const FVoxelMeshData*> Sections)
{
    uint32 Mask = 0;
    for (const FVoxelMeshData* Section : Sections)
        for (int32 Layer = 0; Layer < ChunkLayerCount; ++Layer)
            if (!Section->Layers[Layer].IsEmpty()) Mask |= 1u << Layer;
    return Mask;
}

void UVoxelChunkComponent::SetMesh(FRealtimeMeshStreamSet&& Streams, uint32 LayerMask, uint32 SectionMask)
{
    URealtimeMeshSimple* Mesh = Cast<URealtimeMeshSimple>(GetRealtimeMesh());
    if (!Mesh) return;
    const FRealtimeMeshBufferSetKey Key = GetColumnKey();
    if (!LayerMask)
    {
        if (bHasBufferSet) Mesh->RemoveBufferSet(Key);
        bHasBufferSet = false;
        SectionMask = 0;
    }
    else if (bHasBufferSet)
        Mesh->UpdateBufferSet(Key, MoveTemp(Streams));
    else
    {
        Mesh->CreateBufferSet(Key, MoveTemp(Streams));
        bHasBufferSet = true;
    }
    if (MeshedSections != SectionMask)
    {
        MeshedSections = SectionMask;
        UpdateBounds();
        MarkRenderTransformDirty();
    }
    // Each poly group became a section; point it at its layer's slot. Water casts no shadow.
    for (int32 Layer = 0; Layer < ChunkLayerCount; ++Layer)
    {
        if (!(LayerMask & (1u << Layer))) continue;
        FRealtimeMeshSectionConfig Config(Layer);
        Config.bCastsShadow = Layer != ChunkLayerCount - 1;
        Mesh->UpdateSectionConfig(FRealtimeMeshSectionKey::CreateForPolyGroup(Key, Layer), Config);
    }
}

void UVoxelChunkComponent::BuildStreams(TConstArrayView<const FVoxelMeshData*> Sections, FRealtimeMeshStreamSet& Out)
{
    Out = FRealtimeMeshStreamSet();
    TRealtimeMeshBuilderLocal<uint32, FPackedNormal, FVector2f, 3, uint16> Builder(Out);
    Builder.EnableTangents();
    Builder.EnableTexCoords();
    Builder.EnableColors();
    Builder.EnablePolyGroups();
    int32 Vertices = 0, Indices = 0;
    for (const FVoxelMeshData* Section : Sections)
        for (const FVoxelMeshLayer& Layer : Section->Layers)
        {
            Vertices += Layer.Positions.Num();
            Indices += Layer.Indices.Num();
        }
    Builder.ReserveNumVertices(Vertices);
    Builder.ReserveNumTriangles(Indices / 3 + 3 * ChunkLayerCount);
    constexpr float BlockCm = float(UFoundationProfile::BlockCm);
    // Layer by layer, so each poly group's triangles are one contiguous run of the index buffer.
    for (int32 LayerIndex = 0; LayerIndex < ChunkLayerCount; ++LayerIndex)
    {
        int32 Triangles = 0;
        uint32 LayerFirst = uint32(Builder.NumVertices());
        for (const FVoxelMeshData* Section : Sections)
        {
            const FVoxelMeshLayer& Layer = Section->Layers[LayerIndex];
            const uint32 First = uint32(Builder.NumVertices());
            for (int32 Vertex = 0; Vertex < Layer.Positions.Num(); ++Vertex)
            {
                const int32 Face = FMath::Clamp(FMath::RoundToInt32(Layer.TintFaces[Vertex].Y), 0, 5);
                Builder.AddVertex(Layer.Positions[Vertex] * BlockCm)
                    .SetNormalAndTangent(FaceNormals[Face], FaceTangents[Face])
                    .SetTexCoord(0, Layer.BlockUVs[Vertex])
                    .SetTexCoord(1, Layer.Codes[Vertex])
                    .SetTexCoord(2, Layer.TintFaces[Vertex])
                    .SetColor(Layer.Colors[Vertex]);
            }
            for (int32 Index = 0; Index + 2 < Layer.Indices.Num(); Index += 3, ++Triangles)
                Builder.AddTriangle(First + Layer.Indices[Index], First + Layer.Indices[Index + 1],
                    First + Layer.Indices[Index + 2], uint32(LayerIndex));
        }
        // RealtimeMesh gives each poly group its own ray-tracing segment at the group's offset in the index buffer,
        // and D3D12 needs that offset to be a multiple of 16 bytes: pad every layer to a multiple of four triangles
        // with zero-area ones, which neither rasterization nor ray tracing draws.
        for (; Triangles && Triangles % 4; ++Triangles)
            Builder.AddTriangle(LayerFirst, LayerFirst, LayerFirst, uint32(LayerIndex));
    }
}
