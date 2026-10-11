#include "Decoration/Features/VoxelTreeFeature.h"
#include "Blocks/VoxelBlockTable.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Decoration/Features/VoxelPatchFeatures.h"
#include "Decoration/VoxelBlockPredicates.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelStateProviders.h"
#include "VoxelConstants.h"

// The tree decorators of phase 6's S9c (minecraft.wiki "Tree definition"). Each runs on the tree's builder after the
// trunk, the foliage and the leaves' distance, in the data's order, drawing from the tree's stream, and writes through
// FVoxelTreeBuilder::SetDecoration (emitted after the tree's logs and leaves, only where the cell still holds what this
// Decorate saw). Where the wiki is silent the rules are ours, fitted to scout J's probes in the sandbox
// (C:\dev\sandbox\phase6s9\s9c\); each decorator's comment gives its rule and its draws.

namespace
{
    bool TreeDecoratorFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    struct FTreeDecoratorType
    {
        const TCHAR* Id;
        EVoxelTreeDecorator Type;
    };
    // The types built so far.
    constexpr FTreeDecoratorType TreeDecoratorTypes[] = {
        {TEXT("minecraft:alter_ground"), EVoxelTreeDecorator::AlterGround},
        {TEXT("minecraft:beehive"), EVoxelTreeDecorator::Beehive},
        {TEXT("minecraft:cocoa"), EVoxelTreeDecorator::Cocoa},
        {TEXT("minecraft:trunk_vine"), EVoxelTreeDecorator::TrunkVine},
        {TEXT("minecraft:leave_vine"), EVoxelTreeDecorator::LeaveVine},
        {TEXT("minecraft:place_on_ground"), EVoxelTreeDecorator::PlaceOnGround},
        {TEXT("minecraft:pale_moss"), EVoxelTreeDecorator::PaleMoss},
        {TEXT("minecraft:creaking_heart"), EVoxelTreeDecorator::CreakingHeart},
        {TEXT("minecraft:shelf_mushroom"), EVoxelTreeDecorator::ShelfMushroom},
        {TEXT("minecraft:attached_to_logs"), EVoxelTreeDecorator::AttachedToLogs},
        {TEXT("minecraft:attached_to_leaves"), EVoxelTreeDecorator::AttachedToLeaves},
    };

    // The four horizontal face steps in the Directions order (0 -X west, 1 +X east, 2 -Y north, 3 +Y south), and per
    // step the face of a block placed there that looks back at the block it hangs on (a vine's true face, cocoa's
    // facing).
    const FIntVector TreeDecoratorSides[4] = {
        FIntVector(-1, 0, 0), FIntVector(1, 0, 0), FIntVector(0, -1, 0), FIntVector(0, 1, 0)};
    const TCHAR* const TreeDecoratorBackFaces[4] = {TEXT("east"), TEXT("west"), TEXT("south"), TEXT("north")};
    // Per step its own name: the facing of a block placed there that looks away from the block it hangs on (a shelf
    // mushroom's).
    const TCHAR* const TreeDecoratorOutFaces[4] = {TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south")};

    // A decorator's "probability" field.
    double TreeDecoratorProbability(const FVoxelTreeDecoratorConfig& Decorator)
    {
        static const FName Probability(TEXT("probability"));
        return Decorator.GetNumber(Probability);
    }

    // The 5 x 5 square without its corners that alter_ground turns around a point.
    constexpr int32 AlterGroundCircle = 2;

    // alter_ground (ours, fitted to the mega spruce and mega pine probes: podzol 118.7 and 119.1 per tree, reach 7,
    // its count per ring by Chebyshev radius from the origin within 1.5 sigma; C:\dev\sandbox\phase6s9\s9c\alter\).
    // Around each bottom log (the lowest layer of logs, in cell order) the decorator turns a circle (a 5 x 5 square
    // without its corners) centred at (-1, -1), (2, -1), (-1, 2) and (2, 2) from the log, then makes 5 tries that each
    // draw an offset X then Y, uniform in -3..4, and turn a circle there when the offset lies on that square's edge.
    // In each column it turns, the first cell from 2 above the log down to 3 below that is neither air nor
    // replaceable_by_trees nor the tree's own log, leaf or root takes the provider's state (podzol_beneath_tree turns
    // its tag to podzol and leaves every other block). The vertical search is ours: the probes stand on flat ground.
    void PlaceAlterGround(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        using ECell = FVoxelTreeBuilder::ECell;
        TArray<FIntVector> Logs;
        Builder.GetCells(ECell::Log, Logs);
        if (Logs.IsEmpty() || Decorator.Provider == INDEX_NONE) return;
        const int32 BaseZ = Logs[0].Z;
        TArray<FIntPoint> Centres;
        for (const FIntVector& Log : Logs)
        {
            if (Log.Z != BaseZ) break;
            for (const FIntPoint Offset : {FIntPoint(-1, -1), FIntPoint(2, -1), FIntPoint(-1, 2), FIntPoint(2, 2)})
                Centres.Add(FIntPoint(Log.X, Log.Y) + Offset);
            for (int32 Try = 0; Try < 5; ++Try)
            {
                const int32 X = Builder.Random.NextInt(8) - 3;
                const int32 Y = Builder.Random.NextInt(8) - 3;
                if (X == -3 || X == 4 || Y == -3 || Y == 4) Centres.Add(FIntPoint(Log.X + X, Log.Y + Y));
            }
        }
        // Each column once, in the order the circles reach it (the provider draws nothing).
        TArray<FIntPoint> Columns;
        TSet<FIntPoint> Seen;
        for (const FIntPoint& Centre : Centres)
            for (int32 DY = -AlterGroundCircle; DY <= AlterGroundCircle; ++DY)
                for (int32 DX = -AlterGroundCircle; DX <= AlterGroundCircle; ++DX)
                {
                    if (FMath::Abs(DX) == AlterGroundCircle && FMath::Abs(DY) == AlterGroundCircle) continue;
                    bool bSeen = false;
                    Seen.Add(Centre + FIntPoint(DX, DY), &bSeen);
                    if (!bSeen) Columns.Add(Centre + FIntPoint(DX, DY));
                }
        const FVoxelPlantEnvironment& Env = Builder.Env;
        for (const FIntPoint& Column : Columns)
            for (int32 DZ = 2; DZ >= -3; --DZ)
            {
                const FIntVector Cell(Column.X, Column.Y, BaseZ + DZ);
                // Outside the world: no ground to sample the provider for.
                if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) continue;
                ECell Kind;
                if (Builder.GetKind(Cell, Kind) && (Kind == ECell::Log || Kind == ECell::Leaf || Kind == ECell::Root))
                    continue;
                if (Builder.IsFree(Cell)) continue;
                const uint16 State = Env.Providers.Sample(Decorator.Provider, Builder.Random, Cell, Env.Context);
                if (State != Builder.GetState(Cell)) Builder.SetDecoration(Cell, State);
                break;
            }
    }

