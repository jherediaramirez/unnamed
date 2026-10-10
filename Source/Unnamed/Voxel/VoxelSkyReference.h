#pragma once

#include "CoreMinimal.h"

struct FVoxelTimeline;

// Reads the 26.3 sky reference (Reference/minecraft-sky-26.3, water and sky DN0) into the voxel core's plain timelines.
// The parse lives in the game module because the core has no JSON reader. Reading is strict, as the worldgen reader's:
// an unknown field, ease, modifier or value type, or a track mixing value types, fails with a reason naming the file
// and the field, so a format change is caught rather than half read.
namespace VoxelSkyReference
{
    // <project>/Reference/minecraft-sky-26.3.
    FString GetReferenceRoot();

    // Parses one timeline file's text; Id names it in errors and fills Out.Id. Keys are sorted (file order kept for
    // equal ticks) and the result validated.
    bool ParseTimeline(const FString& Text, const FString& Id, FVoxelTimeline& Out, FString& OutError);
    // Reads data/minecraft/timeline/<name>.json for a full id ("minecraft:day").
    bool ReadTimeline(const FString& Root, const FString& Id, FVoxelTimeline& Out, FString& OutError);

    // The value beneath an overworld timeline track (attributes.json): the overworld dimension type's value if it sets
    // one, else the wiki default; as a timeline value (a float in X, or a colour). False for an attribute without
    // either, or one whose value is not a number or a colour.
    bool ReadOverworldBase(const FString& Root, const FString& Attribute, FVector4f& OutValue, FString& OutError);
}
