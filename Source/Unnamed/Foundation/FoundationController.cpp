#include "Foundation/FoundationController.h"
#include "UnnamedProjectSettings.h"
#include "Foundation/FoundationCoordinates.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationProfile.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Input/UnnamedInputSettings.h"
#include "Menu/MenuPreferences.h"
#include "Menu/MenuVideoSettings.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "DrawDebugHelpers.h"
#include "UnrealClient.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerInput.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"

class FFoundationInputProcessor : public IInputProcessor
{
    TWeakObjectPtr<UFoundationDiagnostics> Owner;

public:
    explicit FFoundationInputProcessor(UFoundationDiagnostics* Value) : Owner(Value) {}

    virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

    virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override
    {
        return Owner.IsValid() && Owner->RouteKey(UUnnamedInputSubsystem::PhysicalKey(E.GetKey()), true, E.IsRepeat());
    }

    virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& E) override
    {
        return Owner.IsValid() && Owner->RouteKey(UUnnamedInputSubsystem::PhysicalKey(E.GetKey()), false);
    }

    virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& E) override
    {
        return Owner.IsValid() && Owner->RouteKey(E.GetEffectingButton(), true);
    }

    virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& E) override
    {
        return Owner.IsValid() && Owner->RouteKey(E.GetEffectingButton(), false);
    }

    virtual bool HandleMouseWheelOrGestureEvent(
        FSlateApplication&, const FPointerEvent& E, const FPointerEvent*) override
    {
        return Owner.IsValid() && Owner->RouteWheel(E.GetWheelDelta());
    }
};

UFoundationDiagnostics::UFoundationDiagnostics()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bTickEvenWhenPaused = true;
}

AMenuPlayerController* UFoundationDiagnostics::Controller() const
{
    return Cast<AMenuPlayerController>(GetOwner());
}

UUnnamedInputSubsystem* UFoundationDiagnostics::Input() const
{
    return Controller() && Controller()->GetLocalPlayer()
        ? Controller()->GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>()
        : nullptr;
}

bool UFoundationDiagnostics::IsGameplay() const
{
    return Controller() && Cast<AFoundationPawn>(Controller()->GetPawn()) && !Controller()->IsMenuOpen() && !bPanel &&
        !bCapture;
}

void UFoundationDiagnostics::BeginPlay()
{
    Super::BeginPlay();
    if (!Controller() || !Controller()->IsLocalController() || !GEngine || !GEngine->GameViewport ||
        !FSlateApplication::IsInitialized())
        return;
    FString Error;
    if (Input()) Input()->EnsureReady(Error);
    Processor = MakeShared<FFoundationInputProcessor>(this);
    FSlateApplication::Get().RegisterInputPreProcessor(Processor, 0);
    FocusHandle =
        FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this, &ThisClass::FocusChanged);
    if (Input())
    {
        Input()->ActionSignal.AddUObject(this, &ThisClass::Action);
        Input()->ResetSignal.AddUObject(this, &ThisClass::CancelActions);
    }
    OverlayRoot = SNew(SOverlay).Visibility(EVisibility::HitTestInvisible) +
        SOverlay::Slot()
            .HAlign(HAlign_Left)
            .VAlign(VAlign_Top)
            .Padding(
                12)[SNew(SBorder).BorderBackgroundColor(FLinearColor(0, 0, 0, .75))[SAssignNew(DebugText, STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Mono", 11))
                    .ColorAndOpacity(FLinearColor::White)]] +
        SOverlay::Slot()
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Bottom)
            .Padding(10)[SAssignNew(HudText, STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                    .ColorAndOpacity(FLinearColor::White)] +
        SOverlay::Slot()
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)[SAssignNew(Crosshair, STextBlock)
                    .Text(FText::FromString(TEXT("+")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
                    .ColorAndOpacity(FLinearColor::White)];
    GEngine->GameViewport->AddViewportWidgetContent(OverlayRoot.ToSharedRef(), 200);
    LastTime = FPlatformTime::Seconds();
    Event(TEXT("diagnostics ready; reference Java26.3 / UE5.8.3"));
}

void UFoundationDiagnostics::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Input())
    {
        Input()->ActionSignal.RemoveAll(this);
        Input()->ResetSignal.RemoveAll(this);
    }
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(FocusHandle);
    }
    if (GEngine && GEngine->GameViewport)
    {
        if (OverlayRoot) GEngine->GameViewport->RemoveViewportWidgetContent(OverlayRoot.ToSharedRef());
        if (PanelRoot) GEngine->GameViewport->RemoveViewportWidgetContent(PanelRoot.ToSharedRef());
    }
    Processor.Reset();
    Super::EndPlay(Reason);
}

