#include "Foundation/FoundationMobs.h"
#include "Foundation/FoundationItemEntities.h"
#include "Foundation/FoundationSimulation.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarMobs(TEXT("foundation.Mobs"), 1,
        TEXT("1: mobs exist (survival T6 20a): foundation.Mobs.Summon cow spawns one that wanders, panics when hit, is "
             "knocked back and dies into its loot and experience. 0: no mobs are spawned, stepped or drawn, and only "
             "training targets can be hit, as before."));

    // [mobs.base_attributes, catalog.entities; value-swaps.md T6] health, movement attribute, armor, toughness,
    // knockback resistance, width x height (eye); [survival.r10.ai_defaults] the panic modifier; [survival.r5.play_calls]
    // the volume; [survival.r10.death_roll] the roll; wiki "Experience" (provisional) the experience.
    const FFoundationMobKind MobKinds[] = {
        {TEXT("cow"), 10, .2f, 0, 0, 0, 90, 140, 130, 2.f, .4f, 90, 1, 3, true},
        {TEXT("pig"), 10, .25f, 0, 0, 0, 90, 90, 76.5, 1.25f, 1, 90, 1, 3, false},
        {TEXT("sheep"), 8, .23f, 0, 0, 0, 90, 130, 123.5, 1.25f, 1, 90, 1, 3, false},
        {TEXT("chicken"), 4, .25f, 0, 0, 0, 40, 70, 64.4, 1.4f, 1, 90, 1, 3, false},
        {TEXT("zombie"), 20, .23f, 2, 0, 0, 60, 195, 174, 0, 1, 90, 5, 5, false},
        {TEXT("skeleton"), 20, .25f, 0, 0, 0, 60, 199, 174, 0, 1, 90, 5, 5, false},
        {TEXT("creeper"), 20, .25f, 0, 0, 0, 60, 170, 144.5, 0, 1, 90, 5, 5, false},
        {TEXT("spider"), 16, .3f, 0, 0, 0, 140, 90, 65, 0, 1, 180, 5, 5, false},
    };
    static_assert(UE_ARRAY_COUNT(MobKinds) == int32(EFoundationMobType::Count));

    FVector2D MobHorizontal(const FVector& V) { return FVector2D(V.X, V.Y); }

    // A random point around From within Range cells horizontally (whole cells, as the AI's random positions); the
    // vertical draw is kept for the record but steering is horizontal until 20b's A*.
    FVector MobRandomTarget(FFoundationMob& Mob, const FVector& From, int32 Range, int32 RangeUp)
    {
        const double Block = 100;
        const int32 X = Mob.Random.RandRange(-Range, Range), Y = Mob.Random.RandRange(-Range, Range);
        Mob.Random.RandRange(-RangeUp, RangeUp);
        return From + FVector(X * Block, Y * Block, 0);
    }

    void MobStartMove(FFoundationMob& Mob, const FVector& Target, float Modifier, bool bPanic)
    {
        Mob.bMoving = true;
        Mob.bPanicking = bPanic;
        Mob.TargetCm = Target;
        Mob.SpeedModifier = Modifier;
        Mob.MoveTicks = Mob.StuckTicks = 0;
    }

    float MobTurnToward(float From, float To, float MaxStep)
    {
        return From + FMath::Clamp(FMath::FindDeltaAngleDegrees(From, To), -MaxStep, MaxStep);
    }
}

bool FoundationMobs::IsOn()
{
    return CVarMobs.GetValueOnAnyThread() != 0;
}

const FFoundationMobKind& FoundationMobs::GetKind(EFoundationMobType Type)
{
    return MobKinds[FMath::Clamp(int32(Type), 0, int32(EFoundationMobType::Count) - 1)];
}

bool FoundationMobs::FindType(FName Id, EFoundationMobType& OutType)
{
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(MobKinds); ++Index)
        if (MobKinds[Index].Id == Id)
        {
            OutType = EFoundationMobType(Index);
            return true;
        }
    return false;
}

