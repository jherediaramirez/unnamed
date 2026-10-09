#include "Foundation/FoundationBlockMeshes.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

FBox FoundationBlockMeshes::GetUnitBox(int32 Kind)
{
    switch (Kind)
    {
    case SlabBottom:
        return FBox(FVector::ZeroVector, FVector(1, 1, .5));
    case SlabTop:
        return FBox(FVector(0, 0, .5), FVector::OneVector);
    default:
        if (Kind >= WaterFirst && Kind < WaterFull)
            return FBox(FVector::ZeroVector, FVector(1, 1, (Kind - WaterFirst + 1) / 9.));
        return FBox(FVector::ZeroVector, FVector::OneVector);
    }
}

int32 FoundationBlockMeshes::GetKind(
    const FFoundationBlockRegistry& Registry, FFoundationBlockState State, bool bWaterAbove)
{
    switch (Registry.GetShape(State))
    {
    case EFoundationBlockShape::SlabBottom:
        return SlabBottom;
    case EFoundationBlockShape::SlabTop:
        return SlabTop;
    case EFoundationBlockShape::Fluid:
    {
        const FFoundationFluid Fluid = Registry.GetFluid(State);
        return bWaterAbove ? WaterFull : WaterFirst + FMath::Clamp(Fluid.Amount, 1, 8) - 1;
    }
    default:
        return Cube;
    }
}

UStaticMesh* FoundationBlockMeshes::Build(UObject* Outer, int32 Kind)
{
    const FBox Unit = GetUnitBox(Kind);
    FMeshDescription Mesh;
    FStaticMeshAttributes Attributes(Mesh);
    Attributes.Register();
    TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
    TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
    TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
    UVs.SetNumChannels(2);
    const FPolygonGroupID Group = Mesh.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Block");

    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        const EFoundationFace Face = EFoundationFace(FaceIndex);
        const FVector3f Normal(FoundationGrid::FaceNormal(Face));
        const int32 Axis = FaceIndex / 2;
        const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
        // The four corners of the face in block units.
        FVector3f Corners[4];
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            FVector3f P;
            P[Axis] = float(FaceIndex % 2 ? Unit.Max[Axis] : Unit.Min[Axis]);
            P[U] = float(Corner == 1 || Corner == 2 ? Unit.Max[U] : Unit.Min[U]);
            P[V] = float(Corner >= 2 ? Unit.Max[V] : Unit.Min[V]);
            Corners[Corner] = P;
        }
        // Unreal draws triangles that wind clockwise seen from outside: Cross(B - A, C - A) opposes the normal.
        if (FVector3f::CrossProduct(Corners[1] - Corners[0], Corners[2] - Corners[0]).Dot(Normal) > 0)
            Swap(Corners[1], Corners[3]);
        // Any tangent in the face plane: the block materials use no normal maps.
        const FVector3f Tangent =
            Axis == 2 ? FVector3f(1, 0, 0) : FVector3f::CrossProduct(FVector3f(0, 0, 1), Normal).GetSafeNormal();
        TArray<FVertexInstanceID, TFixedAllocator<4>> Instances;
        for (const FVector3f& P : Corners)
        {
            const FVertexID Vertex = Mesh.CreateVertex();
            Positions[Vertex] = P * float(UFoundationProfile::BlockCm);
            const FVertexInstanceID Instance = Mesh.CreateVertexInstance(Vertex);
            Normals[Instance] = Normal;
            Tangents[Instance] = Tangent;
            Signs[Instance] = 1.f;
            UVs.Set(Instance, 0, FoundationBlockFaces::GetCanonicalUV(Face, P));
            UVs.Set(Instance, 1, FVector2f(float(FaceIndex), 0));
            Instances.Add(Instance);
        }
        Mesh.CreatePolygon(Group, Instances);
    }

    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
#if WITH_EDITORONLY_DATA
    StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Block"), TEXT("Block")));
#else
    StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Block")));
#endif
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bFastBuild = true;
    Params.bBuildSimpleCollision = false;
    StaticMesh->BuildFromMeshDescriptions({&Mesh}, Params);
    return StaticMesh;
}
