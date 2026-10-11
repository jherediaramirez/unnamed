#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationInputRouter.h"
#include "Foundation/FoundationMenuBridge.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationPawn.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuPreferences.h"
#include "Menu/MenuVideoSettings.h"
#include "UnnamedProjectSettings.h"
#include "UnnamedLog.h"
#include "Foundation/FoundationLab.h"
#include "Voxel/VoxelFlyCamera.h"
#include "Dom/JsonValue.h"
#include "EnhancedInputComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerInput.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

AFoundationController::AFoundationController()
{
    MenuHost = CreateDefaultSubobject<UMenuBrowserHost>(TEXT("MenuHost"));
    InputRouter = CreateDefaultSubobject<UFoundationInputRouter>(TEXT("InputRouter"));
    Hud = CreateDefaultSubobject<UFoundationHud>(TEXT("Hud"));
    Panels = CreateDefaultSubobject<UFoundationPanels>(TEXT("Panels"));
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
}

AFoundationPawn* AFoundationController::GetFoundationPawn() const
{
    return Cast<AFoundationPawn>(GetPawn());
}

UUnnamedInputSubsystem* AFoundationController::GetInputSubsystem() const
{
    return GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>() : nullptr;
}

bool AFoundationController::IsGameplay() const
{
    return GetFoundationPawn() && !MenuHost->IsOpen() && !Panels->IsOpen() && !InputRouter->IsCapturing() &&
        !Hud->IsLoadingScreenShown();
}

void AFoundationController::BeginPlay()
{
    Super::BeginPlay();
    if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem())
    {
        InputSubsystem->OnAction.AddUObject(this, &ThisClass::HandleAction);
        InputSubsystem->OnIntentReset.AddUObject(this, &ThisClass::CancelActions);
    }
    // The pause menu is the same offline menu plus this world's bridge.
    MenuBridge = NewObject<UFoundationMenuBridge>(this);
    MenuBridge->Initialize(this);
    MenuHost->AddBridge(TEXT("foundationmenu"), MenuBridge);
    MenuHost->GetVideoSettings();
    ResumeFoundation();
}

void AFoundationController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem())
    {
        InputSubsystem->OnAction.RemoveAll(this);
        InputSubsystem->OnIntentReset.RemoveAll(this);
    }
    Super::EndPlay(EndPlayReason);
}

void AFoundationController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // One fixed order per frame: the loading screen, timing, pawn-driven updates, then the throttled HUD refresh.
    UpdateLoadingScreen();
    Hud->BeginFrame();
    if (AFoundationPawn* FoundationPawn = GetFoundationPawn())
    {
        // The Playground's pawn updates the held item itself (its equipment seam), and its interaction
        // is the one held-action owner; the token fixture and its repeat serve only the Foundation lab.
        const bool bInteraction = FoundationPawn->HasInteraction();
        if (!bInteraction)
        {
            // Tokens are one item type; switching between token and empty hands restarts attack recovery.
            static const FName Token(TEXT("token"));
            FoundationPawn->Vitals.UpdateMainHand(Actions.Slots[Actions.Slot] > 0 ? Token : NAME_None);
        }
        Hud->NoteMotion(*FoundationPawn);
        Hud->NoteVitals(FoundationPawn->Vitals);
        InputRouter->ReleaseWheelPulses(FoundationPawn->Simulation.Steps);
        if (bInteraction)
            NoteInteraction(*FoundationPawn);
        else
            UpdateHeldActions(*FoundationPawn);
        Hud->DrawDebug(*FoundationPawn);
    }
    Hud->Refresh();
}

