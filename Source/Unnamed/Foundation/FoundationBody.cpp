#include "Foundation/FoundationBody.h"

namespace
{
    const TCHAR* const BodyDirectionNames[8] = {
        TEXT("F"), TEXT("FR"), TEXT("R"), TEXT("BR"), TEXT("B"), TEXT("BL"), TEXT("L"), TEXT("FL")};
    const TCHAR* const BodyLookNames[9] = {
        TEXT("DL"), TEXT("D"), TEXT("DR"), TEXT("L"), TEXT("C"), TEXT("R"), TEXT("UL"), TEXT("U"), TEXT("UR")};

    double MoveBodyWeight(double Weight, double Target, double Seconds)
    {
        return FMath::Abs(Target - Weight) <= Seconds / FFoundationBodyAnimator::AirBlendSeconds
            ? Target
            : Weight + FMath::Sign(Target - Weight) * Seconds / FFoundationBodyAnimator::AirBlendSeconds;
    }
}

FString FoundationBodyClip::GetName(int32 Clip)
{
    if (Clip == Idle) return TEXT("BS_Idle");
    if (Clip >= Walk && Clip < Walk + 8) return FString(TEXT("BS_Walk_")) + BodyDirectionNames[Clip - Walk];
    if (Clip >= Sprint && Clip < Sprint + 8) return FString(TEXT("BS_Sprint_")) + BodyDirectionNames[Clip - Sprint];
    if (Clip == CrouchIdle) return TEXT("BS_CrouchIdle");
    if (Clip >= CrouchWalk && Clip < CrouchWalk + 8)
        return FString(TEXT("BS_CrouchWalk_")) + BodyDirectionNames[Clip - CrouchWalk];
    if (Clip == StandToCrouch) return TEXT("BS_StandToCrouch");
    if (Clip == CrouchToStand) return TEXT("BS_CrouchToStand");
    if (Clip == JumpStart) return TEXT("BS_Jump_Start");
    if (Clip == JumpLoop) return TEXT("BS_Jump_Loop");
    if (Clip == JumpLand) return TEXT("BS_Jump_Land");
    if (Clip >= Look && Clip < Count) return FString(TEXT("BS_Look_")) + BodyLookNames[Clip - Look];
    return FString();
}

double FoundationBodyClip::GetDefaultLength(int32 Clip)
{
    if (Clip >= Walk && Clip < Walk + 8) return 18 / 30.;
    if (Clip >= Sprint && Clip < Sprint + 8) return 16 / 30.;
    if (Clip >= CrouchWalk && Clip < CrouchWalk + 8) return 32 / 30.;
    if (Clip == CrouchIdle || Clip == Idle) return 60 / 30.;
    if (Clip == StandToCrouch || Clip == CrouchToStand) return 8 / 30.;
    if (Clip == JumpStart) return 9 / 30.;
    if (Clip == JumpLoop) return 24 / 30.;
    if (Clip == JumpLand) return 12 / 30.;
    return 0;
}

double FoundationBodyClip::GetSampleSpeed(int32 Gait, int32 Direction)
{
    const bool bDiagonal = Direction % 2 == 1;
    if (Gait == Sprint) return bDiagonal ? 572.69 : 561.23;
    if (Gait == CrouchWalk) return bDiagonal ? 183.16 : 129.52;
    return bDiagonal ? 440.53 : 431.72;
}

float FFoundationBodyPose::GetWeight(int32 Clip) const
{
    float Weight = 0;
    for (const FFoundationBodyLayer& Layer : Base)
        if (Layer.Clip == Clip) Weight += Layer.Weight;
    for (const FFoundationBodyLayer& Layer : Look)
        if (Layer.Clip == Clip) Weight += Layer.Weight;
    return Weight;
}

void FFoundationBodyTurn::Reset(double ViewYaw)
{
    Yaw = PreviousYaw = FMath::UnwindDegrees(ViewYaw);
}

