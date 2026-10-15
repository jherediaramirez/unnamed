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

TArray<FFoundationItemQuad> FoundationItemMesh::BuildQuads(
    int32 Width, int32 Height, TConstArrayView<bool> Opaque, float Grow)
{
    TArray<FFoundationItemQuad> Quads;
    if (Width <= 0 || Height <= 0 || Opaque.Num() != Width * Height) return Quads;
    // A layer over another grows by Grow on both faces and its side faces outward, so it draws over the one under it
    // seen from any side (review 19a2: side faces in the same planes fought along the rim).
    const float Front = FrontZ + Grow, Back = BackZ - Grow;
    // The whole sprite, front and back; a position's UV is the same on both, so the back shows it mirrored.
    for (const float Z : {Front, Back})
    {
        const FVector3f Corners[4] = {{0, 0, Z}, {16, 0, Z}, {16, 16, Z}, {0, 16, Z}};
        const FVector2f UVs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        AddQuad(Quads, Corners, UVs, FIntVector(0, 0, Z == Front ? 1 : -1));
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
            const float Left = X0 - Grow, Right = X1 + Grow, Top = Y1 + Grow, Bottom = Y0 - Grow;
            const FVector2f Centre((X + .5f) / Width, (Y + .5f) / Height);
            const FVector2f UVs[4] = {Centre, Centre, Centre, Centre};
            if (!IsOpaque(X - 1, Y))
            {
                const FVector3f Corners[4] = {{Left, Y0, Back}, {Left, Y1, Back}, {Left, Y1, Front}, {Left, Y0, Front}};
                AddQuad(Quads, Corners, UVs, FIntVector(-1, 0, 0));
            }
            if (!IsOpaque(X + 1, Y))
            {
                const FVector3f Corners[4] = {
                    {Right, Y0, Back}, {Right, Y1, Back}, {Right, Y1, Front}, {Right, Y0, Front}};
                AddQuad(Quads, Corners, UVs, FIntVector(1, 0, 0));
            }
            if (!IsOpaque(X, Y - 1))
            {
                const FVector3f Corners[4] = {{X0, Top, Back}, {X1, Top, Back}, {X1, Top, Front}, {X0, Top, Front}};
                AddQuad(Quads, Corners, UVs, FIntVector(0, 1, 0));
            }
            if (!IsOpaque(X, Y + 1))
            {
                const FVector3f Corners[4] = {
                    {X0, Bottom, Back}, {X1, Bottom, Back}, {X1, Bottom, Front}, {X0, Bottom, Front}};
                AddQuad(Quads, Corners, UVs, FIntVector(0, -1, 0));
            }
        }
    return Quads;
}

TArray<FFoundationItemBox> FoundationItemMesh::GetShieldBoxes()
{
    return {{FVector3f(-6, -11, -2), FVector3f(12, 22, 1), FIntPoint(0, 0)},
        {FVector3f(-1, -3, -1), FVector3f(2, 6, 6), FIntPoint(26, 0)}};
}

