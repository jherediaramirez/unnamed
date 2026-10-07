#include "FoundationWorld.h"
#include "FoundationController.h"
#include "FoundationRuntimeProbe.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "../Input/UnnamedInputSubsystem.h"
#include "../MenuPreferences.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Dom/JsonValue.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

FFoundationActionResult FFoundationActions::Dispatch(const FFoundationActionRequest& R)
{
    FFoundationActionResult Result;
    if (R.Phase == EFoundationPhase::Cancel || R.Phase == EFoundationPhase::Release)
    {
        if (R.Action == EFoundationAction::Attack) bBreaking = false;
        if (R.Action == EFoundationAction::Use) bUsing = false;
        Result = {true, R.Phase == EFoundationPhase::Cancel ? TEXT("cancelled") : TEXT("released")};
    }
    else
        switch (R.Action)
        {
        case EFoundationAction::Attack:
            if (bUsing)
                Result.Reason = TEXT("attack rejected: active item use");
            else if (!R.Target.bInRange)
            {
                bBreaking = false;
                Result.Reason = TEXT("attack rejected: no visible target in reach");
            }
            else if (R.Target.Kind == EFoundationTargetKind::Entity && R.Phase == EFoundationPhase::Hold)
                Result.Reason = TEXT("entity attack requires press");
            else
            {
                bBreaking = R.Target.Kind == EFoundationTargetKind::Block;
                ActiveTarget = R.Target.Name;
                Result = {true,
                    bBreaking ? TEXT("break fixture active (no mining)") : TEXT("entity attack fixture (no damage)")};
            }
            break;
        case EFoundationAction::Use:
            if (bBreaking)
                Result.Reason = TEXT("use rejected: block break active");
            else if (R.Target.bInRange && R.Target.Kind == EFoundationTargetKind::Block &&
                R.Target.bRejectInteraction && !(R.bSneak && (Slots[Slot] > 0 || Offhand > 0)))
                Result.Reason = TEXT("block interaction failed; item/offhand fallback rejected");
            else if (R.Target.bInRange && R.Target.bInteractive &&
                !(R.Target.Kind == EFoundationTargetKind::Block && R.bSneak && (Slots[Slot] > 0 || Offhand > 0)))
                Result = {true, TEXT("target interaction fixture accepted")};
            else if (Slots[Slot] > 0)
            {
                bUsing = true;
                Result = {true, TEXT("main-hand use/place fixture accepted (no placement)")};
            }
            else if (Offhand > 0)
            {
                bUsing = true;
                Result = {true, TEXT("offhand use fixture accepted"), EFoundationHand::Off};
            }
            else
                Result.Reason = TEXT("use passed both empty hands");
            break;
        case EFoundationAction::Pick:
            if (bUsing)
                Result.Reason = TEXT("pick rejected: active use");
            else if (!R.Target.bInRange)
                Result.Reason = TEXT("pick rejected: no target in reach");
            else
            {
                Select(R.Target.Kind == EFoundationTargetKind::Block ? 0 : 1);
                Result = {true, TEXT("picked matching diagnostic token slot")};
            }
            break;
        case EFoundationAction::Drop:
            if (Slots[Slot] <= 0)
                Result.Reason = TEXT("drop rejected: empty fixture slot");
            else
            {
                Slots[Slot] = R.bControl ? 0 : Slots[Slot] - 1;
                Result = {true, R.bControl ? TEXT("dropped fixture stack") : TEXT("dropped one fixture token")};
            }
            break;
        case EFoundationAction::Swap:
            Swap(Slots[Slot], Offhand);
            Cancel();
            Result = {true, TEXT("swapped fixture hands")};
            break;
        case EFoundationAction::Inventory:
            Cancel();
            Result = {true, TEXT("opened inventory routing fixture")};
            break;
        }
    LastResult = Result.Reason;
    LastHand = Result.Hand == EFoundationHand::Main ? TEXT("main") : TEXT("off");
    return Result;
}

void FFoundationActions::Select(int32 NewSlot)
{
    Slot = FMath::Clamp(NewSlot, 0, 8);
    Cancel();
}

void FFoundationActions::Scroll(int32 Notches)
{
    Select(((Slot - Notches) % 9 + 9) % 9);
}

void FFoundationActions::Cancel()
{
    bBreaking = bUsing = false;
    ActiveTarget.Empty();
}

AFoundationLab::AFoundationLab()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Cube = Mesh.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    Material = Mat.Object;
}

