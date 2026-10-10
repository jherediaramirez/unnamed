#if WITH_DEV_AUTOMATION_TESTS
#include "Clock/VoxelWorldClock.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sky/VoxelTimeline.h"
#include "Voxel/VoxelSkyReference.h"

// The sky's timelines (water and sky DN0; the plan's test list, from scout N 5 (a) to (d)). The oracles are scout N's
// black-box sweep: its tables are written out here, and the reference's facts/sweep.json is read too, so a changed
// reference file can't move them.
namespace
{
    bool SkyTimelineRead(FAutomationTestBase& Test, const TCHAR* Id, FVoxelTimeline& Out)
    {
        FString Error;
        const bool bRead = VoxelSkyReference::ReadTimeline(VoxelSkyReference::GetReferenceRoot(), Id, Out, Error);
        Test.TestTrue(FString::Printf(TEXT("%s reads (%s)"), Id, *Error), bRead);
        return bRead;
    }

    FVector4f SkyTimelineBase(FAutomationTestBase& Test, const TCHAR* Attribute)
    {
        FVector4f Base(0.f);
        FString Error;
        Test.TestTrue(FString::Printf(TEXT("%s has a base (%s)"), Attribute, *Error),
            VoxelSkyReference::ReadOverworldBase(VoxelSkyReference::GetReferenceRoot(), Attribute, Base, Error));
        return Base;
    }

