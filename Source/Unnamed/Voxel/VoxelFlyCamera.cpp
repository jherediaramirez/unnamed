#include "Voxel/VoxelFlyCamera.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Foundation/FoundationProfile.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Input/UnnamedInputSubsystem.h"
#include "InputCoreTypes.h"

namespace
{
    // Degrees of turn per unit of raw mouse movement.
    constexpr double FlyLookDegrees = 0.15;
    // Left Shift's speed multiplier.
    constexpr double FlyBoost = 4.;

    // The player stands still while the camera flies: its simulation stops (the pawn simulates in its own Tick) and
    // gameplay actions are off, so the keys move only the camera.
    void SetFlyPlayerFrozen(APlayerController* Controller, bool bFrozen)
    {
        if (!Controller) return;
        if (APawn* Pawn = Controller->GetPawn()) Pawn->SetActorTickEnabled(!bFrozen);
        if (const ULocalPlayer* Player = Controller->GetLocalPlayer())
            if (UUnnamedInputSubsystem* Input = Player->GetSubsystem<UUnnamedInputSubsystem>())
            {
                Input->ClearIntent();
                Input->SetGameplayInputEnabled(!bFrozen);
            }
    }
}

AVoxelFlyCamera::AVoxelFlyCamera()
{
    PrimaryActorTick.bCanEverTick = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    RootComponent = Camera;
}

AVoxelFlyCamera* AVoxelFlyCamera::Find(const UWorld* World)
{
    if (!World) return nullptr;
    for (TActorIterator<AVoxelFlyCamera> It(const_cast<UWorld*>(World)); It; ++It)
        return *It;
    return nullptr;
}

AVoxelFlyCamera* AVoxelFlyCamera::Toggle(UWorld* World, double InBlocksPerSecond)
{
    if (!World) return nullptr;
    if (AVoxelFlyCamera* Existing = Find(World))
    {
        // A new speed while flying keeps flying.
        if (InBlocksPerSecond > 0)
        {
            Existing->BlocksPerSecond = InBlocksPerSecond;
            return Existing;
        }
        Existing->Destroy();
        return nullptr;
    }
    APlayerController* PlayerController = World->GetFirstPlayerController();
    if (!PlayerController || !PlayerController->PlayerCameraManager) return nullptr;
    const APlayerCameraManager& View = *PlayerController->PlayerCameraManager;
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AVoxelFlyCamera* Fly = World->SpawnActor<AVoxelFlyCamera>(View.GetCameraLocation(),
        FRotator(View.GetCameraRotation().Pitch, View.GetCameraRotation().Yaw, 0), Parameters);
    if (!Fly) return nullptr;
    Fly->Camera->SetFieldOfView(View.GetFOVAngle());
    if (InBlocksPerSecond > 0) Fly->BlocksPerSecond = InBlocksPerSecond;
    Fly->Controller = PlayerController;
    PlayerController->SetViewTarget(Fly);
    SetFlyPlayerFrozen(PlayerController, true);
    return Fly;
}

void AVoxelFlyCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // The view and the player go back as they were.
    if (APlayerController* PlayerController = Controller.Get())
    {
        SetFlyPlayerFrozen(PlayerController, false);
        if (PlayerController->GetViewTarget() == this && PlayerController->GetPawn())
            PlayerController->SetViewTarget(PlayerController->GetPawn());
    }
    Super::EndPlay(EndPlayReason);
}

void AVoxelFlyCamera::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const APlayerController* PlayerController = Controller.Get();
    if (bScripted || !PlayerController) return;
    float MouseX = 0, MouseY = 0;
    PlayerController->GetInputMouseDelta(MouseX, MouseY);
    FRotator Rotation = GetActorRotation();
    Rotation.Yaw += MouseX * FlyLookDegrees;
    Rotation.Pitch = FMath::Clamp(Rotation.Pitch + MouseY * FlyLookDegrees, -89., 89.);
    Rotation.Roll = 0;
    SetActorRotation(Rotation);

    const FRotationMatrix Axes(Rotation);
    FVector Move = FVector::ZeroVector;
    auto Held = [PlayerController](const FKey& Key) { return PlayerController->IsInputKeyDown(Key) ? 1. : 0.; };
    Move += Axes.GetUnitAxis(EAxis::X) * (Held(EKeys::W) - Held(EKeys::S));
    Move += Axes.GetUnitAxis(EAxis::Y) * (Held(EKeys::D) - Held(EKeys::A));
    Move.Z += Held(EKeys::SpaceBar) - Held(EKeys::LeftControl);
    if (Move.IsNearlyZero()) return;
    const double Speed = BlocksPerSecond * (Held(EKeys::LeftShift) ? FlyBoost : 1.) * UFoundationProfile::BlockCm;
    SetActorLocation(GetActorLocation() + Move.GetSafeNormal() * Speed * DeltaSeconds);
}
