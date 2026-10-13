#include "Voxel/VoxelWaterFog.h"

#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Engine/PostProcessVolume.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnnamedProjectSettings.h"

namespace
{
    TAutoConsoleVariable<int32> CVarLookWaterFog(TEXT("voxel.Look.WaterFog"), 1,
        TEXT("1: while the camera's eye is in water, 26.3's water fog (the biome's water_fog_color, -8 to 96 blocks); "
             "in lava, lava's fog. 0: no fog in fluids, the view as before."));

    TAutoConsoleVariable<int32> CVarLookWaterFogBright(TEXT("voxel.Look.WaterFogBright"), 1,
        TEXT("1: the water fog's colour brightened as vanilla draws it at noon (its brightest channel at 241). 0: the "
             "biome's water_fog_color as is (darker)."));

    TAutoConsoleVariable<int32> CVarLookWaterSurface(TEXT("voxel.Look.WaterSurface"), 1,
        TEXT("1: water writes its depth, so the fog in water covers the surface by its distance and the surface shows "
             "from below. 0: the surface fogged as the sky (invisible from below). Read when the world loads."));

    const TCHAR* const MaterialName = TEXT("M_WaterFog");

    FLinearColor DisplayColor(int32 Rgb)
    {
        return FLinearColor((Rgb >> 16 & 0xFF) / 255.f, (Rgb >> 8 & 0xFF) / 255.f, (Rgb & 0xFF) / 255.f);
    }
}

EVoxelFluid VoxelWaterFog::GetFluidAt(const FVoxelBlockTable& Table, uint16 State, uint16 Above, double HeightInCell)
{
    const FVoxelStateInfo& Info = Table.Get(State);
    if (!Info.HoldsFluid()) return EVoxelFluid::None;
    const FVoxelStateInfo& Fluid = Info.HeldFluid ? Table.Get(Info.HeldFluid) : Info;
    const double Top = Table.Get(Above).HoldsFluid() ? 1. : Fluid.GetFluidHeight();
    return HeightInCell < Top ? Fluid.Fluid : EVoxelFluid::None;
}

TOptional<VoxelWaterFog::FFog> VoxelWaterFog::GetFog(
    bool bSwitch, EVoxelFluid AtEye, const FVoxelBiome* Biome, double CellCm, bool bBright)
{
    if (!bSwitch) return {};
    if (AtEye == EVoxelFluid::Lava)
        return FFog{DisplayColor(LavaColor), float(LavaStartBlocks * CellCm), float(LavaEndBlocks * CellCm)};
    if (AtEye != EVoxelFluid::Water) return {};
    const int32 Color = Biome && Biome->WaterFogColor != INDEX_NONE ? Biome->WaterFogColor : WaterColor;
    const float EndBlocks = WaterEndBlocks * (Biome ? Biome->WaterFogEndScale : 1.f);
    FLinearColor Shown = DisplayColor(Color);
    if (const float Brightest = FMath::Max3(Shown.R, Shown.G, Shown.B); bBright && Brightest > 0)
        Shown = FLinearColor(Shown.R, Shown.G, Shown.B) * (WaterBrightest / Brightest);
    return FFog{Shown, float(WaterStartBlocks * CellCm), float(EndBlocks * CellCm)};
}

bool VoxelWaterFog::IsBright()
{
    return CVarLookWaterFogBright.GetValueOnGameThread() != 0;
}

bool VoxelWaterFog::IsSurface()
{
    return CVarLookWaterSurface.GetValueOnGameThread() != 0;
}

const TCHAR* VoxelWaterFog::GetWaterMaterialName(bool bSurface, bool bFar)
{
    if (bFar) return bSurface ? TEXT("M_ChunkFarDepthWater") : TEXT("M_ChunkFarWater");
    return bSurface ? TEXT("M_ChunkDepthWater") : TEXT("M_ChunkWater");
}

bool VoxelWaterFog::IsOn()
{
    return CVarLookWaterFog.GetValueOnGameThread() != 0;
}

void VoxelWaterFog::Apply(
    APostProcessVolume& Volume, TObjectPtr<UMaterialInstanceDynamic>& Material, const TOptional<FFog>& Fog)
{
    if (!Material)
    {
        if (!Fog) return;
        UMaterialInterface* Parent =
            Cast<UMaterialInterface>(UUnnamedProjectSettings::GetBlockAssetPath(MaterialName).TryLoad());
        if (!Parent) return;
        Material = UMaterialInstanceDynamic::Create(Parent, &Volume);
    }
    // Added once per volume (also after the volume's settings were replaced).
    if (!Volume.Settings.WeightedBlendables.Array.ContainsByPredicate(
            [&Material](const FWeightedBlendable& Blendable) { return Blendable.Object == Material; }))
        Volume.Settings.WeightedBlendables.Array.Add(FWeightedBlendable(0.f, Material));
    if (Fog)
    {
        Material->SetVectorParameterValue(TEXT("Color"), Fog->Color);
        Material->SetScalarParameterValue(TEXT("Start"), Fog->StartCm);
        Material->SetScalarParameterValue(TEXT("End"), Fog->EndCm);
    }
    for (FWeightedBlendable& Blendable : Volume.Settings.WeightedBlendables.Array)
        if (Blendable.Object == Material) Blendable.Weight = Fog ? 1.f : 0.f;
}
