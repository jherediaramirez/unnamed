#include "Foundation/FoundationMobView.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationMobAssets.h"
#include "Foundation/FoundationPawn.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UnnamedLog.h"

namespace
{
    constexpr double MobPixelCm = 6.25, MobFeetPixels = 24;

    // ModelPart's rotation, Rz(z) Ry(y) Rx(x) on column vectors in Mojang's right-handed axes, carried into the mob's
    // frame (C R C^T) as an Unreal rotation.
    FQuat MobToUnrealRotation(const FVector& Radians)
    {
        const double Cx = FMath::Cos(Radians.X), Sx = FMath::Sin(Radians.X);
        const double Cy = FMath::Cos(Radians.Y), Sy = FMath::Sin(Radians.Y);
        const double Cz = FMath::Cos(Radians.Z), Sz = FMath::Sin(Radians.Z);
        // Column-vector matrices, R[row][column].
        const double Rx[3][3] = {{1, 0, 0}, {0, Cx, -Sx}, {0, Sx, Cx}};
        const double Ry[3][3] = {{Cy, 0, Sy}, {0, 1, 0}, {-Sy, 0, Cy}};
        const double Rz[3][3] = {{Cz, -Sz, 0}, {Sz, Cz, 0}, {0, 0, 1}};
        const auto Multiply = [](const double A[3][3], const double B[3][3], double Out[3][3])
        {
            for (int32 Row = 0; Row < 3; ++Row)
                for (int32 Column = 0; Column < 3; ++Column)
                    Out[Row][Column] = A[Row][0] * B[0][Column] + A[Row][1] * B[1][Column] + A[Row][2] * B[2][Column];
        };
        double Zy[3][3], R[3][3];
        Multiply(Rz, Ry, Zy);
        Multiply(Zy, Rx, R);
        // C maps Mojang to the mob's frame: X = -z, Y = -x, Z = -y.
        const double C[3][3] = {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}};
        double CR[3][3], U[3][3];
        Multiply(C, R, CR);
        const double CT[3][3] = {{0, -1, 0}, {0, 0, -1}, {-1, 0, 0}};
        Multiply(CR, CT, U);
        // FMatrix rows are the images of the basis vectors: U's columns.
        const FMatrix Basis(FVector(U[0][0], U[1][0], U[2][0]), FVector(U[0][1], U[1][1], U[2][1]),
            FVector(U[0][2], U[1][2], U[2][2]), FVector::ZeroVector);
        return Basis.ToQuat();
    }

    FName MobParentOf(FName Path)
    {
        const FString Text = Path.ToString();
        int32 Slash = INDEX_NONE;
        return Text.FindLastChar(TEXT('/'), Slash) ? FName(Text.Left(Slash)) : (Path.IsNone() ? NAME_None : FName());
    }

    UStaticMesh* BuildMobPartMesh(UObject* Outer, const FFoundationMobPartAsset& Part)
    {
        TArray<FVector> Corners;
        TArray<FVector2D> CornerUVs;
        FFoundationMobView::GetPartQuads(Part, Corners, CornerUVs);
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
        Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Mob");
        for (int32 Face = 0; Face < Part.Faces.Num(); ++Face)
        {
            const FVector3f Normal = FVector3f(FFoundationMobView::ToUnreal(Part.Faces[Face].Normal, false));
            const FVector3f Tangent = FVector3f(Corners[Face * 4 + 1] - Corners[Face * 4]).GetSafeNormal();
            TArray<FVertexInstanceID, TFixedAllocator<4>> Instances;
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                const FVertexID Vertex = Mesh.CreateVertex();
                Positions[Vertex] = FVector3f(Corners[Face * 4 + Corner]);
                const FVertexInstanceID Instance = Mesh.CreateVertexInstance(Vertex);
                Normals[Instance] = Normal;
                Tangents[Instance] = Tangent;
                Signs[Instance] = 1.f;
                UVs.Set(Instance, 0, FVector2f(CornerUVs[Face * 4 + Corner]));
                Instances.Add(Instance);
            }
            Mesh.CreatePolygon(Group, Instances);
        }
        UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
#if WITH_EDITORONLY_DATA
        StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Mob"), TEXT("Mob")));
