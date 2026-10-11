#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Streaming/VoxelStreamingPlanner.h"

namespace
{
    // The planner raises every unload margin to at least this (FVoxelStreamingRings::MinUnloadMargin). The test keeps
    // its own copy of the number, so changing the planner's constant fails section 5 until this follows it.
    constexpr int32 StreamingExpectedMinMargin = 2;
    constexpr double StreamingTwoPi = 6.283185307179586;

    // The rings as the test defines them, written without the planner. The gap along one axis between the center
    // column and a column Offset columns away: 0 for the column itself and its neighbours.
    int64 StreamingBruteGap(int64 Offset)
    {
        const int64 Distance = Offset < 0 ? -Offset : Offset;
        return Distance > 1 ? Distance - 1 : 0;
    }

    int64 StreamingBruteGapSquared(FIntPoint Center, FIntPoint Column)
    {
        const int64 GapX = StreamingBruteGap(int64(Column.X) - Center.X);
        const int64 GapY = StreamingBruteGap(int64(Column.Y) - Center.Y);
        return GapX * GapX + GapY * GapY;
    }

    bool StreamingBruteInView(FIntPoint Center, int32 Radius, FIntPoint Column)
    {
        return StreamingBruteGapSquared(Center, Column) <= int64(Radius) * Radius;
    }

    // Needed: the column or any of its 8 neighbours is in view.
    bool StreamingBruteNeeded(FIntPoint Center, int32 Radius, FIntPoint Column)
    {
        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                if (StreamingBruteInView(Center, Radius, FIntPoint(Column.X + OffsetX, Column.Y + OffsetY)))
                    return true;
        return false;
    }

    // Beyond unload: the squared gap exceeds (Radius + 1 + M)^2, past the generated ring by M, the margin but at
    // least 2.
    int64 StreamingBruteUnloadLimitSquared(int32 Radius, int32 Margin)
    {
        const int32 EffectiveMargin = Margin > StreamingExpectedMinMargin ? Margin : StreamingExpectedMinMargin;
        const int64 Limit = int64(Radius) + 1 + EffectiveMargin;
        return Limit * Limit;
    }

    bool StreamingBruteBeyondUnload(FIntPoint Center, int32 Radius, int32 Margin, FIntPoint Column)
    {
        return StreamingBruteGapSquared(Center, Column) > StreamingBruteUnloadLimitSquared(Radius, Margin);
    }

    // Every needed column, found by scanning a square comfortably wider than any needed column.
    TSet<FIntPoint> StreamingBruteNeededSet(FIntPoint Center, int32 Radius)
    {
        TSet<FIntPoint> Result;
        const int32 Half = Radius + 5;
        for (int32 OffsetY = -Half; OffsetY <= Half; ++OffsetY)
            for (int32 OffsetX = -Half; OffsetX <= Half; ++OffsetX)
            {
                const FIntPoint Column(Center.X + OffsetX, Center.Y + OffsetY);
                if (StreamingBruteNeeded(Center, Radius, Column)) Result.Add(Column);
            }
        return Result;
    }

    // The planner's count of columns in view, over the square of half-size ViewRadius + 5 around its center.
    int32 StreamingCountInView(const FVoxelStreamingRings& Rings)
    {
        const int32 Half = Rings.ViewRadius + 5;
        int32 Count = 0;
        for (int32 OffsetY = -Half; OffsetY <= Half; ++OffsetY)
            for (int32 OffsetX = -Half; OffsetX <= Half; ++OffsetX)
                if (Rings.IsInView(FIntPoint(Rings.Center.X + OffsetX, Rings.Center.Y + OffsetY))) ++Count;
        return Count;
    }

    // Row order: by Y, then X.
    bool StreamingBefore(FIntPoint A, FIntPoint B)
    {
        return A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
    }

    // A plan applied to the present columns: unloads leave, loads arrive.
    void StreamingApply(const FVoxelStreamingPlan& StepPlan, TSet<FIntPoint>& Present)
    {
        for (const FIntPoint Column : StepPlan.Unloads)
            Present.Remove(Column);
        for (const FIntPoint Column : StepPlan.Loads)
            Present.Add(Column);
    }

    FString StreamingText(FIntPoint Point)
    {
        return FString::Printf(TEXT("(%d,%d)"), Point.X, Point.Y);
    }

    // The first few columns of a list, for failure messages.
    FString StreamingList(TConstArrayView<FIntPoint> Columns)
    {
        if (Columns.Num() == 0) return TEXT("none");
        FString Text;
        for (int32 Index = 0; Index < Columns.Num() && Index < 4; ++Index)
        {
            if (Index > 0) Text += TEXT(" ");
            Text += StreamingText(Columns[Index]);
        }
        if (Columns.Num() > 4) Text += TEXT(" ...");
        return Text;
    }

    const TCHAR* StreamingYesNo(bool bValue)
    {
        return bValue ? TEXT("true") : TEXT("false");
    }

    // Counts the checks of a loop and keeps the first failure, so a broken rule reports once, not once per column.
    struct FStreamingTally
    {
        int32 Checked = 0;
        int32 Failed = 0;
        FString First;

        // Counts one check and returns whether it passed.
        bool Check(bool bPassed)
        {
            ++Checked;
            if (!bPassed) ++Failed;
            return bPassed;
        }

        // Keeps the description of the first failure.
        void Describe(const FString& Description)
        {
            if (First.IsEmpty()) First = Description;
        }
    };

    // Reports a tally as one error that names the first failure, or as an error when nothing was checked at all.
    void StreamingReport(FAutomationTestBase& Test, const FString& Rule, const FStreamingTally& Tally)
    {
        if (Tally.Checked == 0)
            Test.AddError(FString::Printf(TEXT("%s: nothing was checked"), *Rule));
        else if (Tally.Failed > 0)
            Test.AddError(FString::Printf(
                TEXT("%s: %d of %d checks failed, first: %s"), *Rule, Tally.Failed, Tally.Checked, *Tally.First));
    }

    // Checks that Actual holds each column of Expected exactly once and nothing else.
    void StreamingExpectColumns(FAutomationTestBase& Test, const FString& Rule, TConstArrayView<FIntPoint> Actual,
        const TSet<FIntPoint>& Expected)
    {
        FStreamingTally Tally;
        if (!Tally.Check(Actual.Num() == Expected.Num()))
            Tally.Describe(FString::Printf(TEXT("%d columns listed, %d expected, listed: %s"), Actual.Num(),
                Expected.Num(), *StreamingList(Actual)));
        TSet<FIntPoint> Seen;
        for (const FIntPoint Column : Actual)
        {
            bool bDuplicate = false;
            Seen.Add(Column, &bDuplicate);
            if (!Tally.Check(!bDuplicate))
                Tally.Describe(FString::Printf(TEXT("column %s is listed twice"), *StreamingText(Column)));
            if (!Tally.Check(Expected.Contains(Column)))
                Tally.Describe(FString::Printf(TEXT("column %s is listed but not expected"), *StreamingText(Column)));
        }
        for (const FIntPoint Column : Expected)
            if (!Tally.Check(Seen.Contains(Column)))
                Tally.Describe(FString::Printf(TEXT("column %s is expected but missing"), *StreamingText(Column)));
        StreamingReport(Test, Rule, Tally);
    }