void UFoundationDiagnostics::Event(const FString& Text)
{
    Events.Add(FString::Printf(TEXT("%.2f %s"), GetWorld() ? GetWorld()->GetTimeSeconds() : 0, *Text));
    if (Events.Num() > 24) Events.RemoveAt(0);
}

void UFoundationDiagnostics::CancelActions()
{
    Actions.Cancel();
    Held.Reset();
    for (FKey Key : WheelRelease)
    {
        Pressed.Remove(Key);
        Blocked.Remove(Key);
    }
    WheelRelease.Reset();
    Blocked.Append(Pressed);
    bDebugUsed |= bDebugDown;
    bDebugDown = false;
    LastActionTick = 0;
    Event(TEXT("input cancelled; release held keys to resume"));
}

void UFoundationDiagnostics::SetCapture(bool Active)
{
    bCapture = Active;
    if (Active)
        CancelActions();
    else
        Blocked.Append(Pressed);
    Event(Active ? TEXT("binding capture entered") : TEXT("binding capture left"));
}

bool UFoundationDiagnostics::RouteKey(FKey Key, bool Down, bool Repeat)
{
    if (!Controller() || !Input()) return false;
    LastPhysical = Key.ToString() + (Down ? TEXT(" down") : TEXT(" up"));
    if (Down)
        Pressed.Add(Key);
    else
    {
        Pressed.Remove(Key);
        Blocked.Remove(Key);
    }
    if (bCapture) return false;
    if (Down && (Repeat || Blocked.Contains(Key))) return IsGameplay();
    const FKey Modifier = Input()->CurrentKey(TEXT("key.debug.modifier")),
               Overlay = Input()->CurrentKey(TEXT("key.debug.overlay"));
    if (!Down && ChordKeys.Remove(Key) > 0) return true;
    if (Key == Modifier)
    {
        if (Down)
        {
            bDebugDown = true;
            bDebugUsed = false;
        }
        else
        {
            if (bDebugDown && Key == Overlay && !bDebugUsed)
            {
                bOverlay = !bOverlay;
                Event(TEXT("debug overlay toggled"));
            }
            bDebugDown = false;
            bDebugUsed = false;
        }
        return true;
    }
    if (Key == Overlay && Down)
    {
        bOverlay = !bOverlay;
        Event(TEXT("debug overlay toggled"));
        return true;
    }
    if (Down && bDebugDown)
    {
        for (FName Id : Input()->SupportedIds())
            if (Id.ToString().StartsWith(TEXT("key.debug.")) && Input()->CurrentKey(Id) == Key)
            {
                bDebugUsed = true;
                ChordKeys.Add(Key);
                Action(Id, true);
                return true;
            }
    }
    if (Key == EKeys::Escape && Down)
    {
        if (bPanel)
        {
            ClosePanel();
            return true;
        }
        if (IsGameplay())
        {
            if (auto* PC = Cast<AFoundationController>(Controller())) PC->PauseFoundation();
            return true;
        }
    }
    if (bInventory && Down && Key == Input()->CurrentKey(TEXT("key.inventory")))
    {
        ClosePanel();
        return true;
    }
    if (!bText && Down)
        for (FName Id : {FName(TEXT("key.screenshot")), FName(TEXT("key.fullscreen"))})
            if (Input()->CurrentKey(Id) == Key)
            {
                Action(Id, true);
                return true;
            }
    if (IsGameplay())
    {
        Controller()->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key,
            Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f, false, FPlatformTime::Cycles64()));
        return true;
    }
    return false;
}

