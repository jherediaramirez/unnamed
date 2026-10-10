#include "Clock/VoxelWorldClock.h"

namespace
{
    // The markers of 26.3's day timeline, as /time set names them (scout N 4; the plan's DN1).
    const TPair<const TCHAR*, int32> DayMarkers[] = {
        {TEXT("day"), 1000}, {TEXT("noon"), 6000}, {TEXT("night"), 13000}, {TEXT("midnight"), 18000}};

    const TCHAR* const MoonPhaseNames[FVoxelWorldClock::MoonPhases] = {TEXT("full moon"), TEXT("waning gibbous"),
        TEXT("third quarter"), TEXT("waning crescent"), TEXT("new moon"), TEXT("waxing crescent"),
        TEXT("first quarter"), TEXT("waxing gibbous")};

    // A name with an optional minecraft: namespace, lowercase.
    FString WithoutNamespace(const FString& Name)
    {
        FString Out = Name.ToLower();
        Out.RemoveFromStart(TEXT("minecraft:"));
        return Out;
    }

    // A rate as the replies show it: "2.0", "0.25"; tiny rates in full rather than as 0.0.
    FString DescribeRate(float Rate)
    {
        const FString Short = FString::SanitizeFloat(Rate);
        return float(FCString::Atod(*Short)) == Rate ? Short : FString::Printf(TEXT("%.9g"), Rate);
    }

    // A plain decimal number, as /time's arguments read them: digits with at most one point, no sign or exponent
    // (LexTryParseString alone takes "1x" as 1).
    bool ParseDecimal(const FString& Text, double& OutValue)
    {
        bool bDigit = false;
        for (const TCHAR Char : Text)
            bDigit = bDigit || FChar::IsDigit(Char);
        return bDigit && FCString::IsNumeric(*Text) && Text[0] != '-' && Text[0] != '+' &&
            LexTryParseString(OutValue, *Text) && FMath::IsFinite(OutValue);
    }
}

FVoxelWorldClock::FVoxelWorldClock(double InTickSeconds, int32 InMaxCatchupTicks)
{
    Configure(InTickSeconds, InMaxCatchupTicks);
}

void FVoxelWorldClock::Configure(double InTickSeconds, int32 InMaxCatchupTicks)
{
    TickSeconds = FMath::Max(.001, InTickSeconds);
    MaxCatchupTicks = FMath::Max(1, InMaxCatchupTicks);
}

int32 FVoxelWorldClock::TakeTicks(double Seconds)
{
    if (!(Seconds > 0)) return 0;
    Accumulator += Seconds / TickSeconds;
    if (Accumulator > MaxCatchupTicks)
    {
        DroppedTicks += Accumulator - MaxCatchupTicks;
        Accumulator = MaxCatchupTicks;
    }
    // A hair of slack, so frames that add up to a whole tick give it despite rounding (three of 1/60 s).
    const int32 Count = FMath::Min(MaxCatchupTicks, FMath::FloorToInt32(Accumulator + 1e-9));
    Accumulator = FMath::Max(0., Accumulator - Count);
    return Count;
}

void FVoxelWorldClock::Step(bool bAdvanceTime)
{
    ++GameTick;
    if (!bAdvanceTime || Day.bPaused) return;
    Day.PartialTick += Day.Rate;
    const double Whole = FMath::FloorToDouble(Day.PartialTick);
    Day.PartialTick -= Whole;
    AddTicks(int64(Whole));
}

int32 FVoxelWorldClock::Advance(double Seconds, bool bAdvanceTime)
{
    const int32 Ticks = TakeTicks(Seconds);
    for (int32 Tick = 0; Tick < Ticks; ++Tick)
        Step(bAdvanceTime);
    return Ticks;
}

void FVoxelWorldClock::SetDayClock(const FVoxelDayClock& State)
{
    Day = State;
    Day.TotalTicks = FMath::Clamp<int64>(Day.TotalTicks, 0, MaxTotalTicks);
    Day.PartialTick = FMath::Clamp(Day.PartialTick, 0., .999999);
    SetRate(Day.Rate);
}

const TCHAR* FVoxelWorldClock::GetMoonPhaseName(int32 Phase)
{
    return MoonPhaseNames[((Phase % MoonPhases) + MoonPhases) % MoonPhases];
}

double FVoxelWorldClock::GetSmoothTicks(bool bAdvanceTime) const
{
    const bool bRunning = bAdvanceTime && !Day.bPaused;
    return double(Day.TotalTicks) + Day.PartialTick + (bRunning ? Accumulator * Day.Rate : 0.);
}

void FVoxelWorldClock::SetTotal(int64 Ticks)
{
    Day.TotalTicks = FMath::Clamp<int64>(Ticks, 0, MaxTotalTicks);
    Day.PartialTick = 0;
}

void FVoxelWorldClock::AddTicks(int64 Ticks)
{
    Day.TotalTicks =
        Ticks >= MaxTotalTicks - Day.TotalTicks ? MaxTotalTicks : FMath::Max<int64>(0, Day.TotalTicks + Ticks);
}

void FVoxelWorldClock::SetToMarker(int32 MarkerTicks)
{
    const int32 Ahead = ((MarkerTicks - GetTimeOfDay()) % TicksPerDay + TicksPerDay) % TicksPerDay;
    SetTotal(Day.TotalTicks + Ahead);
}

