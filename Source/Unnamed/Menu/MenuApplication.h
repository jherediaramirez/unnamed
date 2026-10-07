#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MenuApplication.generated.h"

class APlayerController;
class UMenuVideoSettings;
class UMenuInputBridge;
class UMenuPreferences;

// Restricted application lifecycle binding, separate from video-setting mutation.
UCLASS()
class UNNAMED_API UMenuApplication : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(APlayerController* InController, UMenuVideoSettings* InVideoSettings,
        UMenuInputBridge* InInputBridge = nullptr, UMenuPreferences* InPreferences = nullptr);
    void Tick();

    UFUNCTION()
    FString GetState();

    UFUNCTION()
    FString RequestQuit();

    // A UUnnamedProjectSettings world ID, never a browser-supplied package path or console command.
    UFUNCTION()
    FString RequestOpenWorld(const FString& WorldId);

private:
    bool CanQuit() const;
    bool CanOpenWorld() const;
    bool FlushSettings();
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<UMenuVideoSettings> VideoSettings;
    TWeakObjectPtr<UMenuInputBridge> InputBridge;
    TWeakObjectPtr<UMenuPreferences> Preferences;
    bool bQuitPending = false;
    bool bQuitIssued = false;
    double QuitDeadline = 0;
    bool bWorldPending = false;
    bool bTravelIssued = false;
    double WorldDeadline = 0;
    FString PendingMap;
    FString LastError;
};
