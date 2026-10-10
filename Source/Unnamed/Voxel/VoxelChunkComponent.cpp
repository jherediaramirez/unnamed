#include "Voxel/VoxelChunkComponent.h"
#include "Core/RealtimeMeshBuilder.h"
#include "Data/RealtimeMeshUpdateBuilder.h"
#include "Foundation/FoundationProfile.h"
#include "Mesh/RealtimeMeshCardRepresentation.h"
#include "MeshCardBuild.h"
#include "MeshCardRepresentation.h"
#include "Meshing/VoxelMeshData.h"
#include "RealtimeMeshSimple.h"
#include "VoxelConstants.h"

using namespace RealtimeMesh;

namespace
{
    // Normal and tangent (the U axis) per EVoxelFace.
    const FVector3f FaceNormals[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const FVector3f FaceTangents[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    const TCHAR* const ChunkSlotNames[] = {
        TEXT("Opaque"), TEXT("Cutout"), TEXT("Translucent"), TEXT("Water"), TEXT("Model")};
    constexpr int32 ChunkLayerCount = UE_ARRAY_COUNT(ChunkSlotNames);
    static_assert(ChunkLayerCount == int32(EVoxelLayer::Count), "A material slot per EVoxelLayer");

    FRealtimeMeshBufferSetKey GetColumnKey()
    {
        return FRealtimeMeshBufferSetKey::Create(0, 0);
    }

    // How far a card's capture box reaches past its faces, so faces lying on the box's planes are captured.
    constexpr float CardPaddingCm = 10.f;

    // A layer's section: its own material slot. Water casts no shadow. (A section's own shadow flag counts only while
    // no other section of the column casts: RealtimeMesh draws one mesh batch per buffer set, its shadow flag the OR
    // of its sections'. So plants cast shadows at every distance.)
    FRealtimeMeshSectionConfig GetChunkLayerSection(int32 Layer)
    {
        FRealtimeMeshSectionConfig Config(Layer);
        Config.bCastsShadow = Layer != int32(EVoxelLayer::Water);
        return Config;
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

void UVoxelChunkComponent::SetMesh(
    FRealtimeMeshStreamSet&& Streams, FMeshCardsBuildData&& Cards, uint32 LayerMask, uint32 SectionMask)
{
    URealtimeMeshSimple* Mesh = Cast<URealtimeMeshSimple>(GetRealtimeMesh());
    if (!Mesh) return;
    const FRealtimeMeshBufferSetKey Key = GetColumnKey();
    // One RealtimeMesh update for the buffer set, its sections and the cards: each update commits separately, so the
    // calls URealtimeMeshSimple offers would make up to six.
    FRealtimeMeshUpdateBuilder Update;
    if (!LayerMask)
    {
        if (bHasBufferSet)
            Update.AddLODTask<FRealtimeMeshLODManaged>(Key,
                [Key](FRealtimeMeshUpdateContext& Context, FRealtimeMeshLODManaged& LOD)
                { LOD.RemoveSectionGroup(Context, Key); });
        bHasBufferSet = false;
        SectionMask = 0;
    }
    else
    {
        const bool bCreate = !bHasBufferSet;
        if (bCreate)
            Update.AddLODTask<FRealtimeMeshLODManaged>(Key,
                [Key](FRealtimeMeshUpdateContext& Context, FRealtimeMeshLODManaged& LOD)
                { LOD.CreateOrUpdateSectionGroup(Context, Key, FRealtimeMeshBufferSetConfig()); });
        Update.AddSectionGroupTask<FRealtimeMeshBufferSetSimple>(Key,
            [bCreate, Streams = MoveTemp(Streams)](
                FRealtimeMeshUpdateContext& Context, FRealtimeMeshBufferSetSimple& Set) mutable
            {
                // Each poly group becomes a section on its layer's slot, configured once when the set is made.
                if (bCreate)
                    Set.SetPolyGroupSectionHandler(
                        Context, FRealtimeMeshPolyGroupConfigHandler::CreateStatic(&GetChunkLayerSection));
                Set.SetAllStreams(Context, MoveTemp(Streams));
            });
        bHasBufferSet = true;
    }
    // The cards travel with the geometry: RealtimeMesh hands them to its scene proxy, which gives them to Lumen.
    Update.AddMeshTask<FRealtimeMeshSimple>(
        [bCards = LayerMask && !Cards.CardBuildData.IsEmpty(), Cards = MoveTemp(Cards)](
            FRealtimeMeshUpdateContext& Context, FRealtimeMeshSimple& Data) mutable
        {
            if (!bCards)
            {
                Data.ClearCardRepresentation(Context);
                return;
            }
            FCardRepresentationData Representation;
            Representation.MeshCardsBuildData = MoveTemp(Cards);
            Data.SetCardRepresentation(Context, FRealtimeMeshCardRepresentation(Representation));
        });
    Update.Commit(Mesh->GetMeshData());
    if (MeshedSections != SectionMask)
    {
        MeshedSections = SectionMask;
        UpdateBounds();
        MarkRenderTransformDirty();
    }
}

void UVoxelChunkComponent::BuildCards(TConstArrayView<const FVoxelMeshData*> Sections, FMeshCardsBuildData& Out)
{
    Out = FMeshCardsBuildData();
    constexpr float BlockCm = float(UFoundationProfile::BlockCm);
    // Per face direction (EVoxelFace, the same order as Lumen's axis-aligned directions), each section's box of the
    // faces Lumen captures: translucent layers are not in its surface cache.
    TArray<FBox3f> Boxes[6];
    for (const FVoxelMeshData* Section : Sections)
    {
        FBox3f SectionBoxes[6];
        for (FBox3f& Box : SectionBoxes)
            Box.Init();
        for (const EVoxelLayer LayerKind : {EVoxelLayer::Opaque, EVoxelLayer::Cutout})
        {
            const FVoxelMeshLayer& Layer = Section->Layers[int32(LayerKind)];
            for (int32 Vertex = 0; Vertex < Layer.Positions.Num(); ++Vertex)
                SectionBoxes[FMath::Clamp(FMath::RoundToInt32(Layer.TintFaces[Vertex].Y), 0, 5)] +=
                    Layer.Positions[Vertex] * BlockCm;
        }
        for (int32 Face = 0; Face < 6; ++Face)
            if (SectionBoxes[Face].IsValid) Boxes[Face].Add(SectionBoxes[Face]);
    }
    int32 Count = 0;
    for (TArray<FBox3f>& Faces : Boxes)
    {
        Faces.Sort([](const FBox3f& A, const FBox3f& B) { return A.Min.Z < B.Min.Z; });
        Count += Faces.Num();
    }
    // Over budget (columns at the edge of the loaded area have faces in every section): in the direction with the
    // most cards, merge the neighbouring pair whose union is shortest.
    while (Count > MaxCards)
    {
        int32 Busiest = 0;
        for (int32 Face = 1; Face < 6; ++Face)
            if (Boxes[Face].Num() > Boxes[Busiest].Num()) Busiest = Face;
        TArray<FBox3f>& Faces = Boxes[Busiest];
        int32 Best = 0;
        for (int32 Index = 1; Index + 1 < Faces.Num(); ++Index)
            if ((Faces[Index] + Faces[Index + 1]).GetSize().Z < (Faces[Best] + Faces[Best + 1]).GetSize().Z)
                Best = Index;
        Faces[Best] += Faces[Best + 1];
        Faces.RemoveAt(Best + 1);
        --Count;
    }
    for (int32 Face = 0; Face < 6; ++Face)
        for (const FBox3f& Box : Boxes[Face])
        {
            // The card looks back along its direction at the faces that point that way, as
            // MeshCardRepresentation::SetCardsFromBounds builds a mesh's six cards.
            FLumenCardBuildData& Card = Out.CardBuildData.AddDefaulted_GetRef();
            Card.AxisAlignedDirectionIndex = uint8(Face);
            FLumenCardOBBf& CardBox = Card.OBB;
            CardBox.AxisZ = MeshCardRepresentation::GetAxisAlignedDirection(Face);
            CardBox.AxisZ.FindBestAxisVectors(CardBox.AxisX, CardBox.AxisY);
            CardBox.AxisX = FVector3f::CrossProduct(CardBox.AxisZ, CardBox.AxisY).GetSafeNormal();
            const FVector3f Extent = Box.GetExtent() + FVector3f(CardPaddingCm);
            CardBox.Extent = FVector3f(FMath::Abs(CardBox.AxisX | Extent), FMath::Abs(CardBox.AxisY | Extent),
                FMath::Abs(CardBox.AxisZ | Extent));
            CardBox.Origin = Box.GetCenter();
            Out.Bounds += FBox(FVector(Box.Min) - CardPaddingCm, FVector(Box.Max) + CardPaddingCm);
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
