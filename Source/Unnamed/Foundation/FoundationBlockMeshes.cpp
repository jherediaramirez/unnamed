#include "Foundation/FoundationBlockMeshes.h"
#include "Engine/StaticMesh.h"
#include "Foundation/FoundationBlockAssets.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Voxel/VoxelLights.h"

namespace
{
    // Any tangent in the face plane: the block materials use no normal maps.
    FVector3f GetBlockMeshTangent(EFoundationFace Face)
    {
        const FVector3f Normal(FoundationGrid::FaceNormal(Face));
        return int32(Face) / 2 == 2 ? FVector3f(1, 0, 0)
                                    : FVector3f::CrossProduct(FVector3f(0, 0, 1), Normal).GetSafeNormal();
    }

    UStaticMesh* FinishBlockMesh(UObject* Outer, const FMeshDescription& Mesh, bool bFullPrecisionUVs)
    {
        UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
#if WITH_EDITORONLY_DATA
        StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Block"), TEXT("Block")));
#else
        StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Block")));
#endif
        UStaticMesh::FBuildMeshDescriptionsParams Params;
        Params.bFastBuild = true;
        Params.bBuildSimpleCollision = false;
        if (bFullPrecisionUVs)
        {
            UStaticMesh::FBuildMeshDescriptionsLODParams LOD;
            LOD.bUseFullPrecisionUVs = true;
            Params.PerLODOverrides.Add(LOD);
        }
        StaticMesh->BuildFromMeshDescriptions({&Mesh}, Params);
        return StaticMesh;
    }

    // A torch quad cut at the flame line (FoundationBlockMeshes::TorchFlameV) when the line crosses two opposite
    // edges, (A, B) and (C, D): one piece keeps corners A and D, the other B and C, each in its corner's slot, so both
    // keep the quad's winding and corner order. The flame is the piece above the line (smaller v: texture rows grow
    // down). False, and nothing written, when the line does not cross the quad that way.
    bool CutAtTorchFlame(
        const FFoundationModelQuad& Quad, FFoundationModelQuad& OutFlame, FFoundationModelQuad& OutStick)
    {
        constexpr float Line = FoundationBlockMeshes::TorchFlameV;
        auto Crosses = [&Quad](int32 Corner)
        {
            const float From = Quad.UVs[Corner].Y - Line, To = Quad.UVs[(Corner + 1) % 4].Y - Line;
            return (From < 0 && To > 0) || (From > 0 && To < 0);
        };
        auto CutEdge = [&Quad](int32 From, int32 To, FVector3f& OutPosition, FVector2f& OutUV)
        {
            const float T = (Line - Quad.UVs[From].Y) / (Quad.UVs[To].Y - Quad.UVs[From].Y);
            OutPosition = FMath::Lerp(Quad.Positions[From], Quad.Positions[To], T);
            OutUV = FVector2f(FMath::Lerp(Quad.UVs[From].X, Quad.UVs[To].X, T), Line);
        };
        for (int32 A = 0; A < 2; ++A)
        {
            const int32 B = A + 1, C = A + 2, D = (A + 3) % 4;
            if (!Crosses(A) || !Crosses(C) || Crosses(B) || Crosses(D)) continue;
            FVector3f AtB, AtC;
            FVector2f AtBUV, AtCUV;
            CutEdge(A, B, AtB, AtBUV);
            CutEdge(D, C, AtC, AtCUV);
            FFoundationModelQuad WithA = Quad, WithB = Quad;
            WithA.Positions[B] = AtB;
            WithA.UVs[B] = AtBUV;
            WithA.Positions[C] = AtC;
            WithA.UVs[C] = AtCUV;
            WithB.Positions[A] = AtB;
            WithB.UVs[A] = AtBUV;
            WithB.Positions[D] = AtC;
            WithB.UVs[D] = AtCUV;
            const bool bFlameWithA = Quad.UVs[A].Y < Line;
            OutFlame = bFlameWithA ? WithA : WithB;
            OutStick = bFlameWithA ? WithB : WithA;
            OutFlame.Light = FMath::Max(OutFlame.Light, FoundationBlockMeshes::TorchFlameLight);
            return true;
        }
        return false;
    }
}

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
        if (Kind >= LayersFirst && Kind < Count)
            return FBox(FVector::ZeroVector, FVector(1, 1, (Kind - LayersFirst + 1) / 8.));
        return FBox(FVector::ZeroVector, FVector::OneVector);
    }
}