#else
        StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Mob")));
#endif
        UStaticMesh::FBuildMeshDescriptionsParams Params;
        Params.bFastBuild = true;
        Params.bBuildSimpleCollision = false;
        // UVs sit on texel edges of a 64-pixel texture; full precision keeps them exact.
        UStaticMesh::FBuildMeshDescriptionsLODParams LOD;
        LOD.bUseFullPrecisionUVs = true;
        Params.PerLODOverrides.Add(LOD);
        StaticMesh->BuildFromMeshDescriptions({&Mesh}, Params);
        return StaticMesh;
    }

    // foundation.Mobs.Summon <type> and foundation.Mobs.Clear act on the local player's authority.
    FFoundationAuthority* FindMobAuthority(UWorld* World, AFoundationPawn*& OutPawn)
    {
        OutPawn = nullptr;
        if (!World) return nullptr;
        for (TActorIterator<AFoundationPawn> It(World); It; ++It)
            if (It->IsLocallyControlled() && It->GetAuthority())
            {
                OutPawn = *It;
                return It->GetAuthority();
            }
        return nullptr;
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice MobSummonCommand(TEXT("foundation.Mobs.Summon"),
        TEXT("foundation.Mobs.Summon [type]: a mob (cow, the default, pig, sheep, chicken) 3 blocks ahead of the "
             "player, facing it."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
            {
                AFoundationPawn* Pawn = nullptr;
                FFoundationAuthority* Authority = FindMobAuthority(World, Pawn);
                EFoundationMobType Type = EFoundationMobType::Cow;
                if (!Authority)
                    Out.Log(TEXT("foundation.Mobs.Summon: no local player"));
                else if (!Args.IsEmpty() && !FoundationMobs::FindType(FName(Args[0]), Type))
                    Out.Logf(TEXT("foundation.Mobs.Summon: unknown mob %s"), *Args[0]);
                else if (!FoundationMobs::GetKind(Type).bSpawnable)
                    Out.Logf(TEXT("foundation.Mobs.Summon: %s is not spawnable yet (20c-20d)"),
                        *FoundationMobs::GetKind(Type).Id.ToString());
                else
                {
                    Authority->Options.bMobs = FoundationMobs::IsOn();
                    const float Yaw = float(Pawn->GetControlRotation().Yaw);
                    const FVector Ahead = FRotator(0, Yaw, 0).Vector() * 300;
                    const int32 Id =
                        Authority->SpawnMob(Type, Pawn->Simulation.Feet + Ahead - Authority->World.OriginCm, Yaw + 180);
                    if (Id == 0)
                        Out.Log(TEXT("foundation.Mobs.Summon: foundation.Mobs is 0"));
                    else
                        Out.Logf(
                            TEXT("foundation.Mobs.Summon: %s %d"), *FoundationMobs::GetKind(Type).Id.ToString(), Id);
                }
            }));

    FAutoConsoleCommandWithWorldArgsAndOutputDevice MobClearCommand(TEXT("foundation.Mobs.Clear"),
        TEXT("foundation.Mobs.Clear: removes every mob, without loot."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>&, UWorld* World, FOutputDevice& Out)
            {
                AFoundationPawn* Pawn = nullptr;
                if (FFoundationAuthority* Authority = FindMobAuthority(World, Pawn))
                    Out.Logf(TEXT("foundation.Mobs.Clear: %d removed"), Authority->ClearMobs());
            }));
}

FVector FFoundationMobView::ToUnreal(const FVector& Pixels, bool bPoint)
{
    return FVector(-Pixels.Z, -Pixels.X, (bPoint ? MobFeetPixels : 0) - Pixels.Y) * (bPoint ? MobPixelCm : 1);
}

FTransform FFoundationMobView::MakePartTransform(
    const FFoundationMobPartAsset& Part, const FVector& ExtraRotation, const FVector& OffsetPixels)
{
    return FTransform(
        MobToUnrealRotation(Part.Rotation + ExtraRotation), ToUnreal(Part.Pivot + OffsetPixels, false) * MobPixelCm);
}