bool UFoundationDiagnostics::RouteWheel(float Delta)
{
    if (!IsGameplay()) return false;
    const FKey Key = Delta > 0 ? EKeys::MouseScrollUp : EKeys::MouseScrollDown;
    bool Mapped = false;
    for (FName Id : Input()->SupportedIds())
        if (Input()->CurrentKey(Id) == Key && (!Id.ToString().StartsWith(TEXT("key.debug.")) || bDebugDown))
        {
            Mapped = true;
            break;
        }
    if (Mapped)
    {
        RouteKey(Key, true);
        WheelRelease.AddUnique(Key);
        WheelStep = Cast<AFoundationPawn>(Controller()->GetPawn())->Simulation.Steps;
    }
    if (!Mapped)
    {
        auto* Prefs = GetWorld()->GetGameInstance()->GetSubsystem<UMenuPreferences>();
        auto Scale = Prefs->FindPreference(TEXT("mouseWheelSensitivity"));
        auto Discrete = Prefs->FindPreference(TEXT("discreteMouseScroll"));
        const double Sens = Scale.IsValid() ? FMath::Pow(10., Scale->AsNumber() / 100.) : 1.;
        if (WheelRemainder * Delta < 0) WheelRemainder = 0;
        WheelRemainder += (Discrete.IsValid() && Discrete->AsBool() ? FMath::Sign(Delta) : Delta) * Sens;
        const int32 Notches = FMath::TruncToInt32(WheelRemainder);
        if (Notches)
        {
            WheelRemainder -= Notches;
            Actions.Scroll(Notches);
            Event(TEXT("wheel selected slot"));
        }
    }
    return true;
}

void UFoundationDiagnostics::Action(FName Id, bool Down)
{
    const FString S = Id.ToString();
    LastLogical = S + (Down ? TEXT(" press") : TEXT(" release"));
    if (S == TEXT("key.attack") || S == TEXT("key.use"))
    {
        auto* Prefs = GetWorld()->GetGameInstance()->GetSubsystem<UMenuPreferences>();
        const auto Policy = Prefs->FindPreference(S == TEXT("key.attack") ? TEXT("toggleAttack") : TEXT("toggleUse"));
        if (Policy.IsValid() && Policy->AsBool())
        {
            if (!Down) return;
            Down = !Held.Contains(Id);
        }
    }
    if (Down)
        Held.Add(Id);
    else
        Held.Remove(Id);
    auto* Pawn = Cast<AFoundationPawn>(Controller()->GetPawn());
    if (Down)
    {
        Event(LastLogical);
        if (S == TEXT("key.debug.debugOptions"))
        {
            OpenDebugOptions();
            return;
        }
        if (S == TEXT("key.debug.showHitboxes"))
        {
            bShowCollision = !bShowCollision;
            return;
        }
        if (S == TEXT("key.debug.clearChat"))
        {
            Events.Reset();
            Event(TEXT("local history cleared"));
            return;
        }
        if (S == TEXT("key.debug.copyLocation"))
        {
            CopySnapshot();
            return;
        }
        if (S == TEXT("key.debug.dumpVersion"))
        {
            Event(TEXT("Java26.3 protocol777 build2026-09-15 / UE5.8.3 CL58210709"));
            return;
        }
        if (S == TEXT("key.screenshot"))
        {
            FScreenshotRequest::RequestScreenshot(
                FPaths::ProjectSavedDir() / TEXT("Screenshots/Foundation.png"), true, true);
            Event(TEXT("screenshot requested"));
            return;
        }
        if (S == TEXT("key.fullscreen"))
        {
            auto* Video = Controller()->EnsureVideoSettings();
            TSharedPtr<FJsonObject> State;
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Video->GetState()), State);
            if (State)
                Video->ApplySetting(TEXT("fullscreen"),
                    State->GetObjectField(TEXT("settings"))->GetBoolField(TEXT("fullscreen")) ? TEXT("false")
                                                                                              : TEXT("true"));
            return;
        }
        if (S == TEXT("key.toggleGui"))
        {
            bHideHud = !bHideHud;
            return;
        }
        if (S == TEXT("key.togglePerspective") && Pawn)
        {
            Pawn->SetPerspective((Pawn->GetPerspective() + 1) % 3);
            return;
        }
        if (S == TEXT("key.chat") || S == TEXT("key.command"))
        {
            OpenText(S == TEXT("key.command"));
            return;
        }
        if (S == TEXT("key.inventory"))
        {
            OpenInventory();
            return;
        }
        if (S.StartsWith(TEXT("key.hotbar.")))
        {
            Actions.Select(FCString::Atoi(*S.Right(1)) - 1);
            return;
        }
    }
    if (!Pawn) return;
    FFoundationActionRequest Request;
    Request.Target = Pawn->Target();
    Request.Phase = Down ? EFoundationPhase::Press : EFoundationPhase::Release;
    Request.bSneak = Pawn->LastIntent.bSneak;
    Request.bControl = Pressed.Contains(EKeys::LeftControl) || Pressed.Contains(EKeys::RightControl);
    if (S == TEXT("key.attack"))
        Request.Action = EFoundationAction::Attack;
    else if (S == TEXT("key.use"))
    {
        Request.Action = EFoundationAction::Use;
        if (Down) NextUseTick = Pawn->Simulation.Steps + 4;
    }
    else if (S == TEXT("key.pickItem"))
        Request.Action = EFoundationAction::Pick;
    else if (S == TEXT("key.drop"))
        Request.Action = EFoundationAction::Drop;
    else if (S == TEXT("key.swapOffhand"))
        Request.Action = EFoundationAction::Swap;
    else
        return;
    if (Down || S == TEXT("key.attack") || S == TEXT("key.use"))
    {
        const auto Result = Actions.Dispatch(Request);
        Event(Result.Reason);
    }
}

