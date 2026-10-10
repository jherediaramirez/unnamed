#include "Decoration/VoxelDecorationContext.h"
#include "Blocks/VoxelBlockTable.h"

namespace
{
    // Slots to start with: room for 8,192 writes, a little over an ordinary column's ores (about 7,000).
    constexpr int32 InitialSlotBits = 14;
    constexpr int32 LiveMaps = 4;
    constexpr int32 WindowArea = FVoxelDecorationContext::Window * FVoxelDecorationContext::Window;
}

FVoxelDecorationContext::FVoxelDecorationContext(
    FIntPoint InCenter, TConstArrayView<const FVoxelColumn*> Snapshots, const FVoxelBlockTable* InTable)
    : Center(InCenter),
      MinX((InCenter.X - 1) * VoxelConstants::ColumnSize),
      MinY((InCenter.Y - 1) * VoxelConstants::ColumnSize),
      Table(InTable)
{
    if (Table) Raised.Init(int16(VoxelConstants::MinZ - 1), LiveMaps * WindowArea);
    check(Snapshots.Num() == 9);
    for (int32 Column = 0; Column < 9; ++Column)
    {
        check(Snapshots[Column]);
        Columns[Column] = Snapshots[Column];
        int32 Top = VoxelConstants::MinZ - 1;
        for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
            for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                Top = FMath::Max(Top, Snapshots[Column]->GetHeight(EVoxelHeightmap::WorldSurface, X, Y));
        TopZ[Column] = Top;
    }
    thread_local TArray<uint64> Bitmap;
    if (Bitmap.IsEmpty()) Bitmap.SetNumZeroed(WrittenWords);
    Written = Bitmap.GetData();
    Keys.Init(EmptyKey, 1 << InitialSlotBits);
    Values.SetNumUninitialized(1 << InitialSlotBits);
    SlotMask = (1u << InitialSlotBits) - 1;
    HashShift = 32 - InitialSlotBits;
}

FVoxelDecorationContext::~FVoxelDecorationContext()
{
    if (Count * 4 > Keys.Num())
        FMemory::Memzero(Written, WrittenWords * sizeof(uint64));
    else
        for (const uint32 Key : Keys)
            if (Key != EmptyKey) Written[Key >> 6] = 0;
}

void FVoxelDecorationContext::Grow()
{
    TArray<uint32> OldKeys = MoveTemp(Keys);
    TArray<uint16> OldValues = MoveTemp(Values);
    const int32 Slots = OldKeys.Num() * 2;
    Keys.Init(EmptyKey, Slots);
    Values.SetNumUninitialized(Slots);
    SlotMask = uint32(Slots - 1);
    --HashShift;
    for (int32 Old = 0; Old < OldKeys.Num(); ++Old)
    {
        if (OldKeys[Old] == EmptyKey) continue;
        uint32 Slot = Hash(OldKeys[Old]);
        while (Keys[Slot] != EmptyKey)
            Slot = (Slot + 1) & SlotMask;
        Keys[Slot] = OldKeys[Old];
        Values[Slot] = OldValues[Old];
    }
}

void FVoxelDecorationContext::SetState(int32 X, int32 Y, int32 Z, uint16 State)
{
    const int32 LX = X - MinX, LY = Y - MinY, Local = Z - VoxelConstants::MinZ;
    if (uint32(LX) >= uint32(Window) || uint32(LY) >= uint32(Window) || uint32(Local) >= uint32(VoxelConstants::Height))
        return;
    if (2 * (Count + 1) > Keys.Num()) Grow();
    const uint32 Key = uint32((Local * Window + LY) * Window + LX);
    uint32 Slot = Hash(Key);
    while (Keys[Slot] != Key && Keys[Slot] != EmptyKey)
        Slot = (Slot + 1) & SlotMask;
    if (Keys[Slot] == EmptyKey)
    {
        Keys[Slot] = Key;
        ++Count;
    }
    Values[Slot] = State;
    Written[Key >> 6] |= uint64(1) << (Key & 63);
    const int32 Column = (LY >> 4) * 3 + (LX >> 4);
    if (State != 0) TopZ[Column] = FMath::Max(TopZ[Column], Z);
    if (Table && !bFrozenHeights && State != 0)
    {
        // FVoxelColumn's rules: any block; blocks motion; blocks motion or holds fluid; the same without leaves.
        const FVoxelStateInfo& Info = Table->Get(State);
        const bool bMotion = Info.bSolid || Info.HoldsFluid();
        const bool Counts[LiveMaps] = {true, Info.bSolid, bMotion, bMotion && !Info.bLeaves};
        const int32 Cell = LY * Window + LX;
        for (int32 Map = 0; Map < LiveMaps; ++Map)
            if (Counts[Map])
            {
                int16& Top = Raised[Map * WindowArea + Cell];
                Top = int16(FMath::Max(int32(Top), Z));
            }
    }
}

int32 FVoxelDecorationContext::GetHeight(EVoxelDecorationHeightmap Map, int32 X, int32 Y) const
{
    const int32 LX = X - MinX, LY = Y - MinY;
    if (uint32(LX) >= uint32(Window) || uint32(LY) >= uint32(Window)) return VoxelConstants::MinZ - 1;
    static constexpr EVoxelHeightmap SnapshotMap[] = {EVoxelHeightmap::WorldSurface, EVoxelHeightmap::OceanFloor,
        EVoxelHeightmap::WorldSurface, EVoxelHeightmap::OceanFloor, EVoxelHeightmap::MotionBlocking,
        EVoxelHeightmap::MotionBlockingNoLeaves};
    const int32 Snapshot = Columns[(LY >> 4) * 3 + (LX >> 4)]->GetHeight(SnapshotMap[int32(Map)], LX & 15, LY & 15);
    const int32 Live = int32(Map) - int32(EVoxelDecorationHeightmap::WorldSurface);
    if (Live < 0 || Raised.IsEmpty()) return Snapshot;
    return FMath::Max(Snapshot, int32(Raised[Live * WindowArea + LY * Window + LX]));
}

int32 FVoxelDecorationContext::GetBiome(int32 X, int32 Y, int32 Z) const
{
    const int32 LX = X - MinX, LY = Y - MinY;
    if (uint32(LX) >= uint32(Window) || uint32(LY) >= uint32(Window)) return INDEX_NONE;
    return Columns[(LY >> 4) * 3 + (LX >> 4)]->GetBiome(
        LX & 15, LY & 15, FMath::Clamp(Z, VoxelConstants::MinZ, VoxelConstants::MaxZ));
}
