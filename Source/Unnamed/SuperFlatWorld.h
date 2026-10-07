#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "SuperFlatWorld.generated.h"

// Inspection only. Replace the stock spectator pawn when the FPS character is ready.
UCLASS()
class UNNAMED_API ASuperFlatGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ASuperFlatGameMode();
};

UCLASS()
class UNNAMED_API ASuperFlatPreviewController : public APlayerController
{
    GENERATED_BODY()
public:
    void ReturnToMenu();
protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
private:
    bool bReturningToMenu = false;
#if WITH_DEV_AUTOMATION_TESTS
    void VerifyTestWorld();
#endif
};

UCLASS()
class UNNAMED_API ASuperFlatPreviewHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
