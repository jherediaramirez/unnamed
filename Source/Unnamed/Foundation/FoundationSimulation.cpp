#include "FoundationSimulation.h"

namespace
{
    constexpr double Epsilon = 1e-6;

    bool Overlap(double A0, double A1, double B0, double B1)
    {
        return A1 > B0 + Epsilon && A0 < B1 - Epsilon;
    }

    double ClipAxis(const FBox& Box, double Delta, int32 Axis, const TArray<FBox>& Obstacles)
    {
        for (const FBox& Other : Obstacles)
        {
            const int32 A = (Axis + 1) % 3, B = (Axis + 2) % 3;
            if (!Overlap(Box.Min[A], Box.Max[A], Other.Min[A], Other.Max[A]) ||
                !Overlap(Box.Min[B], Box.Max[B], Other.Min[B], Other.Max[B]))
                continue;
            if (Delta > 0 && Box.Max[Axis] <= Other.Min[Axis] + Epsilon)
                Delta = FMath::Min(Delta, FMath::Max(0., Other.Min[Axis] - Box.Max[Axis]));
            if (Delta < 0 && Box.Min[Axis] >= Other.Max[Axis] - Epsilon)
                Delta = FMath::Max(Delta, FMath::Min(0., Other.Max[Axis] - Box.Min[Axis]));
        }
        return Delta;
    }

    bool Supported(const FBox& Box, double Depth, const TArray<FBox>& Obstacles)
    {
        return ClipAxis(Box, -Depth, 2, Obstacles) > -Depth + Epsilon;
    }
}

void FFoundationSimulation::Reset(FVector Position)
{
    *this = {};
    Feet = PreviousFeet = Position;
}

FBox FFoundationSimulation::Bounds(FVector P, double H, double W)
{
    return FBox(P - FVector(W * .5, W * .5, 0), P + FVector(W * .5, W * .5, H));
}

bool FFoundationSimulation::Fits(const FBox& Box, const TArray<FBox>& WorldBoxes)
{
    for (const FBox& Other : WorldBoxes)
        if (Overlap(Box.Min.X, Box.Max.X, Other.Min.X, Other.Max.X) &&
            Overlap(Box.Min.Y, Box.Max.Y, Other.Min.Y, Other.Max.Y) &&
            Overlap(Box.Min.Z, Box.Max.Z, Other.Min.Z, Other.Max.Z))
            return false;
    return true;
}

FVector FFoundationSimulation::Clip(const FBox& Box, FVector Delta, const TArray<FBox>& WorldBoxes)
{
    FVector Result = {};
    const int32 First = FMath::Abs(Delta.X) < FMath::Abs(Delta.Y) ? 1 : 0;
    for (int32 Axis : {2, First, 1 - First})
        Result[Axis] = ClipAxis(Box.ShiftBy(Result), Delta[Axis], Axis, WorldBoxes);
    return Result;
}

FVector2D FFoundationSimulation::ResolveInput(FVector2D Requested, double SlowFactor, double Damping)
{
    if (Requested.IsNearlyZero()) return {};
    const FVector2D Unit = Requested.GetSafeNormal();
    const double Magnitude = FMath::Min(1., Requested.Size()) * Damping * SlowFactor;
    return Unit * FMath::Min(1., Magnitude / FMath::Max(FMath::Abs(Unit.X), FMath::Abs(Unit.Y)));
}

int32 FFoundationSimulation::Advance(double Seconds, const FFoundationIntent& Intent, double YawDegrees,
    const UFoundationProfile& P, const TArray<FBox>& WorldBoxes, const FFoundationEligibility& Eligibility)
{
    const double SafeStep = FMath::Max(.001, P.TickSeconds);
    const double Budget = SafeStep * FMath::Max(1, P.MaxCatchupSteps);
    Accumulator += FMath::Max(0., Seconds);
    if (Accumulator > Budget)
    {
        DroppedSeconds += Accumulator - Budget;
        Accumulator = Budget;
    }
    int32 Count = 0;
    while (Accumulator + 1e-9 >= SafeStep && Count < P.MaxCatchupSteps)
    {
        Step(Intent, YawDegrees, P, WorldBoxes, Eligibility);
        Accumulator = FMath::Max(0., Accumulator - SafeStep);
        ++Count;
    }
    return Count;
}

