#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBody.h"

// What the Unnamed Player holds in its real hand, and its strike (PLAN.md sections 5 and 7, M1b). Pure, so the native
// tests drive it without a mesh: AFoundationPawn feeds the action one frame at a time and hands its layers to both
// bodies; the tool meshes hang on the hand_r_grip socket (FoundationPawnBody.cpp).

namespace FoundationBodyGrip
{
    // The items the body holds itself (the camera's held copy hides while it does): the pickaxes. The other items stay
    // on the camera (PLAN's LEGACY-HAND) until their clips come.
    UNNAMED_API bool IsBodyHeld(FName ItemId);
    // The held item's own model (the extruded sprite the view draws on the camera, built at BlockCm per block) placed
    // in the grip socket's tool frame: the handle along the grip (-Y toward the head), the fist at the butt, the
    // sprite's flat in the tool's YZ plane and its top-left pick point on the strike's authored contact point
    // (StrikeTipCm). Vanilla's pickaxe sprites share one shape; the scale (4.41 cm a pixel, a 70 cm sprite) is what
    // lands the point on the contact (vanilla's third-person hand draws it at 85 cm).
    UNNAMED_API FTransform MakeItemGrip(double BlockCm);
    // The strike's tool tip in the grip's tool frame (UE axes; the authored contact point, SM_UP_Pickaxe's tip).
    inline FVector GetStrikeTipCm()
    {
        return FVector(0, -42.75, 26.5);
    }
    // The grip socket on SK_UnnamedPlayer (its tool frame is SM_UP_Pickaxe's: origin the grip, +Z toward the point).
    inline const TCHAR* GetGripSocket()
    {
        return TEXT("hand_r_grip");
    }
}

// One frame's facts for the action, from the pawn.
struct FFoundationBodyActionFrame
{
    // A body-held item is in the main hand (and the player is alive).
    bool bHolding = false;
    // The interaction's press counter: a new value starts a swing, as the view's swing clock reads it.
    uint32 PressSerial = 0;
    // The latest reference step and the frame's fraction of the next (the pawn's render alpha).
    int64 Step = 0;
    double Alpha = 0;
    // The attack held on a block (mining or the delay between breaks): the swing repeats with no rest.
    bool bRepeating = false;
    // The view off the body (degrees, + up / + right), for the chest's aim.
    double ViewPitchDeg = 0, ViewYawOffsetDeg = 0;
};

// The upper body's action: the pickaxe hold, and StrikeA on the view's 6-step swing clock (FoundationBreak's
// FSwingClock): a press starts a swing at the interaction's step, the strike's time is its phase x the clip's length,
// so frame 9 of 18 (the contact) is drawn as step start + 3 begins, the tick the hit lands. It decides nothing.
struct UNNAMED_API FFoundationBodyAction
{
    static constexpr int32 SwingSteps = 6;
    // The hold fades in and out with the item (PLAN 5: a tool switch fades 0.08 s).
    static constexpr double HoldBlendSeconds = .08;
    // A strike starts at once from the hold (frame 0 is the hold's pose), else fades in over StrikeInSeconds; a new
    // press mid-swing restarts it with a RestartSeconds crossfade; it fades back to the hold over StrikeOutSeconds.
    static constexpr double StrikeInSeconds = .05;
    static constexpr double RestartSeconds = .05;
    static constexpr double StrikeOutSeconds = .10;
    // The chest's aim toward the view (PLAN 5, proposed): pitch -45..35, yaw +-30.
    static constexpr double MinAimPitchDeg = -45;
    static constexpr double MaxAimPitchDeg = 35;
    static constexpr double MaxAimYawDeg = 30;

    // Lengths (s): the strike's clip.
    double StrikeLength = 18 / 60.;
    void Advance(double Seconds, const FFoundationBodyActionFrame& Frame);
    // Clears the action (respawn, death): no swing in flight, the hold at the item's state.
    void Reset();
    // Writes the action's layers, weight and aim into Pose.
    void Write(FFoundationBodyPose& Pose) const;

    uint32 LastPressSerial = 0;
    bool bSeenPress = false, bStriking = false;
    int64 StartStep = MIN_int64 / 2;
    // The swing's phase (0..1) as drawn this frame, -1 when none runs.
    double Phase = -1;
    double HoldWeight = 0, StrikeWeight = 0, StrikeTime = 0;
    // The strike a restart replaced, fading out at its last time.
    double FadingWeight = 0, FadingTime = 0;
    double AimPitchDeg = 0, AimYawDeg = 0;
};
