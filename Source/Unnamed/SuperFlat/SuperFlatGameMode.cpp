#include "SuperFlat/SuperFlatGameMode.h"
#include "SuperFlat/SuperFlatPreviewController.h"
#include "SuperFlat/SuperFlatPreviewHUD.h"
#include "GameFramework/SpectatorPawn.h"

ASuperFlatGameMode::ASuperFlatGameMode()
{
    PlayerControllerClass = ASuperFlatPreviewController::StaticClass();
    DefaultPawnClass = ASpectatorPawn::StaticClass();
    HUDClass = ASuperFlatPreviewHUD::StaticClass();
}
