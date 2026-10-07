#include "SuperFlatWorld.h"

#include "MenuApplication.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Components/InputComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/SpectatorPawn.h"
#include "Kismet/GameplayStatics.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameViewportClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSuperFlat, Log, All);

ASuperFlatGameMode::ASuperFlatGameMode()
{
    PlayerControllerClass = ASuperFlatPreviewController::StaticClass();
    DefaultPawnClass = ASpectatorPawn::StaticClass();
    HUDClass = ASuperFlatPreviewHUD::StaticClass();
}

void ASuperFlatPreviewController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
        LocalPlayer->GetSubsystem<UUnnamedInputSubsystem>()->SetGameplayInputEnabled(false);
    bShowMouseCursor = false;
    SetIgnoreLookInput(false);
    SetIgnoreMoveInput(false);
    SetInputMode(FInputModeGameOnly());
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
    UE_LOG(LogSuperFlat, Display, TEXT("Super Flat ready: 2000 m x 2000 m; temporary stock spectator camera; Escape returns to menu"));
#if WITH_DEV_AUTOMATION_TESTS
    // Opt-in packaged integration check. Never runs in normal play or Shipping.
    if (FParse::Param(FCommandLine::Get(), TEXT("SuperFlatSmokeTest")))
    {
        FTimerHandle Timer;
        GetWorldTimerManager().SetTimer(Timer, this, &ASuperFlatPreviewController::VerifyTestWorld, 4.0f, false);
    }
#endif
}

void ASuperFlatPreviewController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ASuperFlatPreviewController::ReturnToMenu);
}

void ASuperFlatPreviewController::ReturnToMenu()
{
    if (!IsLocalController() || bReturningToMenu) return;
    bReturningToMenu = true;
    UE_LOG(LogSuperFlat, Display, TEXT("Returning to main menu"));
    UGameplayStatics::OpenLevel(this, FName(UMenuApplication::MenuMap()));
}

void ASuperFlatPreviewHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !GEngine) return;
    DrawRect(FLinearColor(0.015f, 0.022f, 0.028f, 0.8f), 16, 16, 550, 64);
    DrawText(TEXT("SUPER FLAT  |  2 km x 2 km  |  Grid: 1 m / 10 m"), FLinearColor::White, 28, 24, GEngine->GetSmallFont());
    DrawText(TEXT("Preview camera: WASD + mouse, E/Q up/down  |  Esc: Menu"), FLinearColor(0.75f, 0.85f, 0.9f), 28, 47, GEngine->GetSmallFont());
}

#if WITH_DEV_AUTOMATION_TESTS
void ASuperFlatPreviewController::VerifyTestWorld()
{
    bool bPassed = GetPawn() && GetPawn()->IsA<ASpectatorPawn>() &&
        GetWorld()->GetAuthGameMode()->IsA<ASuperFlatGameMode>() &&
        UGameplayStatics::GetCurrentLevelName(this) == TEXT("L_SuperFlat") && !bShowMouseCursor;
    int32 FloorHits = 0;
    const TArray<FVector2D> Points = {{0, 0}, {50000, 0}, {-50000, 0}, {0, 50000}, {0, -50000},
        {99900, 99900}, {99900, -99900}, {-99900, 99900}, {-99900, -99900}};
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SuperFlatFloor), false, GetPawn());
    for (const FVector2D& Point : Points)
    {
        FHitResult Hit;
        const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, FVector(Point, 1000), FVector(Point, -1000), ECC_Pawn, Query);
        if (bHit && FMath::Abs(Hit.ImpactPoint.Z) < 0.5 && Hit.ImpactNormal.Z > 0.999) ++FloorHits;
    }
    bPassed &= FloorHits == Points.Num();
    const bool bSpawnClear = !GetWorld()->OverlapBlockingTestByChannel(FVector(0, 0, 110), FQuat::Identity,
        ECC_Pawn, FCollisionShape::MakeCapsule(34, 88), Query);
    bPassed &= bSpawnClear;
    UE_LOG(LogSuperFlat, Display, TEXT("SUPER_FLAT_TEST result=%s floorHits=%d/9 spawnClear=%d map=%s pawn=%s"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"), FloorHits, bSpawnClear,
        *UGameplayStatics::GetCurrentLevelName(this), *GetNameSafe(GetPawn() ? GetPawn()->GetClass() : nullptr));
    // Aim down enough for a useful rendered overview, without changing the authored spawn.
    SetControlRotation(FRotator(-24, 35, 0));
    if (GetPawn()) GetPawn()->SetActorLocation(FVector(-1800, -1200, 1100));
    FTimerHandle CaptureTimer, ReturnTimer;
    GetWorldTimerManager().SetTimer(CaptureTimer, []()
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/SuperFlat.png"), true, false);
    }, 3.0f, false);
    GetWorldTimerManager().SetTimer(ReturnTimer, this, &ASuperFlatPreviewController::ReturnToMenu, 5.0f, false);
}
#endif
