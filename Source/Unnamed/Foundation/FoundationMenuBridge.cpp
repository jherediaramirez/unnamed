#include "Foundation/FoundationMenuBridge.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationInputRouter.h"
#include "Menu/MenuBrowserHost.h"
#include "UnnamedLog.h"

FString UFoundationMenuBridge::GetState()
{
    if (!Controller.IsValid()) return TEXT("{}");
    return FString::Printf(TEXT("{\"inLab\":%s,\"capture\":%s}"),
        Controller->GetFoundationPawn() ? TEXT("true") : TEXT("false"),
        Controller->GetInputRouter()->IsCapturing() ? TEXT("true") : TEXT("false"));
}

void UFoundationMenuBridge::SetCapture(bool bActive)
{
    if (Controller.IsValid()) Controller->GetInputRouter()->SetCapture(bActive);
}

void UFoundationMenuBridge::Resume()
{
    if (Controller.IsValid()) Controller->ResumeFoundation();
}

void UFoundationMenuBridge::ReturnToMenu()
{
    if (Controller.IsValid()) Controller->ReturnToMenu();
}

FString UFoundationMenuBridge::MenuReady()
{
    if (!Controller.IsValid()) return TEXT("{\"shown\":false}");
    UMenuBrowserHost* Host = Controller->GetMenuHost();
    const bool bShown = Host->HandlePageReady();
    UE_LOG(LogUnnamedFoundation, Display, TEXT("Pause menu ready: %.1f ms after %s; %s"),
        (Host->GetPageReadySeconds() - Host->GetStartSeconds()) * 1000, bShown ? TEXT("Show") : TEXT("Prewarm"),
        bShown ? TEXT("showing") : TEXT("hidden until ESC"));
    return FString::Printf(TEXT("{\"shown\":%s}"), bShown ? TEXT("true") : TEXT("false"));
}

void UFoundationMenuBridge::MenuShown(const FString& State)
{
    if (!Controller.IsValid()) return;
    UMenuBrowserHost* Host = Controller->GetMenuHost();
    Host->HandlePageShown(State);
    UE_LOG(LogUnnamedFoundation, Display, TEXT("Pause menu shown: %.1f ms after Show; %s"),
        (FPlatformTime::Seconds() - Host->GetStartSeconds()) * 1000, *State);
}