TArray<FFoundationItemQuad> FoundationItemMesh::BuildBoxQuads(
    TConstArrayView<FFoundationItemBox> Boxes, const FIntPoint& TextureSize, const FVector3f& Scale)
{
    TArray<FFoundationItemQuad> Quads;
    if (TextureSize.X <= 0 || TextureSize.Y <= 0) return Quads;
    const FIntVector Sign(Scale.X < 0 ? -1 : 1, Scale.Y < 0 ? -1 : 1, Scale.Z < 0 ? -1 : 1);
    for (const FFoundationItemBox& Box : Boxes)
    {
        const FVector3f Lo = Box.Min, Hi = Box.Min + Box.Size;
        const int32 U = Box.TextureOffset.X, V = Box.TextureOffset.Y;
        const int32 W = FMath::RoundToInt(Box.Size.X), H = FMath::RoundToInt(Box.Size.Y),
                    D = FMath::RoundToInt(Box.Size.Z);
        // A face is a texture rectangle (origin, size in pixels) and where its pixel offsets (du, dv) land on the
        // box. Each face reads unmirrored seen from outside after Scale's flips: the front (-Z) and bottom run u
        // with +X, the back with -X; the left (-X) strip runs u with -Z, the right (+X) with +Z; the top's v runs
        // from the back to the front, the bottom's from the front to the back.
        auto AddFace = [&](const FIntVector& Normal, int32 FaceU, int32 FaceV, int32 SizeU, int32 SizeV, auto Map)
        {
            FVector3f Positions[4];
            FVector2f UVs[4];
            const FIntPoint Corners[4] = {{0, 0}, {SizeU, 0}, {SizeU, SizeV}, {0, SizeV}};
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                Positions[Corner] = Map(Corners[Corner].X, Corners[Corner].Y) * Scale;
                UVs[Corner] = FVector2f(
                    float(FaceU + Corners[Corner].X) / TextureSize.X, float(FaceV + Corners[Corner].Y) / TextureSize.Y);
            }
            AddQuad(Quads, Positions, UVs, FIntVector(Normal.X * Sign.X, Normal.Y * Sign.Y, Normal.Z * Sign.Z));
        };
        AddFace(FIntVector(0, -1, 0), U + D, V, W, D,
            [&](int32 Du, int32 Dv) { return FVector3f(Lo.X + Du, Lo.Y, Hi.Z - Dv); });
        AddFace(FIntVector(0, 1, 0), U + D + W, V, W, D,
            [&](int32 Du, int32 Dv) { return FVector3f(Lo.X + Du, Hi.Y, Lo.Z + Dv); });
        AddFace(FIntVector(-1, 0, 0), U, V + D, D, H,
            [&](int32 Du, int32 Dv) { return FVector3f(Lo.X, Lo.Y + Dv, Hi.Z - Du); });
        AddFace(FIntVector(0, 0, -1), U + D, V + D, W, H,
            [&](int32 Du, int32 Dv) { return FVector3f(Lo.X + Du, Lo.Y + Dv, Lo.Z); });
        AddFace(FIntVector(1, 0, 0), U + D + W, V + D, D, H,
            [&](int32 Du, int32 Dv) { return FVector3f(Hi.X, Lo.Y + Dv, Lo.Z + Du); });
        AddFace(FIntVector(0, 0, 1), U + 2 * D + W, V + D, W, H,
            [&](int32 Du, int32 Dv) { return FVector3f(Hi.X - Du, Lo.Y + Dv, Hi.Z); });
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

void FoundationItemMesh::FillMeshDescription(
    FMeshDescription& Mesh, TConstArrayView<FFoundationItemMeshLayer> Layers, double BlockCm)
{
    FStaticMeshAttributes Attributes(Mesh);
    Attributes.Register();
    TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
    TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
    TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
    TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
    UVs.SetNumChannels(2);
    const FPolygonGroupID Group = Mesh.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Item");
    for (const FFoundationItemMeshLayer& Layer : Layers)
    {
        const FVector2f LayerCode(float(Layer.Layer), 0);
        // An untinted layer keeps the attribute's default (white): the mesh is what it was before tints.
        const bool bTinted = Layer.Tint != FColor::White;
        const FVector4f Tint(FLinearColor(Layer.Tint));
        for (const FFoundationItemQuad& Quad : Layer.Quads)
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
                if (bTinted) Colors[Instance] = Tint;
                Instances.Add(Instance);
            }
            Mesh.CreatePolygon(Group, Instances);
        }
    }
}

UStaticMesh* FoundationItemMesh::BuildMesh(
    UObject* Outer, TConstArrayView<FFoundationItemQuad> Quads, int32 Layer, double BlockCm)
{
    FFoundationItemMeshLayer Single;
    Single.Quads.Append(Quads.GetData(), Quads.Num());
    Single.Layer = Layer;
    return BuildMesh(Outer, MakeArrayView(&Single, 1), BlockCm);
}

UStaticMesh* FoundationItemMesh::BuildMesh(
    UObject* Outer, TConstArrayView<FFoundationItemMeshLayer> Layers, double BlockCm)
{
    if (Layers.IsEmpty() || Layers[0].Quads.IsEmpty()) return nullptr;
    FMeshDescription Mesh;
    FillMeshDescription(Mesh, Layers, BlockCm);
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