    // beehive (wiki: one nest with the probability, "always located 1 block below the lowest leaves block, but possibly
    // on a different branch", facing south, only with air in front). Ours, fitted to fancy_oak_bees (0.93 nests per
    // tree at probability 1: all 7 misses among the 16 trees whose top log is at dy 3, whose lowest leaves can lie at
    // dy 0, so the nest's cell is the ground) and super_birch_bees (1.00): the candidates are the cells west, east and
    // south of every log in the layer under the lowest leaf (in cell order, each once) that are air with air south of
    // them; the nest takes one of them uniformly, and none is placed when there is none. Draws: NextFloat for the
    // probability, then NextInt over the candidates when there are any. States: the nest.
    void PlaceBeehive(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        using ECell = FVoxelTreeBuilder::ECell;
        if (Builder.Random.NextFloat() >= TreeDecoratorProbability(Decorator)) return;
        int32 Z = MAX_int32;
        for (const FVoxelTreeBuilder::FCell& Cell : Builder.Cells)
            if (Cell.Kind == ECell::Leaf) Z = FMath::Min(Z, Cell.Position.Z - 1);
        if (Z == MAX_int32) return;
        TArray<FIntVector, TInlineAllocator<16>> Candidates;
        for (const FVoxelTreeBuilder::FCell& Cell : Builder.Cells)
            if (Cell.Kind == ECell::Log && Cell.Position.Z == Z)
                for (const int32 Side : {0, 1, 3})
                {
                    const FIntVector Nest = Cell.Position + TreeDecoratorSides[Side];
                    if (Builder.IsAir(Nest) && Builder.IsAir(Nest + TreeDecoratorSides[3])) Candidates.AddUnique(Nest);
                }
        if (!Candidates.IsEmpty())
            Builder.SetDecoration(Candidates[Builder.Random.NextInt(Candidates.Num())], Decorator.States[0]);
    }

    // cocoa (wiki: with the probability the tree gets any; then each face of the trunk's bottom blocks at 25%). Ours,
    // fitted to jungle_tree (probability 0.2: 0.44 cocoa per tree, on 23% of trees, 23 at dy 0 and 21 at dy 1, none
    // at dy 2 though the wiki says the bottom 3 blocks): the logs of the two lowest layers, in cell order, each side
    // west, east, north, south where the cell is air, a pod facing the log, of age 0 to 2 (the probe's pods show all
    // three). Draws: NextFloat for the probability, then per log and side NextFloat (under 0.25 places), then
    // NextInt(3) for the age when the cell is air. States: per age, per side (age * 4 + side).
    void PlaceCocoa(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        using ECell = FVoxelTreeBuilder::ECell;
        if (Builder.Random.NextFloat() >= TreeDecoratorProbability(Decorator)) return;
        TArray<FIntVector> Logs;
        Builder.GetCells(ECell::Log, Logs);
        for (const FIntVector& Log : Logs)
        {
            if (Log.Z > Logs[0].Z + 1) break;
            for (int32 Side = 0; Side < 4; ++Side)
            {
                if (Builder.Random.NextFloat() >= 0.25f) continue;
                const FIntVector Pod = Log + TreeDecoratorSides[Side];
                if (Builder.IsAir(Pod))
                    Builder.SetDecoration(Pod, Decorator.States[Builder.Random.NextInt(3) * 4 + Side]);
            }
        }
    }

    // trunk_vine's chance per side: 2/3, measured in S9d part 2 (the wiki says 75%). The fallen oak and jungle stumps
    // have all four sides open (their log starts 2 or 3 away): 533 vines on 200 stumps, 0.666 per side, where 75%
    // gives 3.0 per stump (z -5.5).
    constexpr float TrunkVineChance = 2.f / 3.f;

    // trunk_vine (wiki: a vine on each side of each trunk block, at TrunkVineChance as measured). Every log counts as
    // trunk (the mega jungle probe's branch logs carry vines: 10 of their 11 open sides in its examples); the vine
    // goes where the cell is air and does not grow down (the probes' log-hung runs end over air). Draws: per log from
    // the bottom up (cell order within a layer), per side west, east, north, south, NextFloat (under TrunkVineChance
    // places). States: the vine per side.
    void PlaceTrunkVine(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        TArray<FIntVector> Logs;
        Builder.GetCells(FVoxelTreeBuilder::ECell::Log, Logs);
        for (const FIntVector& Log : Logs)
            for (int32 Side = 0; Side < 4; ++Side)
            {
                if (Builder.Random.NextFloat() >= TrunkVineChance) continue;
                const FIntVector Vine = Log + TreeDecoratorSides[Side];
                if (Builder.IsAir(Vine)) Builder.SetDecoration(Vine, Decorator.States[Side]);
            }
    }

    // leave_vine (wiki: with the probability a vine at each position beside the foliage, "extended by 4 blocks
    // downwards if possible"). Per leaf (from the bottom up, cell order within a layer) and side west, east, north,
    // south: where the cell beside is air, a vine facing the leaf, then the same vine in each of the 4 cells below
    // while they are air (the probes' leaf-hung runs are 5 long in the open and stop at the first block). Draws: per
    // leaf and side NextFloat (under the probability places). States: the vine per side.
    void PlaceLeaveVine(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        const float Probability = float(TreeDecoratorProbability(Decorator));
        TArray<FIntVector> Leaves;
        Builder.GetCells(FVoxelTreeBuilder::ECell::Leaf, Leaves);
        for (const FIntVector& Leaf : Leaves)
            for (int32 Side = 0; Side < 4; ++Side)
            {
                if (Builder.Random.NextFloat() >= Probability) continue;
                FIntVector Vine = Leaf + TreeDecoratorSides[Side];
                for (int32 Length = 0; Length < 5 && Builder.IsAir(Vine); ++Length, --Vine.Z)
                    Builder.SetDecoration(Vine, Decorator.States[Side]);
            }
    }

    // place_on_ground's radius (the wiki's default 2), and the bounds of the trunk's footprint (its one or four bottom
    // columns, relative to the origin).
    int32 PlaceOnGroundRadius(const FVoxelTreeDecoratorConfig& Decorator)
    {
        static const FName Radius(TEXT("radius"));
        return FMath::Max(0, int32(Decorator.GetNumber(Radius, 2)));
    }
    void PlaceOnGroundFootprint(const FVoxelTreeConfig& Tree, FIntPoint& OutMin, FIntPoint& OutMax)
    {
        OutMin = FIntPoint(MAX_int32, MAX_int32);
        OutMax = FIntPoint(MIN_int32, MIN_int32);
        for (const FIntPoint& Column : VoxelTrunkPlacers::GetFootprint(EVoxelTrunkPlacer(Tree.Trunk.Type)))
        {
            OutMin = FIntPoint(FMath::Min(OutMin.X, Column.X), FMath::Min(OutMin.Y, Column.Y));
            OutMax = FIntPoint(FMath::Max(OutMax.X, Column.X), FMath::Max(OutMax.Y, Column.Y));
        }
        if (OutMin.X > OutMax.X) OutMin = OutMax = FIntPoint::ZeroValue;
    }

