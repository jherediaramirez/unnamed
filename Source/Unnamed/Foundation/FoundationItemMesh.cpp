#include "Foundation/FoundationItemMesh.h"
#include "Engine/StaticMesh.h"
#include "Foundation/FoundationItemAssets.h"
#include "MeshDescription.h"
#include "Misc/Parse.h"
#include "StaticMeshAttributes.h"

namespace
{
    // builtin/generated's layer: one model pixel deep about the model's middle, whatever the sprite's size.
    constexpr float FrontZ = 8.5f;
    constexpr float BackZ = 7.5f;

    // Corners wind counter-clockwise seen from outside: Cross(B - A, C - A) points along the normal.
    void AddQuad(TArray<FFoundationItemQuad>& Quads, const FVector3f (&Positions)[4], const FVector2f (&UVs)[4],
        const FIntVector& Normal)
    {
        FFoundationItemQuad& Quad = Quads.AddDefaulted_GetRef();
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            Quad.Positions[Corner] = Positions[Corner];
            Quad.UVs[Corner] = UVs[Corner];
        }
        Quad.Normal = Normal;
        const FVector3f Facing = FVector3f::CrossProduct(Positions[1] - Positions[0], Positions[2] - Positions[0]);
        if (Facing.Dot(FVector3f(Normal)) < 0)
        {
            Swap(Quad.Positions[1], Quad.Positions[3]);
            Swap(Quad.UVs[1], Quad.UVs[3]);
        }
    }
}

bool FoundationItemMesh::ReadMask(const FFoundationItemTextureAsset& Texture, TArray<bool>& Opaque)
{
    // One hex string per row, top row first, the leftmost pixel in the highest bit.
    Opaque.Reset();
    if (Texture.Width <= 0 || Texture.Height <= 0 || Texture.Mask.Num() != Texture.Height) return false;
    Opaque.Reserve(Texture.Width * Texture.Height);
    for (const FString& Row : Texture.Mask)
    {
        if (Row.Len() != FMath::DivideAndRoundUp(Texture.Width, 4)) return false;
        for (int32 X = 0; X < Texture.Width; ++X)
        {
            const TCHAR Digit = Row[X / 4];
            if (!FChar::IsHexDigit(Digit)) return false;
            Opaque.Add(((FParse::HexDigit(Digit) >> (3 - X % 4)) & 1) != 0);
        }
    }
    return true;
}

TArray<FFoundationItemQuad> FoundationItemMesh::BuildQuads(int32 Width, int32 Height, TConstArrayView<bool> Opaque)
{
    TArray<FFoundationItemQuad> Quads;
    if (Width <= 0 || Height <= 0 || Opaque.Num() != Width * Height) return Quads;
    // The whole sprite, front and back; a position's UV is the same on both, so the back shows it mirrored.
    for (const float Z : {FrontZ, BackZ})
    {
        const FVector3f Corners[4] = {{0, 0, Z}, {16, 0, Z}, {16, 16, Z}, {0, 16, Z}};
        const FVector2f UVs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        AddQuad(Quads, Corners, UVs, FIntVector(0, 0, Z == FrontZ ? 1 : -1));
    }
    auto IsOpaque = [&](int32 X, int32 Y)
    { return X >= 0 && Y >= 0 && X < Width && Y < Height && Opaque[Y * Width + X]; };
    const float PixelX = 16.f / Width, PixelY = 16.f / Height;
    for (int32 Y = 0; Y < Height; ++Y)
        for (int32 X = 0; X < Width; ++X)
        {
            if (!IsOpaque(X, Y)) continue;
            // Row Y counts down from the top; model Y counts up.
            const float X0 = X * PixelX, X1 = X0 + PixelX, Y1 = 16 - Y * PixelY, Y0 = Y1 - PixelY;
            const FVector2f Centre((X + .5f) / Width, (Y + .5f) / Height);
            const FVector2f UVs[4] = {Centre, Centre, Centre, Centre};
            if (!IsOpaque(X - 1, Y))
            {
                const FVector3f Corners[4] = {{X0, Y0, BackZ}, {X0, Y1, BackZ}, {X0, Y1, FrontZ}, {X0, Y0, FrontZ}};
                AddQuad(Quads, Corners, UVs, FIntVector(-1, 0, 0));
            }
            if (!IsOpaque(X + 1, Y))
            {
                const FVector3f Corners[4] = {{X1, Y0, BackZ}, {X1, Y1, BackZ}, {X1, Y1, FrontZ}, {X1, Y0, FrontZ}};
                AddQuad(Quads, Corners, UVs, FIntVector(1, 0, 0));
            }
            if (!IsOpaque(X, Y - 1))
            {
                const FVector3f Corners[4] = {{X0, Y1, BackZ}, {X1, Y1, BackZ}, {X1, Y1, FrontZ}, {X0, Y1, FrontZ}};
                AddQuad(Quads, Corners, UVs, FIntVector(0, 1, 0));
            }
            if (!IsOpaque(X, Y + 1))
            {
                const FVector3f Corners[4] = {{X0, Y0, BackZ}, {X1, Y0, BackZ}, {X1, Y0, FrontZ}, {X0, Y0, FrontZ}};
                AddQuad(Quads, Corners, UVs, FIntVector(0, -1, 0));
            }
        }
    return Quads;
}

