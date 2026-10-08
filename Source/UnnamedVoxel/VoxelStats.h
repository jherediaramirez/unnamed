#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

// `stat Voxel` lists the generator's timings. Each stage declares its cycle stats in this group as it lands and
// pairs them with TRACE_CPUPROFILER_EVENT_SCOPE, so Unreal Insights shows the same scopes.
DECLARE_STATS_GROUP(TEXT("Voxel"), STATGROUP_Voxel, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Generate column"), STAT_VoxelGenerateColumn, STATGROUP_Voxel, UNNAMEDVOXEL_API);
DECLARE_CYCLE_STAT_EXTERN(
    TEXT("Gather section neighbours"), STAT_VoxelGatherSection, STATGROUP_Voxel, UNNAMEDVOXEL_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Mesh section"), STAT_VoxelMeshSection, STATGROUP_Voxel, UNNAMEDVOXEL_API);
