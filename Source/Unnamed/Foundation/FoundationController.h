#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Foundation/FoundationActions.h"
#include "FoundationController.generated.h"

class AFoundationPawn;
class UFoundationHud;
class UFoundationInputRouter;
class UFoundationMenuBridge;
class UFoundationPanels;
class UMenuBrowserHost;
class UUnnamedInputSubsystem;

// Player controller for the Foundation lab. It owns four parts and coordinates them:
//   UMenuBrowserHost       - the shared offline menu, shown as the pause menu;
//   UFoundationInputRouter - raw input, debug chords and binding capture;
//   UFoundationHud         - F3 overlay, native Hud presenter, event log, debug drawing;
//   UFoundationPanels      - Debug Options, chat/command text and inventory fixtures.
// The controller itself handles logical actions, pause/resume and focus loss.
UCLASS()
class UNNAMED_API AFoundationController : public APlayerController
{
    GENERATED_BODY()
public:
    AFoundationController();

    UMenuBrowserHost* GetMenuHost() const { return MenuHost; }
    UFoundationInputRouter* GetInputRouter() const { return InputRouter; }
    UFoundationHud* GetHud() const { return Hud; }
    UFoundationPanels* GetPanels() const { return Panels; }
    AFoundationPawn* GetFoundationPawn() const;
    UUnnamedInputSubsystem* GetInputSubsystem() const;

    // True while the possessed pawn plays: no menu, panel or binding capture in front of it.
    bool IsGameplay() const;

    // foundation.Load.Cover: the rendered frames the loading screen stays up once the world is ready (0 or 3).
    static int32 GetLoadCoverFrames(int32 Mode);
    // Whether the loading screen hides on the frame that counts ReadyFrames ready frames in a row.
    static bool ShouldHideLoadingScreen(int32 ReadyFrames, int32 CoverFrames) { return ReadyFrames > CoverFrames; }

    // A logical action from Enhanced Input or a debug chord, e.g. "key.attack" or "key.debug.showHitboxes".
    void HandleAction(FName Id, bool bDown);
    void ScrollHotbar(int32 Notches);
    // Drops held actions; keys still down must be released before they act again.
    void CancelActions();
    void HandleFocusChanged(bool bActive);

    void PauseFoundation();
    void ResumeFoundation();
    void ReturnToMenu();

    const FString& GetLastLogical() const { return LastLogical; }

    // Typed action test double: hotbar tokens, break/use state, last result. The Foundation lab's
    // receiver; the Playground sends the same actions to the pawn's interaction instead.
    FFoundationActions Actions;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupInputComponent() override;
    virtual void OnPossess(APawn* NewPawn) override;
    virtual void OnUnPossess() override;

private:
    // Menus, panels, HUD toggles, screenshot, fullscreen, perspective and hotbar; false for world actions.
    bool HandleInterfaceAction(const FString& Name);
    // In the Playground, world actions and the hotbar go to the pawn's interaction; false otherwise.
    bool HandleInteractionAction(AFoundationPawn& FoundationPawn, const FString& Name, bool bDown);
    // Repeats held attack/use once per simulation step for the token fixture (Foundation lab only).
    void UpdateHeldActions(const AFoundationPawn& FoundationPawn);
    // The world's loading screen (Tick, before the HUD's frame work): shown while the lab is not ready, for at most 15
    // s; gameplay input and the pawn's simulation stay off under it, and after a timeout the simulation waits for the
    // player to stand on the spawn.
    void UpdateLoadingScreen();
    // Logs the interaction authority's committed and rejected outcomes once each.
    void NoteInteraction(const AFoundationPawn& FoundationPawn);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UMenuBrowserHost> MenuHost;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFoundationInputRouter> InputRouter;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFoundationHud> Hud;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFoundationPanels> Panels;
    UPROPERTY(Transient)
    TObjectPtr<UFoundationMenuBridge> MenuBridge;

    TSet<FName> HeldActions;
    int64 NextUseStep = 0;
    int64 LastActionStep = 0;
    FString LastActionResult;
    int64 LastInteractionEvent = 0;
    FString LastInteractionText;
    FString LastLogical = TEXT("none");
    // Loading screen: when it showed (seconds), done with this load (hidden and not ready since), and the simulation
    // held off after a timeout until the player is placed.
    double LoadingStart = 0;
    bool bLoadingDone = false;
    bool bLoadedOnce = false;
    bool bHoldForPlacement = false;
    // foundation.Load.Cover, read at BeginPlay: frames to keep the screen up once ready, and the ready frames so far.
    int32 LoadCoverFrames = 0;
    int32 ReadyFrames = 0;
    // foundation.Panels.Preload, read at BeginPlay, and whether the creative screen's preload is done.
    bool bPreloadPanels = false;
    bool bPanelsPreloaded = false;
};
