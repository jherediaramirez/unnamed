#pragma once

#include "CoreMinimal.h"

// One clock's saved state, as 26.3's ClockState (total ticks, partial tick, rate, paused; scout N 4). world.json keeps
// the total, the rate and paused (the "clocks" key); the partial tick is not saved, as vanilla's world_clocks.dat
// leaves it out.
struct FVoxelDayClock
{
    int64 TotalTicks = 0;
    // The fraction of a day tick carried between game ticks when the rate is not whole.
    double PartialTick = 0;
    float Rate = 1;
    bool bPaused = false;
};

// The world's one 20 Hz clock (the water and sky plan, section 2; Q1 (a)), owned by AVoxelWorld. Each frame turns the
// world's seconds into whole game ticks, at most MaxCatchupTicks of them (the pawn's own cap): after a hitch the excess
// is dropped and counted. Two counters run on it, as in 26.3:
// - the game tick advances whenever the world runs; fluids (W2a) and later random ticks and mobs use it;
// - the day clock (the overworld's) advances by its rate per game tick, only while the advance_time rule is on and the
//   clock is not paused. advance_time false stops the sky, but water keeps flowing.
// A day is 24,000 ticks; the total keeps counting past it, and the day timeline is the total modulo a day.
class UNNAMEDVOXEL_API FVoxelWorldClock
{
public:
    static constexpr int32 TicksPerDay = 24000;
    static constexpr int32 MoonPhases = 8;
    // /time rate runs from above 0 to 1000 (wiki, Commands/time).
    static constexpr float MaxRate = 1000.f;
    // The largest total a command may reach: a JSON number holds integers exactly up to 2^53.
    static constexpr int64 MaxTotalTicks = (int64(1) << 53) - 1;

    explicit FVoxelWorldClock(double TickSeconds = .05, int32 MaxCatchupTicks = 5);
    // The tick length and the per-frame cap (UFoundationProfile's TickSeconds and MaxCatchupSteps).
    void Configure(double TickSeconds, int32 MaxCatchupTicks);

    // Takes a frame's due game ticks without running them; Step runs one. A caller with per-tick systems (fluids)
    // runs them after each Step.
    int32 TakeTicks(double Seconds);
    void Step(bool bAdvanceTime);
    // TakeTicks, then Step for each; returns the ticks run.
    int32 Advance(double Seconds, bool bAdvanceTime);

    uint64 GetGameTick() const { return GameTick; }
    // How far the frame is into the next game tick, 0 to 1.
    double GetTickAlpha() const { return Accumulator; }
    // Game ticks dropped by the per-frame cap, rounded.
    int64 GetDroppedTicks() const { return FMath::RoundToInt64(DroppedTicks); }
    const FVoxelDayClock& GetDayClock() const { return Day; }
    // A saved or pinned state; the total is clamped to 0..MaxTotalTicks and the rate to (0, MaxRate].
    void SetDayClock(const FVoxelDayClock& State);
    // The day timeline: the total modulo a day.
    int32 GetTimeOfDay() const { return int32(Day.TotalTicks % TicksPerDay); }
    // Vanilla's day count, from 0.
    int64 GetDayNumber() const { return Day.TotalTicks / TicksPerDay; }
    // 0 full moon, then waning gibbous, third quarter, waning crescent, new moon, waxing crescent, first quarter and
    // waxing gibbous: one per day, changing exactly at each multiple of 24,000 (scout N 5 (c)).
    int32 GetMoonPhase() const { return int32(GetDayNumber() % MoonPhases); }
    static const TCHAR* GetMoonPhaseName(int32 Phase);
    // The day clock between game ticks, for drawing: the total, the partial tick and, while it runs, the frame's
    // share of the next tick.
    double GetSmoothTicks(bool bAdvanceTime) const;

    // As /time: set a total, add to it (both clamped to MaxTotalTicks), move forward to the next tick of the day equal
    // to a marker (at or after now), pause, resume, and set the rate (clamped to (0, MaxRate]).
    void SetTotal(int64 Ticks);
    void AddTicks(int64 Ticks);
    void SetToMarker(int32 MarkerTicks);
    void SetPaused(bool bPaused) { Day.bPaused = bPaused; }
    void SetRate(float Rate);

    // The day markers: day 1000, noon 6000, night 13000 and midnight 18000 (as minecraft:day ...); false for others.
    static bool FindMarker(const FString& Name, int32& OutTicks);
    // A /time argument: a non-negative number with an optional unit, d (24,000 ticks), s (20) or t (1), rounded to
    // whole ticks; at most MAX_int32, as vanilla's integer argument.
    static bool ParseTime(const FString& Text, int64& OutTicks);
    // voxel.Time's words: set <time>|day|noon|night|midnight, add <time>, query time|day|gametime, pause, resume,
    // rate <rate>. Fills the reply in 26.3's own words (scout N's probes); false with the error or usage otherwise.
    bool RunCommand(TConstArrayView<FString> Args, FString& OutReply);

    // The F3 line: day number and time of day (with the hour, 0 at 06:00), moon phase, game tick, and the rate,
    // pause or stopped rule when they differ from a running clock.
    FString Describe(bool bAdvanceTime) const;

private:
    double TickSeconds = .05;
    int32 MaxCatchupTicks = 5;
    // Due game ticks not yet taken: below one after TakeTicks.
    double Accumulator = 0;
    double DroppedTicks = 0;
    uint64 GameTick = 0;
    FVoxelDayClock Day;
};
