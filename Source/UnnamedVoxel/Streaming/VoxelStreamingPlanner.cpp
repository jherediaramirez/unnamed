#include "Streaming/VoxelStreamingPlanner.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    // Columns between two columns' squares along one axis: 0 for the same or neighbouring columns.
    int64 GetStreamingAxisGap(int32 From, int32 To)
    {
        return FMath::Max<int64>(0, FMath::Abs(int64(To) - From) - 1);
    }

    bool IsStreamingColumnBefore(FIntPoint A, FIntPoint B)
    {
        return A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
    }

    // How far a row DY columns from the center reaches on X within Radius (a gap): columns up to that many columns
    // from the center's X lie within it. -1 when the row lies beyond it.
    int64 GetStreamingRowReach(int64 DY, int64 Radius)
    {
        const int64 GapY = FMath::Max<int64>(0, FMath::Abs(DY) - 1);
        const int64 Room = Radius * Radius - GapY * GapY;
        if (Room < 0) return -1;
        int64 GapX = int64(FMath::Sqrt(double(Room)));
        while (GapX * GapX > Room)
            --GapX;
        while ((GapX + 1) * (GapX + 1) <= Room)
            ++GapX;
        return GapX + 1;
    }
}

FVoxelStreamingRings::FVoxelStreamingRings(
    FIntPoint InCenter, int32 InViewRadius, int32 InUnloadMargin, int32 InStages, int32 InFarRadius)
    : Center(InCenter),
      ViewRadius(FMath::Max(0, InViewRadius)),
      Stages(FMath::Clamp(InStages, 1, MaxStages)),
      FarRadius(FMath::Max(0, InFarRadius))
{
    UnloadMargin = FMath::Max(GetMinUnloadMargin(Stages), InUnloadMargin);
}

int64 FVoxelStreamingRings::GetGapSquared(FIntPoint Column) const
{
    const int64 GapX = GetStreamingAxisGap(Center.X, Column.X), GapY = GetStreamingAxisGap(Center.Y, Column.Y);
    return GapX * GapX + GapY * GapY;
}

bool FVoxelStreamingRings::IsInView(FIntPoint Column) const
{
    return GetGapSquared(Column) <= int64(ViewRadius) * ViewRadius;
}

bool FVoxelStreamingRings::IsDrawn(FIntPoint Column) const
{
    const int64 Drawn = GetDrawnRadius();
    return GetGapSquared(Column) <= Drawn * Drawn;
}

bool FVoxelStreamingRings::IsNeeded(FIntPoint Column) const
{
    return GetRing(Column) != INDEX_NONE;
}

int32 FVoxelStreamingRings::GetRing(FIntPoint Column) const
{
    // The column with the smallest gap within Ring columns on both axes is Ring columns closer to the center on each.
    const int64 Drawn = GetDrawnRadius();
    const int64 AxisX = GetStreamingAxisGap(Center.X, Column.X), AxisY = GetStreamingAxisGap(Center.Y, Column.Y);
    for (int32 Ring = 0; Ring <= Stages; ++Ring)
    {
        const int64 GapX = FMath::Max<int64>(0, AxisX - Ring), GapY = FMath::Max<int64>(0, AxisY - Ring);
        if (GapX * GapX + GapY * GapY <= Drawn * Drawn) return Ring;
    }
    return INDEX_NONE;
}

bool FVoxelStreamingRings::IsBeyondUnload(FIntPoint Column) const
{
    const int64 Limit = GetUnloadDistance();
    return GetGapSquared(Column) > Limit * Limit;
}

int32 VoxelStreaming::GetViewSector(double YawDegrees)
{
    // Half a sector's shift centers sector 0 on +X.
    double Turns = FMath::Fmod(YawDegrees / 360. + 0.5 / ViewSectorCount, 1.);
    if (Turns < 0) Turns += 1.;
    return FMath::Clamp(int32(Turns * ViewSectorCount), 0, ViewSectorCount - 1);
}

FIntPoint VoxelStreaming::GetViewSectorDirection(int32 ViewSector)
{
    static const FIntPoint Directions[ViewSectorCount] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    return Directions[((ViewSector % ViewSectorCount) + ViewSectorCount) % ViewSectorCount];
}

