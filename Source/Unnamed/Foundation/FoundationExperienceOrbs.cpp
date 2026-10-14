#include "Foundation/FoundationExperienceOrbs.h"
#include "Foundation/FoundationItemEntities.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarExperienceOrbs(TEXT("foundation.Items.Xp"), 1,
        TEXT(
            "1: a survival break of an ore pays experience orbs (coal 0-2, lapis and quartz 2-5, redstone 1-5, diamond "
            "and emerald 3-7, nether gold 0-1) that fly to a player within 8 blocks and fill the XP bar (survival step "
            "12). 0: no orbs spawn and the bar stays, as before. Orbs already in the world stay until picked up."));

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
    FoundationItemEntities::MoveBox(Orb.LocalCm, Orb.PreviousLocalCm, Orb.VelocityCm, Orb.bOnGround,
        GetBox(OriginCm + Orb.LocalCm), Boxes, GravityCm, PushCm);
}
