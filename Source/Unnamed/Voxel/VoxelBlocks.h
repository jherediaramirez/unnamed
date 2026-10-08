#pragma once

#include "CoreMinimal.h"

class FFoundationBlockRegistry;
class FVoxelBlockTable;

// The voxel core's view of the block registry (Docs/Blocks.md): one FVoxelStateInfo per state id, with the face codes
// the block materials already decode. Water's faces come from its still and flow textures, as the Playground draws it.
namespace VoxelBlocks
{
    UNNAMED_API TSharedRef<const FVoxelBlockTable> MakeTable(const FFoundationBlockRegistry& Registry);
    // The table for the project registry (FFoundationBlockRegistry::GetDefault), built on first use on the game thread.
    UNNAMED_API TSharedRef<const FVoxelBlockTable> GetDefault();
}
