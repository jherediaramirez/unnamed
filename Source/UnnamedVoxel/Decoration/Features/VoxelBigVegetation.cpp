#include "Decoration/Features/VoxelBigVegetation.h"
#include "Blocks/VoxelBlockTable.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Decoration/Features/VoxelPatchFeatures.h"
#include "Decoration/VoxelBlockPredicates.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelStateProviders.h"
#include "VoxelConstants.h"

namespace
{
    bool BigVegetationFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    // The fallen log's four directions, in the decorators' side order (0 -X west, 1 +X east, 2 -Y north, 3 +Y south).
    const FIntVector FallenLogSteps[4] = {
        FIntVector(-1, 0, 0), FIntVector(1, 0, 0), FIntVector(0, -1, 0), FIntVector(0, 1, 0)};

    // A fallen tree's decorator: one that reads the logs it is given and writes beside or on them (the data's
    // trunk_vine, attached_to_logs and shelf_mushroom; cocoa reads the same way). The others read the whole tree.
    bool IsFallenDecorator(EVoxelTreeDecorator Type)
    {
        return Type == EVoxelTreeDecorator::TrunkVine || Type == EVoxelTreeDecorator::Cocoa ||
            Type == EVoxelTreeDecorator::ShelfMushroom || Type == EVoxelTreeDecorator::AttachedToLogs;
    }

    // The farthest a fallen decorator's block lies from the origin horizontally, given its logs' reach: on a log
    // (attached_to_logs up or down only) or beside one.
    int32 GetFallenDecoratorReach(const FVoxelTreeDecoratorConfig& Decorator, int32 LogReach)
    {
        const bool bBeside =
            Decorator.Type != EVoxelTreeDecorator::AttachedToLogs || (Decorator.Directions & 0b1111) != 0;
        return LogReach + (bBeside ? 1 : 0);
    }
}

bool VoxelBigVegetation::CompileFallenTree(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags,
    const FVoxelBlockTable& Table, FVoxelBlockPredicates& Predicates, FVoxelStateProviders& Providers,
    FVoxelFallenTreeConfig& Out, FString& OutSkip, FString* OutError)
{
    FVoxelTreeConfig& Tree = Out.Tree;
    Tree.Trunk.Type = uint8(EVoxelTrunkPlacer::Count);
    Tree.Foliage.Type = uint8(EVoxelFoliagePlacer::Count);
    const FVoxelFeatureValue* Trunk = Graph.FindField(Node, TEXT("trunk_provider"));
    const FVoxelFeatureValue* Length = Graph.FindField(Node, TEXT("log_length"));
    if (!Trunk || !Length) return BigVegetationFail(OutError, TEXT("fallen_tree without trunk_provider or log_length"));
    if (!Providers.Compile(Graph, *Trunk, Tags, Predicates, Tree.TrunkProvider, OutSkip, OutError)) return false;
    if (!OutSkip.IsEmpty()) return true;
    if (!VoxelTreeFeature::SetLogStates(Table, Providers, Tree, OutError) ||
        !VoxelTreeFeature::SetFreeSet(Tags, Predicates, Tree, OutError) ||
        !VoxelNumberProviders::CompileInt(Graph, *Length, Out.LogLength, OutError) ||
        !VoxelTreeFeature::CompileDecorators(Graph, Graph.FindField(Node, TEXT("stump_decorators")), Tags, Table,
            Predicates, Providers, Tree.Decorators, Tree.SkippedDecorators, OutError) ||
        !VoxelTreeFeature::CompileDecorators(Graph, Graph.FindField(Node, TEXT("log_decorators")), Tags, Table,
            Predicates, Providers, Out.LogDecorators, Tree.SkippedDecorators, OutError))
        return false;
    for (TArray<FVoxelTreeDecoratorConfig>* List : {&Tree.Decorators, &Out.LogDecorators})
        for (int32 Index = List->Num() - 1; Index >= 0; --Index)
            if (!IsFallenDecorator((*List)[Index].Type))
            {
                Tree.SkippedDecorators.Add(
                    FString(TEXT("minecraft:")) + VoxelTreeDecorators::GetName((*List)[Index].Type));
                List->RemoveAt(Index);
            }

    // The log's far end lies at most log_length from the stump (PlaceFallenTree); the stump's decorators beside it.
    const int32 LogReach = FMath::Max(0, Out.LogLength.Max);
    Tree.Reach = LogReach;
    for (const FVoxelTreeDecoratorConfig& Decorator : Tree.Decorators)
        Tree.Reach = FMath::Max(Tree.Reach, GetFallenDecoratorReach(Decorator, 0));
    for (const FVoxelTreeDecoratorConfig& Decorator : Out.LogDecorators)
        Tree.Reach = FMath::Max(Tree.Reach, GetFallenDecoratorReach(Decorator, LogReach));
    ++Tree.Reach;
    if (Tree.Reach > VoxelConstants::ColumnSize)
        return BigVegetationFail(OutError,
            FString::Printf(TEXT("a fallen tree reaching %d blocks, past the 3 x 3's margin of %d"), Tree.Reach,
                VoxelConstants::ColumnSize));
    return true;
}

