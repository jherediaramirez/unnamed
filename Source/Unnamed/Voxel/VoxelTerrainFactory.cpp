#include "Voxel/VoxelTerrainFactory.h"

#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Density/VoxelDensityGraph.h"
#include "Surface/VoxelSurfaceEvaluator.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Terrain/VoxelFlatTerrain.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelLog.h"
#include "Worlds/UnnamedSavedWorlds.h"

const TCHAR* VoxelTerrainFactory::GetPresetName(const FString& Type)
{
    return Type == TEXT("Large Biomes") ? TEXT("large_biomes")
        : Type == TEXT("AMPLIFIED")     ? TEXT("amplified")
                                        : TEXT("overworld");
}

bool VoxelTerrainFactory::ReadInputs(const FString& ReferenceRoot, const FString& PresetName,
    FVoxelTerrainPreset& OutPreset, FVoxelBiomeTable& OutBiomes, FVoxelSurfaceInputs& OutSurface, FString& OutError)
{
    // Every generator-2 type picks its biomes from the overworld parameter list, as 26.3's presets do; Large Biomes
    // stretches the climate instead.
    return VoxelWorldgen::ReadPreset(ReferenceRoot, PresetName, OutPreset, OutError) &&
        VoxelWorldgen::ReadBiomeParameters(ReferenceRoot, TEXT("overworld"), OutBiomes, OutError) &&
        VoxelWorldgen::ReadSurface(ReferenceRoot, OutPreset.MaterialRule, OutSurface, OutError);
}

TSharedPtr<const IVoxelTerrain> VoxelTerrainFactory::Make(const FUnnamedSavedWorld* World, int64 Seed,
    const FVoxelBlockTable& Table, const FString& ReferenceRoot, FString& OutError)
{
    if (World && World->IsSuperflat())
    {
        // The screen lists layers top first; the terrain stacks them up from the floor. Ids lose their "minecraft:".
        TArray<FVoxelFlatLayer> Stack;
        for (int32 Index = World->FlatLayers.Num() - 1; Index >= 0; --Index)
        {
            const FString Block = World->FlatLayers[Index].Block.RightChop(10);
            Stack.Add(
                {Block == TEXT("air") ? uint16(0) : Table.FindState(FName(*Block)), World->FlatLayers[Index].Height});
        }
        return MakeShared<FVoxelFlatTerrain>(Stack, Table);
    }
    // Saved worlds keep the generator they were made with. The dev seed takes the newest, or -VoxelGenerator=<n>.
    int32 Generator = FUnnamedSavedWorld::CurrentGenerator;
    if (World)
        Generator = World->Generator;
    else
        FParse::Value(FCommandLine::Get(), TEXT("VoxelGenerator="), Generator);
    if (Generator < 2) return MakeShared<FVoxelHeightTerrain>(Seed, Table);

    const TCHAR* PresetName = GetPresetName(World ? World->Type : FString(TEXT("Default")));
    FVoxelTerrainPreset Preset;
    FVoxelBiomeTable Biomes;
    FVoxelSurfaceInputs Surface;
    FString Error;
    if (ReadInputs(ReferenceRoot, PresetName, Preset, Biomes, Surface, Error))
    {
        FVoxelDensityTerrainSettings Settings;
        // A biome outside the overworld list refuses the world (the terrain reports it); world.json is not changed.
        if (World && World->Type == TEXT("Single Biome")) Settings.FixedBiome = World->Biome;
        // The dev seed can show the earlier dressing instead, to compare looks; saved worlds always keep the rules.
        if (!World && FParse::Param(FCommandLine::Get(), TEXT("VoxelNoSurfaceRules"))) Settings.bSurfaceRules = false;
        TSharedRef<FVoxelDensityTerrain> Density =
            MakeShared<FVoxelDensityTerrain>(Preset, uint64(Seed), Table, Settings, &Biomes, &Surface);
        if (Density->IsValid()) return Density;
        Error = Density->GetError();
    }
    OutError = FString::Printf(TEXT("the %s terrain could not be built: %s"), PresetName, *Error);
    if (World) return nullptr;
    UE_LOG(LogUnnamedVoxel, Error, TEXT("The dev seed's %s; using the stand-in terrain."), *OutError);
    return MakeShared<FVoxelHeightTerrain>(Seed, Table);
}

bool VoxelTerrainFactory::Check(const FUnnamedSavedWorld& World, const FString& ReferenceRoot, FString& OutError)
{
    // The seed does not decide whether a preset builds.
    return Make(&World, 0, *VoxelBlocks::GetDefault(), ReferenceRoot, OutError).IsValid();
}
