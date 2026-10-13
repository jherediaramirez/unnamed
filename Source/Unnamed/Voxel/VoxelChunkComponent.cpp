#include "Voxel/VoxelChunkComponent.h"
#include "Core/RealtimeMeshBuilder.h"
#include "Data/RealtimeMeshUpdateBuilder.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"
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
    static_assert(ChunkLayerCount == 5, "UVoxelChunkComponent::LookMaterials has a slot per layer");

    FRealtimeMeshBufferSetKey GetColumnKey()
    {
        return FRealtimeMeshBufferSetKey::Create(0, 0);
    }

    // How far a card's capture box reaches past its faces, so faces lying on the box's planes are captured.
    constexpr float CardPaddingCm = 10.f;

    // BuildStreams' layer order: the see-through layer last, so SplitSeeThrough cuts it off the streams' tail.
    constexpr EVoxelLayer StreamLayerOrder[] = {
        EVoxelLayer::Opaque, EVoxelLayer::Cutout, EVoxelLayer::Water, EVoxelLayer::Model, EVoxelLayer::Translucent};
    static_assert(UE_ARRAY_COUNT(StreamLayerOrder) == ChunkLayerCount, "Every layer once in StreamLayerOrder");
    constexpr uint32 SeeThroughLayer = uint32(EVoxelLayer::Translucent);

    // How much nearer than its column the see-through part sorts (cm): enough to break the tie, far below the
    // distance between two columns' bounds.
    constexpr float SeeThroughSortOffsetCm = -1.f;

    TAutoConsoleVariable<bool> CVarSeeThroughPart(TEXT("voxel.SeeThroughPart"), true,
        TEXT("1: a column's see-through blocks (ice, glass) draw from their own part, after the column's water. 0: in "
             "the column's mesh, where their order against the water flips with the view (the control). Applies to "
             "columns meshed after the change."));

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

int32 UVoxelChunkComponent::SeeThroughParts = 0;
int32 UVoxelChunkComponent::SeeThroughPartsMeshed = 0;