    // place_on_ground (wiki: tries, radius and height default to 128, 2 and 1). Ours, fitted to the ten leaf litter
    // probes (27.2 to 30.5 litter per tree, all at dy 0 on their flat grass; C:\dev\sandbox\phase6s9\s9c\d3\): each
    // try draws a column in the square spanning the trunk's footprint plus the radius (X then Y, uniform: -4..4
    // around a 1 x 1 trunk at radius 4; -4..5 around the dark oak's 2 x 2, whose litter reaches 5 and whose rings
    // match, where a square around a random bottom log falls short at 5), then a Z within the height of the origin's
    // (uniform: 1 try in 5 at height 2 meets flat ground, which gives the oak's 0.76 litter per cell within 2, where
    // both decorators reach, and 0.21 at 3 and 4, where the 96 tries alone do; the probe's 0.77 and 0.21). The cell
    // takes the provider's state where it is air, the top of its column (the live motion_blocking_no_leaves height is
    // the cell below), no log of this tree stands above it (the examples hold no litter in any of their 60 columns
    // under a branch, while leaves above stop nothing: the fancy oaks' and poplars' fewer litter within 1) and the
    // state survives there (leaf litter: a sturdy top below). Draws: per try NextInt for X, Y and Z, then the
    // provider's draws where the cell passes.
    void PlaceOnGround(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        static const FName TriesName(TEXT("tries")), HeightName(TEXT("height"));
        if (Decorator.Provider == INDEX_NONE) return;
        const int32 Tries = int32(Decorator.GetNumber(TriesName, 128));
        const int32 Radius = PlaceOnGroundRadius(Decorator);
        const int32 Height = FMath::Max(0, int32(Decorator.GetNumber(HeightName, 1)));
        FIntPoint FootMin, FootMax;
        PlaceOnGroundFootprint(Builder.Config, FootMin, FootMax);
        const FIntVector Origin = Builder.Origin;
        const int32 MinX = Origin.X + FootMin.X - Radius, MinY = Origin.Y + FootMin.Y - Radius;
        const int32 SizeX = FootMax.X - FootMin.X + 2 * Radius + 1, SizeY = FootMax.Y - FootMin.Y + 2 * Radius + 1;
        // Per column of the square, the highest log of this tree there (MIN_int32 for none).
        TArray<int32, TInlineAllocator<256>> TopLog;
        TopLog.Init(MIN_int32, SizeX * SizeY);
        for (const FVoxelTreeBuilder::FCell& Cell : Builder.Cells)
        {
            const int32 X = Cell.Position.X - MinX, Y = Cell.Position.Y - MinY;
            if (Cell.Kind == FVoxelTreeBuilder::ECell::Log && uint32(X) < uint32(SizeX) && uint32(Y) < uint32(SizeY))
                TopLog[Y * SizeX + X] = FMath::Max(TopLog[Y * SizeX + X], Cell.Position.Z);
        }
        const FVoxelPlantEnvironment& Env = Builder.Env;
        const auto GetState = [&Builder](int32 X, int32 Y, int32 Z) { return Builder.GetState(FIntVector(X, Y, Z)); };
        for (int32 Try = 0; Try < Tries; ++Try)
        {
            const int32 X = Builder.Random.NextInt(SizeX);
            const int32 Y = Builder.Random.NextInt(SizeY);
            const FIntVector Cell(MinX + X, MinY + Y, Origin.Z - Height + Builder.Random.NextInt(2 * Height + 1));
            // The cheap checks first: the column's top, this tree's logs over it, then the cell.
            if (Env.Context.GetHeight(EVoxelDecorationHeightmap::MotionBlockingNoLeaves, Cell.X, Cell.Y) !=
                    Cell.Z - 1 ||
                TopLog[Y * SizeX + X] >= Cell.Z || !Builder.IsAir(Cell))
                continue;
            const uint16 State = Env.Providers.Sample(Decorator.Provider, Builder.Random, Cell, Env.Context);
            if (Env.Survival.Survives(State, Cell.X, Cell.Y, Cell.Z, GetState)) Builder.SetDecoration(Cell, State);
        }
    }

    // shelf_mushroom: a log's chance, and how many log layers from the bottom carry them.
    constexpr float ShelfMushroomChance = 0.375f;
    constexpr int32 ShelfMushroomLayers = 4;

    // shelf_mushroom (wiki: "on each trunk block with the specified probability"). Ours, one rule fitted in S9d part 2
    // to the three poplar probes (0.45 to 0.54 per tree, 0.51 pooled, on 29 to 33% of trees, at most 3; orange's 17,
    // 14, 14 and 9 at dy 0 to 3 and none higher; each beside a log, a branch's once, facing away from it, of age 0 or
    // 1) and to fallen poplar's log (1.05 per tree on 65%, at most 4, only on the two sides across the log, never at
    // its ends): with the probability the tree gets any (a chance per log alone would put one on 42% of the poplars);
    // then each log of the 4 lowest log layers, in cell order, at ShelfMushroomChance takes one of its bark sides at
    // random: west, east, north, south on an upright log, the two across a lying one (its axis from its state). The
    // mushroom goes there where the cell is free and not this tree's leaf (it replaces the leaf litter: dy 0 holds as
    // many as dy 1) and survives (the log behind it). The model: our poplars' about 4 logs there with 86% of their
    // sides open give 0.4 x 4 x 0.375 x 0.86 = 0.52 per tree, on 32% of trees; fallen poplar's 3.52 log cells give
    // 0.8 x 3.52 x 0.375 = 1.06, on 65% (S9c's rule, 9% per open side, gave it 0.65). Draws: NextFloat for the
    // probability, then per log NextFloat (under ShelfMushroomChance it takes a side), NextInt over its bark sides,
    // and NextInt(2) for the age where the cell is free. States: per age, per side (age * 4 + side).
    void PlaceShelfMushroom(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        if (Builder.Random.NextFloat() >= TreeDecoratorProbability(Decorator)) return;
        TArray<FIntVector> Logs;
        Builder.GetCells(FVoxelTreeBuilder::ECell::Log, Logs);
        const auto GetState = [&Builder](int32 X, int32 Y, int32 Z) { return Builder.GetState(FIntVector(X, Y, Z)); };
        const uint16* LogStates = Builder.Config.LogStates;
        for (const FIntVector& Log : Logs)
        {
            if (Log.Z >= Logs[0].Z + ShelfMushroomLayers) break;
            if (Builder.Random.NextFloat() >= ShelfMushroomChance) continue;
            // A log along X has its bark north and south, one along Y west and east; an upright log (or a trunk
            // without an axis) all four.
            const uint16 LogState = Builder.GetState(Log);
            const bool bUpright = LogState == LogStates[2] || LogStates[0] == LogStates[2];
            const int32 Side = bUpright    ? Builder.Random.NextInt(4)
                : LogState == LogStates[0] ? 2 + Builder.Random.NextInt(2)
                                           : Builder.Random.NextInt(2);
            const FIntVector Cell = Log + TreeDecoratorSides[Side];
            if (!Builder.IsFree(Cell) || Builder.IsLeaf(Cell)) continue;
            const uint16 State = Decorator.States[Builder.Random.NextInt(2) * 4 + Side];
            if (Builder.Env.Survival.Survives(State, Cell.X, Cell.Y, Cell.Z, GetState))
                Builder.SetDecoration(Cell, State);
        }
    }

