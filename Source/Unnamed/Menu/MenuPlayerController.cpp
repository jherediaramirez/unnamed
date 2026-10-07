#include "Menu/MenuPlayerController.h"
#include "Menu/MenuBrowserHost.h"

AMenuPlayerController::AMenuPlayerController()
{
    MenuHost = CreateDefaultSubobject<UMenuBrowserHost>(TEXT("MenuHost"));
    PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AMenuPlayerController::BeginPlay()
{
    Super::BeginPlay();
    MenuHost->Show();
}
