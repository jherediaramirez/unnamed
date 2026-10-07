#include "Menu/MenuGameMode.h"
#include "Menu/MenuPlayerController.h"

AMenuGameMode::AMenuGameMode()
{
    PlayerControllerClass = AMenuPlayerController::StaticClass();
    DefaultPawnClass = nullptr;
    HUDClass = nullptr;
}