// The stump: one log (axis y) at the origin, where the cell is free. The log (ours, fitted to the six probes: 600
// trees on flat ground): one of the four directions uniformly (the probes' 166, 134, 152 and 148), log_length - 2
// cells (oak's 4..7 gives 2..5; every probe's farthest log lies at its largest log_length) starting 2 or 3 from the
// stump with even odds (306 and 294: the wiki's "often found 1-2 blocks from the stump"), along that axis. It goes down
// only where every cell is free and the block under each is not (ours: the probes stand on flat ground); otherwise the
// stump stands alone (the wiki: the log "does not always generate"). Then the stump's decorators on the stump and the
// log's on the log, both drawing from the tree's stream; the log is set from the stump outwards, so a decorator reads
// its cells in that order. Draws: NextInt(4) for the direction, log_length's draws, NextInt(2) for the gap, then the
// decorators'.
bool VoxelBigVegetation::PlaceFallenTree(const FVoxelFallenTreeConfig& Config, FIntVector Origin,
    FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env, FVoxelTreeTimes* Times)
{
    uint64 Start = Times ? FPlatformTime::Cycles64() : 0;
    // Adds the time since Start to the fallen trees' stage (only when timed).
    const auto Lap = [Times, &Start]
    {
        if (Times) Times->Cycles[FVoxelTreeTimes::FallenTree] += FPlatformTime::Cycles64() - Start;
    };
    if (Times) ++Times->Calls[FVoxelTreeTimes::FallenTree];
    FVoxelTreeBuilder Builder(Config.Tree, Env, Random);
    Builder.Origin = Origin;
    if (!Builder.SetLog(Origin))
    {
        Lap();
        return false;
    }
    const int32 Side = Random.NextInt(4);
    const int32 Length = FMath::Max(0, Config.LogLength.Sample(Random) - 2);
    const int32 First = 2 + Random.NextInt(2);
    bool bLog = Length > 0;
    for (int32 Index = 0; bLog && Index < Length; ++Index)
    {
        const FIntVector Cell = Origin + FallenLogSteps[Side] * (First + Index);
        const FIntVector Below = Cell - FIntVector(0, 0, 1);
        bLog = Builder.IsFree(Cell) && Below.Z >= VoxelConstants::MinZ && !Builder.IsFree(Below);
    }
    if (bLog)
        for (int32 Index = 0; Index < Length; ++Index)
            Builder.SetLog(Origin + FallenLogSteps[Side] * (First + Index), Side < 2 ? 0 : 1);
    Lap();

    // The stump is cell 0 and the log cells 1 to Length.
    Builder.DecoratedEnd = 1;
    VoxelTreeDecorators::Place(Config.Tree.Decorators, Builder, Times);
    if (bLog)
    {
        Builder.DecoratedBegin = 1;
        Builder.DecoratedEnd = 1 + Length;
        VoxelTreeDecorators::Place(Config.LogDecorators, Builder, Times);
    }
    if (Times) Start = FPlatformTime::Cycles64();

    // As VoxelTreeFeature::Place emits (a fallen tree has no leaves): the logs into air or replaceable_by_trees, the
    // decorations only where the cell still holds what this Decorate saw.
    using ECell = FVoxelTreeBuilder::ECell;
    for (const FVoxelTreeBuilder::FCell& Cell : Builder.Cells)
    {
        const FIntVector At = Cell.Position;
        if (Cell.Kind == ECell::Log)
            Env.Emit({At, Cell.State, EVoxelWriteMode::IfReplaceable, uint16(Config.Tree.FreeSet)});
        else if (Cell.Kind == ECell::Root || Cell.Kind == ECell::Ground || Cell.Kind == ECell::Decoration)
            Env.Emit({At, Cell.State, EVoxelWriteMode::IfUnchanged, Env.Context.GetState(At.X, At.Y, At.Z)});
    }
    Lap();
    return true;
}

