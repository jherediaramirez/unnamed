#include "Density/VoxelCornerCache.h"
#include "Async/UniqueLock.h"

bool FVoxelCornerCache::Initialize(
    int32 InValues, int32 InCornersY, int32 InCellXZ, int32 InColumnSize, const void* InOwner)
{
    Slots.Reset();
    Data.Empty();
    SlotCount = 0;
    if (InValues <= 0 || InCornersY <= 0 || InCornersY > 64 || InCellXZ <= 0 || InColumnSize <= 0 ||
        InColumnSize % InCellXZ != 0)
        return false;
    const int32 InPerLine = InColumnSize / InCellXZ;
    const int64 Count = int64(Window) * Window * (2 * InPerLine - 1);
    if (Count * (int64(InCornersY) * InValues * sizeof(float) + sizeof(FSlot)) > MaxBytes) return false;
    Values = InValues;
    CornersY = InCornersY;
    CellXZ = InCellXZ;
    ColumnSize = InColumnSize;
    PerLine = InPerLine;
    SlotsPerColumn = 2 * PerLine - 1;
    SlotCount = int32(Count);
    AllCorners = CornersY == 64 ? ~uint64(0) : (uint64(1) << CornersY) - 1;
    Owner = InOwner;
    Slots = MakeUnique<FSlot[]>(SlotCount);
    // Values are written before they are read: Find copies only corners a Store has marked.
    Data.SetNumUninitialized(SlotCount * CornersY * Values);
    return true;
}

int64 FVoxelCornerCache::GetBytes() const
{
    return int64(SlotCount) * sizeof(FSlot) + Data.GetAllocatedSize();
}

int32 FVoxelCornerCache::SlotIndex(int32 X, int32 Z) const
{
    if (!IsValid() || X % CellXZ != 0 || Z % CellXZ != 0) return INDEX_NONE;
    const auto FloorDivide = [](int32 A, int32 B) { return A / B - (A % B != 0 && A < 0); };
    const int32 ColumnX = FloorDivide(X, ColumnSize), ColumnZ = FloorDivide(Z, ColumnSize);
    const int32 OffsetX = X - ColumnX * ColumnSize, OffsetZ = Z - ColumnZ * ColumnSize;
    int32 Sub;
    if (OffsetX == 0)
        Sub = OffsetZ / CellXZ;
    else if (OffsetZ == 0)
        Sub = PerLine - 1 + OffsetX / CellXZ;
    else
        return INDEX_NONE;
    return ((ColumnZ & (Window - 1)) * Window + (ColumnX & (Window - 1))) * SlotsPerColumn + Sub;
}

uint64 FVoxelCornerCache::Find(int32 X, int32 Z, uint64 Wanted, float* Out) const
{
    const int32 Index = SlotIndex(X, Z);
    if (Index == INDEX_NONE || Wanted == 0) return 0;
    const FSlot& Slot = Slots[Index];
    UE::TUniqueLock<UE::FMutex> Lock(Slot.Lock);
    if (!Slot.bUsed || Slot.X != X || Slot.Z != Z) return 0;
    const uint64 Found = Slot.Mask & Wanted & AllCorners;
    const float* Source = Data.GetData() + int64(Index) * CornersY * Values;
    for (uint64 Bits = Found; Bits != 0; Bits &= Bits - 1)
    {
        const int32 Y = int32(FMath::CountTrailingZeros64(Bits));
        FMemory::Memcpy(Out + Y * Values, Source + Y * Values, Values * sizeof(float));
    }
    return Found;
}

void FVoxelCornerCache::Store(int32 X, int32 Z, uint64 Mask, const float* In)
{
    const int32 Index = SlotIndex(X, Z);
    Mask &= AllCorners;
    if (Index == INDEX_NONE || Mask == 0) return;
    FSlot& Slot = Slots[Index];
    UE::TUniqueLock<UE::FMutex> Lock(Slot.Lock);
    if (!Slot.bUsed || Slot.X != X || Slot.Z != Z)
    {
        Slot.bUsed = true;
        Slot.X = X;
        Slot.Z = Z;
        Slot.Mask = 0;
    }
    float* Target = Data.GetData() + int64(Index) * CornersY * Values;
    for (uint64 Bits = Mask; Bits != 0; Bits &= Bits - 1)
    {
        const int32 Y = int32(FMath::CountTrailingZeros64(Bits));
        FMemory::Memcpy(Target + Y * Values, In + Y * Values, Values * sizeof(float));
    }
    Slot.Mask |= Mask;
}

void FVoxelCornerCache::Reset()
{
    for (int32 Index = 0; Index < SlotCount; ++Index)
    {
        FSlot& Slot = Slots[Index];
        UE::TUniqueLock<UE::FMutex> Lock(Slot.Lock);
        Slot.bUsed = false;
        Slot.Mask = 0;
    }
}
