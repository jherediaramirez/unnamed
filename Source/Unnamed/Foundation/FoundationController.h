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
};
