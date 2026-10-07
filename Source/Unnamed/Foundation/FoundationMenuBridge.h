#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FoundationMenuBridge.generated.h"

class AFoundationController;

// Browser binding "foundation" (JS FoundationMenu): lets the shared menu act as the
// Foundation pause menu and tell the input router when a key binding is being captured.
UCLASS()
class UNNAMED_API UFoundationMenuBridge : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(AFoundationController* InController) { Controller = InController; }

    // {"inLab": bool, "capture": bool}
    UFUNCTION()
    FString GetState();

    UFUNCTION()
    void SetCapture(bool bActive);

    UFUNCTION()
    void Resume();

    UFUNCTION()
    void ReturnToMenu();

private:
    TWeakObjectPtr<AFoundationController> Controller;
};
