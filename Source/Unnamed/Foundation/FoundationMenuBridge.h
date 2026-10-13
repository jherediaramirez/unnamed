#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FoundationMenuBridge.generated.h"

class AFoundationController;

// Browser binding "foundationmenu" (JS FoundationMenu): lets the shared menu act as the
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

    // A #pause page (foundation.Pause.Menu 1 and 2): its pause screen and panorama have painted, two frames ago.
    // {"shown": bool}: whether the menu is shown, so a pre-warmed page stays hidden and a shown one shows itself.
    UFUNCTION()
    FString MenuReady();

    // A #pause page: its first frame after being shown, once the settings stores have re-read native state. State is
    // what the page shows ({"guiScale": n, "jump": the key bound to key.jump}).
    UFUNCTION()
    void MenuShown(const FString& State);

private:
    TWeakObjectPtr<AFoundationController> Controller;
};
