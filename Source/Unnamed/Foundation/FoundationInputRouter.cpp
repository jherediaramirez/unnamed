#include "Foundation/FoundationInputRouter.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationPawn.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuPreferences.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputKeyEventArgs.h"

namespace
{
    bool IsDebugAction(FName Id)
    {
        return Id.ToString().StartsWith(TEXT("key.debug."));
    }

    // Slate preprocessor: keyboard keys are mapped to their US-layout physical key first.
    class FFoundationInputProcessor : public IInputProcessor
    {
    public:
        explicit FFoundationInputProcessor(UFoundationInputRouter* InRouter) : Router(InRouter) {}

        virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

        virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
        {
            return Router.IsValid() &&
                Router->RouteKey(UUnnamedInputSubsystem::PhysicalKey(Event.GetKey()), true, Event.IsRepeat());
        }

        virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
        {
            return Router.IsValid() && Router->RouteKey(UUnnamedInputSubsystem::PhysicalKey(Event.GetKey()), false);
        }

        virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Event) override
        {
            return Router.IsValid() && Router->RouteKey(Event.GetEffectingButton(), true);
        }

        virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& Event) override
        {
            return Router.IsValid() && Router->RouteKey(Event.GetEffectingButton(), false);
        }

        virtual bool HandleMouseWheelOrGestureEvent(
            FSlateApplication&, const FPointerEvent& Event, const FPointerEvent*) override
        {
            return Router.IsValid() && Router->RouteWheel(Event.GetWheelDelta());
        }

    private:
        TWeakObjectPtr<UFoundationInputRouter> Router;
    };
}

AFoundationController* UFoundationInputRouter::GetFoundationController() const
{
    return Cast<AFoundationController>(GetOwner());
}

void UFoundationInputRouter::BeginPlay()
{
    Super::BeginPlay();
    AFoundationController* Controller = GetFoundationController();
    if (!Controller || !Controller->IsLocalController() || !GEngine || !GEngine->GameViewport ||
        !FSlateApplication::IsInitialized())
        return;
    FString Error;
    if (auto* InputSubsystem = Controller->GetInputSubsystem()) InputSubsystem->EnsureReady(Error);
    Processor = MakeShared<FFoundationInputProcessor>(this);
    FSlateApplication::Get().RegisterInputPreProcessor(Processor, 0);
    FocusHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(
        Controller, &AFoundationController::HandleFocusChanged);
}

void UFoundationInputRouter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(FocusHandle);
    }
    Processor.Reset();
    Super::EndPlay(EndPlayReason);
}

void UFoundationInputRouter::BlockHeldKeys()
{
    for (FKey Key : WheelRelease)
    {
        Pressed.Remove(Key);
        Blocked.Remove(Key);
    }
    WheelRelease.Reset();
    Blocked.Append(Pressed);
    // A held modifier must not toggle the overlay on its later release.
    bDebugModifierUsed |= bDebugModifierDown;
    bDebugModifierDown = false;
}

void UFoundationInputRouter::SetCapture(bool bActive)
{
    bCapture = bActive;
    AFoundationController* Controller = GetFoundationController();
    if (bActive)
        Controller->CancelActions();
    else
        Blocked.Append(Pressed);
    Controller->GetHud()->LogEvent(bActive ? TEXT("binding capture entered") : TEXT("binding capture left"));
}