FVector3f FoundationItemMesh::ToUnreal(const FVector3f& Pixels, double BlockCm)
{
    const FVector3f Model = (Pixels - FVector3f(8)) / 16.f;
    return FVector3f(-Model.Z, Model.X, Model.Y) * float(BlockCm);
}

FVector3f FoundationItemMesh::ToUnrealDirection(const FIntVector& Direction)
{
    return FVector3f(float(-Direction.Z), float(Direction.X), float(Direction.Y));
}

FTransform FoundationItemMesh::MakeDisplayTransform(const FFoundationItemDisplayAsset& Display, double BlockCm)
{
    // The axis map is a mirror (determinant -1), so each Mojang rotation turns about its axis's image by the opposite
    // angle: X about Unreal Y by -a, Y about Unreal Z by -b, Z about Unreal -X by -c (+X by c). FQuat A * B applies
    // B first, so Z still comes first.
    const FVector& Angles = Display.Rotation;
    const FQuat Rotation = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-Angles.X)) *
        FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(-Angles.Y)) *
        FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Angles.Z));
    const FVector Translation = Display.Translation / 16. * BlockCm;
    return FTransform(Rotation, FVector(-Translation.Z, Translation.X, Translation.Y),
        FVector(Display.Scale.Z, Display.Scale.X, Display.Scale.Y));
}

UStaticMesh* FoundationItemMesh::BuildMesh(
    UObject* Outer, TConstArrayView<FFoundationItemQuad> Quads, int32 Layer, double BlockCm)
{
    if (Quads.IsEmpty()) return nullptr;
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
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Item");
    const FVector2f LayerCode(float(Layer), 0);
    for (const FFoundationItemQuad& Quad : Quads)
    {
        const FVector3f Normal = ToUnrealDirection(Quad.Normal);
        // Any tangent in the face plane: the item material uses no normal map.
        const FVector3f Tangent =
            (ToUnreal(Quad.Positions[1], BlockCm) - ToUnreal(Quad.Positions[0], BlockCm)).GetSafeNormal();
        TArray<FVertexInstanceID, TFixedAllocator<4>> Instances;
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            const FVertexID Vertex = Mesh.CreateVertex();
            Positions[Vertex] = ToUnreal(Quad.Positions[Corner], BlockCm);
            const FVertexInstanceID Instance = Mesh.CreateVertexInstance(Vertex);
            Normals[Instance] = Normal;
            Tangents[Instance] = Tangent;
            Signs[Instance] = 1.f;
            UVs.Set(Instance, 0, Quad.UVs[Corner]);
            UVs.Set(Instance, 1, LayerCode);
            Instances.Add(Instance);
        }
        Mesh.CreatePolygon(Group, Instances);
    }
    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
#if WITH_EDITORONLY_DATA
    StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Item"), TEXT("Item")));
#else
    StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Item")));
#endif
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bFastBuild = true;
    Params.bBuildSimpleCollision = false;
    // Sprite UVs sit on 1/32 texel centres and UV 1 holds the layer; full precision keeps both exact.
    UStaticMesh::FBuildMeshDescriptionsLODParams LOD;
    LOD.bUseFullPrecisionUVs = true;
    Params.PerLODOverrides.Add(LOD);
    StaticMesh->BuildFromMeshDescriptions({&Mesh}, Params);
    return StaticMesh;
}
