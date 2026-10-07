#pragma once

#include "CoreMinimal.h"
#include "FoundationProfile.generated.h"

// Lengths are centimeters; velocity is centimeters PER REFERENCE TICK (50 ms).
UCLASS(Config = Game, DefaultConfig)
class UNNAMED_API UFoundationProfile : public UObject
{
    GENERATED_BODY()
public:
    static constexpr double BlockCm = 100.; // Project invariant, not a gameplay override.
    UPROPERTY(Config, EditAnywhere)
    double TickSeconds = .05;
    UPROPERTY(Config, EditAnywhere)
    double WidthCm = 60;
    UPROPERTY(Config, EditAnywhere)
    double StandingHeightCm = 180;
    UPROPERTY(Config, EditAnywhere)
    double CrouchingHeightCm = 150;
    UPROPERTY(Config, EditAnywhere)
    double StandingEyesCm = 162;
    UPROPERTY(Config, EditAnywhere)
    double CrouchingEyesCm = 127;
    UPROPERTY(Config, EditAnywhere)
    double StepCm = 60;
    UPROPERTY(Config, EditAnywhere)
    double GroundAccelerationCmPerTick = 10;
    UPROPERTY(Config, EditAnywhere)
    double AirAccelerationCmPerTick = 2;
    UPROPERTY(Config, EditAnywhere)
    double SprintMultiplier = 1.3;
    UPROPERTY(Config, EditAnywhere)
    double CrouchMultiplier = .3;
    UPROPERTY(Config, EditAnywhere)
    double GroundFriction = .6;
    UPROPERTY(Config, EditAnywhere)
    double HorizontalDrag = .91;
    UPROPERTY(Config, EditAnywhere)
    double VerticalDrag = .98;
    UPROPERTY(Config, EditAnywhere)
    double GravityCmPerTick = 8;
    UPROPERTY(Config, EditAnywhere)
    double JumpCmPerTick = 42;
    UPROPERTY(Config, EditAnywhere)
    double SprintJumpCmPerTick = 20;
    UPROPERTY(Config, EditAnywhere)
    double StopCutoffCmPerTick = .3;
    UPROPERTY(Config, EditAnywhere)
    double InputDamping = .98;
    UPROPERTY(Config, EditAnywhere)
    double SprintFovMultiplier = 1.15;
    UPROPERTY(Config, EditAnywhere)
    double EyeSmoothingPerTick = .5;
    UPROPERTY(Config, EditAnywhere)
    double FovSmoothingPerTick = .5;
    UPROPERTY(Config, EditAnywhere)
    double MinorCollisionAngleDegrees = 8;
    UPROPERTY(Config, EditAnywhere)
    int32 JumpRepeatTicks = 10;
    // Double-tap forward window when the sprintWindow preference is unavailable.
    UPROPERTY(Config, EditAnywhere)
    int32 SprintWindowTicks = 7;
    UPROPERTY(Config, EditAnywhere)
    double AutoJumpHeightCm = 120;
    UPROPERTY(Config, EditAnywhere)
    double AutoJumpProbeHeightCm = 51;
    UPROPERTY(Config, EditAnywhere)
    double AutoJumpLookaheadTicks = 7;
    UPROPERTY(Config, EditAnywhere)
    double EdgeIncrementCm = 5;
    UPROPERTY(Config, EditAnywhere)
    double BlockReachCm = 450;
    UPROPERTY(Config, EditAnywhere)
    double EntityReachCm = 300;
    UPROPERTY(Config, EditAnywhere)
    int32 MaxCatchupSteps = 5;
    UPROPERTY(Config, EditAnywhere)
    FVector MinecraftOriginCm = FVector::ZeroVector;
};
