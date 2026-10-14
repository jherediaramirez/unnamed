#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationBodyAnimInstance.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationCoordinates.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationRuntimeProbe.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuPreferences.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraComponent.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Dom/JsonValue.h"

namespace
{
    TAutoConsoleVariable<int32> CVarSwim(TEXT("foundation.Swim"), 1,
        TEXT("Swimming: 0 walks through fluids as on land (vitals still count lava as water), 1 swims (water and lava "
             "drag, sinking, jump to rise, sneak to sink, jump out at walls; lava no longer counts as water)."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarBody(TEXT("foundation.Body"), 1,
        TEXT("The player body (Better Steve) in the third-person views: 0 none (the hitbox outline as before), 1 "
             "shown in F5's back and front views, hidden in first person."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarBodyYaw(TEXT("foundation.Body.Yaw"), 1,
        TEXT("Where the body faces: 0 always the view (sideways and backward travel play the strafe and backward "
             "clips), 1 vanilla (turns toward travel, backward travel keeps facing forward, the head at most 50 "
             "degrees off the body), 2 the travel direction, backward too (third-person orient-to-movement)."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarSpringArm(TEXT("foundation.View.SpringArm"), 0,
        TEXT("Third-person preview: 1 turns F5's back view into an over-the-shoulder spring arm (325 cm arm, 55 cm "
             "right, 30 cm up, smoothed, pulled in by the world's blocks); 0 vanilla's back view."),
        ECVF_Default);

    // The spring arm preview's shape and smoothing (foundation.View.SpringArm).
    namespace FoundationSpringArm
    {
        constexpr double LengthCm = 325, ShoulderCm = 55, LiftCm = 30;
        // Exponential catch-up rates (1/s): the pivot trails the eyes, the camera's rotation the view.
        constexpr double PositionLagSpeed = 12, RotationLagSpeed = 15;
        // The pivot trails at most this far; a jump farther (respawn, teleport) snaps.
        constexpr double MaxLagCm = 75, SnapCm = 300;
        // The camera's half-size against the world's boxes, as the vanilla views'.
        constexpr double ProbeCm = 5;
    }

    // Closer than this to the eyes the camera would sit inside the head: the body hides.
    constexpr double BodyHideCameraCm = 40;

    // FUnnamedInputIntent is the input layer's request; the simulation takes its own struct so it
    // never depends on input or preference systems.
    FFoundationIntent MakeSimulationIntent(const FUnnamedInputIntent& Input, bool bAutoJump, int32 SprintWindowTicks)
    {
        FFoundationIntent Intent;
        Intent.Move = Input.Move;
        Intent.bJump = Input.bJumpHeld;
        Intent.bCrouch = Input.bCrouchRequested;
        Intent.bSprint = Input.bSprintRequested;
        Intent.bAutoJump = bAutoJump;
        Intent.SprintWindowTicks = SprintWindowTicks;
        return Intent;
    }

    // Everything one movement step can touch: its sweep, step-up and edge probes, auto-jump lookahead
    // and the headroom check, with margin. The provider may return more; never less.
    FBox GetStepRegion(const FFoundationSimulation& Simulation, const UFoundationProfile& Profile)
    {
        const double Reach = Simulation.VelocityPerTick.Size() + Profile.AutoJumpHeightCm + Profile.StepCm + 200.;
        return FFoundationSimulation::Bounds(Simulation.Feet, Profile.StandingHeightCm, Profile.WidthCm)
            .ExpandBy(Reach);
    }
}

AFoundationPawn::AFoundationPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("FeetOrigin"));
    Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("WorldAlignedCollision"));
    Collision->SetupAttachment(RootComponent);
    const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
    Collision->SetBoxExtent({Profile.WidthCm * .5, Profile.WidthCm * .5, Profile.StandingHeightCm * .5});
    Collision->SetRelativeLocation({0, 0, Profile.StandingHeightCm * .5});
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionObjectType(ECC_Pawn);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Eyes"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetRelativeLocation({0, 0, Profile.StandingEyesCm});
    Camera->bUsePawnControlRotation = false;
}

void AFoundationPawn::BeginPlay()
{
    Super::BeginPlay();
    Simulation.Reset(GetActorLocation(), *GetDefault<UFoundationProfile>());
    for (TActorIterator<AFoundationLab> It(GetWorld()); It; ++It)
    {
        Lab = *It;
        break;
    }
    if (!Lab.IsValid()) Lab = GetWorld()->SpawnActor<AFoundationLab>();
    Lab->ConfigurePlayer(Vitals);
    CreateBody();
}

void AFoundationPawn::CreateBody()
{
    const FSoftObjectPath MeshPath = UUnnamedProjectSettings::GetPlayerBodyAssetPath(TEXT("SK_BetterSteve"));
    auto* Mesh = Cast<USkeletalMesh>(MeshPath.TryLoad());
    if (!Mesh)
    {
        UE_LOG(LogUnnamedFoundation, Warning, TEXT("Player body: %s is missing; no body in the third-person views"),
            *MeshPath.ToString());
        return;
    }
    BodyMesh = NewObject<USkeletalMeshComponent>(this, TEXT("Body"));
    BodyMesh->SetupAttachment(RootComponent);
    // Placed every frame at the rendered feet (the actor stands at the simulation's latest step).
    BodyMesh->SetUsingAbsoluteLocation(true);
    BodyMesh->SetUsingAbsoluteRotation(true);
    BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BodyMesh->SetGenerateOverlapEvents(false);
    BodyMesh->SetSkeletalMeshAsset(Mesh);
    // Hidden (first person) it is not posed; UpdateBody switches to posing every frame while it shows.
    BodyMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    BodyMesh->SetAnimInstanceClass(UFoundationBodyAnimInstance::StaticClass());
    BodyMesh->SetHiddenInGame(true);
    BodyMesh->SetTickGroup(PrimaryActorTick.TickGroup);
    BodyMesh->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
    BodyMesh->RegisterComponent();
    const auto* Anim = Cast<UFoundationBodyAnimInstance>(BodyMesh->GetAnimInstance());
    if (!Anim || !Anim->HasClips())
    {
        BodyMesh->DestroyComponent();
        BodyMesh = nullptr;
        return;
    }
    BodyAnimator.SetLengths(Anim->GetClipLengths());
}

void AFoundationPawn::ResetBody()
{
    BodyAnimator = FFoundationBodyAnimator();
    if (const auto* Anim = BodyMesh ? Cast<UFoundationBodyAnimInstance>(BodyMesh->GetAnimInstance()) : nullptr)
        BodyAnimator.SetLengths(Anim->GetClipLengths());
    BodyTurn.Reset(GetControlRotation().Yaw);
    BodyJumps = 0;
}

bool AFoundationPawn::IsBodyShown() const
{
    return BodyMesh && !BodyMesh->bHiddenInGame;
}

EFoundationBodyYaw AFoundationPawn::GetBodyYawMode()
{
    return EFoundationBodyYaw(FMath::Clamp(CVarBodyYaw.GetValueOnGameThread(), 0, 2));
}

bool AFoundationPawn::IsSpringArmView() const
{
    return Perspective == 1 && CVarSpringArm.GetValueOnGameThread() != 0;
}

void AFoundationPawn::UpdateBody(
    double Alpha, double Seconds, const FRotator& Control, const FVector& Eye, const FVector& CameraLocation)
{
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
    Frame.Jumps = BodyJumps;
    BodyJumps = 0;
    BodyAnimator.Advance(Seconds, Frame);
    // The mesh faces +Y; its origin is the feet.
    BodyMesh->SetWorldLocationAndRotation(
        FMath::Lerp(Simulation.PreviousFeet, Simulation.Feet, Alpha), FRotator(0, Frame.BodyYaw - 90, 0));
    if (auto* Anim = Cast<UFoundationBodyAnimInstance>(BodyMesh->GetAnimInstance())) Anim->SetPose(BodyAnimator.Pose);
    const bool bShow = CVarBody.GetValueOnGameThread() != 0 && Perspective != 0 &&
        FVector::DistSquared(CameraLocation, Eye) > FMath::Square(BodyHideCameraCm);
    if (BodyMesh->bHiddenInGame == bShow)
    {
        BodyMesh->SetHiddenInGame(!bShow);
        // The mesh ticks after the pawn, so a body shown this frame is posed this frame (never a stale or reference
        // pose, as "only when rendered" would draw on its first frame).
        BodyMesh->VisibilityBasedAnimTickOption = bShow ? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
                                                        : EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    }
}

FVector AFoundationPawn::PlaceSpringArm(const FVector& Eye, FRotator& View, double Seconds)
{
    using namespace FoundationSpringArm;
    StepCollision.Reset();
    Lab->GatherCollision(FBox(Eye, Eye).ExpandBy(LengthCm + ShoulderCm + LiftCm + MaxLagCm + 50), StepCollision);
    const FBox Probe(FVector(-ProbeCm), FVector(ProbeCm));
    if (!bArmPlaced || FVector::DistSquared(ArmPivot, Eye) > FMath::Square(SnapCm))
    {
        ArmPivot = Eye;
        ArmRotation = View.Quaternion();
        bArmPlaced = true;
    }
    // Position lag: the pivot trails the eyes, at most MaxLagCm and never through a block.
    FVector Lagged = FMath::Lerp(ArmPivot, Eye, 1 - FMath::Exp(-PositionLagSpeed * Seconds));
    Lagged = Eye + (Lagged - Eye).GetClampedToMaxSize(MaxLagCm);
    ArmPivot = Eye + FFoundationSimulation::Clip(Probe.ShiftBy(Eye), Lagged - Eye, StepCollision);
    // Rotation lag: the camera turns after the view (the aim and targeting stay the view's, from the eyes).
    ArmRotation = FQuat::Slerp(ArmRotation, View.Quaternion(), 1 - FMath::Exp(-RotationLagSpeed * Seconds));
    View = ArmRotation.Rotator();
    View.Roll = 0;
    // Over the shoulder, then back along the view; each leg pulled in by the world's boxes.
    const FVector Right = FRotationMatrix(FRotator(0, View.Yaw, 0)).GetScaledAxis(EAxis::Y);
    FVector Location = ArmPivot;
    Location +=
        FFoundationSimulation::Clip(Probe.ShiftBy(Location), Right * ShoulderCm + FVector(0, 0, LiftCm), StepCollision);
    Location += FFoundationSimulation::Clip(Probe.ShiftBy(Location), -View.Vector() * LengthCm, StepCollision);
    return Location;
}

void AFoundationPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (FFoundationAuthority* Authority = GetAuthority()) Authority->RemovePlayer(Interaction.PlayerId);
    Interaction.PlayerId = INDEX_NONE;
    Super::EndPlay(EndPlayReason);
}

void AFoundationPawn::SetSimulationEnabled(bool bEnabled)
{
    bSimulate = bEnabled;
    Simulation.Accumulator = 0;
    Simulation.PreviousFeet = Simulation.Feet;
    Simulation.bSprinting = false;
    Simulation.bHadForward = false;
    Simulation.SprintTimer = 0;
    // Pausing keeps flight but forgets a half-finished double tap.
    Simulation.bHadJump = false;
    Simulation.FlyTriggerTimer = 0;
    LastIntent = {};
}

FVector AFoundationPawn::GetEyePosition() const
{
    const auto* P = GetDefault<UFoundationProfile>();
    return Simulation.Feet + FVector(0, 0, Simulation.bCrouched ? P->CrouchingEyesCm : P->StandingEyesCm);
}

FFoundationTarget AFoundationPawn::TraceTarget() const
{
    return Lab.IsValid()
        ? Lab->TraceTarget(GetEyePosition(), GetControlRotation().Vector(), *GetDefault<UFoundationProfile>())
        : FFoundationTarget{};
}

FFoundationAuthority* AFoundationPawn::GetAuthority() const
{
    return Lab.IsValid() && Interaction.PlayerId != INDEX_NONE ? Lab->GetAuthority() : nullptr;
}

FFoundationPlayerFacts AFoundationPawn::MakeInteractionFacts() const
{
    const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
    FFoundationPlayerFacts Facts;
    Facts.FeetCm = Simulation.Feet;
    Facts.HeightCm = Simulation.HeightCm;
    Facts.WidthCm = Profile.WidthCm;
    // The resolved gameplay eye and current aim: no third-person offset, bob, hand or FOV.
    Facts.EyeCm = GetEyePosition();
    Facts.Aim = GetControlRotation().Vector();
    Facts.FallDistanceCm = Simulation.FallDistanceCm;
    Facts.StepMovedCm = Simulation.LastDisplacement.Size2D();
    Facts.bGrounded = Simulation.bGrounded;
    Facts.bSprinting = Simulation.bSprinting;
    Facts.bBodyInWater = bBodyInWater;
    Facts.bEyesInWater = bEyesInWater;
    Facts.bAlive = !Vitals.IsDead();
    Facts.bSecondaryUse = LastIntent.bCrouch;
    Facts.Step = ReferenceSteps;
    return Facts;
}

void AFoundationPawn::ApplyWorldOffset(const FVector& Offset, bool bWorldShift)
{
    Super::ApplyWorldOffset(Offset, bWorldShift);
    Simulation.Feet += Offset;
    Simulation.PreviousFeet += Offset;
    if (Lab.IsValid()) Lab->ApplyLogicalOffset(Offset);
}

void AFoundationPawn::Respawn()
{
    Vitals.Respawn();
    const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
    Simulation.Reset(Lab.IsValid() ? Lab->GetSafeSpawn(Profile) : FVector::ZeroVector, Profile);
    SetActorLocation(Simulation.Feet);
    ResetBody();
}

void AFoundationPawn::StepVitals(const UFoundationProfile& Profile)
{
    FFoundationVitalsStep Facts;
    Facts.DisplacementCm = Simulation.LastDisplacement;
    Facts.bGrounded = Simulation.bGrounded;
    Facts.bSprinting = Simulation.bSprinting;
    Facts.bJumped = Simulation.bJumped;
    Facts.bSprintJumped = Simulation.bSprintJumped;
    const FBox Body = FFoundationSimulation::Bounds(Simulation.Feet, Simulation.HeightCm, Profile.WidthCm);
    // Swimming tells lava from water; without it lava counts as water, as before.
    const bool bSwim = CVarSwim.GetValueOnGameThread() != 0;
    Facts.bBodyInWater = bBodyInWater = bSwim ? Lab->GetFluid(Body) == EFoundationFluid::Water : Lab->IsInWater(Body);
    Facts.bEyesInWater = bEyesInWater =
        bSwim ? Lab->GetFluid(GetEyePosition()) == EFoundationFluid::Water : Lab->IsInWater(GetEyePosition());
    Vitals.Step(Facts);
    if (Vitals.ShouldRespawn()) Respawn();
}

void AFoundationPawn::SyncInteraction(FFoundationAuthority& Authority)
{
    const FName Preset = Authority.Options.PlacementPreset;
    if (Interaction.Preset.Name != Preset)
        if (const FFoundationPlacementPreset* Found = UFoundationInteractionSettings::Get().FindPreset(Preset))
            Interaction.SetPreset(*Found, GetDefault<UFoundationProfile>()->TickSeconds);
    if (Interaction.SessionId == Authority.GetSessionId()) return;
    // A lab reset: back to spawn with nothing held or pending; keys still down must be released first.
    const bool bWasRegistered = Interaction.SessionId != 0;
    if (auto* FoundationController = Cast<AFoundationController>(GetController());
        FoundationController && bWasRegistered)
        FoundationController->CancelActions();
    Interaction.Resync(Authority);
    if (!bWasRegistered) return;
    const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
    Simulation.Reset(Lab->GetSafeSpawn(Profile), Profile);
    Simulation.bGrounded = true;
    Vitals.ResetFall();
    SetActorLocation(Simulation.Feet);
    ResetBody();
}

void AFoundationPawn::StepInteraction(FFoundationAuthority& Authority, bool bLatestStep, double NowSeconds)
{
    // The authority sees this step's pose; targets' damage cooldowns and item entities step once per reference step.
    Authority.Options.bItemEntities = FoundationItemEntities::IsOn();
    Authority.Options.bFullInventory = FoundationInventory::IsFull();
    Authority.Options.bCrafting = FoundationCrafting::IsEnabled();
    Authority.Options.bItemUse = FoundationItemUse::IsOn();
    Authority.Options.bExperienceOrbs = FoundationExperienceOrbs::IsOn();
    Authority.SetFacts(Interaction.PlayerId, MakeInteractionFacts());
    Authority.Step();
    // Step 12: the orbs this player took this step fill its XP bar.
    if (const int32 Points = Authority.TakeExperience(Interaction.PlayerId)) Vitals.AddExperience(Points);
    Interaction.StepOnce(Authority, Vitals, bLatestStep, NowSeconds);
    // A landed sprint-knockback hit ends the sprint; held sprint input restarts it by the simulation's own rules.
    if (Interaction.bStopSprint)
    {
        Interaction.bStopSprint = false;
        Simulation.bSprinting = false;
    }
}

void AFoundationPawn::PressInteraction(EFoundationInput Input)
{
    Interaction.Press(Input, FPlatformTime::Seconds());
}

void AFoundationPawn::ReleaseInteraction(EFoundationInput Input)
{
    Interaction.Release(Input);
}

void AFoundationPawn::CancelInteraction(EFoundationReason Reason)
{
    if (FFoundationAuthority* Authority = GetAuthority()) Interaction.CancelAll(*Authority, Vitals, Reason);
}

void AFoundationPawn::SelectInteractionSlot(int32 Slot)
{
    if (FFoundationAuthority* Authority = GetAuthority()) Interaction.SelectSlot(*Authority, Vitals, Slot);
}

void AFoundationPawn::ScrollInteraction(int32 Notches)
{
    if (FFoundationAuthority* Authority = GetAuthority()) Interaction.Scroll(*Authority, Vitals, Notches);
}

bool AFoundationPawn::PickInteractionBlock()
{
    FFoundationAuthority* Authority = GetAuthority();
    return Authority && Interaction.Pick(*Authority, Vitals);
}

void AFoundationPawn::DropInteractionItem(bool bWholeStack)
{
    if (FFoundationAuthority* Authority = GetAuthority()) Interaction.Drop(*Authority, Vitals, bWholeStack);
}

void AFoundationPawn::SwapInteractionHands()
{
    if (FFoundationAuthority* Authority = GetAuthority()) Interaction.SwapHands(*Authority, Vitals);
}

void AFoundationPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* PlayerController = Cast<APlayerController>(GetController());
    if (!PlayerController || !PlayerController->GetLocalPlayer() || !Lab.IsValid()) return;
#if WITH_DEV_AUTOMATION_TESTS
    // Opt-in runtime verification (-FoundationProbe=report.json); never part of Shipping.
    if (!bProbeCreated)
    {
        bProbeCreated = true;
        FString ProbePath;
        if (FParse::Value(FCommandLine::Get(), TEXT("FoundationProbe="), ProbePath))
        {
            auto* Probe = NewObject<UFoundationRuntimeProbe>(this);
            Probe->RegisterComponent();
            Probe->AddTickPrerequisiteActor(this);
        }
    }
#endif
    const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
    // Register with the lab's interaction authority (the Playground only).
    if (Interaction.PlayerId == INDEX_NONE)
        if (FFoundationAuthority* LabAuthority = Lab->GetAuthority()) Interaction.PlayerId = LabAuthority->AddPlayer();
    FFoundationAuthority* Authority = GetAuthority();
    const auto* Preferences = GetGameInstance()->GetSubsystem<UMenuPreferences>();
    auto ReadNumber = [Preferences](const TCHAR* Id, double Default)
    {
        const auto Value = Preferences->FindPreference(Id);
        return Value.IsValid() && Value->Type == EJson::Number ? Value->AsNumber() : Default;
    };
    auto ReadBool = [Preferences](const TCHAR* Id, bool Default)
    {
        const auto Value = Preferences->FindPreference(Id);
        return Value.IsValid() && Value->Type == EJson::Boolean ? Value->AsBool() : Default;
    };
    if (bSimulate)
    {
        const FUnnamedInputIntent Request =
            PlayerController->GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>()->ConsumeIntent();
        // Mouse look uses the reference sensitivity curve and invert preferences.
        FRotator View = PlayerController->GetControlRotation();
        const double Gain = 8. * FMath::Pow(ReadNumber(TEXT("sensitivity"), .5) * .6 + .2, 3) * .15;
        View.Yaw += Request.Look.X * Gain * (ReadBool(TEXT("invertXMouse"), false) ? -1 : 1);
        // Within the engine camera manager's own pitch limits (+-89.9 by default): it turns a pitch beyond
        // them, such as -90 from looking fully down, into +89.9 (straight up).
        const APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
        const double MinPitch = FMath::Max(-90., CameraManager ? double(CameraManager->ViewPitchMin) : -89.9);
        const double MaxPitch = FMath::Min(90., CameraManager ? double(CameraManager->ViewPitchMax) : 89.9);
        View.Pitch = FMath::Clamp(
            View.Pitch - Request.Look.Y * Gain * (ReadBool(TEXT("invertYMouse"), false) ? -1 : 1), MinPitch, MaxPitch);
        View.Roll = 0;
        PlayerController->SetControlRotation(View);
        LastIntent = MakeSimulationIntent(Request, ReadBool(TEXT("autoJump"), false),
            FMath::Clamp(int32(ReadNumber(TEXT("sprintWindow"), Profile.SprintWindowTicks)), 0, 10));
        // Per reference step: movement against already committed collision, vitals (each step's eligibility
        // and death come from the state the previous step left), then interaction against the resulting
        // pose. Its commits reach the collision provider before the next movement step.
        const int32 Due = LastDueSteps = Simulation.TakeDueSteps(DeltaSeconds, Profile);
        const double Now = FPlatformTime::Seconds();
        double InteractionSeconds = 0;
        if (Authority)
        {
            SyncInteraction(*Authority);
            Interaction.BeginUpdate();
        }
        for (int32 Index = 0; Index < Due; ++Index)
        {
            const double GatherStart = FPlatformTime::Seconds();
            if (Authority) SyncInteraction(*Authority);
            Eligibility.bMobile = !Vitals.IsDead();
            Eligibility.bEnoughFood = Vitals.HasEnoughFood();
            Eligibility.bMayFly = Vitals.Mode == EFoundationPlayerMode::Creative;
            // An item in use (eating) slows the player and stops sprint starts, from the authority's state.
            const FFoundationPlayer* UsePlayer = Authority ? Authority->FindPlayer(Interaction.PlayerId) : nullptr;
            Eligibility.bItemSlows = UsePlayer && UsePlayer->Use.bActive;
            StepCollision.Reset();
            Lab->GatherCollision(GetStepRegion(Simulation, Profile), StepCollision);
            Lab->GetFluidFacts(CVarSwim.GetValueOnGameThread() != 0, Simulation, Profile, Eligibility);
            InteractionSeconds += FPlatformTime::Seconds() - GatherStart;
            Simulation.Step(LastIntent, View.Yaw, Profile, StepCollision, Eligibility);
            ++ReferenceSteps;
            if (!bBodyTurnStarted) BodyTurn.Reset(View.Yaw);
            bBodyTurnStarted = true;
            BodyTurn.Step(FVector2D(Simulation.Feet - Simulation.PreviousFeet), View.Yaw, GetBodyYawMode());
            if (Simulation.bJumped) ++BodyJumps;
            StepVitals(Profile);
            if (Simulation.bFlying) Vitals.ResetFall(); // No fall distance builds while flying.
            if (Authority)
            {
                const double InteractionStart = FPlatformTime::Seconds();
                StepInteraction(*Authority, Index == Due - 1, Now);
                InteractionSeconds += FPlatformTime::Seconds() - InteractionStart;
            }
        }
        SetActorLocation(Simulation.Feet);
        SetActorRotation(FRotator::ZeroRotator);
        // Falling out of the lab respawns at its spawn (a test-level convenience, not void damage).
        if (Simulation.Feet.Z < Lab->GetFallOutZ())
        {
            Simulation.Reset(Lab->GetSafeSpawn(Profile), Profile);
            Vitals.ResetFall();
            SetActorLocation(Simulation.Feet);
            ResetBody();
        }
        if (Authority)
        {
            // The per-frame preview: what a use would do at the current aim, for presentation.
            const double PreviewStart = FPlatformTime::Seconds();
            Interaction.UpdatePreview(*Authority, MakeInteractionFacts());
            InteractionSeconds += FPlatformTime::Seconds() - PreviewStart;
            InteractionMs.Add(float(InteractionSeconds * 1000.));
            if (InteractionMs.Num() > 4096) InteractionMs.RemoveAt(0, InteractionMs.Num() - 4096);
        }
    }
    Collision->SetBoxExtent({Profile.WidthCm * .5, Profile.WidthCm * .5, Simulation.HeightCm * .5});
    Collision->SetRelativeLocation({0, 0, Simulation.HeightCm * .5});
    // Render between the last two reference ticks.
    const double Alpha = bSimulate ? FMath::Clamp(Simulation.Accumulator / Profile.TickSeconds, 0., 1.) : 1.;
    const FVector Eye = FMath::Lerp(Simulation.PreviousFeet, Simulation.Feet, Alpha) +
        FVector(0, 0, FMath::Lerp(Simulation.PreviousEyeCm, Simulation.EyeCm, Alpha));
    const FRotator Control = PlayerController->GetControlRotation();
    FRotator View = Control;
    FVector CameraLocation = Eye;
    if (IsSpringArmView())
        CameraLocation = PlaceSpringArm(Eye, View, DeltaSeconds);
    else
    {
        bArmPlaced = false;
        if (Perspective)
        {
            // Third person: back (1) or front (2), pulled in by collision like the reference camera.
            const FVector Offset = View.Vector() * (Perspective == 1 ? -400 : 400);
            StepCollision.Reset();
            Lab->GatherCollision(FBox(Eye, Eye).ExpandBy(450), StepCollision);
            CameraLocation +=
                FFoundationSimulation::Clip(FBox(Eye - FVector(5), Eye + FVector(5)), Offset, StepCollision);
            if (Perspective == 2)
            {
                View.Yaw += 180;
                View.Pitch = -View.Pitch;
            }
        }
    }
    Camera->SetWorldLocationAndRotation(CameraLocation, View);
    UpdateBody(Alpha, DeltaSeconds, Control, Eye, CameraLocation);
    int32 ViewportWidth = 0, ViewportHeight = 0;
    PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
    const double FovEffect = ReadNumber(TEXT("fovEffectScale"), 1);
    const double VerticalFov = ReadNumber(TEXT("fov"), 110) *
        (1. + (FMath::Lerp(Simulation.PreviousFovMultiplier, Simulation.FovMultiplier, Alpha) - 1.) * FovEffect);
    const double Aspect = ViewportHeight > 0 ? double(ViewportWidth) / ViewportHeight : 16. / 9.;
    Camera->SetFieldOfView(FoundationCoordinates::HorizontalFov(VerticalFov, Aspect));
    // foundation.Hand.Fit: the held item at a fixed FOV, so it keeps its size whatever Options > FOV and the FOV
    // effects say (first-person primitives, FoundationInteractionView).
    const bool bHandFit = FoundationHand::IsFitOn();
    Camera->SetEnableFirstPersonFieldOfView(bHandFit);
    if (bHandFit)
        Camera->SetFirstPersonFieldOfView(
            FoundationCoordinates::HorizontalFov(FoundationHand::FixedVerticalFov, Aspect));
}

void AFoundationPawn::TeleportFeet(const FVector& FeetCm)
{
    Simulation.Feet = Simulation.PreviousFeet = FeetCm;
    Simulation.VelocityPerTick = Simulation.LastDisplacement = FVector::ZeroVector;
    Simulation.FallDistanceCm = 0;
    Simulation.bGrounded = false;
    Vitals.ResetFall();
    SetActorLocation(FeetCm);
    ResetBody();
}