void AFoundationController::UpdateLoadingScreen()
{
    constexpr double TimeoutSeconds = 15;
    AFoundationPawn* FoundationPawn = GetFoundationPawn();
    const AFoundationLab* Lab = FoundationPawn ? FoundationPawn->GetLab() : nullptr;
    if (!Lab) return;
    bool bShowing = Hud->IsLoadingScreenShown();
    FFoundationLoadState State;
    Lab->GetLoadState(State, bShowing);
    // After the first load only a world started again (the player no longer placed) shows it: a flight or a moved view
    // that is merely not on screen yet does not.
    if (!bLoadingDone && !bShowing && !State.bReady && (!bLoadedOnce || !State.bPlayerPlaced))
    {
        // Not ready: show the screen (a world started again by voxel.Seed shows it again) with a fresh timer.
        Lab->GetLoadState(State, true);
        Hud->ShowLoadingScreen(State);
        // No game viewport (a headless run): nothing to show, so nothing is held either.
        if (!Hud->IsLoadingScreenShown()) return;
        LoadingStart = FPlatformTime::Seconds();
        bShowing = true;
        UE_LOG(LogUnnamedFoundation, Display, TEXT("Loading screen shown"));
    }
    if (bShowing)
    {
        const double Elapsed = FPlatformTime::Seconds() - LoadingStart;
        const bool bTimeout = !State.bReady && Elapsed >= TimeoutSeconds;
        if (State.bReady || bTimeout)
        {
            Hud->HideLoadingScreen();
            bShowing = false;
            bLoadingDone = true;
            bLoadedOnce = true;
            UE_LOG(LogUnnamedFoundation, Display, TEXT("Loading screen hidden after %d ms (%s)"),
                FMath::RoundToInt(Elapsed * 1000.), State.bReady ? TEXT("ready") : TEXT("timeout"));
            // A flight's fly camera froze the player and its input itself; the pause menu and panels own theirs.
            if (!AVoxelFlyCamera::Find(GetWorld()) && !MenuHost->IsOpen() && !Panels->IsOpen()) ResumeFoundation();
            bHoldForPlacement = !State.bPlayerPlaced;
        }
        else
        {
            Hud->UpdateLoadingScreen(State);
            bHoldForPlacement = true;
        }
    }
    else if (State.bReady)
        bLoadingDone = false;
    if (bHoldForPlacement)
    {
        if (State.bPlayerPlaced && !bShowing)
        {
            bHoldForPlacement = false;
            if (IsGameplay()) FoundationPawn->SetSimulationEnabled(true);
        }
        else
        {
            // Nothing may act under the screen, or on an unplaced player: input and simulation stay off.
            if (bShowing)
                if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem();
                    InputSubsystem && InputSubsystem->IsGameplayEnabled())
                    InputSubsystem->SetGameplayInputEnabled(false);
            FoundationPawn->SetSimulationEnabled(false);
        }
    }
}

void AFoundationController::SetupInputComponent()
{
    Super::SetupInputComponent();
    if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem())
        InputSubsystem->BindGameplayInput(Cast<UEnhancedInputComponent>(InputComponent));
}

void AFoundationController::OnPossess(APawn* NewPawn)
{
    Super::OnPossess(NewPawn);
    if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem()) InputSubsystem->ClearIntent();
}

void AFoundationController::OnUnPossess()
{
    if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem()) InputSubsystem->SetGameplayInputEnabled(false);
    Super::OnUnPossess();
}

bool AFoundationController::HandleInterfaceAction(const FString& Name)
{
    if (Name == TEXT("key.debug.debugOptions"))
        Panels->OpenDebugOptions();
    else if (Name == TEXT("key.debug.showHitboxes"))
        Hud->bShowCollision = !Hud->bShowCollision;
    else if (Name == TEXT("key.debug.clearChat"))
    {
        Hud->ClearEvents();
        Hud->LogEvent(TEXT("local history cleared"));
    }
    else if (Name == TEXT("key.debug.copyLocation"))
        Hud->CopySnapshot();
    else if (Name == TEXT("key.debug.dumpVersion"))
        Hud->LogEvent(TEXT("Java26.3 protocol777 build2026-09-15 / UE5.8.3 CL58210709"));
    else if (Name == TEXT("key.screenshot"))
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ProjectSavedDir() / TEXT("Screenshots/Foundation.png"), true, true);
        Hud->LogEvent(TEXT("screenshot requested"));
    }
    else if (Name == TEXT("key.fullscreen"))
        MenuHost->GetVideoSettings()->ToggleFullscreen();
    else if (Name == TEXT("key.toggleGui"))
        Hud->bHideHud = !Hud->bHideHud;
    else if (Name == TEXT("key.togglePerspective") && GetFoundationPawn())
        GetFoundationPawn()->SetPerspective((GetFoundationPawn()->GetPerspective() + 1) % 3);
    else if (Name == TEXT("key.chat") || Name == TEXT("key.command"))
        Panels->OpenText(Name == TEXT("key.command"));
    else if (Name == TEXT("key.inventory"))
        Panels->OpenInventory();
    else if (Name.StartsWith(TEXT("key.hotbar.")))
        Actions.Select(FCString::Atoi(*Name.Right(1)) - 1);
    else
        return false;
    return true;
}

