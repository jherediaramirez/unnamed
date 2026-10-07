#pragma once

#include "CoreMinimal.h"

class UFoundationProfile;

// Future eligibility owner supplies facts; no hunger, item, fluid or survival system here.
struct FFoundationEligibility
{
    bool bMobile = true, bEnoughFood = true, bItemSlows = false;
};

struct FFoundationIntent
{
    FVector2D Move = {}; // X right, Y forward; independent logical bindings feed this.
    bool bSprint = false, bSneak = false, bJump = false, bAutoJump = false;
    int32 SprintWindowTicks = 7;
};

struct UNNAMED_API FFoundationSimulation
{
    FVector Feet = {}, PreviousFeet = {}, VelocityPerTick = {}, LastDisplacement = {};
    double HeightCm = 180, EyeCm = 162, PreviousEyeCm = 162, FovMultiplier = 1, PreviousFovMultiplier = 1;
    double Accumulator = 0, DroppedSeconds = 0, FallDistanceCm = 0;
    int64 Steps = 0;
    int32 JumpCooldown = 0, SprintTimer = 0, AutoJumpDelay = 0;
    bool bGrounded = false, bCrouched = false, bSprinting = false, bHeadroom = true, bHorizontalCollision = false,
         bHadForward = false;
    FString Rejection = TEXT("unhindered fixture: sufficient food, no effects");
    void Reset(FVector Position);
    int32 Advance(double Seconds, const FFoundationIntent& Intent, double YawDegrees, const UFoundationProfile& P,
        const TArray<FBox>& WorldBoxes, const FFoundationEligibility& Eligibility = {});
    void Step(const FFoundationIntent& Intent, double YawDegrees, const UFoundationProfile& P,
        const TArray<FBox>& WorldBoxes, const FFoundationEligibility& Eligibility = {});
    static FBox Bounds(FVector FeetCm, double Height, double Width);
    static bool Fits(const FBox& Box, const TArray<FBox>& WorldBoxes);
    static FVector Clip(const FBox& Box, FVector Delta, const TArray<FBox>& WorldBoxes);
    static FVector2D ResolveInput(FVector2D Requested, double SlowFactor, double Damping = .98);
};
