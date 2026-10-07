#pragma once
#include "CoreMinimal.h"
#include "FoundationSimulation.generated.h"

// Lengths are centimeters; velocity is centimeters PER REFERENCE TICK (50 ms).
UCLASS(Config=Game, DefaultConfig)
class UNNAMED_API UFoundationProfile : public UObject
{
    GENERATED_BODY()
public:
    static constexpr double BlockCm = 100.; // Project invariant, not a gameplay override.
    UPROPERTY(Config, EditAnywhere) double TickSeconds = .05;
    UPROPERTY(Config, EditAnywhere) double WidthCm = 60;
    UPROPERTY(Config, EditAnywhere) double StandingHeightCm = 180;
    UPROPERTY(Config, EditAnywhere) double CrouchingHeightCm = 150;
    UPROPERTY(Config, EditAnywhere) double StandingEyesCm = 162;
    UPROPERTY(Config, EditAnywhere) double CrouchingEyesCm = 127;
    UPROPERTY(Config, EditAnywhere) double StepCm = 60;
    UPROPERTY(Config, EditAnywhere) double GroundAccelerationCmPerTick = 10;
    UPROPERTY(Config, EditAnywhere) double AirAccelerationCmPerTick = 2;
    UPROPERTY(Config, EditAnywhere) double SprintMultiplier = 1.3;
    UPROPERTY(Config, EditAnywhere) double SneakMultiplier = .3;
    UPROPERTY(Config, EditAnywhere) double GroundFriction = .6;
    UPROPERTY(Config, EditAnywhere) double HorizontalDrag = .91;
    UPROPERTY(Config, EditAnywhere) double VerticalDrag = .98;
    UPROPERTY(Config, EditAnywhere) double GravityCmPerTick = 8;
    UPROPERTY(Config, EditAnywhere) double JumpCmPerTick = 42;
    UPROPERTY(Config, EditAnywhere) double SprintJumpCmPerTick = 20;
    UPROPERTY(Config, EditAnywhere) double StopCutoffCmPerTick = .3;
    UPROPERTY(Config, EditAnywhere) double InputDamping = .98;
    UPROPERTY(Config, EditAnywhere) double SprintFovMultiplier = 1.15;
    UPROPERTY(Config, EditAnywhere) double EyeSmoothingPerTick = .5;
    UPROPERTY(Config, EditAnywhere) double FovSmoothingPerTick = .5;
    UPROPERTY(Config, EditAnywhere) double MinorCollisionAngleDegrees = 8;
    UPROPERTY(Config, EditAnywhere) int32 JumpRepeatTicks = 10;
    UPROPERTY(Config, EditAnywhere) double AutoJumpHeightCm = 120;
    UPROPERTY(Config, EditAnywhere) double AutoJumpProbeHeightCm = 51;
    UPROPERTY(Config, EditAnywhere) double AutoJumpLookaheadTicks = 7;
    UPROPERTY(Config, EditAnywhere) double EdgeIncrementCm = 5;
    UPROPERTY(Config, EditAnywhere) double BlockReachCm = 450;
    UPROPERTY(Config, EditAnywhere) double EntityReachCm = 300;
    UPROPERTY(Config, EditAnywhere) int32 MaxCatchupSteps = 5;
    UPROPERTY(Config, EditAnywhere) FVector MinecraftOriginCm = FVector::ZeroVector;
};

namespace FoundationCoordinates
{
    inline FVector ToMinecraft(FVector LocalFeetCm, FVector OriginCm = {}, FIntVector RebaseCm = {})
    { const FVector P = (LocalFeetCm + FVector(RebaseCm) - OriginCm) / UFoundationProfile::BlockCm; return {P.X, P.Z, P.Y}; }
    inline FVector ToUnreal(FVector MC, FVector OriginCm = {}, FIntVector RebaseCm = {})
    { return FVector(MC.X, MC.Z, MC.Y) * UFoundationProfile::BlockCm + OriginCm - FVector(RebaseCm); }
    inline FInt64Vector3 Blocks(FVector MC) { return {FMath::FloorToInt64(MC.X), FMath::FloorToInt64(MC.Y), FMath::FloorToInt64(MC.Z)}; }
    inline double Yaw(double UEYaw) { return FRotator::NormalizeAxis(UEYaw - 90.); }
    inline double Pitch(double UEPitch) { return -UEPitch; }
    inline double ToUnrealYaw(double MCYaw) { return FRotator::NormalizeAxis(MCYaw+90.); }
    inline double ToUnrealPitch(double MCPitch) { return -MCPitch; }
    inline double HorizontalFov(double VerticalDegrees, double Aspect)
    { return FMath::RadiansToDegrees(2. * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalDegrees) * .5) * Aspect)); }
    inline FString Facing(double UEYaw)
    { const int32 Index = (FMath::RoundToInt(FRotator::ClampAxis(UEYaw) / 90.) % 4); return TArray<FString>{TEXT("east (+X)"),TEXT("south (+Z)"),TEXT("west (-X)"),TEXT("north (-Z)")}[Index]; }
}

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
    bool bGrounded = false, bCrouched = false, bSprinting = false, bHeadroom = true, bHorizontalCollision = false, bHadForward = false;
    FString Rejection = TEXT("unhindered fixture: sufficient food, no effects");
    void Reset(FVector Position);
    int32 Advance(double Seconds, const FFoundationIntent& Intent, double YawDegrees, const UFoundationProfile& P,
        const TArray<FBox>& WorldBoxes, const FFoundationEligibility& Eligibility = {});
    void Step(const FFoundationIntent& Intent, double YawDegrees, const UFoundationProfile& P,
        const TArray<FBox>& WorldBoxes, const FFoundationEligibility& Eligibility = {});
    static FBox Bounds(FVector FeetCm, double Height, double Width);
    static bool Fits(const FBox& Box, const TArray<FBox>& WorldBoxes);
    static FVector Clip(const FBox& Box, FVector Delta, const TArray<FBox>& WorldBoxes);
    static FVector2D ResolveInput(FVector2D Requested, double SlowFactor,double Damping=.98);
};
