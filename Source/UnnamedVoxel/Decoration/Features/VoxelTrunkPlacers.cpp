#include "Decoration/Features/VoxelTreeFeature.h"
#include "Blocks/VoxelBlockTable.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Decoration/Features/VoxelPatchFeatures.h"
#include "Decoration/VoxelBlockPredicates.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelStateProviders.h"
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

namespace
{
    // S9e's trunks (ours, fitted to scout J's azalea and mangrove probes by S9e's agent F1,
    // C:\dev\sandbox\phase6s9\s9e\fit\f1-crown; the data gives their numbers, the wiki only bending's outline).
    const FName BendLength(TEXT("bend_length")), MinHeightForLeaves(TEXT("min_height_for_leaves")),
        BranchProbability(TEXT("place_branch_per_log_probability")), ExtraBranchSteps(TEXT("extra_branch_steps")),
        ExtraBranchLength(TEXT("extra_branch_length"));
    // A bend's or a branch's four directions (0 -X, 1 +X, 2 -Y, 3 +Y).
    const FIntVector TrunkSteps[4] = {
        FIntVector(-1, 0, 0), FIntVector(1, 0, 0), FIntVector(0, -1, 0), FIntVector(0, 1, 0)};
    // bending: the chance that the second-highest trunk cell moves too (the probe's three-part trunks, 39%).
    constexpr float BendingLowerShiftChance = 0.4f;
    // upwards_branching: the share of the data's place_branch_per_log_probability a log branches at (its 0.5 gave 12 to
    // 16% too many logs; 0.5 x 0.85 fits all four probes).
    constexpr double UpwardsBranchScale = 0.85;

    // bending_trunk_placer (the wiki: "Places a trunk of the specified height. The topmost 2 blocks have a chance to be
    // moved by one block each. On top of the trunk a straight branch of length bend_length is generated. Foliage is
    // attached to all block of the top branch and any trunk block that is higher than min_height_for_leaves"). Ours:
    // logs at the origin's column from 0 to Height - 3; the cell at Height - 2 moved one along the direction with
    // BendingLowerShiftChance, the one at Height - 1 one further, always (the probe: none of 100 unmoved); then a log
    // above it and bend_length more along the direction at Height (the top log at Height, as the probe). Attachments:
    // the trunk's cells from min_height_for_leaves up, then the top row from the inside out, whether or not each log
    // went down (as the other trunks' attachments). The soil under the bottom log. Draws: NextInt(4) for the direction,
    // bend_length's, NextFloat for the lower move.
    void PlaceBending(
        const FVoxelTreePlacerConfig& Trunk, FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const FIntVector Origin = Builder.Origin;
        const int32 Height = Builder.Height;
        const FIntVector Step = TrunkSteps[Random.NextInt(4)];
        const int32 Bend = FMath::Max(0, Trunk.SampleInt(BendLength, Random, 1));
        const bool bLower = Random.NextFloat() < BendingLowerShiftChance;
        const int32 MinLeaves = Trunk.GetMaxInt(MinHeightForLeaves, 1);
        Builder.SetGroundBelow(Origin - FIntVector(0, 0, 1));
        FIntVector Shift = FIntVector::ZeroValue;
        for (int32 Z = 0; Z < Height; ++Z)
        {
            if ((Z == Height - 2 && bLower) || Z == Height - 1) Shift += Step;
            const FIntVector Cell = Origin + Shift + FIntVector(0, 0, Z);
            Builder.SetLog(Cell);
            if (Z >= MinLeaves) Out.Add({Cell, 0, false});
        }
        for (int32 Index = 0; Index <= Bend; ++Index)
        {
            const FIntVector Cell = Origin + Shift + Step * Index + FIntVector(0, 0, Height);
            Builder.SetLog(Cell);
            Out.Add({Cell, 0, false});
        }
    }

