#include "Voxel/VoxelAtmosphere.h"
#include "Components/DirectionalLightComponent.h"
#include "HAL/IConsoleManager.h"
#include "Sky/VoxelEnvironment.h"
#include "Voxel/VoxelWorld.h"
#include "VoxelLog.h"

// The atmosphere (C:\dev\sandbox\atmosphere-review\plan.md): the environment snapshot and its dump; step 1's sun
// following the clock behind voxel.Atmosphere.Cycle (off by default: the map's fixed daylight); step 5's night.
namespace
{
    TAutoConsoleVariable<int32> CVarAtmosphereCycle(TEXT("voxel.Atmosphere.Cycle"), 0,
        TEXT("Atmosphere: 1 the sun follows the world clock (26.3's orbit; its light fades out below the horizon); 0 "
             "the map's fixed daylight sun, exactly as before."));
    TAutoConsoleVariable<int32> CVarAtmosphereNight(TEXT("voxel.Atmosphere.Night"), 1,
        TEXT(
            "Atmosphere, with Cycle 1: 1 the night (the moon's light, a darker sky, open sky exposed for the night); 0 "
            "no light below the horizon, the day's sky and exposure (black nights)."));
    TAutoConsoleVariable<float> CVarAtmosphereMoonLux(TEXT("voxel.Atmosphere.MoonLux"), 300.f,
        TEXT("Atmosphere: the full moon's light high in the sky, in lux (the sun's is the map's 75,000)."));
    TAutoConsoleVariable<float> CVarAtmosphereNewMoonLight(TEXT("voxel.Atmosphere.NewMoonLight"), .35f,
        TEXT("Atmosphere: the new moon's light as a share of the full moon's; the phases between ease."));
    TAutoConsoleVariable<float> CVarAtmosphereNightStops(TEXT("voxel.Atmosphere.NightStops"), 4.f,
        TEXT("Atmosphere: how many f-stops brighter views under open sky expose at full night (caves keep theirs)."));
    TAutoConsoleVariable<float> CVarAtmosphereNightSky(TEXT("voxel.Atmosphere.NightSky"), .25f,
        TEXT("Atmosphere: x the sky's and the haze's luminance at full night."));
    TAutoConsoleVariable<int32> CVarAtmosphereTwilight(TEXT("voxel.Atmosphere.Twilight"), 0,
        TEXT("Atmosphere, with Cycle 1 and Night 1: a temporary cool open-sky terrain floor and navy sky bridge."));
    TAutoConsoleVariable<float> CVarAtmosphereTwilightFloor(TEXT("voxel.Atmosphere.TwilightFloor"), 1.5f,
        TEXT("Atmosphere: maximum temporary terrain floor (0 to 8); caves keep their existing floor."));
    TAutoConsoleVariable<float> CVarAtmosphereTwilightSky(TEXT("voxel.Atmosphere.TwilightSky"), .2f,
        TEXT("Atmosphere: exposure-compensated navy backdrop through the light handover (0 to 1)."));

    // The moon's light: cool and faint (linear).
    const FLinearColor MoonColor(.62f, .72f, 1.f);

    FAutoConsoleCommandWithWorldArgsAndOutputDevice AtmosphereInfoCommand(TEXT("voxel.Atmosphere.Info"),
        TEXT(
            "voxel.Atmosphere.Info: the voxel world's environment now (day clock, moon phase, sun and moon, night) and "
            "whether voxel.Atmosphere.Cycle drives the sun."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>&, UWorld* World, FOutputDevice& Output)
            {
                const AVoxelWorld* Voxels = AVoxelWorld::Find(World);
                FString Reply = TEXT("voxel.Atmosphere.Info: no voxel world is running.");
                if (Voxels)
                {
                    const FVoxelEnvironment Environment = Voxels->GetEnvironment();
                    const VoxelAtmosphere::FNight Night = VoxelAtmosphere::ReadNight(Environment);
                    Reply = VoxelEnvironment::Describe(Environment) +
                        FString::Printf(TEXT("\nCycle %d, night %d (applied night %.3f, %.2f stops, sky x%.2f); "
                                             "twilight %d (floor %.3f, sky %.4f)"),
                            CVarAtmosphereCycle.GetValueOnGameThread(), CVarAtmosphereNight.GetValueOnGameThread(),
                            Night.Night, Night.ExposureStops, Night.SkyScale,
                            CVarAtmosphereTwilight.GetValueOnGameThread(), Night.TwilightFloor, Night.TwilightSky);
                }
                TArray<FString> Lines;
                Reply.ParseIntoArrayLines(Lines);
                for (const FString& Line : Lines)
                    Output.Log(Line);
            }));
}

bool VoxelAtmosphere::IsCycleOn()
{
    return CVarAtmosphereCycle.GetValueOnGameThread() == 1;
}