    // The six steps of a decorator's Directions bits (0 -X, 1 +X, 2 -Y, 3 +Y, 4 down, 5 up).
    const FIntVector TreeDecoratorSteps[6] = {FIntVector(-1, 0, 0), FIntVector(1, 0, 0), FIntVector(0, -1, 0),
        FIntVector(0, 1, 0), FIntVector(0, 0, -1), FIntVector(0, 0, 1)};

    // attached_to_logs (wiki: "Attaches blocks to exposed faces of all trunk blocks in specified directions", with
    // the probability). Per log (from the bottom up, cell order within a layer; a fallen tree's log decorators read its
    // log alone), with the probability one of the data's directions at random, and the provider's state there where
    // the cell is air, with no survival check (the fallen trees' probes: a mushroom on top of 10% of the log's cells,
    // 0.35 to 0.61 per tree against 0.1 x their log cells, in full daylight). Draws: per log NextFloat, then where it
    // passes NextInt over the directions when there are more than one, then the provider's draws where the cell is
    // air. States: none fixed (the provider's).
    void PlaceAttachedToLogs(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        if (Decorator.Provider == INDEX_NONE || !Decorator.Directions) return;
        TArray<int32, TInlineAllocator<6>> Steps;
        for (int32 Step = 0; Step < 6; ++Step)
            if (Decorator.Directions & (1 << Step)) Steps.Add(Step);
        const float Probability = float(TreeDecoratorProbability(Decorator));
        const FVoxelPlantEnvironment& Env = Builder.Env;
        TArray<FIntVector> Logs;
        Builder.GetCells(FVoxelTreeBuilder::ECell::Log, Logs);
        for (const FIntVector& Log : Logs)
        {
            if (Builder.Random.NextFloat() >= Probability) continue;
            const int32 Step = Steps.Num() > 1 ? Steps[Builder.Random.NextInt(Steps.Num())] : Steps[0];
            const FIntVector Cell = Log + TreeDecoratorSteps[Step];
            if (Builder.IsAir(Cell))
                Builder.SetDecoration(
                    Cell, Env.Providers.Sample(Decorator.Provider, Builder.Random, Cell, Env.Context));
        }
    }

    const FName AttachedEmptyBlocks(TEXT("required_empty_blocks")), AttachedExclusionXZ(TEXT("exclusion_radius_xz")),
        AttachedExclusionY(TEXT("exclusion_radius_y"));

    // attached_to_leaves (the wiki: "Attaches blocks to exposed faces of all foliage blocks in specified directions";
    // exclusion_radius_xz and _y are "the minimum value of the horizontal / vertical distance between two decorations",
    // required_empty_blocks "the number of empty blocks required by the decoration"). Ours, fitted to the mangrove
    // probes' propagules by S9e's agent F1 (C:\dev\sandbox\phase6s9\s9e\fit\f1-crown): per leaf (from the bottom up,
    // cell order within a layer), with the probability one of the data's directions at random; the
    // required_empty_blocks cells that way must be air (the tree's vines and water block them), and no earlier block of
    // this decorator may lie within exclusion_radius_xz across and exclusion_radius_y + 1 up or down (the data's 0
    // alone gave 14 to 21% too many propagules); then the provider's state in the first cell. Draws: per leaf
    // NextFloat, then where it passes NextInt over the directions when there are more than one, then the provider's
    // where it places. States: none fixed (the provider's).
    void PlaceAttachedToLeaves(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        if (Decorator.Provider == INDEX_NONE || !Decorator.Directions) return;
        TArray<int32, TInlineAllocator<6>> Steps;
        for (int32 Step = 0; Step < 6; ++Step)
            if (Decorator.Directions & (1 << Step)) Steps.Add(Step);
        const float Probability = float(TreeDecoratorProbability(Decorator));
        const int32 Empty = FMath::Max(1, int32(Decorator.GetNumber(AttachedEmptyBlocks)));
        const int32 AcrossXY = FMath::Max(0, int32(Decorator.GetNumber(AttachedExclusionXZ)));
        const int32 UpDown = FMath::Max(0, int32(Decorator.GetNumber(AttachedExclusionY))) + 1;
        const FVoxelPlantEnvironment& Env = Builder.Env;
        TArray<FIntVector> Leaves, Placed;
        Builder.GetCells(FVoxelTreeBuilder::ECell::Leaf, Leaves);
        for (const FIntVector& Leaf : Leaves)
        {
            if (Builder.Random.NextFloat() >= Probability) continue;
            const FIntVector Step =
                TreeDecoratorSteps[Steps.Num() > 1 ? Steps[Builder.Random.NextInt(Steps.Num())] : Steps[0]];
            bool bEmpty = true;
            for (int32 Index = 1; Index <= Empty && bEmpty; ++Index)
                bEmpty = Builder.IsAir(Leaf + Step * Index);
            const FIntVector Cell = Leaf + Step;
            const auto IsNear = [&Cell, AcrossXY, UpDown](const FIntVector& Other)
            {
                return FMath::Abs(Other.X - Cell.X) <= AcrossXY && FMath::Abs(Other.Y - Cell.Y) <= AcrossXY &&
                    FMath::Abs(Other.Z - Cell.Z) <= UpDown;
            };
            if (!bEmpty || Placed.ContainsByPredicate(IsNear)) continue;
            if (Builder.SetDecoration(
                    Cell, Env.Providers.Sample(Decorator.Provider, Builder.Random, Cell, Env.Context)))
                Placed.Add(Cell);
        }
    }

    // pale_moss's ground: the configured feature pale_moss_patch (a vegetation_patch: its numbers are read from it at
    // compile, under these names) and its vegetation's to_place (the decorator's provider).
    const FName PaleMossRadiusMin(TEXT("xz_radius_min")), PaleMossRadiusMax(TEXT("xz_radius_max")),
        PaleMossDepth(TEXT("depth")), PaleMossExtraBottom(TEXT("extra_bottom_block_chance")),
        PaleMossExtraEdge(TEXT("extra_edge_column_chance")), PaleMossVegetation(TEXT("vegetation_chance")),
        PaleMossRange(TEXT("vertical_range")), PaleMossReplaceable(TEXT("replaceable"));
    // A hanging moss column's chance to grow one cell further, and a carpet side's chance to grow tall (ours, fitted:
    // below).
    constexpr float PaleMossGrowChance = 0.5f;
    constexpr float PaleMossTallSideChance = 0.5f;

    // A column of hanging moss under Top (air): Top, then one more cell down while the cell below is air and a
    // NextFloat is under PaleMossGrowChance; the lowest cell is the tip.
    void PlacePaleHangingMoss(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder, FIntVector Top)
    {
        int32 Length = 1;
        while (Builder.IsAir(Top - FIntVector(0, 0, Length)) && Builder.Random.NextFloat() < PaleMossGrowChance)
            ++Length;
        for (int32 Index = 0; Index < Length; ++Index)
            Builder.SetDecoration(Top - FIntVector(0, 0, Index), Decorator.States[Index == Length - 1 ? 1 : 2]);
    }

