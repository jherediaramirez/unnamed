#include "Storage/VoxelColumn.h"
#include "Blocks/VoxelBlockTable.h"

uint16 FVoxelColumn::GetState(int32 X, int32 Y, int32 Z) const
{
    if (Z < VoxelConstants::MinZ || Z > VoxelConstants::MaxZ) return 0;
    const int32 Local = Z - VoxelConstants::MinZ;
    return Sections[Local >> 4].Get(X, Y, Local & 15);
}

int32 FVoxelColumn::GetHeight(EVoxelHeightmap Map, int32 X, int32 Y) const
{
    return VoxelConstants::MinZ - 1 + Heightmaps[int32(Map)][Y * VoxelConstants::ColumnSize + X];
}

void FVoxelColumn::Build(TConstArrayView<uint16> Dense, const FVoxelBlockTable& Table)
{
    check(Dense.Num() == DenseVolume);
    for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
        Sections[Section] =
            FVoxelSection::FromStates(Dense.Slice(Section * FVoxelSection::Volume, FVoxelSection::Volume));

    // From the top down, each heightmap stops at its first match.
    for (int32 Cell = 0; Cell < Area; ++Cell)
    {
        uint16 Found[int32(EVoxelHeightmap::Count)] = {};
        int32 Remaining = int32(EVoxelHeightmap::Count);
        for (int32 Local = VoxelConstants::Height - 1; Local >= 0 && Remaining > 0; --Local)
        {
            const uint16 State = Dense[Local * Area + Cell];
            if (State == 0) continue;
            const FVoxelStateInfo& Info = Table.Get(State);
            const uint16 Height = uint16(Local + 1);
            const bool bMotion = Info.bSolid || Info.IsFluid();
            const bool Matches[] = {true, Info.bSolid, bMotion, bMotion && !Info.bLeaves};
            for (int32 Map = 0; Map < int32(EVoxelHeightmap::Count); ++Map)
                if (!Found[Map] && Matches[Map])
                {
                    Found[Map] = Height;
                    --Remaining;
                }
        }
        for (int32 Map = 0; Map < int32(EVoxelHeightmap::Count); ++Map)
            Heightmaps[Map][Cell] = Found[Map];
    }
}

SIZE_T FVoxelColumn::GetAllocatedSize() const
{
    SIZE_T Size = sizeof(*this);
    for (const FVoxelSection& Section : Sections)
        Size += Section.GetAllocatedSize();
    return Size;
}