bool UFoundationInputRouter::RouteKey(FKey Key, bool bDown, bool bRepeat)
{
    AFoundationController* Controller = GetFoundationController();
    UUnnamedInputSubsystem* InputSubsystem = Controller ? Controller->GetInputSubsystem() : nullptr;
    if (!InputSubsystem) return false;
    LastPhysical = Key.ToString() + (bDown ? TEXT(" down") : TEXT(" up"));
    if (bDown)
        Pressed.Add(Key);
    else
    {
        Pressed.Remove(Key);
        Blocked.Remove(Key);
    }
    if (bCapture) return false;
    if (bDown && (bRepeat || Blocked.Contains(Key))) return Controller->IsGameplay();

    // F3 alone toggles the overlay on release; F3 + another debug key runs that chord instead.
    const FKey Modifier = InputSubsystem->CurrentKey(TEXT("key.debug.modifier"));
    const FKey Overlay = InputSubsystem->CurrentKey(TEXT("key.debug.overlay"));
    if (!bDown && ChordKeys.Remove(Key) > 0) return true;
    if (Key == Modifier)
    {
        if (bDown)
        {
            bDebugModifierDown = true;
            bDebugModifierUsed = false;
        }
        else
        {
            if (bDebugModifierDown && Key == Overlay && !bDebugModifierUsed) Controller->GetHud()->ToggleOverlay();
            bDebugModifierDown = false;
            bDebugModifierUsed = false;
        }
        return true;
    }
    if (Key == Overlay && bDown)
    {
        Controller->GetHud()->ToggleOverlay();
        return true;
    }
    if (bDown && bDebugModifierDown)
    {
        for (FName Id : InputSubsystem->SupportedIds())
            if (IsDebugAction(Id) && InputSubsystem->CurrentKey(Id) == Key)
            {
                bDebugModifierUsed = true;
                ChordKeys.Add(Key);
                Controller->HandleAction(Id, true);
                return true;
            }
    }

    UFoundationPanels* Panels = Controller->GetPanels();
    if (Key == EKeys::Escape && bDown)
    {
        if (Panels->IsOpen())
        {
            Panels->Close();
            return true;
        }
        if (Controller->IsGameplay())
        {
            Controller->PauseFoundation();
            return true;
        }
    }
    if (Panels->IsInventoryOpen() && bDown && Key == InputSubsystem->CurrentKey(TEXT("key.inventory")))
    {
        Panels->Close();
        return true;
    }
    // Screenshot and fullscreen work everywhere except while typing.
    if (!Panels->IsTextOpen() && bDown)
        for (FName Id : {FName(TEXT("key.screenshot")), FName(TEXT("key.fullscreen"))})
            if (InputSubsystem->CurrentKey(Id) == Key)
            {
                Controller->HandleAction(Id, true);
                return true;
            }
    if (Controller->IsGameplay())
    {
        Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key,
            bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f, false, FPlatformTime::Cycles64()));
        return true;
    }
    return false;
}

bool UFoundationInputRouter::RouteWheel(float Delta)
{
    AFoundationController* Controller = GetFoundationController();
    if (!Controller || !Controller->IsGameplay()) return false;
    UUnnamedInputSubsystem* InputSubsystem = Controller->GetInputSubsystem();
    const FKey Key = Delta > 0 ? EKeys::MouseScrollUp : EKeys::MouseScrollDown;
    bool bMapped = false;
    for (FName Id : InputSubsystem->SupportedIds())
        if (InputSubsystem->CurrentKey(Id) == Key && (!IsDebugAction(Id) || bDebugModifierDown))
        {
            bMapped = true;
            break;
        }
    if (bMapped)
    {
        // A bound wheel notch is a one-step press of its action.
        RouteKey(Key, true);
        WheelRelease.AddUnique(Key);
        WheelStep = Controller->GetFoundationPawn()->Simulation.Steps;
        return true;
    }
    // Unbound wheel scrolls the hotbar, using the saved wheel preferences.
    const auto* Preferences = GetWorld()->GetGameInstance()->GetSubsystem<UMenuPreferences>();
    const auto Sensitivity = Preferences->FindPreference(TEXT("mouseWheelSensitivity"));
    const auto Discrete = Preferences->FindPreference(TEXT("discreteMouseScroll"));
    const double Scale = Sensitivity.IsValid() ? FMath::Pow(10., Sensitivity->AsNumber() / 100.) : 1.;
    if (WheelRemainder * Delta < 0) WheelRemainder = 0;
    WheelRemainder += (Discrete.IsValid() && Discrete->AsBool() ? FMath::Sign(Delta) : Delta) * Scale;
    const int32 Notches = FMath::TruncToInt32(WheelRemainder);
    if (Notches)
    {
        WheelRemainder -= Notches;
        Controller->ScrollHotbar(Notches);
    }
    return true;
}

void UFoundationInputRouter::ReleaseWheelPulses(int64 SimulationStep)
{
    if (WheelRelease.IsEmpty() || SimulationStep <= WheelStep) return;
    const TArray<FKey> Keys = MoveTemp(WheelRelease);
    WheelRelease.Reset();
    for (FKey Key : Keys)
        RouteKey(Key, false);
}
