#include "Menu/MenuPlayerController.h"
#include "UnnamedLog.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuBrowserSubsystem.h"
#include "Engine/GameInstance.h"

AMenuPlayerController::AMenuPlayerController()
{
    MenuHost = CreateDefaultSubobject<UMenuBrowserHost>(TEXT("MenuHost"));
    PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AMenuPlayerController::BeginPlay()
{
    Super::BeginPlay();
    // menu.Title.Keep: 0 a fresh browser for this map (until 2026-10-12); 1 and 2 the game's kept title browser.
    const int32 KeepMode = UMenuBrowserSubsystem::GetKeepMode();
    UE_LOG(LogUnnamedMenu, Display, TEXT("Switch menu.Title.Keep = %d (read at BeginPlay)"), KeepMode);
    if (KeepMode > 0)
    {
        MenuHost->ShowTitle(KeepMode);
        return;
    }
    // Set to 0 after a kept browser was made: back to today's fresh browser per map.
    if (UMenuBrowserSubsystem* Titles =
            GetGameInstance() ? GetGameInstance()->GetSubsystem<UMenuBrowserSubsystem>() : nullptr)
        Titles->Release();
    MenuHost->Show();
}
