#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Foundation/FoundationActions.h"
#include "Foundation/FoundationBody.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationSimulation.h"
#include "Foundation/FoundationVitals.h"
#include "FoundationPawn.generated.h"

class AFoundationLab;
class UBoxComponent;
class UCameraComponent;
class USkeletalMeshComponent;

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
    // A survival screen (the inventory, a container) is open: the simulation keeps running (vitals, item entities,
    // orbs, furnaces) with the player's movement and look input zeroed (Codex review 12, finding 2).
    void SetScreenInput(bool bScreen);
    bool HasScreenInput() const { return bScreenInput; }
    // Restores fresh vitals at the lab's spawn (death respawn, also used by the probe).
    void Respawn();
    // Moves the feet to a world position (cm), as /tp: motion and fall distance reset, flight and pose kept.
    void TeleportFeet(const FVector& FeetCm);

    void SetPerspective(int32 Value) { Perspective = Value; }

    int32 GetPerspective() const { return Perspective; }

    UCameraComponent* GetCamera() const { return Camera; }

    // The player body (Better Steve), drawn in the third-person views; null when its assets are missing.
    USkeletalMeshComponent* GetBodyMesh() const { return BodyMesh; }
    bool IsBodyShown() const;
    // foundation.Body.Yaw.
    static EFoundationBodyYaw GetBodyYawMode();
    // foundation.View.SpringArm: the back view is the over-the-shoulder spring arm (a third-person preview).
    bool IsSpringArmView() const;
    // Where the body faces (stepped with the simulation) and which clips it plays (advanced per frame).
    FFoundationBodyTurn BodyTurn;
    FFoundationBodyAnimator BodyAnimator;
    // After a respawn, lab reset or teleport: the body stands facing the view, no jump or landing pending.
    void ResetBody();

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
    // Adds the body when its mesh and clips exist (UUnnamedProjectSettings::PlayerBodyRoot).
    void CreateBody();
    // Places, poses and shows or hides the body for this frame (Eye: the rendered eyes; CameraLocation: the camera).
    void UpdateBody(
        double Alpha, double Seconds, const FRotator& Control, const FVector& Eye, const FVector& CameraLocation);
    // The spring arm's camera location for this frame; View becomes its (lagged) rotation.
    FVector PlaceSpringArm(const FVector& Eye, FRotator& View, double Seconds);

    UPROPERTY()
    TObjectPtr<UBoxComponent> Collision;
    UPROPERTY()
    TObjectPtr<UCameraComponent> Camera;
    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> BodyMesh;
    // Jumps the simulation started since the body's last frame.
    int32 BodyJumps = 0;
    bool bBodyTurnStarted = false;
    // The spring arm's lagged pivot and rotation, while its view is on.
    FVector ArmPivot = FVector::ZeroVector;
    FQuat ArmRotation = FQuat::Identity;
    bool bArmPlaced = false;
    TWeakObjectPtr<AFoundationLab> Lab;
    // Reused per step: the collision provider's boxes near the player.
    TArray<FBox> StepCollision;
    bool bSimulate = true;
    bool bScreenInput = false;
    bool bProbeCreated = false;
    bool bBodyInWater = false, bEyesInWater = false;
    int32 Perspective = 0;
};