    // Describes how Values differ from "each integer in [-Extent, Extent] exactly once"; empty when they match.
    FString StreamingDescribeEachOnce(TConstArrayView<int32> Values, int32 Extent)
    {
        TMap<int32, int32> Counts;
        for (const int32 Value : Values)
            ++Counts.FindOrAdd(Value);
        for (const TPair<int32, int32>& Pair : Counts)
            if (Pair.Key < -Extent || Pair.Key > Extent)
                return FString::Printf(TEXT("%d is present but lies outside [%d, %d]"), Pair.Key, -Extent, Extent);
        for (int32 Value = -Extent; Value <= Extent; ++Value)
        {
            const int32* Count = Counts.Find(Value);
            const int32 Times = Count ? *Count : 0;
            if (Times != 1) return FString::Printf(TEXT("%d appears %d times"), Value, Times);
        }
        return FString();
    }

    // Far LOD: a column's ring with Stages stage rings around the columns drawn (gap <= Drawn): 0 drawn, otherwise the
    // smallest Ring such that a drawn column lies within Ring columns of it on both axes (a dilation of the drawn set);
    // INDEX_NONE past Stages.
    int32 StreamingBruteRing(FIntPoint Center, int32 Drawn, int32 Stages, FIntPoint Column)
    {
        for (int32 Ring = 0; Ring <= Stages; ++Ring)
            for (int32 OffsetY = -Ring; OffsetY <= Ring; ++OffsetY)
                for (int32 OffsetX = -Ring; OffsetX <= Ring; ++OffsetX)
                    if (StreamingBruteInView(Center, Drawn, FIntPoint(Column.X + OffsetX, Column.Y + OffsetY)))
                        return Ring;
        return INDEX_NONE;
    }

    // Checks that Actual lists exactly Expected's columns in Expected's order.
    void StreamingExpectSameOrder(FAutomationTestBase& Test, const FString& Rule, TConstArrayView<FIntPoint> Actual,
        TConstArrayView<FIntPoint> Expected)
    {
        FStreamingTally Tally;
        if (!Tally.Check(Actual.Num() == Expected.Num()))
            Tally.Describe(FString::Printf(TEXT("%d columns listed, %d expected"), Actual.Num(), Expected.Num()));
        for (int32 Index = 0; Index < Actual.Num() && Index < Expected.Num(); ++Index)
            if (!Tally.Check(Actual[Index] == Expected[Index]))
                Tally.Describe(FString::Printf(TEXT("position %d holds %s, expected %s"), Index,
                    *StreamingText(Actual[Index]), *StreamingText(Expected[Index])));
        StreamingReport(Test, Rule, Tally);
    }
}

