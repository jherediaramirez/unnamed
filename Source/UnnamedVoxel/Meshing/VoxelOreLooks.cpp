#include "Meshing/VoxelOreLooks.h"

#include "Blocks/VoxelBlockTable.h"
#include "Meshing/VoxelMesher.h"

// Ore and Rock fill FVoxelStateInfo's padding after TintSource, so the block table's stride is what it was before them.
// A new field may change this; then check the mesh bench before updating the size.
static_assert(sizeof(FVoxelStateInfo) == 68, "FVoxelStateInfo changed size: keep the ore look classes in its padding");

const TCHAR* const VoxelOreLooks::OreNames[OreCount] = {TEXT("coal"), TEXT("iron"), TEXT("copper"), TEXT("gold"),
    TEXT("redstone"), TEXT("emerald"), TEXT("lapis"), TEXT("diamond")};

void VoxelOreLooks::Classify(FVoxelBlockTable& Table)
{
    // (ore class, rock class) per block.
    TArray<TPair<uint8, uint8>> ByBlock;
    ByBlock.SetNumZeroed(Table.GetBlockLimit());
    for (int32 Ore = 0; Ore < OreCount; ++Ore)
        for (int32 Base = 0; Base < 2; ++Base)
        {
            const int32 Block =
                Table.FindBlock(FString::Printf(TEXT("%s%s_ore"), Base ? TEXT("deepslate_") : TEXT(""), OreNames[Ore]));
            if (Block != INDEX_NONE) ByBlock[Block].Key = uint8(1 + Ore * 2 + Base);
        }
    const TPair<const TCHAR*, uint8> Rocks[] = {{TEXT("stone"), 1}, {TEXT("deepslate"), 1}, {TEXT("granite"), 2},
        {TEXT("diorite"), 3}, {TEXT("andesite"), 4}, {TEXT("tuff"), 5}};
    for (const TPair<const TCHAR*, uint8>& Rock : Rocks)
    {
        const int32 Block = Table.FindBlock(Rock.Key);
        if (Block != INDEX_NONE) ByBlock[Block].Value = Rock.Value;
    }
    for (int32 State = 0; State < Table.GetStateCount(); ++State)
    {
        FVoxelStateInfo Info = Table.Get(uint16(State));
        const TPair<uint8, uint8> Classes =
            ByBlock.IsValidIndex(Info.Block) ? ByBlock[Info.Block] : TPair<uint8, uint8>(0, 0);
        const bool bCube = Info.Shape == EVoxelShape::Full;
        const uint8 Ore =
            bCube && Info.Layer == EVoxelLayer::Opaque && !Info.Faces[VoxelFaces::OverlayIndex] ? Classes.Key : 0;
        const uint8 Rock = bCube ? Classes.Value : 0;
        if (Info.Ore == Ore && Info.Rock == Rock) continue;
        Info.Ore = Ore;
        Info.Rock = Rock;
        Table.SetState(uint16(State), Info);
    }
}

int32 VoxelOreLooks::PickHost(const FVoxelBlockTable& Table, const uint16* States, int32 Index)
{
    constexpr int32 Size = FVoxelPaddedSection::Size;
    constexpr int32 FaceSteps[6] = {-1, 1, -Size, Size, -Size * Size, Size * Size};
    // Neighbours per rock class; class 0 (not a rock) is ignored.
    int32 Counts[6] = {};
    for (const int32 Step : FaceSteps)
        ++Counts[Table.Get(States[Index + Step]).Rock];
    if (Counts[1] + Counts[2] + Counts[3] + Counts[4] + Counts[5] == 0)
        for (int32 DZ = -1; DZ <= 1; ++DZ)
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                    if (FMath::Abs(DX) + FMath::Abs(DY) + FMath::Abs(DZ) >= 2)
                        ++Counts[Table.Get(States[Index + (DZ * Size + DY) * Size + DX]).Rock];
    int32 Best = 0, BestCount = 0;
    bool bTie = false;
    for (int32 Rock = 1; Rock < 6; ++Rock)
    {
        if (Counts[Rock] > BestCount)
        {
            Best = Rock;
            BestCount = Counts[Rock];
            bTie = false;
        }
        else if (BestCount && Counts[Rock] == BestCount)
            bTie = true;
    }
    return BestCount && !bTie && Best >= 2 ? Best - 1 : 0;
}

uint32 VoxelOreLooks::GetLookWord(uint8 Ore, int32 Host, int32 OreHost, int32 OreGlint)
{
    if (!Ore || (!OreHost && !OreGlint)) return 0;
    const int32 OreBase = Ore - 1;
    const int32 Shown = OreHost ? Host : 0;
    if (OreGlint == DebugMask)
        return LookBit | uint32(DebugMask) << LevelShift | uint32(GetSlot(EMode::Composite, 0, OreBase));
    if (OreGlint == 1)
        return LookBit | 1u << LevelShift |
            uint32(GetSlot(OreHost == 2 ? EMode::SwapOreMips : EMode::CompositeOreMips, Shown, OreBase));
    if (!Shown) return 0;
    return LookBit | uint32(GetSlot(OreHost == 2 ? EMode::Swap : EMode::Composite, Shown, OreBase));
}
