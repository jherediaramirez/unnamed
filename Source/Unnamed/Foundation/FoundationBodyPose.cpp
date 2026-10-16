#include "Foundation/FoundationBodyPose.h"
#include "TwoBoneIK.h"

namespace
{
    // A leg is solved to at most this fraction of its length, so the knee never locks straight.
    constexpr double MaxLegReach = .995;

    void SetComponent(TArrayView<FTransform> Local, TConstArrayView<int32> Parents, const TArray<FTransform>& Component,
        int32 Bone, const FTransform& Transform)
    {
        const int32 Parent = Parents[Bone];
        Local[Bone] = Parent == INDEX_NONE ? Transform : Transform.GetRelativeTransform(Component[Parent]);
    }
}

bool FoundationBodyPose::FBones::IsValid() const
{
    return Pelvis != INDEX_NONE && Spine[0] != INDEX_NONE && Spine[1] != INDEX_NONE && Neck != INDEX_NONE &&
        Thigh[0] != INDEX_NONE && Thigh[1] != INDEX_NONE && Calf[0] != INDEX_NONE && Calf[1] != INDEX_NONE &&
        Foot[0] != INDEX_NONE && Foot[1] != INDEX_NONE;
}

double FoundationBodyPose::GetBlend(double PitchDeg, const FFoundationFirstPersonTuning& Tuning)
{
    const double Span = Tuning.StartPitchDeg - Tuning.EndPitchDeg;
    const double T = Span > 0 ? FMath::Clamp((Tuning.StartPitchDeg - PitchDeg) / Span, 0., 1.) : 0.;
    return T * T * (3 - 2 * T);
}

void FoundationBodyPose::ToComponentSpace(
    TConstArrayView<FTransform> Local, TConstArrayView<int32> Parents, TArray<FTransform>& OutComponent)
{
    OutComponent.SetNumUninitialized(Local.Num());
    for (int32 Bone = 0; Bone < Local.Num(); ++Bone)
        OutComponent[Bone] = Parents[Bone] == INDEX_NONE ? Local[Bone] : Local[Bone] * OutComponent[Parents[Bone]];
}

