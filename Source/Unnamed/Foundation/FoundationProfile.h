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
    // Creative flight. Steady speeds follow from the air drag model: a * InputDamping / (1 - HorizontalDrag)
    // horizontally, a / (1 - FlyVerticalDrag) vertically.
    // Double-tap jump window; vanilla 7 ticks (Flight Window mod listing, secondary source).
    UPROPERTY(Config, EditAnywhere)
    int32 FlyToggleWindowTicks = 7;
    // 5 cm/tick gives 10.89 blocks/s (wiki Flying: 10.92).
    UPROPERTY(Config, EditAnywhere)
    double FlyAccelerationCmPerTick = 5;
    // Sprint-flying 21.78 blocks/s (wiki Flying: 21.6).
    UPROPERTY(Config, EditAnywhere)
    double FlySprintMultiplier = 2;
    // With FlyVerticalDrag, 7.50 blocks/s up or down (wiki Flying: 7.49); the 15 / .6 split is a feel choice.
    UPROPERTY(Config, EditAnywhere)
    double FlyVerticalCmPerTick = 15;
    UPROPERTY(Config, EditAnywhere)
    double FlyVerticalDrag = .6;
    // Flying widens the view 10% (wiki Flying), on top of the sprint widening.
    UPROPERTY(Config, EditAnywhere)
    double FlyingFovMultiplier = 1.1;
    // Swimming (foundation.Swim 1). Steady speed a * InputDamping / (1 - drag): 1.96 blocks/s in water (wiki
    // Swimming: 1.97), 3.92 sprinting with eyes under (wiki: 3.918). Drags and gravities: scout AA's probe on the
    // 26.3 server (velocity times drag, then gravity, every tick).
    UPROPERTY(Config, EditAnywhere)
    double SwimAccelerationCmPerTick = 2;
    UPROPERTY(Config, EditAnywhere)
    double WaterDrag = .8;
    UPROPERTY(Config, EditAnywhere)
    double WaterSprintDrag = .9;
    UPROPERTY(Config, EditAnywhere)
    double WaterGravityCmPerTick = .5;
    UPROPERTY(Config, EditAnywhere)
    double LavaDrag = .5;
    UPROPERTY(Config, EditAnywhere)
    double LavaGravityCmPerTick = 2;
    // Unverified feel values: held jump rises and sneak sinks by this each tick; a ground jump still works in fluid
    // shallower than ShallowJumpCm at the feet; at a wall, a body raised JumpOutRaiseCm that fits out of the fluid
    // jumps out at JumpOutCmPerTick.
    UPROPERTY(Config, EditAnywhere)
    double SwimVerticalCmPerTick = 4;
    UPROPERTY(Config, EditAnywhere)
    double ShallowJumpCm = 40;
    UPROPERTY(Config, EditAnywhere)
    double JumpOutRaiseCm = 60;
    UPROPERTY(Config, EditAnywhere)
    double JumpOutCmPerTick = 30;
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
