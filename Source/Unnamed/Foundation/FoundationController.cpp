#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationInputRouter.h"
#include "Foundation/FoundationMenuBridge.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationPawn.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuPreferences.h"
#include "Menu/MenuVideoSettings.h"
#include "UnnamedProjectSettings.h"
#include "Dom/JsonValue.h"
#include "EnhancedInputComponent.h"
#include "Engine/GameInstance.h"
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
    return GetFoundationPawn() && !MenuHost->IsOpen() && !Panels->IsOpen() && !InputRouter->IsCapturing();
}

void AFoundationController::BeginPlay()
{
    Super::BeginPlay();
    if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem())
    {
        InputSubsystem->ActionSignal.AddUObject(this, &ThisClass::HandleAction);
        InputSubsystem->ResetSignal.AddUObject(this, &ThisClass::CancelActions);
    }
    // The pause menu is the same offline menu plus this world's bridge.
    MenuBridge = NewObject<UFoundationMenuBridge>(this);
    MenuBridge->Initialize(this);
    MenuHost->AddBridge(TEXT("foundation"), MenuBridge);
    MenuHost->GetVideoSettings();
    ResumeFoundation();
}

void AFoundationController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem())
    {
        InputSubsystem->ActionSignal.RemoveAll(this);
        InputSubsystem->ResetSignal.RemoveAll(this);
    }
    Super::EndPlay(EndPlayReason);
}

void AFoundationController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // One fixed order per frame: timing, pawn-driven updates, then the throttled HUD refresh.
    Hud->BeginFrame();
    if (const AFoundationPawn* FoundationPawn = GetFoundationPawn())
    {
        Hud->NoteMotion(*FoundationPawn);
        InputRouter->ReleaseWheelPulses(FoundationPawn->Simulation.Steps);
        UpdateHeldActions(*FoundationPawn);
        Hud->DrawDebug(*FoundationPawn);
    }
    Hud->Refresh();
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
    if (bDown)
    {
        Hud->LogEvent(LastLogical);
        if (HandleInterfaceAction(Name)) return;
    }

    AFoundationPawn* FoundationPawn = GetFoundationPawn();
    if (!FoundationPawn) return;
    FFoundationActionRequest Request;
    Request.Target = FoundationPawn->Target();
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
    if (bDown || bAttack || bUse) Hud->LogEvent(Actions.Dispatch(Request).Reason);
}

void AFoundationController::UpdateHeldActions(const AFoundationPawn& FoundationPawn)
{
    const int64 Step = FoundationPawn.Simulation.Steps;
    if (!IsGameplay() || Step == LastActionStep) return;
    LastActionStep = Step;
    const FFoundationTarget Target = FoundationPawn.Target();
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
    Actions.Scroll(Notches);
    Hud->LogEvent(TEXT("wheel selected slot"));
}

void AFoundationController::CancelActions()
{
    Actions.Cancel();
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
    if (IsGameplay()) PauseFoundation();
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