void FFoundationMobView::GetPartQuads(
    const FFoundationMobPartAsset& Part, TArray<FVector>& OutPositions, TArray<FVector2D>& OutUVs)
{
    OutPositions.Reset();
    OutUVs.Reset();
    for (const FFoundationMobFaceAsset& Face : Part.Faces)
    {
        if (Face.Positions.Num() != 4 || Face.UVs.Num() != 4) continue;
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            OutPositions.Add(ToUnreal(Face.Positions[Corner], false) * MobPixelCm);
            OutUVs.Add(Face.UVs[Corner]);
        }
    }
}

FTransform FFoundationMobView::MakeRootTransform(const FFoundationMob& Mob, const FVector& OriginCm, double Alpha)
{
    const FVector Feet = OriginCm + FMath::Lerp(Mob.PreviousLocalCm, Mob.LocalCm, Alpha);
    const float Yaw = Mob.PreviousYaw + FMath::FindDeltaAngleDegrees(Mob.PreviousYaw, Mob.Yaw) * float(Alpha);
    FQuat Rotation = FRotator(0, Yaw, 0).Quaternion();
    // [survival.r10.death_roll] about the mob's own lengthwise axis through its feet, at the fractional death time.
    if (Mob.DeathTime > 0)
    {
        const float Roll = FoundationMobs::GetDeathRoll(
            float(Mob.DeathTime) + float(Alpha), FoundationMobs::GetKind(Mob.Type).DeathRollDegrees);
        Rotation = Rotation * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Roll));
    }
    // The model's feet stand at its y 24: the root sits that high in the mob's frame.
    return FTransform(FTransform(FVector(0, 0, MobFeetPixels * MobPixelCm)).ToMatrixNoScale() *
        FTransform(Rotation, Feet).ToMatrixNoScale());
}

FVector FFoundationMobView::GetPartAnimation(
    const FFoundationMob& Mob, FName Part, double Alpha, FVector* OutOffsetPixels)
{
    using namespace FoundationMobs;
    static const FName Legs[] = {
        TEXT("right_hind_leg"), TEXT("left_hind_leg"), TEXT("right_front_leg"), TEXT("left_front_leg")};
    static const FName BipedLegs[] = {TEXT("right_leg"), TEXT("left_leg")};
    if (OutOffsetPixels) *OutOffsetPixels = FVector::ZeroVector;
    const float Position = FMath::Lerp(Mob.PreviousWalkPosition, Mob.WalkPosition, float(Alpha));
    const float Speed = FMath::Min(1.f, FMath::Lerp(Mob.PreviousWalkSpeed, Mob.WalkSpeed, float(Alpha)));
    for (int32 Leg = 0; Leg < UE_ARRAY_COUNT(Legs); ++Leg)
        if (Part == Legs[Leg]) return FVector(GetLegPitch(Leg, Position, Speed), 0, 0);
    for (int32 Leg = 0; Leg < UE_ARRAY_COUNT(BipedLegs); ++Leg)
        if (Part == BipedLegs[Leg]) return FVector(GetBipedLegPitch(Leg, Position, Speed), 0, 0);
    if (Part == TEXT("right_wing") || Part == TEXT("left_wing"))
    {
        const float Roll = GetWingRoll(FMath::Lerp(Mob.PreviousFlap, Mob.Flap, float(Alpha)),
            FMath::Lerp(Mob.PreviousFlapSpeed, Mob.FlapSpeed, float(Alpha)));
        return FVector(0, 0, Part == TEXT("right_wing") ? Roll : -Roll);
    }
    if (Part == TEXT("head"))
    {
        const float HeadYaw =
            Mob.PreviousHeadYaw + FMath::FindDeltaAngleDegrees(Mob.PreviousHeadYaw, Mob.HeadYaw) * float(Alpha);
        const float BodyYaw = Mob.PreviousYaw + FMath::FindDeltaAngleDegrees(Mob.PreviousYaw, Mob.Yaw) * float(Alpha);
        float Pitch = FMath::DegreesToRadians(FMath::Lerp(Mob.PreviousHeadPitch, Mob.HeadPitch, float(Alpha)));
        // A grazing sheep's head drops and nods (its wool's head with it).
        if (Mob.GrazeTicks > 0)
        {
            const float Ticks = float(Mob.GrazeTicks) - float(Alpha);
            Pitch = GetGrazePitch(Ticks, Pitch);
            if (OutOffsetPixels) *OutOffsetPixels = FVector(0, GetGrazeDrop(Ticks) * GrazeDropPixels, 0);
        }
        return FVector(Pitch, FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(BodyYaw, HeadYaw)), 0);
    }
    return FVector::ZeroVector;
}

