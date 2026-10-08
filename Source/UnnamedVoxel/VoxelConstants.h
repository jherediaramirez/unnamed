#pragma once

#include "CoreMinimal.h"

// World dimensions in blocks. Cells use the project's Unreal axes (X east, Y south, Z up), so Z plays the reference's
// Y: the world spans Z -64..319 in 24 sections of 16, and water fills open space below sea level (top water block 62).
namespace VoxelConstants
{
    constexpr int32 SectionSize = 16;
    // A column (the reference's chunk) is 16 x 16 blocks across X and Y and the full world height.
    constexpr int32 ColumnSize = SectionSize;
    constexpr int32 MinZ = -64;
    constexpr int32 Height = 384;
    constexpr int32 MaxZ = MinZ + Height - 1;
    constexpr int32 SectionCount = Height / SectionSize;
    constexpr int32 SeaLevel = 63;

    static_assert(MinZ % SectionSize == 0 && Height % SectionSize == 0, "Sections must tile the world range");
}