VoxelAtmosphere::FNight VoxelAtmosphere::ReadNight(const FVoxelEnvironment& Environment)
{
    FNight Out;
    if (!IsCycleOn() || CVarAtmosphereNight.GetValueOnGameThread() != 1) return Out;
    Out.Night = FMath::RoundToFloat(Environment.Night * 64.f) / 64.f;
    Out.ExposureStops = FMath::Max(0.f, CVarAtmosphereNightStops.GetValueOnGameThread());
    if (CVarAtmosphereTwilight.GetValueOnGameThread() == 1)
    {
        const float Bridge = FMath::RoundToFloat(TwilightWeight(Environment) * 64.f) / 64.f;
        const float SkyBridge = FMath::RoundToFloat(TwilightSkyWeight(Environment) * 64.f) / 64.f;
        Out.TwilightFloor = Bridge * FMath::Clamp(CVarAtmosphereTwilightFloor.GetValueOnGameThread(), 0.f, 8.f);
        Out.TwilightSky = SkyBridge * FMath::Clamp(CVarAtmosphereTwilightSky.GetValueOnGameThread(), 0.f, 1.f);
    }
    Out.SkyScale = FMath::Max(0.f, CVarAtmosphereNightSky.GetValueOnGameThread());
    return Out;
}

float VoxelAtmosphere::TwilightWeight(const FVoxelEnvironment& Environment)
{
    // The sun keeps its atmospheric glow. A faint minimum covers the unlit handover, then yields to the moon.
    // Exposure alone cannot brighten this interval: both directional sources reach zero at the switch.
    return FMath::Clamp(Environment.Night, 0.f, 1.f) * (1.f - FMath::Clamp(Environment.Moonlight, 0.f, 1.f));
}

float VoxelAtmosphere::TwilightSkyWeight(const FVoxelEnvironment& Environment)
{
    // Full directional intensity arrives while the moon is still low and the atmospheric sky remains dark.
    // Ease the navy addition over the moon's sine altitude instead, reaching the unchanged sky at midnight.
    const float Height = FMath::Clamp(float(Environment.MoonDirection.Z), 0.f, 1.f);
    return FMath::Clamp(Environment.Night, 0.f, 1.f) * (1.f - FMath::SmoothStep(0.f, 1.f, Height));
}

void VoxelAtmosphere::ComposeNight(VoxelLook::FTargets& Targets, const FNight& Night, double Pin)
{
    const double Shift = Night.ExposureStops * Night.Night * FMath::Clamp(Pin, 0., 1.);
    Targets.Exposure.MinEV -= Shift;
    Targets.Exposure.MaxEV -= Shift;
    const float Sky = FMath::Lerp(1.f, Night.SkyScale, Night.Night);
    Targets.SkyLuminance *= Sky;
    Targets.FogColorScale *= Sky;
    Targets.TwilightFloor = Night.TwilightFloor * float(FMath::Clamp(Pin, 0., 1.));
}

float VoxelAtmosphere::MoonLux(const FVoxelEnvironment& Environment, float FullLux, float NewMoonShare)
{
    return FullLux * Environment.Moonlight * FMath::Lerp(NewMoonShare, 1.f, Environment.MoonIllumination);
}

FVoxelEnvironment AVoxelWorld::GetEnvironment() const
{
    return VoxelEnvironment::Evaluate(Clock, bAdvanceTime);
}

void AVoxelWorld::ApplyCycle()
{
    if (!IsValid(SunLight)) return;
    if (!VoxelAtmosphere::IsCycleOn())
    {
        if (!AppliedSun) return;
        AppliedSun.Reset();
        SunLight->SetWorldRotation(MapSunRotation);
        SunLight->SetIntensity(MapSunIntensity);
        SunLight->SetLightColor(MapSunColor);
        UE_LOG(LogUnnamedVoxel, Log, TEXT("Atmosphere: cycle off, the map's sun back (%s, %.0f lux)"),
            *MapSunRotation.ToCompactString(), MapSunIntensity);
        return;
    }
    // One light: the sun's while it is up, then (with the night on) the moon's. The sun's fades out across twilight (6
    // degrees below) and the moon's only fades in from there, so the hand over doesn't jump. A paused clock gives the
    // same values every frame, and then the light (and its shadows) are left alone.
    const FVoxelEnvironment Environment = GetEnvironment();
    FAppliedSun Light{
        VoxelEnvironment::LightRotation(Environment.SunDirection), MapSunIntensity * Environment.Daylight, MapSunColor};
    if (Environment.Daylight == 0.f && CVarAtmosphereNight.GetValueOnGameThread() == 1)
    {
        Light.Rotation = VoxelEnvironment::LightRotation(Environment.MoonDirection);
        Light.Intensity =
            VoxelAtmosphere::MoonLux(Environment, FMath::Max(0.f, CVarAtmosphereMoonLux.GetValueOnGameThread()),
                FMath::Clamp(CVarAtmosphereNewMoonLight.GetValueOnGameThread(), 0.f, 1.f));
        Light.Color = MoonColor;
    }
    if (AppliedSun && *AppliedSun == Light) return;
    if (!AppliedSun)
        UE_LOG(LogUnnamedVoxel, Log, TEXT("Atmosphere: cycle on at time %d (sun elevation %.2f, daylight %.3f)"),
            Environment.TimeOfDay, Environment.SunElevation, Environment.Daylight);
    AppliedSun = Light;
    SunLight->SetWorldRotation(Light.Rotation);
    SunLight->SetIntensity(Light.Intensity);
    SunLight->SetLightColor(Light.Color);
}
