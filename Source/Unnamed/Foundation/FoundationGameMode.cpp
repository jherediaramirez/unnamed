#include "Foundation/FoundationGameMode.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationPawn.h"

AFoundationGameMode::AFoundationGameMode()
{
    DefaultPawnClass = AFoundationPawn::StaticClass();
    PlayerControllerClass = AFoundationController::StaticClass();
    HUDClass = nullptr;
}