void UVoxelChunkComponent::Setup(TConstArrayView<UMaterialInterface*> LayerMaterials)
{
    SlotMaterials = TArray<TObjectPtr<UMaterialInterface>>(LayerMaterials);
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

void UVoxelChunkComponent::SetFarLook(
    bool bFar, bool bShadows, TConstArrayView<TObjectPtr<UMaterialInterface>> LayerMaterials)
{
    bFarLook = bFar;
    bFarShadows = bShadows;
    if (SeeThroughPart) SeeThroughPart->SetFarLook(bFar, bShadows, LayerMaterials);
    const bool bCast = !bFar || bShadows;
    if (bool(CastShadow) != bCast) SetCastShadow(bCast);
    if (bool(bVisibleInRayTracing) == bFar) SetVisibleInRayTracing(!bFar);
    for (int32 Layer = 0; Layer < ChunkLayerCount; ++Layer)
    {
        UMaterialInterface* Override =
            bFar && LayerMaterials.IsValidIndex(Layer) ? LayerMaterials[Layer].Get() : nullptr;
        if (Override == LookMaterials[Layer]) continue;
        LookMaterials[Layer] = Override;
        // A null override falls back to the mesh's own slot material.
        SetMaterial(Layer, Override);
    }
}

void UVoxelChunkComponent::SetMesh(
    FRealtimeMeshStreamSet&& Streams, FMeshCardsBuildData&& Cards, uint32 LayerMask, uint32 SectionMask)
{
    URealtimeMeshSimple* Mesh = Cast<URealtimeMeshSimple>(GetRealtimeMesh());
    if (!Mesh) return;
    // The see-through layer moves to the part, which empties when the column has none (the class comment).
    if (!bIsSeeThroughPart)
    {
        FRealtimeMeshStreamSet SeeThrough;
        const bool bSplit = (LayerMask >> SeeThroughLayer & 1) && CVarSeeThroughPart.GetValueOnGameThread() &&
            SplitSeeThrough(Streams, SeeThrough);
        if (bSplit) LayerMask &= ~(1u << SeeThroughLayer);
        if (bSplit || (SeeThroughPart && SeeThroughPart->bHasBufferSet))
            MakeSeeThroughPart()->SetMesh(MoveTemp(SeeThrough), FMeshCardsBuildData(),
                bSplit ? 1u << SeeThroughLayer : 0, bSplit ? SectionMask : 0);
    }
    const bool bHadBufferSet = bHasBufferSet;
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
    if (bIsSeeThroughPart) SeeThroughPartsMeshed += int32(bHasBufferSet) - int32(bHadBufferSet);
    if (MeshedSections != SectionMask)
    {
        MeshedSections = SectionMask;
        UpdateBounds();
        MarkRenderTransformDirty();
    }
}

UVoxelChunkComponent* UVoxelChunkComponent::MakeSeeThroughPart()
{
    if (SeeThroughPart) return SeeThroughPart;
    UVoxelChunkComponent* Part = NewObject<UVoxelChunkComponent>(GetOwner());
    Part->bIsSeeThroughPart = true;
    Part->SetupAttachment(this);
    Part->RegisterComponent();
    TArray<UMaterialInterface*, TInlineAllocator<ChunkLayerCount>> Materials;
    for (UMaterialInterface* Material : SlotMaterials)
        Materials.Add(Material);
    Part->Setup(Materials);
    Part->SetTranslucencySortDistanceOffset(SeeThroughSortOffsetCm);
    Part->SetVisibility(GetVisibleFlag());
    TArray<TObjectPtr<UMaterialInterface>, TInlineAllocator<ChunkLayerCount>> Looks;
    for (UMaterialInterface* Look : LookMaterials)
        Looks.Add(Look);
    Part->SetFarLook(bFarLook, bFarShadows, Looks);
    SeeThroughPart = Part;
    ++SeeThroughParts;
    return Part;
}

void UVoxelChunkComponent::OnVisibilityChanged()
{
    Super::OnVisibilityChanged();
    if (SeeThroughPart) SeeThroughPart->SetVisibility(GetVisibleFlag());
}

void UVoxelChunkComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    if (SeeThroughPart && !SeeThroughPart->IsBeingDestroyed()) SeeThroughPart->DestroyComponent();
    SeeThroughPart = nullptr;
    if (bIsSeeThroughPart)
    {
        --SeeThroughParts;
        SeeThroughPartsMeshed -= int32(bHasBufferSet);
        bHasBufferSet = false;
    }
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UVoxelChunkComponent::SplitSeeThrough(FRealtimeMeshStreamSet& Streams, FRealtimeMeshStreamSet& Out)
{
    const FRealtimeMeshStream* Groups = Streams.Find(FRealtimeMeshStreams::PolyGroups);
    const FRealtimeMeshStream* Triangles = Streams.Find(FRealtimeMeshStreams::Triangles);
    if (!Groups || !Triangles || Groups->Num() != Triangles->Num() || !Groups->Num()) return false;
    // The streams BuildStreams writes, and no others.
    bool bKnown = true;
    Streams.ForEach(
        [&bKnown](const FRealtimeMeshStream& Stream)
        {
            const FRealtimeMeshStreamKey& Key = Stream.GetStreamKey();
            bKnown &= Key.IsVertexStream() || Key == FRealtimeMeshStreams::Triangles ||
                Key == FRealtimeMeshStreams::PolyGroups;
        });
    if (!bKnown) return false;
    // The see-through triangles are the tail (StreamLayerOrder), padding included; their vertices are the tail too.
    const uint16* GroupData = Groups->GetData<uint16>();
    const int32 TriangleCount = Triangles->Num();
    int32 FirstTriangle = TriangleCount;
    while (FirstTriangle > 0 && GroupData[FirstTriangle - 1] == SeeThroughLayer)
        --FirstTriangle;
    if (FirstTriangle == TriangleCount) return false;
    const TIndex3<uint32>* TriangleData = Triangles->GetData<TIndex3<uint32>>();
    uint32 FirstVertex = MAX_uint32;
    for (int32 Triangle = FirstTriangle; Triangle < TriangleCount; ++Triangle)
        FirstVertex = FMath::Min3(
            FirstVertex, FMath::Min(TriangleData[Triangle].V0, TriangleData[Triangle].V1), TriangleData[Triangle].V2);
    Out = FRealtimeMeshStreamSet();
    Streams.ForEach(
        [&Out, FirstTriangle, FirstVertex](FRealtimeMeshStream& Stream)
        {
            const bool bVertex = Stream.GetStreamKey().IsVertexStream();
            const int32 First = bVertex ? int32(FirstVertex) : FirstTriangle;
            FRealtimeMeshStream& Tail = Out.AddStream(Stream.GetStreamKey(), Stream.GetLayout());
            // The same layout: a plain copy.
            Tail.SetNumUninitialized(Stream.Num() - First);
            FMemory::Memcpy(Tail.GetData(), Stream.GetDataRawAtVertex(First), SIZE_T(Tail.Num()) * Stream.GetStride());
            Stream.SetNumUninitialized(First);
            if (Stream.GetStreamKey() == FRealtimeMeshStreams::Triangles)
                for (TIndex3<uint32>& Triangle : MakeArrayView(Tail.GetData<TIndex3<uint32>>(), Tail.Num()))
                {
                    Triangle.V0 -= FirstVertex;
                    Triangle.V1 -= FirstVertex;
                    Triangle.V2 -= FirstVertex;
                }
        });
    return true;
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
    // Layer by layer, so each poly group's triangles are one contiguous run of the index buffer; see-through last.
    for (const EVoxelLayer LayerKind : StreamLayerOrder)
    {
        const int32 LayerIndex = int32(LayerKind);
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
