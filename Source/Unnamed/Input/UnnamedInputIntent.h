#pragma once

#include "CoreMinimal.h"
#include "UnnamedInputIntent.generated.h"

// Requests describe intent only. FoundationSimulation resolves pose, gait, velocity and
// eligibility (for example, crouching blocks a sprint start) independently.

USTRUCT(BlueprintType)
struct FUnnamedInputIntent
{
    GENERATED_BODY()
    // X = right, Y = forward. Opposites cancel; diagonals have length <= 1.
    UPROPERTY(BlueprintReadOnly)
    FVector2D Move = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly)
    FVector2D Look = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly)
    bool bCrouchRequested = false;
    UPROPERTY(BlueprintReadOnly)
    bool bSprintRequested = false;
    UPROPERTY(BlueprintReadOnly)
    bool bJumpHeld = false;
    UPROPERTY(BlueprintReadOnly)
    bool bJumpPressed = false;
};

struct FUnnamedInputIntentModel
{
    FUnnamedInputIntent Intent;
    bool bSprintDown = false, bCrouchDown = false;
    bool bToggleSprint = false, bToggleCrouch = false;

    void Move(FVector2D Value) { Intent.Move = Value.GetClampedToMaxSize(1.0); }

    void Sprint(bool bDown)
    {
        if (bDown == bSprintDown) return;
        bSprintDown = bDown;
        if (bToggleSprint)
        {
            if (bDown) Intent.bSprintRequested = !Intent.bSprintRequested;
        }
        else
            Intent.bSprintRequested = bDown;
    }

    void Crouch(bool bDown)
    {
        if (bDown == bCrouchDown) return;
        bCrouchDown = bDown;
        if (bToggleCrouch)
        {
            if (bDown) Intent.bCrouchRequested = !Intent.bCrouchRequested;
        }
        else
            Intent.bCrouchRequested = bDown;
    }

    void Jump(bool bDown)
    {
        if (bDown && !Intent.bJumpHeld) Intent.bJumpPressed = true;
        Intent.bJumpHeld = bDown;
    }

    void Reset()
    {
        Intent = {};
        bSprintDown = bCrouchDown = false;
    }

    void SetPolicies(bool bSprintToggle, bool bCrouchToggle)
    {
        Reset();
        bToggleSprint = bSprintToggle;
        bToggleCrouch = bCrouchToggle;
    }
};