    // upwards_branching_trunk_placer (ours; the wiki gives only the fields): logs at the origin's column from 0 to
    // Height - 1. From the bottom up, after its log, each trunk log below the top one branches with
    // place_branch_per_log_probability x UpwardsBranchScale: a direction, S =
    // extra_branch_steps and extra_branch_length (drawn, unused: the probes show no effect), then S - 1 logs climbing
    // diagonally, one out and one up each, every one an attachment, and an attachment above the last (S 1: one out and
    // one up from the trunk log). Then the trunk's attachment above its top log. No soil under the trunk (a mangrove's
    // root takes the cell). Draws: per trunk log below the top NextFloat, and on a branch NextInt(4),
    // extra_branch_steps', extra_branch_length's.
    void PlaceUpwardsBranching(
        const FVoxelTreePlacerConfig& Trunk, FVoxelTreeBuilder& Builder, TArray<FVoxelFoliageAttachment>& Out)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const FIntVector Origin = Builder.Origin;
        const int32 Height = Builder.Height;
        const float Chance = float(Trunk.GetNumber(BranchProbability) * UpwardsBranchScale);
        for (int32 Z = 0; Z < Height; ++Z)
        {
            Builder.SetLog(Origin + FIntVector(0, 0, Z));
            if (Z > Height - 2 || Random.NextFloat() >= Chance) continue;
            const FIntVector Step = TrunkSteps[Random.NextInt(4)];
            const int32 Steps = FMath::Max(1, Trunk.SampleInt(ExtraBranchSteps, Random, 1));
            Trunk.SampleInt(ExtraBranchLength, Random, 0);
            FIntVector End = Origin + FIntVector(0, 0, Z);
            for (int32 K = 1; K < Steps; ++K)
            {
                End = Origin + Step * K + FIntVector(0, 0, Z + K);
                Builder.SetLog(End);
                Out.Add({End, 0, false});
            }
            Out.Add({Steps == 1 ? End + Step + FIntVector(0, 0, 1) : End + FIntVector(0, 0, 1), 0, false});
        }
        Out.Add({Origin + FIntVector(0, 0, Height), 0, false});
    }
}

EVoxelTrunkPlacer VoxelTrunkPlacers::FindType(FStringView Type)
{
    if (Type == TEXT("minecraft:bending_trunk_placer")) return EVoxelTrunkPlacer::Bending;
    if (Type == TEXT("minecraft:upwards_branching_trunk_placer")) return EVoxelTrunkPlacer::UpwardsBranching;
    if (Type == TEXT("minecraft:straight_trunk_placer")) return EVoxelTrunkPlacer::Straight;
    if (Type == TEXT("minecraft:fancy_trunk_placer")) return EVoxelTrunkPlacer::Fancy;
    if (Type == TEXT("minecraft:forking_trunk_placer")) return EVoxelTrunkPlacer::Forking;
    if (Type == TEXT("minecraft:cherry_trunk_placer")) return EVoxelTrunkPlacer::Cherry;
    if (Type == TEXT("minecraft:poplar_trunk_placer")) return EVoxelTrunkPlacer::Poplar;
    if (Type == TEXT("minecraft:dark_oak_trunk_placer")) return EVoxelTrunkPlacer::DarkOak;
    if (Type == TEXT("minecraft:mega_jungle_trunk_placer")) return EVoxelTrunkPlacer::MegaJungle;
    // giant_trunk_placer was skipped until the S9c refit gave mega_pine_foliage_placer its crown table (MegaPineRadii):
    // mega spruce and mega pine now pass Trees.Shapes.
    if (Type == TEXT("minecraft:giant_trunk_placer")) return EVoxelTrunkPlacer::Giant;
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
    case EVoxelTrunkPlacer::Bending:
        PlaceBending(Trunk, Builder, Out);
        return;
    case EVoxelTrunkPlacer::UpwardsBranching:
        PlaceUpwardsBranching(Trunk, Builder, Out);
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
    case EVoxelTrunkPlacer::Bending:
        // The top row's far end: the trunk's two moves and the bend.
        return 2 + FMath::Max(0, Trunk.GetMaxInt(BendLength, 1));
    case EVoxelTrunkPlacer::UpwardsBranching:
        // A branch's last log (S - 1 out), or a one-step branch's attachment.
        return FMath::Max(1, Trunk.GetMaxInt(ExtraBranchSteps, 1) - 1);
    default:
        return VoxelConstants::ColumnSize;
    }
}

