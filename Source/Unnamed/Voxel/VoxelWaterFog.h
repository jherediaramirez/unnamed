#pragma once

#include "CoreMinimal.h"

class APostProcessVolume;
class UMaterialInstanceDynamic;
class FVoxelBlockTable;
struct FVoxelBiome;
enum class EVoxelFluid : uint8;

// The fog while the camera's eye is in a fluid (Docs/Records/2026-10-12-WaterFog.md). In water: linear fog by distance
// to the camera, from WaterStartBlocks to the biome's end, toward the biome's water_fog_color. In lava: lava's fog, the
// view closing within a block. The post-process material M_WaterFog draws it (CreateBlockMaterials.py); above the
// fluid, and at voxel.Look.WaterFog 0, it is off and the view is as before.
namespace VoxelWaterFog
{
    // The 26.3 environment attributes' defaults, minecraft:visual/water_fog_* (minecraft.wiki/w/Environment_attribute,
    // Reference/minecraft-sky-26.3/attributes.json): colour #050533, start -8 and end 96 blocks.
    constexpr int32 WaterColor = 0x050533;
    constexpr float WaterStartBlocks = -8;
    constexpr float WaterEndBlocks = 96;
    // Vanilla draws the water fog colour brightened, not as is: in the wiki's 1.18 deep warm ocean frame (noon, open
    // water) the full fog is #041f33 x 4.73 on every channel, its brightest channel at 241. The attribute is "also
    // affected by time of day, weather, and potion effects" (minecraft.wiki/w/Environment_attribute), how is not
    // published; a dark cave frame shows about x2.2. So, with voxel.Look.WaterFogBright 1, the colour is scaled until
    // its brightest channel reaches this (noon in open water; darker times and places are unverified and not done).
    constexpr float WaterBrightest = 241.f / 255.f;
    // Lava's fog: colour #991A00, sight about 1 block (minecraft.wiki/w/Fog). The start is unverified: 0.
    constexpr int32 LavaColor = 0x991A00;
    constexpr float LavaStartBlocks = 0;
    constexpr float LavaEndBlocks = 1;

    // M_WaterFog's parameters: the colour as display values (sRGB / 255, mixed after the tonemapper), distances in cm.
    struct FFog
    {
        FLinearColor Color = FLinearColor::Black;
        float StartCm = 0;
        float EndCm = 0;
        bool operator==(const FFog& Other) const = default;
    };

    // The fluid the eye is in, from the states of its cell and the cell above and its height in the cell (0 bottom, 1
    // top): the cell's fluid (water held by kelp or waterlogged states included) under its surface, which is the cell's
    // top when fluid lies above, else None. As AVoxelWorld::GetWaterBox, but telling water from lava.
    UNNAMED_API EVoxelFluid GetFluidAt(const FVoxelBlockTable& Table, uint16 State, uint16 Above, double HeightInCell);

    // The fog for the fluid at the eye and the biome there (null: the defaults), or none: the switch is off or the eye
    // is not in a fluid.
    // bBright: the water colour brightened to WaterBrightest (above); false keeps the attribute's colour as is.
    UNNAMED_API TOptional<FFog> GetFog(
        bool bSwitch, EVoxelFluid AtEye, const FVoxelBiome* Biome, double CellCm, bool bBright = true);

    // voxel.Look.WaterFog and voxel.Look.WaterFogBright as read now.
    UNNAMED_API bool IsOn();
    UNNAMED_API bool IsBright();
    // voxel.Look.WaterSurface, read when the world loads its materials: 1 draws water with the materials that write
    // their depth (M_ChunkDepthWater, M_ChunkFarDepthWater), so from below the fog covers the surface by its own
    // distance and the surface shows (the default); 0 keeps M_ChunkWater and M_ChunkFarWater, the surface fogged as the sky.
    UNNAMED_API bool IsSurface();
    // The water layer's material name, near or far (bFar), for the switch's value.
    UNNAMED_API const TCHAR* GetWaterMaterialName(bool bSurface, bool bFar);

    // Puts the fog on Volume (M_WaterFog as a blendable at weight 1), or takes it off (weight 0). Material holds the
    // dynamic instance between calls; it is made on first use.
    UNNAMED_API void Apply(
        APostProcessVolume& Volume, TObjectPtr<UMaterialInstanceDynamic>& Material, const TOptional<FFog>& Fog);
}
