#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelLook.h"

struct FVoxelEnvironment;

// The atmosphere's night (plan step 5), with voxel.Atmosphere.Cycle 1 and voxel.Atmosphere.Night 1: the moon lights the
// world through the same light as the sun (ApplyCycle), the sky and haze darken, and open-sky views expose for the
// night, so it is dark but never black and torches matter. Caves keep their exposure: only the open-sky pin's part
// moves. Composed in ApplyLook, the one writer of the exposure and sky.
namespace VoxelAtmosphere
{
    // The night look now; all zero (no change from the day look) with the cycle or the night off.
    struct FNight
    {
        // FVoxelEnvironment::Night, in steps of 1/64 so twilight applies a step at a time.
        float Night = 0.f;
        // voxel.Atmosphere.NightStops: how many f-stops brighter open-sky views expose at full night.
        float ExposureStops = 0.f;
        // voxel.Atmosphere.NightSky: x the sky's and haze's luminance at full night.
        float SkyScale = 1.f;

        bool operator==(const FNight& Other) const = default;
    };

    UNNAMED_API bool IsCycleOn();
    UNNAMED_API FNight ReadNight(const FVoxelEnvironment& Environment);
    // The day look's targets at night: the open-sky exposure range (both ends) moves down by ExposureStops x Night x
    // Pin, and the sky and haze luminance by SkyScale's share. Pin 0 (a cave, or SkyPin 0) keeps today's exposure.
    UNNAMED_API void ComposeNight(VoxelLook::FTargets& Targets, const FNight& Night, double Pin);

    // The moon light's intensity in lux: voxel.Atmosphere.MoonLux x its light's share, dimmer for a thinner moon down
    // to voxel.Atmosphere.NewMoonLight at new moon.
    UNNAMED_API float MoonLux(const FVoxelEnvironment& Environment, float FullLux, float NewMoonShare);
}
