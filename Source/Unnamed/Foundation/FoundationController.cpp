#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationInputRouter.h"
#include "Foundation/FoundationMenuBridge.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationCommands.h"
#include "Foundation/FoundationDeath.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Hud/HudDrawList.h"
#include "Hud/HudPresenter.h"
#include "Voxel/VoxelWorld.h"
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
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "UnrealClient.h"

namespace
{
    TAutoConsoleVariable<int32> CVarPauseMenu(TEXT("foundation.Pause.Menu"), 1,
        TEXT("The ESC menu: 0 a fresh opaque browser per ESC that starts on the title screen (until 2026-10-12: about "
             "0.2-0.4 s of black, then a title frame); 1 one browser per world, made hidden when the world starts and "
             "shown at once; 2 a fresh browser per ESC that stays transparent until its pause screen has painted "
             "(diagnostic). Read when a world starts: change it, then reload the world."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarLoadCover(TEXT("foundation.Load.Cover"), 1,
        TEXT("A world's first frames: 0 the loading screen only while the world is not ready, so a world that is ready "
             "at "
             "once (the Playground) shows its first rendered frame uncovered: dark and unexposed for 66-114 ms (until "
             "2026-10-12); 1 the loading screen from the world's first frame, hidden 3 rendered frames after the world "
             "is ready. Read when a world starts."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarPanelsPreload(TEXT("foundation.Panels.Preload"), 1,
        TEXT(
            "The creative inventory's first open in a world: 0 it builds its screen and loads its textures in the open "
            "frame, a 55-72 ms frame that draws the items without the container background (until 2026-10-12); 1 the "
            "screen is built (its textures loaded) in the world's first frame with the player in its lab, and the "
            "first open shows it. Read when a world starts."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarPauseBackground(TEXT("foundation.Pause.Background"), 1,
        TEXT(
            "Behind the ESC menu (foundation.Pause.Menu 1 and 2): 0 the title screen's blurred panorama (until "
            "2026-10-12); 1 the world, blurred by Menu Background Blur (setting x viewport height / 1080 as Slate blur "
            "strength; vanilla's strength unmeasured) and darkened by inworld_menu_background, as vanilla. Read when a "
            "world starts."),
        ECVF_Default);
}

AFoundationController::AFoundationController()
{
    MenuHost = CreateDefaultSubobject<UMenuBrowserHost>(TEXT("MenuHost"));
    InputRouter = CreateDefaultSubobject<UFoundationInputRouter>(TEXT("InputRouter"));
    Hud = CreateDefaultSubobject<UFoundationHud>(TEXT("Hud"));
    Panels = CreateDefaultSubobject<UFoundationPanels>(TEXT("Panels"));
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
}

int32 AFoundationController::GetLoadCoverFrames(int32 Mode)
{
    return Mode > 0 ? 3 : 0;
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
        !Hud->IsLoadingScreenShown() && !GetFoundationPawn()->IsDeathScreenActive();
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
    MenuHost->SetPauseMode(CVarPauseMenu.GetValueOnGameThread());
    MenuHost->SetPauseBackground(CVarPauseBackground.GetValueOnGameThread());
    UE_LOG(LogUnnamedFoundation, Display,
        TEXT("Pause menu mode %d, background %d (foundation.Pause.Menu and .Background, read at BeginPlay)"),
        MenuHost->GetPauseMode(), MenuHost->GetPauseBackground());
    LoadCoverFrames = GetLoadCoverFrames(CVarLoadCover.GetValueOnGameThread());
    UE_LOG(LogUnnamedFoundation, Display, TEXT("Switch foundation.Load.Cover = %d (read at BeginPlay)"),
        LoadCoverFrames > 0 ? 1 : 0);
    bPreloadPanels = CVarPanelsPreload.GetValueOnGameThread() != 0;
    UE_LOG(LogUnnamedFoundation, Display, TEXT("Switch foundation.Panels.Preload = %d (read at BeginPlay)"),
        bPreloadPanels ? 1 : 0);
    ResumeFoundation();
    // Mode 1: the pause menu loads now, hidden, so ESC shows it at once.
    MenuHost->Prewarm();
}

void AFoundationController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem())
    {
        InputSubsystem->OnAction.RemoveAll(this);
        InputSubsystem->OnIntentReset.RemoveAll(this);
    }
    if (DeathInput && GEngine && GEngine->GameViewport)
        GEngine->GameViewport->RemoveViewportWidgetContent(DeathInput.ToSharedRef());
    DeathInput.Reset();
    Super::EndPlay(EndPlayReason);
}

void AFoundationController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // One fixed order per frame: the loading screen, timing, pawn-driven updates, then the throttled HUD refresh.
    UpdateLoadingScreen();
    UpdateDeathScreen();
    if (bPreloadPanels && !bPanelsPreloaded) bPanelsPreloaded = Panels->PreloadCreative();
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
    // that is merely not on screen yet does not. foundation.Load.Cover 1 also shows it over a world's first frames when
    // the world is ready at once.
    if (!bLoadingDone && !bShowing && (!State.bReady || (LoadCoverFrames > 0 && !bLoadedOnce)) &&
        (!bLoadedOnce || !State.bPlayerPlaced))
    {
        // Not ready: show the screen (a world started again by voxel.Seed shows it again) with a fresh timer.
        Lab->GetLoadState(State, true);
        Hud->ShowLoadingScreen(State);
        // No game viewport (a headless run): nothing to show, so nothing is held either.
        if (!Hud->IsLoadingScreenShown()) return;
        LoadingStart = FPlatformTime::Seconds();
        ReadyFrames = 0;
        bShowing = true;
        UE_LOG(LogUnnamedFoundation, Display, TEXT("Loading screen shown"));
    }
    if (bShowing)
    {
        const double Elapsed = FPlatformTime::Seconds() - LoadingStart;
        const bool bTimeout = !State.bReady && Elapsed >= TimeoutSeconds;
        ReadyFrames = State.bReady ? ReadyFrames + 1 : 0;
        if (ShouldHideLoadingScreen(ReadyFrames, LoadCoverFrames) || bTimeout)
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

void AFoundationController::UpdateDeathScreen()
{
    // Hardcore's variant ("Game Over!", Spectate World and a confirm before the title screen) is a known issue: there
    // is no hardcore world yet. Under the pause menu or the loading screen the screen waits for them.
    AFoundationPawn* FoundationPawn = GetFoundationPawn();
    // Open screens don't pause the simulation, so the player can die in one: as vanilla, death closes it (the
    // inventory, a container, the chat) and the death screen takes its place.
    if (FoundationPawn && FoundationPawn->IsDeathScreenActive() && Panels->IsOpen() && !MenuHost->IsOpen())
        Panels->Close();
    const bool bActive = FoundationPawn && FoundationPawn->IsDeathScreenActive() && !MenuHost->IsOpen() &&
        !Panels->IsOpen() && !Hud->IsLoadingScreenShown();
    if (bActive == bDeathScreen) return;
    bDeathScreen = bActive;
    if (!bActive)
    {
        if (DeathInput && GEngine && GEngine->GameViewport)
            GEngine->GameViewport->RemoveViewportWidgetContent(DeathInput.ToSharedRef());
        DeathInput.Reset();
        DeathHover = EHudDeathButton::None;
        // Respawned (or the switch turned off): play resumes. A menu or panel that took over restores input itself
        // when it closes.
        if (FoundationPawn && !MenuHost->IsOpen() && !Panels->IsOpen() && !Hud->IsLoadingScreenShown())
            ResumeFoundation();
        return;
    }
    // As the panels take input (UFoundationPanels::Show), but the world and the pawn keep stepping: vanilla's death
    // screen does not pause, and its buttons enable 20 ticks after death. Escape does nothing here (vanilla's
    // shouldCloseOnEsc is false): the input router pauses only in gameplay.
    CancelActions();
    if (UUnnamedInputSubsystem* InputSubsystem = GetInputSubsystem())
    {
        InputSubsystem->SetGameplayInputEnabled(false);
        InputSubsystem->ClearIntent();
    }
    bShowMouseCursor = true;
    FInputModeUIOnly Mode;
    if (GEngine && GEngine->GameViewport)
    {
        const TWeakObjectPtr<AFoundationController> WeakThis = this;
        auto Handler = [WeakThis](bool bPress)
        {
            return FPointerEventHandler::CreateLambda(
                [WeakThis, bPress](const FGeometry& Geometry, const FPointerEvent& Event)
                {
                    if (WeakThis.IsValid()) WeakThis->HandleDeathMouse(Geometry, Event, bPress);
                    return FReply::Handled();
                });
        };
        // clang-format off
        DeathInput = SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush(TEXT("NoBorder")))
            .Visibility(EVisibility::Visible)
            .OnMouseButtonDown(Handler(true))
            .OnMouseMove(Handler(false));
        // clang-format on
        GEngine->GameViewport->AddViewportWidgetContent(DeathInput.ToSharedRef(), 250);
        Mode.SetWidgetToFocus(DeathInput);
    }
    SetInputMode(Mode);
    Hud->LogEvent(TEXT("death screen"));
}

void AFoundationController::HandleDeathMouse(const FGeometry& Geometry, const FPointerEvent& Event, bool bPress)
{
    // Viewport pixels (the widget fills the viewport; Slate's DPI scale taken out, as SHudView draws) to the HUD's GUI
    // pixels, then the same rectangles the HUD draws.
    const int32 Scale = FMath::Max(1, Hud->GetPresenter() ? Hud->GetPresenter()->GetGuiScale() : 1);
    const float SlateScale = FMath::Max(Geometry.GetAccumulatedLayoutTransform().GetScale(), .01f);
    const FVector2f Pixels = FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())) * SlateScale;
    const FIntPoint Gui(FMath::FloorToInt32(Pixels.X / Scale), FMath::FloorToInt32(Pixels.Y / Scale));
    FIntPoint Viewport;
    GetViewportSize(Viewport.X, Viewport.Y);
    DeathHover = HudDrawList::HitDeathButton(HudModel::GetLogicalSize(Viewport, Scale), Gui);
    AFoundationPawn* FoundationPawn = GetFoundationPawn();
    // A left press on an enabled button acts (vanilla buttons click on press); before tick 20 they are disabled.
    if (!bPress || Event.GetEffectingButton() != EKeys::LeftMouseButton || !FoundationPawn ||
        !FoundationDeath::AreButtonsEnabled(FoundationPawn->GetDeathTicks()))
        return;
    if (DeathHover == EHudDeathButton::Respawn && FoundationPawn->RequestRespawn())
    {
        Hud->LogEvent(TEXT("death screen: respawn"));
        UpdateDeathScreen();
    }
    // Vanilla single player (not hardcore) returns to the title screen without a confirm.
    else if (DeathHover == EHudDeathButton::TitleScreen)
        ReturnToMenu();
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
        if (FoundationChat::AreCommandsEnabled()) Chat.Clear();
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
        TEXT("interacted"), TEXT("tool broke"), TEXT("target down"), TEXT("lab reset"), TEXT("popped"),
        TEXT("picked up"), TEXT("eating"), TEXT("ate"), TEXT("stripped"), TEXT("took xp"), TEXT("filled"),
        TEXT("emptied"), TEXT("extinguished"), TEXT("opened"), TEXT("mob sound"), TEXT("equipped"), TEXT("used")};
    static_assert(UE_ARRAY_COUNT(Kinds) == int32(EFoundationEventKind::BlockUsed) + 1, "one name per event kind");
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
        else if (Event.Kind == EFoundationEventKind::Interacted ||
            Event.Kind == EFoundationEventKind::ContainerOpened || Event.Kind == EFoundationEventKind::BlockUsed)
            Text += TEXT(" ") + Event.Interaction.ToString();
        else if (Event.Kind == EFoundationEventKind::Eating || Event.Kind == EFoundationEventKind::MobSound)
            continue; // Every fourth step of a meal, a mob's steps and calls: too chatty for the event log.
        else if (Event.Kind == EFoundationEventKind::Ate)
            Text = FString::Printf(TEXT("ate %s"), *Authority->Rules.GetItem(Event.Item).Id.ToString());
        else if (Event.Kind == EFoundationEventKind::PickedUp)
            Text = FString::Printf(
                TEXT("picked up %d %s"), Event.Count, *Authority->Rules.GetItem(Event.Item).Id.ToString());
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

void AFoundationController::SubmitChat(const FString& Text)
{
    const double Now = FPlatformTime::Seconds();
    // Vanilla trims the line and collapses runs of spaces (StringUtil.normalizeSpace); an empty line sends nothing.
    FString Line = Text.TrimStartAndEnd().Left(256);
    while (Line.ReplaceInline(TEXT("  "), TEXT(" ")))
        ;
    if (Line.IsEmpty()) return;
    FFoundationCommandContext Context;
    if (!Line.StartsWith(TEXT("/")))
    {
        Chat.AddText(FoundationCommands::Format(TEXT("chat.type.text"), {Context.PlayerName, Line}),
            FoundationChat::White(), Now);
        return;
    }
    AFoundationPawn* FoundationPawn = GetFoundationPawn();
    Context.Authority = FoundationPawn ? FoundationPawn->GetAuthority() : nullptr;
    Context.PlayerId = FoundationPawn ? FoundationPawn->Interaction.PlayerId : 0;
    Context.Vitals = FoundationPawn ? &FoundationPawn->Vitals : nullptr;
    if (FoundationPawn)
    {
        Context.FeetCm = FoundationPawn->Simulation.Feet;
        Context.MinecraftOriginCm = GetDefault<UFoundationProfile>()->MinecraftOriginCm;
        Context.RebaseCm = GetWorld()->OriginLocation;
        Context.Teleport = [WeakPawn = TWeakObjectPtr<AFoundationPawn>(FoundationPawn)](const FVector& FeetCm)
        {
            if (WeakPawn.IsValid()) WeakPawn->TeleportFeet(FeetCm);
        };
    }
    if (AVoxelWorld* Voxels = AVoxelWorld::Find(GetWorld()))
    {
        Context.Clock = &Voxels->GetClock();
        // The saved world's "Allow Cheats"; the dev world (no saved world) allows them.
        if (const FUnnamedSavedWorld* Saved = Voxels->GetSavedWorld()) Context.bCommands = Saved->bCommands;
    }
    for (TArray<FHudChatRun>& Runs : FoundationCommands::Run(Line, Context))
        Chat.Add(MoveTemp(Runs), Now);
    Hud->LogEvent(TEXT("command: ") + Line);
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