int32 FoundationBlockMeshes::GetKind(
    const FFoundationBlockRegistry& Registry, FFoundationBlockState State, bool bWaterAbove)
{
    const EFoundationBlockShape Shape = Registry.GetShape(State);
    if (const int32 Layers = GetFoundationShapeLayers(Shape)) return LayersFirst + Layers - 1;
    switch (Shape)
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
        const FVector3f Tangent = GetBlockMeshTangent(Face);
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
    return FinishBlockMesh(Outer, Mesh, false);
}

bool FoundationBlockMeshes::HoldsFluid(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
{
    const EFoundationBlockShape Shape = Registry.GetShape(State);
    return Shape == EFoundationBlockShape::Fluid ||
        (Shape == EFoundationBlockShape::Model && Registry.GetFluid(State).Amount > 0);
}

uint8 FoundationBlockMeshes::PackFluid(
    const FFoundationBlockRegistry& Registry, FFoundationBlockState State, bool bFluidAbove)
{
    const EFoundationBlockShape Shape = Registry.GetShape(State);
    const FFoundationFluid Fluid = Registry.GetFluid(State);
    FVoxelStateInfo Info;
    if (Shape == EFoundationBlockShape::Fluid)
    {
        Info.Fluid = Fluid.bLava ? EVoxelFluid::Lava : Fluid.bWater ? EVoxelFluid::Water : EVoxelFluid::None;
        Info.FluidAmount = uint8(FMath::Clamp(Fluid.Amount, 0, 8));
        Info.FluidLevel = uint8(FMath::Clamp(Fluid.Level, 0, 15));
    }
    else if (Shape == EFoundationBlockShape::Model && Fluid.Amount > 0)
    {
        // The water a model holds is a source, as the voxel table's HeldFluid is.
        Info.Fluid = EVoxelFluid::Water;
        Info.FluidAmount = 8;
    }
    return VoxelFluidSurface::Pack(Info, bFluidAbove);
}

bool FoundationBlockMeshes::MakeFluidSurface(const FFoundationBlockRegistry& Registry,
    TFunctionRef<FFoundationBlockState(const FIntVector&)> GetState, const FIntVector& Cell, int32 Mode,
    bool bOpenCorners, VoxelFluidSurface::FSurface& Out)
{
    if (Mode <= 0) return false;
    uint8 Ring[9];
    for (int32 DY = -1; DY <= 1; ++DY)
        for (int32 DX = -1; DX <= 1; ++DX)
        {
            const FIntVector At = Cell + FIntVector(DX, DY, 0);
            const FFoundationBlockState State = GetState(At);
            // Drawn full under any fluid, as the voxel mesher packs it.
            Ring[(DY + 1) * 3 + DX + 1] = HoldsFluid(Registry, State)
                ? PackFluid(Registry, State, HoldsFluid(Registry, GetState(At + FIntVector(0, 0, 1))))
                : bOpenCorners && State.IsAir() ? VoxelFluidSurface::OpenCell
                                                : 0;
        }
    return VoxelFluidSurface::Make(Ring, Mode, Out, bOpenCorners);
}

bool FoundationBlockMeshes::IsFlowTop(const VoxelFluidSurface::FSurface& Surface)
{
    return !Surface.Flow.IsNearlyZero(VoxelFluidSurface::FlatFlow);
}

void FoundationBlockMeshes::GetFluidFaces(const VoxelFluidSurface::FSurface& Surface, FFluidFace Out[6])
{
    const bool bFlowTop = IsFlowTop(Surface);
    FVector2f FlowUVs[4];
    if (bFlowTop) VoxelFluidSurface::GetFlowUVs(Surface.Flow, FlowUVs);
    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        // VoxelMesher's FluidFace corners in the cell's own space.
        const int32 Axis = FaceIndex / 2, AxisU = (Axis + 1) % 3, AxisV = (Axis + 2) % 3;
        const bool bPositive = FaceIndex % 2 != 0;
        const float CornerU[4] = {0, bPositive ? 0.f : 1.f, 1, bPositive ? 1.f : 0.f};
        const float CornerV[4] = {0, bPositive ? 1.f : 0.f, 1, bPositive ? 0.f : 1.f};
        FFluidFace& Face = Out[FaceIndex];
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            FVector3f P;
            P[Axis] = bPositive ? 1.f : 0.f;
            P[AxisU] = CornerU[Corner];
            P[AxisV] = CornerV[Corner];
            const int32 Top = VoxelFluidSurface::GetCorner(P.X > .5f, P.Y > .5f);
            if (P.Z > .5f) P.Z = Surface.Corners[Top];
            Face.Positions[Corner] = P;
            Face.UVs[Corner] = bFlowTop && FaceIndex == int32(EFoundationFace::Up)
                ? FlowUVs[Top]
                : FoundationBlockFaces::GetCanonicalUV(EFoundationFace(FaceIndex), P);
        }
    }
}

