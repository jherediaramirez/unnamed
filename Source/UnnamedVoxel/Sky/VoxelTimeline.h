#pragma once

#include "CoreMinimal.h"

// 26.3's timelines (water and sky DN0): the day's sun, moon and stars, sky and fog colours and sky light, and the
// moon's phases, as keyframed tracks over a period. Read from Reference/minecraft-sky-26.3 by the game module
// (VoxelSkyReference), since the core has no JSON reader. The reading is the reference README's "Reading a timeline":
// sorted keyframes (file order kept for equal ticks), wrapping around the period, eased, lerped, then a modifier
// applied to the attribute's base value. Plain data: any thread may sample it.

enum class EVoxelTimelineEase : uint8
{
    Linear,
    Constant,
    // A CSS-style curve from (0, 0) to (1, 1) through (x1, y1) and (x2, y2).
    CubicBezier,
};

// How a track's value applies to the attribute's value beneath it (the dimension's, else the attribute's default).
enum class EVoxelTimelineModifier : uint8
{
    Override,
    Multiply,
    Maximum,
    Or,
    And,
};

// Numbers and colours lerp; booleans and names (the moon phases) step like Constant.
enum class EVoxelTimelineValue : uint8
{
    Float,
    Color,
    Bool,
    Name,
};

// A keyframe's value: a float in X; a colour as (R, G, B, A), each the file's byte over 255 (alpha 1 for #rrggbb);
// a boolean as 0 or 1 in X; a name as its index in the track's Names, in X.
struct FVoxelTimelineKey
{
    int32 Ticks = 0;
    FVector4f Value = FVector4f(0.f);
};

struct UNNAMEDVOXEL_API FVoxelTimelineTrack
{
    // The environment attribute it sets, such as "minecraft:visual/sun_angle".
    FString Attribute;
    EVoxelTimelineValue Type = EVoxelTimelineValue::Float;
    EVoxelTimelineEase Ease = EVoxelTimelineEase::Linear;
    // x1, y1, x2, y2 of a CubicBezier ease.
    FVector4f Bezier = FVector4f(0.f);
    EVoxelTimelineModifier Modifier = EVoxelTimelineModifier::Override;
    // Sorted by tick, file order kept for equal ticks (SortKeys).
    TArray<FVoxelTimelineKey> Keys;
    // A Name track's names, in the order the file first uses them (keys index it; compared case-sensitively).
    TArray<FString> Names;

    void SortKeys();
    // The value at Ticks (any value: it wraps around the period). Between the last key at or before the tick and the
    // first key after it, a period earlier or later where none is; the progress eased, the value lerped. With no
    // period (0) the track doesn't repeat: before its first key it holds the first value, after its last the last.
    FVector4f Sample(double Ticks, int32 PeriodTicks) const;
};

struct FVoxelTimelineMarker
{
    FString Name;
    int32 Ticks = 0;
    bool bShowInCommands = false;
};

struct UNNAMEDVOXEL_API FVoxelTimeline
{
    // The timeline's id, such as "minecraft:day", and the clock it reads ("minecraft:overworld").
    FString Id;
    FString Clock;
    // 0 for a timeline that doesn't repeat (early_game: period_ticks left out).
    int32 PeriodTicks = 0;
    TArray<FVoxelTimelineTrack> Tracks;
    TArray<FVoxelTimelineMarker> Markers;

    const FVoxelTimelineTrack* FindTrack(const FString& Attribute) const;
    const FVoxelTimelineMarker* FindMarker(const FString& Name) const;
    // The track's raw value at Ticks (no modifier); false if the timeline has no such track.
    bool Sample(const FString& Attribute, double Ticks, FVector4f& OutValue) const;
    // The track applied to Base with its modifier; Base itself if the timeline has no such track.
    FVector4f Apply(const FString& Attribute, double Ticks, const FVector4f& Base) const;
    // No negative period, keys from 0 (and within the period, if any) and sorted, names in range.
    bool Validate(FString& OutError) const;
};

// Sky helpers on top of the timelines.
namespace VoxelSky
{
    UNNAMEDVOXEL_API FVector4f ApplyModifier(
        EVoxelTimelineModifier Modifier, const FVector4f& Base, const FVector4f& Value);
    // The CSS-style cubic Bezier ease at progress U (0..1): solves x(s) = U for s and returns y(s).
    UNNAMEDVOXEL_API double CubicBezier(const FVector4f& Points, double U);
    // "#rrggbb" or "#aarrggbb" as (R, G, B, A) bytes over 255; false for anything else.
    UNNAMEDVOXEL_API bool ParseColor(const FString& Text, FVector4f& OutColor);
    // A colour as "#rrggbb" (rounded bytes), or "#aarrggbb" when bWithAlpha.
    UNNAMEDVOXEL_API FString ColorToHex(const FVector4f& Color, bool bWithAlpha = false);

    // The light at an open-sky block from gameplay/sky_light_level: the ceiling of the level (scout N 5 (b): 5.037
    // gives 6, 4.0 gives 4), 0 to 15.
    UNNAMEDVOXEL_API int32 OpenSkyLight(float Level);

    // The sun, moon and star angles in degrees at Ticks of the day timeline (0 straight up, 90 the western horizon;
    // the moon's runs 180 ahead and past 360), and the directions towards the sun and the moon in the voxel axes
    // (X east, Y south, Z up): (-sin a, 0, cos a).
    struct FAngles
    {
        float Sun = 0.f;
        float Moon = 0.f;
        float Stars = 0.f;
        FVector3f SunDirection = FVector3f::ZeroVector;
        FVector3f MoonDirection = FVector3f::ZeroVector;
    };
    UNNAMEDVOXEL_API FAngles SampleAngles(const FVoxelTimeline& Day, double Ticks);
    UNNAMEDVOXEL_API FVector3f DirectionFromAngle(float Degrees);

    // The moon phase at a total tick count (the moon timeline's period is 8 days): 0 full moon, as
    // FVoxelWorldClock::GetMoonPhase; INDEX_NONE if the timeline has no moon_phase track or names an unknown phase.
    UNNAMEDVOXEL_API int32 SampleMoonPhase(const FVoxelTimeline& Moon, double TotalTicks);

    // The attributes the sky reads.
    inline const TCHAR* const SunAngle = TEXT("minecraft:visual/sun_angle");
    inline const TCHAR* const MoonAngle = TEXT("minecraft:visual/moon_angle");
    inline const TCHAR* const StarAngle = TEXT("minecraft:visual/star_angle");
    inline const TCHAR* const StarBrightness = TEXT("minecraft:visual/star_brightness");
    inline const TCHAR* const SkyColor = TEXT("minecraft:visual/sky_color");
    inline const TCHAR* const FogColor = TEXT("minecraft:visual/fog_color");
    inline const TCHAR* const SkyLightColor = TEXT("minecraft:visual/sky_light_color");
    inline const TCHAR* const SkyLightFactor = TEXT("minecraft:visual/sky_light_factor");
    inline const TCHAR* const SunriseSunsetColor = TEXT("minecraft:visual/sunrise_sunset_color");
    inline const TCHAR* const MoonPhase = TEXT("minecraft:visual/moon_phase");
    inline const TCHAR* const SkyLightLevel = TEXT("minecraft:gameplay/sky_light_level");
    inline const TCHAR* const MonstersBurn = TEXT("minecraft:gameplay/monsters_burn");
}