void FVoxelWorldClock::SetRate(float Rate)
{
    Day.Rate = FMath::IsFinite(Rate) && Rate > 0 ? FMath::Min(Rate, MaxRate) : 1.f;
}

bool FVoxelWorldClock::FindMarker(const FString& Name, int32& OutTicks)
{
    const FString Plain = WithoutNamespace(Name);
    for (const TPair<const TCHAR*, int32>& Marker : DayMarkers)
        if (Plain == Marker.Key)
        {
            OutTicks = Marker.Value;
            return true;
        }
    return false;
}

bool FVoxelWorldClock::ParseTime(const FString& Text, int64& OutTicks)
{
    FString Number = Text.TrimStartAndEnd();
    double Unit = 1;
    if (Number.EndsWith(TEXT("d"), ESearchCase::CaseSensitive))
        Unit = TicksPerDay;
    else if (Number.EndsWith(TEXT("s"), ESearchCase::CaseSensitive))
        Unit = 20;
    if (Unit != 1 || Number.EndsWith(TEXT("t"), ESearchCase::CaseSensitive)) Number.LeftChopInline(1);
    double Value = 0;
    if (!ParseDecimal(Number, Value)) return false;
    const double Ticks = FMath::RoundHalfFromZero(Value * Unit);
    if (Ticks > MAX_int32) return false;
    OutTicks = int64(Ticks);
    return true;
}

bool FVoxelWorldClock::RunCommand(TConstArrayView<FString> Args, FString& OutReply)
{
    const FString Verb = Args.Num() ? Args[0].ToLower() : FString();
    const FString Word = Args.Num() == 2 ? Args[1] : FString();
    int64 Ticks = 0;
    int32 Marker = 0;
    if (Verb == TEXT("set") && Args.Num() == 2 && FindMarker(Word, Marker))
    {
        SetToMarker(Marker);
        OutReply = TEXT("Set minecraft:overworld to time marker minecraft:") + WithoutNamespace(Word);
        return true;
    }
    if ((Verb == TEXT("set") || Verb == TEXT("add")) && Args.Num() == 2)
    {
        if (!ParseTime(Word, Ticks))
        {
            OutReply = FString::Printf(TEXT("\"%s\" is not a time: whole ticks, or a number with d (24,000 ticks), s "
                                            "(20) or t, from 0 to 2147483647 ticks."),
                *Word);
            return false;
        }
        if (Verb == TEXT("set"))
            SetTotal(Ticks);
        else
            AddTicks(Ticks);
        OutReply = FString::Printf(TEXT("Set minecraft:overworld to %lld tick(s)"), Day.TotalTicks);
        return true;
    }
    if (Verb == TEXT("query") && Args.Num() == 2)
    {
        const FString What = WithoutNamespace(Word);
        if (What == TEXT("time"))
            OutReply = FString::Printf(TEXT("Clock minecraft:overworld is at %lld tick(s)"), Day.TotalTicks);
        else if (What == TEXT("day"))
            OutReply = FString::Printf(TEXT("Timeline minecraft:day is at %d tick(s)"), GetTimeOfDay());
        else if (What == TEXT("gametime"))
            OutReply = FString::Printf(TEXT("The game time is %llu tick(s)"), GameTick);
        else
        {
            OutReply = TEXT("voxel.Time query takes time, day or gametime.");
            return false;
        }
        return true;
    }
    if ((Verb == TEXT("pause") || Verb == TEXT("resume")) && Args.Num() == 1)
    {
        SetPaused(Verb == TEXT("pause"));
        OutReply = Day.bPaused ? TEXT("Paused clock minecraft:overworld") : TEXT("Resumed clock minecraft:overworld");
        return true;
    }
    if (Verb == TEXT("rate") && Args.Num() == 2)
    {
        double Rate = 0;
        if (!ParseDecimal(Word, Rate) || Rate <= 0 || Rate > MaxRate)
        {
            OutReply = FString::Printf(TEXT("\"%s\" is not a rate: above 0 and at most 1000."), *Word);
            return false;
        }
        SetRate(float(Rate));
        OutReply = FString::Printf(
            TEXT("Clock minecraft:overworld will now advance at %sx normal rate"), *DescribeRate(Day.Rate));
        return true;
    }
    OutReply = TEXT("voxel.Time set <time>|day|noon|night|midnight, add <time>, query time|day|gametime, pause, "
                    "resume, rate <rate>. A time is whole ticks, or a number with d (24,000 ticks), s (20) or t.");
    return false;
}

FString FVoxelWorldClock::Describe(bool bAdvanceTime) const
{
    const int32 TimeOfDay = GetTimeOfDay();
    FString Out =
        FString::Printf(TEXT("Time: day %lld at %d (%02d:%02d) | moon %s | game tick %llu"), GetDayNumber(), TimeOfDay,
            (TimeOfDay / 1000 + 6) % 24, TimeOfDay % 1000 * 60 / 1000, GetMoonPhaseName(GetMoonPhase()), GameTick);
    if (Day.Rate != 1.f) Out += FString::Printf(TEXT(" | rate %sx"), *DescribeRate(Day.Rate));
    if (Day.bPaused) Out += TEXT(" | paused");
    if (!bAdvanceTime) Out += TEXT(" | advance_time off");
    return Out;
}