// The streaming planner against brute-force definitions written in this file. Failures in loops report once, naming
// the radius, the center and the first column that broke the rule.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelStreamingPlannerTest, "Unnamed.Voxel.Streaming.Planner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelStreamingPlannerTest::RunTest(const FString& Parameters)
{
    // 1. Counts at the origin, and the planner against the brute force for every scanned column.
    {
        struct FCountCase
        {
            int32 Radius, InView, Needed;
        };
        for (const FCountCase Case :
            {FCountCase{0, 9, 25}, FCountCase{1, 21, 45}, FCountCase{4, 89, 137}, FCountCase{12, 545, 657}})
        {
            const FIntPoint Center(0, 0);
            const FVoxelStreamingRings Rings(Center, Case.Radius, 2);
            const FString Where = FString::Printf(TEXT("R %d at %s"), Case.Radius, *StreamingText(Center));
            const int32 Half = Case.Radius + 5;
            const int32 Extent = Rings.GetNeededExtent();
            int32 PlannerInView = 0, PlannerNeeded = 0, BruteInView = 0, BruteNeeded = 0;
            FStreamingTally InViewTally, NeededTally, ExtentTally;
            for (int32 Y = -Half; Y <= Half; ++Y)
                for (int32 X = -Half; X <= Half; ++X)
                {
                    const FIntPoint Column(X, Y);
                    const bool bBruteInView = StreamingBruteInView(Center, Case.Radius, Column);
                    const bool bBruteNeeded = StreamingBruteNeeded(Center, Case.Radius, Column);
                    const bool bInView = Rings.IsInView(Column);
                    const bool bNeeded = Rings.IsNeeded(Column);
                    BruteInView += bBruteInView ? 1 : 0;
                    BruteNeeded += bBruteNeeded ? 1 : 0;
                    PlannerInView += bInView ? 1 : 0;
                    PlannerNeeded += bNeeded ? 1 : 0;
                    if (!InViewTally.Check(bInView == bBruteInView))
                        InViewTally.Describe(FString::Printf(TEXT("column %s: IsInView is %s, expected %s"),
                            *StreamingText(Column), StreamingYesNo(bInView), StreamingYesNo(bBruteInView)));
                    if (!NeededTally.Check(bNeeded == bBruteNeeded))
                        NeededTally.Describe(FString::Printf(TEXT("column %s: IsNeeded is %s, expected %s"),
                            *StreamingText(Column), StreamingYesNo(bNeeded), StreamingYesNo(bBruteNeeded)));
                    if (bBruteNeeded && !ExtentTally.Check(FMath::Abs(X) <= Extent && FMath::Abs(Y) <= Extent))
                        ExtentTally.Describe(FString::Printf(
                            TEXT("needed column %s lies beyond the extent %d"), *StreamingText(Column), Extent));
                }
            TestEqual(FString::Printf(TEXT("%s: columns in view"), *Where), PlannerInView, Case.InView);
            TestEqual(FString::Printf(TEXT("%s: columns needed"), *Where), PlannerNeeded, Case.Needed);
            TestEqual(FString::Printf(TEXT("%s: columns in view by the test's own definition"), *Where), BruteInView,
                Case.InView);
            TestEqual(FString::Printf(TEXT("%s: columns needed by the test's own definition"), *Where), BruteNeeded,
                Case.Needed);
            StreamingReport(*this, Where + TEXT(": IsInView against the brute force"), InViewTally);
            StreamingReport(*this, Where + TEXT(": IsNeeded against the brute force"), NeededTally);
            StreamingReport(*this, Where + TEXT(": GetNeededExtent bounds every needed column"), ExtentTally);

            TArray<FIntPoint> NeededColumns;
            VoxelStreaming::GetNeededColumns(Rings, 0, NeededColumns);
            StreamingExpectColumns(
                *this, Where + TEXT(": GetNeededColumns"), NeededColumns, StreamingBruteNeededSet(Center, Case.Radius));
        }
    }

    // 2. Coverage: wherever the camera stands in the center column, the column holding every point R * 16 - 0.001
    // blocks away from it is in view, so a fog that ends R * 16 blocks away never shows a missing column.
    {
        const double Samples[] = {0.0, 4.0, 8.0, 12.0, 15.999};
        for (const int32 Radius : {1, 4, 12})
        {
            const FVoxelStreamingRings Rings(FIntPoint(0, 0), Radius, 2);
            const double Reach = Radius * 16.0 - 0.001;
            FStreamingTally Tally;
            for (const double CameraX : Samples)
                for (const double CameraY : Samples)
                    for (int32 AngleIndex = 0; AngleIndex < 720; ++AngleIndex)
                    {
                        const double Angle = StreamingTwoPi * AngleIndex / 720.0;
                        const double PointX = CameraX + Reach * FMath::Cos(Angle);
                        const double PointY = CameraY + Reach * FMath::Sin(Angle);
                        const FIntPoint Column(FMath::FloorToInt32(PointX / 16.0), FMath::FloorToInt32(PointY / 16.0));
                        if (!Tally.Check(Rings.IsInView(Column)))
                            Tally.Describe(FString::Printf(
                                TEXT("camera at (%.3f, %.3f), %.1f degrees: the point (%.3f, %.3f) lies in column %s"),
                                CameraX, CameraY, AngleIndex * 0.5, PointX, PointY, *StreamingText(Column)));
                    }
            const FString Where =
                FString::Printf(TEXT("R %d, camera in column %s"), Radius, *StreamingText(Rings.Center));
            StreamingReport(*this, Where + TEXT(": the column R*16 - 0.001 blocks away is in view"), Tally);
        }
    }

    // 3. The priority order of GetNeededColumns in all 8 view sectors.
    {
        for (const int32 Radius : {1, 4, 12})
        {
            const FIntPoint Center(0, 0);
            const FVoxelStreamingRings Rings(Center, Radius, 2);
            const int32 NeededCount = StreamingBruteNeededSet(Center, Radius).Num();
            for (int32 Sector = 0; Sector < VoxelStreaming::ViewSectorCount; ++Sector)
            {
                const FIntPoint Ahead = VoxelStreaming::GetViewSectorDirection(Sector);
                const FString Where =
                    FString::Printf(TEXT("R %d, sector %d at %s"), Radius, Sector, *StreamingText(Center));
                TArray<FIntPoint> Order, Again;
                VoxelStreaming::GetNeededColumns(Rings, Sector, Order);
                VoxelStreaming::GetNeededColumns(Rings, Sector, Again);
                TestEqual(Where + TEXT(": number of columns"), Order.Num(), NeededCount);
                TestTrue(Where + TEXT(": two calls give identical arrays"), Order == Again);

                FStreamingTally DistanceTally, AheadTally, RowTally;
                for (int32 Index = 1; Index < Order.Num(); ++Index)
                {
                    const FIntPoint Earlier = Order[Index - 1] - Center, Later = Order[Index] - Center;
                    const int64 EarlierDistance = int64(Earlier.X) * Earlier.X + int64(Earlier.Y) * Earlier.Y;
                    const int64 LaterDistance = int64(Later.X) * Later.X + int64(Later.Y) * Later.Y;
                    const int64 EarlierAhead = int64(Earlier.X) * Ahead.X + int64(Earlier.Y) * Ahead.Y;
                    const int64 LaterAhead = int64(Later.X) * Ahead.X + int64(Later.Y) * Ahead.Y;
                    if (!DistanceTally.Check(EarlierDistance <= LaterDistance))
                        DistanceTally.Describe(
                            FString::Printf(TEXT("%s (distance squared %lld) is listed before %s (%lld)"),
                                *StreamingText(Order[Index - 1]), EarlierDistance, *StreamingText(Order[Index]),
                                LaterDistance));
                    if (EarlierDistance == LaterDistance && !AheadTally.Check(EarlierAhead >= LaterAhead))
                        AheadTally.Describe(FString::Printf(TEXT("%s (ahead %lld) is listed before %s (ahead %lld)"),
                            *StreamingText(Order[Index - 1]), EarlierAhead, *StreamingText(Order[Index]), LaterAhead));
                    if (EarlierDistance == LaterDistance && EarlierAhead == LaterAhead &&
                        !RowTally.Check(StreamingBefore(Earlier, Later)))
                        RowTally.Describe(FString::Printf(TEXT("%s is listed before %s, which is not Y then X order"),
                            *StreamingText(Order[Index - 1]), *StreamingText(Order[Index])));
                }
                StreamingReport(*this, Where + TEXT(": squared distance never decreases"), DistanceTally);
                StreamingReport(*this, Where + TEXT(": ahead never increases at equal distance"), AheadTally);
                StreamingReport(
                    *this, Where + TEXT(": Y then X strictly increases at equal distance and ahead"), RowTally);
            }

            // The sector settles ties between opposite columns.
            struct FTieCase
            {
                int32 Sector;
                FIntPoint First, Second;
            };
            for (const FTieCase Opposite : {FTieCase{0, FIntPoint(3, 0), FIntPoint(-3, 0)},
                     FTieCase{4, FIntPoint(-3, 0), FIntPoint(3, 0)}, FTieCase{2, FIntPoint(0, 3), FIntPoint(0, -3)}})
            {
                TArray<FIntPoint> Order;
                VoxelStreaming::GetNeededColumns(Rings, Opposite.Sector, Order);
                const FIntPoint FirstColumn = Center + Opposite.First, SecondColumn = Center + Opposite.Second;
                const int32 FirstIndex = Order.Find(FirstColumn);
                const int32 SecondIndex = Order.Find(SecondColumn);
                const FString TieWhere =
                    FString::Printf(TEXT("R %d, sector %d at %s"), Radius, Opposite.Sector, *StreamingText(Center));
                if (FirstIndex == INDEX_NONE || SecondIndex == INDEX_NONE)
                    AddError(FString::Printf(TEXT("%s: columns %s and %s are not both listed"), *TieWhere,
                        *StreamingText(FirstColumn), *StreamingText(SecondColumn)));
                else
                    TestTrue(FString::Printf(TEXT("%s: column %s (at %d) is listed before %s (at %d)"), *TieWhere,
                                 *StreamingText(FirstColumn), FirstIndex, *StreamingText(SecondColumn), SecondIndex),
                        FirstIndex < SecondIndex);
            }
        }
    }

    // 4. Yaw in degrees to view sector, and the sector directions.
    {
        struct FYawCase
        {
            double Degrees;
            int32 Sector;
        };
        for (const FYawCase Case : {FYawCase{0., 0}, FYawCase{22.4, 0}, FYawCase{22.6, 1}, FYawCase{45., 1},
                 FYawCase{90., 2}, FYawCase{180., 4}, FYawCase{270., 6}, FYawCase{-90., 6}, FYawCase{337.4, 7},
                 FYawCase{337.6, 0}, FYawCase{359., 0}, FYawCase{405., 1}, FYawCase{720., 0}, FYawCase{-720., 0}})
            TestEqual(FString::Printf(TEXT("GetViewSector(%.1f degrees)"), Case.Degrees),
                VoxelStreaming::GetViewSector(Case.Degrees), Case.Sector);

        const FIntPoint Directions[VoxelStreaming::ViewSectorCount] = {FIntPoint(1, 0), FIntPoint(1, 1),
            FIntPoint(0, 1), FIntPoint(-1, 1), FIntPoint(-1, 0), FIntPoint(-1, -1), FIntPoint(0, -1), FIntPoint(1, -1)};
        for (int32 Sector = 0; Sector < VoxelStreaming::ViewSectorCount; ++Sector)
        {
            const FIntPoint Direction = VoxelStreaming::GetViewSectorDirection(Sector);
            TestTrue(FString::Printf(TEXT("GetViewSectorDirection(%d) is %s, expected %s"), Sector,
                         *StreamingText(Direction), *StreamingText(Directions[Sector])),
                Direction == Directions[Sector]);
        }

        // Sectors outside 0..7 wrap around.
        struct FWrapCase
        {
            int32 Sector;
            FIntPoint Direction;
        };
        for (const FWrapCase Wrap : {FWrapCase{-1, FIntPoint(1, -1)}, FWrapCase{8, FIntPoint(1, 0)}})
        {
            const FIntPoint Direction = VoxelStreaming::GetViewSectorDirection(Wrap.Sector);
            TestTrue(FString::Printf(TEXT("GetViewSectorDirection(%d) is %s, expected %s"), Wrap.Sector,
                         *StreamingText(Direction), *StreamingText(Wrap.Direction)),
                Direction == Wrap.Direction);
        }
    }

    // 5. The constructor's clamps, and no needed column is ever beyond the unload distance.
    {
        TestEqual(TEXT("MinUnloadMargin"), FVoxelStreamingRings::MinUnloadMargin, StreamingExpectedMinMargin);
        for (const int32 Margin : {-1, 0, 1})
            TestEqual(FString::Printf(TEXT("Margin %d is raised to the minimum"), Margin),
                FVoxelStreamingRings(FIntPoint(2, 3), 6, Margin).UnloadMargin, StreamingExpectedMinMargin);
        for (const int32 Margin : {5, 9})
            TestEqual(FString::Printf(TEXT("Margin %d stays"), Margin),
                FVoxelStreamingRings(FIntPoint(2, 3), 6, Margin).UnloadMargin, Margin);
        for (const int32 Radius : {-100, -1})
            TestEqual(FString::Printf(TEXT("Radius %d becomes 0"), Radius),
                FVoxelStreamingRings(FIntPoint(2, 3), Radius, 5).ViewRadius, 0);
        for (const int32 Radius : {0, 1, 12})
            TestEqual(FString::Printf(TEXT("Radius %d stays"), Radius),
                FVoxelStreamingRings(FIntPoint(2, 3), Radius, 5).ViewRadius, Radius);
        TestTrue(TEXT("The constructor keeps the center"),
            FVoxelStreamingRings(FIntPoint(2, 3), 6, 5).Center == FIntPoint(2, 3));

        for (const int32 Radius : {0, 1, 4, 12})
            for (const int32 Margin : {0, 2, 5})
            {
                const FIntPoint Center(0, 0);
                const FVoxelStreamingRings Rings(Center, Radius, Margin);
                const FString Where =
                    FString::Printf(TEXT("R %d, margin %d at %s"), Radius, Margin, *StreamingText(Center));
                const int32 Half = Radius + Margin + 6;
                FStreamingTally NeededTally, UnloadTally;
                for (int32 Y = -Half; Y <= Half; ++Y)
                    for (int32 X = -Half; X <= Half; ++X)
                    {
                        const FIntPoint Column(X, Y);
                        const bool bBeyond = Rings.IsBeyondUnload(Column);
                        const bool bBruteBeyond = StreamingBruteBeyondUnload(Center, Radius, Margin, Column);
                        if (!UnloadTally.Check(bBeyond == bBruteBeyond))
                            UnloadTally.Describe(FString::Printf(TEXT("column %s: IsBeyondUnload is %s, expected %s"),
                                *StreamingText(Column), StreamingYesNo(bBeyond), StreamingYesNo(bBruteBeyond)));
                        if ((Rings.IsNeeded(Column) || StreamingBruteNeeded(Center, Radius, Column)) &&
                            !NeededTally.Check(!bBeyond))
                            NeededTally.Describe(FString::Printf(
                                TEXT("needed column %s is beyond the unload distance"), *StreamingText(Column)));
                    }
                StreamingReport(*this, Where + TEXT(": IsBeyondUnload against the brute force"), UnloadTally);
                StreamingReport(*this, Where + TEXT(": no needed column is beyond the unload distance"), NeededTally);
            }
    }

    // 6. Hysteresis: after the first move, going back and forth across a column border loads and unloads nothing.
    {
        const int32 Radius = 12, Margin = 2;
        struct FMoveCase
        {
            FIntPoint Moved;
            int32 Sector;
        };
        for (const FMoveCase Hop : {FMoveCase{FIntPoint(1, 0), 0}, FMoveCase{FIntPoint(1, 1), 1}})
        {
            const FIntPoint Origin(0, 0);
            const FString Where = FString::Printf(TEXT("R %d, margin %d, toggling %s and %s"), Radius, Margin,
                *StreamingText(Origin), *StreamingText(Hop.Moved));
            TSet<FIntPoint> Present = StreamingBruteNeededSet(Origin, Radius);

            // The first move loads the columns that became needed and unloads nothing.
            FVoxelStreamingPlan FirstPlan;
            VoxelStreaming::Plan(FVoxelStreamingRings(Hop.Moved, Radius, Margin), Hop.Sector, Present, FirstPlan);
            TSet<FIntPoint> ExpectedLoads;
            for (const FIntPoint Column : StreamingBruteNeededSet(Hop.Moved, Radius))
                if (!Present.Contains(Column)) ExpectedLoads.Add(Column);
            TestTrue(Where + TEXT(": the first move has columns to load"), ExpectedLoads.Num() > 0);
            StreamingExpectColumns(*this, Where + TEXT(": loads of the first move"), FirstPlan.Loads, ExpectedLoads);
            if (FirstPlan.Unloads.Num() > 0)
            {
                const FIntPoint FirstUnload = FirstPlan.Unloads[0];
                FString Message = FString::Printf(TEXT("%s: the first move unloads %d columns: %s."), *Where,
                    FirstPlan.Unloads.Num(), *StreamingList(FirstPlan.Unloads));
                Message += FString::Printf(TEXT(" Column %s has squared gap %lld from %s, and the limit is %lld."),
                    *StreamingText(FirstUnload), StreamingBruteGapSquared(Hop.Moved, FirstUnload),
                    *StreamingText(Hop.Moved), StreamingBruteUnloadLimitSquared(Radius, Margin));
                AddError(Message);
            }
            StreamingApply(FirstPlan, Present);

            // Ten toggles, each applied, back and forth between the two centers.
            FStreamingTally QuietTally;
            for (int32 Toggle = 1; Toggle <= 10; ++Toggle)
            {
                const FIntPoint ToggleCenter = Toggle % 2 == 1 ? Origin : Hop.Moved;
                FVoxelStreamingPlan TogglePlan;
                VoxelStreaming::Plan(
                    FVoxelStreamingRings(ToggleCenter, Radius, Margin), Hop.Sector, Present, TogglePlan);
                if (!QuietTally.Check(TogglePlan.Loads.Num() == 0 && TogglePlan.Unloads.Num() == 0))
                    QuietTally.Describe(FString::Printf(TEXT("toggle %d to %s: %d loads (%s), %d unloads (%s)"), Toggle,
                        *StreamingText(ToggleCenter), TogglePlan.Loads.Num(), *StreamingList(TogglePlan.Loads),
                        TogglePlan.Unloads.Num(), *StreamingList(TogglePlan.Unloads)));
                StreamingApply(TogglePlan, Present);
            }
            StreamingReport(*this, Where + TEXT(": every plan after the first move is empty"), QuietTally);
        }
    }

    // 7. GetUnloads returns exactly the columns beyond the unload distance, sorted by Y then X.
    {
        const int32 Radius = 12, Margin = 2, Half = 30;
        const FIntPoint Center(0, 0);
        const FString Where = FString::Printf(TEXT("R %d, margin %d at %s"), Radius, Margin, *StreamingText(Center));
        // Present is in descending order on purpose, so the planner has to sort the result.
        TArray<FIntPoint> PresentColumns, Expected;
        for (int32 X = Half; X >= -Half; --X)
            for (int32 Y = Half; Y >= -Half; --Y)
                PresentColumns.Add(FIntPoint(X, Y));
        // Scanning Y, then X, produces the expected order directly.
        for (int32 Y = -Half; Y <= Half; ++Y)
            for (int32 X = -Half; X <= Half; ++X)
                if (StreamingBruteBeyondUnload(Center, Radius, Margin, FIntPoint(X, Y))) Expected.Add(FIntPoint(X, Y));

        TArray<FIntPoint> Unloads;
        VoxelStreaming::GetUnloads(FVoxelStreamingRings(Center, Radius, Margin), PresentColumns, Unloads);
        TestTrue(Where + TEXT(": some present columns are beyond the unload distance"), Expected.Num() > 0);
        TestEqual(Where + TEXT(": number of unloads"), Unloads.Num(), Expected.Num());
        FStreamingTally Tally;
        for (int32 Index = 0; Index < Unloads.Num() && Index < Expected.Num(); ++Index)
            if (!Tally.Check(Unloads[Index] == Expected[Index]))
                Tally.Describe(FString::Printf(TEXT("position %d holds %s, expected %s"), Index,
                    *StreamingText(Unloads[Index]), *StreamingText(Expected[Index])));
        StreamingReport(*this, Where + TEXT(": unloads are the beyond-unload columns, sorted by Y then X"), Tally);
    }

    // 8. Steady state: one more step of a straight walk loads and unloads one column in each row.
    {
        const int32 Radius = 12, Margin = 2;
        const int32 Extent = Radius + 2;
        struct FWalkCase
        {
            FIntPoint Direction;
            int32 Sector;
        };
        for (const FWalkCase Walk : {FWalkCase{FIntPoint(1, 0), 0}, FWalkCase{FIntPoint(0, -1), 6}})
        {
            const FString Where =
                FString::Printf(TEXT("R %d, margin %d, walking %s"), Radius, Margin, *StreamingText(Walk.Direction));
            TSet<FIntPoint> Present = StreamingBruteNeededSet(FIntPoint(0, 0), Radius);
            for (int32 Step = 1; Step <= 40; ++Step)
            {
                const FVoxelStreamingRings StepRings(
                    FIntPoint(Walk.Direction.X * Step, Walk.Direction.Y * Step), Radius, Margin);
                FVoxelStreamingPlan StepPlan;
                VoxelStreaming::Plan(StepRings, Walk.Sector, Present, StepPlan);
                StreamingApply(StepPlan, Present);
            }

            const FIntPoint FinalCenter(Walk.Direction.X * 41, Walk.Direction.Y * 41);
            FVoxelStreamingPlan FinalPlan;
            VoxelStreaming::Plan(FVoxelStreamingRings(FinalCenter, Radius, Margin), Walk.Sector, Present, FinalPlan);
            const FString Step41 = Where + TEXT(", step 41 to ") + StreamingText(FinalCenter);
            TestEqual(
                Step41 + TEXT(": loads and unloads match in number"), FinalPlan.Loads.Num(), FinalPlan.Unloads.Num());

            // The coordinate across the walk: Y when walking along X, X when walking along Y.
            TArray<int32> LoadRows, UnloadRows;
            for (const FIntPoint Column : FinalPlan.Loads)
                LoadRows.Add(Walk.Direction.X != 0 ? Column.Y : Column.X);
            for (const FIntPoint Column : FinalPlan.Unloads)
                UnloadRows.Add(Walk.Direction.X != 0 ? Column.Y : Column.X);
            const FString LoadProblem = StreamingDescribeEachOnce(LoadRows, Extent);
            if (!LoadProblem.IsEmpty())
                AddError(FString::Printf(TEXT("%s: loads should hold each of -%d..%d once, but %s (loads: %s)"),
                    *Step41, Extent, Extent, *LoadProblem, *StreamingList(FinalPlan.Loads)));
            const FString UnloadProblem = StreamingDescribeEachOnce(UnloadRows, Extent);
            if (!UnloadProblem.IsEmpty())
                AddError(FString::Printf(TEXT("%s: unloads should hold each of -%d..%d once, but %s (unloads: %s)"),
                    *Step41, Extent, Extent, *UnloadProblem, *StreamingList(FinalPlan.Unloads)));
        }
    }

    // 9. A smaller view radius unloads the surplus and loads nothing; the larger radius loads it back.
    {
        const int32 LargeRadius = 12, SmallRadius = 4, Margin = 2;
        const FIntPoint Center(0, 0);
        const FString Where = FString::Printf(
            TEXT("R %d to %d and back, margin %d at %s"), LargeRadius, SmallRadius, Margin, *StreamingText(Center));
        TSet<FIntPoint> Present = StreamingBruteNeededSet(Center, LargeRadius);

        FVoxelStreamingPlan ShrinkPlan;
        VoxelStreaming::Plan(FVoxelStreamingRings(Center, SmallRadius, Margin), 0, Present, ShrinkPlan);
        TestEqual(Where + TEXT(": the smaller radius loads nothing, loads: ") + StreamingList(ShrinkPlan.Loads),
            ShrinkPlan.Loads.Num(), 0);
        TSet<FIntPoint> ExpectedUnloads;
        for (const FIntPoint Column : Present)
            if (StreamingBruteBeyondUnload(Center, SmallRadius, Margin, Column)) ExpectedUnloads.Add(Column);
        TestTrue(Where + TEXT(": some present columns are beyond the smaller radius"), ExpectedUnloads.Num() > 0);
        StreamingExpectColumns(
            *this, Where + TEXT(": unloads of the smaller radius"), ShrinkPlan.Unloads, ExpectedUnloads);
        StreamingApply(ShrinkPlan, Present);

        FVoxelStreamingPlan GrowPlan;
        VoxelStreaming::Plan(FVoxelStreamingRings(Center, LargeRadius, Margin), 0, Present, GrowPlan);
        TestEqual(Where + TEXT(": the larger radius unloads nothing, unloads: ") + StreamingList(GrowPlan.Unloads),
            GrowPlan.Unloads.Num(), 0);
        TSet<FIntPoint> ExpectedLoads;
        for (const FIntPoint Column : StreamingBruteNeededSet(Center, LargeRadius))
            if (!Present.Contains(Column)) ExpectedLoads.Add(Column);
        TestTrue(Where + TEXT(": the larger radius has columns to load back"), ExpectedLoads.Num() > 0);
        StreamingExpectColumns(*this, Where + TEXT(": loads of the larger radius"), GrowPlan.Loads, ExpectedLoads);
    }

    // 10. Far and negative centers give the origin's list and in-view count, translated by the center.
    {
        const int32 Sector = 3;
        for (const int32 Radius : {1, 4, 12})
        {
            const FVoxelStreamingRings OriginRings(FIntPoint(0, 0), Radius, 2);
            TArray<FIntPoint> OriginOrder;
            VoxelStreaming::GetNeededColumns(OriginRings, Sector, OriginOrder);
            const int32 OriginInView = StreamingCountInView(OriginRings);
            for (const FIntPoint Center :
                {FIntPoint(-5, -7), FIntPoint(1875000, -1875000), FIntPoint(-1875000, 1875000)})
            {
                const FVoxelStreamingRings Rings(Center, Radius, 2);
                const FString Where =
                    FString::Printf(TEXT("R %d, sector %d at %s"), Radius, Sector, *StreamingText(Center));
                TArray<FIntPoint> Order;
                VoxelStreaming::GetNeededColumns(Rings, Sector, Order);
                TestEqual(Where + TEXT(": number of columns"), Order.Num(), OriginOrder.Num());
                FStreamingTally Tally;
                for (int32 Index = 0; Index < Order.Num() && Index < OriginOrder.Num(); ++Index)
                    if (!Tally.Check(Order[Index] == OriginOrder[Index] + Center))
                        Tally.Describe(FString::Printf(TEXT("position %d holds %s, expected %s"), Index,
                            *StreamingText(Order[Index]), *StreamingText(OriginOrder[Index] + Center)));
                StreamingReport(*this, Where + TEXT(": the origin's list translated by the center, same order"), Tally);
                TestEqual(Where + TEXT(": columns in view"), StreamingCountInView(Rings), OriginInView);
            }
        }
    }

    // 11. Far LOD: with a far radius past the view, the columns drawn (gap <= D = max(ViewRadius, FarRadius)) are ring
    // 0, the stage rings grow from them and the view is still gap <= ViewRadius. The planner against the brute force
    // for every scanned column; the counts are pinned from the brute force (scripted outside the engine), and the
    // unload distance is D + Stages + the margin (2, raised to 3 with 3 stages).
    {
        struct FFarCase
        {
            int32 ViewRadius, FarRadius, Stages, InView, Drawn, Needed, UnloadDistance;
        };
        for (const FFarCase Case : {FFarCase{12, 24, 1, 545, 1993, 2201, 27}, FFarCase{12, 24, 3, 545, 1993, 2641, 30},
                 FFarCase{12, 32, 1, 545, 3473, 3745, 35}, FFarCase{12, 32, 3, 545, 3473, 4313, 38}})
        {
            const FIntPoint Center(0, 0);
            const FVoxelStreamingRings Rings(Center, Case.ViewRadius, 2, Case.Stages, Case.FarRadius);
            const int32 Drawn = FMath::Max(Case.ViewRadius, Case.FarRadius);
            const FString Where = FString::Printf(TEXT("R %d, far %d, %d stages at %s"), Case.ViewRadius,
                Case.FarRadius, Case.Stages, *StreamingText(Center));
            TestEqual(Where + TEXT(": drawn radius"), Rings.GetDrawnRadius(), Drawn);
            TestEqual(Where + TEXT(": unload distance"), Rings.GetUnloadDistance(), Case.UnloadDistance);
            TestEqual(Where + TEXT(": needed extent"), Rings.GetNeededExtent(), Drawn + 1 + Case.Stages);
            const int32 Half = Drawn + Case.Stages + 5;
            const int32 Extent = Rings.GetNeededExtent();
            const int64 UnloadSquared = int64(Case.UnloadDistance) * Case.UnloadDistance;
            int32 PlannerInView = 0, PlannerDrawn = 0, PlannerFar = 0, PlannerNeeded = 0;
            int32 BruteInView = 0, BruteDrawn = 0, BruteNeeded = 0;
            int32 BrutePerRing[FVoxelStreamingRings::MaxStages + 1] = {};
            TSet<FIntPoint> BruteNeededSet;
            FStreamingTally InViewTally, DrawnTally, FarTally, RingTally, UnloadTally, ExtentTally;
            for (int32 Y = -Half; Y <= Half; ++Y)
                for (int32 X = -Half; X <= Half; ++X)
                {
                    const FIntPoint Column(X, Y);
                    const bool bBruteInView = StreamingBruteInView(Center, Case.ViewRadius, Column);
                    const bool bBruteDrawn = StreamingBruteInView(Center, Drawn, Column);
                    const int32 BruteRing = StreamingBruteRing(Center, Drawn, Case.Stages, Column);
                    const bool bBruteBeyond = StreamingBruteGapSquared(Center, Column) > UnloadSquared;
                    const bool bInView = Rings.IsInView(Column), bDrawn = Rings.IsDrawn(Column);
                    const bool bFar = Rings.IsFar(Column);
                    const int32 Ring = Rings.GetRing(Column);
                    BruteInView += bBruteInView ? 1 : 0;
                    BruteDrawn += bBruteDrawn ? 1 : 0;
                    PlannerInView += bInView ? 1 : 0;
                    PlannerDrawn += bDrawn ? 1 : 0;
                    PlannerFar += bFar ? 1 : 0;
                    PlannerNeeded += Rings.IsNeeded(Column) ? 1 : 0;
                    if (BruteRing != INDEX_NONE)
                    {
                        ++BruteNeeded;
                        ++BrutePerRing[BruteRing];
                        BruteNeededSet.Add(Column);
                    }
                    if (!InViewTally.Check(bInView == bBruteInView))
                        InViewTally.Describe(FString::Printf(TEXT("column %s: IsInView is %s, expected %s"),
                            *StreamingText(Column), StreamingYesNo(bInView), StreamingYesNo(bBruteInView)));
                    if (!DrawnTally.Check(bDrawn == bBruteDrawn))
                        DrawnTally.Describe(FString::Printf(TEXT("column %s: IsDrawn is %s, expected %s"),
                            *StreamingText(Column), StreamingYesNo(bDrawn), StreamingYesNo(bBruteDrawn)));
                    if (!FarTally.Check(bFar == (bBruteDrawn && !bBruteInView)))
                        FarTally.Describe(
                            FString::Printf(TEXT("column %s: IsFar is %s, expected %s"), *StreamingText(Column),
                                StreamingYesNo(bFar), StreamingYesNo(bBruteDrawn && !bBruteInView)));
                    if (!RingTally.Check(Ring == BruteRing))
                        RingTally.Describe(FString::Printf(
                            TEXT("column %s: GetRing is %d, expected %d"), *StreamingText(Column), Ring, BruteRing));
                    if (!UnloadTally.Check(Rings.IsBeyondUnload(Column) == bBruteBeyond))
                        UnloadTally.Describe(FString::Printf(TEXT("column %s: IsBeyondUnload is %s, expected %s"),
                            *StreamingText(Column), StreamingYesNo(Rings.IsBeyondUnload(Column)),
                            StreamingYesNo(bBruteBeyond)));
                    if (BruteRing != INDEX_NONE &&
                        !ExtentTally.Check(FMath::Abs(X) <= Extent && FMath::Abs(Y) <= Extent))
                        ExtentTally.Describe(FString::Printf(
                            TEXT("needed column %s lies beyond the extent %d"), *StreamingText(Column), Extent));
                }
            AddInfo(
                FString::Printf(TEXT("%s, brute force: %d in view, %d drawn (%d far), %d needed (rings %d %d %d %d)"),
                    *Where, BruteInView, BruteDrawn, BruteDrawn - BruteInView, BruteNeeded, BrutePerRing[0],
                    BrutePerRing[1], BrutePerRing[2], BrutePerRing[3]));
            TestEqual(Where + TEXT(": columns in view"), PlannerInView, Case.InView);
            TestEqual(Where + TEXT(": columns drawn"), PlannerDrawn, Case.Drawn);
            TestEqual(Where + TEXT(": columns far"), PlannerFar, Case.Drawn - Case.InView);
            TestEqual(Where + TEXT(": columns needed"), PlannerNeeded, Case.Needed);
            TestEqual(Where + TEXT(": columns in view by the test's own definition"), BruteInView, Case.InView);
            TestEqual(Where + TEXT(": columns drawn by the test's own definition"), BruteDrawn, Case.Drawn);
            TestEqual(Where + TEXT(": columns needed by the test's own definition"), BruteNeeded, Case.Needed);
            TestEqual(Where + TEXT(": ring 0 is the drawn set"), BrutePerRing[0], Case.Drawn);
            StreamingReport(*this, Where + TEXT(": IsInView against the brute force"), InViewTally);
            StreamingReport(*this, Where + TEXT(": IsDrawn against the brute force"), DrawnTally);
            StreamingReport(*this, Where + TEXT(": IsFar against the brute force"), FarTally);
            StreamingReport(*this, Where + TEXT(": GetRing against the brute force"), RingTally);
            StreamingReport(*this, Where + TEXT(": IsBeyondUnload against the brute force"), UnloadTally);
            StreamingReport(*this, Where + TEXT(": GetNeededExtent bounds every needed column"), ExtentTally);

            TArray<FIntPoint> NeededColumns;
            VoxelStreaming::GetNeededColumns(Rings, 0, NeededColumns);
            StreamingExpectColumns(*this, Where + TEXT(": GetNeededColumns"), NeededColumns, BruteNeededSet);
        }
    }

    // 12. D drives the stage rings and the unload distance: the rings (12, far 24, 3 stages) have the rings, the needed
    // list, the unloads, the unload distance and the extent of plain radius 24, while IsInView still means gap <= 12.
    {
        const FIntPoint Center(3, -2);
        const FVoxelStreamingRings FarRings(Center, 12, 2, 3, 24);
        const FVoxelStreamingRings WideRings(Center, 24, 2, 3);
        const FString Where = FString::Printf(TEXT("R 12, far 24, 3 stages at %s"), *StreamingText(Center));
        TestEqual(Where + TEXT(": unload distance equals radius 24's"), FarRings.GetUnloadDistance(),
            WideRings.GetUnloadDistance());
        TestEqual(Where + TEXT(": needed extent equals radius 24's"), FarRings.GetNeededExtent(),
            WideRings.GetNeededExtent());
        const int32 Half = 24 + 3 + 6;
        TArray<FIntPoint> PresentColumns;
        FStreamingTally RingTally, InViewTally, DrawnTally, BeyondTally;
        for (int32 Y = -Half; Y <= Half; ++Y)
            for (int32 X = -Half; X <= Half; ++X)
            {
                const FIntPoint Column = Center + FIntPoint(X, Y);
                PresentColumns.Add(Column);
                const int32 FarRing = FarRings.GetRing(Column), WideRing = WideRings.GetRing(Column);
                if (!RingTally.Check(FarRing == WideRing))
                    RingTally.Describe(FString::Printf(TEXT("column %s: GetRing is %d, radius 24's is %d"),
                        *StreamingText(Column), FarRing, WideRing));
                const bool bInView = FarRings.IsInView(Column), bBruteInView = StreamingBruteInView(Center, 12, Column);
                if (!InViewTally.Check(bInView == bBruteInView))
                    InViewTally.Describe(FString::Printf(TEXT("column %s: IsInView is %s, expected %s"),
                        *StreamingText(Column), StreamingYesNo(bInView), StreamingYesNo(bBruteInView)));
                if (!DrawnTally.Check(FarRings.IsDrawn(Column) == WideRings.IsInView(Column)))
                    DrawnTally.Describe(FString::Printf(TEXT("column %s: IsDrawn is %s, radius 24's IsInView %s"),
                        *StreamingText(Column), StreamingYesNo(FarRings.IsDrawn(Column)),
                        StreamingYesNo(WideRings.IsInView(Column))));
                if (!BeyondTally.Check(FarRings.IsBeyondUnload(Column) == WideRings.IsBeyondUnload(Column)))
                    BeyondTally.Describe(FString::Printf(TEXT("column %s: IsBeyondUnload is %s, radius 24's %s"),
                        *StreamingText(Column), StreamingYesNo(FarRings.IsBeyondUnload(Column)),
                        StreamingYesNo(WideRings.IsBeyondUnload(Column))));
            }
        StreamingReport(*this, Where + TEXT(": GetRing equals radius 24's"), RingTally);
        StreamingReport(*this, Where + TEXT(": IsInView is still gap <= 12"), InViewTally);
        StreamingReport(*this, Where + TEXT(": IsDrawn equals radius 24's IsInView"), DrawnTally);
        StreamingReport(*this, Where + TEXT(": IsBeyondUnload equals radius 24's"), BeyondTally);
        for (const int32 Sector : {0, 3})
        {
            TArray<FIntPoint> FarOrder, WideOrder;
            VoxelStreaming::GetNeededColumns(FarRings, Sector, FarOrder);
            VoxelStreaming::GetNeededColumns(WideRings, Sector, WideOrder);
            StreamingExpectSameOrder(*this,
                FString::Printf(TEXT("%s, sector %d: GetNeededColumns equals radius 24's, same order"), *Where, Sector),
                FarOrder, WideOrder);
        }
        TArray<FIntPoint> FarUnloads, WideUnloads;
        VoxelStreaming::GetUnloads(FarRings, PresentColumns, FarUnloads);
        VoxelStreaming::GetUnloads(WideRings, PresentColumns, WideUnloads);
        TestTrue(Where + TEXT(": some present columns are beyond the unload distance"), WideUnloads.Num() > 0);
        StreamingExpectSameOrder(*this, Where + TEXT(": GetUnloads equals radius 24's"), FarUnloads, WideUnloads);
    }

    // 13. A far radius of 0, or any up to the view radius, changes nothing: the needed list (order included, two
    // sectors), every ring, the unloads against a fixed present set and the unload distance equal those of the rings
    // without a far radius, and no column is far.
    {
        const FIntPoint Center(-5, 7);
        for (const int32 Radius : {0, 1, 4, 12})
            for (const int32 Stages : {1, 3})
            {
                const FVoxelStreamingRings Plain(Center, Radius, 2, Stages);
                const int32 Half = Radius + Stages + 6;
                // Present is in descending order on purpose, so the planner has to sort the result.
                TArray<FIntPoint> PresentColumns;
                for (int32 X = Half; X >= -Half; --X)
                    for (int32 Y = Half; Y >= -Half; --Y)
                        PresentColumns.Add(Center + FIntPoint(X, Y));
                TArray<FIntPoint> PlainUnloads, PlainOrders[2];
                VoxelStreaming::GetUnloads(Plain, PresentColumns, PlainUnloads);
                const int32 Sectors[2] = {0, 5};
                for (int32 Index = 0; Index < 2; ++Index)
                    VoxelStreaming::GetNeededColumns(Plain, Sectors[Index], PlainOrders[Index]);
                TestTrue(FString::Printf(TEXT("R %d, %d stages: some present columns are beyond the unload distance"),
                             Radius, Stages),
                    PlainUnloads.Num() > 0);
                TArray<int32> FarRadii;
                for (const int32 Far : {0, 1, Radius - 1, Radius})
                    if (Far >= 0 && Far <= Radius) FarRadii.AddUnique(Far);
                for (const int32 Far : FarRadii)
                {
                    const FVoxelStreamingRings Rings(Center, Radius, 2, Stages, Far);
                    const FString Where = FString::Printf(
                        TEXT("R %d, far %d, %d stages at %s"), Radius, Far, Stages, *StreamingText(Center));
                    TestEqual(Where + TEXT(": drawn radius is the view radius"), Rings.GetDrawnRadius(), Radius);
                    TestEqual(Where + TEXT(": unload distance"), Rings.GetUnloadDistance(), Plain.GetUnloadDistance());
                    TestEqual(Where + TEXT(": needed extent"), Rings.GetNeededExtent(), Plain.GetNeededExtent());
                    for (int32 Index = 0; Index < 2; ++Index)
                    {
                        TArray<FIntPoint> Order;
                        VoxelStreaming::GetNeededColumns(Rings, Sectors[Index], Order);
                        StreamingExpectSameOrder(*this,
                            FString::Printf(TEXT("%s, sector %d: GetNeededColumns equals far 0's, same order"), *Where,
                                Sectors[Index]),
                            Order, PlainOrders[Index]);
                    }
                    TArray<FIntPoint> Unloads;
                    VoxelStreaming::GetUnloads(Rings, PresentColumns, Unloads);
                    StreamingExpectSameOrder(*this, Where + TEXT(": GetUnloads equals far 0's"), Unloads, PlainUnloads);
                    FStreamingTally RingTally, ViewTally, FarTally;
                    for (const FIntPoint Column : PresentColumns)
                    {
                        const int32 Ring = Rings.GetRing(Column), PlainRing = Plain.GetRing(Column);
                        if (!RingTally.Check(Ring == PlainRing && Rings.IsNeeded(Column) == Plain.IsNeeded(Column) &&
                                Rings.IsBeyondUnload(Column) == Plain.IsBeyondUnload(Column)))
                            RingTally.Describe(FString::Printf(TEXT("column %s: GetRing is %d, far 0's is %d"),
                                *StreamingText(Column), Ring, PlainRing));
                        if (!ViewTally.Check(Rings.IsInView(Column) == Plain.IsInView(Column) &&
                                Rings.IsDrawn(Column) == Plain.IsInView(Column)))
                            ViewTally.Describe(
                                FString::Printf(TEXT("column %s: IsInView %s, IsDrawn %s, far 0's IsInView %s"),
                                    *StreamingText(Column), StreamingYesNo(Rings.IsInView(Column)),
                                    StreamingYesNo(Rings.IsDrawn(Column)), StreamingYesNo(Plain.IsInView(Column))));
                        if (!FarTally.Check(!Rings.IsFar(Column)))
                            FarTally.Describe(FString::Printf(TEXT("column %s is far"), *StreamingText(Column)));
                    }
                    StreamingReport(
                        *this, Where + TEXT(": GetRing, IsNeeded and IsBeyondUnload equal far 0's"), RingTally);
                    StreamingReport(*this, Where + TEXT(": drawn is exactly in view, as far 0's"), ViewTally);
                    StreamingReport(*this, Where + TEXT(": no column is far"), FarTally);
                }
            }
    }

    return true;
}
#endif
