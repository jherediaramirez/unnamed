#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MenuVideoSettings.generated.h"

class SWindow;

// Restricted video-setting binding. No console or arbitrary settings API.
UCLASS()
class UNNAMED_API UMenuVideoSettings : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(bool bAllowNativeChanges);
    void Tick();

    bool IsReadyForExit() const { return !bWaitingForWindow && !bTransitionPending; }

    bool FlushForExit(FString& OutError);

    UFUNCTION()
    FString GetState();

    UFUNCTION()
    FString ApplySetting(const FString& Name, const FString& ValueJson);

    UFUNCTION()
    FString HardReset();

private:
    TSharedPtr<SWindow> GetWindow() const;
    bool SetFullscreen(bool bEnabled);
    bool SetFrameLimit(int32 Value);
    bool SetVSync(bool bEnabled);
    void Load();
    bool Save();
    FString Snapshot(bool bSuccess = true) const;

    bool bNativeAllowed = false;
    bool bWaitingForWindow = false;
    bool bPersistenceEnabled = false;
    bool bLoadedSettings = false;
    bool bTransitionPending = false;
    bool bTargetFullscreen = false;
    bool bPreviousFullscreen = false;
    bool bSavedFullscreen = false;
    bool bVSync = true;
    int32 FrameLimit = 120;
    int32 GuiScale = 0;
    int32 BackgroundBlur = 5;
    FIntPoint WindowedSize = FIntPoint(1280, 720);
    FVector2D WindowedPosition = FVector2D::ZeroVector;
    double TransitionDeadline = 0;
    double SampleStart = 0;
    double MeasuredFPS = 0;
    uint32 SampleFrames = 0;
    FString SettingsFile;
    FString LastError;
};
