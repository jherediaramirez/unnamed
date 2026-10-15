#pragma once

#include "CoreMinimal.h"

class UFoundationProfile;

// Movement eligibility facts. The pawn supplies death (mobility), food and the game mode's flight
// permission from FFoundationVitals each step; item slowdown has no owner yet. The simulation itself
// has no survival rules.
enum class EFoundationFluid : uint8
{
    None,
    Water,
    Lava
};

struct FFoundationEligibility
{
    bool bMobile = true, bEnoughFood = true, bItemSlows = false, bMayFly = false;
    // Fluid facts at the pose before the step (foundation.Swim 1; left empty they change nothing). IsFluidFree
    // answers the jump-out test; unset, every box counts as free.
    EFoundationFluid Fluid = EFoundationFluid::None;
    bool bEyesInFluid = false;
    double FluidAtFeetCm = 0;
    TFunction<bool(const FBox&)> IsFluidFree;
    // foundation.SwimPose 1: sprinting in water swims in the low box.
    bool bSwimPose = false;
};

// One simulation step's request. The pawn builds it from the input layer's FUnnamedInputIntent
// plus the player's movement preferences; the simulation never reads input or preferences.
struct FFoundationIntent
{
    FVector2D Move = {}; // X right, Y forward; independent logical bindings feed this.
    bool bSprint = false, bCrouch = false, bJump = false;
    bool bAutoJump = false; // Preference "autoJump".
    int32 SprintWindowTicks = 0; // Preference "sprintWindow": double-tap sprint window; 0 disables it.
    // The held use's FOV factor: a drawn bow's FoundationBow::GetFovTarget (step 19c); 1 otherwise.
    double ItemFovMultiplier = 1;
};

struct UNNAMED_API FFoundationSimulation
{
    FVector Feet = {}, PreviousFeet = {}, VelocityPerTick = {}, LastDisplacement = {};
    // Pose starts from the profile in Reset().
    double HeightCm = 0, EyeCm = 0, PreviousEyeCm = 0, FovMultiplier = 1, PreviousFovMultiplier = 1;
    double Accumulator = 0, DroppedSeconds = 0, FallDistanceCm = 0;
    int64 Steps = 0;
    int32 JumpCooldown = 0, SprintTimer = 0, AutoJumpDelay = 0, FlyTriggerTimer = 0;
    bool bGrounded = false, bCrouched = false, bSprinting = false, bHeadroom = true, bHorizontalCollision = false,
         bHadForward = false;
    // Swimming (26.3 updateSwimming): bSwimming is the state, bLowPose the 0.6-tall box it uses; the low box stays
    // after swimming ends until a standing or crouching box fits.
    bool bSwimming = false, bLowPose = false;
    // Creative flight, toggled by a double tap of jump; bHadJump is the previous step's held jump.
    bool bFlying = false, bHadJump = false;
    // The last step started a jump, and whether it was a sprint jump (food exhaustion facts).
    bool bJumped = false, bSprintJumped = false;
    FString Rejection = TEXT("unhindered fixture: sufficient food, no effects");
    // The pose's eye height, before smoothing: swimming, crouching or standing.
    double GetPoseEyesCm(const UFoundationProfile& P) const;
    // Stands the player at Position with the profile's standing height and eyes.
    void Reset(FVector Position, const UFoundationProfile& P);
    // Banks Seconds and returns how many reference steps are due now (bounded catch-up). Callers that
    // must act between steps run Step themselves; Advance does both.
    int32 TakeDueSteps(double Seconds, const UFoundationProfile& P);
    int32 Advance(double Seconds, const FFoundationIntent& Intent, double YawDegrees, const UFoundationProfile& P,
        const TArray<FBox>& WorldBoxes, const FFoundationEligibility& Eligibility = {});
    void Step(const FFoundationIntent& Intent, double YawDegrees, const UFoundationProfile& P,
        const TArray<FBox>& WorldBoxes, const FFoundationEligibility& Eligibility = {});
    static FBox Bounds(FVector FeetCm, double Height, double Width);
    static bool Fits(const FBox& Box, const TArray<FBox>& WorldBoxes);
    static FVector Clip(const FBox& Box, FVector Delta, const TArray<FBox>& WorldBoxes);
    static FVector2D ResolveInput(FVector2D Requested, double SlowFactor, double Damping);
};
