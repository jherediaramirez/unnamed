#include "Storage/VoxelColumn.h"
#include "Blocks/VoxelBlockTable.h"

namespace
{
    // A heightmap value (Z - MinZ + 1, 0 for none) per EVoxelHeightmap, from one X, Y's states top down: each map
    // stops at its first match. GetLocalState(Local) reads the state at Z = MinZ + Local.
    template <typename FGetLocalState>
    void FindColumnHeights(
        const FVoxelBlockTable& Table, FGetLocalState&& GetLocalState, uint16 (&Found)[int32(EVoxelHeightmap::Count)])
    {
        int32 Remaining = int32(EVoxelHeightmap::Count);
        for (uint16& Height : Found)
            Height = 0;
        for (int32 Local = VoxelConstants::Height - 1; Local >= 0 && Remaining > 0; --Local)
        {
            const uint16 State = GetLocalState(Local);
            if (State == 0) continue;
            const FVoxelStateInfo& Info = Table.Get(State);
            const bool bMotion = Info.bSolid || Info.IsFluid();
            const bool Matches[] = {true, Info.bSolid, bMotion, bMotion && !Info.bLeaves};
            for (int32 Map = 0; Map < int32(EVoxelHeightmap::Count); ++Map)
                if (!Found[Map] && Matches[Map])
                {
                    Found[Map] = uint16(Local + 1);
                    --Remaining;
                }
        }
    }
}

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

    for (int32 Cell = 0; Cell < Area; ++Cell)
    {
        uint16 Found[int32(EVoxelHeightmap::Count)];
        FindColumnHeights(Table, [&Dense, Cell](int32 Local) { return Dense[Local * Area + Cell]; }, Found);
        for (int32 Map = 0; Map < int32(EVoxelHeightmap::Count); ++Map)
            Heightmaps[Map][Cell] = Found[Map];
    }
}

void FVoxelColumn::SetState(int32 X, int32 Y, int32 Z, uint16 State, const FVoxelBlockTable& Table)
{
    if (Z < VoxelConstants::MinZ || Z > VoxelConstants::MaxZ) return;
    const int32 Local = Z - VoxelConstants::MinZ;
    Sections[Local >> 4].Set(FVoxelSection::GetIndex(X, Y, Local & 15), State);
    uint16 Found[int32(EVoxelHeightmap::Count)];
    FindColumnHeights(Table, [this, X, Y](int32 Cell) { return Sections[Cell >> 4].Get(X, Y, Cell & 15); }, Found);
    for (int32 Map = 0; Map < int32(EVoxelHeightmap::Count); ++Map)
        Heightmaps[Map][Y * VoxelConstants::ColumnSize + X] = Found[Map];
}

SIZE_T FVoxelColumn::GetAllocatedSize() const
{
    SIZE_T Size = sizeof(*this);
    for (const FVoxelSection& Section : Sections)
        Size += Section.GetAllocatedSize();
    return Size;
}
