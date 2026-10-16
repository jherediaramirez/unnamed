#pragma once

#include "CoreMinimal.h"

// The first-person body's corrections (PLAN.md section 3.4, "planted feet, chest moves"): only the owner's
// first-person copy of the Unnamed Player runs them, after the base pose; the world body, its shadow and every other
// view keep the plain pose. Looking down, the hips move back with the legs re-solved to the base pose's feet, and the
// chest leans back under the camera, so the belly, legs and feet come into view. Pure (component-space math on a
// bone array), so the native tests drive it without a mesh.

// The frame's view, in the body's component space (cm; the mesh faces +Y, its origin is the feet).
struct FFoundationFirstPersonView
{
    FVector EyeCm = FVector(0, 0, 162);
    // The view's horizontal forward (unit, Z = 0).
    FVector Forward = FVector(0, 1, 0);
    // + up, degrees.
    double PitchDeg = 0;
    // 0 standing, 1 crouched (the eyes' crouch).
    double CrouchAmount = 0;
    // The low pose's weight: the corrections fade out with it (a swimming body is not a standing one).
    double LowWeight = 0;
};

// The tuning: the standing values are PLAN.md 3.4's proposal; the crouched ones Jesus picked from the crouch sweep
// (2026-10-16, setting B: clearance 23.1 cm). foundation.Body.FP.* override them at runtime.
struct FFoundationFirstPersonTuning
{
    // The corrections blend in by smoothstep from looking this far down (degrees, negative = down) ...
    double StartPitchDeg = -20;
    // ... to fully at this pitch.
    double EndPitchDeg = -75;
    // Hips moved back at full blend (cm), standing and crouched.
    double PelvisBackStandCm = 10, PelvisBackCrouchCm = 22;
    // neck_01 at least this far behind the eye (cm, along the view), at no blend and full blend, standing and crouched.
    double AnchorStandCm[2] = {4, 16}, AnchorCrouchCm[2] = {18, 30};
    // The chest lean is shared by spine_01 and spine_02, each at most this much (degrees).
    double MaxSpineBendDeg = 35;
    // Crouched only (scaled by the eyes' crouch, at every pitch): the hips this much lower with the legs re-solved to
    // the base pose's feet, so the shoulders and arms sit further under the low eye (cm) ...
    double CrouchDropCm = 14;
    // ... and the chest turned this fraction of the way toward the view (spine_02, at most MaxTwistDeg), so the near
    // shoulder does not swing into the eye when the view is off the body.
    double CrouchTwist = 1, MaxTwistDeg = 50;
};

// What one frame's corrections did, for the probe and the tests.
struct FFoundationFirstPersonResult
{
    double Blend = 0, PelvisBackCm = 0, PelvisDropCm = 0, SpineBendDeg[2] = {0, 0}, TwistDeg = 0;
    // neck_01 after the corrections: how far behind the eye along the view, and how far below it (cm).
    double NeckBehindEyeCm = 0, NeckBelowEyeCm = 0;
    // The worst leg's ankle miss after the leg re-solve (cm; 0 when every foot reached its base pose's place).
    double FootErrorCm = 0;
};

namespace FoundationBodyPose
{
    // The bones the corrections move, as indices into the pose's bone array (INDEX_NONE when absent).
    struct FBones
    {
        int32 Pelvis = INDEX_NONE, Spine[2] = {INDEX_NONE, INDEX_NONE}, Neck = INDEX_NONE;
        int32 Thigh[2] = {INDEX_NONE, INDEX_NONE}, Calf[2] = {INDEX_NONE, INDEX_NONE},
              Foot[2] = {INDEX_NONE, INDEX_NONE};
        bool IsValid() const;
    };

    // The blend for a pitch: 0 above StartPitchDeg, 1 below EndPitchDeg, smoothstep between.
    UNNAMED_API double GetBlend(double PitchDeg, const FFoundationFirstPersonTuning& Tuning);
    // Component-space transforms from local ones; Parents[Bone] < Bone (INDEX_NONE for the root).
    UNNAMED_API void ToComponentSpace(
        TConstArrayView<FTransform> Local, TConstArrayView<int32> Parents, TArray<FTransform>& OutComponent);
    // Applies the corrections to Local (parent-relative transforms, parents before children) in place.
    UNNAMED_API FFoundationFirstPersonResult Apply(TArrayView<FTransform> Local, TConstArrayView<int32> Parents,
        const FBones& Bones, const FFoundationFirstPersonView& View, const FFoundationFirstPersonTuning& Tuning);

    // The action layer over the base pose (PLAN 5's mask): each bone toward Action by its weight, in component space
    // where MeshSpace says so (the spine: the upper body takes the action's orientation, not the gait's lean), else
    // parent-relative. Local and Action are parent-relative, parents before children.
    UNNAMED_API void BlendUpperBody(TArrayView<FTransform> Local, TConstArrayView<FTransform> Action,
        TConstArrayView<int32> Parents, TConstArrayView<float> Weights, TConstArrayView<bool> MeshSpace);
    // The chest's aim: Chest turned in component space (about its own head) by PitchDeg about the body's lateral axis
    // (+ up) and then YawDeg about the vertical (+ toward the body's right, as the view's yaw).
    UNNAMED_API void ApplyAim(
        TArrayView<FTransform> Local, TConstArrayView<int32> Parents, int32 Chest, double PitchDeg, double YawDeg);
}
