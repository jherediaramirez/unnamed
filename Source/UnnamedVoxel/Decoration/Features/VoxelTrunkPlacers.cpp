#include "Decoration/Features/VoxelTreeFeature.h"
#include "VoxelConstants.h"

// The trunk placers and the root placer (phase 6, S9a). Each shape is ours where minecraft.wiki's "Tree definition" is
// silent, fitted to scout J's probes in C:\dev\sandbox\phase6s9\fit (Docs/VoxelWorld.md, "Trees"). A placer reads its
// data fields from FVoxelTreePlacerConfig by the data's names.

namespace
{
    const FIntPoint TrunkSingleColumn[1] = {{0, 0}};
    const FIntPoint TrunkFourColumns[4] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

    // straight_trunk_placer: logs at the origin column from 0 to Height - 1, the soil under the bottom one, and one
    // attachment at the cell above the top log.
    void PlaceStraight(FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
    {
        const FIntVector Origin = Builder.Origin;
        Builder.SetGroundBelow(Origin - FIntVector(0, 0, 1));
        for (int32 Z = 0; Z < Builder.Height; ++Z)
            Builder.SetLog(Origin + FIntVector(0, 0, Z));
        Out.Add({Origin + FIntVector(0, 0, Builder.Height), 0, false});
    }

    // fancy_trunk_placer's numbers (ours, fitted to the six fancy oak probes): trees below FancyBranchHeight have only
    // the top cluster; a branch leaves the trunk at FancyBranchBase or higher (wiki: at least 20% up); the skip chance
    // grows to FancyLowSkip at the lowest level; the node lies within min(FancyNodeMax, FancyNodeFraction * h - 1) of
    // the trunk, and its branch rises floor(FancyRise * distance).
    constexpr int32 FancyBranchHeight = 7, FancyBranchBase = 3;
    constexpr double FancyLowSkip = 0.05, FancyNodeMax = 5, FancyNodeFraction = 0.45, FancyRise = 0.7;

    // The nearest whole number, halves rounding up.
    int32 RoundHalfUp(double Value)
    {
        return FMath::FloorToInt32(Value + 0.5);
    }

    // Logs from Start to End (both included): N = the largest axis difference, cell I = Start + round(I * Delta / N).
    // Each log lies along the dominant axis of the whole line (ties go to Z, then X), so diagonal steps leave cells
    // that touch only by an edge or a corner.
    void SetLogLine(FVoxelTreeBuilder& Builder, FIntVector Start, FIntVector End)
    {
        const FIntVector Delta = End - Start;
        const int32 AbsX = FMath::Abs(Delta.X), AbsY = FMath::Abs(Delta.Y), AbsZ = FMath::Abs(Delta.Z);
        const int32 Axis = AbsZ >= AbsX && AbsZ >= AbsY ? 2 : AbsX >= AbsY ? 0 : 1;
        const int32 Steps = FMath::Max3(AbsX, AbsY, AbsZ);
        for (int32 Index = 0; Index <= Steps; ++Index)
        {
            const double T = Steps > 0 ? double(Index) / Steps : 0;
            Builder.SetLog(
                Start + FIntVector(RoundHalfUp(T * Delta.X), RoundHalfUp(T * Delta.Y), RoundHalfUp(T * Delta.Z)), Axis);
        }
    }

    // fancy_trunk_placer (ours, fitted; wiki: a straight trunk with diagonal branches ending in foliage clusters). The
    // trunk stands from 0 to h - k, k the largest of 0..3 with k(k + 1) / 2 <= h - 3, and the top cluster's attachment
    // is at h - 3. From h 7 on, each level from h - 4 down to min(h - 4, floor(0.3 h) + 1) tries one cluster, top level
    // first: skipped when u >= 1 - FancyLowSkip * f (f from 0 at the top level to 1 at the lowest); else a node at
    // distance rc * sqrt(u) and a uniform angle (rounded offsets), a branch line from the trunk at the level minus the
    // rise up to the node, and an attachment there. A branch that would leave the trunk below FancyBranchBase drops the
    // cluster.
    void PlaceFancyTrunk(FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const FIntVector Origin = Builder.Origin;
        const int32 Height = Builder.Height;
        int32 Shrink = 0;
        while (Shrink < 3 && (Shrink + 1) * (Shrink + 2) / 2 <= Height - 3)
            ++Shrink;
        Builder.SetGroundBelow(Origin - FIntVector(0, 0, 1));
        for (int32 Z = 0; Z <= Height - Shrink; ++Z)
            Builder.SetLog(Origin + FIntVector(0, 0, Z));
        Out.Add({Origin + FIntVector(0, 0, Height - 3), 0, false});
        if (Height < FancyBranchHeight) return;

        const int32 TopLevel = Height - 4;
        const int32 LowLevel = FMath::Min(TopLevel, 3 * Height / 10 + 1);
        const double NodeRadius = FMath::Min(FancyNodeMax, FancyNodeFraction * Height - 1);
        for (int32 Level = TopLevel; Level >= LowLevel; --Level)
        {
            const double Fraction = TopLevel > LowLevel ? double(TopLevel - Level) / (TopLevel - LowLevel) : 0;
            if (Random.NextDouble() >= 1 - FancyLowSkip * Fraction) continue;
            const double Distance = NodeRadius * FMath::Sqrt(Random.NextDouble());
            const double Angle = 2 * UE_DOUBLE_PI * Random.NextDouble();
            const int32 NodeX = RoundHalfUp(Distance * FMath::Cos(Angle));
            const int32 NodeY = RoundHalfUp(Distance * FMath::Sin(Angle));
            const int32 Rise = FMath::FloorToInt32(FancyRise * FMath::Sqrt(double(NodeX * NodeX + NodeY * NodeY)));
            if (Level - Rise < FancyBranchBase) continue;
            const FIntVector Node = Origin + FIntVector(NodeX, NodeY, Level);
            SetLogLine(Builder, Origin + FIntVector(0, 0, Level - Rise), Node);
            Out.Add({Node, 0, false});
        }
    }

    // forking_trunk_placer's numbers (ours, fitted to acacia): the fork is tried with ForkingForkChance. The directions
    // are +X, +Y, -X, -Y in this order.
    constexpr double ForkingForkChance = 0.85;
    const FIntPoint ForkingDirections[4] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};

