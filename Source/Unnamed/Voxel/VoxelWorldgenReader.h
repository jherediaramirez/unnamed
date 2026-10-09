#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class FVoxelBiomeTable;
class FVoxelDensityGraph;
class FVoxelSurfaceRules;
struct FVoxelDensityNoise;
struct FVoxelSurfaceInputs;
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

    // Biomes and surface rules (VoxelSurfaceReader.cpp). Each biome file's attributes, features and carvers are
    // skipped by name; any other unknown field fails.

    // Reads biome/<name>.json for each full id ("minecraft:plains") into Out.Biomes. Ids Out already holds are reused.
    bool ReadBiomes(const FString& Root, TConstArrayView<FString> Ids, FVoxelBiomeTable& Out, FString& OutError);

    // Reads multi_noise_biome_source_parameter_list/<List>.json, then the points of the preset it names from the
    // reference's facts/biome_parameters.json and the biomes they use, into Out (emptied first), then validates it.
    bool ReadBiomeParameters(const FString& Root, const FString& List, FVoxelBiomeTable& Out, FString& OutError);

    // Reads material_rule/<Rule>.json and every rule and condition it references into Out (emptied first), then
    // validates the tree. Out.Root is that rule.
    bool ReadSurfaceRules(const FString& Root, const FString& Rule, FVoxelSurfaceRules& Out, FString& OutError);

    // Reads the surface rule tree Rule names (an id, "minecraft:overworld", as a preset's MaterialRule) and the noises
    // it reads (FVoxelSurfaceEvaluator::GetNoiseIds) into Out.
    bool ReadSurface(const FString& Root, const FString& Rule, FVoxelSurfaceInputs& Out, FString& OutError);

    // Write a rule or a condition back as the reference's JSON. A named node is written as its name, except Index
    // itself when bExpand is set (a named node's own file).
    TSharedPtr<FJsonValue> WriteSurfaceRule(const FVoxelSurfaceRules& Rules, int32 Index, bool bExpand);
    TSharedPtr<FJsonValue> WriteSurfaceCondition(const FVoxelSurfaceRules& Rules, int32 Index, bool bExpand);
} // namespace VoxelWorldgen
