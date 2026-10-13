#if WITH_DEV_AUTOMATION_TESTS
#include "Clock/VoxelWorldClock.h"
#include "Misc/AutomationTest.h"
#include "Sky/VoxelEnvironment.h"
#include "Sky/VoxelTimeline.h"
#include "Voxel/VoxelAtmosphere.h"
#include "Voxel/VoxelLook.h"
#include "Voxel/VoxelSkyReference.h"

// The environment snapshot (atmosphere step 1): its orbit against the reference's minecraft:day timeline, and its
// reading of the world clock at the day's wrap, while paused and at other rates.
namespace
{
    // The difference of two angles in degrees, -180 to 180.
    double EnvironmentAngleDelta(double A, double B)
    {
        return FMath::UnwindDegrees(A - B);
    }

    FVoxelWorldClock EnvironmentClockAt(int64 Total)
    {
        FVoxelWorldClock Clock;
        Clock.SetTotal(Total);
        return Clock;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelEnvironmentOrbitTest, "Unnamed.Voxel.Atmosphere.Orbit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelEnvironmentOrbitTest::RunTest(const FString& Parameters)
{
    FVoxelTimeline Day;
    FString Error;
    if (!TestTrue(TEXT("minecraft:day reads"),
            VoxelSkyReference::ReadTimeline(VoxelSkyReference::GetReferenceRoot(), TEXT("minecraft:day"), Day, Error)))
        return false;
    // The formula is the timeline's sun, at every 25 ticks and between ticks, within 0.1 degrees: 26.3's keyframed
    // orbit differs from the formula by up to 0.056 (around 500 and 11500), a hundredth of the sun's width.
    double Worst = 0;
    for (double Ticks = 0; Ticks < FVoxelWorldClock::TicksPerDay; Ticks += 12.5)
    {
        const VoxelSky::FAngles Angles = VoxelSky::SampleAngles(Day, Ticks);
        const double Delta = FMath::Abs(EnvironmentAngleDelta(VoxelEnvironment::SunAngle(Ticks), Angles.Sun));
        Worst = FMath::Max(Worst, Delta);
        if (Delta > .1)
            AddError(FString::Printf(
                TEXT("sun at %.1f: %.4f, the timeline's %.4f"), Ticks, VoxelEnvironment::SunAngle(Ticks), Angles.Sun));
    }
    AddInfo(FString::Printf(TEXT("largest difference from the timeline: %.5f degrees"), Worst));
    TestEqual(TEXT("noon is 0"), VoxelEnvironment::SunAngle(6000.), 0.f);
    TestNearlyEqual(TEXT("midnight is 180"), double(VoxelEnvironment::SunAngle(18000.)), 180., 1e-4);
    // Far into a world the day's fraction survives: a billion days on, the same sky.
    const double Far = 1e9 * FVoxelWorldClock::TicksPerDay;
    TestNearlyEqual(TEXT("a billion days on, 3000 is the same"),
        EnvironmentAngleDelta(VoxelEnvironment::SunAngle(Far + 3000.), VoxelEnvironment::SunAngle(3000.)), 0., 1e-3);
    // Wraps: always 0 up to 360, negative times too.
    for (const double Ticks : {-30000., -18000.5, -1., 5999.9999999, 29999.9999999})
    {
        const float Angle = VoxelEnvironment::SunAngle(Ticks);
        TestTrue(FString::Printf(TEXT("%.7f in [0, 360): %f"), Ticks, Angle), Angle >= 0.f && Angle < 360.f);
    }
    TestNearlyEqual(TEXT("a day earlier is the same"),
        EnvironmentAngleDelta(VoxelEnvironment::SunAngle(-21000.), VoxelEnvironment::SunAngle(3000.)), 0., 1e-3);
    // The light shines from the body: down at noon, westwards (-X) with the sun in the east at dawn, up at midnight.
    const auto Shines = [](double Ticks)
    {
        return FVector3f(
            VoxelEnvironment::LightRotation(VoxelSky::DirectionFromAngle(VoxelEnvironment::SunAngle(Ticks))).Vector());
    };
    TestTrue(TEXT("noon light points down"), Shines(6000.).Equals(FVector3f(0, 0, -1), 1e-5f));
    TestTrue(TEXT("dawn light points west and down"), Shines(0.).X < -.9f && Shines(0.).Z < 0.f);
    TestTrue(TEXT("sunset light points east"), Shines(12000.).X > .9f);
    for (double Ticks = 0; Ticks < FVoxelWorldClock::TicksPerDay; Ticks += 1000)
    {
        const FVector3f Towards = VoxelSky::DirectionFromAngle(VoxelEnvironment::SunAngle(Ticks));
        TestTrue(FString::Printf(TEXT("light opposite the sun at %.0f"), Ticks), Shines(Ticks).Equals(-Towards, 1e-5f));
    }
    // The snapshot's directions and elevation follow the same angle.
    const FVoxelEnvironment Dawn = VoxelEnvironment::Evaluate(EnvironmentClockAt(0), true);
    const VoxelSky::FAngles DawnAngles = VoxelSky::SampleAngles(Day, 0.);
    TestTrue(TEXT("sun direction at 0 as the timeline's"), Dawn.SunDirection.Equals(DawnAngles.SunDirection, 2e-3f));
    TestTrue(TEXT("moon opposite"), Dawn.MoonDirection.Equals(-Dawn.SunDirection, 1e-5f));
    TestNearlyEqual(TEXT("sun elevation at 0"), double(Dawn.SunElevation),
        FMath::RadiansToDegrees(FMath::Asin(double(DawnAngles.SunDirection.Z))), .1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelEnvironmentDaylightTest, "Unnamed.Voxel.Atmosphere.Daylight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelEnvironmentDaylightTest::RunTest(const FString& Parameters)
{
    const FVoxelEnvironment Noon = VoxelEnvironment::Evaluate(EnvironmentClockAt(6000), true);
    TestNearlyEqual(TEXT("noon: sun straight up"), double(Noon.SunElevation), 90., 1e-3);
    TestEqual(TEXT("noon: full daylight"), Noon.Daylight, 1.f);
    const FVoxelEnvironment Midnight = VoxelEnvironment::Evaluate(EnvironmentClockAt(18000), true);
    TestNearlyEqual(TEXT("midnight: sun straight down"), double(Midnight.SunElevation), -90., 1e-3);
    TestEqual(TEXT("midnight: no sun light"), Midnight.Daylight, 0.f);
    TestNearlyEqual(TEXT("on the horizon: half"), double(VoxelEnvironment::DaylightAt(0.f)), .5, 1e-6);
    TestEqual(TEXT("as far below: none"), VoxelEnvironment::DaylightAt(-VoxelEnvironment::DaylightFullElevation), 0.f);
    TestEqual(TEXT("moon: none until the sun is out"),
        VoxelEnvironment::MoonlightAt(VoxelEnvironment::DaylightFullElevation), 0.f);
    TestEqual(TEXT("moon: all at twice that"),
        VoxelEnvironment::MoonlightAt(2 * VoxelEnvironment::DaylightFullElevation), 1.f);
    TestEqual(
        TEXT("at the full elevation: all"), VoxelEnvironment::DaylightAt(VoxelEnvironment::DaylightFullElevation), 1.f);
    // Through the evening the sun sinks and its light only falls, reaching none before midnight.
    float LastElevation = 91.f, LastDaylight = 1.f;
    bool bReachedNight = false;
    for (int64 Ticks = 6000; Ticks <= 18000; Ticks += 100)
    {
        const FVoxelEnvironment At = VoxelEnvironment::Evaluate(EnvironmentClockAt(Ticks), true);
        TestTrue(FString::Printf(TEXT("sun sinks at %lld"), Ticks), At.SunElevation < LastElevation);
        TestTrue(FString::Printf(TEXT("light falls at %lld"), Ticks), At.Daylight <= LastDaylight);
        LastElevation = At.SunElevation;
        LastDaylight = At.Daylight;
        bReachedNight |= At.Daylight == 0.f && Ticks < 18000;
    }
    TestTrue(TEXT("dark before midnight"), bReachedNight);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelEnvironmentClockTest, "Unnamed.Voxel.Atmosphere.Clock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelEnvironmentClockTest::RunTest(const FString& Parameters)
{
    // The day's wrap: the day count and the moon phase step at 24,000, the sun moves on smoothly.
    {
        FVoxelWorldClock Clock = EnvironmentClockAt(23999);
        const FVoxelEnvironment Before = VoxelEnvironment::Evaluate(Clock, true);
        Clock.Advance(.05, true);
        const FVoxelEnvironment After = VoxelEnvironment::Evaluate(Clock, true);
        TestEqual(TEXT("wrap: time 23999"), Before.TimeOfDay, 23999);
        TestEqual(TEXT("wrap: then 0"), After.TimeOfDay, 0);
        TestEqual(TEXT("wrap: day 0 then 1"), After.DayNumber, Before.DayNumber + 1);
        TestEqual(
            TEXT("wrap: moon phase 0 then 1"), After.MoonPhase, (Before.MoonPhase + 1) % FVoxelWorldClock::MoonPhases);
        TestTrue(TEXT("wrap: the sun moves a tick's worth"),
            FMath::Abs(EnvironmentAngleDelta(After.SunAngle, Before.SunAngle)) < .05);
    }
    // Reading never moves the clock.
    {
        FVoxelWorldClock Clock = EnvironmentClockAt(1000);
        Clock.Advance(.07, true);
        const int64 Total = Clock.GetDayClock().TotalTicks;
        const uint64 GameTick = Clock.GetGameTick();
        const FVoxelEnvironment First = VoxelEnvironment::Evaluate(Clock, true);
        const FVoxelEnvironment Second = VoxelEnvironment::Evaluate(Clock, true);
        TestEqual(TEXT("read: total unchanged"), Clock.GetDayClock().TotalTicks, Total);
        TestEqual(TEXT("read: game tick unchanged"), Clock.GetGameTick(), GameTick);
        TestEqual(TEXT("read: twice the same"), First.SmoothTicks, Second.SmoothTicks);
        TestTrue(TEXT("read: the frame's share of the next tick"), First.SmoothTicks > double(Total));
        TestEqual(TEXT("read: advance_time off holds the tick"), VoxelEnvironment::Evaluate(Clock, false).SmoothTicks,
            double(Total));
        TestFalse(TEXT("read: advance_time off shows"), VoxelEnvironment::Evaluate(Clock, false).bAdvancing);
    }
    // Paused, the game ticks run but the sky stands.
    {
        FVoxelWorldClock Clock = EnvironmentClockAt(12000);
        Clock.SetPaused(true);
        const FVoxelEnvironment Before = VoxelEnvironment::Evaluate(Clock, true);
        for (int32 Frame = 0; Frame < 30; ++Frame)
            Clock.Advance(1. / 60, true);
        const FVoxelEnvironment After = VoxelEnvironment::Evaluate(Clock, true);
        TestTrue(TEXT("paused: shows"), After.bPaused);
        TestEqual(TEXT("paused: same sun"), After.SunAngle, Before.SunAngle);
        TestEqual(TEXT("paused: same time"), After.TimeOfDay, 12000);
    }
    // At rate 2 the sky runs twice as far in the same frames.
    {
        FVoxelWorldClock One = EnvironmentClockAt(12000), Two = EnvironmentClockAt(12000);
        Two.SetRate(2.f);
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            One.Advance(1. / 60, true);
            Two.Advance(1. / 60, true);
        }
        const FVoxelEnvironment AtOne = VoxelEnvironment::Evaluate(One, true);
        const FVoxelEnvironment AtTwo = VoxelEnvironment::Evaluate(Two, true);
        TestEqual(TEXT("rate 1: 20 ticks"), AtOne.TimeOfDay, 12020);
        TestEqual(TEXT("rate 2: 40 ticks"), AtTwo.TimeOfDay, 12040);
        TestEqual(TEXT("rate 2: shows"), AtTwo.Rate, 2.f);
        TestTrue(TEXT("rate 2: the sun twice as far on"),
            EnvironmentAngleDelta(AtTwo.SunAngle, VoxelEnvironment::SunAngle(12000.)) >
                1.9 * EnvironmentAngleDelta(AtOne.SunAngle, VoxelEnvironment::SunAngle(12000.)));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelEnvironmentNightTest, "Unnamed.Voxel.Atmosphere.Night",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelEnvironmentNightTest::RunTest(const FString& Parameters)
{
    // The night eases across twilight: none with the sun 6 degrees up, half at the horizon, full 6 below.
    TestEqual(TEXT("day: no night"), VoxelEnvironment::NightAt(6.f), 0.f);
    TestNearlyEqual(TEXT("horizon: half"), double(VoxelEnvironment::NightAt(0.f)), .5, 1e-6);
    TestEqual(TEXT("6 below: full night"), VoxelEnvironment::NightAt(-6.f), 1.f);
    // The moon's lit part by phase: full, quarter, new, and wrapping.
    TestNearlyEqual(TEXT("full moon lit"), double(VoxelEnvironment::MoonIlluminationAt(0)), 1., 1e-6);
    TestNearlyEqual(TEXT("third quarter half lit"), double(VoxelEnvironment::MoonIlluminationAt(2)), .5, 1e-6);
    TestNearlyEqual(TEXT("new moon dark"), double(VoxelEnvironment::MoonIlluminationAt(4)), 0., 1e-6);
    TestNearlyEqual(TEXT("phase 8 is full again"), double(VoxelEnvironment::MoonIlluminationAt(8)), 1., 1e-6);
    // Midnight of day 0 (full moon) and of day 4 (new moon): the moon straight up, full night.
    const FVoxelEnvironment Full = VoxelEnvironment::Evaluate(EnvironmentClockAt(18000), true);
    const FVoxelEnvironment New = VoxelEnvironment::Evaluate(EnvironmentClockAt(4 * 24000 + 18000), true);
    TestNearlyEqual(TEXT("midnight: moon straight up"), double(Full.MoonElevation), 90., 1e-3);
    TestEqual(TEXT("midnight: full moonlight"), Full.Moonlight, 1.f);
    TestEqual(TEXT("midnight: full night"), Full.Night, 1.f);
    const FVoxelEnvironment Noon = VoxelEnvironment::Evaluate(EnvironmentClockAt(6000), true);
    TestEqual(TEXT("noon: no moonlight"), Noon.Moonlight, 0.f);
    TestEqual(TEXT("noon: no night"), Noon.Night, 0.f);
    // The moon light: the full lux at a high full moon, the new moon's share at a new one, none with it set.
    TestNearlyEqual(TEXT("full moon lux"), double(VoxelAtmosphere::MoonLux(Full, 300.f, .35f)), 300., 1e-3);
    TestNearlyEqual(TEXT("new moon lux"), double(VoxelAtmosphere::MoonLux(New, 300.f, .35f)), 105., 1e-3);
    TestEqual(TEXT("no moon by day"), VoxelAtmosphere::MoonLux(Noon, 300.f, .35f), 0.f);

    // The night composed onto the day look: nothing changes without a night.
    const VoxelLook::FTargets Day = VoxelLook::Solve(FVoxelLook(), 1.);
    VoxelLook::FTargets Same = Day;
    VoxelAtmosphere::ComposeNight(Same, VoxelAtmosphere::FNight(), 1.);
    TestEqual(TEXT("no night: same min"), Same.Exposure.MinEV, Day.Exposure.MinEV);
    TestEqual(TEXT("no night: same max"), Same.Exposure.MaxEV, Day.Exposure.MaxEV);
    TestEqual(TEXT("no night: same sky"), Same.SkyLuminance, Day.SkyLuminance);
    VoxelAtmosphere::FNight Night;
    Night.Night = 1.f;
    Night.ExposureStops = 3.f;
    Night.SkyScale = .5f;
    // Under open sky the exposure range moves 3 stops down and the sky halves.
    VoxelLook::FTargets Open = Day;
    VoxelAtmosphere::ComposeNight(Open, Night, 1.);
    TestNearlyEqual(TEXT("open sky: min 3 stops down"), Open.Exposure.MinEV, Day.Exposure.MinEV - 3, 1e-9);
    TestNearlyEqual(TEXT("open sky: max 3 stops down"), Open.Exposure.MaxEV, Day.Exposure.MaxEV - 3, 1e-9);
    TestNearlyEqual(TEXT("open sky: bias kept"), Open.Exposure.Bias, Day.Exposure.Bias, 1e-9);
    TestNearlyEqual(TEXT("sky halved"), double(Open.SkyLuminance), double(Day.SkyLuminance) * .5, 1e-6);
    TestNearlyEqual(TEXT("haze halved"), double(Open.FogColorScale), double(Day.FogColorScale) * .5, 1e-6);
    // A cave (pin 0) keeps its exposure at night.
    const VoxelLook::FTargets CaveDay = VoxelLook::Solve(FVoxelLook(), 0.);
    VoxelLook::FTargets Cave = CaveDay;
    VoxelAtmosphere::ComposeNight(Cave, Night, 0.);
    TestEqual(TEXT("cave: same min"), Cave.Exposure.MinEV, CaveDay.Exposure.MinEV);
    TestEqual(TEXT("cave: same max"), Cave.Exposure.MaxEV, CaveDay.Exposure.MaxEV);
    // Half way into twilight, half the stops.
    Night.Night = .5f;
    VoxelLook::FTargets Dusk = Day;
    VoxelAtmosphere::ComposeNight(Dusk, Night, 1.);
    TestNearlyEqual(TEXT("dusk: 1.5 stops down"), Dusk.Exposure.MinEV, Day.Exposure.MinEV - 1.5, 1e-9);
    return true;
}
#endif