bool FFoundationMobView::Prepare(EFoundationMobType Type)
{
    FTypeDraw& Draw = Types[int32(Type)];
    if (Draw.bTried) return !Draw.Layers.IsEmpty();
    Draw.bTried = true;
    AActor* Owner = View.GetOwner();
    const UFoundationMobAssets* Assets = UFoundationMobAssets::LoadDefault();
    if (!Owner || !Owner->GetRootComponent() || !Assets) return false;
    const FName Id = FoundationMobs::GetKind(Type).Id;
    TArray<const FFoundationMobModelAsset*> Models;
    Assets->FindLayers(Id, Models);
    UMaterialInterface* Material = Assets->Material.LoadSynchronous();
    TArray<FTransform> Hidden;
    Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), MaxMobs);
    for (const FFoundationMobModelAsset* Model : Models)
    {
        UTexture2D* Texture = Model->Texture.LoadSynchronous();
        if (!Material || !Texture)
        {
            UE_LOG(LogUnnamedFoundation, Warning,
                TEXT("Mobs: DA_Mobs has no material or texture for %s; it and the "
                     "layers after it are not drawn (run ImportMobs.py)"),
                *Model->Layer.ToString());
            break;
        }
        UMaterialInstanceDynamic* Skin = UMaterialInstanceDynamic::Create(Material, Owner);
        Skin->SetTextureParameterValue(TEXT("Skin"), Texture);
        View.Owned.Add(Skin);
        FLayerDraw& Layer = Draw.Layers.AddDefaulted_GetRef();
        Layer.Model = Model;
        for (const FFoundationMobPartAsset& Part : Model->Parts)
        {
            FPartDraw& PartDraw = Layer.Parts.AddDefaulted_GetRef();
            UStaticMesh* Mesh = BuildMobPartMesh(Owner, Part);
            if (!Mesh) continue;
            View.Owned.Add(Mesh);
            UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Owner);
            Component->SetStaticMesh(Mesh);
            Component->SetMaterial(0, Skin);
            Component->SetNumCustomDataFloats(1);
            Component->SetMobility(EComponentMobility::Movable);
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetCanEverAffectNavigation(false);
            Component->SetVisibility(false);
            Component->SetupAttachment(Owner->GetRootComponent());
            Component->RegisterComponent();
            View.Owned.Add(Component);
            PartDraw.Component = Component;
            PartDraw.Ids = Component->AddInstancesById(Hidden, false, false);
        }
    }
    if (Draw.Layers.IsEmpty())
        UE_LOG(LogUnnamedFoundation, Warning,
            TEXT("Mobs: DA_Mobs has no model, texture or material for %s; it is not "
                 "drawn (run ImportMobs.py)"),
            *Id.ToString());
    return !Draw.Layers.IsEmpty();
}