    // A vegetation state at Cell (above new moss): a carpet grows a side toward each of this tree's logs beside it,
    // tall at PaleMossTallSideChance (else low; a NextFloat per such side, west, east, north, south), and a tall side
    // puts a carpet above (bottom false, that side low) where the cell is air; a two-cell plant needs its upper cell
    // air.
    void PlacePaleMossPlant(
        const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder, FIntVector Cell, uint16 State)
    {
        static const FName Bottom(TEXT("bottom")), Low(TEXT("low")), Tall(TEXT("tall")), False(TEXT("false")),
            Half(TEXT("half")), Lower(TEXT("lower")), UpperHalf(TEXT("upper"));
        const FVoxelPlantEnvironment& Env = Builder.Env;
        const auto GetState = [&Builder](int32 X, int32 Y, int32 Z) { return Builder.GetState(FIntVector(X, Y, Z)); };
        if (!Builder.IsAir(Cell) || !Env.Survival.Survives(State, Cell.X, Cell.Y, Cell.Z, GetState)) return;
        const FIntVector Above = Cell + FIntVector(0, 0, 1);
        // A two-cell plant's upper half: the environment's, else its half property's (the tree tests' kit has none).
        const uint16* UpperOf = Env.UpperOf.Find(State);
        const int32 Upper = UpperOf                    ? int32(*UpperOf)
            : Env.Table.GetValue(State, Half) == Lower ? Env.Table.WithValue(State, Half, UpperHalf)
                                                       : INDEX_NONE;
        if (Upper != INDEX_NONE)
        {
            if (!Builder.IsAir(Above)) return;
            Builder.SetDecoration(Cell, State);
            Builder.SetDecoration(Above, uint16(Upper));
            return;
        }
        int32 UpperState = INDEX_NONE;
        if (Env.Table.Get(State).Block == Env.Table.Get(Decorator.States[3]).Block)
        {
            // The upper carpet starts from the provider's state (bottom false), not from State: until the S9c review
            // it took State after the earlier sides had grown, so it inherited their low sides, and a failed lookup
            // could place it sideless. Only a side whose tall write succeeded sets its side (low) on it, and a lookup
            // that fails places none.
            const int32 UpperBase = Env.Table.WithValue(State, Bottom, False);
            for (int32 Side = 0; Side < 4; ++Side)
            {
                if (!Builder.IsLog(Cell + TreeDecoratorSides[Side])) continue;
                const FName Face(TreeDecoratorOutFaces[Side]);
                const bool bTall = Builder.Random.NextFloat() < PaleMossTallSideChance;
                const int32 Grown = Env.Table.WithValue(State, Face, bTall ? Tall : Low);
                if (Grown != INDEX_NONE) State = uint16(Grown);
                if (!bTall || Grown == INDEX_NONE || UpperBase == INDEX_NONE) continue;
                const int32 Topper =
                    Env.Table.WithValue(uint16(UpperState != INDEX_NONE ? UpperState : UpperBase), Face, Low);
                if (Topper != INDEX_NONE) UpperState = Topper;
            }
        }
        Builder.SetDecoration(Cell, State);
        if (UpperState != INDEX_NONE && Builder.IsAir(Above)) Builder.SetDecoration(Above, uint16(UpperState));
    }

    // pale_moss (wiki: ground, trunk and leaves probabilities). Ours, fitted to the pale_oak and pale_oak_creaking
    // probes (C:\dev\sandbox\phase6s9\s9c\d3b\), in the wiki's order:
    // - The ground, at ground_probability (0.8: moss on 77% and 75% of trees): the configured feature pale_moss_patch
    //   as vegetation_patch places it (VoxelPatchFeatures::PlaceVegetationPatch's footprint: each radius 2..4 plus
    //   one, corners out, edges at 0.75; floor within vertical_range; depth 1; vegetation at 0.3 from its to_place),
    //   centred on a random bottom log, read through this tree's view (no floor under the trunk: the probe's dirt
    //   stays). The probes' three examples with moss centre on a trunk column ((0,1), (0,0), (1,0)); per Chebyshev
    //   ring from the origin per placed tree the model gives 5.0, 15.6, 19.6, 16.2, 7.9, 1.8 moss blocks (the pooled
    //   probes 5.0, 15.6, 19.0, 14.5, 7.3, 1.6; centred on the origin it reaches 5 only, the probes 6). A carpet
    //   beside a log grows a side toward it (all 5 such carpets in the examples; 2 tall, each with a carpet above):
    //   tall at 0.5, from the probes' 0.40 and 0.41 upper carpets per tree over about 0.77 carpets beside the trunk.
    // - Under each log (from the bottom up, cell order within a layer) whose cell below is air, at trunk_probability
    //   (0.4), a column of hanging moss; then the same under each leaf at leaves_probability (0.15; the examples: 10
    //   of 18 open log bottoms, 28 of 216 open leaf bottoms). A column grows a cell further while the cell below is
    //   air at 0.5 (the examples' columns: 18 of 1, 14 of 2, 5 of 3 and one of 5 that met the ground; the probes'
    //   cells per dy 0, 1, 2 are 3.4, 7.3 and 12.6% of their hanging moss, the model over the examples' open bottoms
    //   2.8, 7.3 and 12.7%, and 18.8 per tree against the probes' 19.15 and 17.81). The tip is the lowest cell.
    // Draws: NextFloat for the ground; when it passes, NextInt over the bottom logs, the two radii (X then Y), then
    // per column (X outer, Y inner) an edge's NextFloat, and where a floor is found NextFloat for the extra bottom
    // block; where that column then set ground, NextFloat for vegetation, and where it passes the provider's draws and
    // a carpet's side NextFloats (one per log beside it, where the carpet can stand). Then per open log
    // bottom NextFloat, per open leaf bottom NextFloat, and per placed column a NextFloat per cell it tries to add.
    // States: pale moss block, hanging moss tip, hanging moss not tip, the carpet (its block for the side rule).
    void PlacePaleMoss(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        static const FName Ground(TEXT("ground_probability")), Trunk(TEXT("trunk_probability")),
            Leaves(TEXT("leaves_probability"));
        using ECell = FVoxelTreeBuilder::ECell;
        const FVoxelPlantEnvironment& Env = Builder.Env;
        TArray<FIntVector> Cells;
        Builder.GetCells(ECell::Log, Cells);
        if (Builder.Random.NextFloat() < float(Decorator.GetNumber(Ground)) && !Cells.IsEmpty() &&
            Decorator.Provider != INDEX_NONE)
        {
            int32 Bottom = 1;
            while (Bottom < Cells.Num() && Cells[Bottom].Z == Cells[0].Z)
                ++Bottom;
            const FIntVector Centre = Cells[Builder.Random.NextInt(Bottom)];
            const int32 Min = int32(Decorator.GetNumber(PaleMossRadiusMin)),
                        Max = int32(Decorator.GetNumber(PaleMossRadiusMax));
            const int32 RadiusX = FMath::Max(0, Max > Min ? Builder.Random.NextRange(Min, Max) : Min) + 1;
            const int32 RadiusY = FMath::Max(0, Max > Min ? Builder.Random.NextRange(Min, Max) : Min) + 1;
            const int32 Range = int32(Decorator.GetNumber(PaleMossRange));
            const int32 Depth = int32(Decorator.GetNumber(PaleMossDepth));
            const float ExtraEdge = float(Decorator.GetNumber(PaleMossExtraEdge)),
                        ExtraBottom = float(Decorator.GetNumber(PaleMossExtraBottom)),
                        Vegetation = float(Decorator.GetNumber(PaleMossVegetation));
            const FVoxelBlockSet& Replaceable = Env.Predicates.GetSet(int32(Decorator.GetNumber(PaleMossReplaceable)));
            // Inside the world, replaceable and none of this tree's logs or leaves (the cheap checks first, then one
            // lookup of the tree's cells).
            const auto IsGround = [&](FIntVector Cell)
            {
                if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return false;
                const uint16 State = Builder.GetState(Cell);
                if (State == 0 || !Replaceable.Contains(Env.Table.Get(State).Block)) return false;
                ECell Kind;
                return !Builder.GetKind(Cell, Kind) || (Kind != ECell::Log && Kind != ECell::Leaf);
            };
            for (int32 DX = -RadiusX; DX <= RadiusX; ++DX)
                for (int32 DY = -RadiusY; DY <= RadiusY; ++DY)
                {
                    const bool bEdgeX = FMath::Abs(DX) == RadiusX, bEdgeY = FMath::Abs(DY) == RadiusY;
                    if (bEdgeX && bEdgeY) continue;
                    if ((bEdgeX || bEdgeY) && Builder.Random.NextFloat() >= ExtraEdge) continue;
                    FIntVector Floor(Centre.X + DX, Centre.Y + DY, Centre.Z + Range);
                    for (; Floor.Z >= Centre.Z - Range; --Floor.Z)
                        if (Builder.IsAir(Floor) && IsGround(Floor - FIntVector(0, 0, 1))) break;
                    if (Floor.Z < Centre.Z - Range) continue;
                    const int32 Deep = Depth + (Builder.Random.NextFloat() < ExtraBottom ? 1 : 0);
                    bool bGround = false;
                    for (int32 Index = 1; Index <= Deep && IsGround(Floor - FIntVector(0, 0, Index)); ++Index)
                        bGround |= Builder.SetDecoration(Floor - FIntVector(0, 0, Index), Decorator.States[0]);
                    if (bGround && Builder.Random.NextFloat() < Vegetation)
                        PlacePaleMossPlant(Decorator, Builder, Floor,
                            Env.Providers.Sample(Decorator.Provider, Builder.Random, Floor, Env.Context));
                }
        }
        const float TrunkChance = float(Decorator.GetNumber(Trunk)), LeafChance = float(Decorator.GetNumber(Leaves));
        for (const FIntVector& Log : Cells)
            if (Builder.IsAir(Log - FIntVector(0, 0, 1)) && Builder.Random.NextFloat() < TrunkChance)
                PlacePaleHangingMoss(Decorator, Builder, Log - FIntVector(0, 0, 1));
        Builder.GetCells(ECell::Leaf, Cells);
        for (const FIntVector& Leaf : Cells)
            if (Builder.IsAir(Leaf - FIntVector(0, 0, 1)) && Builder.Random.NextFloat() < LeafChance)
                PlacePaleHangingMoss(Decorator, Builder, Leaf - FIntVector(0, 0, 1));
    }

