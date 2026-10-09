#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class FVoxelDensityGraph;
struct FVoxelDensityNoise;
struct FVoxelTerrainPreset;

// Reads the 26.3 worldgen reference (Reference/minecraft-worldgen-26.3) into the voxel core's plain density graph, and
// writes graph nodes back as the reference's JSON. The parse lives in the game module because the core depends on Core
// alone and has no JSON reader. Reading is strict: an unknown node type, a missing or unexpected field, a missing file
// or a cycle fails with a reason naming the file and the field, so a format change is caught rather than half read.
namespace VoxelWorldgen
{
    // Loads a file under data/minecraft/worldgen/ by its relative path, for example "noise/continentalness.json".
    using FLoadFile = TFunctionRef<bool(const FString& Path, FString& OutText)>;

    // <project>/Reference/minecraft-worldgen-26.3.
    FString GetReferenceRoot();

    // Reads noise_settings/<Preset>.json and everything it references from the reference folder at Root, then validates
    // the graph.
    bool ReadPreset(const FString& Root, const FString& Preset, FVoxelTerrainPreset& Out, FString& OutError);

    // Reads the named density functions and noises (full ids such as "minecraft:overworld/continents"), with everything
    // they reference, into Graph. Names Graph already holds are reused.
    bool ReadReferences(const FString& Root, TConstArrayView<FString> Functions, TConstArrayView<FString> Noises,
        FVoxelDensityGraph& Graph, FString& OutError);
    bool ReadReferences(FLoadFile LoadFile, TConstArrayView<FString> Functions, TConstArrayView<FString> Noises,
        FVoxelDensityGraph& Graph, FString& OutError);

    // Writes Node as the reference's JSON. A node that is a named function is written as its name, except Node itself
    // when bExpand is set (a function's own file).
    TSharedPtr<FJsonValue> WriteValue(const FVoxelDensityGraph& Graph, int32 Node, bool bExpand);
    // Writes a noise as its file, leaving out the count and modifiers where the reference does.
    TSharedPtr<FJsonValue> WriteNoise(const FVoxelDensityNoise& Noise);
} // namespace VoxelWorldgen