    // forking_trunk_placer (ours, fitted; wiki: one or two branches, leaning one to three blocks sideways). Draws in
    // order: the lean direction D, the bend start s = h - 1 - U(0..3), the lean U(1..3), the fork direction (one of the
    // other three), the fork drop q = U(1..3), the fork length n = U(1..3) and the fork try. The trunk has a log at
    // every level 0 to h - 1, stepping one along D at each level from s while lean is left (diagonal steps); the top
    // cluster attaches at the top log. The fork starts at the straight trunk's level s - 1 - q (when that is 0 or more)
    // and climbs one along its direction and one up per log, never above the top log; its end attaches with radius
    // bonus -1.
    void PlaceForking(FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const FIntVector Origin = Builder.Origin;
        const int32 Height = Builder.Height;
        Builder.SetGroundBelow(Origin - FIntVector(0, 0, 1));
        const int32 LeanDirection = Random.NextInt(4);
        const int32 BendStart = Height - 1 - Random.NextInt(4);
        int32 LeanLeft = Random.NextRange(1, 3);
        int32 ForkDirection = Random.NextInt(3);
        if (ForkDirection >= LeanDirection) ++ForkDirection;
        const int32 ForkDrop = Random.NextRange(1, 3);
        // Uniform, fitted to both acacia probes' fork logs (the fit report's 10 : 13 : 16 came from one of them).
        const int32 ForkLength = Random.NextRange(1, 3);
        const bool bTryFork = Random.NextDouble() < ForkingForkChance;

        FIntVector Cell = Origin;
        for (int32 Z = 0; Z < Height; ++Z)
        {
            if (Z >= BendStart && LeanLeft > 0)
            {
                Cell += FIntVector(ForkingDirections[LeanDirection].X, ForkingDirections[LeanDirection].Y, 0);
                --LeanLeft;
            }
            Cell.Z = Origin.Z + Z;
            Builder.SetLog(Cell);
        }
        Out.Add({Cell, 0, false});

        const int32 ForkBase = BendStart - 1 - ForkDrop;
        if (!bTryFork || ForkBase < 0) return;
        FIntVector ForkCell = Origin + FIntVector(0, 0, ForkBase);
        for (int32 Step = 0; Step < ForkLength && ForkCell.Z + 1 <= Origin.Z + Height - 1; ++Step)
        {
            ForkCell += FIntVector(ForkingDirections[ForkDirection].X, ForkingDirections[ForkDirection].Y, 1);
            Builder.SetLog(ForkCell);
        }
        if (ForkCell.X != Origin.X || ForkCell.Y != Origin.Y) Out.Add({ForkCell, -1, false});
    }

