#include "Sky/VoxelTimeline.h"

#include "Algo/StableSort.h"
#include "Clock/VoxelWorldClock.h"

namespace
{
    // 26.3's moon phase names, in FVoxelWorldClock's order (0 full moon).
    const TCHAR* const MoonPhaseIds[FVoxelWorldClock::MoonPhases] = {TEXT("full_moon"), TEXT("waning_gibbous"),
        TEXT("third_quarter"), TEXT("waning_crescent"), TEXT("new_moon"), TEXT("waxing_crescent"),
        TEXT("first_quarter"), TEXT("waxing_gibbous")};
}

void FVoxelTimelineTrack::SortKeys()
{
    Algo::StableSortBy(Keys, &FVoxelTimelineKey::Ticks);
}

FVector4f FVoxelTimelineTrack::Sample(double Ticks, int32 PeriodTicks) const
{
    if (Keys.IsEmpty()) return FVector4f(0.f);
    const bool bRepeats = PeriodTicks > 0;
    // A non-finite tick reads as tick 0.
    const double Finite = FMath::IsFinite(Ticks) ? Ticks : 0.;
    double Time = bRepeats ? FMath::Fmod(Finite, double(PeriodTicks)) : Finite;
    if (Time < 0 && bRepeats) Time += PeriodTicks;
    // The last key at or before the tick (the later of equal keys: the wrap pair at the day's 6000 is 360 then 0),
    // else the last key one period earlier; the first key after it, else the first key one period later. A timeline
    // without a period holds its first value before its first key and its last value after its last.
    int32 Next = 0;
    while (Next < Keys.Num() && Keys[Next].Ticks <= Time)
        ++Next;
    if (!bRepeats && (Next == 0 || Next == Keys.Num())) return Next == 0 ? Keys[0].Value : Keys.Last().Value;
    const bool bWrapsBack = Next == 0;
    const bool bWrapsOn = Next == Keys.Num();
    const FVoxelTimelineKey& Previous = bWrapsBack ? Keys.Last() : Keys[Next - 1];
    const FVoxelTimelineKey& Following = bWrapsOn ? Keys[0] : Keys[Next];
    const double PreviousTicks = Previous.Ticks - (bWrapsBack ? PeriodTicks : 0);
    const double FollowingTicks = Following.Ticks + (bWrapsOn ? PeriodTicks : 0);
    if (Ease == EVoxelTimelineEase::Constant || Type == EVoxelTimelineValue::Bool ||
        Type == EVoxelTimelineValue::Name || FollowingTicks <= PreviousTicks)
        return Previous.Value;
    double Progress = (Time - PreviousTicks) / (FollowingTicks - PreviousTicks);
    if (Ease == EVoxelTimelineEase::CubicBezier) Progress = VoxelSky::CubicBezier(Bezier, Progress);
    // The attributes are single precision: lerp in float.
    return Previous.Value + (Following.Value - Previous.Value) * float(Progress);
}

const FVoxelTimelineTrack* FVoxelTimeline::FindTrack(const FString& Attribute) const
{
    return Tracks.FindByPredicate([&Attribute](const FVoxelTimelineTrack& Track)
        { return Track.Attribute.Equals(Attribute, ESearchCase::CaseSensitive); });
}

const FVoxelTimelineMarker* FVoxelTimeline::FindMarker(const FString& Name) const
{
    return Markers.FindByPredicate(
        [&Name](const FVoxelTimelineMarker& Marker) { return Marker.Name.Equals(Name, ESearchCase::CaseSensitive); });
}

bool FVoxelTimeline::Sample(const FString& Attribute, double Ticks, FVector4f& OutValue) const
{
    const FVoxelTimelineTrack* Track = FindTrack(Attribute);
    if (!Track) return false;
    OutValue = Track->Sample(Ticks, PeriodTicks);
    return true;
}

FVector4f FVoxelTimeline::Apply(const FString& Attribute, double Ticks, const FVector4f& Base) const
{
    const FVoxelTimelineTrack* Track = FindTrack(Attribute);
    return Track ? VoxelSky::ApplyModifier(Track->Modifier, Base, Track->Sample(Ticks, PeriodTicks)) : Base;
}

bool FVoxelTimeline::Validate(FString& OutError) const
{
    if (PeriodTicks < 0)
    {
        OutError = FString::Printf(TEXT("%s: period_ticks %d is negative"), *Id, PeriodTicks);
        return false;
    }
    for (const FVoxelTimelineTrack& Track : Tracks)
    {
        if (Track.Keys.IsEmpty())
        {
            OutError = FString::Printf(TEXT("%s: %s has no keyframes"), *Id, *Track.Attribute);
            return false;
        }
        for (int32 Index = 0; Index < Track.Keys.Num(); ++Index)
        {
            const FVoxelTimelineKey& Key = Track.Keys[Index];
            if (Key.Ticks < 0 || (PeriodTicks > 0 && Key.Ticks > PeriodTicks) ||
                (Index > 0 && Key.Ticks < Track.Keys[Index - 1].Ticks))
            {
                OutError =
                    FString::Printf(TEXT("%s: %s's keyframe %d (tick %d) is negative, past the period or unsorted"),
                        *Id, *Track.Attribute, Index, Key.Ticks);
                return false;
            }
            if (Track.Type == EVoxelTimelineValue::Name && !Track.Names.IsValidIndex(int32(Key.Value.X)))
            {
                OutError = FString::Printf(TEXT("%s: %s's keyframe %d names no value"), *Id, *Track.Attribute, Index);
                return false;
            }
        }
    }
    return true;
}

