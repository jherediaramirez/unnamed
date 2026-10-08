#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationCoordinates.h"
#include "Foundation/FoundationRuntimeProbe.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuPreferences.h"
#include "Components/BoxComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Dom/JsonValue.h"

namespace
{
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
}

void AFoundationPawn::StepVitals(const UFoundationProfile& Profile)
{
    FFoundationVitalsStep Facts;
    Facts.DisplacementCm = Simulation.LastDisplacement;
    Facts.bGrounded = Simulation.bGrounded;
    Facts.bSprinting = Simulation.bSprinting;
    Facts.bJumped = Simulation.bJumped;
    Facts.bSprintJumped = Simulation.bSprintJumped;
    Facts.bBodyInWater = bBodyInWater =
        Lab->IsInWater(FFoundationSimulation::Bounds(Simulation.Feet, Simulation.HeightCm, Profile.WidthCm));
    Facts.bEyesInWater = bEyesInWater = Lab->IsInWater(GetEyePosition());
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
}

void AFoundationPawn::StepInteraction(FFoundationAuthority& Authority, bool bLatestStep, double NowSeconds)
{
    // The authority sees this step's pose; targets' damage cooldowns count once per reference step.
    Authority.SetFacts(Interaction.PlayerId, MakeInteractionFacts());
    Authority.Step();
    Interaction.StepOnce(Authority, Vitals, bLatestStep, NowSeconds);
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
            StepCollision.Reset();
            Lab->GatherCollision(GetStepRegion(Simulation, Profile), StepCollision);
            InteractionSeconds += FPlatformTime::Seconds() - GatherStart;
            Simulation.Step(LastIntent, View.Yaw, Profile, StepCollision, Eligibility);
            ++ReferenceSteps;
            StepVitals(Profile);
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
        if (Simulation.Feet.Z < -1000)
        {
            Simulation.Reset(Lab->GetSafeSpawn(Profile), Profile);
            Vitals.ResetFall();
            SetActorLocation(Simulation.Feet);
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
    FVector Eye = FMath::Lerp(Simulation.PreviousFeet, Simulation.Feet, Alpha) +
        FVector(0, 0, FMath::Lerp(Simulation.PreviousEyeCm, Simulation.EyeCm, Alpha));
    FRotator View = PlayerController->GetControlRotation();
    if (Perspective)
    {
        // Third person: back (1) or front (2), pulled in by collision like the reference camera.
        const FVector Offset = View.Vector() * (Perspective == 1 ? -400 : 400);
        StepCollision.Reset();
        Lab->GatherCollision(FBox(Eye, Eye).ExpandBy(450), StepCollision);
        Eye += FFoundationSimulation::Clip(FBox(Eye - FVector(5), Eye + FVector(5)), Offset, StepCollision);
        if (Perspective == 2)
        {
            View.Yaw += 180;
            View.Pitch = -View.Pitch;
        }
    }
    Camera->SetWorldLocationAndRotation(Eye, View);
    int32 ViewportWidth = 0, ViewportHeight = 0;
    PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
    const double FovEffect = ReadNumber(TEXT("fovEffectScale"), 1);
    const double VerticalFov = ReadNumber(TEXT("fov"), 70) *
        (1. + (FMath::Lerp(Simulation.PreviousFovMultiplier, Simulation.FovMultiplier, Alpha) - 1.) * FovEffect);
    Camera->SetFieldOfView(FoundationCoordinates::HorizontalFov(
        VerticalFov, ViewportHeight > 0 ? double(ViewportWidth) / ViewportHeight : 16. / 9.));
}
