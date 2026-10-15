#include "Foundation/FoundationArrows.h"

#include "HAL/IConsoleManager.h"

namespace
{
    int32 GBowOn = 1;
    FAutoConsoleVariableRef CVarBow(TEXT("foundation.Items.Bow"), GBowOn,
        TEXT("1: a bow draws and shoots arrows (survival step 19c); 0: it passes and no arrow spawns."));

    // A triangular value in (-Scale, Scale): Scale x (U - V).
    double Triangle(FRandomStream& Random, double Scale)
    {
        return Scale * (double(Random.GetFraction()) - double(Random.GetFraction()));
    }
}

bool FoundationBow::IsOn()
{
    return GBowOn != 0;
}

float FoundationBow::GetPower(int32 Ticks)
{
    const float F = float(FMath::Max(Ticks, 0)) / float(FullDrawTicks);
    return FMath::Min((F * F + F * 2.f) / 3.f, 1.f);
}

float FoundationBow::GetFovTarget(int32 Ticks)
{
    const float F = FMath::Min(float(FMath::Max(Ticks, 0)) / float(FullDrawTicks), 1.f);
    return 1.f - .15f * F * F;
}

float FoundationBow::GetShootPitch(float Power, float U)
{
    return 1.f / (U * .4f + 1.2f) + Power * .5f;
}

float FoundationArrows::GetHitPitch(float U)
{
    return 1.2f / (U * .2f + .9f);
}

int32 FoundationArrows::GetDamage(float SpeedBlocks, bool bCrit, FRandomStream& Random)
{
    const double Raw = FMath::Clamp(double(SpeedBlocks) * 2.0, 0.0, double(MAX_int32));
    const int32 Damage = int32(FMath::CeilToDouble(Raw));
    if (!bCrit) return Damage;
    // randInt(0, floor(damage / 2) + 1) inclusive, saturating.
    const int64 Bonus = Random.RandRange(0, Damage / 2 + 1);
    return int32(FMath::Min<int64>(int64(Damage) + Bonus, MAX_int32));
}

void FoundationArrows::Integrate(FVector& VelocityCm, bool bInWater)
{
    VelocityCm *= double(bInWater ? WaterDrag : AirDrag);
    VelocityCm.Z -= GravityCm;
}

FVector FoundationArrows::Launch(const FVector& Direction, float SpeedBlocks, float Inaccuracy, FRandomStream& Random)
{
    FVector Unit = Direction.GetSafeNormal();
    const double Scale = SpreadScale * double(Inaccuracy);
    if (Scale > 0)
    {
        Unit.X += Triangle(Random, Scale);
        Unit.Y += Triangle(Random, Scale);
        Unit.Z += Triangle(Random, Scale);
        Unit = Unit.GetSafeNormal();
    }
    return Unit * double(SpeedBlocks) * BlockCm;
}

void FoundationArrows::Face(const FVector& VelocityCm, float& OutYaw, float& OutPitch)
{
    if (VelocityCm.IsNearlyZero()) return;
    OutYaw = float(FMath::RadiansToDegrees(FMath::Atan2(VelocityCm.Y, VelocityCm.X)));
    OutPitch = float(FMath::RadiansToDegrees(FMath::Atan2(VelocityCm.Z, FVector2D(VelocityCm).Size())));
}

bool FoundationArrows::Sweep(
    const FVector& From, const FVector& To, TConstArrayView<FBox> Boxes, double& OutTime, int32* OutIndex)
{
    const FVector Delta = To - From;
    bool bHit = false;
    for (int32 Index = 0; Index < Boxes.Num(); ++Index)
    {
        const FBox& Box = Boxes[Index];
        double Enter = 0, Exit = 1;
        bool bMiss = false;
        for (int32 Axis = 0; Axis < 3 && !bMiss; ++Axis)
        {
            if (FMath::IsNearlyZero(Delta[Axis]))
            {
                bMiss = From[Axis] < Box.Min[Axis] || From[Axis] > Box.Max[Axis];
                continue;
            }
            double A = (Box.Min[Axis] - From[Axis]) / Delta[Axis], B = (Box.Max[Axis] - From[Axis]) / Delta[Axis];
            if (A > B) Swap(A, B);
            Enter = FMath::Max(Enter, A);
            Exit = FMath::Min(Exit, B);
            bMiss = Enter > Exit;
        }
        if (bMiss || (bHit && Enter >= OutTime)) continue;
        bHit = true;
        OutTime = Enter;
        if (OutIndex) *OutIndex = Index;
    }
    return bHit;
}