FFoundationFirstPersonResult FoundationBodyPose::Apply(TArrayView<FTransform> Local, TConstArrayView<int32> Parents,
    const FBones& Bones, const FFoundationFirstPersonView& View, const FFoundationFirstPersonTuning& Tuning)
{
    FFoundationFirstPersonResult Result;
    const double Keep = 1 - FMath::Clamp(View.LowWeight, 0., 1.);
    if (!Bones.IsValid() || Keep <= 0) return Result;
    const TArray<FTransform> Original(Local.GetData(), Local.Num());
    const double Blend = Result.Blend = GetBlend(View.PitchDeg, Tuning);
    const double Crouch = FMath::Clamp(View.CrouchAmount, 0., 1.);
    FVector Back = -FVector(View.Forward.X, View.Forward.Y, 0).GetSafeNormal();
    if (Back.IsNearlyZero()) Back = FVector(0, -1, 0);
    const double PelvisBack = Blend * FMath::Lerp(Tuning.PelvisBackStandCm, Tuning.PelvisBackCrouchCm, Crouch);
    const double Anchor = FMath::Lerp(FMath::Lerp(Tuning.AnchorStandCm[0], Tuning.AnchorStandCm[1], Blend),
        FMath::Lerp(Tuning.AnchorCrouchCm[0], Tuning.AnchorCrouchCm[1], Blend), Crouch);

    TArray<FTransform> Component;
    ToComponentSpace(Local, Parents, Component);
    const FTransform FootTarget[2] = {Component[Bones.Foot[0]], Component[Bones.Foot[1]]};

    // 1. The hips back, and down as far as the legs need to still reach the base pose's feet (crouched, at least
    // CrouchDropCm).
    double Drop = Crouch * FMath::Max(Tuning.CrouchDropCm, 0.);
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const FVector Hip = Component[Bones.Thigh[Side]].GetLocation(),
                      Knee = Component[Bones.Calf[Side]].GetLocation();
        const FVector Ankle = FootTarget[Side].GetLocation();
        const double Reach = FMath::Max(
            MaxLegReach * (FVector::Dist(Hip, Knee) + FVector::Dist(Knee, Ankle)), FVector::Dist(Hip, Ankle));
        const FVector Moved = Hip + Back * PelvisBack - Ankle;
        const double Horizontal = FVector2D(Moved).Size();
        if (Moved.Size() > Reach && Horizontal < Reach)
            Drop = FMath::Max(Drop, Moved.Z - FMath::Sqrt(Reach * Reach - Horizontal * Horizontal));
    }
    FTransform Pelvis = Component[Bones.Pelvis];
    Pelvis.AddToTranslation(Back * PelvisBack - FVector(0, 0, Drop));
    SetComponent(Local, Parents, Component, Bones.Pelvis, Pelvis);
    Result.PelvisBackCm = PelvisBack;
    Result.PelvisDropCm = Drop;
    ToComponentSpace(Local, Parents, Component);

    // 2. Each leg re-solved (two-bone) to its base pose's ankle, the foot keeping its base rotation (the ball with it).
    for (int32 Side = 0; Side < 2; ++Side)
    {
        FTransform Thigh = Component[Bones.Thigh[Side]], Calf = Component[Bones.Calf[Side]],
                   Foot = Component[Bones.Foot[Side]];
        // The knee bends the way it already does (forward when the leg is straight).
        const FVector Mid = (Thigh.GetLocation() + Foot.GetLocation()) * .5;
        FVector Bend = Calf.GetLocation() - Mid;
        Bend = Bend.SizeSquared() > 1e-4 ? Bend.GetSafeNormal() : FVector(0, 1, 0);
        AnimationCore::SolveTwoBoneIK(
            Thigh, Calf, Foot, Calf.GetLocation() + Bend * 50, FootTarget[Side].GetLocation(), false, 1, 1);
        Foot.SetRotation(FootTarget[Side].GetRotation());
        Result.FootErrorCm =
            FMath::Max(Result.FootErrorCm, FVector::Dist(Foot.GetLocation(), FootTarget[Side].GetLocation()));
        SetComponent(Local, Parents, Component, Bones.Thigh[Side], Thigh);
        Local[Bones.Calf[Side]] = Calf.GetRelativeTransform(Thigh);
        Local[Bones.Foot[Side]] = Foot.GetRelativeTransform(Calf);
    }
    ToComponentSpace(Local, Parents, Component);

    // 2b. Crouched, the chest turns toward the view about its own vertical (spine_02), the arms with it.
    const double ViewYaw = FMath::Atan2(-View.Forward.X, View.Forward.Y);
    const double Twist = FMath::Clamp(ViewYaw * Crouch * FMath::Clamp(Tuning.CrouchTwist, 0., 1.),
        -FMath::DegreesToRadians(Tuning.MaxTwistDeg), FMath::DegreesToRadians(Tuning.MaxTwistDeg));
    if (FMath::Abs(Twist) > 1e-6)
    {
        FTransform Chest = Component[Bones.Spine[1]];
        Chest.SetRotation(FQuat(FVector::UpVector, Twist) * Chest.GetRotation());
        SetComponent(Local, Parents, Component, Bones.Spine[1], Chest);
        ToComponentSpace(Local, Parents, Component);
    }
    Result.TwistDeg = FMath::RadiansToDegrees(Twist);

    // 3. The chest leans back (about the axis across the view) until neck_01 is Anchor behind the eye: half the way
    // at spine_01, the rest at spine_02, never forward, each bone at most MaxSpineBendDeg.
    const FVector Axis = FVector::CrossProduct(FVector::UpVector, Back);
    const double Start = FVector::DotProduct(Component[Bones.Neck].GetLocation() - View.EyeCm, Back);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const double Behind = Index == 0 ? Start + (Anchor - Start) * .5 : Anchor;
        const FTransform Spine = Component[Bones.Spine[Index]];
        const FVector Pivot = Spine.GetLocation();
        const FVector Arm = Component[Bones.Neck].GetLocation() - Pivot;
        const double ArmBack = FVector::DotProduct(Arm, Back), ArmUp = Arm.Z;
        const double Radius = FMath::Sqrt(ArmBack * ArmBack + ArmUp * ArmUp);
        if (Radius < 1) continue;
        // Rotating by Angle toward Back: the arm's back component becomes Radius * cos(Angle - Phi).
        const double Want = Behind - FVector::DotProduct(Pivot - View.EyeCm, Back);
        const double Phi = FMath::Atan2(ArmUp, ArmBack), Spread = FMath::Acos(FMath::Clamp(Want / Radius, -1., 1.));
        const double A = FMath::UnwindRadians(Phi - Spread), B = FMath::UnwindRadians(Phi + Spread);
        const double Angle =
            FMath::Clamp(FMath::Abs(A) < FMath::Abs(B) ? A : B, 0., FMath::DegreesToRadians(Tuning.MaxSpineBendDeg));
        Result.SpineBendDeg[Index] = FMath::RadiansToDegrees(Angle);
        if (Angle <= 0) continue;
        FTransform Leaned = Spine;
        Leaned.SetRotation(FQuat(Axis, Angle) * Spine.GetRotation());
        SetComponent(Local, Parents, Component, Bones.Spine[Index], Leaned);
        ToComponentSpace(Local, Parents, Component);
    }

    // A partly faded-in low pose fades the corrections out with it.
    if (Keep < 1)
        for (const int32 Bone : {Bones.Pelvis, Bones.Spine[0], Bones.Spine[1], Bones.Thigh[0], Bones.Thigh[1],
                 Bones.Calf[0], Bones.Calf[1], Bones.Foot[0], Bones.Foot[1]})
            Local[Bone].Blend(Original[Bone], FTransform(Local[Bone]), float(Keep));
    ToComponentSpace(Local, Parents, Component);
    const FVector Neck = Component[Bones.Neck].GetLocation();
    Result.NeckBehindEyeCm = FVector::DotProduct(Neck - View.EyeCm, Back);
    Result.NeckBelowEyeCm = View.EyeCm.Z - Neck.Z;
    return Result;
}
