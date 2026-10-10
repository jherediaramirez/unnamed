#pragma once

#include "CoreMinimal.h"

class FVoxelDensityTerrain;

namespace VoxelBench
{
    // The carvers' bench (phase 6, S10; voxel.Bench Carvers): (2 Radius + 1)^2 columns on this thread, each
    // generated with the carvers off and on in alternating order (corner sharing off on both sides, so neither finds
    // the other's corners). Reports Generate on both sides and the time the carvers add, the carve pass's split and
    // counts, the path cache's size, and the blocks that differ with the path cache off (the gate: 0). Terrain needs
    // its carvers compiled (GetCarvers().IsInitialized()).
    UNNAMEDVOXEL_API FString RunCarvers(const FVoxelDensityTerrain& Terrain, int32 Radius);
}