// Off until the mangroves pass their probes (S9e part 1: on grass they do, but tall_mangrove@mudwater's 28 survivors
// are taller and branchier than F1's crown with F2's roots gives, leaves -24%, z -3.2; agent F4 found no joint rule;
// C:\dev\sandbox\phase6s9\s9e\fit\f4-joint). The root placer, the upwards_branching trunk and attached_to_leaves stay
// built for S9e part 2; until then the mangroves' selector entries place nothing, as before.
bool VoxelRootPlacers::IsMangroveBuilt()
{
    return false;
}

namespace
{
    // mangrove_root_placer's walk (ours, fitted to scout J's four mangrove probes by S9e's agent F2,
    // C:\dev\sandbox\phase6s9\s9e\fit\f2-roots): an arm's step goes outward with MangroveOutChance times the ring's
    // share, a step in the outward band the share refuses is wasted, and a strand under an outward step lands within
    // MangroveStrandDepth.
    constexpr float MangroveOutChance = 0.36f;
    constexpr int32 MangroveStrandDepth = 2;
}

// The root placer (ours, fitted; the data gives its numbers, the wiki no algorithm). The trunk rises by k =
// trunk_offset_y. A root goes under the bottom log, then four arms (+X, -X, +Y, -Y) start beside it at its height. Each
// step draws u: under MangroveOutChance it is an outward attempt, which moves one cell out along the arm's axis when u
// is also under MangroveOutChance * share(r) (share = (W - 2 - r) / (W - 3) clamped to 0..1, W = max_root_width, r the
// arm's ring) and the cell there can take a root, first drawing whether a strand hangs from the cell it leaves
// (random_skew_chance); otherwise the attempt is wasted. Any other step goes down where the cell below can take a root,
// and else ends the arm (it has landed). Every step, moves and wasted ones alike, counts; the tree fails at once when
// an arm takes more than max_root_length (the probes' placed rates fall with the descent from the bottom log to the
// ground: 100/100, 91/100 on grass, 89/100 and 28/100 on mud under two water). A strand walks down from below the cell
// it hangs from through air or replaceable_by_trees (water included, mud not) that holds no root, up to
// MangroveStrandDepth + 1 cells, and is rooted only when it stops within MangroveStrandDepth. A cell can take a root
// when it is free (air or replaceable_by_trees) or in can_grow_through, the tree's own roots included; a root over
// muddy_roots_in is the muddy root state, over water the root state waterlogged. Then above each root, in the order
// they went down, where the cell is air and holds none of the tree's cells (nor is the bottom log's), a draw places
// above_root_provider's block at above_root_placement_chance. Draws: trunk_offset_y's, per arm step NextFloat and on a
// move NextFloat for the strand, each root's provider draws as it goes down, then per moss cell NextFloat and the
// provider's.
bool VoxelRootPlacers::PlaceMangrove(const FVoxelMangroveRoots& Roots, FVoxelTreeBuilder& Builder)
{
    const FVoxelPlantEnvironment& Env = Builder.Env;
    FVoxelXoroshiro& Random = Builder.Random;
    const int32 Rise = FMath::Max(0, Roots.TrunkOffsetY.Sample(Random));
    const FIntVector Base = Builder.Origin;
    const FIntVector Trunk = Base + FIntVector(0, 0, Rise);
    Builder.Origin = Trunk;
    const FVoxelBlockSet& Free = Env.Predicates.GetSet(Builder.Config.FreeSet);
    const FVoxelBlockSet& Through = Env.Predicates.GetSet(Roots.CanGrowThroughSet);
    const FVoxelBlockSet& Muddy = Env.Predicates.GetSet(Roots.MuddyRootsInSet);
    const auto IsInside = [](const FIntVector& Cell)
    { return Cell.Z >= VoxelConstants::MinZ && Cell.Z <= VoxelConstants::MaxZ; };
    const auto CanGrow = [&](const FIntVector& Cell)
    {
        if (!IsInside(Cell) || Builder.IsLog(Cell)) return false;
        const int32 Block = Env.Table.Get(Builder.GetState(Cell)).Block;
        return Free.Contains(Block) || Through.Contains(Block);
    };
    static const FName Waterlogged(TEXT("waterlogged")), True(TEXT("true"));
    const auto PlaceRoot = [&](const FIntVector& Cell)
    {
        const uint16 Was = Builder.GetState(Cell);
        const int32 Provider = Muddy.Contains(Env.Table.Get(Was).Block) ? Roots.MuddyRootProvider : Roots.RootProvider;
        uint16 State = Env.Providers.Sample(Provider, Random, Cell, Env.Context);
        if (Provider == Roots.RootProvider && Env.Survival.IsWaterSource(Was))
        {
            const int32 Wet = Env.Table.WithValue(State, Waterlogged, True);
            if (Wet != INDEX_NONE) State = uint16(Wet);
        }
        Builder.SetRoot(Cell, State);
    };
    const int32 Width = Roots.MaxRootWidth;
    const auto Share = [Width](int32 Ring)
    { return Width > 3 ? FMath::Clamp(float(Width - 2 - Ring) / float(Width - 3), 0.f, 1.f) : 0.f; };
    const FIntVector Down(0, 0, -1);

    if (CanGrow(Trunk + Down)) PlaceRoot(Trunk + Down);
    static const FIntVector Arms[4] = {
        FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0)};
    for (const FIntVector& Step : Arms)
    {
        FIntVector At = Trunk + Step;
        if (!CanGrow(At)) continue;
        PlaceRoot(At);
        int32 Ring = 1, Steps = 0;
        while (true)
        {
            const float U = Random.NextFloat();
            if (U < MangroveOutChance)
            {
                if (U < MangroveOutChance * Share(Ring) && CanGrow(At + Step))
                {
                    if (Random.NextFloat() < Roots.RandomSkewChance)
                    {
                        // A strand under the cell the arm leaves.
                        TArray<FIntVector, TInlineAllocator<4>> Strand;
                        for (FIntVector Cell = At + Down; Strand.Num() <= MangroveStrandDepth; Cell += Down)
                        {
                            FVoxelTreeBuilder::ECell Kind;
                            if (!IsInside(Cell) || Builder.GetKind(Cell, Kind) ||
                                !Free.Contains(Env.Table.Get(Builder.GetState(Cell)).Block))
                                break;
                            Strand.Add(Cell);
                        }
                        if (Strand.Num() >= 1 && Strand.Num() <= MangroveStrandDepth)
                            for (const FIntVector& Cell : Strand)
                                PlaceRoot(Cell);
                    }
                    At += Step;
                    ++Ring;
                    PlaceRoot(At);
                }
            }
            else if (CanGrow(At + Down))
            {
                At += Down;
                PlaceRoot(At);
            }
            else
                break;
            if (++Steps > Roots.MaxRootLength) return false;
        }
    }

    // The moss over the roots, in the order they went down.
    if (Roots.AboveRootProvider != INDEX_NONE && Roots.AboveRootChance > 0)
    {
        const int32 Count = Builder.Cells.Num();
        for (int32 Index = 0; Index < Count; ++Index)
        {
            if (Builder.Cells[Index].Kind != FVoxelTreeBuilder::ECell::Root) continue;
            const FIntVector Above = Builder.Cells[Index].Position + FIntVector(0, 0, 1);
            FVoxelTreeBuilder::ECell Kind;
            if (Above == Trunk || Builder.GetKind(Above, Kind) || !Builder.IsAir(Above)) continue;
            if (Random.NextFloat() < Roots.AboveRootChance)
                Builder.SetRoot(Above, Env.Providers.Sample(Roots.AboveRootProvider, Random, Above, Env.Context));
        }
    }
    return true;
}

int32 VoxelRootPlacers::GetReach(const FVoxelMangroveRoots& Roots)
{
    return Roots.MaxRootWidth;
}