// The cap (minecraft.wiki "Huge mushroom"; the probes' examples give every cell and face): a brown cap is one layer
// of (2r + 1)^2 without its corners at the top; a red cap a (2r - 1)^2 top over four walls, each 2r - 1 wide and 3
// high, at distance r (45 blocks each at the data's r 3 and 2). A face is true where the cell beside it holds no cap
// block, except the bottom face and a red wall's inner face (toward the stem, inside the cap's radius).
bool VoxelBigVegetation::CompileHugeMushroom(const FVoxelFeatureGraph& Graph, int32 Node, bool bRed,
    FVoxelBlockTags& Tags, const FVoxelBlockTable& Table, FVoxelBlockPredicates& Predicates,
    FVoxelStateProviders& Providers, FVoxelHugeMushroomConfig& Out, FString& OutSkip, FString* OutError)
{
    Out.bRed = bRed;
    const FVoxelFeatureValue* Radius = Graph.FindField(Node, TEXT("foliage_radius"));
    Out.Radius = Radius && Radius->Type == EVoxelFeatureValueType::Number ? int32(Radius->Number) : 2;
    if (Out.Radius < 1 || Out.Radius > 8) return BigVegetationFail(OutError, TEXT("a huge mushroom's radius not read"));
    const auto State = [&](const TCHAR* Key, uint16& OutState) -> bool
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        int32 Provider = INDEX_NONE;
        if (!Value) return BigVegetationFail(OutError, FString::Printf(TEXT("a huge mushroom without %s"), Key));
        if (!Providers.Compile(Graph, *Value, Tags, Predicates, Provider, OutSkip, OutError)) return false;
        if (!OutSkip.IsEmpty()) return true;
        TArray<uint16> States;
        Providers.GetStates(Provider, States);
        if (States.Num() != 1)
            return BigVegetationFail(OutError, FString::Printf(TEXT("a huge mushroom's %s not one state"), Key));
        OutState = States[0];
        return true;
    };
    uint16 CapState = 0;
    if (!State(TEXT("stem_provider"), Out.Stem) || (OutSkip.IsEmpty() && !State(TEXT("cap_provider"), CapState)))
        return false;
    if (!OutSkip.IsEmpty()) return true;
    const FVoxelFeatureValue* CanPlaceOn = Graph.FindField(Node, TEXT("can_place_on"));
    if (!CanPlaceOn) return BigVegetationFail(OutError, TEXT("a huge mushroom without can_place_on"));
    if (!Predicates.Compile(Graph, Graph.GetNode(*CanPlaceOn), Tags, Out.CanPlaceOn, OutSkip, OutError)) return false;
    if (!OutSkip.IsEmpty()) return true;
    const FVoxelBlockSet* Replaceable = Tags.Find(TEXT("minecraft:replaceable_by_trees"), OutError);
    if (!Replaceable) return false;
    FVoxelBlockSet Free = *Replaceable;
    Free.Add(0);
    Out.FreeSet = Predicates.AddSet(MoveTemp(Free));

    const int32 R = Out.Radius;
    TSet<FIntVector> Cells;
    for (int32 DZ = bRed ? -3 : 0; DZ <= 0; ++DZ)
        for (int32 DY = -R; DY <= R; ++DY)
            for (int32 DX = -R; DX <= R; ++DX)
            {
                const int32 Ring = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
                const bool bCorner = FMath::Abs(DX) == R && FMath::Abs(DY) == R;
                if (bRed ? (DZ == 0 ? Ring < R : Ring == R && !bCorner) : !bCorner) Cells.Add(FIntVector(DX, DY, DZ));
            }
    // Our face steps and the reference's names: west, east, north, south, down, up.
    static const FName Faces[6] = {TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south"), TEXT("down"), TEXT("up")};
    static const FIntVector Steps[6] = {FIntVector(-1, 0, 0), FIntVector(1, 0, 0), FIntVector(0, -1, 0),
        FIntVector(0, 1, 0), FIntVector(0, 0, -1), FIntVector(0, 0, 1)};
    static const FName True(TEXT("true")), False(TEXT("false"));
    for (const FIntVector& Cell : Cells)
    {
        int32 Cap = CapState;
        for (int32 Face = 0; Face < 6 && Cap != INDEX_NONE; ++Face)
        {
            const FIntVector Beside = Cell + Steps[Face];
            const bool bInside = Face < 4 && FMath::Max(FMath::Abs(Beside.X), FMath::Abs(Beside.Y)) < R;
            const bool bOpen = Face != 4 && !Cells.Contains(Beside) && !bInside;
            Cap = Table.WithValue(uint16(Cap), Faces[Face], bOpen ? True : False);
        }
        if (Cap == INDEX_NONE) return BigVegetationFail(OutError, TEXT("a huge mushroom's cap faces not in the table"));
        Out.Cap.Add({Cell, uint16(Cap)});
    }
    // In Z, then Y, then X order, so the emission's order does not hang on the set's.
    Out.Cap.Sort(
        [](const TPair<FIntVector, uint16>& A, const TPair<FIntVector, uint16>& B)
        {
            return A.Key.Z != B.Key.Z ? A.Key.Z < B.Key.Z : A.Key.Y != B.Key.Y ? A.Key.Y < B.Key.Y : A.Key.X < B.Key.X;
        });
    return true;
}