void FFoundationSimulation::Step(const FFoundationIntent& Intent, double YawDegrees, const UFoundationProfile& P,
    const TArray<FBox>& WorldBoxes, const FFoundationEligibility& Eligibility)
{
    ++Steps;
    PreviousFeet = Feet;
    PreviousEyeCm = EyeCm;
    PreviousFovMultiplier = FovMultiplier;
    SprintTimer = FMath::Max(0, SprintTimer - 1);
    JumpCooldown = FMath::Max(0, JumpCooldown - 1);
    bHeadroom = Fits(Bounds(Feet, P.StandingHeightCm, P.WidthCm), WorldBoxes);
    bCrouched = Intent.bSneak || !bHeadroom;
    HeightCm = bCrouched ? P.CrouchingHeightCm : P.StandingHeightCm;
    const double TargetEye = bCrouched ? P.CrouchingEyesCm : P.StandingEyesCm;
    EyeCm += (TargetEye - EyeCm) * P.EyeSmoothingPerTick;
    const bool Forward = Intent.Move.Y > 1e-5;
    const bool Possible = Eligibility.bMobile && Eligibility.bEnoughFood;
    const bool CanStart = Possible && Forward && !Eligibility.bItemSlows && !bCrouched;
    if (Intent.bSneak || Eligibility.bItemSlows || Intent.Move.Y < 0) SprintTimer = 0;
    if (!bSprinting && CanStart)
    {
        if (!bHadForward)
        {
            if (SprintTimer > 0)
                bSprinting = true;
            else
                SprintTimer = Intent.SprintWindowTicks;
        }
        if (Intent.bSprint) bSprinting = true;
    }
    if (!Possible || !Forward || bHorizontalCollision) bSprinting = false;
    bHadForward = Forward;
    Rejection = !Eligibility.bMobile ? TEXT("mobility restricted")
        : !Eligibility.bEnoughFood   ? TEXT("sprint: insufficient food")
        : !bHeadroom                 ? TEXT("standing blocked: ceiling")
        : bCrouched                  ? TEXT("sprint start blocked: crouch")
        : !Forward                   ? TEXT("sprint needs forward")
                                     : TEXT("eligible: unhindered fixture");
    FovMultiplier += ((bSprinting ? P.SprintFovMultiplier : 1.) - FovMultiplier) * P.FovSmoothingPerTick;
    if (VelocityPerTick.SizeSquared2D() < FMath::Square(P.StopCutoffCmPerTick))
        VelocityPerTick.X = VelocityPerTick.Y = 0;
    if (FMath::Abs(VelocityPerTick.Z) < P.StopCutoffCmPerTick) VelocityPerTick.Z = 0;
    const bool AutoJump = AutoJumpDelay > 0;
    AutoJumpDelay = 0;
    const bool Jump = Intent.bJump || AutoJump;
    const double Yaw = FMath::DegreesToRadians(YawDegrees);
    const FVector ForwardVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0), RightVector(-FMath::Sin(Yaw), FMath::Cos(Yaw), 0);
    if (Jump && bGrounded && JumpCooldown == 0 && Eligibility.bMobile)
    {
        VelocityPerTick.Z = FMath::Max(P.JumpCmPerTick, VelocityPerTick.Z);
        if (bSprinting) VelocityPerTick += ForwardVector * P.SprintJumpCmPerTick;
        JumpCooldown = P.JumpRepeatTicks;
    }
    if (!Jump) JumpCooldown = 0;
    const bool WasGrounded = bGrounded;
    const double Friction = (WasGrounded ? P.GroundFriction : 1.) * P.HorizontalDrag;
    const FVector2D Input = ResolveInput(
        Eligibility.bMobile ? Intent.Move : FVector2D::ZeroVector, bCrouched ? P.SneakMultiplier : 1., P.InputDamping);
    const double Accel = (WasGrounded ? P.GroundAccelerationCmPerTick : P.AirAccelerationCmPerTick) *
        (bSprinting ? P.SprintMultiplier : 1.);
    VelocityPerTick += (ForwardVector * Input.Y + RightVector * Input.X) * Accel;
    FBox Box = Bounds(Feet, HeightCm, P.WidthCm);
    FVector Wanted = VelocityPerTick;
    if (Intent.bSneak && Wanted.Z <= 0 &&
        (bGrounded || (FallDistanceCm < P.StepCm && Supported(Box, P.StepCm - FallDistanceCm, WorldBoxes))))
    {
        auto Reduce = [&](double V)
        { return FMath::Abs(V) <= P.EdgeIncrementCm ? 0. : V - FMath::Sign(V) * P.EdgeIncrementCm; };
        while (Wanted.X != 0 && !Supported(Box.ShiftBy(FVector(Wanted.X, 0, 0)), P.StepCm, WorldBoxes))
            Wanted.X = Reduce(Wanted.X);
        while (Wanted.Y != 0 && !Supported(Box.ShiftBy(FVector(0, Wanted.Y, 0)), P.StepCm, WorldBoxes))
            Wanted.Y = Reduce(Wanted.Y);
        while (Wanted.X != 0 && Wanted.Y != 0 &&
            !Supported(Box.ShiftBy(FVector(Wanted.X, Wanted.Y, 0)), P.StepCm, WorldBoxes))
        {
            Wanted.X = Reduce(Wanted.X);
            Wanted.Y = Reduce(Wanted.Y);
        }
    }
    FVector Moved = Clip(Box, Wanted, WorldBoxes);
    const bool HitDown = Wanted.Z < 0 && !FMath::IsNearlyEqual(Wanted.Z, Moved.Z, Epsilon);
    if ((WasGrounded || HitDown) &&
        (FMath::Abs(Moved.X - Wanted.X) > Epsilon || FMath::Abs(Moved.Y - Wanted.Y) > Epsilon))
    {
        TArray<double> Candidates;
        for (const FBox& B : WorldBoxes)
        {
            const double H = B.Max.Z - Feet.Z;
            if (H > 0 && H <= P.StepCm + Epsilon) Candidates.AddUnique(H);
        }
        Candidates.Sort();
        for (double H : Candidates)
        {
            const FVector Trial = Clip(Box, FVector(Wanted.X, Wanted.Y, H), WorldBoxes);
            if (Trial.Z + Epsilon < H) continue;
            FVector Settled =
                Trial + Clip(Box.ShiftBy(Trial), FVector(0, 0, FMath::Min(0., Wanted.Z) - Trial.Z), WorldBoxes);
            if (Settled.SizeSquared2D() > Moved.SizeSquared2D() + Epsilon)
            {
                Moved = Settled;
                break;
            }
        }
    }
    bHorizontalCollision = FMath::Abs(Wanted.X - Moved.X) > Epsilon || FMath::Abs(Wanted.Y - Moved.Y) > Epsilon;
    // Vanilla ignores sufficiently shallow glancing collisions for stopping sprint.
    if (bHorizontalCollision && Wanted.SizeSquared2D() > Epsilon && Moved.SizeSquared2D() > Epsilon)
    {
        const double Dot = FVector::DotProduct(Wanted.GetSafeNormal2D(), Moved.GetSafeNormal2D());
        if (Dot > FMath::Cos(FMath::DegreesToRadians(P.MinorCollisionAngleDegrees))) bHorizontalCollision = false;
    }
    bGrounded = (Wanted.Z < 0 && FMath::Abs(Moved.Z - Wanted.Z) > Epsilon) ||
        (Wanted.Z <= 0 && Supported(Box.ShiftBy(Moved), .001, WorldBoxes));
    for (int32 Axis = 0; Axis < 3; ++Axis)
        if (FMath::Abs(Wanted[Axis] - Moved[Axis]) > Epsilon) VelocityPerTick[Axis] = 0;
    Feet += Moved;
    LastDisplacement = Moved;
    FallDistanceCm = bGrounded ? 0 : FallDistanceCm + FMath::Max(0., -Moved.Z);
    VelocityPerTick.Z = (VelocityPerTick.Z - P.GravityCmPerTick) * P.VerticalDrag;
    VelocityPerTick.X *= Friction;
    VelocityPerTick.Y *= Friction;
    if (bSprinting && bHorizontalCollision) bSprinting = false;
    if (Intent.bAutoJump && bGrounded && !Intent.bSneak && !bCrouched && !Input.IsNearlyZero())
    {
        const FVector Direction = (ForwardVector * Input.Y + RightVector * Input.X).GetSafeNormal();
        const double Ahead = FMath::Max(Accel * P.AutoJumpLookaheadTicks, Moved.Size2D());
        if (FVector::DotProduct(Direction, ForwardVector) >= -.15)
        {
            const FVector Obstacle =
                Clip(Bounds(Feet, P.AutoJumpProbeHeightCm, P.WidthCm), Direction * Ahead, WorldBoxes);
            if (Obstacle.Size2D() + .001 < Ahead)
                for (const FBox& B : WorldBoxes)
                {
                    const double H = B.Max.Z - Feet.Z;
                    if (H <= P.StepCm || H > P.AutoJumpHeightCm) continue;
                    const FBox Raised = Bounds(Feet + FVector(0, 0, H + .01), HeightCm, P.WidthCm);
                    if (Fits(Raised, WorldBoxes) &&
                        Clip(Raised, Direction * Ahead, WorldBoxes).Equals(Direction * Ahead, .001) &&
                        !Fits(Bounds(Feet + Direction * Ahead, HeightCm, P.WidthCm), TArray<FBox>{B}))
                    {
                        AutoJumpDelay = 1;
                        break;
                    }
                }
        }
    }
}
