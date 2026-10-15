#include "Foundation/FoundationBlocks.h"
#include "Blocks/VoxelBlockTable.h"

namespace
{
    constexpr int32 SectionVolume =
        FoundationGrid::SectionCells * FoundationGrid::SectionCells * FoundationGrid::SectionCells;

    // Until SetRegistry, a world knows only air: building one never loads assets (worlds live in actors whose
    // class defaults are constructed at startup).
    TSharedRef<const FFoundationBlockRegistry> GetAirOnlyRegistry()
    {
        static const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::Build(nullptr, {});
        return Registry;
    }
}

int32 FFoundationSectionStorage::IndexInSection(const FIntVector& Cell)
{
    const FIntVector Section = FoundationGrid::SectionOf(Cell);
    const FIntVector Local = Cell - Section * FoundationGrid::SectionCells;
    return (Local.Z * FoundationGrid::SectionCells + Local.Y) * FoundationGrid::SectionCells + Local.X;
}

FFoundationCell FFoundationSectionStorage::Get(const FIntVector& Cell) const
{
    const FSection* Section = Sections.Find(FoundationGrid::SectionOf(Cell));
    return Section ? Section->Cells[IndexInSection(Cell)] : FFoundationCell{};
}

void FFoundationSectionStorage::Set(const FIntVector& Cell, const FFoundationCell& Value)
{
    FSection& Section = Sections.FindOrAdd(FoundationGrid::SectionOf(Cell));
    if (Section.Cells.IsEmpty()) Section.Cells.SetNum(SectionVolume);
    FFoundationCell& Slot = Section.Cells[IndexInSection(Cell)];
    const int32 Delta = int32(!Value.State.IsAir()) - int32(!Slot.State.IsAir());
    Section.Solid += Delta;
    Solid += Delta;
    Slot = Value;
}

void FFoundationSectionStorage::Empty()
{
    Sections.Empty();
    Solid = 0;
}

void FFoundationSectionStorage::ForEachSolid(const FIntVector& Min, const FIntVector& Max,
    TFunctionRef<void(const FIntVector&, const FFoundationCell&)> Visit) const
{
    if (Min.X > Max.X || Min.Y > Max.Y || Min.Z > Max.Z) return;
    const FIntVector First = FoundationGrid::SectionOf(Min), Last = FoundationGrid::SectionOf(Max);
    // Large ranges (a whole-world clear or redraw) visit only the sections that exist, in the same
    // Z, Y, X order a range scan would use.
    const int64 RangeSections = int64(Last.X - First.X + 1) * int64(Last.Y - First.Y + 1) * int64(Last.Z - First.Z + 1);
    TArray<FIntVector> Keys;
    if (RangeSections > Sections.Num())
    {
        for (const TPair<FIntVector, FSection>& Pair : Sections)
            if (Pair.Key.X >= First.X && Pair.Key.Y >= First.Y && Pair.Key.Z >= First.Z && Pair.Key.X <= Last.X &&
                Pair.Key.Y <= Last.Y && Pair.Key.Z <= Last.Z)
                Keys.Add(Pair.Key);
        Keys.Sort([](const FIntVector& A, const FIntVector& B)
            { return A.Z != B.Z ? A.Z < B.Z
                  : A.Y != B.Y  ? A.Y < B.Y
                                : A.X < B.X; });
    }
    else
    {
        for (int32 SZ = First.Z; SZ <= Last.Z; ++SZ)
            for (int32 SY = First.Y; SY <= Last.Y; ++SY)
                for (int32 SX = First.X; SX <= Last.X; ++SX)
                    Keys.Add(FIntVector(SX, SY, SZ));
    }
    for (const FIntVector& SectionKey : Keys)
    {
        const FSection* Section = Sections.Find(SectionKey);
        if (!Section || Section->Solid == 0) continue;
        const FIntVector Base = SectionKey * FoundationGrid::SectionCells;
        const FIntVector Low(FMath::Max(Min.X, Base.X), FMath::Max(Min.Y, Base.Y), FMath::Max(Min.Z, Base.Z));
        const FIntVector High(FMath::Min(Max.X, Base.X + FoundationGrid::SectionCells - 1),
            FMath::Min(Max.Y, Base.Y + FoundationGrid::SectionCells - 1),
            FMath::Min(Max.Z, Base.Z + FoundationGrid::SectionCells - 1));
        for (int32 Z = Low.Z; Z <= High.Z; ++Z)
            for (int32 Y = Low.Y; Y <= High.Y; ++Y)
                for (int32 X = Low.X; X <= High.X; ++X)
                {
                    const FIntVector Cell(X, Y, Z);
                    const FFoundationCell& Value = Section->Cells[IndexInSection(Cell)];
                    if (!Value.State.IsAir()) Visit(Cell, Value);
                }
    }
}

