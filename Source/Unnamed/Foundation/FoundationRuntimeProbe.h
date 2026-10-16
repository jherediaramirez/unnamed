#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FoundationRuntimeProbe.generated.h"

class AFoundationPawn;
class AFoundationController;
class UMenuInputBridge;
class FJsonValue;

// Opt-in application-level verification. Uses the real controller/action pipeline,
// never OS event injection. Reports this distinction in its output.
UCLASS()
class UFoundationRuntimeProbe : public UActorComponent
{
    GENERATED_BODY()
public:
    UFoundationRuntimeProbe();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;

private:
    AFoundationPawn* Pawn = nullptr;
    AFoundationController* Controller = nullptr;
    UPROPERTY()
    TObjectPtr<UMenuInputBridge> InputBridge;

    struct FCase
    {
        FString Name;
        double Duration;
        TFunction<void()> Start, End;
    };

    TArray<FCase> Cases;
    // Optional per-frame work of the running case (set by its Start, cleared by the next case).
    TFunction<void()> FrameHook;
    // Set by a case whose FrameHook steps the game itself: the case runs past its duration until the hook clears this
    // (at most MaxHoldSeconds, so a stuck hook still lets its End report the failure).
    bool bHoldCase = false;
    static constexpr double MaxHoldSeconds = 10;
    // Extra measurements merged into the report (interaction timing, costs, captures).
    TSharedPtr<class FJsonObject> Extra;
    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> PhaseRates;
    TArray<FKey> DownKeys;
    TArray<TPair<FKey, bool>> QueuedKeys;
    int32 DeferKeysUntilFrame = 0;
    FString Output;
    int32 Phase = -1, Failures = 0, RequestedFPS = 60, Frames = 0, PhaseStartFrame = 0;
    double Started = 0, TotalStarted = 0, PhaseStarted = 0, Apex = 0, LastZ = 0;
    double LastSampleTime = 0, GameMsSum = 0, RenderMsSum = 0, GpuMsSum = 0;
    int32 CpuSamples = 0, GpuSamples = 0;
    TArray<double> FrameSamples;
    // Engine game-thread time per frame of the running phase (ms), for percentiles.
    TArray<double> GameSamples;
    // Interaction suite: run the ordinary scenarios with this preset (-InteractionPreset=) instead of the reference.
    FString ScenarioPreset;
    int64 LaunchTick = -1, LandTick = -1;
    bool bDone = false;
    void PressKey(FKey Key, bool bDown);
    // Stands the player at a world position (cm); fresh vitals unless a case continues the current ones.
    void ResetPlayer(FVector Position = FVector(0, 0, 0), bool bFreshVitals = true);
    // The same at a position local to the movement course; the default start is the course origin.
    void ResetCoursePlayer(FVector Local = FVector(0, 0, 0), bool bFreshVitals = true);
    // Where the movement course's local (0, 0, 0) sits in the world (the lab's course origin).
    FVector GetCourseOrigin() const;
    void Record(FString Name, bool bPassed, FString Detail);
    void Measure(FString Name, double Actual, double Expected, double Tolerance);
    void AddCase(FString Name, double Duration, TFunction<void()> Start, TFunction<void()> End);
    void Finish();
    // -FoundationSuite=movement: the input pipeline and movement physics, played on the course.
    void AddMovementCases();
    // -HudCaptures: HUD captures and the player-vitals fixtures (HudRuntimeProbe.cpp).
    void AddHudCases();
    // -FoundationSuite=interaction: the Playground's interaction stations (FoundationInteractionProbe.cpp).
    void AddInteractionCases();
    // -FoundationSuite=tour: named screenshots of the Playground with the HUD visible (FoundationTourProbe.cpp).
    void AddTourCases();
    // -FoundationSuite=views: player-view screenshots of the generated world with the HUD (FoundationViewsProbe.cpp).
    void AddViewsCases();
    // -FoundationSuite=pause: the frames around ESC and Back to Game (FoundationPauseProbe.cpp).
    void AddPauseCases();
    // -FoundationSuite=swim: the swimming control pair in the Playground pool (FoundationSwimProbe.cpp).
    void AddSwimCases();
    // -FoundationSuite=body: the player body and the spring arm preview, with shots (FoundationBodyProbe.cpp).
    void AddBodyCases();
    // The Unnamed Player's first-person body, in the body suite: the M1a acceptance rows (clearance, toes, head,
    // shadow, view switching, planted feet, the simulation unchanged, cost; FoundationBodyFirstPersonProbe.cpp).
    void AddFirstPersonBodyCases();
    // -FoundationBodySweep (with -FoundationSuite=body): only the crouched first-person corrections over a grid of
    // their settings, with a table and shots (FoundationBodyFirstPersonProbe.cpp).
    void AddFirstPersonBodySweep();
    // M1b in the body suite: the pickaxe in the real hand and StrikeA on the swing clock
    // (FoundationBodyStrikeProbe.cpp).
    void AddStrikeBodyCases();
    // -FoundationSuite=mobs: a summoned cow walking, hit and dying into its drops, and the foundation.Mobs control
    // pair, with shots (FoundationMobsProbe.cpp).
    void AddMobCases();
    // -FoundationSuite=smear: mobs and drops moving across the view, the held sword and a turning camera, with shots
    // for the smear fix's before/after (K-PLAYER-7, FoundationSmearProbe.cpp).
    void AddSmearCases();
    // The chat commands (step 15), in the interaction and views suites (FoundationCommandsProbe.cpp).
    void AddCommandsCases();
    // The death screen (step 11a) and its switch-0 control, in the interaction suite (FoundationDeathProbe.cpp).
    void AddDeathCases();
    // Player footsteps (step 13a2, FoundationFootstepsProbe.cpp).
    void AddFootstepCases();
    // The pause menu's life in the world's foundation.Pause.Menu mode, in the all and pause suites
    // (FoundationPauseProbe.cpp).
    void AddPauseMenuCases();
};
