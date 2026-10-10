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

    // Tasks in flight when voxel.MaxTasksInFlight is 0: twice the task workers, so workers never idle between
    // finishing one job and the game thread queuing the next.
    UNNAMED_API int32 GetMaxTasksInFlight();
    // voxel.TintBlend clamped to -1 (biome tints off) to FVoxelMeshOptions::MaxTintBlend.
    UNNAMED_API int32 GetTintBlend();

    // The text voxel.Info prints: the world range, the dev seed, every voxel.* variable with its value and what set
    // it, the derived limits and the mesh backend; with World's voxel world running, its streaming stages too.
    UNNAMED_API FString DescribeInfo(const UWorld* World = nullptr);
}