void VoxelStreaming::GetNeededColumns(const FVoxelStreamingRings& Rings, int32 ViewSector, TArray<FIntPoint>& Out)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelStreaming_GetNeededColumns);
    struct FCandidate
    {
        int64 DistanceSquared;
        int64 Ahead;
        FIntPoint Column;
    };
    const FIntPoint Ahead = GetViewSectorDirection(ViewSector);
    const int32 Extent = Rings.GetNeededExtent();
    TArray<FCandidate> Candidates;
    Candidates.Reserve((2 * Extent + 1) * (2 * Extent + 1));
    for (int32 DY = -Extent; DY <= Extent; ++DY)
        for (int32 DX = -Extent; DX <= Extent; ++DX)
        {
            const FIntPoint Column = Rings.Center + FIntPoint(DX, DY);
            if (Rings.IsNeeded(Column))
                Candidates.Add({int64(DX) * DX + int64(DY) * DY, int64(DX) * Ahead.X + int64(DY) * Ahead.Y, Column});
        }
    // A total order (no two candidates share a column), so the result does not depend on the sort's stability.
    Candidates.Sort(
        [](const FCandidate& A, const FCandidate& B)
        {
            if (A.DistanceSquared != B.DistanceSquared) return A.DistanceSquared < B.DistanceSquared;
            if (A.Ahead != B.Ahead) return A.Ahead > B.Ahead;
            return IsStreamingColumnBefore(A.Column, B.Column);
        });
    Out.Reset(Candidates.Num());
    for (const FCandidate& Candidate : Candidates)
        Out.Add(Candidate.Column);
}

void VoxelStreaming::GetUnloads(
    const FVoxelStreamingRings& Rings, TConstArrayView<FIntPoint> Present, TArray<FIntPoint>& Out)
{
    Out.Reset();
    for (const FIntPoint Column : Present)
        if (Rings.IsBeyondUnload(Column)) Out.Add(Column);
    Out.Sort([](FIntPoint A, FIntPoint B) { return IsStreamingColumnBefore(A, B); });
}

void VoxelStreaming::Plan(
    const FVoxelStreamingRings& Rings, int32 ViewSector, const TSet<FIntPoint>& Present, FVoxelStreamingPlan& Out)
{
    TArray<FIntPoint> Needed;
    GetNeededColumns(Rings, ViewSector, Needed);
    Out.Loads.Reset();
    for (const FIntPoint Column : Needed)
        if (!Present.Contains(Column)) Out.Loads.Add(Column);
    GetUnloads(Rings, Present.Array(), Out.Unloads);
}

void VoxelStreaming::ForEachLeft(FIntPoint From, FIntPoint To, int32 Radius, TFunctionRef<void(FIntPoint)> Visit)
{
    const int64 Reach = FMath::Max(0, Radius);
    for (int64 Y = int64(From.Y) - Reach - 1; Y <= int64(From.Y) + Reach + 1; ++Y)
    {
        const int64 FromReach = GetStreamingRowReach(Y - From.Y, Reach);
        if (FromReach < 0) continue;
        // The part of the row To still reaches is skipped in one step.
        const int64 ToReach = GetStreamingRowReach(Y - To.Y, Reach);
        const int64 KeptMin = ToReach < 0 ? MAX_int64 : int64(To.X) - ToReach;
        const int64 KeptMax = ToReach < 0 ? MIN_int64 : int64(To.X) + ToReach;
        for (int64 X = int64(From.X) - FromReach; X <= int64(From.X) + FromReach; ++X)
        {
            if (X >= KeptMin && X <= KeptMax)
            {
                X = KeptMax;
                continue;
            }
            Visit(FIntPoint(int32(X), int32(Y)));
        }
    }
}

FVoxelNeededOrder::FVoxelNeededOrder(const FVoxelStreamingRings& Rings, int32 InViewSector)
    : ViewRadius(Rings.ViewRadius),
      FarRadius(Rings.FarRadius),
      Stages(Rings.Stages),
      ViewSector(InViewSector),
      Extent(Rings.GetNeededExtent())
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelStreaming_BuildNeededOrder);
    FVoxelStreamingRings AtOrigin = Rings;
    AtOrigin.Center = FIntPoint::ZeroValue;
    VoxelStreaming::GetNeededColumns(AtOrigin, ViewSector, Offsets);
    const int32 Side = 2 * Extent + 1;
    Indices.Init(INDEX_NONE, Side * Side);
    OffsetRings.SetNumUninitialized(Offsets.Num());
    for (int32 Index = 0; Index < Offsets.Num(); ++Index)
    {
        OffsetRings[Index] = uint8(AtOrigin.GetRing(Offsets[Index]));
        Indices[(Offsets[Index].Y + Extent) * Side + Offsets[Index].X + Extent] = Index;
    }
}

bool FVoxelNeededOrder::Matches(const FVoxelStreamingRings& Rings, int32 InViewSector) const
{
    return ViewRadius == Rings.ViewRadius && FarRadius == Rings.FarRadius && Stages == Rings.Stages &&
        ViewSector == InViewSector;
}

int32 FVoxelNeededOrder::FindIndex(FIntPoint Offset) const
{
    if (ViewRadius < 0 || FMath::Abs(int64(Offset.X)) > Extent || FMath::Abs(int64(Offset.Y)) > Extent)
        return INDEX_NONE;
    return Indices[(Offset.Y + Extent) * (2 * Extent + 1) + Offset.X + Extent];
}

void FVoxelNeededOrder::GetColumns(FIntPoint Center, TArray<FIntPoint>& Out) const
{
    Out.SetNumUninitialized(Offsets.Num());
    for (int32 Index = 0; Index < Offsets.Num(); ++Index)
        Out[Index] = Center + Offsets[Index];
}
