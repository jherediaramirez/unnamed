#include "Foundation/FoundationPawn.h"
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

void AFoundationPawn::ApplyWorldOffset(const FVector& Offset, bool bWorldShift)
{
    Super::ApplyWorldOffset(Offset, bWorldShift);
    Simulation.Feet += Offset;
    Simulation.PreviousFeet += Offset;
    if (Lab.IsValid())
    {
        for (FBox& Box : Lab->CollisionBoxes)
            Box = Box.ShiftBy(Offset);
        for (auto& Item : Lab->Primitives)
            Item.Box = Item.Box.ShiftBy(Offset);
    }
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
        View.Pitch = FMath::Clamp(
            View.Pitch - Request.Look.Y * Gain * (ReadBool(TEXT("invertYMouse"), false) ? -1 : 1), -90., 90.);
        View.Roll = 0;
        PlayerController->SetControlRotation(View);
        LastIntent = MakeSimulationIntent(Request, ReadBool(TEXT("autoJump"), false),
            FMath::Clamp(int32(ReadNumber(TEXT("sprintWindow"), Profile.SprintWindowTicks)), 0, 10));
        Simulation.Advance(DeltaSeconds, LastIntent, View.Yaw, Profile, Lab->CollisionBoxes, Eligibility);
        SetActorLocation(Simulation.Feet);
        SetActorRotation(FRotator::ZeroRotator);
        // Falling out of the lab respawns at the origin.
        if (Simulation.Feet.Z < -1000)
        {
            Simulation.Reset({0, 0, 0}, Profile);
            SetActorLocation(Simulation.Feet);
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
        Eye += FFoundationSimulation::Clip(FBox(Eye - FVector(5), Eye + FVector(5)), Offset, Lab->CollisionBoxes);
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