SIZE_T FFoundationSectionStorage::GetAllocatedSize() const
{
    SIZE_T Bytes = Sections.GetAllocatedSize();
    for (const auto& Pair : Sections)
        Bytes += Pair.Value.Cells.GetAllocatedSize();
    return Bytes;
}

FFoundationBlockWorld::FFoundationBlockWorld()
    : Registry(GetAirOnlyRegistry()), Storage(MakeUnique<FFoundationSectionStorage>())
{
}

void FFoundationBlockWorld::SetStorage(TUniquePtr<IFoundationBlockStorage> NewStorage)
{
    check(NewStorage.IsValid());
    Storage = MoveTemp(NewStorage);
}

bool FFoundationBlockWorld::IsKnown(const FIntVector& Cell) const
{
    return Cell.X >= BoundsMin.X && Cell.Y >= BoundsMin.Y && Cell.Z >= BoundsMin.Z && Cell.X <= BoundsMax.X &&
        Cell.Y <= BoundsMax.Y && Cell.Z <= BoundsMax.Z && Storage->IsLoaded(Cell);
}

FFoundationCell FFoundationBlockWorld::Get(const FIntVector& Cell) const
{
    return IsKnown(Cell) ? Storage->Get(Cell) : FFoundationCell{};
}

int64 FFoundationBlockWorld::Set(const FIntVector& Cell, FFoundationBlockState State)
{
    check(IsKnown(Cell));
    FFoundationBlockChange Change;
    Change.Cell = Cell;
    Change.Before = Storage->Get(Cell).State;
    Change.After = State;
    Change.Revision = ++Revision;
    Storage->Set(Cell, {State, Revision});
    Record(Change);
    // The hook may set other cells (placed lava meeting water): this change keeps its own revision.
    if (OnSet) OnSet(Cell, Change.Before, State);
    return Change.Revision;
}

void FFoundationBlockWorld::SetMany(TConstArrayView<TPair<FIntVector, FFoundationBlockState>> Cells)
{
    TArray<TPair<FIntVector, FFoundationCell>> Writes;
    TArray<FFoundationBlockChange> Committed;
    Writes.Reserve(Cells.Num());
    Committed.Reserve(Cells.Num());
    for (const TPair<FIntVector, FFoundationBlockState>& Cell : Cells)
    {
        check(IsKnown(Cell.Key));
        FFoundationBlockChange& Change = Committed.AddDefaulted_GetRef();
        Change.Cell = Cell.Key;
        Change.Before = Storage->Get(Cell.Key).State;
        Change.After = Cell.Value;
        Change.Revision = ++Revision;
        Writes.Add({Cell.Key, FFoundationCell{Cell.Value, Change.Revision}});
    }
    Storage->SetMany(Writes);
    for (const FFoundationBlockChange& Change : Committed)
        Record(Change);
    if (OnSet)
        for (const FFoundationBlockChange& Change : Committed)
            OnSet(Change.Cell, Change.Before, Change.After);
}

void FFoundationBlockWorld::Clear()
{
    const FIntVector Low = BoundsMin, High = BoundsMax;
    TArray<FIntVector> Solid;
    Storage->ForEachSolid(Low, High, [&Solid](const FIntVector& Cell, const FFoundationCell&) { Solid.Add(Cell); });
    // Removals are logged so presentation drops them; then storage (and old revisions) is released.
    ++Revision;
    for (const FIntVector& Cell : Solid)
        Record({Cell, Storage->Get(Cell).State, FFoundationBlockState{}, Revision});
    Storage->Empty();
}