// The height (wiki: "normally between 5 and 7 blocks inclusive", with a 1 in 12 chance of twice as high; the probes:
// the cap's top at dy 4, 5 or 6, about 30% each, and at 8, 10 or 12, about 3% each, over h stem blocks at dy 0 to h -
// 1): h is 4 + NextInt(3), doubled when NextInt(12) is 0. The checks (ours, after the wiki's growth rules; the probes
// stand on open flat ground): the block under the origin passes can_place_on; the stem's column up to the cap's top is
// free (air or replaceable_by_trees) for a red mushroom ("only the blocks directly above"), and for a brown one its
// first three cells and then the cap's square from dy 3 up to the top ("a 7 x (height - 3) x 7 region"); otherwise
// it fails, writing nothing. The stem then the cap go where the cell is free, the rest stays. Draws: NextInt(3), then
// NextInt(12).
bool VoxelBigVegetation::PlaceHugeMushroom(const FVoxelHugeMushroomConfig& Config, FIntVector Origin,
    FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env, FVoxelTreeTimes* Times)
{
    const uint64 Start = Times ? FPlatformTime::Cycles64() : 0;
    // Adds the time since Start to the huge mushrooms' stage (only when timed); returns bPlaced.
    const auto Done = [Times, Start](bool bPlaced)
    {
        if (!Times) return bPlaced;
        Times->Cycles[FVoxelTreeTimes::HugeMushroom] += FPlatformTime::Cycles64() - Start;
        ++Times->Calls[FVoxelTreeTimes::HugeMushroom];
        return bPlaced;
    };
    int32 Height = 4 + Random.NextInt(3);
    if (Random.NextInt(12) == 0) Height *= 2;
    if (Origin.Z - 1 < VoxelConstants::MinZ || Origin.Z + Height > VoxelConstants::MaxZ ||
        !Env.Predicates.Test(Config.CanPlaceOn, Env.Context, Origin - FIntVector(0, 0, 1)))
        return Done(false);
    const FVoxelBlockSet& Free = Env.Predicates.GetSet(Config.FreeSet);
    const auto IsFree = [&Env, &Free](int32 X, int32 Y, int32 Z)
    { return Free.Contains(Env.Table.Get(Env.Context.GetState(X, Y, Z)).Block); };
    const int32 Square = Config.bRed ? 0 : Config.Radius;
    for (int32 DZ = 0; DZ <= Height; ++DZ)
    {
        const int32 Reach = DZ < 3 ? 0 : Square;
        for (int32 DY = -Reach; DY <= Reach; ++DY)
            for (int32 DX = -Reach; DX <= Reach; ++DX)
                if (!IsFree(Origin.X + DX, Origin.Y + DY, Origin.Z + DZ)) return Done(false);
    }
    for (int32 DZ = 0; DZ < Height; ++DZ)
        Env.Emit({Origin + FIntVector(0, 0, DZ), Config.Stem, EVoxelWriteMode::IfReplaceable, uint16(Config.FreeSet)});
    const FIntVector Top = Origin + FIntVector(0, 0, Height);
    for (const TPair<FIntVector, uint16>& Cell : Config.Cap)
        Env.Emit({Top + Cell.Key, Cell.Value, EVoxelWriteMode::IfReplaceable, uint16(Config.FreeSet)});
    return Done(true);
}