void AFoundationController::HandleAction(FName Id, bool bDown)
{
    const FString Name = Id.ToString();
    LastLogical = Name + (bDown ? TEXT(" press") : TEXT(" release"));
    const bool bAttack = Name == TEXT("key.attack");
    const bool bUse = Name == TEXT("key.use");
    if (bAttack || bUse)
    {
        // Toggle Attack/Use: each press flips the held state; releases are ignored.
        const auto* Preferences = GetGameInstance()->GetSubsystem<UMenuPreferences>();
        const auto Policy = Preferences->FindPreference(bAttack ? TEXT("toggleAttack") : TEXT("toggleUse"));
        if (Policy.IsValid() && Policy->AsBool())
        {
            if (!bDown) return;
            bDown = !HeldActions.Contains(Id);
        }
    }
    if (bDown)
        HeldActions.Add(Id);
    else
        HeldActions.Remove(Id);
    if (bDown) Hud->LogEvent(LastLogical);
    AFoundationPawn* FoundationPawn = GetFoundationPawn();
    if (FoundationPawn && FoundationPawn->HasInteraction() && HandleInteractionAction(*FoundationPawn, Name, bDown))
        return;
    if (bDown && HandleInterfaceAction(Name)) return;
    if (!FoundationPawn) return;
    FFoundationActionRequest Request;
    Request.Target = FoundationPawn->TraceTarget();
    Request.Phase = bDown ? EFoundationPhase::Press : EFoundationPhase::Release;
    Request.bCrouch = FoundationPawn->LastIntent.bCrouch;
    Request.bControl = InputRouter->IsKeyDown(EKeys::LeftControl) || InputRouter->IsKeyDown(EKeys::RightControl);
    if (bAttack)
        Request.Action = EFoundationAction::Attack;
    else if (bUse)
    {
        Request.Action = EFoundationAction::Use;
        if (bDown) NextUseStep = FoundationPawn->Simulation.Steps + 4;
    }
    else if (Name == TEXT("key.pickItem"))
        Request.Action = EFoundationAction::Pick;
    else if (Name == TEXT("key.drop"))
        Request.Action = EFoundationAction::Drop;
    else if (Name == TEXT("key.swapOffhand"))
        Request.Action = EFoundationAction::Swap;
    else
        return;
    // Only attack and use have a release phase.
    if (!bDown && !bAttack && !bUse) return;
    const bool bWasBreaking = Actions.bBreaking, bWasUsing = Actions.bUsing;
    Hud->LogEvent(Actions.Dispatch(Request).Reason);
    // Attack recovery restarts on a swing (an entity in reach, or nothing in reach) and on an abandoned break.
    const bool bSwing =
        bDown && !bWasUsing && (!Request.Target.bInRange || Request.Target.Kind == EFoundationTargetKind::Entity);
    if (bAttack && (bSwing || (!bDown && bWasBreaking))) FoundationPawn->Vitals.ResetAttackStrength();
}

bool AFoundationController::HandleInteractionAction(AFoundationPawn& FoundationPawn, const FString& Name, bool bDown)
{
    if (Name == TEXT("key.attack") || Name == TEXT("key.use"))
    {
        // Logical held state (Hold/Toggle resolved above); the interaction times everything else.
        const EFoundationInput Input = Name == TEXT("key.attack") ? EFoundationInput::Attack : EFoundationInput::Use;
        if (bDown)
            FoundationPawn.PressInteraction(Input);
        else
            FoundationPawn.ReleaseInteraction(Input);
        return true;
    }
    const bool bHotbar = Name.StartsWith(TEXT("key.hotbar."));
    const bool bWorldAction =
        Name == TEXT("key.pickItem") || Name == TEXT("key.drop") || Name == TEXT("key.swapOffhand");
    if (!bHotbar && !bWorldAction) return false;
    if (!bDown) return true;
    if (bHotbar)
        FoundationPawn.SelectInteractionSlot(FCString::Atoi(*Name.Right(1)) - 1);
    else if (Name == TEXT("key.pickItem"))
        FoundationPawn.PickInteractionBlock();
    else if (Name == TEXT("key.drop"))
        FoundationPawn.DropInteractionItem(
            InputRouter->IsKeyDown(EKeys::LeftControl) || InputRouter->IsKeyDown(EKeys::RightControl));
    else
        FoundationPawn.SwapInteractionHands();
    Hud->LogEvent(FoundationPawn.Interaction.LastText);
    return true;
}

void AFoundationController::NoteInteraction(const AFoundationPawn& FoundationPawn)
{
    const FFoundationAuthority* Authority = FoundationPawn.GetAuthority();
    if (!Authority) return;
    static const TCHAR* Kinds[] = {TEXT("placed"), TEXT("broke"), TEXT("hit"), TEXT("whiff"), TEXT("rejected"),
        TEXT("interacted"), TEXT("tool broke"), TEXT("target down"), TEXT("lab reset"), TEXT("popped")};
    for (const FFoundationEvent& Event : Authority->GetEvents())
    {
        if (Event.Serial <= LastInteractionEvent) continue;
        LastInteractionEvent = Event.Serial;
        FString Text =
            FString::Printf(TEXT("%s %s"), Kinds[int32(Event.Kind)], *FoundationGrid::DescribeCell(Event.Cell));
        if (Event.Kind == EFoundationEventKind::Hit)
            Text = FString::Printf(TEXT("hit target #%d for %.2f%s"), Event.EntityId, Event.Amount,
                Event.bCritical ? TEXT(" CRITICAL") : TEXT(""));
        else if (Event.Kind == EFoundationEventKind::Rejected)
            Text += FString(TEXT(": ")) + LexToString(Event.Reason);
        else if (Event.Kind == EFoundationEventKind::Interacted)
            Text += TEXT(" ") + Event.Interaction.ToString();
        // Held repeats can refuse the same thing every step; log a repeat once.
        if (Text == LastInteractionText) continue;
        LastInteractionText = Text;
        Hud->LogEvent(Text);
    }
}