void FoundationBlockOffsets::CopyTo(const FFoundationBlockType& Block, FVoxelStateInfo& Info)
{
    Info.Offset = Block.OffsetKind == EFoundationOffsetKind::XYZ ? EVoxelOffset::HorizontalAndDown
        : Block.OffsetKind == EFoundationOffsetKind::XZ          ? EVoxelOffset::Horizontal
                                                                 : EVoxelOffset::None;
    Info.OffsetHorizontal = Block.OffsetMaxHorizontal;
    Info.OffsetDown = Block.OffsetKind == EFoundationOffsetKind::XYZ ? Block.OffsetMinVertical : 0.f;
}

FVector FoundationBlockOffsets::GetOffset(const FFoundationBlockType& Block, const FIntVector& Cell)
{
    if (Block.OffsetKind == EFoundationOffsetKind::None) return FVector::ZeroVector;
    FVoxelStateInfo Info;
    CopyTo(Block, Info);
    return FVector(VoxelModels::GetOffset(Info, Cell.X, Cell.Y));
}

namespace
{
    // Where a state's unit boxes start in a cell, world cm: the cell's corner, moved when the shape moves.
    FVector GetShapeMin(const FFoundationBlockRegistry& Registry, const FIntVector& Cell, const FVector& OriginCm,
        FFoundationBlockState State, bool bCollision)
    {
        const FFoundationBlockType& Block = Registry.GetBlock(State);
        const bool bMoves = bCollision ? Block.bCollisionMovesWithOffset : Block.bOutlineMovesWithOffset;
        const FVector Min = FoundationGrid::CellMin(Cell, OriginCm);
        return bMoves ? Min + FoundationBlockOffsets::GetOffset(Block, Cell) * UFoundationProfile::BlockCm : Min;
    }

    bool AppendShapeBoxes(const FFoundationBlockRegistry& Registry, const FIntVector& Cell, const FVector& OriginCm,
        FFoundationBlockState State, bool bCollision, TArray<FBox>& Out)
    {
        static thread_local TArray<FBox> Units;
        if (bCollision)
            Registry.GetCollisionBoxes(State, Units);
        else
            Registry.GetOutlineBoxes(State, Units);
        const FVector Min = GetShapeMin(Registry, Cell, OriginCm, State, bCollision);
        for (const FBox& Unit : Units)
            Out.Add(FBox(Min + Unit.Min * UFoundationProfile::BlockCm, Min + Unit.Max * UFoundationProfile::BlockCm));
        return !Units.IsEmpty();
    }
}

bool FFoundationBlockWorld::GetSelectionBox(const FIntVector& Cell, FFoundationBlockState State, FBox& OutBox) const
{
    FBox Unit;
    if (!Registry->GetSelectionShape(State, Unit)) return false;
    const FVector Min = GetShapeMin(*Registry, Cell, OriginCm, State, false);
    OutBox = FBox(Min + Unit.Min * UFoundationProfile::BlockCm, Min + Unit.Max * UFoundationProfile::BlockCm);
    return true;
}

bool FFoundationBlockWorld::GetCollisionBox(const FIntVector& Cell, FFoundationBlockState State, FBox& OutBox) const
{
    FBox Unit;
    if (!Registry->GetCollisionShape(State, Unit)) return false;
    const FVector Min = GetShapeMin(*Registry, Cell, OriginCm, State, true);
    OutBox = FBox(Min + Unit.Min * UFoundationProfile::BlockCm, Min + Unit.Max * UFoundationProfile::BlockCm);
    return true;
}

bool FFoundationBlockWorld::GetOutlineBoxes(
    const FIntVector& Cell, FFoundationBlockState State, TArray<FBox>& Out) const
{
    return AppendShapeBoxes(*Registry, Cell, OriginCm, State, false, Out);
}