uint32 FoundationBlockMeshes::GetFluidSurfaceKey(const VoxelFluidSurface::FSurface& Surface)
{
    uint32 Key = 0;
    for (int32 Corner = 0; Corner < 4; ++Corner)
        Key |= uint32(FMath::Clamp(FMath::RoundToInt32(Surface.Corners[Corner] * 108), 0, 127)) << (7 * Corner);
    return Key;
}

UStaticMesh* FoundationBlockMeshes::BuildFluidSurface(UObject* Outer, const VoxelFluidSurface::FSurface& Surface)
{
    FFluidFace Faces[6];
    GetFluidFaces(Surface, Faces);
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
        // Lit as the face's own normal, as the voxel world lights a sloped top as up.
        const FVector3f Normal(FoundationGrid::FaceNormal(Face));
        const FVector3f Tangent = GetBlockMeshTangent(Face);
        FFluidFace Quad = Faces[FaceIndex];
        // Unreal draws triangles that wind clockwise seen from outside. Swapping corners 1 and 3 keeps the 0-2
        // diagonal the voxel mesher splits along.
        if (FVector3f::CrossProduct(Quad.Positions[1] - Quad.Positions[0], Quad.Positions[2] - Quad.Positions[0])
                .Dot(Normal) > 0)
        {
            Swap(Quad.Positions[1], Quad.Positions[3]);
            Swap(Quad.UVs[1], Quad.UVs[3]);
        }
        FVertexInstanceID Instances[4];
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            const FVertexID Vertex = Mesh.CreateVertex();
            Positions[Vertex] = Quad.Positions[Corner] * float(UFoundationProfile::BlockCm);
            const FVertexInstanceID Instance = Mesh.CreateVertexInstance(Vertex);
            Normals[Instance] = Normal;
            Tangents[Instance] = Tangent;
            Signs[Instance] = 1.f;
            UVs.Set(Instance, 0, Quad.UVs[Corner]);
            UVs.Set(Instance, 1, FVector2f(float(FaceIndex), 0));
            Instances[Corner] = Instance;
        }
        const FVertexInstanceID First[3] = {Instances[0], Instances[1], Instances[2]};
        const FVertexInstanceID Second[3] = {Instances[0], Instances[2], Instances[3]};
        Mesh.CreateTriangle(Group, First);
        Mesh.CreateTriangle(Group, Second);
    }
    return FinishBlockMesh(Outer, Mesh, false);
}

uint32 FoundationBlockMeshes::MakeModelCode(const FFoundationModelQuad& Quad)
{
    return (uint32(Quad.Texture) & FoundationBlockFaces::TextureMask) |
        (Quad.TintIndex >= 0 ? FoundationBlockFaces::TintBit : 0u) |
        uint32(FMath::Clamp(Quad.Light, 0, 15)) << ModelLightShift | ModelCodeBit;
}

bool FoundationBlockMeshes::IsRepeatedQuad(TConstArrayView<FFoundationModelQuad> Quads, int32 Index)
{
    const FFoundationModelQuad& Quad = Quads[Index];
    for (int32 Earlier = 0; Earlier < Index; ++Earlier)
    {
        const FFoundationModelQuad& Other = Quads[Earlier];
        bool bSame = Other.Texture == Quad.Texture && Other.TintIndex == Quad.TintIndex &&
            Other.CullFace == Quad.CullFace && Other.Shade == Quad.Shade && Other.Light == Quad.Light;
        for (int32 Vertex = 0; bSame && Vertex < 4; ++Vertex)
            bSame = Other.Positions[Vertex] == Quad.Positions[Vertex] && Other.UVs[Vertex] == Quad.UVs[Vertex];
        if (bSame) return true;
    }
    return false;
}

