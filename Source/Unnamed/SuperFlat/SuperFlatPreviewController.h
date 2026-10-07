#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SuperFlatPreviewController.generated.h"

UCLASS()
class UNNAMED_API ASuperFlatPreviewController : public APlayerController
{
    GENERATED_BODY()
public:
    void ReturnToMenu();

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;

private:
    bool bReturningToMenu = false;
#if WITH_DEV_AUTOMATION_TESTS
    void VerifyTestWorld();
#endif
};
