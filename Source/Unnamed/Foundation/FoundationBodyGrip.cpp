#include "Foundation/FoundationBodyGrip.h"
#include "Foundation/FoundationItemMesh.h"

namespace
{
    double MoveActionWeight(double Weight, double Target, double Seconds, double BlendSeconds)
    {
        if (BlendSeconds <= 0) return Target;
        const double Step = Seconds / BlendSeconds;
        return FMath::Abs(Target - Weight) <= Step ? Target : Weight + FMath::Sign(Target - Weight) * Step;
    }
}

bool FoundationBodyGrip::IsBodyHeld(FName ItemId)
{
    return !ItemId.IsNone() && ItemId.ToString().EndsWith(TEXT("_pickaxe"));
}

FTransform FoundationBodyGrip::MakeItemGrip(double BlockCm)
{
    // The pickaxe sprite in model pixels (Mojang, Y up, the slab's middle at Z 8): the handle's centre line runs at 45
    // degrees from the butt (2, 1) to the head's outer corner (13.5, 12.5); the head's points are (5, 12.5) and
    // (13.5, 4), mirror images across it.
    auto Model = [BlockCm](double X, double Y)
    { return FVector(FoundationItemMesh::ToUnreal(FVector3f(float(X), float(Y), 8.f), BlockCm)); };
    const FVector Butt = Model(2, 1), Corner = Model(13.5, 12.5), Point = Model(5, 12.5);
    const FVector Along = (Corner - Butt).GetSafeNormal();
    const FVector ToPoint = Point - Butt;
    const double PointAlong = FVector::DotProduct(ToPoint, Along);
    const FVector Across = ToPoint - Along * PointAlong;
    const FVector Side = Across.GetSafeNormal();
    const FVector Normal = FVector::CrossProduct(Along, Side);
    // The point lands on the contact: its distance off the handle sets the scale, its distance along it the grip.
    const FVector Tip = GetStrikeTipCm();
    const double Scale = Tip.Z / Across.Size();
    const FVector Grip = Butt + Along * (PointAlong - FMath::Abs(Tip.Y) / Scale);
    // Along -> tool -Y (toward the head), Side -> +Z (toward the point), their normal -> -X: rows are the images of
    // the mesh axes.
    const FVector ToAlong(0, -1, 0), ToSide(0, 0, 1), ToNormal = FVector::CrossProduct(ToAlong, ToSide);
    FMatrix Rotation = FMatrix::Identity;
    for (int32 Axis = 0; Axis < 3; ++Axis)
        Rotation.SetAxis(Axis, ToAlong * Along[Axis] + ToSide * Side[Axis] + ToNormal * Normal[Axis]);
    const FQuat Turn(Rotation);
    return FTransform(Turn, -Turn.RotateVector(Grip * Scale), FVector(Scale));
}
void FFoundationBodyAction::Reset()
{
    bStriking = false;
    Phase = -1;
    StrikeWeight = FadingWeight = 0;
    StrikeTime = FadingTime = 0;
}

void FFoundationBodyAction::Advance(double Seconds, const FFoundationBodyActionFrame& Frame)
{
    const double Dt = FMath::Max(Seconds, 0.);
    // A new press starts a swing at the interaction's step (the first frame only learns the counter).
    if (!bSeenPress)
    {
        bSeenPress = true;
        LastPressSerial = Frame.PressSerial;
    }
    else if (Frame.PressSerial != LastPressSerial)
    {
        LastPressSerial = Frame.PressSerial;
        if (Frame.bHolding)
        {
            // Mid-swing, the old strike crossfades out at its last time while the new one starts at frame 0.
            if (bStriking && StrikeWeight > 0)
            {
                FadingTime = StrikeTime;
                FadingWeight = StrikeWeight;
            }
            StartStep = Frame.Step;
            bStriking = true;
        }
    }
    if (!Frame.bHolding) bStriking = false;
    HoldWeight = MoveActionWeight(HoldWeight, Frame.bHolding ? 1 : 0, Dt, HoldBlendSeconds);

    // The swing clock: phase = (step + alpha - start) / 6, repeating while the attack is held on a block.
    Phase = -1;
    if (bStriking)
    {
        const double T = (double(Frame.Step - StartStep) + Frame.Alpha) / SwingSteps;
        if (T < 0)
            Phase = 0;
        else if (Frame.bRepeating)
            Phase = T - FMath::FloorToDouble(T);
        else if (T < 1)
            Phase = T;
        else
            bStriking = false;
    }
    if (Phase >= 0)
        StrikeTime = Phase * StrikeLength;
    else if (StrikeWeight > 0)
        // Fading back to the hold from the strike's end (frame 18, the hold's pose).
        StrikeTime = StrikeLength;
    // In: at once from a full hold, else StrikeInSeconds; out: StrikeOutSeconds.
    if (bStriking)
        StrikeWeight = HoldWeight >= 1 ? 1 : MoveActionWeight(StrikeWeight, 1, Dt, StrikeInSeconds);
    else
        StrikeWeight = MoveActionWeight(StrikeWeight, 0, Dt, StrikeOutSeconds);
    FadingWeight = MoveActionWeight(FadingWeight, 0, Dt, RestartSeconds);

    AimPitchDeg = FMath::Clamp(Frame.ViewPitchDeg, MinAimPitchDeg, MaxAimPitchDeg);
    AimYawDeg = FMath::Clamp(Frame.ViewYawOffsetDeg, -MaxAimYawDeg, MaxAimYawDeg);
}

void FFoundationBodyAction::Write(FFoundationBodyPose& Pose) const
{
    using namespace FoundationBodyClip;
    Pose.Action.Reset();
    Pose.ActionWeight = float(HoldWeight);
    Pose.AimPitchDeg = float(AimPitchDeg);
    Pose.AimYawDeg = float(AimYawDeg);
    if (HoldWeight <= 1e-4)
    {
        Pose.ActionWeight = 0;
        return;
    }
    // Within the upper body: the fading strike, the strike, then the hold for the rest.
    const double Fading = FMath::Min(FadingWeight, 1.);
    const double Strike = FMath::Min(StrikeWeight, 1.) * (1 - Fading);
    const double Hold = FMath::Max(0., 1 - Fading - Strike);
    if (Fading > 1e-4) Pose.Action.Add({StrikeA, float(FadingTime), float(Fading)});
    if (Strike > 1e-4) Pose.Action.Add({StrikeA, float(StrikeTime), float(Strike)});
    if (Hold > 1e-4) Pose.Action.Add({PickaxeHold, 0.f, float(Hold)});
}