void AFoundationLab::Add(FString Label, FVector Min, FVector Max, EFoundationTargetKind Kind, bool Interactive)
{
    const FBox Box(Min, Max);
    Primitives.Add({Box, Label, Kind, Interactive});
    CollisionBoxes.Add(Box);
    auto* Mesh = NewObject<UStaticMeshComponent>(this);
    Mesh->SetStaticMesh(Cube);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetupAttachment(RootComponent);
    Mesh->RegisterComponent();
    Mesh->SetWorldLocation(Box.GetCenter());
    Mesh->SetWorldScale3D(Box.GetSize() / 100.);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    if (Material)
    {
        auto* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
        Dynamic->SetVectorParameterValue(TEXT("Color"),
            Kind == EFoundationTargetKind::Entity ? FLinearColor(.9, .35, .15)
                : Interactive                     ? FLinearColor(.2, .6, .8)
                                                  : FLinearColor(.5, .6, .55));
        Mesh->SetMaterial(0, Dynamic);
    }
    auto* Text = NewObject<UTextRenderComponent>(this);
    Text->SetupAttachment(RootComponent);
    Text->RegisterComponent();
    Text->SetText(FText::FromString(Label));
    Text->SetWorldSize(18);
    Text->SetHorizontalAlignment(EHTA_Center);
    Text->SetWorldLocation({Box.GetCenter().X, Box.Min.Y - 4, Box.Max.Z + 25});
    Text->SetWorldRotation({0, -90, 0});
}

void AFoundationLab::BeginPlay()
{
    Super::BeginPlay();
    Add(TEXT("100 cm = 1 block | +X east, +Y south"), {-3000, -3000, -100}, {3000, 3000, 0});
    Add(TEXT("50 cm step"), {300, -250, 0}, {500, -50, 50});
    Add(TEXT("60 cm step"), {600, -250, 0}, {800, -50, 60});
    Add(TEXT("61 cm blocked"), {900, -250, 0}, {1100, -50, 61});
    Add(TEXT("100 cm jump"), {1200, -250, 0}, {1400, -50, 100});
    Add(TEXT("155 cm ceiling: crouch"), {300, 300, 155}, {800, 700, 210});
    Add(TEXT("gap 59 cm BLOCKED"), {-700, 300, 0}, {-600, 900, 220});
    Add(TEXT("gap wall"), {-541, 300, 0}, {-441, 900, 220});
    Add(TEXT("gap 61 cm CLEAR"), {-380, 300, 0}, {-280, 900, 220});
    Add(TEXT("raised ledge +2 blocks"), {-1500, -1000, 0}, {-1000, -500, 200});
    Add(TEXT("ledge access 50"), {-1800, -800, 0}, {-1700, -600, 50});
    Add(TEXT("ledge access100"), {-1700, -800, 0}, {-1600, -600, 100});
    Add(TEXT("ledge access150"), {-1600, -800, 0}, {-1500, -600, 150});
    Add(TEXT("block fixture: 4.5 block reach"), {500, 1000, 0}, {600, 1100, 200}, EFoundationTargetKind::Block, true);
    Add(TEXT("entity fixture: 3 block reach"), {-200, 1000, 0}, {-140, 1060, 180}, EFoundationTargetKind::Entity, true);
    Add(TEXT("occluder"), {-200, 1300, 0}, {0, 1350, 230});
    Add(TEXT("hidden entity"), {-160, 1400, 0}, {-100, 1460, 180}, EFoundationTargetKind::Entity, true);
    Add(TEXT("corner comparison"), {-600, -600, 0}, {-500, -500, 200});
    for (int32 I = -10; I <= 10; ++I)
    {
        Add(FString::Printf(TEXT("%d m"), I), {double(I * 100), -1500, -.2}, {double(I * 100 + 1), -1300, 0});
    }
}

FFoundationTarget AFoundationLab::Target(FVector Eye, FVector Direction, const UFoundationProfile& P) const
{
    FFoundationTarget Best;
    double BestDistance = FMath::Max(P.BlockReachCm, P.EntityReachCm) + 100;
    for (const auto& Item : Primitives)
    {
        const FVector End = Eye + Direction * BestDistance;
        FVector Hit, Normal;
        float Time = 0;
        if (FMath::LineExtentBoxIntersection(Item.Box, Eye, End, FVector::ZeroVector, Hit, Normal, Time))
        {
            const double Distance = (Hit - Eye).Size();
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                Best.Kind = Item.Kind;
                Best.Name = Item.Label;
                Best.Hit = Hit;
                Best.Normal = Normal;
                Best.DistanceCm = Distance;
                Best.ReachCm = Item.Kind == EFoundationTargetKind::Entity ? P.EntityReachCm : P.BlockReachCm;
                Best.bInRange = Distance < Best.ReachCm - .0001;
                Best.bInteractive = Item.bInteractive;
            }
        }
    }
    return Best;
}

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

AFoundationGameMode::AFoundationGameMode()
{
    DefaultPawnClass = AFoundationPawn::StaticClass();
    PlayerControllerClass = AFoundationController::StaticClass();
    HUDClass = nullptr;
}