bool VoxelBigVegetation::CompileRootSystem(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags,
    const FVoxelBlockTable& Table, FVoxelBlockPredicates& Predicates, FVoxelStateProviders& Providers,
    FVoxelRootSystemConfig& Out, FString& OutSkip, FString* OutError)
{
    // feature: an inline placed feature of no placement over the tree's configured feature.
    const FVoxelFeatureValue* Placed = Graph.FindField(Node, TEXT("feature"));
    const int32 PlacedNode = Placed ? Graph.GetNode(*Placed) : INDEX_NONE;
    const FVoxelFeatureValue* Inner = PlacedNode == INDEX_NONE ? nullptr : Graph.FindField(PlacedNode, TEXT("feature"));
    const FVoxelFeatureValue* Placement =
        PlacedNode == INDEX_NONE ? nullptr : Graph.FindField(PlacedNode, TEXT("placement"));
    const int32 TreeNode = Inner ? Graph.GetNode(*Inner) : INDEX_NONE;
    if (TreeNode == INDEX_NONE) return BigVegetationFail(OutError, TEXT("root_system without its feature"));
    if (Placement && Placement->Type == EVoxelFeatureValueType::List && Placement->Count > 0)
    {
        OutSkip = TEXT("root_system over a placed feature with modifiers");
        return true;
    }
    if (Graph.GetType(TreeNode) != TEXT("minecraft:tree"))
    {
        OutSkip = FString::Printf(TEXT("root_system of %s"), *FString(Graph.GetType(TreeNode)));
        return true;
    }
    if (!VoxelTreeFeature::Compile(Graph, TreeNode, Tags, Table, Predicates, Providers, Out.Tree, OutSkip, OutError))
        return false;
    if (!OutSkip.IsEmpty()) return true;

    const auto Int = [&](const TCHAR* Key, int32& OutValue) -> bool
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        if (!Value || Value->Type != EVoxelFeatureValueType::Number)
            return BigVegetationFail(OutError, FString::Printf(TEXT("root_system without %s"), Key));
        OutValue = int32(Value->Number);
        return true;
    };
    if (!Int(TEXT("required_vertical_space_for_tree"), Out.RequiredVerticalSpace) ||
        !Int(TEXT("allowed_vertical_water_for_tree"), Out.AllowedVerticalWater) ||
        !Int(TEXT("root_column_max_height"), Out.RootColumnMaxHeight) || !Int(TEXT("root_radius"), Out.RootRadius) ||
        !Int(TEXT("root_placement_attempts"), Out.RootPlacementAttempts) ||
        !Int(TEXT("hanging_root_radius"), Out.HangingRootRadius) ||
        !Int(TEXT("hanging_root_placement_attempts"), Out.HangingRootAttempts) ||
        !Int(TEXT("hanging_roots_vertical_span"), Out.HangingRootsVerticalSpan))
        return false;
    const auto Provider = [&](const TCHAR* Key, int32& OutProvider) -> bool
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        if (!Value) return BigVegetationFail(OutError, FString::Printf(TEXT("root_system without %s"), Key));
        return Providers.Compile(Graph, *Value, Tags, Predicates, OutProvider, OutSkip, OutError);
    };
    if (!Provider(TEXT("root_state_provider"), Out.RootProvider) ||
        (OutSkip.IsEmpty() && !Provider(TEXT("hanging_root_state_provider"), Out.HangingRootProvider)))
        return false;
    if (!OutSkip.IsEmpty()) return true;
    const FVoxelFeatureValue* Allowed = Graph.FindField(Node, TEXT("allowed_tree_position"));
    if (!Allowed) return BigVegetationFail(OutError, TEXT("root_system without allowed_tree_position"));
    if (!Predicates.Compile(Graph, Graph.GetNode(*Allowed), Tags, Out.AllowedTreePosition, OutSkip, OutError))
        return false;
    if (!OutSkip.IsEmpty()) return true;
    FVoxelBlockSet Replaceable;
    if (!VoxelPredicateReading::ReadBlockSet(
            Graph, Graph.FindField(Node, TEXT("root_replaceable")), Tags, Replaceable, OutError))
        return false;
    Out.RootReplaceable = Predicates.AddSet(MoveTemp(Replaceable));
    // The tree stands over the origin's column, so its reach holds; the roots' squares lie within their radii.
    const int32 Reach = FMath::Max(Out.Tree.Reach, FMath::Max(Out.RootRadius, Out.HangingRootRadius) + 1);
    if (Reach > VoxelConstants::ColumnSize)
        return BigVegetationFail(OutError, FString::Printf(TEXT("a root system reaching %d blocks"), Reach));
    return true;
}

