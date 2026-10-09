#include "Voxel/VoxelTerrainFactory.h"

#include "Blocks/VoxelBlockTable.h"
#include "Density/VoxelDensityGraph.h"
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
    FString Error;
    if (VoxelWorldgen::ReadPreset(ReferenceRoot, PresetName, Preset, Error))
    {
        TSharedRef<FVoxelDensityTerrain> Density = MakeShared<FVoxelDensityTerrain>(Preset, uint64(Seed), Table);
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