FVector4f VoxelSky::ApplyModifier(EVoxelTimelineModifier Modifier, const FVector4f& Base, const FVector4f& Value)
{
    switch (Modifier)
    {
    case EVoxelTimelineModifier::Multiply:
        return Base * Value;
    case EVoxelTimelineModifier::Maximum:
        return FVector4f(FMath::Max(Base.X, Value.X), FMath::Max(Base.Y, Value.Y), FMath::Max(Base.Z, Value.Z),
            FMath::Max(Base.W, Value.W));
    case EVoxelTimelineModifier::Or:
        return FVector4f(Base.X != 0.f || Value.X != 0.f ? 1.f : 0.f, 0.f, 0.f, 0.f);
    case EVoxelTimelineModifier::And:
        return FVector4f(Base.X != 0.f && Value.X != 0.f ? 1.f : 0.f, 0.f, 0.f, 0.f);
    case EVoxelTimelineModifier::Override:
    default:
        return Value;
    }
}

double VoxelSky::CubicBezier(const FVector4f& Points, double U)
{
    const auto Coordinate = [](double S, double A, double B)
    { return 3. * (1. - S) * (1. - S) * S * A + 3. * (1. - S) * S * S * B + S * S * S; };
    // x(s) rises monotonically for control x in 0..1: halve the interval until s is exact to double precision.
    double Low = 0., High = 1.;
    for (int32 Step = 0; Step < 60; ++Step)
    {
        const double Mid = (Low + High) * .5;
        (Coordinate(Mid, Points.X, Points.Z) < U ? Low : High) = Mid;
    }
    return Coordinate((Low + High) * .5, Points.Y, Points.W);
}

bool VoxelSky::ParseColor(const FString& Text, FVector4f& OutColor)
{
    if (!Text.StartsWith(TEXT("#")) || (Text.Len() != 7 && Text.Len() != 9)) return false;
    for (int32 Index = 1; Index < Text.Len(); ++Index)
        if (!FChar::IsHexDigit(Text[Index])) return false;
    const uint32 Packed = FParse::HexNumber(*Text.Mid(1));
    const bool bAlpha = Text.Len() == 9;
    OutColor = FVector4f(float((Packed >> 16) & 255) / 255.f, float((Packed >> 8) & 255) / 255.f,
        float(Packed & 255) / 255.f, bAlpha ? float(Packed >> 24) / 255.f : 1.f);
    return true;
}

FString VoxelSky::ColorToHex(const FVector4f& Color, bool bWithAlpha)
{
    const auto Byte = [](float Channel) { return FMath::Clamp(FMath::RoundToInt32(Channel * 255.f), 0, 255); };
    return bWithAlpha
        ? FString::Printf(TEXT("#%02x%02x%02x%02x"), Byte(Color.W), Byte(Color.X), Byte(Color.Y), Byte(Color.Z))
        : FString::Printf(TEXT("#%02x%02x%02x"), Byte(Color.X), Byte(Color.Y), Byte(Color.Z));
}

int32 VoxelSky::OpenSkyLight(float Level)
{
    return FMath::Clamp(FMath::CeilToInt32(Level), 0, 15);
}

FVector3f VoxelSky::DirectionFromAngle(float Degrees)
{
    const double Radians = FMath::DegreesToRadians(double(Degrees));
    return FVector3f(float(-FMath::Sin(Radians)), 0.f, float(FMath::Cos(Radians)));
}

VoxelSky::FAngles VoxelSky::SampleAngles(const FVoxelTimeline& Day, double Ticks)
{
    // Override tracks over a default of 0 (minecraft.wiki, Environment attribute).
    FAngles Out;
    Out.Sun = Day.Apply(SunAngle, Ticks, FVector4f(0.f)).X;
    Out.Moon = Day.Apply(MoonAngle, Ticks, FVector4f(0.f)).X;
    Out.Stars = Day.Apply(StarAngle, Ticks, FVector4f(0.f)).X;
    Out.SunDirection = DirectionFromAngle(Out.Sun);
    Out.MoonDirection = DirectionFromAngle(Out.Moon);
    return Out;
}

int32 VoxelSky::SampleMoonPhase(const FVoxelTimeline& Moon, double TotalTicks)
{
    const FVoxelTimelineTrack* Track = Moon.FindTrack(MoonPhase);
    if (!Track || Track->Type != EVoxelTimelineValue::Name) return INDEX_NONE;
    const int32 Name = int32(Track->Sample(TotalTicks, Moon.PeriodTicks).X);
    for (int32 Phase = 0; Phase < FVoxelWorldClock::MoonPhases; ++Phase)
        if (Track->Names.IsValidIndex(Name) &&
            Track->Names[Name].Equals(MoonPhaseIds[Phase], ESearchCase::CaseSensitive))
            return Phase;
    return INDEX_NONE;
}
