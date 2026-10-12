#include "Voxel/VoxelEdgeFog.h"

#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"
#include "VoxelConstants.h"

namespace
{
    TAutoConsoleVariable<float> CVarFogHazeStart(TEXT("voxel.Fog.HazeStart"), 48.f,
        TEXT("Far LOD step 2: the haze's clear air near the camera, in blocks. Far enough that caves stay clear."));
    TAutoConsoleVariable<float> CVarFogHazeDensity(TEXT("voxel.Fog.HazeDensity"), 0.004f,
        TEXT("Far LOD step 2: the haze's height fog density at sea level, per 10 blocks as the engine counts it."));
    TAutoConsoleVariable<float> CVarFogHazeFalloff(TEXT("voxel.Fog.HazeFalloff"), 0.2f,
        TEXT("Far LOD step 2: the haze's height falloff (0-2): its density halves every 10 / falloff blocks up, so "
             "valleys are hazier than peaks."));
    TAutoConsoleVariable<FString> CVarFogHazeSky(TEXT("voxel.Fog.HazeSky"), TEXT("6 4.2 2.6"),
        TEXT("Far LOD step 2: the haze's colour as a multiple of the sky's ambient light, \"r g b\" or one value. The "
             "default matches the horizon just above the fogged floor."));
    TAutoConsoleVariable<float> CVarFogAerialScale(TEXT("voxel.Fog.AerialScale"), 4.f,
        TEXT("Far LOD step 2: the sky atmosphere's aerial perspective distance scale with the haze (1 is physical)."));
    TAutoConsoleVariable<float> CVarFogFloorDepth(TEXT("voxel.Fog.FloorDepth"), 2000.f,
        TEXT("Far LOD step 2: the fog floor's depth under the world's bottom with the haze, in blocks; below 0 hides "
             "it (look checks)."));

    FLinearColor ParseSky(const FString& Text)
    {
        TArray<FString> Words;
        Text.ParseIntoArray(Words, TEXT(" "));
        float Channels[3] = {1.f, 1.f, 1.f};
        for (int32 Index = 0; Index < 3; ++Index)
            if (Words.Num())
                Channels[Index] = FMath::Max(0.f, FCString::Atof(*Words[FMath::Min(Index, Words.Num() - 1)]));
        return FLinearColor(Channels[0], Channels[1], Channels[2]);
    }
}

VoxelEdgeFog::FFade VoxelEdgeFog::GetFarFade(int32 DrawnRadius)
{
    constexpr double ColumnCm = VoxelConstants::ColumnSize * UFoundationProfile::BlockCm;
    const double EndCm = FMath::Max(0, DrawnRadius) * ColumnCm;
    return {FMath::Max(0., EndCm - FadeColumns * ColumnCm), EndCm};
}

VoxelEdgeFog::FHaze VoxelEdgeFog::ReadHaze()
{
    FHaze Haze;
    Haze.StartBlocks = FMath::Max(0.f, CVarFogHazeStart.GetValueOnGameThread());
    Haze.Density = FMath::Max(0.f, CVarFogHazeDensity.GetValueOnGameThread());
    Haze.Falloff = FMath::Clamp(CVarFogHazeFalloff.GetValueOnGameThread(), 0.f, 2.f);
    // Read every frame: the colour is parsed again only when the text changes.
    static FString SkyText;
    static FLinearColor Sky = ParseSky(SkyText);
    if (const FString& Text = CVarFogHazeSky.GetValueOnGameThread(); Text != SkyText)
    {
        SkyText = Text;
        Sky = ParseSky(SkyText);
    }
    Haze.Sky = Sky;
    Haze.AerialScale = FMath::Max(0.f, CVarFogAerialScale.GetValueOnGameThread());
    Haze.FloorDepthBlocks = FMath::Max(-1.f, CVarFogFloorDepth.GetValueOnGameThread());
    return Haze;
}
