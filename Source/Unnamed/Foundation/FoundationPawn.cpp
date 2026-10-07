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

AFoundationPawn::AFoundationPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("FeetOrigin"));
    Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("WorldAlignedCollision"));
    Collision->SetupAttachment(RootComponent);
    Collision->SetBoxExtent({30, 30, 90});
    Collision->SetRelativeLocation({0, 0, 90});
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionObjectType(ECC_Pawn);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Eyes"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetRelativeLocation({0, 0, 162});
    Camera->bUsePawnControlRotation = false;
}

void AFoundationPawn::BeginPlay()
{
    Super::BeginPlay();
    Simulation.Reset(GetActorLocation());
    for (TActorIterator<AFoundationLab> It(GetWorld()); It; ++It)
    {
        Lab = *It;
        break;
    }
    if (!Lab.IsValid()) Lab = GetWorld()->SpawnActor<AFoundationLab>();
}

void AFoundationPawn::SetSimulationEnabled(bool Enabled)
{
    bSimulate = Enabled;
    Simulation.Accumulator = 0;
    Simulation.PreviousFeet = Simulation.Feet;
    Simulation.bSprinting = false;
    Simulation.bHadForward = false;
    Simulation.SprintTimer = 0;
    LastIntent = {};
}

FVector AFoundationPawn::EyePosition() const
{
    const auto* P = GetDefault<UFoundationProfile>();
    return Simulation.Feet + FVector(0, 0, Simulation.bCrouched ? P->CrouchingEyesCm : P->StandingEyesCm);
}

FFoundationTarget AFoundationPawn::Target() const
{
    return Lab.IsValid() ? Lab->Target(EyePosition(), GetControlRotation().Vector(), *GetDefault<UFoundationProfile>())
                         : FFoundationTarget{};
}

void AFoundationPawn::ApplyWorldOffset(const FVector& Offset, bool WorldShift)
{
    Super::ApplyWorldOffset(Offset, WorldShift);
    Simulation.Feet += Offset;
    Simulation.PreviousFeet += Offset;
    if (Lab.IsValid())
    {
        for (auto& B : Lab->CollisionBoxes)
            B = B.ShiftBy(Offset);
        for (auto& Item : Lab->Primitives)
            Item.Box = Item.Box.ShiftBy(Offset);
    }
}

void AFoundationPawn::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* PC = Cast<APlayerController>(GetController());
    if (!PC || !PC->GetLocalPlayer() || !Lab.IsValid()) return;
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
    const auto& P = *GetDefault<UFoundationProfile>();
    auto* Prefs = GetGameInstance()->GetSubsystem<UMenuPreferences>();
    auto Number = [&](const TCHAR* Id, double Default)
    {
        auto V = Prefs->FindPreference(Id);
        return V.IsValid() && V->Type == EJson::Number ? V->AsNumber() : Default;
    };
    auto Boolean = [&](const TCHAR* Id, bool Default)
    {
        auto V = Prefs->FindPreference(Id);
        return V.IsValid() && V->Type == EJson::Boolean ? V->AsBool() : Default;
    };
    if (bSimulate)
    {
        const auto Request = PC->GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>()->ConsumeIntent();
        FRotator View = PC->GetControlRotation();
        const double Gain = 8. * FMath::Pow(Number(TEXT("sensitivity"), .5) * .6 + .2, 3) * .15;
        View.Yaw += Request.Look.X * Gain * (Boolean(TEXT("invertXMouse"), false) ? -1 : 1);
        View.Pitch = FMath::Clamp(
            View.Pitch - Request.Look.Y * Gain * (Boolean(TEXT("invertYMouse"), false) ? -1 : 1), -90., 90.);
        View.Roll = 0;
        PC->SetControlRotation(View);
        LastIntent.Move = Request.Move;
        LastIntent.bJump = Request.bJumpHeld;
        LastIntent.bSneak = Request.bCrouchRequested;
        LastIntent.bSprint = Request.bSprintRequested;
        LastIntent.bAutoJump = Boolean(TEXT("autoJump"), false);
        LastIntent.SprintWindowTicks = FMath::Clamp(int32(Number(TEXT("sprintWindow"), 7)), 0, 10);
        Simulation.Advance(Dt, LastIntent, View.Yaw, P, Lab->CollisionBoxes, Eligibility);
        SetActorLocation(Simulation.Feet);
        SetActorRotation(FRotator::ZeroRotator);
        if (Simulation.Feet.Z < -1000)
        {
            Simulation.Reset({0, 0, 0});
            SetActorLocation(Simulation.Feet);
        }
    }
    Collision->SetBoxExtent({P.WidthCm * .5, P.WidthCm * .5, Simulation.HeightCm * .5});
    Collision->SetRelativeLocation({0, 0, Simulation.HeightCm * .5});
    const double Alpha = bSimulate ? FMath::Clamp(Simulation.Accumulator / P.TickSeconds, 0., 1.) : 1.;
    FVector Eye = FMath::Lerp(Simulation.PreviousFeet, Simulation.Feet, Alpha) +
        FVector(0, 0, FMath::Lerp(Simulation.PreviousEyeCm, Simulation.EyeCm, Alpha));
    FRotator View = PC->GetControlRotation();
    if (Perspective)
    {
        const FVector Offset = View.Vector() * (Perspective == 1 ? -400 : 400);
        const FVector Travel =
            FFoundationSimulation::Clip(FBox(Eye - FVector(5), Eye + FVector(5)), Offset, Lab->CollisionBoxes);
        Eye += Travel;
        if (Perspective == 2)
        {
            View.Yaw += 180;
            View.Pitch = -View.Pitch;
        }
    }
    Camera->SetWorldLocationAndRotation(Eye, View);
    int32 W = 0, H = 0;
    PC->GetViewportSize(W, H);
    const double Effect = Number(TEXT("fovEffectScale"), 1);
    const double V = Number(TEXT("fov"), 70) *
        (1. + (FMath::Lerp(Simulation.PreviousFovMultiplier, Simulation.FovMultiplier, Alpha) - 1.) * Effect);
    Camera->SetFieldOfView(FoundationCoordinates::HorizontalFov(V, H > 0 ? double(W) / H : 16. / 9.));
}
