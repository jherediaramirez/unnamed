#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MenuPlayerController.generated.h"

class UMenuBrowserHost;

// Controller for the menu map: no pawn, just the offline menu shown on BeginPlay.
UCLASS()
class UNNAMED_API AMenuPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AMenuPlayerController();

    UMenuBrowserHost* GetMenuHost() const { return MenuHost; }

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UMenuBrowserHost> MenuHost;
};