FBox FoundationMobs::GetBox(EFoundationMobType Type, const FVector& WorldCm)
{
    const FFoundationMobKind& Kind = GetKind(Type);
    const FVector Half(Kind.WidthCm * .5, Kind.WidthCm * .5, 0);
    return FBox(WorldCm - Half, WorldCm + Half + FVector(0, 0, Kind.HeightCm));
}

FBox FoundationMobs::GetStepRegion(const FFoundationMob& Mob, const FVector& OriginCm)
{
    // This step's velocity with the most input and a jump, the step-up's rise, and a centimetre for contact.
    const double Drive = GetKind(Mob.Type).Speed * Mob.SpeedModifier;
    const double Accel = FMath::Max(Drive * Drive * WalkAccelCm, AirAccelCm);
    const FVector Reach =
        Mob.VelocityCm.GetAbs() + FVector(Accel, Accel, FMath::Max(JumpCm, StepCm)) + FVector(GravityCm + 1);
    return GetBox(Mob.Type, OriginCm + Mob.LocalCm).ExpandBy(Reach);
}

void FoundationMobs::ApplyKnockback(FVector& VelocityCm, const FVector2D& Away, bool bGrounded, double PowerCm)
{
    if (PowerCm <= 0) return;
    const FVector2D Unit = Away.GetSafeNormal();
    VelocityCm.X = VelocityCm.X * .5 + Unit.X * PowerCm;
    VelocityCm.Y = VelocityCm.Y * .5 + Unit.Y * PowerCm;
    if (bGrounded) VelocityCm.Z = FMath::Min(PowerCm, VelocityCm.Z * .5 + PowerCm);
}

void FoundationMobs::OnHurt(FFoundationMob& Mob, const FVector& OriginCm, const FVector& AttackerWorldCm,
    bool bFreshHit, bool bByPlayer, int32 Player)
{
    const FFoundationMobKind& Kind = GetKind(Mob.Type);
    Mob.HurtTime = HurtTicks;
    if (bFreshHit)
    {
        FVector2D Away = MobHorizontal(OriginCm + Mob.LocalCm - AttackerWorldCm);
        // Standing on the attacker: a random direction, as the knockback needs one.
        if (Away.IsNearlyZero())
            Away = FVector2D(Mob.Random.FRandRange(-1, 1), Mob.Random.FRandRange(-1, 1));
        ApplyKnockback(Mob.VelocityCm, Away, Mob.bOnGround, KnockbackCm * (1 - Kind.KnockbackResistance));
    }
    Mob.bHurtByEntity = true;
    Mob.HurtFromCm = AttackerWorldCm - OriginCm;
    if (Mob.Health.IsDead())
    {
        Mob.bKilledByPlayer = bByPlayer;
        Mob.Killer = Player;
    }
}

void FoundationMobs::Think(FFoundationMob& Mob, const FVector& OriginCm, TConstArrayView<FVector> PlayerEyes)
{
    if (!Mob.IsAlive()) return;
    const FFoundationMobKind& Kind = GetKind(Mob.Type);
    // Panic (priority 1) takes over from the stroll (priority 5) on a hit by an entity, and runs leg after leg while
    // the hit is recent.
    if (Mob.bHurtByEntity) Mob.SinceHurt = 0;
    const bool bRecent = Mob.SinceHurt < PanicMemoryTicks;
    ++Mob.SinceHurt;
    if (Kind.PanicSpeed > 0 && (Mob.bHurtByEntity || (bRecent && Mob.bPanicking && !Mob.bMoving)))
        MobStartMove(Mob, MobRandomTarget(Mob, Mob.LocalCm, PanicRangeCells, PanicRangeUpCells), Kind.PanicSpeed, true);
    Mob.bHurtByEntity = false;
    if (!Mob.bMoving) Mob.bPanicking = false;
    if (!Mob.bMoving && Mob.Random.RandHelper(StrollChance) == 0)
        MobStartMove(Mob, MobRandomTarget(Mob, Mob.LocalCm, StrollRangeCells, StrollRangeUpCells), 1, false);
    // Look-at-player (priority 6) runs beside the movement goals.
    const FVector Eye = OriginCm + Mob.LocalCm + FVector(0, 0, Kind.EyeCm);
    const FVector* Nearest = nullptr;
    double Best = LookRangeCm * LookRangeCm;
    for (const FVector& Player : PlayerEyes)
        if (const double Distance = FVector::DistSquared(Player, Eye); Distance < Best)
        {
            Best = Distance;
            Nearest = &Player;
        }
    if (Mob.LookTicks > 0)
        --Mob.LookTicks;
    else if (Nearest && Mob.Random.GetFraction() < LookChance)
        Mob.LookTicks = LookMinTicks + Mob.Random.RandHelper(LookRandomTicks);
    if (Nearest && Mob.LookTicks > 0)
        Mob.LookAtCm = *Nearest - OriginCm;
    else
        Mob.LookTicks = 0;
}