    const FName CherryBranchCount(TEXT("branch_count")), CherryBranchStart(TEXT("branch_start_offset_from_top")),
        CherryBranchLength(TEXT("branch_horizontal_length")), CherryBranchEnd(TEXT("branch_end_offset_from_top"));

    // The chance a cherry branch's next step is horizontal while it still has both kinds left (ours, fitted).
    constexpr double CherryStepHorizontal = 0.5;

    // The log axis along a horizontal direction (0 X, 1 Y).
    int32 HorizontalAxis(FIntPoint Direction)
    {
        return Direction.X != 0 ? 0 : 1;
    }

    // cherry_trunk_placer (branch_count, branch_start_offset_from_top, branch_horizontal_length and
    // branch_end_offset_from_top from the data; the shape ours, fitted to cherry_bees_005). Draws in order: the branch
    // count n, the direction D (one of four), the starts s (n >= 2: a second start from the range without the first,
    // the two sorted, lower first), then per side branch, lower first, its length L, its end e and its path's steps.
    // Side branches start at level h + s - 1; the trunk column stands from 0 to that start for n = 1, to the higher
    // start for n = 2, and to h - 1 for n = 3 (a middle stem with foliage of its own). The lower branch points along D,
    // the upper one the opposite way. A branch reaches L out (L + 1 where the column goes on above its start): its
    // first log one out at its start, then the other horizontal steps and h - 1 + e - start vertical ones; while both
    // kinds are left, the next step is horizontal with CherryStepHorizontal. Each branch end attaches, then n = 3's
    // stem top.
    void PlaceCherry(
        const FVoxelTreePlacerConfig& Trunk, FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const FIntVector Origin = Builder.Origin;
        const int32 Height = Builder.Height;
        const int32 Count = Trunk.SampleInt(CherryBranchCount, Random, 1);
        const int32 Direction = Random.NextInt(4);
        int32 Starts[2] = {Trunk.SampleInt(CherryBranchStart, Random), 0};
        const FIntPoint Directions[2] = {
            ForkingDirections[Direction], FIntPoint(-ForkingDirections[Direction].X, -ForkingDirections[Direction].Y)};
        const int32 SideCount = Count >= 2 ? 2 : 1;
        if (SideCount == 2)
        {
            const FVoxelIntProvider* StartRange = Trunk.FindInt(CherryBranchStart);
            const int32 Low = StartRange ? StartRange->Min : Starts[0], High = StartRange ? StartRange->Max : Starts[0];
            const int32 Other = High > Low ? Random.NextRange(Low, High - 1) : Low;
            Starts[1] = Other >= Starts[0] ? Other + 1 : Other;
            if (Starts[1] < Starts[0]) Swap(Starts[0], Starts[1]);
        }
        const int32 BranchZ[2] = {Height + Starts[0] - 1, Height + Starts[1] - 1};
        const int32 ColumnTop = Count >= 3 ? Height - 1 : BranchZ[SideCount - 1];

        Builder.SetGroundBelow(Origin - FIntVector(0, 0, 1));
        for (int32 Z = 0; Z <= ColumnTop; ++Z)
            Builder.SetLog(Origin + FIntVector(0, 0, Z));
        for (int32 Branch = 0; Branch < SideCount; ++Branch)
        {
            const FIntPoint Step = Directions[Branch];
            const int32 Axis = HorizontalAxis(Step);
            const int32 Length = Trunk.SampleInt(CherryBranchLength, Random);
            const int32 End = Height - 1 + Trunk.SampleInt(CherryBranchEnd, Random);
            int32 HorizontalLeft = Length + (ColumnTop > BranchZ[Branch] ? 1 : 0) - 1;
            int32 VerticalLeft = FMath::Max(0, End - BranchZ[Branch]);
            FIntVector Cell = Origin + FIntVector(Step.X, Step.Y, BranchZ[Branch]);
            Builder.SetLog(Cell, Axis);
            while (HorizontalLeft > 0 || VerticalLeft > 0)
            {
                if (VerticalLeft == 0 || (HorizontalLeft > 0 && Random.NextDouble() < CherryStepHorizontal))
                {
                    --HorizontalLeft;
                    Cell += FIntVector(Step.X, Step.Y, 0);
                    Builder.SetLog(Cell, Axis);
                }
                else
                {
                    --VerticalLeft;
                    Cell.Z += 1;
                    Builder.SetLog(Cell);
                }
            }
            Out.Add({Cell, 0, false});
        }
        if (Count >= 3) Out.Add({Origin + FIntVector(0, 0, Height - 1), 0, false});
    }