void FFoundationBodyTurn::Step(FVector2D DisplacementCm, double ViewYaw, EFoundationBodyYaw Mode)
{
    PreviousYaw = Yaw;
    if (Mode == EFoundationBodyYaw::View)
    {
        Yaw = FMath::UnwindDegrees(ViewYaw);
        return;
    }
    // Vanilla's LivingEntity (unverified, recalled): the target is the travel direction while moving, else the body's
    // own yaw; the body turns 0.3 of the way to it, then the head limit drags it.
    double Target = Yaw;
    if (DisplacementCm.SizeSquared() > FMath::Square(MinTravelCm))
    {
        const double Travel = FMath::RadiansToDegrees(FMath::Atan2(DisplacementCm.Y, DisplacementCm.X));
        const bool bBackward =
            Mode == EFoundationBodyYaw::Vanilla && FMath::Abs(FMath::FindDeltaAngleDegrees(ViewYaw, Travel)) > 95;
        Target = bBackward ? Travel + 180 : Travel;
    }
    Yaw += FMath::FindDeltaAngleDegrees(Yaw, Target) * TurnPerStep;
    if (Mode == EFoundationBodyYaw::Vanilla)
    {
        const double Head = FMath::FindDeltaAngleDegrees(Yaw, ViewYaw);
        if (FMath::Abs(Head) > VanillaHeadLimit) Yaw += Head - FMath::Sign(Head) * VanillaHeadLimit;
    }
    Yaw = FMath::UnwindDegrees(Yaw);
}

double FFoundationBodyTurn::GetYaw(double Alpha, double ViewYaw, EFoundationBodyYaw Mode) const
{
    if (Mode == EFoundationBodyYaw::View) return FMath::UnwindDegrees(ViewYaw);
    return FMath::UnwindDegrees(PreviousYaw + FMath::FindDeltaAngleDegrees(PreviousYaw, Yaw) * Alpha);
}

FFoundationBodyAnimator::FFoundationBodyAnimator()
{
    for (int32 Clip = 0; Clip < FoundationBodyClip::Count; ++Clip)
        Lengths[Clip] = FoundationBodyClip::GetDefaultLength(Clip);
}

void FFoundationBodyAnimator::SetLengths(const TArray<double>& InLengths)
{
    for (int32 Clip = 0; Clip < FoundationBodyClip::Count && Clip < InLengths.Num(); ++Clip)
        Lengths[Clip] = InLengths[Clip];
}

