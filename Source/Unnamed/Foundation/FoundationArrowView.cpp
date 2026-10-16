#include "Foundation/FoundationArrowView.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationMobAssets.h"
#include "Foundation/FoundationMobView.h"
#include "Foundation/FoundationPawn.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"

namespace
{
    // The model's length runs along Mojang's x, its nose at +x (the back's fletching plane sits at x -9.9), which the
    // mobs' axes turn to Unreal's -Y: a quarter turn brings the nose to +X.
    const FQuat NoseToX(FVector::UpVector, HALF_PI);

    UStaticMesh* BuildArrowMesh(UObject* Outer, const FFoundationMobModelAsset& Model)
    {
        TArray<FVector> Corners;
        TArray<FVector2D> CornerUVs;
        FFoundationArrowView::GetModelQuads(Model, Corners, CornerUVs);
        if (Corners.IsEmpty()) return nullptr;
        FMeshDescription Mesh;
        FStaticMeshAttributes Attributes(Mesh);
        Attributes.Register();
        TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
        TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
        TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
        TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
        TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
        UVs.SetNumChannels(1);
        const FPolygonGroupID Group = Mesh.CreatePolygonGroup();
        Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Arrow");
        for (int32 Face = 0; Face + 3 < Corners.Num(); Face += 4)
        {
            const FVector3f Edge1 = FVector3f(Corners[Face + 1] - Corners[Face]);
            const FVector3f Edge2 = FVector3f(Corners[Face + 3] - Corners[Face]);
            const FVector3f Normal = FVector3f::CrossProduct(Edge2, Edge1).GetSafeNormal();
            TArray<FVertexInstanceID, TFixedAllocator<4>> Instances;
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                const FVertexID Vertex = Mesh.CreateVertex();
                Positions[Vertex] = FVector3f(Corners[Face + Corner]);
                const FVertexInstanceID Instance = Mesh.CreateVertexInstance(Vertex);
                Normals[Instance] = Normal;
                Tangents[Instance] = Edge1.GetSafeNormal();
                Signs[Instance] = 1.f;
                UVs.Set(Instance, 0, FVector2f(CornerUVs[Face + Corner]));
                Instances.Add(Instance);
            }
            Mesh.CreatePolygon(Group, Instances);
        }
        UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
#if WITH_EDITORONLY_DATA
        StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Arrow"), TEXT("Arrow")));
#else
        StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Arrow")));
#endif
        UStaticMesh::FBuildMeshDescriptionsParams Params;
        Params.bFastBuild = true;
        Params.bBuildSimpleCollision = false;
        UStaticMesh::FBuildMeshDescriptionsLODParams LOD;
        LOD.bUseFullPrecisionUVs = true;
        Params.PerLODOverrides.Add(LOD);
        StaticMesh->BuildFromMeshDescriptions({&Mesh}, Params);
        return StaticMesh;
    }
}

void FFoundationArrowView::GetModelQuads(
    const FFoundationMobModelAsset& Model, TArray<FVector>& OutPositions, TArray<FVector2D>& OutUVs)
{
    OutPositions.Reset();
    OutUVs.Reset();
    TArray<FVector> Corners;
    TArray<FVector2D> CornerUVs;
    for (const FFoundationMobPartAsset& Part : Model.Parts)
    {
        // The parts hang off the root (no deeper nesting: ImportProjectiles.py refuses it).
        const FTransform Pose = FFoundationMobView::MakePartTransform(Part, FVector::ZeroVector);
        FFoundationMobView::GetPartQuads(Part, Corners, CornerUVs);
        for (int32 Corner = 0; Corner < Corners.Num(); ++Corner)
        {
            OutPositions.Add(NoseToX.RotateVector(Pose.TransformPosition(Corners[Corner])));
            OutUVs.Add(CornerUVs[Corner]);
        }
    }
}