void AFoundationController::UpdateHeldActions(const AFoundationPawn& FoundationPawn)
{
    const int64 Step = FoundationPawn.Simulation.Steps;
    if (!IsGameplay() || Step == LastActionStep) return;
    LastActionStep = Step;
    const FFoundationTarget Target = FoundationPawn.TraceTarget();
    if (Actions.bBreaking && Target.Name != Actions.ActiveTarget)
    {
        Actions.Cancel();
        Hud->LogEvent(TEXT("break cancelled: target changed"));
    }
    if (HeldActions.Contains(TEXT("key.attack")))
    {
        FFoundationActionRequest Request;
        Request.Action = EFoundationAction::Attack;
        Request.Phase = EFoundationPhase::Hold;
        Request.Target = Target;
        Actions.Dispatch(Request);
    }
    // Held use repeats every four reference steps.
    if (HeldActions.Contains(TEXT("key.use")) && !Actions.bUsing && Step >= NextUseStep)
    {
        NextUseStep = Step + 4;
        FFoundationActionRequest Request;
        Request.Action = EFoundationAction::Use;
        Request.Phase = EFoundationPhase::Hold;
        Request.Target = Target;
        Request.bCrouch = FoundationPawn.LastIntent.bCrouch;
        Actions.Dispatch(Request);
    }
    if (Actions.LastResult != LastActionResult)
    {
        LastActionResult = Actions.LastResult;
        Hud->LogEvent(Actions.LastResult);
    }
}

void AFoundationController::ScrollHotbar(int32 Notches)
{
    if (AFoundationPawn* FoundationPawn = GetFoundationPawn(); FoundationPawn && FoundationPawn->HasInteraction())
        FoundationPawn->ScrollInteraction(Notches);
    else
        Actions.Scroll(Notches);
    Hud->LogEvent(TEXT("wheel selected slot"));
}

void AFoundationController::CancelActions()
{
    Actions.Cancel();
    if (AFoundationPawn* FoundationPawn = GetFoundationPawn())
        FoundationPawn->CancelInteraction(EFoundationReason::Canceled);
    HeldActions.Reset();
    InputRouter->BlockHeldKeys();
    LastActionStep = 0;
    Hud->LogEvent(TEXT("input cancelled; release held keys to resume"));
}

void AFoundationController::HandleFocusChanged(bool bActive)
{
    if (bActive)
    {
        Hud->LogEvent(TEXT("window focus restored; actions require fresh press"));
        return;
    }
    CancelActions();
    if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem()) InputSubsystem->ClearIntent();
    // Closing the game window deactivates the application as well; by then the game viewport is gone and there
    // is no menu to open (opening one asserted on the missing viewport widget).
    const bool bViewport = GEngine && GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid();
    if (IsGameplay() && bViewport && !IsEngineExitRequested()) PauseFoundation();
    Hud->LogEvent(TEXT("window focus lost"));
}

void AFoundationController::PauseFoundation()
{
    if (AFoundationPawn* FoundationPawn = GetFoundationPawn()) FoundationPawn->SetSimulationEnabled(false);
    MenuHost->Show();
    Hud->LogEvent(TEXT("pause: existing menu opened"));
}

void AFoundationController::ResumeFoundation()
{
    MenuHost->Hide();
    UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem();
    if (!InputSubsystem) return;
    InputSubsystem->BindGameplayInput(Cast<UEnhancedInputComponent>(InputComponent));
    InputSubsystem->SetGameplayInputEnabled(true);
    // Engine debug execs otherwise steal F1/F5 and Shift in development games.
    // This affects only this gameplay controller, not editor viewport shortcuts.
    if (PlayerInput) PlayerInput->DebugExecBindings.Empty();
    bShowMouseCursor = false;
    ResetIgnoreMoveInput();
    ResetIgnoreLookInput();
    SetInputMode(FInputModeGameOnly());
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
    if (AFoundationPawn* FoundationPawn = GetFoundationPawn()) FoundationPawn->SetSimulationEnabled(true);
    Hud->LogEvent(TEXT("gameplay resumed: release held keys before new actions"));
}

void AFoundationController::ReturnToMenu()
{
    CancelActions();
    UGameplayStatics::OpenLevel(this, FName(UUnnamedProjectSettings::GetMenuMapPackage()));
}
