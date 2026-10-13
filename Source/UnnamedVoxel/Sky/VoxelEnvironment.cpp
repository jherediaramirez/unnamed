#include "Sky/VoxelEnvironment.h"

#include "Clock/VoxelWorldClock.h"
#include "Sky/VoxelTimeline.h"

float VoxelEnvironment::SunAngle(double Ticks)
{
    // The day's share since noon, then the orbit's ease: a little faster through the night, slower through the day.
    // The day is taken first so a large total keeps its fraction.
    const double Day = FVoxelWorldClock::TicksPerDay;
    double InDay = FMath::Fmod(Ticks, Day);
    if (InDay < 0) InDay += Day;
    const double Share = FMath::Fmod(InDay + Day * .75, Day) / Day;
    const double Eased = (Share * 2 + (.5 - FMath::Cos(Share * UE_DOUBLE_PI) / 2)) / 3;
    // A share just under 1 may round to 360 itself.
    const float Degrees = float(Eased * 360);
    return Degrees >= 360.f ? 0.f : Degrees;
}

FRotator VoxelEnvironment::LightRotation(const FVector3f& TowardsBody)
{
    return (-FVector(TowardsBody)).Rotation();
}

float VoxelEnvironment::DaylightAt(float SunElevation)
{
    return float(
        FMath::SmoothStep(-double(DaylightFullElevation), double(DaylightFullElevation), double(SunElevation)));
}

float VoxelEnvironment::NightAt(float SunElevation)
{
    return 1 - DaylightAt(SunElevation);
}

float VoxelEnvironment::MoonlightAt(float MoonElevation)
{
    return float(
        FMath::SmoothStep(double(DaylightFullElevation), 2 * double(DaylightFullElevation), double(MoonElevation)));
}

float VoxelEnvironment::MoonIlluminationAt(int32 MoonPhase)
{
    const int32 Phase =
        ((MoonPhase % FVoxelWorldClock::MoonPhases) + FVoxelWorldClock::MoonPhases) % FVoxelWorldClock::MoonPhases;
    return float((1 + FMath::Cos(Phase * UE_DOUBLE_TWO_PI / FVoxelWorldClock::MoonPhases)) / 2);
}

FVoxelEnvironment VoxelEnvironment::Evaluate(const FVoxelWorldClock& Clock, bool bAdvanceTime)
{
    FVoxelEnvironment Out;
    const FVoxelDayClock& Day = Clock.GetDayClock();
    Out.TotalTicks = Day.TotalTicks;
    Out.DayNumber = Clock.GetDayNumber();
    Out.TimeOfDay = Clock.GetTimeOfDay();
    Out.Rate = Day.Rate;
    Out.bPaused = Day.bPaused;
    Out.bAdvancing = bAdvanceTime;
    Out.SmoothTicks = Clock.GetSmoothTicks(bAdvanceTime);
    Out.MoonPhase = Clock.GetMoonPhase();
    Out.SunAngle = SunAngle(Out.SmoothTicks);
    Out.MoonAngle = Out.SunAngle + 180.f;
    Out.SunDirection = VoxelSky::DirectionFromAngle(Out.SunAngle);
    Out.MoonDirection = VoxelSky::DirectionFromAngle(Out.MoonAngle);
    Out.SunElevation = float(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(double(Out.SunDirection.Z), -1., 1.))));
    Out.Daylight = DaylightAt(Out.SunElevation);
    Out.MoonElevation = -Out.SunElevation;
    Out.Moonlight = MoonlightAt(Out.MoonElevation);
    Out.MoonIllumination = MoonIlluminationAt(Out.MoonPhase);
    Out.Night = NightAt(Out.SunElevation);
    return Out;
}

FString VoxelEnvironment::Describe(const FVoxelEnvironment& Environment)
{
    return FString::Printf(TEXT("Day %lld, time %d (total %lld, smooth %.3f), rate %g%s%s, moon phase %d (%s)\n"
                                "Sun angle %.3f, elevation %.3f, direction (%.4f, %.4f, %.4f), daylight %.3f; moon "
                                "angle %.3f, elevation %.3f, moonlight %.3f, lit %.3f; night %.3f"),
        Environment.DayNumber, Environment.TimeOfDay, Environment.TotalTicks, Environment.SmoothTicks, Environment.Rate,
        Environment.bPaused ? TEXT(", paused") : TEXT(""),
        Environment.bAdvancing ? TEXT("") : TEXT(", advance_time off"), Environment.MoonPhase,
        FVoxelWorldClock::GetMoonPhaseName(Environment.MoonPhase), Environment.SunAngle, Environment.SunElevation,
        Environment.SunDirection.X, Environment.SunDirection.Y, Environment.SunDirection.Z, Environment.Daylight,
        Environment.MoonAngle, Environment.MoonElevation, Environment.Moonlight, Environment.MoonIllumination,
        Environment.Night);
}
