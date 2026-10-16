// AFoundationPawn's player body: the world body every view sees and, for the Unnamed Player, the owner's first-person
// copy (Docs/Foundation/Architecture.md, "Player body and the third-person views"; PLAN.md section 3).
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationBodyAnimInstance.h"
#include "Foundation/FoundationProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UnnamedLog.h"

namespace
{
    TAutoConsoleVariable<int32> CVarBody(TEXT("foundation.Body"), 1,
        TEXT("The player body: 0 none (the hitbox outline as before), 1 shown (foundation.Body.Set says which and "
             "where)."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarBodySet(TEXT("foundation.Body.Set"), 1,
        TEXT("Which body: 0 Better Steve, in F5's back and front views only (hidden in first person); 1 the Unnamed "
             "Player, in every view, with a first-person copy (foundation.Body.FirstPerson). Falls back to 0 when the "
             "Unnamed Player's assets are missing."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarBodyFirstPerson(TEXT("foundation.Body.FirstPerson"), 1,
        TEXT("The Unnamed Player in first person: 1 its first-person copy (no head, legs and feet in view looking "
             "down) and the world body's shadow, head included; 0 no body in first person, as Better Steve."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarBodyYaw(TEXT("foundation.Body.Yaw"), 1,
        TEXT("Where the body faces: 0 always the view (sideways and backward travel play the strafe and backward "
             "clips), 1 vanilla (turns toward travel, backward travel keeps facing forward, the head at most 50 "
             "degrees off the body), 2 the travel direction, backward too (third-person orient-to-movement)."),
        ECVF_Default);
    // The first-person corrections' tuning (FFoundationFirstPersonTuning; proposed values, PLAN.md 3.4).
    TAutoConsoleVariable<float> CVarFpStartPitch(TEXT("foundation.Body.FP.StartPitch"), -20,
        TEXT("First-person body: the corrections start blending in at this pitch (degrees, negative = down)."),
        ECVF_Default);
    TAutoConsoleVariable<float> CVarFpEndPitch(TEXT("foundation.Body.FP.EndPitch"), -75,
        TEXT("First-person body: the corrections are full at this pitch (degrees)."), ECVF_Default);
    TAutoConsoleVariable<float> CVarFpPelvisBack(TEXT("foundation.Body.FP.PelvisBack"), 10,
        TEXT("First-person body: the hips move back this far at full blend, standing (cm)."), ECVF_Default);
    TAutoConsoleVariable<float> CVarFpPelvisBackCrouch(TEXT("foundation.Body.FP.PelvisBackCrouch"), 15,
        TEXT("First-person body: the hips move back this far at full blend, crouched (cm)."), ECVF_Default);
    TAutoConsoleVariable<float> CVarFpAnchor(TEXT("foundation.Body.FP.Anchor"), 16,
        TEXT("First-person body: neck_01 this far behind the eye at full blend, standing (cm; 4 at no blend)."),
        ECVF_Default);
    TAutoConsoleVariable<float> CVarFpAnchorCrouch(TEXT("foundation.Body.FP.AnchorCrouch"), 22,
        TEXT("First-person body: neck_01 this far behind the eye at full blend, crouched (cm; 10 at no blend)."),
        ECVF_Default);

    // Closer than this to the eyes the camera would sit inside the head: the world body is no longer drawn for the
    // owner (the Unnamed Player keeps it as the shadow's world-space representation).
    constexpr double BodyHideCameraCm = 40;
    const FName BodyHeadMaterial(TEXT("M_UP_Head"));

    // Draws a body component or not; while it is the body of the feature in use it is posed every frame, drawn or not,
    // so a view switch never shows a stale or reference pose.
    void ShowBody(USkeletalMeshComponent* Mesh, bool bShow, bool bAlwaysPose)
    {
        if (Mesh->bHiddenInGame == bShow) Mesh->SetHiddenInGame(!bShow);
        const EVisibilityBasedAnimTickOption Tick = bShow || bAlwaysPose
            ? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
            : EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
        if (Mesh->VisibilityBasedAnimTickOption != Tick) Mesh->VisibilityBasedAnimTickOption = Tick;
    }
}

EFoundationBodyYaw AFoundationPawn::GetBodyYawMode()
{
    return EFoundationBodyYaw(FMath::Clamp(CVarBodyYaw.GetValueOnGameThread(), 0, 2));
}

EFoundationBodySet AFoundationPawn::GetRequestedBodySet()
{
    return CVarBodySet.GetValueOnGameThread() == 0 ? EFoundationBodySet::BetterSteve
                                                   : EFoundationBodySet::UnnamedPlayer;
}

bool AFoundationPawn::IsFirstPersonBodyOn()
{
    return CVarBodyFirstPerson.GetValueOnGameThread() != 0;
}

FFoundationFirstPersonTuning AFoundationPawn::GetFirstPersonTuning()
{
    FFoundationFirstPersonTuning Tuning;
    Tuning.StartPitchDeg = CVarFpStartPitch.GetValueOnGameThread();
    Tuning.EndPitchDeg = CVarFpEndPitch.GetValueOnGameThread();
    Tuning.PelvisBackStandCm = CVarFpPelvisBack.GetValueOnGameThread();
    Tuning.PelvisBackCrouchCm = CVarFpPelvisBackCrouch.GetValueOnGameThread();
    Tuning.AnchorStandCm[1] = CVarFpAnchor.GetValueOnGameThread();
    Tuning.AnchorCrouchCm[1] = CVarFpAnchorCrouch.GetValueOnGameThread();
    return Tuning;
}

USkeletalMeshComponent* AFoundationPawn::MakeBodyComponent(USkeletalMesh* Mesh, FName Name)
{
    auto* Component = NewObject<USkeletalMeshComponent>(this, Name);
    Component->SetupAttachment(RootComponent);
    // Placed every frame at the rendered feet (the actor stands at the simulation's latest step).
    Component->SetUsingAbsoluteLocation(true);
    Component->SetUsingAbsoluteRotation(true);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetGenerateOverlapEvents(false);
    Component->SetSkeletalMeshAsset(Mesh);
    Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    Component->SetAnimInstanceClass(UFoundationBodyAnimInstance::StaticClass());
    Component->SetHiddenInGame(true);
    Component->SetTickGroup(PrimaryActorTick.TickGroup);
    Component->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
    Component->RegisterComponent();
    const auto* Anim = Cast<UFoundationBodyAnimInstance>(Component->GetAnimInstance());
    if (Anim && Anim->HasClips()) return Component;
    Component->DestroyComponent();
    return nullptr;
}

void AFoundationPawn::CreateBody()
{
    RequestedBodySet = GetRequestedBodySet();
    for (const EFoundationBodySet Set : {RequestedBodySet, EFoundationBodySet::BetterSteve})
    {
        const FSoftObjectPath MeshPath = FoundationBodyClip::GetAssetPath(Set, FoundationBodyClip::GetMeshName(Set));
        auto* Mesh = Cast<USkeletalMesh>(MeshPath.TryLoad());
        if (Mesh) BodyMesh = MakeBodyComponent(Mesh, TEXT("Body"));
        if (BodyMesh)
        {
            BodySet = Set;
            if (Set == EFoundationBodySet::UnnamedPlayer)
            {
                // The owner's first-person copy: seen only by the owner, no shadow (the world body casts it), in no
                // capture or reflection, its head off (the owner's camera is inside it).
                FirstPersonBodyMesh = MakeBodyComponent(Mesh, TEXT("FirstPersonBody"));
                if (USkeletalMeshComponent* Copy = FirstPersonBodyMesh)
                {
                    Copy->SetOnlyOwnerSee(true);
                    Copy->SetCastShadow(false);
                    Copy->SetVisibleInRayTracing(false);
                    Copy->bVisibleInReflectionCaptures = false;
                    Copy->bVisibleInRealTimeSkyCaptures = false;
                    Copy->SetHiddenInSceneCapture(true);
                    const int32 Head = Mesh->GetMaterials().IndexOfByPredicate([](const FSkeletalMaterial& Material)
                        { return Material.MaterialSlotName == BodyHeadMaterial; });
                    const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
                    if (Head != INDEX_NONE && RenderData && !RenderData->LODRenderData.IsEmpty())
                    {
                        const TArray<FSkelMeshRenderSection>& Sections = RenderData->LODRenderData[0].RenderSections;
                        for (int32 Section = 0; Section < Sections.Num(); ++Section)
                            if (Sections[Section].MaterialIndex == Head)
                                Copy->ShowMaterialSection(Head, Section, false, 0);
                    }
                    else
                        UE_LOG(LogUnnamedFoundation, Warning,
                            TEXT("Player body: no %s section to hide in first person"), *BodyHeadMaterial.ToString());
                }
            }
            break;
        }
        UE_LOG(LogUnnamedFoundation, Warning, TEXT("Player body: %s or its clips are missing%s"), *MeshPath.ToString(),
            Set == EFoundationBodySet::BetterSteve ? TEXT("; no body") : TEXT("; trying Better Steve"));
        if (Set == EFoundationBodySet::BetterSteve) return;
    }
    ResetBody();
}

void AFoundationPawn::DestroyBody()
{
    for (TObjectPtr<USkeletalMeshComponent>* Mesh : {&BodyMesh, &FirstPersonBodyMesh})
        if (*Mesh)
        {
            (*Mesh)->DestroyComponent();
            *Mesh = nullptr;
        }
    bBodyOwnerHidden = false;
}

void AFoundationPawn::ResetBody()
{
    BodyAnimator = FFoundationBodyAnimator(BodySet);
    if (const auto* Anim = BodyMesh ? Cast<UFoundationBodyAnimInstance>(BodyMesh->GetAnimInstance()) : nullptr)
        BodyAnimator.SetLengths(Anim->GetClipLengths());
    BodyTurn.Reset(GetControlRotation().Yaw);
    BodyJumps = 0;
}

bool AFoundationPawn::IsBodyShown() const
{
    return BodyMesh && !BodyMesh->bHiddenInGame && !bBodyOwnerHidden;
}

bool AFoundationPawn::IsBodyShadowOnly() const
{
    return BodyMesh && !BodyMesh->bHiddenInGame && bBodyOwnerHidden;
}

bool AFoundationPawn::IsFirstPersonBodyShown() const
{
    return FirstPersonBodyMesh && !FirstPersonBodyMesh->bHiddenInGame;
}

void AFoundationPawn::UpdateBody(
    double Alpha, double Seconds, const FRotator& Control, const FVector& Eye, const FVector& CameraLocation)
{
    // foundation.Body.Set changed: remake the bodies.
    if (GetRequestedBodySet() != RequestedBodySet)
    {
        DestroyBody();
        CreateBody();
    }
    if (!BodyMesh) return;
    const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
    const EFoundationBodyYaw Mode = GetBodyYawMode();
    FFoundationBodyFrame Frame;
    Frame.Velocity = FVector2D(Simulation.Feet - Simulation.PreviousFeet) / Profile.TickSeconds;
    Frame.ViewYaw = Control.Yaw;
    Frame.ViewPitch = Control.Pitch;
    Frame.BodyYaw = BodyTurn.GetYaw(Alpha, Control.Yaw, Mode);
    const double EyeCm = FMath::Lerp(Simulation.PreviousEyeCm, Simulation.EyeCm, Alpha);
    Frame.CrouchAmount = (Profile.StandingEyesCm - EyeCm) / (Profile.StandingEyesCm - Profile.CrouchingEyesCm);
    Frame.bCrouched = Simulation.bCrouched;
    Frame.bGrounded = Simulation.bGrounded;
    Frame.bFlying = Simulation.bFlying;
    Frame.bLowPose = Simulation.bLowPose;
    Frame.Jumps = BodyJumps;
    BodyJumps = 0;
    BodyAnimator.Advance(Seconds, Frame);
    // The mesh faces +Y; its origin is the feet.
    const FTransform Placement(
        FRotator(0, Frame.BodyYaw - 90, 0), FMath::Lerp(Simulation.PreviousFeet, Simulation.Feet, Alpha));
    BodyMesh->SetWorldLocationAndRotation(Placement.GetLocation(), Placement.GetRotation());
    if (auto* Anim = Cast<UFoundationBodyAnimInstance>(BodyMesh->GetAnimInstance())) Anim->SetPose(BodyAnimator.Pose);
    const bool bOn = CVarBody.GetValueOnGameThread() != 0;
    const bool bNear = Perspective == 0 || FVector::DistSquared(CameraLocation, Eye) <= FMath::Square(BodyHideCameraCm);

    if (BodySet == EFoundationBodySet::BetterSteve)
    {
        // Better Steve, as before the Unnamed Player: the third-person views only.
        ShowBody(BodyMesh, bOn && !bNear, false);
        return;
    }

    // The Unnamed Player. The first-person copy: the same pose, the corrections from this frame's view.
    const bool bFirstPerson = FirstPersonBodyMesh && IsFirstPersonBodyOn();
    if (FirstPersonBodyMesh)
    {
        FirstPersonBodyMesh->SetWorldLocationAndRotation(Placement.GetLocation(), Placement.GetRotation());
        if (auto* Anim = Cast<UFoundationBodyAnimInstance>(FirstPersonBodyMesh->GetAnimInstance()))
        {
            FFoundationFirstPersonView View;
            View.EyeCm = Placement.InverseTransformPosition(Eye);
            View.Forward = Placement.InverseTransformVectorNoScale(FRotator(0, Control.Yaw, 0).Vector());
            View.PitchDeg = FRotator::NormalizeAxis(Control.Pitch);
            View.CrouchAmount = FMath::Clamp(Frame.CrouchAmount, 0., 1.);
            View.LowWeight = BodyAnimator.Pose.LowWeight;
            Anim->SetPose(BodyAnimator.Pose);
            Anim->SetFirstPerson(View, GetFirstPersonTuning());
        }
    }
    // A spectator has no body; the dead player's first-person copy goes (the world body stays, standing, for now).
    const bool bBody = bOn && Vitals.Mode != EFoundationPlayerMode::Spectator;
    // The world body: everyone's from afar; with the camera at the eyes the owner's shadow only (world-space
    // representation, flipped in the same frame as the view), or nothing without the first-person copy.
    const bool bWorldShown = bBody && (!bNear || bFirstPerson);
    const bool bOwnerHidden = bWorldShown && bNear;
    if (bOwnerHidden != bBodyOwnerHidden)
    {
        bBodyOwnerHidden = bOwnerHidden;
        BodyMesh->SetFirstPersonPrimitiveType(
            bOwnerHidden ? EFirstPersonPrimitiveType::WorldSpaceRepresentation : EFirstPersonPrimitiveType::None);
    }
    ShowBody(BodyMesh, bWorldShown, bBody);
    if (FirstPersonBodyMesh)
        ShowBody(FirstPersonBodyMesh, bBody && bFirstPerson && Perspective == 0 && !Vitals.IsDead(), bBody);
}
