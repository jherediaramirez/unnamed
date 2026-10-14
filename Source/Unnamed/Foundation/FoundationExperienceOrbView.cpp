#include "Foundation/FoundationExperienceOrbView.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationPawn.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"

bool FFoundationExperienceOrbView::Prepare()
{
    if (bTried) return Quads != nullptr;
    bTried = true;
    AActor* Owner = View.GetOwner();
    if (!Owner || !Owner->GetRootComponent()) return false;
    const FString Path = UUnnamedProjectSettings::GetEntityAssetPath(TEXT("M_ExperienceOrb")).ToString();
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path, {}, LOAD_NoWarn | LOAD_Quiet);
    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!Material || !Plane)
    {
        UE_LOG(LogUnnamedFoundation, Warning,
            TEXT("Experience orbs: M_ExperienceOrb missing under %s (run CreateEntityAssets.py); orbs are not drawn"),
            *UUnnamedProjectSettings::Get().EntityRoot);
        return false;
    }
    Quads = NewObject<UInstancedStaticMeshComponent>(Owner);
    Quads->SetStaticMesh(Plane);
    Quads->SetMaterial(0, Material);
    Quads->SetNumCustomDataFloats(DataFloats);
    Quads->SetMobility(EComponentMobility::Movable);
    Quads->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Quads->SetCanEverAffectNavigation(false);
    Quads->SetCastShadow(false);
    Quads->SetVisibility(false);
    Quads->SetupAttachment(Owner->GetRootComponent());
    Quads->RegisterComponent();
    View.Owned.Add(Quads);
    TArray<FTransform> Hidden;
    Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), MaxOrbs);
    Ids = Quads->AddInstancesById(Hidden, false, false);
    return true;
}

void FFoundationExperienceOrbView::Update(
    const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, double Alpha)
{
    using namespace FoundationExperienceOrbs;
    // The level-up hook: the local player's level reaching or passing a multiple of 5 (provisional, wiki
    // "Experience").
    if (Pawn)
    {
        const int32 Level = Pawn->Vitals.Level;
        if (LastLevel >= 0 && Level > LastLevel && Level / 5 > LastLevel / 5)
        {
            ++LevelUpSounds;
            if (FFoundationBreakEffects* Effects = View.GetBreakEffects())
                Effects->PlayEvent(TEXT("entity.player.levelup"), Pawn->GetActorLocation(), .75f, 1.f);
        }
        LastLevel = Level;
    }
    const TArray<FFoundationExperienceOrb>& Orbs = Authority.GetExperienceOrbs();
    Drawn = 0;
    Data.Reset();
    if (Orbs.IsEmpty() && Shown == 0) return;
    if (!Prepare()) return;
    const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    const APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
    const FVector Viewer = Camera ? Camera->GetCameraLocation() : Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
    const FRotator CameraRotation = Camera ? Camera->GetCameraRotation() : FRotator::ZeroRotator;
    // Camera-facing quads: the plane's +Z normal toward the camera, its X up the screen.
    const FQuat Facing =
        FRotationMatrix::MakeFromZX(-CameraRotation.Vector(), FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Z))
            .ToQuat();
    const FVector Origin = Authority.World.OriginCm;
    TArray<TPair<double, int32>> Wanted;
    for (int32 Index = 0; Index < Orbs.Num(); ++Index)
    {
        const double Distance = FVector::DistSquared(Origin + Orbs[Index].LocalCm, Viewer);
        if (!Orbs[Index].bFrozen && (!Pawn || Distance <= DrawCm * DrawCm)) Wanted.Add({Distance, Index});
    }
    if (Wanted.Num() > MaxOrbs)
    {
        Wanted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
        Wanted.SetNum(MaxOrbs);
    }
    for (int32 Slot = 0; Slot < Wanted.Num(); ++Slot)
    {
        const FFoundationExperienceOrb& Orb = Orbs[Wanted[Slot].Value];
        const FVector At = Origin + FMath::Lerp(Orb.PreviousLocalCm, Orb.LocalCm, Alpha) + FVector(0, 0, QuadUpCm);
        const FColor Color = GetColor(Orb.Age - 1 + Alpha);
        const float Values[DataFloats] = {float(GetIcon(Orb.Value)), float(Color.R), float(Color.B)};
        Data.Append(Values, DataFloats);
        Quads->SetCustomDataById(Ids[Slot], Values);
        Quads->UpdateInstanceTransformById(Ids[Slot], FTransform(Facing, At, FVector(QuadCm / 100)), true);
    }
    // Quads no longer wanted shrink to nothing.
    for (int32 Slot = Wanted.Num(); Slot < Shown; ++Slot)
        Quads->UpdateInstanceTransformById(Ids[Slot], FTransform(FQuat::Identity, Viewer, FVector::ZeroVector), true);
    Shown = Drawn = Wanted.Num();
    Quads->SetVisibility(Shown > 0);
}

void FFoundationExperienceOrbView::OnPickedUp(const FFoundationEvent& Event)
{
    // Provisional volume and pitch (the item pickup's call shape; the sound batch brings the event).
    ++PickupSounds;
    if (FFoundationBreakEffects* Effects = View.GetBreakEffects())
        Effects->PlayEvent(TEXT("entity.experience_orb.pickup"), Event.LocationCm, .1f, FMath::FRandRange(.55f, 1.25f));
}

FString FFoundationExperienceOrbView::Describe() const
{
    if (!Quads) return TEXT("no quads");
    FTransform First;
    const bool bFirst = Quads->GetInstanceCount() > 0 && Quads->GetInstanceTransform(0, First, true);
    return FString::Printf(TEXT("registered %d visible %d instances %d bounds %s material %s first %s"),
        Quads->IsRegistered(), Quads->IsVisible(), Quads->GetInstanceCount(), *Quads->Bounds.GetBox().ToString(),
        *GetNameSafe(Quads->GetMaterial(0)), bFirst ? *First.ToString() : TEXT("none"));
}
