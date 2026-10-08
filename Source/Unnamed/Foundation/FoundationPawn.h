#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Foundation/FoundationActions.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationSimulation.h"
#include "Foundation/FoundationVitals.h"
#include "FoundationPawn.generated.h"

class AFoundationLab;
class UBoxComponent;
class UCameraComponent;

UCLASS()
class UNNAMED_API AFoundationPawn : public APawn
{
    GENERATED_BODY()
public:
    AFoundationPawn();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;
    FFoundationSimulation Simulation;
    FFoundationIntent LastIntent;
    // The last step's eligibility, derived from Vitals (death stops movement, food gates sprint).
    FFoundationEligibility Eligibility;
    // Health, food, air, experience and attack recovery; stepped with the simulation.
    FFoundationVitals Vitals;
    // Place/mine/attack intent and timing, in a lab with an interaction authority (the Playground).
    FFoundationInteraction Interaction;
    FVector GetEyePosition() const;
    FFoundationTarget TraceTarget() const;
    void SetSimulationEnabled(bool bEnabled);
    // Restores fresh vitals at the lab's spawn (death respawn, also used by the probe).
    void Respawn();

    void SetPerspective(int32 Value) { Perspective = Value; }

    int32 GetPerspective() const { return Perspective; }

    UCameraComponent* GetCamera() const { return Camera; }

    AFoundationLab* GetLab() const { return Lab.Get(); }

    // The lab's interaction authority once this pawn is registered with it; null in the Foundation lab.
    FFoundationAuthority* GetAuthority() const;
    bool HasInteraction() const { return GetAuthority() != nullptr; }
    // This step's authoritative facts: gameplay eye and aim, body, movement and water state.
    FFoundationPlayerFacts MakeInteractionFacts() const;
    // Logical input for the interaction (Hold/Toggle already resolved by the controller).
    void PressInteraction(EFoundationInput Input);
    void ReleaseInteraction(EFoundationInput Input);
    void CancelInteraction(EFoundationReason Reason);
    void SelectInteractionSlot(int32 Slot);
    void ScrollInteraction(int32 Notches);
    bool PickInteractionBlock();
    void DropInteractionItem(bool bWholeStack);
    void SwapInteractionHands();
    // Game-thread time of collision gathering plus interaction for recent engine updates (ms).
    TArray<float> InteractionMs;
    // Reference steps the last engine update ran (catch-up after a hitch runs several).
    int32 LastDueSteps = 0;
    // Every reference step this pawn has run. Unlike Simulation.Steps it never restarts (respawn, falling out of
    // the lab), so the interaction's step clock only moves forward.
    int64 ReferenceSteps = 0;

private:
    // Feeds one reference step of movement and water facts to Vitals; respawns once death has played out.
    void StepVitals(const UFoundationProfile& Profile);
    // One reference step of interaction, after movement and vitals, so the step's pose and death apply.
    void StepInteraction(FFoundationAuthority& Authority, bool bLatestStep, double NowSeconds);
    // Registers with the lab's authority, adopts its preset and returns to spawn after a lab reset.
    void SyncInteraction(FFoundationAuthority& Authority);

    UPROPERTY()
    TObjectPtr<UBoxComponent> Collision;
    UPROPERTY()
    TObjectPtr<UCameraComponent> Camera;
    TWeakObjectPtr<AFoundationLab> Lab;
    // Reused per step: the collision provider's boxes near the player.
    TArray<FBox> StepCollision;
    bool bSimulate = true;
    bool bProbeCreated = false;
    bool bBodyInWater = false, bEyesInWater = false;
    int32 Perspective = 0;
};
