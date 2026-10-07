#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "FoundationInputRouter.generated.h"

class AFoundationController;
class IInputProcessor;

// Sees every key, mouse button and wheel event before Slate and the menu page. It owns the
// debug-modifier chords (F3 + key), binding capture, and blocking of keys still held across a
// context change; everything else goes to Enhanced Input while the player is in gameplay.
UCLASS(ClassGroup = Foundation)
class UNNAMED_API UFoundationInputRouter : public UActorComponent
{
    GENERATED_BODY()
public:
    // Returns true when the event is consumed and must not reach Slate or the page.
    bool RouteKey(FKey Key, bool bDown, bool bRepeat = false);
    bool RouteWheel(float Delta);

    // While the menu captures a new binding, raw input bypasses gameplay and chords.
    void SetCapture(bool bActive);
    bool IsCapturing() const { return bCapture; }

    // Every key held now must be released before it acts again.
    void BlockHeldKeys();
    // Wheel "keys" are released once the simulation has stepped past their pulse.
    void ReleaseWheelPulses(int64 SimulationStep);

    bool IsKeyDown(FKey Key) const { return Pressed.Contains(Key); }
    const FString& GetLastPhysical() const { return LastPhysical; }

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    AFoundationController* GetFoundationController() const;

    TSharedPtr<IInputProcessor> Processor;
    FDelegateHandle FocusHandle;
    TSet<FKey> Pressed;
    TSet<FKey> Blocked;
    TSet<FKey> ChordKeys;
    TArray<FKey> WheelRelease;
    int64 WheelStep = 0;
    double WheelRemainder = 0;
    bool bDebugModifierDown = false;
    bool bDebugModifierUsed = false;
    bool bCapture = false;
    FString LastPhysical = TEXT("none");
};
