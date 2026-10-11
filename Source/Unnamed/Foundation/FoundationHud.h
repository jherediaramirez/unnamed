#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FoundationHud.generated.h"

class AFoundationController;
class AFoundationPawn;
class FFoundationAuthority;
class STextBlock;
class SWidget;
class SHudLoadingScreen;
class UHudPresenter;
struct FFoundationVitals;
struct FFoundationLoadState;

// The F3 debug overlay, native Hud presenter, recent-event log and debug
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
    // Logs each applied damage, death and respawn once.
    void NoteVitals(const FFoundationVitals& Vitals);
    void DrawDebug(const AFoundationPawn& Pawn) const;
    // F3 text stays at 10 Hz. The separate presentation adapter samples at up to 20 Hz.
    void Refresh();

    // The world's loading screen, above the HUD (Z-order 300; the pause menu's CEF is at 100).
    void ShowLoadingScreen(const FFoundationLoadState& State);
    void UpdateLoadingScreen(const FFoundationLoadState& State);
    void HideLoadingScreen();
    bool IsLoadingScreenShown() const { return LoadingScreen.IsValid(); }

    bool bOverlay = false;
    bool bHideHud = false;
    bool bShowCollision = false;
    UHudPresenter* GetPresenter() const { return Presenter; }

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    AFoundationController* GetFoundationController() const;
    // The Playground's F3 lines: lab rules, hands, target and preview, timing and target health.
    static FString BuildInteractionText(const AFoundationPawn& Pawn, const FFoundationAuthority& Authority);

    TSharedPtr<SWidget> OverlayRoot;
    TSharedPtr<SHudLoadingScreen> LoadingScreen;
    TSharedPtr<STextBlock> DebugText;
    UPROPERTY(Transient)
    TObjectPtr<UHudPresenter> Presenter;
    TArray<FString> Events;
    FString LastMotionState;
    int32 LoggedDamageEvents = 0;
    bool bLoggedDead = false;
    double Now = 0;
    double LastFrameTime = 0;
    double FrameMs = 0;
    double NextRefreshTime = 0;
};