bool FFoundationArrowView::Prepare()
{
    if (bTried) return Arrows != nullptr;
    AActor* Owner = View.GetOwner();
    if (!Owner || !Owner->GetRootComponent()) return false;
    bTried = true;
    const FString Path = UUnnamedProjectSettings::GetEntityAssetPath(TEXT("DA_Projectiles")).ToString();
    const UFoundationMobAssets* Assets = LoadObject<UFoundationMobAssets>(nullptr, *Path, {}, LOAD_NoWarn | LOAD_Quiet);
    const FFoundationMobModelAsset* Model = Assets
        ? Assets->Models.FindByPredicate(
              [](const FFoundationMobModelAsset& M) { return M.Layer == TEXT("arrow#main"); })
        : nullptr;
    UMaterialInterface* Material = Assets ? Assets->Material.LoadSynchronous() : nullptr;
    UTexture2D* Texture = Model ? Model->Texture.LoadSynchronous() : nullptr;
    UStaticMesh* Mesh = Material && Texture ? BuildArrowMesh(Owner, *Model) : nullptr;
    if (!Mesh)
    {
        UE_LOG(LogUnnamedFoundation, Warning,
            TEXT("Arrows: DA_Projectiles (its arrow#main model, texture or material) missing under %s (run "
                 "ImportProjectiles.py); arrows are not drawn"),
            *UUnnamedProjectSettings::Get().EntityRoot);
        return false;
    }
    View.Owned.Add(Mesh);
    UMaterialInstanceDynamic* Skin = UMaterialInstanceDynamic::Create(Material, Owner);
    Skin->SetTextureParameterValue(TEXT("Skin"), Texture);
    View.Owned.Add(Skin);
    Arrows = NewObject<UInstancedStaticMeshComponent>(Owner);
    Arrows->SetStaticMesh(Mesh);
    Arrows->SetMaterial(0, Skin);
    // M_Mob's custom data 0 is the hurt overlay: none.
    Arrows->SetNumCustomDataFloats(1);
    Arrows->SetMobility(EComponentMobility::Movable);
    Arrows->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Arrows->SetCanEverAffectNavigation(false);
    Arrows->SetVisibility(false);
    Arrows->SetupAttachment(Owner->GetRootComponent());
    Arrows->RegisterComponent();
    View.Owned.Add(Arrows);
    TArray<FTransform> Hidden;
    Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), MaxArrows);
    Ids = Arrows->AddInstancesById(Hidden, false, false);
    return true;
}

void FFoundationArrowView::Update(const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, double Alpha)
{
    const TArray<FFoundationArrow>& All = Authority.GetArrows();
    Drawn = 0;
    if (All.IsEmpty() && Shown == 0) return;
    if (!Prepare()) return;
    const FVector Origin = Authority.World.OriginCm;
    const FVector Viewer = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
    TArray<TPair<double, int32>> Wanted;
    for (int32 Index = 0; Index < All.Num(); ++Index)
    {
        const double Distance = FVector::DistSquared(Origin + All[Index].LocalCm, Viewer);
        if (!Pawn || Distance <= DrawCm * DrawCm) Wanted.Add({Distance, Index});
    }
    if (Wanted.Num() > MaxArrows)
    {
        Wanted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
        Wanted.SetNum(MaxArrows);
    }
    for (int32 Slot = 0; Slot < Wanted.Num(); ++Slot)
    {
        const FFoundationArrow& Arrow = All[Wanted[Slot].Value];
        const FVector At = Origin + FMath::Lerp(Arrow.PreviousLocalCm, Arrow.LocalCm, Alpha);
        Arrows->UpdateInstanceTransformById(Ids[Slot], FTransform(FRotator(Arrow.Pitch, Arrow.Yaw, 0), At), true);
    }
    for (int32 Slot = Wanted.Num(); Slot < Shown; ++Slot)
        Arrows->UpdateInstanceTransformById(Ids[Slot], FTransform(FQuat::Identity, Viewer, FVector::ZeroVector), true);
    Shown = Drawn = Wanted.Num();
    Arrows->SetVisibility(Shown > 0);
}