    // creaking_heart (wiki: a trunk cell fully enclosed by logs). With the probability, one of this tree's logs whose
    // six neighbours are all this tree's logs (in the builder's cell order) becomes the heart, uniformly; none
    // when there is none. The probe's 31 hearts in 100 trees lie in the 2 x 2 trunk's columns at dy 3 to 7 (its
    // example's is enclosed on all six sides). Draws: NextFloat for the probability, then NextInt over the candidates
    // when there are any. States: the heart (dormant, natural) per log axis (0 X, 1 Y, 2 Z), its log's axis kept.
    void PlaceCreakingHeart(const FVoxelTreeDecoratorConfig& Decorator, FVoxelTreeBuilder& Builder)
    {
        if (Builder.Random.NextFloat() >= TreeDecoratorProbability(Decorator)) return;
        using ECell = FVoxelTreeBuilder::ECell;
        TArray<int32, TInlineAllocator<16>> Candidates;
        for (int32 Index = 0; Index < Builder.Cells.Num(); ++Index)
        {
            const FVoxelTreeBuilder::FCell& Cell = Builder.Cells[Index];
            bool bEnclosed = Cell.Kind == ECell::Log && Builder.IsLog(Cell.Position + FIntVector(0, 0, 1)) &&
                Builder.IsLog(Cell.Position - FIntVector(0, 0, 1));
            for (int32 Side = 0; Side < 4 && bEnclosed; ++Side)
                bEnclosed = Builder.IsLog(Cell.Position + TreeDecoratorSides[Side]);
            if (bEnclosed) Candidates.Add(Index);
        }
        if (Candidates.IsEmpty()) return;
        const int32 Heart = Candidates[Builder.Random.NextInt(Candidates.Num())];
        int32 Axis = 2;
        for (int32 Each = 0; Each < 3; ++Each)
            if (Builder.Config.LogStates[Each] == Builder.Cells[Heart].State) Axis = Each;
        // The heart is no log: it becomes a decoration (emitted where the cell still holds what this Decorate saw,
        // and counted as the decorator's block rather than a log).
        Builder.ReplaceWithDecoration(Heart, Decorator.States[Axis]);
    }
}

double FVoxelTreeDecoratorConfig::GetNumber(FName Name, double Default) const
{
    for (const TPair<FName, double>& Pair : Numbers)
        if (Pair.Key == Name) return Pair.Value;
    return Default;
}

EVoxelTreeDecorator VoxelTreeDecorators::FindType(FStringView Type)
{
    for (const FTreeDecoratorType& Each : TreeDecoratorTypes)
        if (Type == Each.Id) return Each.Type;
    return EVoxelTreeDecorator::Count;
}

const TCHAR* VoxelTreeDecorators::GetName(EVoxelTreeDecorator Type)
{
    for (const FTreeDecoratorType& Each : TreeDecoratorTypes)
        if (Each.Type == Type) return Each.Id + FCString::Strlen(TEXT("minecraft:"));
    return TEXT("?");
}