    const FName PoplarBranchAmount(TEXT("branch_amount")), PoplarAboveBranches(TEXT("trunk_height_above_branches"));

    // poplar_trunk_placer (branch_amount and trunk_height_above_branches from the data; the shape ours, fitted to the
    // three poplar probes): logs at the origin column from 0 to h - 1, then m = branch_amount one-log arms at the
    // branch layer b = h - 1 - trunk_height_above_branches, along the first m of the four directions in a uniform
    // random order (a Fisher-Yates shuffle). One attachment at the cell above the top log, with the branch layer's
    // depth below it (the foliage ends there).
    void PlacePoplar(
        const FVoxelTreePlacerConfig& Trunk, FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const FIntVector Origin = Builder.Origin;
        const int32 Height = Builder.Height;
        Builder.SetGroundBelow(Origin - FIntVector(0, 0, 1));
        for (int32 Z = 0; Z < Height; ++Z)
            Builder.SetLog(Origin + FIntVector(0, 0, Z));
        const int32 Arms = Trunk.SampleInt(PoplarBranchAmount, Random);
        const int32 Above = Trunk.SampleInt(PoplarAboveBranches, Random, 4);
        const int32 BranchZ = Height - 1 - Above;
        FIntPoint Order[4] = {ForkingDirections[0], ForkingDirections[1], ForkingDirections[2], ForkingDirections[3]};
        for (int32 Index = 3; Index > 0; --Index)
            Swap(Order[Index], Order[Random.NextInt(Index + 1)]);
        for (int32 Arm = 0; Arm < FMath::Min(Arms, 4); ++Arm)
            Builder.SetLog(Origin + FIntVector(Order[Arm].X, Order[Arm].Y, BranchZ), HorizontalAxis(Order[Arm]));
        Out.Add({Origin + FIntVector(0, 0, Height), 0, false, Height - BranchZ});
    }

    // The 2 x 2 trunk of giant_trunk_placer and mega_jungle_trunk_placer (ours, fitted to the mega spruce, mega pine
    // and mega jungle probes; wiki: a "single-block top"): the soil under the four columns, logs at the four columns
    // from 0 to h - 2, and the origin column alone at h - 1 (every probe tree has 4 (h - 1) + 1 trunk logs).
    void PlaceTwoByTwo(FVoxelTreeBuilder& Builder)
    {
        const FIntVector Origin = Builder.Origin;
        for (const FIntPoint& Column : TrunkFourColumns)
            Builder.SetGroundBelow(Origin + FIntVector(Column.X, Column.Y, -1));
        for (int32 Z = 0; Z < Builder.Height - 1; ++Z)
            for (const FIntPoint& Column : TrunkFourColumns)
                Builder.SetLog(Origin + FIntVector(Column.X, Column.Y, Z));
        Builder.SetLog(Origin + FIntVector(0, 0, Builder.Height - 1));
    }

