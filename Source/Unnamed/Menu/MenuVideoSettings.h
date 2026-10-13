#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MenuVideoSettings.generated.h"

class SWindow;

// First-run values; Hard Reset restores them.
namespace MenuVideoDefaults
{
    constexpr bool bVSync = true;
    constexpr int32 FrameLimit = 120;
    constexpr int32 GuiScale = 0; // 0 = Auto
    constexpr int32 BackgroundBlur = 5;
}

// Restricted video-setting binding. No console or arbitrary settings API.
UCLASS()
class UNNAMED_API UMenuVideoSettings : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(bool bAllowNativeChanges);
    void Tick();
    // The kept title browser's settings (menu.Title.Keep): Reinitialize gives what a fresh object's Initialize would
    // (the menu map's); ReloadSaved only re-reads the saved values over the defaults, with no window or frame-rate
    // change, for a title repainted inside a world.
    void Reinitialize(bool bAllowNativeChanges);
    void ReloadSaved();
    // Successful saves by any instance in this process: a world's Options change the kept title's look.
    static uint32 GetSaveCount();

    // Shared read-only native view; zero is Auto. HUD and menu resolve their own minimum viewport.
    int32 GetGuiScale() const { return GuiScale; }
    // Menu Background Blur, 0-10 (the in-world pause menu's blur strength).
    int32 GetBackgroundBlur() const { return BackgroundBlur; }

    bool IsReadyForExit() const { return !bWaitingForWindow && !bTransitionPending; }

    bool FlushForExit(FString& OutError);

    UFUNCTION()
    FString GetState();

    UFUNCTION()
    FString ApplySetting(const FString& Name, const FString& ValueJson);

    UFUNCTION()
    FString HardReset();

    // Native shortcut (key.fullscreen): the same validated, saved request as the menu toggle.
    FString ToggleFullscreen();

private:
    TSharedPtr<SWindow> GetWindow() const;
    bool SetFullscreen(bool bEnabled);
    bool SetFrameLimit(int32 Value);
    bool SetVSync(bool bEnabled);
    void Load();
    bool Save();
    // The saved values back at their first-run defaults.
    void ResetSavedValues();
    FString Snapshot(bool bSuccess = true) const;

    bool bNativeAllowed = false;
    bool bWaitingForWindow = false;
    bool bPersistenceEnabled = false;
    bool bLoadedSettings = false;
    bool bTransitionPending = false;
    bool bTargetFullscreen = false;
    bool bPreviousFullscreen = false;
    bool bSavedFullscreen = false;
    bool bVSync = MenuVideoDefaults::bVSync;
    int32 FrameLimit = MenuVideoDefaults::FrameLimit;
    int32 GuiScale = MenuVideoDefaults::GuiScale;
    int32 BackgroundBlur = MenuVideoDefaults::BackgroundBlur;
    FIntPoint WindowedSize = FIntPoint(1280, 720);
    FVector2D WindowedPosition = FVector2D::ZeroVector;
    double TransitionDeadline = 0;
    double SampleStart = 0;
    double MeasuredFPS = 0;
    uint32 SampleFrames = 0;
    FString SettingsFile;
    FString LastError;
};