void FFoundationMobView::Update(const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, double Alpha)
{
    const TArray<FFoundationMob>& Mobs = Authority.GetMobs();
    Drawn = 0;
    Overlays.Reset();
    const bool bOn = Authority.Options.bMobs;
    const FVector Origin = Authority.World.OriginCm;
    const FVector Viewer = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
    for (int32 TypeIndex = 0; TypeIndex < int32(EFoundationMobType::Count); ++TypeIndex)
    {
        FTypeDraw& Draw = Types[TypeIndex];
        const bool bAny = bOn &&
            Mobs.ContainsByPredicate([TypeIndex](const FFoundationMob& Mob) { return int32(Mob.Type) == TypeIndex; });
        if (!bAny && !Draw.bTried) continue;
        if (bAny && !Prepare(EFoundationMobType(TypeIndex))) continue;
        // The nearest MaxMobs of the type within DrawCm fill the slots (Codex review 19 #2).
        TArray<TPair<double, int32>, TInlineAllocator<MaxMobs>> Order;
        for (int32 Index = 0; bOn && Index < Mobs.Num(); ++Index)
        {
            const FFoundationMob& Mob = Mobs[Index];
            if (int32(Mob.Type) != TypeIndex || Mob.bFrozen) continue;
            const double Distance = Pawn ? FVector::DistSquared(Origin + Mob.LocalCm, Viewer) : 0;
            if (Distance <= DrawCm * DrawCm) Order.Emplace(Distance, Index);
        }
        if (Order.Num() > MaxMobs)
        {
            Order.StableSort(
                [](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
            Order.SetNum(MaxMobs);
        }
        int32 Slot = 0;
        for (const TPair<double, int32>& Entry : Order)
        {
            const FFoundationMob& Mob = Mobs[Entry.Value];
            const FTransform Root = MakeRootTransform(Mob, Origin, Alpha);
            // [hurt.overlay, mobs.hurt_overlay] red while hurt or dying.
            const float Overlay = Mob.HurtTime > 0 || Mob.DeathTime > 0 ? 1.f : 0.f;
            Overlays.Add(Overlay);
            for (FLayerDraw& Layer : Draw.Layers)
            {
                TMap<FName, FTransform> Placed;
                for (int32 PartIndex = 0; PartIndex < Layer.Model->Parts.Num(); ++PartIndex)
                {
                    const FFoundationMobPartAsset& Part = Layer.Model->Parts[PartIndex];
                    const FTransform* Parent = Part.Path.IsNone() ? nullptr : Placed.Find(MobParentOf(Part.Path));
                    FVector Offset;
                    const FVector Rotation = GetPartAnimation(Mob, Part.Path, Alpha, &Offset);
                    const FTransform World = MakePartTransform(Part, Rotation, Offset) * (Parent ? *Parent : Root);
                    Placed.Add(Part.Path, World);
                    FPartDraw& PartDraw = Layer.Parts[PartIndex];
                    if (!PartDraw.Component) continue;
                    PartDraw.Component->SetCustomDataValueById(PartDraw.Ids[Slot], 0, Overlay);
                    PartDraw.Component->UpdateInstanceTransformById(PartDraw.Ids[Slot], World, true);
                }
            }
            ++Slot;
            ++Drawn;
        }
        for (FLayerDraw& Layer : Draw.Layers)
            for (FPartDraw& PartDraw : Layer.Parts)
            {
                if (!PartDraw.Component) continue;
                for (int32 Hide = Slot; Hide < PartDraw.Shown; ++Hide)
                    PartDraw.Component->UpdateInstanceTransformById(
                        PartDraw.Ids[Hide], FTransform(FQuat::Identity, Viewer, FVector::ZeroVector), true);
                PartDraw.Shown = Slot;
                PartDraw.Component->SetVisibility(Slot > 0);
            }
    }
}

void FFoundationMobView::OnSound(const FFoundationEvent& Event)
{
    ++Sounds;
    if (FFoundationBreakEffects* Effects = View.GetBreakEffects())
    {
        // [survival.r5.play_calls] steps at pitch 1 (F 3582); ambient, hurt and death at a random pitch.
        const bool bStep = Event.Interaction.ToString().EndsWith(TEXT(".step"));
        Effects->PlayEvent(Event.Interaction, Event.LocationCm, Event.Amount,
            bStep ? FoundationMobs::StepPitch : FoundationMobs::GetSoundPitch(FMath::FRand(), FMath::FRand()));
    }
}

FString FFoundationMobView::Describe() const
{
    FString Text = FString::Printf(TEXT("mobs drawn %d, sounds %d"), Drawn, Sounds);
    for (int32 TypeIndex = 0; TypeIndex < int32(EFoundationMobType::Count); ++TypeIndex)
        if (Types[TypeIndex].bTried)
        {
            int32 Parts = 0;
            for (const FLayerDraw& Layer : Types[TypeIndex].Layers)
                Parts += Layer.Parts.Num();
            Text += FString::Printf(TEXT("; %s layers %d parts %d"),
                *FoundationMobs::GetKind(EFoundationMobType(TypeIndex)).Id.ToString(), Types[TypeIndex].Layers.Num(),
                Parts);
        }
    return Text;
}