    // giant_trunk_placer: the 2 x 2 trunk and one double-trunk attachment above the top log. No random draws.
    void PlaceGiant(FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
    {
        PlaceTwoByTwo(Builder);
        Out.Add({Builder.Origin + FIntVector(0, 0, Builder.Height), 0, true});
    }

    // mega_jungle_trunk_placer's branches (ours, fitted (S9b refit) to the mega jungle probe and its six example
    // trees): the first branch's last log at h - MegaJungleTopGap - U(0..MegaJungleFirstDrop), the next ones
    // U(MegaJungleStepMin..MegaJungleStepMax) lower while at h / 2 or above; every branch has MegaJungleBranchSteps
    // steps, and its canopy gets its bottom layer with chance MegaJungleBottomChance.
    constexpr int32 MegaJungleTopGap = 3, MegaJungleFirstDrop = 3, MegaJungleStepMin = 2, MegaJungleStepMax = 5,
                    MegaJungleBranchSteps = 4;
    constexpr double MegaJungleBottomChance = 0.5;

    // mega_jungle_trunk_placer (ours, fitted; wiki: a giant trunk with branches in its upper half): the 2 x 2 trunk, a
    // double-trunk attachment above the top log with radius bonus 1, then the branches (ours, fitted (S9b refit)), the
    // last log of the first at E = h - 3 - U(0..3), going on while E >= h / 2, each E then falling by U(2..5) (drawn
    // after every branch, the last included, as the fit does). Draws
    // per branch: an angle a = 2 pi u, then (after its logs) the canopy's bottom layer with chance 1/2. Step
    // t = 1 .. 4 puts a log at (int)(1.5 + t cos a), (int)(1.5 + t sin a) from the origin, out from the (1, 1)
    // column's centre and truncated toward zero (so up to 4 logs show toward +X / +Y and 2 toward -X / -Y), and at
    // z = E - 2 + t / 2 (rising 2 over the 4 steps). A single-trunk attachment with no bonus one above the last log
    // carries the bottom-layer flag. Every log stands upright: the probe's logs are all axis y.
    void PlaceMegaJungle(FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const FIntVector Origin = Builder.Origin;
        const int32 Height = Builder.Height;
        PlaceTwoByTwo(Builder);
        Out.Add({Origin + FIntVector(0, 0, Height), 1, true});
        for (int32 End = Height - MegaJungleTopGap - Random.NextRange(0, MegaJungleFirstDrop); End >= Height / 2;
            End -= Random.NextRange(MegaJungleStepMin, MegaJungleStepMax))
        {
            const double Angle = Random.NextDouble() * 2 * UE_DOUBLE_PI;
            const double CosA = FMath::Cos(Angle), SinA = FMath::Sin(Angle);
            FIntVector Last = Origin;
            for (int32 Step = 1; Step <= MegaJungleBranchSteps; ++Step)
            {
                // The cast truncates toward zero on the local offset, before the origin is added (floor fails the
                // probe); steps inside the 2 x 2 land on its logs.
                Last = Origin +
                    FIntVector(static_cast<int32>(1.5 + Step * CosA), static_cast<int32>(1.5 + Step * SinA),
                        End - MegaJungleBranchSteps / 2 + Step / 2);
                Builder.SetLog(Last);
            }
            const bool bBottomLayer = Random.NextDouble() < MegaJungleBottomChance;
            Out.Add({Last + FIntVector(0, 0, 1), 0, false, 0, bBottomLayer});
        }
    }

    // dark_oak_trunk_placer's constants (ours, fitted to the dark oak and pale oak probes): the chance the trunk's top
    // leans, the lean length's and the bend depth's maxima, the chance per ring column of a branch column and its
    // length range (the bend depth and the branch chance and lengths are ours, fitted (S9b refit)), and the ring
    // around the base 2 x 2 in draw order.
    constexpr double DarkOakLeanChance = 0.5, DarkOakBranchChance = 0.31;
    constexpr int32 DarkOakLeanMax = 2, DarkOakBendMax = 2, DarkOakBranchMin = 2, DarkOakBranchMax = 4;
    const FIntPoint DarkOakRing[12] = {
        {-1, 0}, {-1, 1}, {2, 0}, {2, 1}, {0, -1}, {1, -1}, {0, 2}, {1, 2}, {-1, -1}, {2, -1}, {-1, 2}, {2, 2}};
    const FIntPoint DarkOakDirections[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    // dark_oak_trunk_placer (ours, fitted; wiki: a 2 x 2 trunk with branch columns in its top blocks): the soil under
    // the four columns, the 2 x 2 from 0 to h - 1. Draws: a lean (chance 1/2), a direction, a lean length
    // L = U(1..DarkOakLeanMax) and a bend depth M = U(0..DarkOakBendMax), all four always drawn so the stream does not
    // depend on the lean. Then per ring column in DarkOakRing's order a branch column (chance DarkOakBranchChance) of
    // n = U(DarkOakBranchMin..DarkOakBranchMax) logs. One double-trunk attachment above the (shifted) top 2 x 2, no
    // bonus, then one single-trunk attachment per branch column.
    void PlaceDarkOak(FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const FIntVector Origin = Builder.Origin;
        const int32 Height = Builder.Height;
        for (const FIntPoint& Column : TrunkFourColumns)
            Builder.SetGroundBelow(Origin + FIntVector(Column.X, Column.Y, -1));
        const bool bLean = Random.NextDouble() < DarkOakLeanChance;
        const FIntPoint Direction = DarkOakDirections[Random.NextInt(4)];
        const int32 Lean = Random.NextRange(1, DarkOakLeanMax);
        // The bend (ours, fitted (S9b refit)): the trunk moves one block per layer from layer S = h - 1 - M upward
        // until it has made min(L, M + 1) moves, then goes straight up, so the top layer h - 1 is always fully shifted.
        const int32 Bend = Random.NextRange(0, DarkOakBendMax);
        const int32 Steps = bLean ? FMath::Min(Lean, Bend + 1) : 0;
        const int32 Start = Height - 1 - Bend;
        const auto Shift = [&](int32 Z)
        {
            const int32 Step = FMath::Clamp(Z - Start + 1, 0, Steps);
            return Origin + FIntVector(Direction.X * Step, Direction.Y * Step, Z);
        };
        for (int32 Z = 0; Z < Height; ++Z)
            for (const FIntPoint& Column : TrunkFourColumns)
                Builder.SetLog(Shift(Z) + FIntVector(Column.X, Column.Y, 0));
        Out.Add({Shift(Height - 1) + FIntVector(0, 0, 1), 0, true});
        // Branch columns (ours, fitted (S9b refit)): they stand on the ring around the base 2 x 2 and never move with
        // the lean, logs from h - 1 - n to h - 2; each adds a single-trunk attachment one above its top log, where
        // PlaceDarkOakFoliage puts a branch blob.
        for (const FIntPoint& Column : DarkOakRing)
            if (Random.NextDouble() < DarkOakBranchChance)
            {
                const int32 Length = Random.NextRange(DarkOakBranchMin, DarkOakBranchMax);
                for (int32 Z = FMath::Max(0, Height - 1 - Length); Z < Height - 1; ++Z)
                    Builder.SetLog(Origin + FIntVector(Column.X, Column.Y, Z));
                Out.Add({Origin + FIntVector(Column.X, Column.Y, Height - 1), 0, false});
            }
    }
}

EVoxelTrunkPlacer VoxelTrunkPlacers::FindType(FStringView Type)
{
    if (Type == TEXT("minecraft:straight_trunk_placer")) return EVoxelTrunkPlacer::Straight;
    if (Type == TEXT("minecraft:fancy_trunk_placer")) return EVoxelTrunkPlacer::Fancy;
    if (Type == TEXT("minecraft:forking_trunk_placer")) return EVoxelTrunkPlacer::Forking;
    if (Type == TEXT("minecraft:cherry_trunk_placer")) return EVoxelTrunkPlacer::Cherry;
    if (Type == TEXT("minecraft:poplar_trunk_placer")) return EVoxelTrunkPlacer::Poplar;
    if (Type == TEXT("minecraft:dark_oak_trunk_placer")) return EVoxelTrunkPlacer::DarkOak;
    if (Type == TEXT("minecraft:mega_jungle_trunk_placer")) return EVoxelTrunkPlacer::MegaJungle;
    // giant_trunk_placer (PlaceGiant) is written but stays skipped: its trees still fail Trees.Shapes (mega pine's
    // crown radius rule).
    return EVoxelTrunkPlacer::Count;
}

void VoxelTrunkPlacers::Place(
    const FVoxelTreePlacerConfig& Trunk, FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
{
    switch (EVoxelTrunkPlacer(Trunk.Type))
    {
    case EVoxelTrunkPlacer::Straight:
        PlaceStraight(Builder, Out);
        return;
    case EVoxelTrunkPlacer::Fancy:
        PlaceFancyTrunk(Builder, Out);
        return;
    case EVoxelTrunkPlacer::Forking:
        PlaceForking(Builder, Out);
        return;
    case EVoxelTrunkPlacer::Cherry:
        PlaceCherry(Trunk, Builder, Out);
        return;
    case EVoxelTrunkPlacer::Poplar:
        PlacePoplar(Trunk, Builder, Out);
        return;
    case EVoxelTrunkPlacer::Giant:
        PlaceGiant(Builder, Out);
        return;
    case EVoxelTrunkPlacer::MegaJungle:
        PlaceMegaJungle(Builder, Out);
        return;
    case EVoxelTrunkPlacer::DarkOak:
        PlaceDarkOak(Builder, Out);
        return;
    default:
        return;
    }
}

TConstArrayView<FIntPoint> VoxelTrunkPlacers::GetFootprint(EVoxelTrunkPlacer Type)
{
    switch (Type)
    {
    case EVoxelTrunkPlacer::Giant:
    case EVoxelTrunkPlacer::MegaJungle:
    case EVoxelTrunkPlacer::DarkOak:
        return TrunkFourColumns;
    default:
        return TrunkSingleColumn;
    }
}

int32 VoxelTrunkPlacers::GetReach(const FVoxelTreePlacerConfig& Trunk)
{
    switch (EVoxelTrunkPlacer(Trunk.Type))
    {
    case EVoxelTrunkPlacer::Straight:
        return 0;
    case EVoxelTrunkPlacer::Fancy:
        // A node's rounded offset is at most FancyNodeMax.
        return FMath::CeilToInt32(FancyNodeMax);
    case EVoxelTrunkPlacer::Forking:
        // Up to three lean steps, or three fork steps.
        return 3;
    case EVoxelTrunkPlacer::Cherry:
        // A branch's horizontal logs, L + 1 where the column goes on above its start.
        return Trunk.GetMaxInt(CherryBranchLength) + 1;
    case EVoxelTrunkPlacer::Poplar:
        // The one-log arms.
        return 1;
    case EVoxelTrunkPlacer::Giant:
        // The 2 x 2's far columns.
        return 1;
    case EVoxelTrunkPlacer::MegaJungle:
        // A branch's last log, (int)(1.5 + 4) = 5 out at most toward +X / +Y ((int)(1.5 - 4) = -2 toward -X / -Y).
        return MegaJungleBranchSteps + 1;
    case EVoxelTrunkPlacer::DarkOak:
        // The top 2 x 2's far column on a layer shifted DarkOakLeanMax; attachments reach 2 (the shifted top's min
        // corner, or a branch column, which never shifts).
        return DarkOakLeanMax + 1;
    default:
        return VoxelConstants::ColumnSize;
    }
}

bool VoxelRootPlacers::IsMangroveBuilt()
{
    return false;
}

bool VoxelRootPlacers::PlaceMangrove(const FVoxelMangroveRoots& Roots, FVoxelTreeBuilder& Builder)
{
    return false;
}

int32 VoxelRootPlacers::GetReach(const FVoxelMangroveRoots& Roots)
{
    return Roots.MaxRootWidth;
}
