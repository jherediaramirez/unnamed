#include "Foundation/FoundationBodyGrip.h"

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

FLinearColor FoundationBodyGrip::GetHandleColor()
{
    // The builder's M_Prop_Wood (up_build.py build_props).
    return FLinearColor(.42f, .29f, .16f);
}

FLinearColor FoundationBodyGrip::GetHeadColor(FName ItemId)
{
    // Flat stand-ins for the vanilla tiers until the tool gets its own texture (provisional).
    const FString Id = ItemId.ToString();
    if (Id.StartsWith(TEXT("wooden"))) return FLinearColor(.55f, .40f, .22f);
    if (Id.StartsWith(TEXT("iron"))) return FLinearColor(.80f, .80f, .80f);
    if (Id.StartsWith(TEXT("golden"))) return FLinearColor(.95f, .78f, .20f);
    if (Id.StartsWith(TEXT("diamond"))) return FLinearColor(.30f, .88f, .82f);
    if (Id.StartsWith(TEXT("netherite"))) return FLinearColor(.27f, .24f, .25f);
    // Stone, and the builder's M_Prop_PickStone.
    return FLinearColor(.50f, .50f, .52f);
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
