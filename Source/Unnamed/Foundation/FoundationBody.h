#pragma once

#include "CoreMinimal.h"

// The player's body: where it faces and which clips play at what time and weight. Pure, so the native tests drive it
// without a mesh. AFoundationPawn steps the turn with the simulation and advances the animator once per rendered
// frame; UFoundationBodyAnimInstance samples the clips it names.

// foundation.Body.Set: which character the body is. Both use the same clip indices (FoundationBodyClip).
enum class EFoundationBodySet : uint8
{
    // Better Steve (UUnnamedProjectSettings::PlayerBodyRoot): the third-person views only.
    BetterSteve,
    // The Unnamed Player (UnnamedPlayerRoot): both views, with a first-person copy (FoundationBodyPose.h).
    UnnamedPlayer
};

// foundation.Body.Yaw: where the body faces.
enum class EFoundationBodyYaw : uint8
{
    // Always the view's yaw: sideways and backward travel play the strafe and backward clips.
    View,
    // Vanilla's rule: 0.3 of the way per reference step toward the travel direction (travel more than 95 degrees off
    // the view turns toward its opposite, so walking backward keeps facing forward), the head at most 50 degrees off.
    // The constants are unverified (recalled from LivingEntity's tick and tickHeadTurn; no published source found).
    Vanilla,
    // Toward the travel direction, backward included (third-person orient-to-movement); the head looks at most 90
    // degrees off, and back toward the front when the view is behind the body.
    Travel
};

namespace FoundationBodyClip
{
    // Clip indices. Each gait has 8 directions in the order F FR R BR B BL L FL (0, 45, ... -45 degrees, + right).
    enum : int32
    {
        Idle = 0,
        Walk = 1,
        Sprint = Walk + 8,
        CrouchIdle = Sprint + 8,
        CrouchWalk = CrouchIdle + 1,
        StandToCrouch = CrouchWalk + 8,
        CrouchToStand,
        JumpStart,
        JumpLoop,
        JumpLand,
        // The 9 look poses, a row per pitch from looking down: DL D DR, L C R, UL U UR.
        Look,
        // The crouching look poses, the same grid (Better Steve has one grid: these name its standing poses).
        CrouchLook = Look + 9,
        // The low box's pose (swimming); the Unnamed Player only (Better Steve keeps its air loop there).
        Low = CrouchLook + 9,
        // The pickaxe held (the strikes' link pose) and the first strike; the Unnamed Player only (FoundationBodyGrip).
        PickaxeHold,
        StrikeA,
        Count
    };
    // The asset name under the set's root, e.g. "BS_Walk_FR" or "UP_Crouch_FR"; empty when the set has no such clip.
    UNNAMED_API FString GetName(int32 Clip, EFoundationBodySet Set = EFoundationBodySet::BetterSteve);
    // The authored length (s; Better Steve at 30 fps, the Unnamed Player at 60), 0 for a clip the set lacks; the anim
    // instance hands the animator the assets' own lengths.
    UNNAMED_API double GetDefaultLength(int32 Clip, EFoundationBodySet Set = EFoundationBodySet::BetterSteve);
    // The skeletal mesh's name under the set's root: "SK_BetterSteve" or "SK_UnnamedPlayer".
    UNNAMED_API FString GetMeshName(EFoundationBodySet Set);
    // The package of an asset of the set (UUnnamedProjectSettings::GetPlayerBodyAssetPath or
    // GetUnnamedPlayerAssetPath).
    UNNAMED_API FSoftObjectPath GetAssetPath(EFoundationBodySet Set, const FString& AssetName);
    // The ground speed (cm/s) a gait (Walk, Sprint or CrouchWalk) is authored at in Direction (0..7): the game's
    // straight and diagonal speeds, so keyboard input lands exactly on a clip.
    UNNAMED_API double GetSampleSpeed(int32 Gait, int32 Direction);
}

// One rendered frame's facts, from the simulation interpolated to the frame.
struct FFoundationBodyFrame
{
    // Horizontal velocity (cm/s).
    FVector2D Velocity = FVector2D::ZeroVector;
    // The rendered body yaw and the view (control rotation), in degrees.
    double BodyYaw = 0, ViewYaw = 0, ViewPitch = 0;
    // 0 at the standing eye height, 1 at the crouching one (the simulation's smoothed eyes).
    double CrouchAmount = 0;
    bool bCrouched = false, bGrounded = true, bFlying = false;
    // The simulation's low box (swimming): the Low pose where the set has one.
    bool bLowPose = false;
    // Jumps started since the last frame.
    int32 Jumps = 0;
};

