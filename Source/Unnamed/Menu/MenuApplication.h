#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Worlds/UnnamedSavedWorlds.h"
#include "MenuApplication.generated.h"

class APlayerController;
class UMenuVideoSettings;
class UMenuInputBridge;
class UMenuPreferences;

// Restricted application lifecycle binding, separate from video-setting mutation: quit, world travel and the saved
// worlds of Create New World. Every call returns the whole state.
UCLASS()
class UNNAMED_API UMenuApplication : public UObject
{
    GENERATED_BODY()
public:
    // Also reads the saved worlds once; the page polls GetState, which lists them from memory.
    void Initialize(APlayerController* InController, UMenuVideoSettings* InVideoSettings,
        UMenuInputBridge* InInputBridge = nullptr, UMenuPreferences* InPreferences = nullptr);
    void Tick();

    UFUNCTION()
    FString GetState();

    UFUNCTION()
    FString RequestQuit();

    // A UUnnamedProjectSettings world id or a saved world's id, never a browser-supplied package path or console
    // command. A saved world opens the generated map with ?VoxelWorld=<id>.
    UFUNCTION()
    FString RequestOpenWorld(const FString& WorldId);

    // Creates a world from the Create screen's draft (JSON), saves it and opens it.
    UFUNCTION()
    FString CreateWorld(const FString& DraftJson);

    // Deletes a saved world's folder for good.
    UFUNCTION()
    FString DeleteWorld(const FString& WorldId);

    // Renames a saved world (Edit World's Save); its id stays.
    UFUNCTION()
    FString RenameWorld(const FString& WorldId, const FString& Name);

private:
    bool CanQuit() const;
    bool CanOpenWorld() const;
    bool FlushSettings();
    void RefreshSavedWorlds();
    // Queues travel once settings are flushed (Tick); a request while one is pending is ignored.
    void QueueTravel(const FString& Map, const FString& Options, const FString& Label);
    TArray<FUnnamedSavedWorld> SavedWorlds;
    FString PendingOptions;
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
