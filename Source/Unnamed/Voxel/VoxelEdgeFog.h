#pragma once

#include "CoreMinimal.h"

// The view's edge (far LOD step 2, Docs/VoxelWorld.md "The edge"). While far meshes form it and voxel.Fog.Haze is on,
// the air is clear near the camera with a light haze over distance, denser low, coloured by the sky, and the far meshes
// fade into the sky behind them over the drawn radius's last columns (the M_ChunkFar* materials). Otherwise, as at
// voxel.FarRadius 0, step 1's fog wall closes the drawn radius.
namespace VoxelEdgeFog
{
    // Columns over which the far meshes fade out, ending at the drawn radius: 32 blocks at radius 24, near vanilla's
    // fog band (92% to 100% of the render distance, minecraft.wiki/w/Fog).
    constexpr int32 FadeColumns = 2;

    // The haze and the far fade apply: the switch is on and far meshes form the edge (FarRadius beyond ViewRadius).
    inline bool UsesHaze(bool bSwitch, int32 ViewRadius, int32 FarRadius)
    {
        return bSwitch && FarRadius > ViewRadius;
    }

    // Where the far meshes start and finish fading out, in centimetres of horizontal distance to the camera.
    struct FFade
    {
        double StartCm = 0;
        double EndCm = 0;
        bool operator==(const FFade& Other) const = default;
    };
    UNNAMED_API FFade GetFarFade(int32 DrawnRadius);
    // No fade: far meshes draw whole, as the materials' unset parameters (CreateBlockMaterials.py).
    inline FFade GetNoFade()
    {
        return {1e9, 2e9};
    }

    // The haze's settings, the voxel.Fog.* variables as read now.
    struct FHaze
    {
        // Clear air up to here, in blocks.
        float StartBlocks = 0;
        // The height fog's density at sea level (per 10 blocks, as the engine counts it).
        float Density = 0;
        // The height fog's falloff: the density halves every 10 / Falloff blocks up, doubles as often down.
        float Falloff = 0;
        // The sky's ambient light the haze scatters, per channel (SkyAtmosphereAmbientContributionColorScale).
        FLinearColor Sky = FLinearColor::White;
        // The sky atmosphere's aerial perspective distance scale.
        float AerialScale = 1;
        // The fog floor's depth under the world's bottom, in blocks: deep enough that the haze covers it fully. Below 0
        // the floor is hidden (look checks: the sky atmosphere's planet shows below the horizon).
        float FloorDepthBlocks = 0;

        bool operator==(const FHaze& Other) const = default;
    };
    UNNAMED_API FHaze ReadHaze();
}