// Ours, fitted to scout J's rooted_azalea_tree probe (100 placements under a 10-block stone and dirt ceiling; S9e's
// fit, C:\dev\sandbox\phase6s9\s9e\fit\f3a-rootsystem): the data and the wiki give no algorithm, only the wiki's
// "rooted dirt randomly replaces blocks in a 5x5 cross-section centered on the tree until it reaches the lush caves".
// - The tree's spot: from the origin up to root_column_max_height above it, the first cell passing
//   allowed_tree_position whose required_vertical_space_for_tree cells from it up are free (air or
//   replaceable_by_trees), allowing up to allowed_vertical_water_for_tree water cells among them. None: it fails,
//   writing nothing. (level_test_distance and max_level_deviation, 0 in the data, are not read.)
// - The roots: from two below the spot (the tree's own soil goes one below) down while the column's cell is in
//   root_replaceable (at most root_column_max_height layers), per layer root_placement_attempts tries at
//   (NextInt(r) - NextInt(r), NextInt(r) - NextInt(r)) for r = root_radius (-2..2, triangular), each writing the root
//   state where the cell is in root_replaceable (the probe: 115 per placement, reach 2).
// - The hanging roots: hanging_root_placement_attempts tries at the same triangular offsets for hanging_root_radius,
//   NextInt(hanging_roots_vertical_span) below the cell under the lowest root layer, each writing the hanging root
//   state where the cell is air and the cell above is neither air, hanging roots nor water (the probe: 8, all directly
//   under the ceiling).
// - Then the tree at the spot (its draws follow). Its failure keeps the roots (ours) and returns false.
// Draws: per root try two NextInt pairs and the provider's; per hanging try two NextInt pairs, NextInt(span) and the
// provider's; then the tree's.
bool VoxelBigVegetation::PlaceRootSystem(const FVoxelRootSystemConfig& Config, FIntVector Origin,
    FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env, FVoxelTreeTimes* Times)
{
    const uint64 Start = Times ? FPlatformTime::Cycles64() : 0;
    // Adds the time since Start to the root systems' stage (only when timed).
    const auto Lap = [Times, Start]
    {
        if (!Times) return;
        Times->Cycles[FVoxelTreeTimes::RootSystem] += FPlatformTime::Cycles64() - Start;
        ++Times->Calls[FVoxelTreeTimes::RootSystem];
    };
    const FVoxelDecorationContext& Context = Env.Context;
    const FVoxelBlockSet& Free = Env.Predicates.GetSet(Config.Tree.FreeSet);
    const FVoxelBlockSet& Replaceable = Env.Predicates.GetSet(Config.RootReplaceable);
    const auto BlockAt = [&](const FIntVector& Cell)
    { return Env.Table.Get(Context.GetState(Cell.X, Cell.Y, Cell.Z)).Block; };
    const auto IsInside = [](const FIntVector& Cell)
    { return Cell.Z >= VoxelConstants::MinZ && Cell.Z <= VoxelConstants::MaxZ; };

    // The tree's spot.
    int32 Spot = INDEX_NONE;
    for (int32 DZ = 0; DZ <= Config.RootColumnMaxHeight && Spot == INDEX_NONE; ++DZ)
    {
        const FIntVector Cell = Origin + FIntVector(0, 0, DZ);
        if (Cell.Z - 1 < VoxelConstants::MinZ || Cell.Z + Config.RequiredVerticalSpace > VoxelConstants::MaxZ) break;
        // The space's first cell before the predicate (S9e's cut, exact): a spot is free or water itself, which most
        // of the column (rock) is not.
        const uint16 Here = Context.GetState(Cell.X, Cell.Y, Cell.Z);
        if (Config.RequiredVerticalSpace > 0 &&
            (Env.Survival.IsWaterSource(Here) ? Config.AllowedVerticalWater < 1
                                              : !Free.Contains(Env.Table.Get(Here).Block)))
            continue;
        if (!Env.Predicates.Test(Config.AllowedTreePosition, Context, Cell)) continue;
        int32 Water = 0;
        bool bSpace = true;
        for (int32 Up = 0; Up < Config.RequiredVerticalSpace && bSpace; ++Up)
        {
            const FIntVector Above = Cell + FIntVector(0, 0, Up);
            if (Env.Survival.IsWaterSource(Context.GetState(Above.X, Above.Y, Above.Z)))
                bSpace = ++Water <= Config.AllowedVerticalWater;
            else
                bSpace = Free.Contains(BlockAt(Above));
        }
        if (bSpace) Spot = Cell.Z;
    }
    if (Spot == INDEX_NONE)
    {
        Lap();
        return false;
    }

    // The roots, each cell once; a cell this system wrote reads as its new state (the working copy has it in a
    // Decorate, not in the tests' environment). Its cells lie in a box under the spot, kept as a flat grid (S9e's cut:
    // a TMap cost most of a root system's time).
    const int32 Half = FMath::Max(FMath::Max(Config.RootRadius, Config.HangingRootRadius) - 1, 0), Side = 2 * Half + 1;
    const int32 Depth = Config.RootColumnMaxHeight + FMath::Max(Config.HangingRootsVerticalSpan, 0) + 2;
    constexpr uint16 Unwritten = MAX_uint16;
    TArray<uint16, TInlineAllocator<4096>> Own;
    Own.Init(Unwritten, Side * Side * Depth);
    const auto OwnIndex = [&](const FIntVector& Cell)
    {
        const int32 DX = Cell.X - Origin.X + Half, DY = Cell.Y - Origin.Y + Half, DZ = Spot - 1 - Cell.Z;
        return DX >= 0 && DX < Side && DY >= 0 && DY < Side && DZ >= 0 && DZ < Depth ? (DZ * Side + DY) * Side + DX
                                                                                     : INDEX_NONE;
    };
    const auto GetState = [&](const FIntVector& Cell)
    {
        const int32 Index = OwnIndex(Cell);
        return Index != INDEX_NONE && Own[Index] != Unwritten ? Own[Index] : Context.GetState(Cell.X, Cell.Y, Cell.Z);
    };
    const auto Write = [&](const FIntVector& Cell, int32 Provider)
    {
        const int32 Index = OwnIndex(Cell);
        if (Index == INDEX_NONE || Own[Index] != Unwritten) return;
        const uint16 Was = Context.GetState(Cell.X, Cell.Y, Cell.Z);
        const uint16 State = Env.Providers.Sample(Provider, Random, Cell, Env.Context);
        Own[Index] = State;
        Env.Emit({Cell, State, EVoxelWriteMode::IfUnchanged, Was});
    };
    // One offset from a pair of draws, in order.
    const auto Offset = [&Random](int32 Radius)
    {
        if (Radius <= 0) return 0;
        const int32 Plus = Random.NextInt(Radius);
        return Plus - Random.NextInt(Radius);
    };
    int32 Bottom = Spot - 1;
    for (int32 Layer = 0; Layer < Config.RootColumnMaxHeight; ++Layer)
    {
        const FIntVector Center(Origin.X, Origin.Y, Spot - 2 - Layer);
        if (!IsInside(Center) || !Replaceable.Contains(BlockAt(Center))) break;
        Bottom = Center.Z;
        for (int32 Try = 0; Try < Config.RootPlacementAttempts; ++Try)
        {
            const int32 DX = Offset(Config.RootRadius);
            const int32 DY = Offset(Config.RootRadius);
            const FIntVector Cell = Center + FIntVector(DX, DY, 0);
            if (Replaceable.Contains(Env.Table.Get(GetState(Cell)).Block)) Write(Cell, Config.RootProvider);
        }
    }
    const int32 Air = Env.Table.FindBlock(TEXT("air")), Hanging = Env.Table.FindBlock(TEXT("hanging_roots"));
    for (int32 Try = 0; Try < Config.HangingRootAttempts; ++Try)
    {
        const int32 DX = Offset(Config.HangingRootRadius);
        const int32 DY = Offset(Config.HangingRootRadius);
        const int32 Down = Config.HangingRootsVerticalSpan > 0 ? Random.NextInt(Config.HangingRootsVerticalSpan) : 0;
        const FIntVector Cell(Origin.X + DX, Origin.Y + DY, Bottom - 1 - Down);
        const FIntVector Above = Cell + FIntVector(0, 0, 1);
        if (!IsInside(Cell) || !IsInside(Above) || Env.Table.Get(GetState(Cell)).Block != Air) continue;
        const uint16 AboveState = GetState(Above);
        const int32 AboveBlock = Env.Table.Get(AboveState).Block;
        if (AboveBlock == Air || AboveBlock == Hanging || Env.Survival.IsWaterSource(AboveState)) continue;
        Write(Cell, Config.HangingRootProvider);
    }
    Lap();
    return VoxelTreeFeature::Place(Config.Tree, FIntVector(Origin.X, Origin.Y, Spot), Random, Env, Times);
}
