#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

// `stat Voxel` lists the generator's timings. Each stage declares its cycle stats in this group as it lands and
// pairs them with TRACE_CPUPROFILER_EVENT_SCOPE, so Unreal Insights shows the same scopes.
DECLARE_STATS_GROUP(TEXT("Voxel"), STATGROUP_Voxel, STATCAT_Advanced);
