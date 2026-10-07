#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MenuApplication.generated.h"

class APlayerController;
class UMenuVideoSettings;
class UMenuInputSettings;
class UMenuPreferences;

// Restricted application lifecycle binding, separate from video-setting mutation.
UCLASS()

class UNNAMED_API UMenuApplication : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(APlayerController* InController, UMenuVideoSettings* InVideoSettings,
        UMenuInputSettings* InInputSettings = nullptr, UMenuPreferences* InPreferences = nullptr);
    void Tick();

    UFUNCTION()
    FString GetState();

    UFUNCTION()
    FString RequestQuit();

    // A catalog ID, never a browser-supplied package path or console command.
    UFUNCTION()
    FString RequestOpenWorld(const FString& WorldId);

    static const TCHAR* SuperFlatMap() { return TEXT("/Game/Maps/L_SuperFlat"); }

    static const TCHAR* MenuMap() { return TEXT("/Game/Menu/L_Menu"); }

private:
    bool CanQuit() const;
    bool CanOpenWorld() const;
    bool FlushSettings();
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<UMenuVideoSettings> VideoSettings;
    TWeakObjectPtr<UMenuInputSettings> InputSettings;
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