void FFoundationBodyAnimator::Advance(double Seconds, const FFoundationBodyFrame& Frame)
{
    using namespace FoundationBodyClip;
    const double Dt = FMath::Max(Seconds, 0.);
    // Speed and travel direction relative to the body (+ right).
    Velocity = bStarted ? Velocity + (Frame.Velocity - Velocity) * (1 - FMath::Exp(-Dt / VelocitySmoothingSeconds))
                        : Frame.Velocity;
    const double Speed = Velocity.Size();
    const double Direction = Speed > 1
        ? FMath::FindDeltaAngleDegrees(Frame.BodyYaw, FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X)))
        : 0;
    const double Sector = FMath::Fmod(Direction + 360., 360.) / 45.;
    const int32 D0 = FMath::FloorToInt32(Sector) % 8, D1 = (D0 + 1) % 8;
    const double Between = Sector - FMath::FloorToDouble(Sector);
    auto SampleSpeed = [D0, D1, Between](int32 Gait)
    { return FMath::Lerp(GetSampleSpeed(Gait, D0), GetSampleSpeed(Gait, D1), Between); };
    const double WalkSpeed = SampleSpeed(Walk), SprintSpeed = SampleSpeed(Sprint),
                 CrouchSpeed = SampleSpeed(CrouchWalk);

    // Standing: idle at 0, walk at its speed, sprint at its speed; past the last sample the clip plays faster.
    double StandIdle = 0, StandWalk = 0, StandSprint = 0;
    if (Speed <= WalkSpeed)
        StandIdle = 1 - (StandWalk = Speed / WalkSpeed);
    else if (Speed <= SprintSpeed)
        StandWalk = 1 - (StandSprint = (Speed - WalkSpeed) / (SprintSpeed - WalkSpeed));
    else
        StandSprint = 1;
    const double CrouchWalkWeight = FMath::Min(Speed / CrouchSpeed, 1.), CrouchIdleWeight = 1 - CrouchWalkWeight;

    // One phase per stance, shared by its directions (every loop starts on the left foot's touchdown). Standing still
    // holds it at 0, so the first step is always the left foot's. The clocks move before the pose is built: the
    // frame's pose is at the frame's time, as the body's position is (one frame behind, a planted foot would shift with
    // every change of frame time).
    const double StandCycle = FMath::Lerp(Lengths[Walk], Lengths[Sprint], StandSprint);
    const double StandRate = Speed > SprintSpeed ? Speed / SprintSpeed : 1;
    const double CrouchRate = Speed > CrouchSpeed ? Speed / CrouchSpeed : 1;
    if (Speed < 1)
        StandPhase = CrouchPhase = 0;
    else
    {
        StandPhase = FMath::Frac(StandPhase + Dt * StandRate / FMath::Max(StandCycle, .001));
        CrouchPhase = FMath::Frac(CrouchPhase + Dt * CrouchRate / FMath::Max(Lengths[CrouchWalk], .001));
    }
    IdleTime = FMath::Fmod(IdleTime + Dt, FMath::Max(Lengths[Idle], .001));
    CrouchIdleTime = FMath::Fmod(CrouchIdleTime + Dt, FMath::Max(Lengths[CrouchIdle], .001));

    // Crouch: the stances blend by the eye height; standing still, the transition clip plays from the toggle. The clips
    // are eased as the game's eyes are, so a toggle starts its clip where the eyes are: at rest from the clip's start,
    // toggled back mid-transition part way in (no jump to the other end).
    if (!bStarted)
        bCrouchTarget = Frame.bCrouched;
    else if (Frame.bCrouched != bCrouchTarget)
    {
        bCrouchTarget = Frame.bCrouched;
        const double Down = FMath::Clamp(Frame.CrouchAmount, 1e-6, 1 - 1e-6);
        const double Remaining = bCrouchTarget ? 1 - Down : Down;
        CrouchTransitionTime = FMath::Max(0., EyeHalfLifeSeconds * FMath::Log2(1 / Remaining));
    }
    const int32 Transition = bCrouchTarget ? StandToCrouch : CrouchToStand;
    const bool bTransition = CrouchTransitionTime < Lengths[Transition];
    const double Crouch = FMath::Clamp(Frame.CrouchAmount, 0., 1.);

    // Air: Start from a jump, Loop when Start ends or after AirGraceSeconds of any other fall (flight too), Land on
    // touchdown, then back to the ground clips.
    const bool bAirborne = !Frame.bGrounded || Frame.bFlying;
    if (Frame.Jumps > 0 && !Frame.bFlying)
    {
        Air = EAir::Start;
        AirClip = JumpStart;
        AirClipTime = 0;
    }
    else if (bAirborne)
    {
        AirborneSeconds += Dt;
        if ((Air == EAir::Ground || Air == EAir::Land) && (AirborneSeconds > AirGraceSeconds || Frame.bFlying))
        {
            Air = EAir::Loop;
            AirClip = JumpLoop;
            LoopTime = 0;
        }
        else if (Air == EAir::Start && AirClipTime >= Lengths[JumpStart])
        {
            Air = EAir::Loop;
            AirClip = JumpLoop;
            LoopTime = AirClipTime - Lengths[JumpStart];
        }
    }
    else
    {
        AirborneSeconds = 0;
        if (Air == EAir::Start || Air == EAir::Loop)
        {
            if (Air == EAir::Start) StartTime = AirClipTime;
            Air = EAir::Land;
            LandTime = 0;
        }
        else if (Air == EAir::Land && LandTime >= Lengths[JumpLand])
        {
            Air = EAir::Ground;
            // Land ends on the idle's first frame.
            IdleTime = 0;
        }
    }
    const bool bInAir = Air == EAir::Start || Air == EAir::Loop;
    // Land is in place: landing while moving hands back to locomotion.
    const double LandTarget = Air == EAir::Land ? 1 - FMath::Min(Speed / (.5 * GetSampleSpeed(Walk, 0)), 1.) : 0;
    AirWeight = bStarted ? MoveBodyWeight(AirWeight, bInAir ? 1 : 0, Dt) : (bInAir ? 1 : 0);
    LandWeight = MoveBodyWeight(LandWeight, LandTarget, Dt);
    if (AirWeight + LandWeight > 1) LandWeight = 1 - AirWeight;
    const double Ground = 1 - AirWeight - LandWeight;

    Pose.Base.Reset();
    Pose.Look.Reset();
    auto Add = [](TArray<FFoundationBodyLayer, TInlineAllocator<12>>& Layers, int32 Clip, double Time, double Weight)
    {
        if (Weight > 1e-4) Layers.Add({Clip, float(Time), float(Weight)});
    };
    auto AddGait = [&](int32 Gait, double Phase, double Weight)
    {
        Add(Pose.Base, Gait + D0, Phase * Lengths[Gait + D0], Weight * (1 - Between));
        Add(Pose.Base, Gait + D1, Phase * Lengths[Gait + D1], Weight * Between);
    };
    const double Stand = Ground * (1 - Crouch), Crouched = Ground * Crouch;
    if (bTransition)
        Add(Pose.Base, Transition, CrouchTransitionTime, Stand * StandIdle + Crouched * CrouchIdleWeight);
    else
    {
        Add(Pose.Base, Idle, IdleTime, Stand * StandIdle);
        Add(Pose.Base, CrouchIdle, CrouchIdleTime, Crouched * CrouchIdleWeight);
    }
    AddGait(Walk, StandPhase, Stand * StandWalk);
    AddGait(Sprint, StandPhase, Stand * StandSprint);
    AddGait(CrouchWalk, CrouchPhase, Crouched * CrouchWalkWeight);
    // The air clip keeps its own clock while it fades out under Land.
    if (AirClip == JumpStart)
        Add(Pose.Base, JumpStart, FMath::Min(Air == EAir::Start ? AirClipTime : StartTime, Lengths[JumpStart]),
            AirWeight);
    else
        Add(Pose.Base, JumpLoop, FMath::Fmod(LoopTime, FMath::Max(Lengths[JumpLoop], .001)), AirWeight);
    // Land too fades out on its own clock (a jump straight from a landing).
    Add(Pose.Base, JumpLand, FMath::Min(LandTime, Lengths[JumpLand]), LandWeight);
    double Total = 0;
    for (const FFoundationBodyLayer& Layer : Pose.Base)
        Total += Layer.Weight;
    if (Total <= 0)
        Pose.Base.Add({Idle, float(IdleTime), 1.f});
    else
        for (FFoundationBodyLayer& Layer : Pose.Base)
            Layer.Weight = float(Layer.Weight / Total);

    // The head: the look poses on a 3 x 3 grid (yaw and pitch -90, 0, 90), bilinear. A view behind the body (travel
    // facing) turns the head back toward the front rather than holding it 90 degrees sideways.
    double LookYaw = FMath::FindDeltaAngleDegrees(Frame.BodyYaw, Frame.ViewYaw);
    if (FMath::Abs(LookYaw) > 90) LookYaw = FMath::Sign(LookYaw) * (180 - FMath::Abs(LookYaw));
    const double LookPitch = FMath::Clamp(FMath::UnwindDegrees(Frame.ViewPitch), -90., 90.);
    const double GridX = (LookYaw + 90) / 90, GridY = (LookPitch + 90) / 90;
    const int32 X0 = FMath::Min(FMath::FloorToInt32(GridX), 1), Y0 = FMath::Min(FMath::FloorToInt32(GridY), 1);
    const double FX = GridX - X0, FY = GridY - Y0;
    const double Corners[4] = {(1 - FX) * (1 - FY), FX * (1 - FY), (1 - FX) * FY, FX * FY};
    for (int32 Corner = 0; Corner < 4; ++Corner)
        if (Corners[Corner] > 1e-4)
            Pose.Look.Add({Look + (Y0 + Corner / 2) * 3 + X0 + Corner % 2, 0.f, float(Corners[Corner])});

    // The event clocks start at 0 on their event's frame.
    CrouchTransitionTime += Dt;
    AirClipTime += Dt;
    LoopTime += Dt;
    LandTime += Dt;
    bStarted = true;
}