FVector3f FoundationBlockMeshes::GetOverlayLift(TConstArrayView<FFoundationModelQuad> Quads, int32 Index)
{
    const FFoundationModelQuad& Quad = Quads[Index];
    for (int32 Earlier = 0; Earlier < Index; ++Earlier)
    {
        const FFoundationModelQuad& Other = Quads[Earlier];
        if (Other.Positions[0] == Quad.Positions[0] && Other.Positions[1] == Quad.Positions[1] &&
            Other.Positions[2] == Quad.Positions[2] && Other.Positions[3] == Quad.Positions[3])
            return FVector3f(FoundationGrid::FaceNormal(Quad.Facing)) * OverlayLift;
    }
    return FVector3f::ZeroVector;
}

TArray<FFoundationModelQuad> FoundationBlockMeshes::GetDrawnModelQuads(
    const FFoundationBlockRegistry& Registry, int32 Model)
{
    static const FName TorchTexture(TEXT("torch"));
    const UFoundationBlockAssets* Assets = Registry.GetAssets();
    const int32 Torch = Assets ? Assets->Textures.IndexOfByPredicate([](const FFoundationBlockTextureAsset& Texture)
                                     { return Texture.Name == TorchTexture; })
                               : INDEX_NONE;
    const TConstArrayView<FFoundationModelQuad> Baked = Registry.GetModelQuads(Model);
    TArray<FFoundationModelQuad> Quads;
    Quads.Reserve(Baked.Num());
    for (const FFoundationModelQuad& Quad : Baked)
    {
        FFoundationModelQuad Flame, Stick;
        if (Torch == INDEX_NONE || Quad.Texture != Torch)
            Quads.Add(Quad);
        else if (CutAtTorchFlame(Quad, Flame, Stick))
        {
            Quads.Add(Flame);
            Quads.Add(Stick);
        }
        else
        {
            // Uncut: a quad wholly above the line (the top face) is flame.
            FFoundationModelQuad& Whole = Quads.Add_GetRef(Quad);
            bool bAbove = true;
            for (const FVector2f& UV : Quad.UVs)
                bAbove &= UV.Y <= TorchFlameV;
            if (bAbove) Whole.Light = FMath::Max(Whole.Light, TorchFlameLight);
        }
    }
    return Quads;
}

int32 FoundationBlockMeshes::GetDrawnLight(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
{
    return Registry.GetBlock(State).bTorchRule ? FVoxelLights::ReadTorchGlow() : Registry.GetLightEmission(State);
}

UStaticMesh* FoundationBlockMeshes::BuildModel(UObject* Outer, TConstArrayView<FFoundationModelQuad> Quads)
{
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

    int32 Drawn = 0;
    for (int32 Index = 0; Index < Quads.Num(); ++Index)
    {
        if (IsRepeatedQuad(Quads, Index)) continue;
        const FFoundationModelQuad& Quad = Quads[Index];
        const FVector3f Normal(FoundationGrid::FaceNormal(Quad.Shade));
        const FVector3f Tangent = GetBlockMeshTangent(Quad.Shade);
        const FVector2f Code(float(int32(Quad.Shade)), float(MakeModelCode(Quad)));
        const FVector3f Lift = GetOverlayLift(Quads, Index);
        // The baked quads are wound like the cube faces already (Unnamed.Voxel.Mesher.ModelData checks every one).
        TArray<FVertexInstanceID, TFixedAllocator<4>> Instances;
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            const FVertexID Vertex = Mesh.CreateVertex();
            Positions[Vertex] = (Quad.Positions[Corner] + Lift) * float(UFoundationProfile::BlockCm);
            const FVertexInstanceID Instance = Mesh.CreateVertexInstance(Vertex);
            Normals[Instance] = Normal;
            Tangents[Instance] = Tangent;
            Signs[Instance] = 1.f;
            UVs.Set(Instance, 0, Quad.UVs[Corner]);
            UVs.Set(Instance, 1, Code);
            Instances.Add(Instance);
        }
        Mesh.CreatePolygon(Group, Instances);
        ++Drawn;
    }
    return Drawn ? FinishBlockMesh(Outer, Mesh, true) : nullptr;
}