FVector2D FoundationMobs::Steer(FFoundationMob& Mob, const FVector& OriginCm, bool& bOutJump)
{
    bOutJump = false;
    if (!Mob.IsAlive() || !Mob.bMoving) return FVector2D::ZeroVector;
    const FVector2D ToTarget = MobHorizontal(Mob.TargetCm - Mob.LocalCm);
    const double Distance = ToTarget.Size();
    const double Before = MobHorizontal(Mob.TargetCm - Mob.PreviousLocalCm).Size();
    Mob.StuckTicks = Before - Distance < StuckCm ? Mob.StuckTicks + 1 : 0;
    if (Distance < ArriveCm || Mob.StuckTicks >= GiveUpTicks || ++Mob.MoveTicks > MaxMoveTicks)
    {
        Mob.bMoving = false;
        return FVector2D::ZeroVector;
    }
    const float Heading = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X));
    Mob.Yaw = FMath::UnwindDegrees(MobTurnToward(Mob.Yaw, Heading, BodyTurnDegrees));
    // Blocked on the ground past the step-up: jump.
    bOutJump = Mob.bOnGround && Mob.bHorizontalCollision;
    return ToTarget / Distance;
}

void FoundationMobs::Move(
    FFoundationMob& Mob, const FVector& OriginCm, const TArray<FBox>& Boxes, const FVector2D& InputCm, bool bJump)
{
    Mob.PreviousLocalCm = Mob.LocalCm;
    const bool bWasGrounded = Mob.bOnGround;
    FVector& Velocity = Mob.VelocityCm;
    const double Drive = GetKind(Mob.Type).Speed * Mob.SpeedModifier;
    const FVector2D Push = InputCm * (bWasGrounded ? Drive * Drive * WalkAccelCm : AirAccelCm);
    Velocity.X += Push.X;
    Velocity.Y += Push.Y;
    if (bJump && bWasGrounded) Velocity.Z = JumpCm;
    const FBox Box = GetBox(Mob.Type, OriginCm + Mob.LocalCm);
    FVector Moved = FFoundationSimulation::Clip(Box, Velocity, Boxes);
    const auto Blocked = [&Velocity](const FVector& Delta) { return Delta.X != Velocity.X || Delta.Y != Velocity.Y; };
    bool bStepped = false;
    // [mobs.base_attributes] a grounded mob blocked sideways climbs up to the step height: up, across, back down.
    if (bWasGrounded && Blocked(Moved) && Velocity.Z <= 0)
    {
        const FVector Up = FFoundationSimulation::Clip(Box, FVector(0, 0, StepCm), Boxes);
        const FVector Across = FFoundationSimulation::Clip(Box.ShiftBy(Up), FVector(Velocity.X, Velocity.Y, 0), Boxes);
        const FVector Down =
            FFoundationSimulation::Clip(Box.ShiftBy(Up + Across), FVector(0, 0, -Up.Z + FMath::Min(Velocity.Z, 0.)), Boxes);
        const FVector Stepped = Up + Across + Down;
        if (MobHorizontal(Stepped).SizeSquared() > MobHorizontal(Moved).SizeSquared() + 1e-6 && Down.Z > -Up.Z + Velocity.Z)
        {
            Moved = Stepped;
            bStepped = true;
        }
    }
    Mob.LocalCm += Moved;
    Mob.bHorizontalCollision = Blocked(Moved) && !bStepped;
    Mob.bOnGround = bStepped || (Velocity.Z < 0 && Moved.Z != Velocity.Z);
    if (Moved.X != Velocity.X && !bStepped) Velocity.X = 0;
    if (Moved.Y != Velocity.Y && !bStepped) Velocity.Y = 0;
    if (Moved.Z != Velocity.Z && !bStepped) Velocity.Z = 0;
    if (bStepped) Velocity.Z = 0;
    // The ground's retention follows where the step began (the scout's kb_probe: 0.546 for the step the hit landed on
    // the ground, then 0.91 in the air).
    const double Retention = bWasGrounded ? GroundRetention : AirRetention;
    Velocity.X *= Retention;
    Velocity.Y *= Retention;
    Velocity.Z = (Velocity.Z - GravityCm) * VerticalDrag;
}