bool FFoundationBlockWorld::GetCollisionBoxes(
    const FIntVector& Cell, FFoundationBlockState State, TArray<FBox>& Out) const
{
    // Cubes, slabs and layers have their one box; a model state has its list.
    if (Registry->GetShape(State) != EFoundationBlockShape::Model)
    {
        FBox Box;
        if (!GetCollisionBox(Cell, State, Box)) return false;
        Out.Add(Box);
        return true;
    }
    return AppendShapeBoxes(*Registry, Cell, OriginCm, State, true, Out);
}

void FFoundationBlockWorld::GatherCollision(const FBox& Region, TArray<FBox>& Out) const
{
    // Cells whose unit box can touch the region; collision shapes stay inside their cell horizontally, moved ones too
    // (bamboo, dripstone and sulfur spike move theirs; Blocks.PlantShapes checks it). With foundation.Blocks.Shapes the
    // cells just below and above add the boxes that leave them vertically into the region.
    const FIntVector First = FoundationGrid::CellAt(Region.Min, OriginCm),
                     Last = FoundationGrid::CellAt(Region.Max, OriginCm);
    const int32 Outer = FoundationBlockFamilies::AreShapesEnabled() ? 1 : 0;
    const FIntVector Low(
        FMath::Max(First.X, BoundsMin.X), FMath::Max(First.Y, BoundsMin.Y), FMath::Max(First.Z - Outer, BoundsMin.Z));
    const FIntVector High(
        FMath::Min(Last.X, BoundsMax.X), FMath::Min(Last.Y, BoundsMax.Y), FMath::Min(Last.Z + Outer, BoundsMax.Z));
    Storage->ForEachSolid(Low, High,
        [this, &Out, &Region, &First, &Last](const FIntVector& Cell, const FFoundationCell& Value)
        {
            const int32 Before = Out.Num();
            GetCollisionBoxes(Cell, Value.State, Out);
            if (Cell.Z < First.Z || Cell.Z > Last.Z)
                FoundationCollisionGather::KeepLeaving(Region, FoundationGrid::CellMin(Cell, OriginCm).Z,
                    FoundationGrid::CellMin(Cell + FIntVector(0, 0, 1), OriginCm).Z, Out, Before);
        });
}

void FoundationCollisionGather::KeepLeaving(
    const FBox& Region, double CellBottom, double CellTop, TArray<FBox>& Out, int32 First)
{
    // Within 1e-3 cm of the cell counts as inside (the boxes are block fractions scaled to cm).
    constexpr double Tolerance = 1e-3;
    int32 Kept = First;
    for (int32 Index = First; Index < Out.Num(); ++Index)
    {
        const FBox& Box = Out[Index];
        const bool bLeaves = Box.Min.Z < CellBottom - Tolerance || Box.Max.Z > CellTop + Tolerance;
        const bool bOverlaps = Box.Min.X < Region.Max.X && Box.Max.X > Region.Min.X && Box.Min.Y < Region.Max.Y &&
            Box.Max.Y > Region.Min.Y && Box.Min.Z < Region.Max.Z && Box.Max.Z > Region.Min.Z;
        if (bLeaves && bOverlaps) Out[Kept++] = Box;
    }
    Out.SetNum(Kept, EAllowShrinking::No);
}

int32 FFoundationBlockWorld::GetSolidCount() const
{
    return Storage->GetSolidCount();
}

SIZE_T FFoundationBlockWorld::GetAllocatedSize() const
{
    return Storage->GetAllocatedSize() + Changes.GetAllocatedSize();
}

void FFoundationBlockWorld::Record(const FFoundationBlockChange& Change)
{
    if (Changes.Num() >= MaxPendingChanges)
    {
        Changes.Reset();
        bChangesOverflowed = true;
    }
    Changes.Add(Change);
}

TArray<FFoundationBlockChange> FFoundationBlockWorld::TakeChanges()
{
    TArray<FFoundationBlockChange> Result = MoveTemp(Changes);
    Changes.Reset();
    return Result;
}
