#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MenuApplication.generated.h"

class APlayerController;
class UMenuVideoSettings;

// Restricted application lifecycle binding, separate from video-setting mutation.
UCLASS()
class UNNAMED_API UMenuApplication : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(APlayerController* InController, UMenuVideoSettings* InVideoSettings);
    void Tick();

    UFUNCTION()
    FString GetState();

    UFUNCTION()
    FString RequestQuit();

private:
    bool CanQuit() const;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<UMenuVideoSettings> VideoSettings;
    bool bQuitPending = false;
    bool bQuitIssued = false;
    double QuitDeadline = 0;
    FString LastError;
};