bool VoxelTreeDecorators::Compile(const FVoxelFeatureGraph& Graph, int32 Node, EVoxelTreeDecorator Type,
    FVoxelBlockTags& Tags, const FVoxelBlockTable& Table, FVoxelBlockPredicates& Predicates,
    FVoxelStateProviders& Providers, FVoxelTreeDecoratorConfig& Out, FString& OutSkip, FString* OutError)
{
    Out.Type = Type;
    const FVoxelFeatureNode& Data = Graph.Nodes[Node];
    for (int32 Index = 0; Index < Data.FieldCount; ++Index)
    {
        const FVoxelFeatureField& Field = Graph.Fields[Data.FirstField + Index];
        const FStringView Name = Field.Key == INDEX_NONE ? FStringView() : FStringView(Graph.Strings[Field.Key]);
        const FVoxelFeatureValue& Value = Field.Value;
        if (Value.Type == EVoxelFeatureValueType::Number || Value.Type == EVoxelFeatureValueType::Bool)
            Out.Numbers.Add({FName(Name), Value.Number});
        else if (Name == TEXT("provider") || Name == TEXT("block_provider") || Name == TEXT("block_state_provider"))
        {
            if (!Providers.Compile(Graph, Value, Tags, Predicates, Out.Provider, OutSkip, OutError)) return false;
        }
        else if (Name == TEXT("directions") && Value.Type == EVoxelFeatureValueType::List)
        {
            static const TCHAR* const Steps[] = {
                TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south"), TEXT("down"), TEXT("up")};
            for (int32 Item = 0; Item < Value.Count; ++Item)
            {
                const FVoxelFeatureValue& Direction = Graph.Items[Value.Index + Item];
                int32 Step = INDEX_NONE;
                for (int32 Each = 0; Direction.Type == EVoxelFeatureValueType::String && Each < 6; ++Each)
                    if (Graph.Strings[Direction.Index] == Steps[Each]) Step = Each;
                if (Step == INDEX_NONE) return TreeDecoratorFail(OutError, TEXT("a decorator direction not read"));
                Out.Directions |= uint8(1 << Step);
            }
        }
    }
    // The fixed states (each decorator's comment gives their order).
    const auto AddState = [&](const TCHAR* Block, TConstArrayView<TPair<FName, FName>> Properties) -> bool
    {
        const int32 State = Table.MakeState(Block, Properties);
        if (State == INDEX_NONE)
            return TreeDecoratorFail(OutError, FString::Printf(TEXT("a %s state not in the block table"), Block));
        Out.States.Add(uint16(State));
        return true;
    };
    static const FName Facing(TEXT("facing")), Age(TEXT("age")), True(TEXT("true"));
    switch (Type)
    {
    case EVoxelTreeDecorator::Beehive:
        return AddState(
            TEXT("bee_nest"), {{Facing, FName(TEXT("south"))}, {FName(TEXT("honey_level")), FName(TEXT("0"))}});
    case EVoxelTreeDecorator::Cocoa:
        for (const TCHAR* const PodAge : {TEXT("0"), TEXT("1"), TEXT("2")})
            for (const TCHAR* const Face : TreeDecoratorBackFaces)
                if (!AddState(TEXT("cocoa"), {{Age, FName(PodAge)}, {Facing, FName(Face)}})) return false;
        return true;
    case EVoxelTreeDecorator::TrunkVine:
    case EVoxelTreeDecorator::LeaveVine:
        for (const TCHAR* const Face : TreeDecoratorBackFaces)
            if (!AddState(TEXT("vine"), {{FName(Face), True}})) return false;
        return true;
    case EVoxelTreeDecorator::ShelfMushroom:
        for (const TCHAR* const MushroomAge : {TEXT("0"), TEXT("1")})
            for (const TCHAR* const Face : TreeDecoratorOutFaces)
                if (!AddState(TEXT("shelf_mushroom"), {{Age, FName(MushroomAge)}, {Facing, FName(Face)}})) return false;
        return true;
    case EVoxelTreeDecorator::CreakingHeart:
        // Per our axis X, Y, Z: the data's x, z, y.
        for (const TCHAR* const Axis : {TEXT("x"), TEXT("z"), TEXT("y")})
            if (!AddState(TEXT("creaking_heart"),
                    {{FName(TEXT("axis")), FName(Axis)}, {FName(TEXT("creaking_heart_state")), FName(TEXT("dormant"))},
                        {FName(TEXT("natural")), True}}))
                return false;
        return true;
    case EVoxelTreeDecorator::PaleMoss:
    {
        // The ground: the configured feature pale_moss_patch (a floor vegetation_patch), its numbers, its replaceable
        // tag as a block set and its vegetation's to_place as the provider.
        const int32 Entry = Graph.FindEntry(EVoxelFeatureRegistry::Feature, TEXT("minecraft:pale_moss_patch"));
        const int32 Patch =
            Entry == INDEX_NONE ? INDEX_NONE : Graph.Entries[int32(EVoxelFeatureRegistry::Feature)][Entry].Node;
        if (Patch == INDEX_NONE || Graph.GetType(Patch) != TEXT("minecraft:vegetation_patch"))
            return TreeDecoratorFail(OutError, TEXT("pale_moss without the pale_moss_patch vegetation_patch"));
        const auto Number = [&](const TCHAR* Key, double& OutValue) -> bool
        {
            const FVoxelFeatureValue* Value = Graph.FindField(Patch, Key);
            if (!Value ||
                (Value->Type != EVoxelFeatureValueType::Number && Value->Type != EVoxelFeatureValueType::Bool))
                return TreeDecoratorFail(OutError, FString::Printf(TEXT("pale_moss_patch without a %s number"), Key));
            OutValue = Value->Number;
            return true;
        };
        double Depth = 0, ExtraBottom = 0, ExtraEdge = 0, Vegetation = 0, Range = 0, Min = 0, Max = 0;
        if (!Number(TEXT("depth"), Depth) || !Number(TEXT("extra_bottom_block_chance"), ExtraBottom) ||
            !Number(TEXT("extra_edge_column_chance"), ExtraEdge) || !Number(TEXT("vegetation_chance"), Vegetation) ||
            !Number(TEXT("vertical_range"), Range))
            return false;
        const FVoxelFeatureValue* Radius = Graph.FindField(Patch, TEXT("xz_radius"));
        const int32 RadiusNode = Radius ? Graph.GetNode(*Radius) : INDEX_NONE;
        if (Radius && Radius->Type == EVoxelFeatureValueType::Number)
            Min = Max = Radius->Number;
        else if (RadiusNode != INDEX_NONE && Graph.GetType(RadiusNode) == TEXT("minecraft:uniform"))
        {
            const FVoxelFeatureValue* Low = Graph.FindField(RadiusNode, TEXT("min_inclusive"));
            const FVoxelFeatureValue* High = Graph.FindField(RadiusNode, TEXT("max_inclusive"));
            if (!Low || !High) return TreeDecoratorFail(OutError, TEXT("pale_moss_patch's xz_radius not read"));
            Min = Low->Number;
            Max = High->Number;
        }
        else
            return TreeDecoratorFail(OutError, TEXT("pale_moss_patch's xz_radius not read"));
        const FVoxelFeatureValue* Replaceable = Graph.FindField(Patch, TEXT("replaceable"));
        if (!Replaceable || Replaceable->Type != EVoxelFeatureValueType::String ||
            !Graph.Strings[Replaceable->Index].StartsWith(TEXT("#")))
            return TreeDecoratorFail(OutError, TEXT("pale_moss_patch without a replaceable tag"));
        const FVoxelBlockSet* Set = Tags.Find(Graph.Strings[Replaceable->Index].RightChop(1), OutError);
        if (!Set) return false;
        // The vegetation: a placed feature (no placements) of a simple_block, whose to_place is the provider.
        const FVoxelFeatureValue* Placed = Graph.FindField(Patch, TEXT("vegetation_feature"));
        const int32 PlacedNode = Placed ? Graph.GetNode(*Placed) : INDEX_NONE;
        const FVoxelFeatureValue* Feature =
            PlacedNode == INDEX_NONE ? nullptr : Graph.FindField(PlacedNode, TEXT("feature"));
        const int32 FeatureNode = Feature ? Graph.GetNode(*Feature) : INDEX_NONE;
        const FVoxelFeatureValue* ToPlace =
            FeatureNode != INDEX_NONE && Graph.GetType(FeatureNode) == TEXT("minecraft:simple_block")
            ? Graph.FindField(FeatureNode, TEXT("to_place"))
            : nullptr;
        if (!ToPlace) return TreeDecoratorFail(OutError, TEXT("pale_moss_patch's vegetation not a simple_block"));
        if (!Providers.Compile(Graph, *ToPlace, Tags, Predicates, Out.Provider, OutSkip, OutError)) return false;
        Out.Numbers.Append({{PaleMossDepth, Depth}, {PaleMossExtraBottom, ExtraBottom}, {PaleMossExtraEdge, ExtraEdge},
            {PaleMossVegetation, Vegetation}, {PaleMossRange, Range}, {PaleMossRadiusMin, Min},
            {PaleMossRadiusMax, Max}, {PaleMossReplaceable, double(Predicates.AddSet(*Set))}});
        return AddState(TEXT("pale_moss_block"), {}) &&
            AddState(TEXT("pale_hanging_moss"), {{FName(TEXT("tip")), True}}) &&
            AddState(TEXT("pale_hanging_moss"), {{FName(TEXT("tip")), FName(TEXT("false"))}}) &&
            AddState(TEXT("pale_moss_carpet"), {});
    }
    default:
        return true;
    }
}

int32 VoxelTreeDecorators::GetReach(
    const FVoxelTreeDecoratorConfig& Decorator, const FVoxelTreeConfig& Tree, int32 TreeReach)
{
    switch (Decorator.Type)
    {
    case EVoxelTreeDecorator::AlterGround:
    {
        // A circle's edge from the farthest centre: offsets -3..4, or the fixed centres at -1 and 2, plus 2, around
        // the trunk's bottom columns.
        int32 Reach = 0;
        for (const FIntPoint& Column : VoxelTrunkPlacers::GetFootprint(EVoxelTrunkPlacer(Tree.Trunk.Type)))
            for (const int32 Coordinate : {Column.X, Column.Y})
                Reach = FMath::Max(Reach,
                    FMath::Max(FMath::Abs(Coordinate - 3 - AlterGroundCircle),
                        FMath::Abs(Coordinate + 4 + AlterGroundCircle)));
        return Reach;
    }
    case EVoxelTreeDecorator::PlaceOnGround:
    {
        // The square's edge: the footprint's farthest column plus the radius.
        const int32 Radius = PlaceOnGroundRadius(Decorator);
        FIntPoint Min, Max;
        PlaceOnGroundFootprint(Tree, Min, Max);
        return FMath::Max(FMath::Max(FMath::Abs(Min.X - Radius), FMath::Abs(Max.X + Radius)),
            FMath::Max(FMath::Abs(Min.Y - Radius), FMath::Abs(Max.Y + Radius)));
    }
    case EVoxelTreeDecorator::PaleMoss:
    {
        // The hanging moss under a leaf, or the ground patch's edge: a bottom log's column plus the largest radius
        // plus one.
        const int32 Radius = FMath::Max(0, int32(Decorator.GetNumber(PaleMossRadiusMax))) + 1;
        FIntPoint Min, Max;
        PlaceOnGroundFootprint(Tree, Min, Max);
        return FMath::Max(TreeReach,
            FMath::Max(FMath::Max(FMath::Abs(Min.X - Radius), FMath::Abs(Max.X + Radius)),
                FMath::Max(FMath::Abs(Min.Y - Radius), FMath::Abs(Max.Y + Radius))));
    }
    case EVoxelTreeDecorator::CreakingHeart:
        // One of the logs.
        return VoxelTrunkPlacers::GetReach(Tree.Trunk);
    case EVoxelTreeDecorator::Beehive:
        // A nest beside a log, and the air south of it it reads (the S9c review: + 1 left that read out; the reach
        // only bounds the tree against the 3 x 3's margin when it compiles, so this changes no placement).
        return VoxelTrunkPlacers::GetReach(Tree.Trunk) + 2;
    case EVoxelTreeDecorator::Cocoa:
    case EVoxelTreeDecorator::TrunkVine:
    case EVoxelTreeDecorator::ShelfMushroom:
        // A block beside a log.
        return VoxelTrunkPlacers::GetReach(Tree.Trunk) + 1;
    case EVoxelTreeDecorator::LeaveVine:
        // A vine beside a leaf.
        return TreeReach + 1;
    case EVoxelTreeDecorator::AttachedToLogs:
        // On a log, or beside one in a horizontal direction.
        return VoxelTrunkPlacers::GetReach(Tree.Trunk) + ((Decorator.Directions & 0b1111) ? 1 : 0);
    case EVoxelTreeDecorator::AttachedToLeaves:
        // Under or over a leaf, or beside one in a horizontal direction, and the required empty cells it reads that
        // way.
        return TreeReach +
            ((Decorator.Directions & 0b1111) ? FMath::Max(1, int32(Decorator.GetNumber(AttachedEmptyBlocks))) : 0);
    default:
        return TreeReach;
    }
}

void VoxelTreeDecorators::Place(const FVoxelTreeConfig& Config, FVoxelTreeBuilder& Builder, FVoxelTreeTimes* Times)
{
    Place(Config.Decorators, Builder, Times);
}

void VoxelTreeDecorators::Place(
    TConstArrayView<FVoxelTreeDecoratorConfig> Decorators, FVoxelTreeBuilder& Builder, FVoxelTreeTimes* Times)
{
    for (const FVoxelTreeDecoratorConfig& Decorator : Decorators)
    {
        const uint64 Start = Times ? FPlatformTime::Cycles64() : 0;
        switch (Decorator.Type)
        {
        case EVoxelTreeDecorator::AlterGround:
            PlaceAlterGround(Decorator, Builder);
            break;
        case EVoxelTreeDecorator::Beehive:
            PlaceBeehive(Decorator, Builder);
            break;
        case EVoxelTreeDecorator::Cocoa:
            PlaceCocoa(Decorator, Builder);
            break;
        case EVoxelTreeDecorator::TrunkVine:
            PlaceTrunkVine(Decorator, Builder);
            break;
        case EVoxelTreeDecorator::LeaveVine:
            PlaceLeaveVine(Decorator, Builder);
            break;
        case EVoxelTreeDecorator::PlaceOnGround:
            PlaceOnGround(Decorator, Builder);
            break;
        case EVoxelTreeDecorator::ShelfMushroom:
            PlaceShelfMushroom(Decorator, Builder);
            break;
        case EVoxelTreeDecorator::PaleMoss:
            PlacePaleMoss(Decorator, Builder);
            break;
        case EVoxelTreeDecorator::CreakingHeart:
            PlaceCreakingHeart(Decorator, Builder);
            break;
        case EVoxelTreeDecorator::AttachedToLogs:
            PlaceAttachedToLogs(Decorator, Builder);
            break;
        case EVoxelTreeDecorator::AttachedToLeaves:
            PlaceAttachedToLeaves(Decorator, Builder);
            break;
        default:
            break;
        }
        if (!Times) continue;
        const int32 Stage = FVoxelTreeTimes::FirstDecorator + int32(Decorator.Type);
        Times->Cycles[Stage] += FPlatformTime::Cycles64() - Start;
        ++Times->Calls[Stage];
    }
}
