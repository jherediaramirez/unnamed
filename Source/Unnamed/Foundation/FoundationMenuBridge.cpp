#include "Foundation/FoundationMenuBridge.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationInputRouter.h"

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