    // facts/sweep.json's rows: "ticks" (65) or "moon" (16); Count rows, each an object, or an error.
    TArray<TSharedPtr<FJsonObject>> SkyTimelineSweep(FAutomationTestBase& Test, const TCHAR* Field, int32 Count)
    {
        FString Text;
        TSharedPtr<FJsonObject> Sweep;
        const FString Path = FPaths::Combine(VoxelSkyReference::GetReferenceRoot(), TEXT("facts/sweep.json"));
        if (!FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Sweep) || !Sweep.IsValid())
        {
            Test.AddError(TEXT("facts/sweep.json reads"));
            return {};
        }
        TArray<TSharedPtr<FJsonObject>> Rows;
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (Sweep->TryGetArrayField(Field, Values))
        {
            for (const TSharedPtr<FJsonValue>& Value : *Values)
            {
                const TSharedPtr<FJsonObject>* Row = nullptr;
                if (Value->TryGetObject(Row)) Rows.Add(*Row);
            }
        }
        if (!Test.TestEqual(FString::Printf(TEXT("facts/sweep.json: %s rows"), Field), Rows.Num(), Count)) return {};
        return Rows;
    }

    struct FSkyTimelineAngleRow
    {
        int32 Tick;
        double SunAngle;
    };

    // Scout N 5 (a), measured (sweep.json): the sun angle in degrees. The facts' table gives 256.42 at 22500 as a model
    // value; the sweep measured 258.291, as the model also gives.
    const FSkyTimelineAngleRow SkyTimelineAngles[] = {{0, 282.374}, {133, 284.436}, {1000, 297.546}, {3000, 325.401},
        {6000, 0.}, {9000, 34.599}, {12000, 77.626}, {12500, 85.489}, {12786, 90.064}, {13000, 93.521}, {18000, 180.},
        {22500, 258.291}, {23999, 282.359}};

    struct FSkyTimelineLightRow
    {
        int32 Tick;
        double Level;
        int32 Light;
        double Factor;
        double Stars;
        int32 Detector;
        int32 Inverted;
    };

    // Scout N 5 (b): sky_light_level (15 x the track), the open-sky light, sky_light_factor, star_brightness and both
    // daylight detectors. 13670 to 22330 is one column in the facts; its ends are rows here.
    const FSkyTimelineLightRow SkyTimelineLights[] = {{0, 14.18857, 15, .70332, .05463, 7, 0},
        {92, 14.74986, 15, .74071, .037, 7, 0}, {133, 15., 15, .75737, .03416, 7, 0},
        {500, 15., 15, .90652, .00878, 8, 0}, {11500, 15., 15, .90652, .00566, 8, 0},
        {11867, 15., 15, .75737, .03265, 7, 0}, {12500, 11.1381, 12, .50011, .1782, 4, 3},
        {12786, 9.39323, 10, .38387, .28564, 3, 5}, {13000, 8.08763, 9, .2969, .38943, 2, 6},
        {13228, 6.69662, 7, .24, .5, 1, 8}, {13500, 5.03716, 6, .24, .5, 1, 9}, {13670, 4., 4, .24, .5, 0, 11},
        {22330, 4., 4, .24, .5, 0, 11}, {22500, 5.03716, 6, .24, .5, 1, 9}, {23460, 10.89407, 11, .48385, .19292, 4, 4},
        {23999, 14.18247, 15, .70291, .05482, 7, 0}};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSkyTimelineParseTest, "Unnamed.Voxel.Sky.Timeline.Parse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSkyTimelineParseTest::RunTest(const FString& Parameters)
{
    FVoxelTimeline Day, Moon, EarlyGame, Villagers;
    if (!SkyTimelineRead(*this, TEXT("minecraft:day"), Day) || !SkyTimelineRead(*this, TEXT("minecraft:moon"), Moon) ||
        !SkyTimelineRead(*this, TEXT("minecraft:early_game"), EarlyGame) ||
        !SkyTimelineRead(*this, TEXT("minecraft:villager_schedule"), Villagers))
        return false;
    TestEqual(TEXT("day: period"), Day.PeriodTicks, 24000);
    TestEqual(TEXT("day: clock"), Day.Clock, FString(TEXT("minecraft:overworld")));
    TestEqual(TEXT("day: 18 tracks"), Day.Tracks.Num(), 18);
    TestEqual(TEXT("day: 6 markers"), Day.Markers.Num(), 6);
    TestEqual(TEXT("moon: period"), Moon.PeriodTicks, 192000);
    TestEqual(TEXT("moon: 2 tracks"), Moon.Tracks.Num(), 2);
    TestEqual(TEXT("early_game doesn't repeat"), EarlyGame.PeriodTicks, 0);

    const FVoxelTimelineTrack* Sun = Day.FindTrack(VoxelSky::SunAngle);
    const FVoxelTimelineTrack* Stars = Day.FindTrack(VoxelSky::StarBrightness);
    const FVoxelTimelineTrack* Sky = Day.FindTrack(VoxelSky::SkyColor);
    const FVoxelTimelineTrack* Burn = Day.FindTrack(TEXT("minecraft:gameplay/monsters_burn"));
    const FVoxelTimelineTrack* Phase = Moon.FindTrack(VoxelSky::MoonPhase);
    if (!TestTrue(TEXT("the tracks exist"), Sun && Stars && Sky && Burn && Phase)) return false;
    TestTrue(TEXT("sun_angle: Bezier (0.362, 0.241, 0.638, 0.759), override"),
        Sun->Ease == EVoxelTimelineEase::CubicBezier && Sun->Bezier == FVector4f(.362f, .241f, .638f, .759f) &&
            Sun->Modifier == EVoxelTimelineModifier::Override);
    if (!TestEqual(TEXT("sun_angle: 2 keys"), Sun->Keys.Num(), 2)) return false;
    // The wrap pair keeps file order: 360 then 0 at 6000.
    TestTrue(TEXT("sun_angle: 360 then 0 at 6000"),
        Sun->Keys[0].Ticks == 6000 && Sun->Keys[0].Value.X == 360.f && Sun->Keys[1].Ticks == 6000 &&
            Sun->Keys[1].Value.X == 0.f);
    TestTrue(TEXT("star_brightness: linear, maximum, 12 keys"),
        Stars->Ease == EVoxelTimelineEase::Linear && Stars->Modifier == EVoxelTimelineModifier::Maximum &&
            Stars->Keys.Num() == 12);
    TestTrue(TEXT("sky_color: a multiplied colour"),
        Sky->Type == EVoxelTimelineValue::Color && Sky->Modifier == EVoxelTimelineModifier::Multiply);
    TestTrue(TEXT("monsters_burn: or, booleans"),
        Burn->Type == EVoxelTimelineValue::Bool && Burn->Modifier == EVoxelTimelineModifier::Or);
    TestTrue(TEXT("moon_phase: 8 names"), Phase->Type == EVoxelTimelineValue::Name && Phase->Names.Num() == 8);

    // early_game: false until 120,000 ticks, then true for good; and'ed with the base.
    FVector4f Patrol;
    EarlyGame.Sample(TEXT("minecraft:gameplay/can_pillager_patrol_spawn"), 119999., Patrol);
    TestEqual(TEXT("early_game at 119,999"), Patrol.X, 0.f);
    EarlyGame.Sample(TEXT("minecraft:gameplay/can_pillager_patrol_spawn"), 5000000., Patrol);
    TestEqual(TEXT("early_game long after 120,000"), Patrol.X, 1.f);

    // Equal ticks keep file order: the later key holds from the tick on, the earlier one ends the ramp before it.
    FVoxelTimeline Pair;
    FString Error;
    TestTrue(TEXT("a pair at one tick parses"),
        VoxelSkyReference::ParseTimeline(
            TEXT(R"({"clock":"minecraft:overworld","period_ticks":20,"tracks":{"a":{"keyframes":[)"
                 R"({"ticks":10,"value":1.0},{"ticks":10,"value":5.0},{"ticks":0,"value":0.0}]}}})"),
            TEXT("pair"), Pair, Error));
    FVector4f Value;
    Pair.Sample(TEXT("a"), 10., Value);
    TestEqual(TEXT("pair: the later key at its tick"), Value.X, 5.f);
    Pair.Sample(TEXT("a"), 5., Value);
    TestEqual(TEXT("pair: the earlier key ends the ramp"), Value.X, .5f);
    Pair.Sample(TEXT("a"), 15., Value);
    TestEqual(TEXT("pair: from the later key around the wrap"), Value.X, 2.5f);

    // Strict reading: each of these fails, and the reason names the problem.
    struct FSkyTimelineBad
    {
        const TCHAR* Text;
        const TCHAR* Reason;
    };
    const FSkyTimelineBad Bad[] = {
        {TEXT(
             R"({"clock":"c","period_ticks":10,"tracks":{"a":{"modifier":"add","keyframes":[{"ticks":0,"value":1}]}}})"),
            TEXT("modifier \"add\"")},
        {TEXT(R"({"clock":"c","period_ticks":10,"tracks":{"a":{"modifier":3,"keyframes":[{"ticks":0,"value":1}]}}})"),
            TEXT("modifier")},
        {TEXT(
             R"({"clock":"c","period_ticks":10,"tracks":{"a":{"ease":"in_out_sine","keyframes":[{"ticks":0,"value":1}]}}})"),
            TEXT("unknown ease")},
        {TEXT(
             R"({"clock":"c","period_ticks":10,"tracks":{"a":{"ease":{"cubic_bezier":[0,0,1]},"keyframes":[{"ticks":0,"value":1}]}}})"),
            TEXT("unknown ease")},
        {TEXT(
             R"({"clock":"c","period_ticks":10,"tracks":{"a":{"ease":{"cubic_bezier":[1.5,0,0.5,1]},"keyframes":[{"ticks":0,"value":1}]}}})"),
            TEXT("unknown ease")},
        {TEXT(
             R"({"clock":"c","period_ticks":10,"tracks":{"a":{"keyframes":[{"ticks":0,"value":1},{"ticks":5,"value":"#ffffff"}]}}})"),
            TEXT("type differs")},
        {TEXT(R"({"clock":"c","period_ticks":10,"tracks":{"a":{"keyframes":[{"ticks":11,"value":1}]}}})"),
            TEXT("past the period")},
        {TEXT(R"({"clock":"c","period_ticks":10,"tracks":{"a":{"keyframes":[{"Ticks":0,"value":1}]}}})"),
            TEXT("keyframe 0 is not")},
        {TEXT(R"({"clock":"c","period_ticks":10,"tracks":{"a":{"keyframes":[{"ticks":"10","value":1}]}}})"),
            TEXT("keyframe 0 is not")},
        {TEXT(R"({"clock":"c","tracks":{"a":{"keyframes":[{"ticks":1e12,"value":1}]}}})"), TEXT("keyframe 0 is not")},
        {TEXT(R"({"clock":"c","period_ticks":10,"tracks":{"a":{"keyframes":[{"ticks":0,"value":"#fffff"}]}}})"),
            TEXT("keyframe 0 is not")},
        {TEXT(R"({"clock":"c","period_ticks":10,"tracks":{"a":{"keyframes":[]}}})"), TEXT("no keyframes")},
        {TEXT(R"({"clock":"c","period_ticks":10,"tracks":{"a":5}})"), TEXT("is not an object")},
        {TEXT(R"({"clock":"c","period_ticks":10.5,"tracks":{}})"), TEXT("period_ticks")},
        {TEXT(R"({"clock":5,"period_ticks":10,"tracks":{}})"), TEXT("needs a clock")},
        {TEXT(R"({"clock":"c","period":10,"tracks":{}})"), TEXT("unexpected field \"period\"")},
        {TEXT(R"({"clock":"c","period_ticks":10,"time_markers":{"m":11},"tracks":{}})"), TEXT("time marker m")},
        {TEXT(
             R"({"clock":"c","period_ticks":10,"time_markers":{"m":{"ticks":1,"show_in_commands":"yes"}},"tracks":{}})"),
            TEXT("time marker m")},
    };
    for (const FSkyTimelineBad& Case : Bad)
    {
        FVoxelTimeline Out;
        FString Reason;
        TestFalse(FString::Printf(TEXT("refused: %s"), Case.Text),
            VoxelSkyReference::ParseTimeline(Case.Text, TEXT("bad"), Out, Reason));
        TestTrue(FString::Printf(TEXT("the reason for %s names \"%s\" (%s)"), Case.Text, Case.Reason, *Reason),
            Reason.StartsWith(TEXT("bad")) && Reason.Contains(Case.Reason, ESearchCase::CaseSensitive));
    }
    // A timeline without a period takes markers at any tick.
    FVoxelTimeline Open;
    TestTrue(TEXT("a marker past tick 0 without a period"),
        VoxelSkyReference::ParseTimeline(
            TEXT(R"({"clock":"c","time_markers":{"m":50},"tracks":{}})"), TEXT("open"), Open, Error) &&
            Open.Markers.Num() == 1 && Open.Markers[0].Ticks == 50);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSkyTimelineAnglesTest, "Unnamed.Voxel.Sky.Timeline.Angles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSkyTimelineAnglesTest::RunTest(const FString& Parameters)
{
    FVoxelTimeline Day;
    if (!SkyTimelineRead(*this, TEXT("minecraft:day"), Day)) return false;
    // Scout N 5 (a): within 0.01 degrees (the model's largest error is 0.0036 at tick 730). The moon is the sun plus
    // 180 (running past 360), the stars are the sun.
    for (const FSkyTimelineAngleRow& Row : SkyTimelineAngles)
    {
        const VoxelSky::FAngles Angles = VoxelSky::SampleAngles(Day, Row.Tick);
        TestNearlyEqual(FString::Printf(TEXT("sun at %d"), Row.Tick), double(Angles.Sun), Row.SunAngle, .01);
        TestNearlyEqual(FString::Printf(TEXT("moon at %d"), Row.Tick), double(Angles.Moon), Row.SunAngle + 180., .01);
        TestNearlyEqual(FString::Printf(TEXT("stars at %d"), Row.Tick), double(Angles.Stars), Row.SunAngle, .01);
    }
    for (const TSharedPtr<FJsonObject>& Row : SkyTimelineSweep(*this, TEXT("ticks"), 65))
    {
        const int32 Tick = int32(Row->GetNumberField(TEXT("tick")));
        const VoxelSky::FAngles Angles = VoxelSky::SampleAngles(Day, Tick);
        TestNearlyEqual(FString::Printf(TEXT("sweep: sun at %d"), Tick), double(Angles.Sun),
            Row->GetNumberField(TEXT("sun_angle")), .01);
        TestNearlyEqual(FString::Printf(TEXT("sweep: moon at %d"), Tick), double(Angles.Moon),
            Row->GetNumberField(TEXT("moon_angle")), .01);
        TestNearlyEqual(FString::Printf(TEXT("sweep: stars at %d"), Tick), double(Angles.Stars),
            Row->GetNumberField(TEXT("star_angle")), .01);
    }
    // The wrap: 0 from 6000 on, nearly 360 just before.
    TestNearlyEqual(TEXT("sun just before 6000"), double(VoxelSky::SampleAngles(Day, 5999.99).Sun), 360., .01);
    TestEqual(TEXT("sun at 6000 exactly"), VoxelSky::SampleAngles(Day, 6000.).Sun, 0.f);
    // A whole day later the sky is the same (the clock's total keeps counting past 24,000).
    TestNearlyEqual(TEXT("sun at 30000 equals 6000"), double(VoxelSky::SampleAngles(Day, 30000.).Sun), 0., 1e-4);
    TestNearlyEqual(TEXT("sun at 24000 + 3000"), double(VoxelSky::SampleAngles(Day, 27000.).Sun), 325.401, .01);

    // Directions (scout N 4): towards the sun at tick 0 is east and low, (0.977, 0, 0.214); the moon is opposite.
    const VoxelSky::FAngles Dawn = VoxelSky::SampleAngles(Day, 0.);
    TestTrue(TEXT("sun direction at 0"), Dawn.SunDirection.Equals(FVector3f(.977f, 0.f, .214f), .001f));
    TestTrue(TEXT("moon opposite the sun"), Dawn.MoonDirection.Equals(-Dawn.SunDirection, 1e-5f));
    TestTrue(
        TEXT("noon straight up"), VoxelSky::SampleAngles(Day, 6000.).SunDirection.Equals(FVector3f(0, 0, 1), 1e-6f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSkyTimelineLevelsTest, "Unnamed.Voxel.Sky.Timeline.Levels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSkyTimelineLevelsTest::RunTest(const FString& Parameters)
{
    FVoxelTimeline Day;
    if (!SkyTimelineRead(*this, TEXT("minecraft:day"), Day)) return false;
    // The bases beneath the tracks: the wiki defaults 15, 1 and 0 (the overworld sets none of them).
    const FVector4f LevelBase = SkyTimelineBase(*this, VoxelSky::SkyLightLevel);
    const FVector4f FactorBase = SkyTimelineBase(*this, VoxelSky::SkyLightFactor);
    const FVector4f StarBase = SkyTimelineBase(*this, VoxelSky::StarBrightness);
    TestTrue(TEXT("bases 15, 1, 0"), LevelBase.X == 15.f && FactorBase.X == 1.f && StarBase.X == 0.f);
    const auto Check = [&](int32 Tick, double Level, double Factor, double Stars, const TCHAR* Source)
    {
        TestNearlyEqual(FString::Printf(TEXT("%s: sky_light_level at %d"), Source, Tick),
            double(Day.Apply(VoxelSky::SkyLightLevel, Tick, LevelBase).X), Level, 1e-5);
        TestNearlyEqual(FString::Printf(TEXT("%s: sky_light_factor at %d"), Source, Tick),
            double(Day.Apply(VoxelSky::SkyLightFactor, Tick, FactorBase).X), Factor, 1e-5);
        TestNearlyEqual(FString::Printf(TEXT("%s: star_brightness at %d"), Source, Tick),
            double(Day.Apply(VoxelSky::StarBrightness, Tick, StarBase).X), Stars, 1e-5);
    };
    for (const FSkyTimelineLightRow& Row : SkyTimelineLights)
        Check(Row.Tick, Row.Level, Row.Factor, Row.Stars, TEXT("5 (b)"));
    for (const TSharedPtr<FJsonObject>& Row : SkyTimelineSweep(*this, TEXT("ticks"), 65))
    {
        Check(int32(Row->GetNumberField(TEXT("tick"))), Row->GetNumberField(TEXT("sky_light_level")),
            Row->GetNumberField(TEXT("sky_light_factor")), Row->GetNumberField(TEXT("star_brightness")), TEXT("sweep"));
    }
    // The night's level is exactly 4 in single precision (15 x 0.26666668).
    TestEqual(TEXT("sky_light_level at midnight"), Day.Apply(VoxelSky::SkyLightLevel, 18000., LevelBase).X, 4.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSkyTimelineLightTest, "Unnamed.Voxel.Sky.Timeline.OpenSkyLight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSkyTimelineLightTest::RunTest(const FString& Parameters)
{
    FVoxelTimeline Day;
    if (!SkyTimelineRead(*this, TEXT("minecraft:day"), Day)) return false;
    const FVector4f LevelBase = SkyTimelineBase(*this, VoxelSky::SkyLightLevel);
    const auto Light = [&](int32 Tick)
    { return VoxelSky::OpenSkyLight(Day.Apply(VoxelSky::SkyLightLevel, Tick, LevelBase).X); };
    // The light at an open-sky block is the ceiling of the level (5.037 gives 6, 4.0 gives 4). The detectors are a
    // table: the inverted one is 15 minus the light; the normal one also depends on the sun, so only its rows are
    // pinned, and it never exceeds the light.
    for (const FSkyTimelineLightRow& Row : SkyTimelineLights)
    {
        TestEqual(FString::Printf(TEXT("5 (b): light at %d"), Row.Tick), Light(Row.Tick), Row.Light);
        TestEqual(
            FString::Printf(TEXT("5 (b): inverted detector at %d"), Row.Tick), 15 - Light(Row.Tick), Row.Inverted);
        TestTrue(FString::Printf(TEXT("5 (b): detector at %d within the light"), Row.Tick), Row.Detector <= Row.Light);
    }
    for (const TSharedPtr<FJsonObject>& Row : SkyTimelineSweep(*this, TEXT("ticks"), 65))
    {
        const int32 Tick = int32(Row->GetNumberField(TEXT("tick")));
        TestEqual(
            FString::Printf(TEXT("sweep: light at %d"), Tick), Light(Tick), int32(Row->GetNumberField(TEXT("light"))));
        TestEqual(FString::Printf(TEXT("sweep: inverted detector at %d"), Tick), 15 - Light(Tick),
            int32(Row->GetNumberField(TEXT("detector_inverted"))));
        TestTrue(FString::Printf(TEXT("sweep: detector at %d within the light"), Tick),
            Row->GetNumberField(TEXT("detector")) <= Light(Tick));
    }
    TestEqual(TEXT("ceiling of 5.037"), VoxelSky::OpenSkyLight(5.03716f), 6);
    TestEqual(TEXT("ceiling of 4.0"), VoxelSky::OpenSkyLight(4.f), 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSkyTimelineMoonTest, "Unnamed.Voxel.Sky.Timeline.MoonPhase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSkyTimelineMoonTest::RunTest(const FString& Parameters)
{
    FVoxelTimeline Moon;
    if (!SkyTimelineRead(*this, TEXT("minecraft:moon"), Moon)) return false;
    // Scout N 5 (c): the phase changes exactly at each multiple of 24,000 (the day's tick 0), not at noon or midnight.
    const TPair<int32, int32> Listed[] = {{0, 0}, {6000, 0}, {18000, 0}, {23999, 0}, {24000, 1}, {48000, 2}, {90000, 3},
        {114000, 4}, {138000, 5}, {162000, 6}, {186000, 7}, {210000, 0}};
    FVoxelWorldClock Clock;
    for (const TPair<int32, int32>& Row : Listed)
    {
        TestEqual(
            FString::Printf(TEXT("5 (c): phase at %d"), Row.Key), VoxelSky::SampleMoonPhase(Moon, Row.Key), Row.Value);
        Clock.SetTotal(Row.Key);
        TestEqual(FString::Printf(TEXT("5 (c): DN1's clock agrees at %d"), Row.Key), Clock.GetMoonPhase(), Row.Value);
    }
    const FVoxelTimelineTrack* Track = Moon.FindTrack(VoxelSky::MoonPhase);
    for (const TSharedPtr<FJsonObject>& Row : SkyTimelineSweep(*this, TEXT("moon"), 16))
    {
        const int32 Tick = int32(Row->GetNumberField(TEXT("tick")));
        const int32 Phase = VoxelSky::SampleMoonPhase(Moon, Tick);
        TestTrue(FString::Printf(TEXT("sweep: %s at %d"), *Row->GetStringField(TEXT("phase")), Tick),
            Track && Phase != INDEX_NONE && Track->Names.Contains(Row->GetStringField(TEXT("phase"))) &&
                Track->Names[int32(Track->Sample(Tick, Moon.PeriodTicks).X)] == Row->GetStringField(TEXT("phase")));
        Clock.SetTotal(Tick);
        TestEqual(FString::Printf(TEXT("sweep: DN1's clock agrees at %d"), Tick), Clock.GetMoonPhase(), Phase);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSkyTimelineColorsTest, "Unnamed.Voxel.Sky.Timeline.Colors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSkyTimelineColorsTest::RunTest(const FString& Parameters)
{
    FVoxelTimeline Day;
    if (!SkyTimelineRead(*this, TEXT("minecraft:day"), Day)) return false;
    // Scout N 5 (d): plains (the overworld's #78a7ff) at noon, and black at 13670 and midnight. The in-between values
    // are DN0's colour probe (COLOUR_TESTS).
    const FVector4f Sky = SkyTimelineBase(*this, VoxelSky::SkyColor);
    TestEqual(TEXT("the overworld's sky"), VoxelSky::ColorToHex(Sky), FString(TEXT("#78a7ff")));
    TestEqual(
        TEXT("sky at 6000"), VoxelSky::ColorToHex(Day.Apply(VoxelSky::SkyColor, 6000., Sky)), FString(TEXT("#78a7ff")));
    TestEqual(TEXT("sky at 13670"), VoxelSky::ColorToHex(Day.Apply(VoxelSky::SkyColor, 13670., Sky)),
        FString(TEXT("#000000")));
    TestEqual(TEXT("sky at 18000"), VoxelSky::ColorToHex(Day.Apply(VoxelSky::SkyColor, 18000., Sky)),
        FString(TEXT("#000000")));
    FVector4f Parsed;
    TestTrue(TEXT("ARGB parses"),
        VoxelSky::ParseColor(TEXT("#ccffffff"), Parsed) && Parsed.W == 204.f / 255.f && Parsed.X == 1.f);
    TestEqual(TEXT("ARGB round trip"), VoxelSky::ColorToHex(Parsed, true), FString(TEXT("#ccffffff")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSkyTimelineMarkersTest, "Unnamed.Voxel.Sky.Timeline.Markers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSkyTimelineMarkersTest::RunTest(const FString& Parameters)
{
    FVoxelTimeline Day;
    if (!SkyTimelineRead(*this, TEXT("minecraft:day"), Day)) return false;
    // DN1's marker constants (FVoxelWorldClock::FindMarker) equal day.json's: exactly the four shown in commands.
    TArray<FString> Shown;
    for (const FVoxelTimelineMarker& Marker : Day.Markers)
    {
        if (!Marker.bShowInCommands) continue;
        Shown.Add(Marker.Name);
        int32 Ticks = -1;
        TestTrue(
            FString::Printf(TEXT("%s: DN1 knows it"), *Marker.Name), FVoxelWorldClock::FindMarker(Marker.Name, Ticks));
        TestEqual(FString::Printf(TEXT("%s: DN1's tick"), *Marker.Name), Ticks, Marker.Ticks);
    }
    Shown.Sort();
    TestEqual(TEXT("the markers shown in commands"), FString::Join(Shown, TEXT(",")),
        FString(TEXT("minecraft:day,minecraft:midnight,minecraft:night,minecraft:noon")));
    const FVoxelTimelineMarker* Wake = Day.FindMarker(TEXT("minecraft:wake_up_from_sleep"));
    TestTrue(TEXT("wake_up_from_sleep: 0, not shown"), Wake && Wake->Ticks == 0 && !Wake->bShowInCommands);
    int32 Ticks = -1;
    TestFalse(TEXT("DN1 doesn't offer wake_up_from_sleep"),
        FVoxelWorldClock::FindMarker(TEXT("minecraft:wake_up_from_sleep"), Ticks));
    return true;
}

#endif
