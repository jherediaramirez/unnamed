#if WITH_DEV_AUTOMATION_TESTS
#include "Clock/VoxelWorldClock.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include <limits>
#include "Worlds/UnnamedSavedWorlds.h"

// The world clock (water and sky DN1; the plan's test list, from scout N 5 (c) and (f)).
namespace
{
    // Runs voxel.Time's words on a clock; the reply, or "refused: <reply>".
    FString RunTime(FVoxelWorldClock& Clock, const TCHAR* Command)
    {
        TArray<FString> Args;
        FString(Command).ParseIntoArrayWS(Args);
        FString Reply;
        return Clock.RunCommand(Args, Reply) ? Reply : TEXT("refused: ") + Reply;
    }

    // Runs whole frames of Seconds each; the game ticks they ran.
    int32 RunFrames(FVoxelWorldClock& Clock, int32 Frames, double Seconds, bool bAdvanceTime = true)
    {
        int32 Ticks = 0;
        for (int32 Frame = 0; Frame < Frames; ++Frame)
            Ticks += Clock.Advance(Seconds, bAdvanceTime);
        return Ticks;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelClockTicksTest, "Unnamed.Voxel.Clock.Ticks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelClockTicksTest::RunTest(const FString&)
{
    // 20 game ticks per second at any frame rate, the partial tick carried between frames.
    for (const int32 Fps : {30, 60, 144, 165, 240})
    {
        FVoxelWorldClock Clock;
        TestEqual(
            FString::Printf(TEXT("%d frames of 1/%d s run 20 ticks"), Fps, Fps), RunFrames(Clock, Fps, 1. / Fps), 20);
        TestEqual(TEXT("...and the game tick counts them"), Clock.GetGameTick(), uint64(20));
        TestEqual(TEXT("...and the day clock with them, at rate 1"), Clock.GetDayClock().TotalTicks, int64(20));
    }
    FVoxelWorldClock Clock;
    TestEqual(TEXT("0.07 s runs one tick"), Clock.Advance(.07, true), 1);
    TestEqual(TEXT("...a second 0.07 s one more (1.8 due)"), Clock.Advance(.07, true), 1);
    TestEqual(TEXT("...a third two (2.2 due)"), Clock.Advance(.07, true), 2);
    TestTrue(TEXT("...with 0.2 of a tick carried"), FMath::IsNearlyEqual(Clock.GetTickAlpha(), .2, 1e-9));
    TestTrue(TEXT("The smooth day time adds the carried share"),
        FMath::IsNearlyEqual(Clock.GetSmoothTicks(true), 4.2, 1e-9));
    TestTrue(TEXT("...but not while advance_time is off"), FMath::IsNearlyEqual(Clock.GetSmoothTicks(false), 4., 1e-9));
    FVoxelWorldClock Short;
    TestEqual(TEXT("Four 0.01 s frames run no tick"), RunFrames(Short, 4, .01), 0);
    TestEqual(TEXT("...the fifth runs one"), Short.Advance(.01, true), 1);
    FVoxelWorldClock Idle;
    TestEqual(TEXT("No time, no tick"),
        Idle.Advance(0, true) + Idle.Advance(-1, true) + Idle.Advance(std::numeric_limits<double>::quiet_NaN(), true),
        0);
    TestEqual(TEXT("A new world's clock starts at tick 0"), FVoxelWorldClock().GetDayClock().TotalTicks, int64(0));
    TestEqual(
        TEXT("...on day 0 at time 0"), FVoxelWorldClock().GetTimeOfDay() + int32(FVoxelWorldClock().GetDayNumber()), 0);

    // TakeTicks then Step is Advance, for callers that run a system after each tick (W2a's fluids).
    FVoxelWorldClock Split;
    const int32 Taken = Split.TakeTicks(.12);
    for (int32 Index = 0; Index < Taken; ++Index)
        Split.Step(true);
    TestEqual(TEXT("TakeTicks takes the due ticks (0.12 s: 2)"), Taken, 2);
    TestEqual(TEXT("...and each Step runs one"), Split.GetGameTick(), uint64(2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelClockDayTest, "Unnamed.Voxel.Clock.DayClock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelClockDayTest::RunTest(const FString&)
{
    FVoxelWorldClock Clock;
    Clock.SetRate(3);
    RunFrames(Clock, 20, .05);
    TestEqual(TEXT("Rate 3: 60 day ticks in 20 game ticks"), Clock.GetDayClock().TotalTicks, int64(60));
    Clock.SetRate(.5f);
    RunFrames(Clock, 3, .05);
    TestEqual(TEXT("Rate 0.5: 3 game ticks give one day tick..."), Clock.GetDayClock().TotalTicks, int64(61));
    TestTrue(TEXT("...and half a tick carried"), FMath::IsNearlyEqual(Clock.GetDayClock().PartialTick, .5, 1e-9));
    RunFrames(Clock, 1, .05);
    TestEqual(TEXT("...which the next one completes"), Clock.GetDayClock().TotalTicks, int64(62));

    Clock.SetRate(1);
    Clock.SetPaused(true);
    const uint64 GameTick = Clock.GetGameTick();
    RunFrames(Clock, 20, .05);
    TestEqual(TEXT("Paused: the day stops"), Clock.GetDayClock().TotalTicks, int64(62));
    TestEqual(TEXT("...but the game tick runs"), Clock.GetGameTick(), GameTick + 20);
    Clock.SetPaused(false);
    RunFrames(Clock, 20, .05, false);
    TestEqual(TEXT("advance_time off: the day stops"), Clock.GetDayClock().TotalTicks, int64(62));
    TestEqual(TEXT("...but the game tick runs"), Clock.GetGameTick(), GameTick + 40);
    RunFrames(Clock, 20, .05);
    TestEqual(TEXT("Resumed with the rule on: the day runs again"), Clock.GetDayClock().TotalTicks, int64(82));

    Clock.SetRate(0);
    TestEqual(TEXT("A rate of 0 is not taken (1)"), Clock.GetDayClock().Rate, 1.f);
    Clock.SetRate(5000);
    TestEqual(TEXT("A rate above 1000 is 1000"), Clock.GetDayClock().Rate, 1000.f);
    Clock.SetRate(1);

    // The moon's phase changes exactly at each multiple of 24,000 (scout N 5 (c)); a world starts on a full moon.
    const TPair<int64, const TCHAR*> Phases[] = {{0, TEXT("full moon")}, {6000, TEXT("full moon")},
        {18000, TEXT("full moon")}, {23999, TEXT("full moon")}, {24000, TEXT("waning gibbous")},
        {48000, TEXT("third quarter")}, {90000, TEXT("waning crescent")}, {114000, TEXT("new moon")},
        {138000, TEXT("waxing crescent")}, {162000, TEXT("first quarter")}, {186000, TEXT("waxing gibbous")},
        {210000, TEXT("full moon")}};
    for (const TPair<int64, const TCHAR*>& Phase : Phases)
    {
        Clock.SetTotal(Phase.Key);
        TestEqual(FString::Printf(TEXT("Moon at %lld"), Phase.Key),
            FString(FVoxelWorldClock::GetMoonPhaseName(Clock.GetMoonPhase())), FString(Phase.Value));
    }
    Clock.SetTotal(25002);
    TestEqual(TEXT("Day number from 0"), Clock.GetDayNumber(), int64(1));
    TestEqual(TEXT("Time of day: the total mod 24,000"), Clock.GetTimeOfDay(), 1002);
    TestEqual(TEXT("The F3 line"), Clock.Describe(true),
        FString(TEXT("Time: day 1 at 1002 (07:00) | moon waning gibbous | game tick 84")));
    Clock.SetRate(3);
    Clock.SetPaused(true);
    TestEqual(TEXT("...with the rate, the pause and the rule"), Clock.Describe(false),
        FString(TEXT("Time: day 1 at 1002 (07:00) | moon waning gibbous | game tick 84 | rate 3.0x | paused | "
                     "advance_time off")));
    Clock.SetTotal(18000 + 500);
    TestTrue(TEXT("Midnight and a half hour reads 00:30"), Clock.Describe(true).Contains(TEXT("(00:30)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelClockCommandsTest, "Unnamed.Voxel.Clock.Commands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelClockCommandsTest::RunTest(const FString&)
{
    // Scout N's probe: "time add 25000" from 2 answered "Set minecraft:overworld to 25002 tick(s)", and "time query
    // day" then "Timeline minecraft:day is at 1004 tick(s)" (two more ticks had run).
    FVoxelWorldClock Clock;
    TestEqual(TEXT("set 0"), RunTime(Clock, TEXT("set 0")), FString(TEXT("Set minecraft:overworld to 0 tick(s)")));
    RunFrames(Clock, 2, .05);
    TestEqual(TEXT("add 25000 from 2"), RunTime(Clock, TEXT("add 25000")),
        FString(TEXT("Set minecraft:overworld to 25002 tick(s)")));
    RunFrames(Clock, 2, .05);
    TestEqual(TEXT("query day: the total mod 24,000"), RunTime(Clock, TEXT("query day")),
        FString(TEXT("Timeline minecraft:day is at 1004 tick(s)")));
    TestEqual(TEXT("query minecraft:day"), RunTime(Clock, TEXT("query minecraft:day")),
        FString(TEXT("Timeline minecraft:day is at 1004 tick(s)")));
    TestEqual(TEXT("query time: the total"), RunTime(Clock, TEXT("query time")),
        FString(TEXT("Clock minecraft:overworld is at 25004 tick(s)")));
    TestEqual(
        TEXT("query gametime"), RunTime(Clock, TEXT("query gametime")), FString(TEXT("The game time is 4 tick(s)")));
    TestEqual(TEXT("rate 2"), RunTime(Clock, TEXT("rate 2")),
        FString(TEXT("Clock minecraft:overworld will now advance at 2.0x normal rate")));
    TestEqual(TEXT("pause"), RunTime(Clock, TEXT("pause")), FString(TEXT("Paused clock minecraft:overworld")));
    TestTrue(TEXT("...pauses"), Clock.GetDayClock().bPaused);
    TestEqual(TEXT("resume"), RunTime(Clock, TEXT("resume")), FString(TEXT("Resumed clock minecraft:overworld")));
    TestFalse(TEXT("...resumes"), Clock.GetDayClock().bPaused);

    // Times: whole ticks, or a number with d (24,000), s (20) or t.
    const TPair<const TCHAR*, int64> Times[] = {{TEXT("1d"), 24000}, {TEXT("0.5d"), 12000}, {TEXT("20s"), 400},
        {TEXT("100t"), 100}, {TEXT("13219"), 13219}, {TEXT("2147483647"), 2147483647}};
    for (const TPair<const TCHAR*, int64>& Time : Times)
    {
        int64 Ticks = -1;
        TestTrue(FString::Printf(TEXT("%s is a time"), Time.Key), FVoxelWorldClock::ParseTime(Time.Key, Ticks));
        TestEqual(FString::Printf(TEXT("...of %lld ticks"), Time.Value), Ticks, Time.Value);
    }
    RunTime(Clock, TEXT("set 1d"));
    TestEqual(TEXT("set 1d"), Clock.GetDayClock().TotalTicks, int64(24000));
    for (const TCHAR* Bad : {TEXT("set -5"), TEXT("set abc"), TEXT("add 2147483648"), TEXT("set 1x"), TEXT("set d"),
             TEXT("rate 0"), TEXT("rate 1001"), TEXT("rate x"), TEXT("query moon"), TEXT("set"), TEXT(""),
             TEXT("pause now"), TEXT("jump 5")})
        TestTrue(FString::Printf(TEXT("Refused: \"%s\""), Bad), RunTime(Clock, Bad).StartsWith(TEXT("refused: ")));
    TestEqual(TEXT("...and the clock is unchanged"), Clock.GetDayClock().TotalTicks, int64(24000));
    TestEqual(TEXT("...at its rate"), Clock.GetDayClock().Rate, 2.f);
    TestEqual(TEXT("A tiny rate is shown in full, not as 0.0"), RunTime(Clock, TEXT("rate 0.0000001")),
        FString(TEXT("Clock minecraft:overworld will now advance at 1.00000001e-07x normal rate")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelClockMarkersTest, "Unnamed.Voxel.Clock.Markers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelClockMarkersTest::RunTest(const FString&)
{
    // A marker moves the clock forward to its next tick of the day, never back (wiki, Commands/time).
    FVoxelWorldClock Clock;
    TestEqual(TEXT("set noon"), RunTime(Clock, TEXT("set noon")),
        FString(TEXT("Set minecraft:overworld to time marker minecraft:noon")));
    TestEqual(TEXT("...from 0 is 6000"), Clock.GetDayClock().TotalTicks, int64(6000));
    RunTime(Clock, TEXT("set noon"));
    TestEqual(TEXT("At noon, noon stays"), Clock.GetDayClock().TotalTicks, int64(6000));
    RunTime(Clock, TEXT("set day"));
    TestEqual(TEXT("day after noon is the next day's 1000"), Clock.GetDayClock().TotalTicks, int64(25000));
    RunTime(Clock, TEXT("set night"));
    TestEqual(TEXT("night: 13000 of that day"), Clock.GetDayClock().TotalTicks, int64(37000));
    RunTime(Clock, TEXT("set midnight"));
    TestEqual(TEXT("midnight: 18000"), Clock.GetDayClock().TotalTicks, int64(42000));
    TestEqual(TEXT("minecraft:noon"), RunTime(Clock, TEXT("set minecraft:noon")),
        FString(TEXT("Set minecraft:overworld to time marker minecraft:noon")));
    TestEqual(TEXT("...after midnight is the next day's noon"), Clock.GetDayClock().TotalTicks, int64(54000));
    int32 Marker = 0;
    TestFalse(TEXT("dusk is not a marker"), FVoxelWorldClock::FindMarker(TEXT("dusk"), Marker));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelClockCapTest, "Unnamed.Voxel.Clock.Cap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelClockCapTest::RunTest(const FString&)
{
    // The pawn's cap (UFoundationProfile::MaxCatchupSteps, 5): a hitch runs at most 5 ticks, and the rest is dropped.
    FVoxelWorldClock Clock(.05, 5);
    TestEqual(TEXT("0.15 s behind runs all 3 ticks"), Clock.Advance(.15, true), 3);
    TestEqual(TEXT("...dropping none"), Clock.GetDroppedTicks(), int64(0));
    TestEqual(TEXT("A 1 s hitch runs 5 ticks"), Clock.Advance(1., true), 5);
    TestEqual(TEXT("...and drops 15"), Clock.GetDroppedTicks(), int64(15));
    TestTrue(TEXT("...carrying nothing"), FMath::IsNearlyEqual(Clock.GetTickAlpha(), 0., 1e-9));
    TestEqual(TEXT("...and the day clock moved with the ticks run"), Clock.GetDayClock().TotalTicks, int64(8));
    TestEqual(TEXT("The next frames run as before"), RunFrames(Clock, 60, 1. / 60), 20);
    TestEqual(TEXT("...dropping no more"), Clock.GetDroppedTicks(), int64(15));
    FVoxelWorldClock One(.05, 1);
    TestEqual(TEXT("A cap of 1 runs one tick a frame"), One.Advance(.5, true), 1);
    TestEqual(TEXT("...and drops 9"), One.GetDroppedTicks(), int64(9));
    return true;
}

namespace
{
    // A world.json written by the build before DN1 (create-world.mjs's "Hello World" after play, at 24d76ee: byte for
    // byte).
    const TCHAR* const PreClockWorldJson = TEXT("{\r\n") TEXT("\t\"version\": 1,\r\n") TEXT(
        "\t\"generator\": 2,\r\n") TEXT("\t\"name\": \"Hello Again\",\r\n") TEXT("\t\"seed\": \"hello\",\r\n")
        TEXT("\t\"gameMode\": \"Survival\",\r\n") TEXT("\t\"difficulty\": \"Normal\",\r\n") TEXT(
            "\t\"commands\": false,\r\n") TEXT("\t\"type\": \"Default\",\r\n") TEXT("\t\"structures\": true,\r\n")
            TEXT("\t\"bonus\": false,\r\n") TEXT("\t\"biome\": \"minecraft:plains\",\r\n") TEXT(
                "\t\"flatBiome\": \"minecraft:plains\",\r\n") TEXT("\t\"flat\": [\r\n") TEXT("\t\t{\r\n")
                TEXT("\t\t\t\"block\": \"minecraft:grass_block\",\r\n") TEXT("\t\t\t\"height\": 1\r\n") TEXT(
                    "\t\t},\r\n") TEXT("\t\t{\r\n") TEXT("\t\t\t\"block\": \"minecraft:dirt\",\r\n")
                    TEXT("\t\t\t\"height\": 2\r\n") TEXT("\t\t},\r\n") TEXT("\t\t{\r\n") TEXT(
                        "\t\t\t\"block\": \"minecraft:bedrock\",\r\n") TEXT("\t\t\t\"height\": 1\r\n") TEXT("\t\t}\r\n")
                        TEXT("\t],\r\n") TEXT("\t\"experiments\":\r\n") TEXT("\t{\r\n") TEXT("\t},\r\n") TEXT(
                            "\t\"rules\":\r\n") TEXT("\t{\r\n") TEXT("\t\t\"advance_time\": true,\r\n")
                            TEXT("\t\t\"advance_weather\": true,\r\n") TEXT("\t\t\"allow_entering_nether_using_"
                                                                            "portals\": true,\r\n") TEXT(
                                "\t\t\"block_drops\": true,\r\n") TEXT("\t\t\"block_explosion_drop_decay\": true,\r\n")
                                TEXT("\t\t\"command_blocks_work\": true,\r\n") TEXT(
                                    "\t\t\"command_block_output\": true,\r\n") TEXT("\t\t\"drowning_damage\": "
                                                                                    "true,\r\n")
                                    TEXT("\t\t\"elytra_movement_check\": true,\r\n") TEXT("\t\t\"ender_pearls_vanish_"
                                                                                          "on_death\": true,\r\n") TEXT(
                                        "\t\t\"entity_drops\": true,\r\n") TEXT("\t\t\"fall_damage\": true,\r\n")
                                        TEXT("\t\t\"fire_damage\": true,\r\n") TEXT("\t\t\"fire_spread_radius_around_"
                                                                                    "player\": 128,\r\n") TEXT(
                                            "\t\t\"forgive_dead_players\": true,\r\n")
                                            TEXT("\t\t\"freeze_damage\": true,\r\n") TEXT(
                                                "\t\t\"global_sound_events\": true,\r\n")
                                                TEXT("\t\t\"immediate_respawn\": false,\r\n") TEXT(
                                                    "\t\t\"keep_inventory\": false,\r\n") TEXT("\t\t\"lava_source_"
                                                                                               "conversion\": "
                                                                                               "false,\r\n")
                                                    TEXT("\t\t\"limited_crafting\": false,\r\n") TEXT(
                                                        "\t\t\"locator_bar\": true,\r\n") TEXT("\t\t\"log_admin_"
                                                                                               "commands\": true,\r\n")
                                                        TEXT("\t\t\"max_block_modifications\": 32768,\r\n") TEXT(
                                                            "\t\t\"max_command_forks\": 65536,\r\n") TEXT("\t\t\"max_"
                                                                                                          "command_"
                                                                                                          "sequence_"
                                                                                                          "length\": "
                                                                                                          "65536,\r\n")
                                                            TEXT("\t\t\"max_entity_cramming\": 24,\r\n") TEXT(
                                                                "\t\t\"max_minecart_speed\": 8,\r\n") TEXT("\t\t\"max_"
                                                                                                           "snow_"
                                                                                                           "accumulatio"
                                                                                                           "n_height\":"
                                                                                                           " 1,\r\n")
                                                                TEXT("\t\t\"mob_drops\": true,\r\n") TEXT(
                                                                    "\t\t\"mob_explosion_drop_decay\": true,\r\n")
                                                                    TEXT(
                                                                        "\t\t\"mob_griefing\": true,\r\n")
                                                                        TEXT("\t\t\"natural_health_regeneration\": "
                                                                             "true,\r\n") TEXT("\t\t\"player_movement_"
                                                                                               "check\": true,\r\n") TEXT(
                                                                            "\t\t\"players_nether_portal_creative_"
                                                                            "delay\": 0,\r\n") TEXT("\t\t\"players_"
                                                                                                    "nether_portal_"
                                                                                                    "default_delay\": "
                                                                                                    "80,\r\n")
                                                                            TEXT("\t\t\"players_sleeping_percentage\": "
                                                                                 "100,\r\n") TEXT("\t\t\"projectiles_"
                                                                                                  "can_break_blocks\": "
                                                                                                  "true,\r\n") TEXT("\t"
                                                                                                                    "\t"
                                                                                                                    "\""
                                                                                                                    "pv"
                                                                                                                    "p"
                                                                                                                    "\""
                                                                                                                    ": "
                                                                                                                    "tr"
                                                                                                                    "ue"
                                                                                                                    ","
                                                                                                                    "\r"
                                                                                                                    "\n") TEXT(
                                                                                "\t\t\"raids\": true,\r\n") TEXT("\t\t"
                                                                                                                 "\"ran"
                                                                                                                 "dom_"
                                                                                                                 "tick_"
                                                                                                                 "speed"
                                                                                                                 "\": "
                                                                                                                 "3,"
                                                                                                                 "\r\n")
                                                                                TEXT("\t\t\"reduced_debug_info\": "
                                                                                     "false,\r\n") TEXT("\t\t\"respawn_"
                                                                                                        "radius\": "
                                                                                                        "10,\r\n") TEXT(
                                                                                    "\t\t\"send_command_feedback\": "
                                                                                    "true,\r\n") TEXT("\t\t\"show_"
                                                                                                      "advancement_"
                                                                                                      "messages\": "
                                                                                                      "true,\r\n")
                                                                                    TEXT("\t\t\"show_death_messages\": "
                                                                                         "true,\r\n") TEXT(
                                                                                        "\t\t\"spawner_blocks_work\": "
                                                                                        "true,\r\n") TEXT("\t\t\"spawn_"
                                                                                                          "mobs\": "
                                                                                                          "true,\r\n")
                                                                                        TEXT("\t\t\"spawn_monsters\": "
                                                                                             "true,\r\n") TEXT(
                                                                                            "\t\t\"spawn_patrols\": "
                                                                                            "true,\r\n") TEXT("\t\t\"sp"
                                                                                                              "awn_"
                                                                                                              "phantoms"
                                                                                                              "\": "
                                                                                                              "true,"
                                                                                                              "\r\n")
                                                                                            TEXT("\t\t\"spawn_"
                                                                                                 "wandering_traders\": "
                                                                                                 "true,\r\n") TEXT(
                                                                                                "\t\t\"spawn_wardens\":"
                                                                                                " true,\r\n") TEXT("\t"
                                                                                                                   "\t"
                                                                                                                   "\"s"
                                                                                                                   "pec"
                                                                                                                   "tat"
                                                                                                                   "ors"
                                                                                                                   "_ge"
                                                                                                                   "ner"
                                                                                                                   "ate"
                                                                                                                   "_ch"
                                                                                                                   "unk"
                                                                                                                   "s\""
                                                                                                                   ": "
                                                                                                                   "tru"
                                                                                                                   "e,"
                                                                                                                   "\r"
                                                                                                                   "\n")
                                                                                                TEXT("\t\t\"spread_"
                                                                                                     "vines\": "
                                                                                                     "true,\r\n") TEXT(
                                                                                                    "\t\t\"tnt_"
                                                                                                    "explodes\": "
                                                                                                    "true,\r\n")
                                                                                                    TEXT("\t\t\"tnt_"
                                                                                                         "explosion_"
                                                                                                         "drop_decay\":"
                                                                                                         " false,\r\n")
                                                                                                        TEXT("\t\t\"uni"
                                                                                                             "versal_"
                                                                                                             "anger\": "
                                                                                                             "false,"
                                                                                                             "\r\n") TEXT(
                                                                                                            "\t\t\"wate"
                                                                                                            "r_source_"
                                                                                                            "conversion"
                                                                                                            "\": "
                                                                                                            "true\r\n") TEXT("\t},\r\n")
                                                                                                            TEXT("\t\"s"
                                                                                                                 "eedVa"
                                                                                                                 "lue\""
                                                                                                                 ": "
                                                                                                                 "\"165"
                                                                                                                 "67679"
                                                                                                                 "05311"
                                                                                                                 "47700"
                                                                                                                 "7\","
                                                                                                                 "\r\n") TEXT(
                                                                                                                "\t\"cr"
                                                                                                                "eated"
                                                                                                                "\": "
                                                                                                                "\"2026"
                                                                                                                "-10-"
                                                                                                                "10T19:"
                                                                                                                "04:01."
                                                                                                                "092Z\""
                                                                                                                ",\r\n")
                                                                                                                TEXT(
                                                                                                                    "\t"
                                                                                                                    "\""
                                                                                                                    "la"
                                                                                                                    "st"
                                                                                                                    "Pl"
                                                                                                                    "ay"
                                                                                                                    "ed"
                                                                                                                    "\""
                                                                                                                    ": "
                                                                                                                    "\""
                                                                                                                    "20"
                                                                                                                    "26"
                                                                                                                    "-1"
                                                                                                                    "0-"
                                                                                                                    "10"
                                                                                                                    "T1"
                                                                                                                    "9:"
                                                                                                                    "04"
                                                                                                                    ":0"
                                                                                                                    "1."
                                                                                                                    "09"
                                                                                                                    "2Z"
                                                                                                                    "\""
                                                                                                                    "\r"
                                                                                                                    "\n")
                                                                                                                    TEXT(
                                                                                                                        "}\n");

    // A JSON document with one change.
    FString EditClockJson(const FString& Base, TFunctionRef<void(FJsonObject&)> Edit)
    {
        TSharedPtr<FJsonObject> Object;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Base), Object);
        Edit(*Object);
        FString Json;
        FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
        return Json;
    }

    // world.json with this "clocks" key: a JSON value in text, or none.
    FString WithClocks(const FString& Base, const TCHAR* Clocks)
    {
        // Wrapped in an object, since the reader takes only an object or an array at the top.
        TSharedPtr<FJsonObject> Wrapper;
        FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(FString(TEXT("{\"v\":")) + Clocks + TEXT("}")), Wrapper);
        const TSharedPtr<FJsonValue> Value = Wrapper ? Wrapper->TryGetField(TEXT("v")) : nullptr;
        return EditClockJson(Base, [&Value](FJsonObject& File) { File.SetField(TEXT("clocks"), Value); });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelClockSavedWorldTest, "Unnamed.Voxel.Clock.SavedWorld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelClockSavedWorldTest::RunTest(const FString&)
{
    // world.json's optional "clocks" key at version 1 (Jesus's decision, the plan's Q2 (a)).
    FUnnamedSavedWorld Old, Loaded;
    FString Error;
    TestTrue(TEXT("A file from before the clock loads"), UnnamedSavedWorlds::FromJson(PreClockWorldJson, Old, Error));
    TestFalse(TEXT("...with no clock: a new world's tick 0"), Old.Clock.IsSet());
    TestEqual(TEXT("...and writes back byte for byte: an unplayed world's file is unchanged"),
        UnnamedSavedWorlds::ToJson(Old), FString(PreClockWorldJson));

    FUnnamedSavedWorld Played = Old;
    Played.Clock = FUnnamedSavedWorld::FClock{13219, 3.f, true};
    const FString Json = UnnamedSavedWorlds::ToJson(Played);
    TSharedPtr<FJsonObject> File;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), File);
    const TSharedPtr<FJsonObject>* Clocks = nullptr;
    const TSharedPtr<FJsonObject>* Overworld = nullptr;
    TestTrue(TEXT("A played world writes clocks.minecraft:overworld"),
        File && File->TryGetObjectField(TEXT("clocks"), Clocks) &&
            (*Clocks)->TryGetObjectField(TEXT("minecraft:overworld"), Overworld));
    if (Overworld)
        TestTrue(TEXT("...with vanilla's names and values"),
            (*Overworld)->GetNumberField(TEXT("total_ticks")) == 13219 && (*Overworld)->GetBoolField(TEXT("paused")) &&
                (*Overworld)->GetNumberField(TEXT("rate")) == 3. && (*Overworld)->Values.Num() == 3);
    TestEqual(TEXT("...still at version 1"), File ? int32(File->GetNumberField(TEXT("version"))) : 0, 1);
    TestTrue(TEXT("It reads back"), UnnamedSavedWorlds::FromJson(Json, Loaded, Error));
    TestTrue(TEXT("...with the same clock"),
        Loaded.Clock.IsSet() && Loaded.Clock->TotalTicks == 13219 && Loaded.Clock->Rate == 3.f &&
            Loaded.Clock->bPaused);
    TestEqual(TEXT("...and writes the same file"), UnnamedSavedWorlds::ToJson(Loaded), Json);

    // Defaults are left out, as vanilla's world_clocks.dat; exact values round-trip.
    struct FClocked
    {
        FUnnamedSavedWorld::FClock Clock;
        // The overworld's fields as condensed JSON, when pinned.
        const TCHAR* Expected;
    };
    const FClocked Clocked[] = {
        {{0, 1.f, false}, TEXT("{}")},
        {{20, 1.f, false}, TEXT("{\"total_ticks\":20}")},
        {{FVoxelWorldClock::MaxTotalTicks, .1f, false}, nullptr},
        {{5, 1e-7f, false}, nullptr},
        {{7, 1000.f, true}, nullptr},
    };
    for (const auto& [Clock, Expected] : Clocked)
    {
        FUnnamedSavedWorld World = Old;
        World.Clock = Clock;
        const FString Text = UnnamedSavedWorlds::ToJson(World);
        const FString What = FString::Printf(TEXT("%lld ticks at rate %g"), Clock.TotalTicks, Clock.Rate);
        if (Expected)
        {
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), File);
            FString Written;
            FJsonSerializer::Serialize(
                File->GetObjectField(TEXT("clocks"))->GetObjectField(TEXT("minecraft:overworld")).ToSharedRef(),
                TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Written));
            TestEqual(What + TEXT(" writes only what differs"), Written, FString(Expected));
        }
        if (Clock.TotalTicks == FVoxelWorldClock::MaxTotalTicks)
            TestTrue(What + TEXT(" is written as its exact digits"),
                Text.Contains(TEXT("\"total_ticks\": 9007199254740991,")) && Text.Contains(TEXT("\"rate\": 0.1")));
        TestTrue(What + TEXT(" round-trips"),
            UnnamedSavedWorlds::FromJson(Text, Loaded, Error) && Loaded.Clock.IsSet() &&
                Loaded.Clock->TotalTicks == Clock.TotalTicks && Loaded.Clock->Rate == Clock.Rate &&
                Loaded.Clock->bPaused == Clock.bPaused);
    }

    // Other clocks and fields are ignored; a newer version is refused, clock or not; a broken clock is refused.
    TestTrue(TEXT("The End's clock and an unknown field are ignored"),
        UnnamedSavedWorlds::FromJson(WithClocks(Json,
                                         TEXT("{\"minecraft:the_end\":{\"total_ticks\":20},"
                                              "\"minecraft:overworld\":{\"total_ticks\":5,\"spin\":1}}")),
            Loaded, Error) &&
            Loaded.Clock.IsSet() && Loaded.Clock->TotalTicks == 5 && Loaded.Clock->Rate == 1.f);
    TestTrue(TEXT("No overworld clock: none"),
        UnnamedSavedWorlds::FromJson(WithClocks(Json, TEXT("{\"minecraft:the_end\":{}}")), Loaded, Error) &&
            !Loaded.Clock.IsSet());
    TestFalse(TEXT("Refused: a newer version with a clock"),
        UnnamedSavedWorlds::FromJson(
            EditClockJson(Json, [](FJsonObject& F) { F.SetNumberField(TEXT("version"), 2); }), Loaded, Error));
    for (const TCHAR* Bad : {TEXT("[]"), TEXT("5"), TEXT("{\"minecraft:overworld\":5}"),
             TEXT("{\"minecraft:overworld\":{\"total_ticks\":-1}}"),
             TEXT("{\"minecraft:overworld\":{\"total_ticks\":1.5}}"),
             TEXT("{\"minecraft:overworld\":{\"total_ticks\":\"20\"}}"),
             TEXT("{\"minecraft:overworld\":{\"total_ticks\":9007199254740992}}"),
             TEXT("{\"minecraft:overworld\":{\"rate\":0}}"), TEXT("{\"minecraft:overworld\":{\"rate\":1001}}"),
             TEXT("{\"minecraft:overworld\":{\"paused\":1}}"), TEXT("{\"minecraft:overworld\":{\"rate\":1e-50}}")})
        TestFalse(FString(TEXT("Refused: clocks ")) + Bad,
            UnnamedSavedWorlds::FromJson(WithClocks(Json, Bad), Loaded, Error));

    // Through the store: saving keeps the clock, and renaming and touching keep it too.
    const FString Root =
        FPaths::ProjectSavedDir() / TEXT("Automation") / (TEXT("ClockWorlds-") + FGuid::NewGuid().ToString());
    UnnamedSavedWorlds::SetRootForTesting(Root);
    ON_SCOPE_EXIT
    {
        UnnamedSavedWorlds::SetRootForTesting(TEXT(""));
        IFileManager::Get().DeleteDirectory(*Root, false, true);
    };
    Played.Id = TEXT("clock-world");
    TestTrue(TEXT("A world with a clock saves"), UnnamedSavedWorlds::Save(Played, Error));
    TestTrue(TEXT("...and renames"), UnnamedSavedWorlds::Rename(Played.Id, TEXT("Clock World"), Error));
    TestTrue(TEXT("...and is touched"), UnnamedSavedWorlds::Touch(Played.Id, Error));
    TestTrue(TEXT("...keeping its clock"),
        UnnamedSavedWorlds::Load(Played.Id, Loaded, Error) && Loaded.Clock.IsSet() &&
            Loaded.Clock->TotalTicks == 13219 && Loaded.Name == TEXT("Clock World"));
    return true;
}
#endif
