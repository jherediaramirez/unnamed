#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class IConsoleVariable;
class UWorld;

// Options > Render Distance (the saved preference renderDistance: vanilla's slider, 2 to 32 chunks, default 12;
// minecraft.wiki/w/Options) drives the streaming rings. Full meshes reach the slider's radius and the far ring twice
// that, up to 24: the largest far radius the ray tracing crash set has run (ViewRadius 24 and 32, FarRadius 24, all
// clean; Docs/Records/2026-10-11-Voxel-Phase9-S1-Far-Ring.md and the S1b record). The default 12 gives the project
// settings' 12 and 24.
namespace VoxelRenderDistance
{
    constexpr int32 MinChunks = 2;
    constexpr int32 MaxChunks = 32;
    constexpr int32 DefaultChunks = 12;
    // The far ring's cap: far LOD past it has never run under ray tracing.
    constexpr int32 MaxFarRadius = 24;

    struct FRadii
    {
        int32 ViewRadius = 0;
        int32 FarRadius = 0;
        bool operator==(const FRadii& Other) const = default;
    };

    // The slider's radii, the slider clamped to 2-32. From 24 up the far ring is no wider than the view, so none draws
    // and the edge is step 1's fog wall at the view radius.
    UNNAMED_API FRadii FromChunks(int32 Chunks);

    // The radii to push: the saved preference's (a number; anything else reads as the default 12), or the project
    // settings' when voxel.RenderDistance.FromOptions is 0.
    UNNAMED_API FRadii GetWanted(bool bFromOptions, const FJsonValue* Preference, const FRadii& ProjectSettings);

    // Moves one variable to Value at game-override priority: above the project settings, below a [ConsoleVariables]
    // ini, the command line (-ExecCmds) and the console, which keep their values. Returns whether it changed.
    UNNAMED_API bool SetRadius(IConsoleVariable& Variable, int32 Value);

    // Each frame from the voxel world, and before its streaming starts: pushes GetWanted's radii into voxel.ViewRadius
    // and voxel.FarRadius where they differ. Does nothing without the menu's preferences (automation without a game).
    UNNAMED_API void Apply(const UWorld* World);
}