void UFoundationDiagnostics::FocusChanged(bool Active)
{
    if (!Active)
    {
        CancelActions();
        if (Input()) Input()->ClearIntent();
        if (IsGameplay())
            if (auto* PC = Cast<AFoundationController>(Controller())) PC->PauseFoundation();
        Event(TEXT("window focus lost"));
    }
    else
        Event(TEXT("window focus restored; actions require fresh press"));
}

void UFoundationDiagnostics::ShowPanel(TSharedRef<SWidget> Content)
{
    if (bPanel) ClosePanel();
    bPanel = true;
    CancelActions();
    if (Input()) Input()->SetGameplayInputEnabled(false);
    if (auto* Pawn = Cast<AFoundationPawn>(Controller()->GetPawn())) Pawn->SetSimulationEnabled(false);
    PanelRoot = SNew(SBox)
                    .HAlign(HAlign_Center)
                    .VAlign(VAlign_Center)[SNew(SBorder).Padding(20).BorderBackgroundColor(
                        FLinearColor(.02, .03, .04, 1))[Content]];
    GEngine->GameViewport->AddViewportWidgetContent(PanelRoot.ToSharedRef(), 300);
    Controller()->bShowMouseCursor = true;
    FInputModeUIOnly Mode;
    Controller()->SetInputMode(Mode);
    Event(TEXT("blocking fixture/options context"));
}

void UFoundationDiagnostics::ClosePanel()
{
    if (PanelRoot && GEngine && GEngine->GameViewport)
        GEngine->GameViewport->RemoveViewportWidgetContent(PanelRoot.ToSharedRef());
    PanelRoot.Reset();
    bPanel = bText = bInventory = false;
    if (!Controller()->IsMenuOpen()) Resume();
}

void UFoundationDiagnostics::Resume()
{
    if (auto* PC = Cast<AFoundationController>(Controller())) PC->ResumeFoundation();
}

void UFoundationDiagnostics::ReturnToMenu()
{
    CancelActions();
    UGameplayStatics::OpenLevel(this, FName(UUnnamedProjectSettings::GetMenuMapPackage()));
}

void UFoundationDiagnostics::CopySnapshot()
{
    FPlatformApplicationMisc::ClipboardCopy(*Snapshot());
    Event(TEXT("diagnostic snapshot copied"));
}

