#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

// The voxel world's console surface. The variables are the live tunables: UVoxelWorldSettings pushes its values into
// them at project-setting priority, the console or an ini can override one for a session, and any thread may read
// them. Docs/VoxelWorld.md lists which phase uses each.
namespace VoxelConsole
{
    extern UNNAMED_API TAutoConsoleVariable<int32> CVarViewRadius;
    extern UNNAMED_API TAutoConsoleVariable<int32> CVarUnloadMargin;
    // Far LOD (phase 9, S1): the far ring's radius, far shadows, the canopy's layer and the debug tint.
    extern UNNAMED_API TAutoConsoleVariable<int32> CVarFarRadius;
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarFarShadows;
    extern UNNAMED_API TAutoConsoleVariable<int32> CVarFarCanopyLayer;
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarFarTrees;
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarFarTint;
    // Far LOD step 1b: far meshes of columns at the view's edge that show nothing go before refining full meshes.
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarFarInnerFirst;
    // Far LOD step 2: the haze and the far meshes' edge fade while far meshes form the edge, or step 1's fog wall (0).
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarFogHaze;
    // The loading screen's look: 1 draws the chunk map under the progress bar (26.3's), 0 leaves it out.
    extern UNNAMED_API TAutoConsoleVariable<int32> CVarLoadingScreenMap;
    extern UNNAMED_API TAutoConsoleVariable<float> CVarApplyBudgetMs;
    extern UNNAMED_API TAutoConsoleVariable<int32> CVarMaxTasksInFlight;
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarAmbientOcclusion;
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarGreedyMeshing;
    extern UNNAMED_API TAutoConsoleVariable<int32> CVarTintBlend;
    // Debug only, not a setting: columns at the edge of the view mesh against air (captures' cut through the world).
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarEdgeWalls;
    // Decoration (phase 6): on, streamed, bench and gate columns are decorated; off is the control that keeps them as
    // Generate leaves them. Read when a world starts; a change rebuilds it.
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarDecoration;
    // With decoration, where the freeze pass runs: at Finalize (on, S3) or inside the snapshot (off, S2's order).
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarDecorationFreezeLast;
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarDecorationPlants;
    // With plants, the tree feature (phase 6, S9a); off is the control that reproduces S8's goldens and hashes.
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarDecorationTrees;
    // Ore veins (phase 6, S4): on, Generate runs the ore_vein rules in streamed worlds and benches; off is the control
    // that gives the blocks from before S4. Read when a world starts; a change rebuilds it.
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarOreVeins;
    // Render-only ore looks (scout E's look calls; Meshing/VoxelOreLooks.h): voxel.Look.OreHost and
    // voxel.Look.OreGlint. A change remeshes every loaded column; benches read them too.
    extern UNNAMED_API TAutoConsoleVariable<int32> CVarOreHost;
    extern UNNAMED_API TAutoConsoleVariable<int32> CVarOreGlint;
    // The carvers (phase 6, S10); off by default until S11.
    extern UNNAMED_API TAutoConsoleVariable<bool> CVarCarvers;

    // Tasks in flight when voxel.MaxTasksInFlight is 0: twice the task workers, so workers never idle between
    // finishing one job and the game thread queuing the next.
    UNNAMED_API int32 GetMaxTasksInFlight();
    // voxel.TintBlend clamped to -1 (biome tints off) to FVoxelMeshOptions::MaxTintBlend.
    UNNAMED_API int32 GetTintBlend();
    // voxel.Look.OreHost as 0 (vanilla), 1 (composite) or 2 (palette swap); other values read as 0.
    UNNAMED_API int32 GetOreHost();
    // voxel.Look.OreGlint as 0 (vanilla), 1 (ore-aware mips and gloss) or 3 (debug mask); other values read as 0.
    UNNAMED_API int32 GetOreGlint();

    // The text voxel.Info prints: the world range, the dev seed, every voxel.* variable with its value and what set
    // it, the derived limits and the mesh backend; with World's voxel world running, its streaming stages too.
    UNNAMED_API FString DescribeInfo(const UWorld* World = nullptr);
}
