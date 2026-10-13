#pragma once

#include "CoreMinimal.h"

class FVoxelWorldClock;

// The world's environment at one moment (atmosphere step 1, C:\dev\sandbox\atmosphere-review\plan.md): the day clock
// and the sun and moon it puts in the sky, read from the world's one clock (FVoxelWorldClock), never a second one.
// A plain value: Evaluate reads the clock and changes nothing, so any number of readers may ask. It is the presentation
// side's input for now (voxel.Atmosphere.Cycle moves the sun with it); weather, local sky light and saved simulation
// age come in later steps, and gameplay rules don't read it yet.
struct FVoxelEnvironment
{
    // The day clock as the world keeps it: the total, the day count from 0, the tick within the day (0 to 23,999), the
    // rate, paused, and the saved world's advance_time rule (off stops the day clock).
    int64 TotalTicks = 0;
    int64 DayNumber = 0;
    int32 TimeOfDay = 0;
    float Rate = 1.f;
    bool bPaused = false;
    bool bAdvancing = true;
    // The day clock between game ticks (FVoxelWorldClock::GetSmoothTicks), which the sky below is sampled at.
    double SmoothTicks = 0;
    // 0 full moon to 7 waxing gibbous (FVoxelWorldClock::GetMoonPhase).
    int32 MoonPhase = 0;
    // As VoxelSky::FAngles, in degrees: 0 straight up, 90 the western horizon; the moon's is the sun's plus 180.
    float SunAngle = 0.f;
    float MoonAngle = 0.f;
    // Towards the sun and the moon in the voxel axes (X east, Y south, Z up).
    FVector3f SunDirection = FVector3f::ZeroVector;
    FVector3f MoonDirection = FVector3f::ZeroVector;
    // The sun's height above the horizon, -90 to 90 degrees.
    float SunElevation = 0.f;
    // The sun light's share: 0 with the sun below the horizon, easing to 1 at DaylightFullElevation.
    float Daylight = 0.f;
};

namespace VoxelEnvironment
{
    inline constexpr float DaylightFullElevation = 6.f;

    // The sun angle at a day clock time, 0 to 360 degrees: the classic eased day orbit as a formula, so a packaged game
    // needs no Reference/ files. It stays within 0.06 degrees of 26.3's (the reference's minecraft:day sun_angle
    // track; Unnamed.Voxel.Atmosphere.Orbit).
    UNNAMEDVOXEL_API float SunAngle(double Ticks);
    // A directional light's rotation for a body in that direction: it shines from the body, so along the opposite.
    UNNAMEDVOXEL_API FRotator LightRotation(const FVector3f& TowardsBody);
    // The sun light's share at a sun elevation in degrees (FVoxelEnvironment::Daylight).
    UNNAMEDVOXEL_API float DaylightAt(float SunElevation);
    UNNAMEDVOXEL_API FVoxelEnvironment Evaluate(const FVoxelWorldClock& Clock, bool bAdvanceTime);
    // voxel.Atmosphere.Info's lines.
    UNNAMEDVOXEL_API FString Describe(const FVoxelEnvironment& Environment);
}
