#include "Foundation/FoundationExperienceOrbs.h"
#include "Foundation/FoundationItemEntities.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarExperienceOrbs(TEXT("foundation.Items.Xp"), 1,
        TEXT(
            "1: a survival break of an ore pays experience orbs (coal 0-2, lapis and quartz 2-5, redstone 1-5, diamond "
            "and emerald 3-7, nether gold 0-1) that fly to a player within 8 blocks and fill the XP bar (survival step "
            "12). 0: no orbs spawn and the bar stays, as before. Orbs already in the world stay: they still move (out "
            "of a block placed over them too), fly to a player and pay their points (as drops stay at "
            "foundation.Items.Entities 0)."));

    struct FBlockExperience
    {
        const TCHAR* Block;
        int32 Min, Max;
    };
    // [xp.ore_drops, xp.ore] the deepslate ores pay as their stone ones.
    constexpr FBlockExperience BlockExperience[] = {
        {TEXT("coal_ore"), 0, 2},
        {TEXT("deepslate_coal_ore"), 0, 2},
        {TEXT("diamond_ore"), 3, 7},
        {TEXT("deepslate_diamond_ore"), 3, 7},
        {TEXT("emerald_ore"), 3, 7},
        {TEXT("deepslate_emerald_ore"), 3, 7},
        {TEXT("lapis_ore"), 2, 5},
        {TEXT("deepslate_lapis_ore"), 2, 5},
        {TEXT("nether_quartz_ore"), 2, 5},
        {TEXT("redstone_ore"), 1, 5},
        {TEXT("deepslate_redstone_ore"), 1, 5},
        {TEXT("nether_gold_ore"), 0, 1},
    };
}

bool FoundationExperienceOrbs::IsOn()
{
    return CVarExperienceOrbs.GetValueOnAnyThread() != 0;
}

bool FoundationExperienceOrbs::CanMerge(const FFoundationExperienceOrb& Seeker, const FFoundationExperienceOrb& Other)
{
    if ((Seeker.Id - Other.Id) % MergeIdPeriod == 0) return true;
    // The roll is a pure function of the pair, so a replay merges the same orbs.
    return Seeker.bNew &&
        FRandomStream(int32(HashCombine(GetTypeHash(Seeker.Id), GetTypeHash(Other.Id)))).FRand() < NewMergeChance;
}

float FoundationExperienceOrbs::GetLevelUpVolume(int32 LastLevel, int32 Level, int64 Step, int64 LastSoundStep)
{
    if (LastLevel < 0 || Level <= LastLevel || Level / 5 == LastLevel / 5) return 0;
    // A step before the last sound's (a reset authority) counts as never.
    if (LastSoundStep >= 0 && Step >= LastSoundStep && Step - LastSoundStep <= LevelUpGapTicks) return 0;
    // Level by level, as XP is added (provisional reading of survival.r5.rules; Codex review 14, finding 3): the first
    // multiple of 5 reached plays, and the cooldown silences the rest of the same gain.
    return LevelUpVolume * FMath::Min(float((LastLevel / 5 + 1) * 5) / LevelUpFullLevel, 1.f);
}

int64 FoundationExperienceOrbs::GetLevelUpStep(int64 FrameStep, int32 Level, int32 GainLevel, int64 GainStep)
{
    return GainStep >= 0 && GainLevel == Level ? GainStep : FrameStep;
}

float FoundationExperienceOrbs::GetPickupPitch(float U, float V)
{
    return .9f + .35f * (U - V);
}

int32 FoundationExperienceOrbs::GetIcon(int32 Value)
{
    int32 Icon = 0;
    for (int32 Index = 1; Index < UE_ARRAY_COUNT(IconThresholds); ++Index)
        Icon = Value >= IconThresholds[Index] ? Index : Icon;
    return Icon;
}

int32 FoundationExperienceOrbs::GetSplitValue(int32 Total)
{
    int32 Value = 1;
    for (const int32 Threshold : IconThresholds)
        Value = Total >= Threshold ? Threshold : Value;
    return Value;
}

FColor FoundationExperienceOrbs::GetColor(double AgeTicks)
{
    const double Phase = AgeTicks / 2;
    const auto Channel = [](double Value) { return uint8(FMath::Clamp(FMath::FloorToInt(Value), 0, 255)); };
    return FColor(Channel((FMath::Sin(Phase) + 1) * .5 * 255), 255,
        Channel((FMath::Sin(Phase + 4 * UE_DOUBLE_PI / 3) + 1) * .1 * 255), 128);
}

bool FoundationExperienceOrbs::GetBlockExperience(FName Block, int32& OutMin, int32& OutMax)
{
    for (const FBlockExperience& Entry : BlockExperience)
        if (Block == FName(Entry.Block))
        {
            OutMin = Entry.Min;
            OutMax = Entry.Max;
            return true;
        }
    return false;
}

FBox FoundationExperienceOrbs::GetBox(const FVector& WorldCm)
{
    return FBox(WorldCm - FVector(BoxCm * .5, BoxCm * .5, 0), WorldCm + FVector(BoxCm * .5, BoxCm * .5, BoxCm));
}

FBox FoundationExperienceOrbs::GetMergeBox(const FVector& WorldCm)
{
    return GetBox(WorldCm).ExpandBy(MergeGrowCm);
}

FVector FoundationExperienceOrbs::GetAttraction(const FVector& OrbWorldCm, const FVector& TargetWorldCm)
{
    const FVector Delta = TargetWorldCm - OrbWorldCm;
    const double Distance = Delta.Size();
    if (Distance >= FollowCm || Distance <= 0) return FVector::ZeroVector;
    const double Pull = 1 - Distance / FollowCm;
    return Delta / Distance * (Pull * Pull * AttractionCm);
}

FBox FoundationExperienceOrbs::GetStepRegion(
    const FFoundationExperienceOrb& Orb, const FVector& OriginCm, const FVector& PushCm)
{
    // This step's velocity after gravity and the push, plus a centimetre for contact.
    const FVector Reach = (Orb.VelocityCm - FVector(0, 0, GravityCm) + PushCm).GetAbs() + FVector(1);
    return GetBox(OriginCm + Orb.LocalCm).ExpandBy(Reach);
}

void FoundationExperienceOrbs::Move(
    FFoundationExperienceOrb& Orb, const FVector& OriginCm, const TArray<FBox>& Boxes, const FVector& PushCm)
{
    // The speed it would land at: this step's vertical velocity after gravity and the push (MoveBox zeroes it). Only
    // an orb in the air bounces (one resting under a downward pull stays down).
    const double Landing = -(Orb.VelocityCm.Z - GravityCm + PushCm.Z);
    const bool bWasInAir = !Orb.bOnGround;
    // Inside a block an orb skips gravity (R23, survival2.r23.xp_rules); the push-out's draw is a pure function of the
    // orb and its age.
    FoundationItemEntities::MoveBox(Orb.LocalCm, Orb.PreviousLocalCm, Orb.VelocityCm, Orb.bOnGround,
        GetBox(OriginCm + Orb.LocalCm), Boxes, GravityCm, PushCm,
        FRandomStream(int32(HashCombine(GetTypeHash(Orb.Id), GetTypeHash(Orb.Age)))).GetFraction(), false);
    if (bWasInAir && Orb.bOnGround && Landing > BounceMinCm) Orb.VelocityCm.Z = Landing * BounceFactor;
}
