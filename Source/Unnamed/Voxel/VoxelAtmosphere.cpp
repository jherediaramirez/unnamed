#include "Components/DirectionalLightComponent.h"
#include "HAL/IConsoleManager.h"
#include "Sky/VoxelEnvironment.h"
#include "Voxel/VoxelWorld.h"
#include "VoxelLog.h"

// Atmosphere step 1 (C:\dev\sandbox\atmosphere-review\plan.md): the environment snapshot, its dump, and the first thing
// that reads it, the sun following the clock behind voxel.Atmosphere.Cycle (off by default: the map's fixed daylight).
namespace
{
    TAutoConsoleVariable<int32> CVarAtmosphereCycle(TEXT("voxel.Atmosphere.Cycle"), 0,
        TEXT("Atmosphere: 1 the sun follows the world clock (26.3's orbit; its light fades out below the horizon); 0 "
             "the map's fixed daylight sun, exactly as before."));

    FAutoConsoleCommandWithWorldArgsAndOutputDevice AtmosphereInfoCommand(TEXT("voxel.Atmosphere.Info"),
        TEXT("voxel.Atmosphere.Info: the voxel world's environment now (day clock, moon phase, sun and moon) and "
             "whether voxel.Atmosphere.Cycle drives the sun."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>&, UWorld* World, FOutputDevice& Output)
            {
                const AVoxelWorld* Voxels = AVoxelWorld::Find(World);
                FString Reply = TEXT("voxel.Atmosphere.Info: no voxel world is running.");
                if (Voxels)
                    Reply = VoxelEnvironment::Describe(Voxels->GetEnvironment()) +
                        FString::Printf(TEXT("\nCycle %d"), CVarAtmosphereCycle.GetValueOnGameThread());
                TArray<FString> Lines;
                Reply.ParseIntoArrayLines(Lines);
                for (const FString& Line : Lines)
                    Output.Log(Line);
            }));
}

FVoxelEnvironment AVoxelWorld::GetEnvironment() const
{
    return VoxelEnvironment::Evaluate(Clock, bAdvanceTime);
}

void AVoxelWorld::ApplyCycle()
{
    if (!IsValid(SunLight)) return;
    if (CVarAtmosphereCycle.GetValueOnGameThread() != 1)
    {
        if (!AppliedSun) return;
        AppliedSun.Reset();
        SunLight->SetWorldRotation(MapSunRotation);
        SunLight->SetIntensity(MapSunIntensity);
        UE_LOG(LogUnnamedVoxel, Log, TEXT("Atmosphere: cycle off, the map's sun back (%s, %.0f lux)"),
            *MapSunRotation.ToCompactString(), MapSunIntensity);
        return;
    }
    // A paused clock gives the same values every frame, and then the light (and its shadows) are left alone.
    const FVoxelEnvironment Environment = GetEnvironment();
    const FRotator Rotation = VoxelEnvironment::LightRotation(Environment.SunDirection);
    const float Intensity = MapSunIntensity * Environment.Daylight;
    if (AppliedSun && AppliedSun->Key == Rotation && AppliedSun->Value == Intensity) return;
    if (!AppliedSun)
        UE_LOG(LogUnnamedVoxel, Log, TEXT("Atmosphere: cycle on at time %d (sun elevation %.2f, daylight %.3f)"),
            Environment.TimeOfDay, Environment.SunElevation, Environment.Daylight);
    AppliedSun.Emplace(Rotation, Intensity);
    SunLight->SetWorldRotation(Rotation);
    SunLight->SetIntensity(Intensity);
}