struct FFoundationBodyLayer
{
    int32 Clip = 0;
    float Time = 0, Weight = 0;
};

struct FFoundationBodyPose
{
    // The base pose; the weights sum to 1.
    TArray<FFoundationBodyLayer, TInlineAllocator<12>> Base;
    // The head look: mesh-space additive look poses over the base, from the standing and crouching grids; the weights
    // sum to 1.
    TArray<FFoundationBodyLayer, TInlineAllocator<8>> Look;
    // How far the low pose has faded in (0..1): the first-person corrections fade out with it.
    float LowWeight = 0;
    // The upper body's action (FFoundationBodyAction): its clips (weights sum to 1), how much of the upper body they
    // take (0..1, the hold's fade), and the chest's aim toward the view (degrees, + up and right).
    TArray<FFoundationBodyLayer, TInlineAllocator<3>> Action;
    float ActionWeight = 0, AimPitchDeg = 0, AimYawDeg = 0;
    float GetWeight(int32 Clip) const;
};

// The body's yaw, stepped with the simulation (20 Hz) and rendered between its last two steps.
struct UNNAMED_API FFoundationBodyTurn
{
    // Below this horizontal step (cm) the body keeps its target (vanilla: 0.05 blocks a tick). These four constants are
    // unverified (recalled from vanilla's LivingEntity, not checked against the 26.3 jar or a published source).
    static constexpr double MinTravelCm = 5;
    static constexpr double TurnPerStep = .3;
    static constexpr double VanillaHeadLimit = 50;
    double Yaw = 0, PreviousYaw = 0;
    void Reset(double ViewYaw);
    // One reference step: its horizontal displacement (cm) and the view's yaw.
    void Step(FVector2D DisplacementCm, double ViewYaw, EFoundationBodyYaw Mode);
    // The rendered yaw; View follows the frame's own view yaw.
    double GetYaw(double Alpha, double ViewYaw, EFoundationBodyYaw Mode) const;
};

// Picks the clips for a frame: a locomotion blend per stance (idle and 8 directions by speed, one shared phase per
// stance so the feet stay planted), the crouch transitions, the jump (Start, Loop, Land) and the head look.
struct UNNAMED_API FFoundationBodyAnimator
{
    // Any fall without a jump shows the air loop after this long (steps down stay on the ground clips).
    static constexpr double AirGraceSeconds = .15;
    static constexpr double AirBlendSeconds = .1;
    static constexpr double VelocitySmoothingSeconds = .04;
    // The game's eyes halve their remaining height change each 50 ms step (UFoundationProfile::EyeSmoothingPerTick .5);
    // the crouch transition clips are eased the same way.
    static constexpr double EyeHalfLifeSeconds = .05;

    explicit FFoundationBodyAnimator(EFoundationBodySet Set = EFoundationBodySet::BetterSteve);
    // The clips' lengths (s); a clip of length 0 is one the body lacks (the Low pose on Better Steve).
    void SetLengths(const TArray<double>& InLengths);
    void Advance(double Seconds, const FFoundationBodyFrame& Frame);

    enum class EAir : uint8
    {
        Ground,
        Start,
        Loop,
        Land
    };
    FFoundationBodyPose Pose;
    double Lengths[FoundationBodyClip::Count];
    FVector2D Velocity = FVector2D::ZeroVector;
    // Normalized gait phases (0..1), one per stance, and the idles' own times (s).
    double StandPhase = 0, CrouchPhase = 0, IdleTime = 0, CrouchIdleTime = 0;
    bool bCrouchTarget = false, bStarted = false;
    double CrouchTransitionTime = 1e9;
    EAir Air = EAir::Ground;
    // The last air clip (JumpStart or JumpLoop), which fades out under Land on its own clock.
    int32 AirClip = FoundationBodyClip::JumpLoop;
    // AirClipTime is Start's time, LoopTime the loop's, LandTime Land's; StartTime is where Start stopped when it
    // landed early.
    double AirClipTime = 0, LoopTime = 0, LandTime = 0, StartTime = 0, AirborneSeconds = 0, AirWeight = 0,
           LandWeight = 0, LowWeight = 0;
};