void FoundationMobs::Animate(FFoundationMob& Mob)
{
    Mob.PreviousWalkPosition = Mob.WalkPosition;
    Mob.PreviousWalkSpeed = Mob.WalkSpeed;
    Mob.PreviousHeadYaw = Mob.HeadYaw;
    Mob.PreviousHeadPitch = Mob.HeadPitch;
    const double Blocks = MobHorizontal(Mob.LocalCm - Mob.PreviousLocalCm).Size() / 100;
    const float Wanted = FMath::Min(1.f, float(Blocks) * WalkSpeedScale);
    Mob.WalkSpeed += (Wanted - Mob.WalkSpeed) * WalkSpeedSmoothing;
    Mob.WalkPosition += Mob.WalkSpeed;
    // The head: toward the looked-at player, else back over the body; the yaw within HeadYawLimit of the body.
    const FFoundationMobKind& Kind = GetKind(Mob.Type);
    float WantedYaw = Mob.Yaw, WantedPitch = 0;
    if (Mob.LookTicks > 0)
    {
        const FVector Delta = Mob.LookAtCm - (Mob.LocalCm + FVector(0, 0, Kind.EyeCm));
        WantedYaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
        WantedPitch = FMath::RadiansToDegrees(-FMath::Atan2(Delta.Z, MobHorizontal(Delta).Size()));
    }
    Mob.HeadYaw = MobTurnToward(Mob.HeadYaw, WantedYaw, HeadTurnDegrees);
    const float Relative = FMath::Clamp(FMath::FindDeltaAngleDegrees(Mob.Yaw, Mob.HeadYaw), -HeadYawLimit, HeadYawLimit);
    Mob.HeadYaw = FMath::UnwindDegrees(Mob.Yaw + Relative);
    Mob.HeadPitch = FMath::Clamp(MobTurnToward(Mob.HeadPitch, WantedPitch, HeadTurnDegrees), -HeadPitchLimit, HeadPitchLimit);
}

float FoundationMobs::GetDeathRoll(float DeathTime, float MaxDegrees)
{
    if (DeathTime <= 1) return 0;
    return MaxDegrees * FMath::Min(1.f, FMath::Sqrt((DeathTime - 1) * 1.6f / 20.f));
}

float FoundationMobs::GetLegPitch(int32 Leg, float Position, float Speed)
{
    // Right hind and left front swing together; the other pair half a cycle later.
    const float Offset = Leg == 1 || Leg == 2 ? UE_PI : 0;
    return FMath::Cos(Position * LegPhase + Offset) * LegAmplitude * Speed;
}

float FoundationMobs::GetSoundPitch(float U, float V)
{
    return 1 + .2f * (U - V);
}
