#include "Foundation/FoundationBlocks.h"

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

bool FFoundationBlockWorld::IsKnown(const FIntVector& Cell) const
{
    return Cell.X >= BoundsMin.X && Cell.Y >= BoundsMin.Y && Cell.Z >= BoundsMin.Z && Cell.X <= BoundsMax.X &&
        Cell.Y <= BoundsMax.Y && Cell.Z <= BoundsMax.Z;
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
    return Revision;
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

bool FFoundationBlockWorld::GetSelectionBox(const FIntVector& Cell, FFoundationBlockState State, FBox& OutBox) const
{
    FBox Unit;
    if (!Registry->GetSelectionShape(State, Unit)) return false;
    const FVector Min = FoundationGrid::CellMin(Cell, OriginCm);
    OutBox = FBox(Min + Unit.Min * UFoundationProfile::BlockCm, Min + Unit.Max * UFoundationProfile::BlockCm);
    return true;
}

bool FFoundationBlockWorld::GetCollisionBox(const FIntVector& Cell, FFoundationBlockState State, FBox& OutBox) const
{
    FBox Unit;
    if (!Registry->GetCollisionShape(State, Unit)) return false;
    const FVector Min = FoundationGrid::CellMin(Cell, OriginCm);
    OutBox = FBox(Min + Unit.Min * UFoundationProfile::BlockCm, Min + Unit.Max * UFoundationProfile::BlockCm);
    return true;
}

void FFoundationBlockWorld::GatherCollision(const FBox& Region, TArray<FBox>& Out) const
{
    // Cells whose unit box can touch the region; collision shapes stay inside their cell.
    FIntVector Low = FoundationGrid::CellAt(Region.Min, OriginCm), High = FoundationGrid::CellAt(Region.Max, OriginCm);
    Low = FIntVector(FMath::Max(Low.X, BoundsMin.X), FMath::Max(Low.Y, BoundsMin.Y), FMath::Max(Low.Z, BoundsMin.Z));
    High =
        FIntVector(FMath::Min(High.X, BoundsMax.X), FMath::Min(High.Y, BoundsMax.Y), FMath::Min(High.Z, BoundsMax.Z));
    Storage->ForEachSolid(Low, High,
        [this, &Out](const FIntVector& Cell, const FFoundationCell& Value)
        {
            FBox Box;
            if (GetCollisionBox(Cell, Value.State, Box)) Out.Add(Box);
        });
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
