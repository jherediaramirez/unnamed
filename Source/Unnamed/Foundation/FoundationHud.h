#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FoundationHud.generated.h"

class AFoundationController;
class AFoundationPawn;
class STextBlock;
class SWidget;

// The F3 debug overlay, the hotbar/help line, the crosshair, the recent-event log and debug
// drawing. Display only: it reads the controller, pawn and router and changes no game state.
// AFoundationController drives it once per frame (BeginFrame, NoteMotion, DrawDebug, Refresh).
UCLASS(ClassGroup = Foundation)
class UNNAMED_API UFoundationHud : public UActorComponent
{
    GENERATED_BODY()
public:
    void LogEvent(const FString& Text);
    void ClearEvents() { Events.Reset(); }
    void ToggleOverlay();
    // The text the overlay shows; also copied by key.debug.copyLocation.
    FString BuildSnapshot() const;
    void CopySnapshot();

    void BeginFrame();
    void NoteMotion(const AFoundationPawn& Pawn);
    void DrawDebug(const AFoundationPawn& Pawn) const;
    // Updates widget text and visibility at most ten times a second.
    void Refresh();

    bool bOverlay = false;
    bool bHideHud = false;
    bool bShowCollision = false;

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    AFoundationController* GetFoundationController() const;

    TSharedPtr<SWidget> OverlayRoot;
    TSharedPtr<STextBlock> DebugText;
    TSharedPtr<STextBlock> HudText;
    TSharedPtr<STextBlock> Crosshair;
    TArray<FString> Events;
    FString LastMotionState;
    double Now = 0;
    double LastFrameTime = 0;
    double FrameMs = 0;
    double NextRefreshTime = 0;
};