void UFoundationDiagnostics::OpenDebugOptions()
{
    if (bPanel)
    {
        ClosePanel();
        return;
    }
    auto* Settings = Input() ? Input()->Settings() : nullptr;
    if (!Settings) return;
    auto List = SNew(SVerticalBox);
    List->AddSlot()
        .AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("Debug Options — independent of menu GUI scale")))];
    List->AddSlot().AutoHeight().Padding(0, 10)[SNew(SSpinBox<float>)
            .MinValue(.5f)
            .MaxValue(2.f)
            .Delta(.1f)
            .Value(Settings->DebugTextScale)
            .OnValueChanged_Lambda(
                [Settings](float V)
                {
                    Settings->DebugTextScale = V;
                    FString Error;
                    Settings->SaveChecked(Error);
                })];
    const TArray<FString> Labels = {TEXT("Coordinates / camera / pose"), TEXT("Movement / eligibility"),
        TEXT("Input contexts / bindings"), TEXT("Hotbar / hands / targeting"), TEXT("Performance / recent events")};
    for (int32 I = 0; I < Labels.Num(); ++I)
        List->AddSlot().AutoHeight().Padding(0, 4)[SNew(SCheckBox)
                .IsChecked((Settings->DebugSections & (1 << I)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda(
                    [Settings, I](ECheckBoxState V)
                    {
                        if (V == ECheckBoxState::Checked)
                            Settings->DebugSections |= 1 << I;
                        else
                            Settings->DebugSections &= ~(1 << I);
                        FString Error;
                        Settings->SaveChecked(Error);
                    })[SNew(STextBlock).Text(FText::FromString(Labels[I]))]];
    List->AddSlot().AutoHeight()[SNew(SCheckBox)
            .IsChecked(Settings->bDebugUnrealCoordinates ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda(
                [Settings](ECheckBoxState V)
                {
                    Settings->bDebugUnrealCoordinates = V == ECheckBoxState::Checked;
                    FString Error;
                    Settings->SaveChecked(Error);
                })[SNew(STextBlock).Text(FText::FromString(TEXT("Show Unreal actor/camera coordinates (cm)")))]];
    List->AddSlot().AutoHeight().Padding(0, 8)[SNew(SButton)
            .Text(FText::FromString(TEXT("Copy diagnostic snapshot")))
            .OnClicked_Lambda(
                [this]
                {
                    CopySnapshot();
                    return FReply::Handled();
                })];
    List->AddSlot().AutoHeight()[SNew(SButton)
            .Text(FText::FromString(TEXT("Done")))
            .OnClicked_Lambda(
                [this]
                {
                    ClosePanel();
                    return FReply::Handled();
                })];
    ShowPanel(List);
    bOverlay = true;
}

void UFoundationDiagnostics::OpenText(bool Command)
{
    TSharedPtr<SEditableTextBox> Edit;
    auto List = SNew(SVerticalBox);
    List->AddSlot().AutoHeight()[SNew(STextBlock)
            .Text(FText::FromString(TEXT("Local chat/command fixture — Enter records text, Escape cancels")))];
    List->AddSlot().AutoHeight().Padding(0, 10)[SAssignNew(Edit, SEditableTextBox)
            .MinDesiredWidth(600)
            .Text(FText::FromString(Command ? TEXT("/") : TEXT("")))
            .OnTextCommitted_Lambda(
                [this](const FText& Text, ETextCommit::Type Type)
                {
                    if (Type == ETextCommit::OnEnter)
                    {
                        Event(TEXT("local text (no network/command execution): ") + Text.ToString().Left(256));
                        ClosePanel();
                    }
                })];
    ShowPanel(List);
    bText = true;
    FSlateApplication::Get().SetKeyboardFocus(Edit);
}

void UFoundationDiagnostics::OpenInventory()
{
    FFoundationActionRequest R;
    R.Action = EFoundationAction::Inventory;
    Actions.Dispatch(R);
    auto List = SNew(SVerticalBox);
    List->AddSlot().AutoHeight()[SNew(STextBlock)
            .Text(FText::FromString(TEXT("INVENTORY INPUT FIXTURE\nNo items, crafting or survival inventory.\nToken "
                                         "counts exercise drop/swap/use contracts.\nEscape returns to play.")))];
    List->AddSlot().AutoHeight()[SNew(SButton)
            .Text(FText::FromString(TEXT("Close")))
            .OnClicked_Lambda(
                [this]
                {
                    ClosePanel();
                    return FReply::Handled();
                })];
    ShowPanel(List);
    bInventory = true;
}

FString UFoundationDiagnostics::Snapshot() const
{
    const auto* PC = Controller();
    const auto* Pawn = PC ? Cast<AFoundationPawn>(PC->GetPawn()) : nullptr;
    auto* LocalInput = Input();
    auto* Settings = LocalInput ? LocalInput->Settings() : nullptr;
    const int32 Sections = Settings ? Settings->DebugSections : 31;
    FString Out = TEXT("FOUNDATION | Java 26.3 / UE 5.8.3 | 1 block = 100 cm\n");
    if (!Pawn) Out += TEXT("PLAYER: no player possessed (menu)\n");
    if (Pawn)
    {
        const auto& S = Pawn->Simulation;
        const auto& I = Pawn->LastIntent;
        const auto* P = GetDefault<UFoundationProfile>();
        const auto MC = FoundationCoordinates::ToMinecraft(S.Feet, P->MinecraftOriginCm, GetWorld()->OriginLocation);
        const auto Block = FoundationCoordinates::Blocks(MC);
        const auto View = PC->GetControlRotation();
        if (Sections & 1)
        {
            Out += FString::Printf(
                TEXT("Feet XYZ blocks: %.3f / %.3f / %.3f | block: %lld / %lld / %lld\nFacing %s | MC yaw %.2f pitch "
                     "%.2f | view %d\nPose %s | box %.1f x %.1f x %.1f cm | eyes %.2f cm (smoothed %.2f)\n"),
                MC.X, MC.Y, MC.Z, Block.X, Block.Y, Block.Z, *FoundationCoordinates::Facing(View.Yaw),
                FoundationCoordinates::Yaw(View.Yaw), FoundationCoordinates::Pitch(View.Pitch), Pawn->GetPerspective(),
                S.bCrouched ? TEXT("crouched") : TEXT("standing"), P->WidthCm, P->WidthCm, S.HeightCm,
                S.bCrouched ? P->CrouchingEyesCm : P->StandingEyesCm, S.EyeCm);
            if (Settings && Settings->bDebugUnrealCoordinates)
                Out += FString::Printf(TEXT("UE feet/actor cm %s | camera cm %s\n"), *S.Feet.ToCompactString(),
                    *Pawn->GetCamera()->GetComponentLocation().ToCompactString());
        }
        if (Sections & 2)
            Out += FString::Printf(
                TEXT("%s | sprint intent=%d resolved=%d | sneak intent=%d pose=%d | headroom=%d\nVelocity post-drag "
                     "cm/ref.tick %s | measured H %.4f blocks/s V %.4f blocks/s\nDisplacement %.5f blocks/reference "
                     "tick (20 Hz) | step %lld | dropped %.3fs\nEligibility: %s\n"),
                S.bGrounded ? TEXT("grounded") : TEXT("airborne"), I.bSprint, S.bSprinting, I.bSneak, S.bCrouched,
                S.bHeadroom, *S.VelocityPerTick.ToCompactString(), S.LastDisplacement.Size2D() / 5.,
                S.LastDisplacement.Z / 5., S.LastDisplacement.Size() / 100., S.Steps, S.DroppedSeconds, *S.Rejection);
        if (Sections & 8)
        {
            const auto T = Pawn->Target();
            Out += FString::Printf(TEXT("Hotbar %d | main=%d off=%d | hand=%s | break=%d use=%d\nTarget %s | distance "
                                        "%.3f reach %.2f blocks | allowed=%d\nHit cm %s normal %s\nResult: %s\n"),
                Actions.Slot + 1, Actions.Slots[Actions.Slot], Actions.Offhand, *Actions.LastHand, Actions.bBreaking,
                Actions.bUsing, *T.Name, T.DistanceCm / 100., T.ReachCm / 100., T.bInRange, *T.Hit.ToCompactString(),
                *T.Normal.ToCompactString(), *Actions.LastResult);
        }
    }
    if (Sections & 4)
    {
        const FString Contexts = LocalInput ? LocalInput->AppliedContexts() : TEXT("no input subsystem");
        Out += FString::Printf(TEXT("Contexts: %s + diagnostics preprocessor | capture=%d text=%d panel=%d | "
                                    "cursor=%s\nPhysical %s -> logical %s\n"),
            *Contexts, bCapture, bText, bPanel,
            PC && PC->bShowMouseCursor ? TEXT("visible") : TEXT("game capture requested"), *LastPhysical, *LastLogical);
        if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
            Out += FString::Printf(TEXT("Viewport actual mouse capture=%d focus=%d\n"),
                GEngine->GameViewport->Viewport->HasMouseCapture(), GEngine->GameViewport->Viewport->HasFocus());
        else
            Out += TEXT("Viewport capture/focus: unavailable\n");
        if (Settings)
            Out += TEXT("Profile: ") + Settings->GetActiveKeyProfileId() + TEXT(" | focus ") +
                (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive() ? TEXT("active")
                                                                                           : TEXT("inactive")) +
                TEXT("\n");
        if (LocalInput)
        {
            int32 Count = 0;
            for (FName Id : LocalInput->SupportedIds())
            {
                Out += Id.ToString().Replace(TEXT("key."), TEXT("")) + TEXT("=") +
                    LocalInput->CurrentKey(Id).ToString() + TEXT("  ");
                if (++Count % 5 == 0) Out += TEXT("\n");
            }
            Out += TEXT("\n");
        }
    }
    if (Sections & 16)
    {
        Out += FString::Printf(TEXT("Frame %.2f ms | %.1f FPS | "), FrameMs, FrameMs > 0 ? 1000. / FrameMs : 0.);
        const uint32 GPUCycles = GDynamicRHI ? RHIGetGPUFrameCycles() : 0;
        Out += GPUCycles ? FString::Printf(TEXT("GPU %.3f ms\n"), FPlatformTime::ToMilliseconds(GPUCycles))
                         : TEXT("GPU timing: unavailable\n");
        if (GGameThreadTime && GRenderThreadTime)
            Out += FString::Printf(TEXT("Engine CPU game %.3f ms / render %.3f ms (excludes idle)\n"),
                FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime));
        else
            Out += TEXT("Engine CPU timings: unavailable\n");
        for (int32 I = FMath::Max(0, Events.Num() - 6); I < Events.Num(); ++I)
            Out += Events[I] + TEXT("\n");
    }
    return Out;
}

FString UFoundationDiagnostics::State() const
{
    return FString::Printf(TEXT("{\"inLab\":%s,\"capture\":%s}"),
        Controller() && Cast<AFoundationPawn>(Controller()->GetPawn()) ? TEXT("true") : TEXT("false"),
        bCapture ? TEXT("true") : TEXT("false"));
}

void UFoundationDiagnostics::TickComponent(float Dt, ELevelTick Kind, FActorComponentTickFunction* F)
{
    Super::TickComponent(Dt, Kind, F);
    if (!Controller()) return;
    const double Now = FPlatformTime::Seconds();
    FrameMs = FrameMs == 0 ? (Now - LastTime) * 1000 : FrameMs * .9 + (Now - LastTime) * 100.;
    LastTime = Now;
    auto* Pawn = Cast<AFoundationPawn>(Controller()->GetPawn());
    if (Pawn)
    {
        const auto& S = Pawn->Simulation;
        const FString Motion = FString::Printf(
            TEXT("ground=%d crouch=%d sprint=%d headroom=%d"), S.bGrounded, S.bCrouched, S.bSprinting, S.bHeadroom);
        if (Motion != LastMotionState)
        {
            LastMotionState = Motion;
            Event(Motion);
        }
        if (WheelRelease.Num() && S.Steps > WheelStep)
        {
            auto Keys = MoveTemp(WheelRelease);
            WheelRelease.Reset();
            for (FKey Key : Keys)
                RouteKey(Key, false);
        }
        if (IsGameplay() && S.Steps != LastActionTick)
        {
            LastActionTick = S.Steps;
            const auto T = Pawn->Target();
            if (Actions.bBreaking && T.Name != Actions.ActiveTarget)
            {
                Actions.Cancel();
                Event(TEXT("break cancelled: target changed"));
            }
            if (Held.Contains(TEXT("key.attack")))
            {
                FFoundationActionRequest R;
                R.Action = EFoundationAction::Attack;
                R.Phase = EFoundationPhase::Hold;
                R.Target = T;
                Actions.Dispatch(R);
            }
            if (Held.Contains(TEXT("key.use")) && !Actions.bUsing && S.Steps >= NextUseTick)
            {
                NextUseTick = S.Steps + 4;
                FFoundationActionRequest R;
                R.Action = EFoundationAction::Use;
                R.Phase = EFoundationPhase::Hold;
                R.Target = T;
                R.bSneak = Pawn->LastIntent.bSneak;
                Actions.Dispatch(R);
            }
            if (Actions.LastResult != LastActionResult)
            {
                LastActionResult = Actions.LastResult;
                Event(Actions.LastResult);
            }
        }
        if (Pawn->GetPerspective())
        {
            const auto& P = *GetDefault<UFoundationProfile>();
            DrawDebugBox(GetWorld(), S.Feet + FVector(0, 0, S.HeightCm / 2),
                FVector(P.WidthCm / 2, P.WidthCm / 2, S.HeightCm / 2), FColor::Cyan, false, -1, 0, 1.5);
        }
        if (bShowCollision && Pawn->GetLab())
            for (const auto& B : Pawn->GetLab()->CollisionBoxes)
                DrawDebugBox(GetWorld(), B.GetCenter(), B.GetExtent(), FColor::Green, false, -1, 0, .3f);
    }
    if (Now < SampleAt || !DebugText) return;
    SampleAt = Now + .1;
    DebugText->SetVisibility(bOverlay ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
    if (bOverlay)
    {
        DebugText->SetText(FText::FromString(Snapshot()));
        if (Input() && Input()->Settings())
            DebugText->SetFont(
                FCoreStyle::GetDefaultFontStyle("Mono", FMath::RoundToInt(11 * Input()->Settings()->DebugTextScale)));
    }
    const bool Hud = Pawn && !bHideHud && !Controller()->IsMenuOpen() && !bPanel;
    HudText->SetVisibility(Hud ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
    Crosshair->SetVisibility(Hud ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
    FString Bar = TEXT("ESC menu   |   F3 diagnostics   |   F3+F6 options\n");
    for (int32 I = 0; I < 9; ++I)
        Bar += I == Actions.Slot ? FString::Printf(TEXT(" [%d:%d] "), I + 1, Actions.Slots[I])
                                 : FString::Printf(TEXT("  %d:%d  "), I + 1, Actions.Slots[I]);
    HudText->SetText(FText::FromString(Bar));
}

FString UFoundationMenuBridge::GetState()
{
    return Diagnostics.IsValid() ? Diagnostics->State() : TEXT("{}");
}

void UFoundationMenuBridge::SetCapture(bool Active)
{
    if (Diagnostics.IsValid()) Diagnostics->SetCapture(Active);
}

void UFoundationMenuBridge::Resume()
{
    if (Diagnostics.IsValid()) Diagnostics->Resume();
}

void UFoundationMenuBridge::ReturnToMenu()
{
    if (Diagnostics.IsValid()) Diagnostics->ReturnToMenu();
}

void AFoundationController::SetupInputComponent()
{
    Super::SetupInputComponent();
    if (GetLocalPlayer())
        GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>()->BindGameplayInput(
            Cast<UEnhancedInputComponent>(InputComponent));
}

void AFoundationController::BeginPlay()
{
    APlayerController::BeginPlay();
    PrimaryActorTick.bTickEvenWhenPaused = true;
    EnsureVideoSettings();
    ResumeFoundation();
}

void AFoundationController::OnPossess(APawn* P)
{
    Super::OnPossess(P);
    if (GetLocalPlayer()) GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>()->ClearIntent();
}

void AFoundationController::OnUnPossess()
{
    if (GetLocalPlayer()) GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>()->SetGameplayInputEnabled(false);
    Super::OnUnPossess();
}

void AFoundationController::PauseFoundation()
{
    if (auto* P = Cast<AFoundationPawn>(GetPawn())) P->SetSimulationEnabled(false);
    ShowMenu();
    Diagnostics->Event(TEXT("pause: existing menu opened"));
}

void AFoundationController::ResumeFoundation()
{
    HideMenu();
    if (!GetLocalPlayer()) return;
    auto* LocalInput = GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>();
    LocalInput->BindGameplayInput(Cast<UEnhancedInputComponent>(InputComponent));
    LocalInput->SetGameplayInputEnabled(true);
    // Engine debug execs otherwise steal F1/F5 and Shift in development games.
    // This affects only this gameplay controller, not editor viewport shortcuts.
    if (PlayerInput) PlayerInput->DebugExecBindings.Empty();
    bShowMouseCursor = false;
    ResetIgnoreMoveInput();
    ResetIgnoreLookInput();
    SetInputMode(FInputModeGameOnly());
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
    if (auto* P = Cast<AFoundationPawn>(GetPawn())) P->SetSimulationEnabled(true);
    if (Diagnostics) Diagnostics->Event(TEXT("gameplay resumed: release held keys before new actions"));
}
