#include "Decoration/Features/VoxelTreeFeature.h"
#include "Algo/StableSort.h"
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
    bool TreeFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    FStringView TreeFieldName(const FVoxelFeatureGraph& Graph, const FVoxelFeatureField& Field)
    {
        return Field.Key == INDEX_NONE ? FStringView() : FStringView(Graph.Strings[Field.Key]);
    }

    // A placer node's fields by name (its type left out): ints, numbers and one block list.
    bool CompilePlacerFields(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags,
        FVoxelBlockPredicates& Predicates, FVoxelTreePlacerConfig& Out, FString* OutError)
    {
        const FVoxelFeatureNode& Placer = Graph.Nodes[Node];
        for (int32 Index = 0; Index < Placer.FieldCount; ++Index)
        {
            const FVoxelFeatureField& Field = Graph.Fields[Placer.FirstField + Index];
            const FName Name(TreeFieldName(Graph, Field));
            if (Name == TEXT("type")) continue;
            const FVoxelFeatureValue& Value = Field.Value;
            if (Value.Type == EVoxelFeatureValueType::Number || Value.Type == EVoxelFeatureValueType::Bool)
            {
                Out.Numbers.Add({Name, Value.Number});
                if (Value.Number == FMath::RoundToDouble(Value.Number))
                    Out.Ints.Add({Name, FVoxelIntProvider::MakeConstant(int32(Value.Number))});
            }
            else if (Value.Type == EVoxelFeatureValueType::Node)
            {
                FVoxelIntProvider Provider;
                FString Error;
                if (VoxelNumberProviders::CompileInt(Graph, Value, Provider, &Error))
                    Out.Ints.Add({Name, Provider});
                else if (const int32 Range = Graph.GetNode(Value); Range != INDEX_NONE)
                {
                    // An untyped {min_inclusive, max_inclusive} (cherry_trunk_placer's branch_start_offset_from_top)
                    // reads as uniform.
                    const FVoxelFeatureValue* Min = Graph.FindField(Range, TEXT("min_inclusive"));
                    const FVoxelFeatureValue* Max = Graph.FindField(Range, TEXT("max_inclusive"));
                    if (Min && Max && Min->Type == EVoxelFeatureValueType::Number &&
                        Max->Type == EVoxelFeatureValueType::Number)
                        Out.Ints.Add({Name, FVoxelIntProvider::MakeUniform(int32(Min->Number), int32(Max->Number))});
                    else
                        return TreeFail(OutError,
                            FString::Printf(TEXT("placer field %s is not an int provider"), *Name.ToString()));
                }
                else
                    return TreeFail(
                        OutError, FString::Printf(TEXT("placer field %s is not an int provider"), *Name.ToString()));
            }
            else if (Value.Type == EVoxelFeatureValueType::String || Value.Type == EVoxelFeatureValueType::List)
            {
                FVoxelBlockSet Set;
                if (!VoxelPredicateReading::ReadBlockSet(Graph, &Value, Tags, Set, OutError)) return false;
                Out.BlockSet = Predicates.AddSet(MoveTemp(Set));
            }
        }
        return true;
    }

    int32 TreeInt(const FVoxelFeatureGraph& Graph, int32 Node, const TCHAR* Key, int32 Default)
    {
        const FVoxelFeatureValue* Value = Node == INDEX_NONE ? nullptr : Graph.FindField(Node, Key);
        return Value && (Value->Type == EVoxelFeatureValueType::Number || Value->Type == EVoxelFeatureValueType::Bool)
            ? int32(Value->Number)
            : Default;
    }

    // The FIntVector step of an axis (0 X, 1 Y, 2 Z).
    constexpr int32 TreeSteps[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
}

const FVoxelIntProvider* FVoxelTreePlacerConfig::FindInt(FName Name) const
{
    for (const TPair<FName, FVoxelIntProvider>& Pair : Ints)
        if (Pair.Key == Name) return &Pair.Value;
    return nullptr;
}

int32 FVoxelTreePlacerConfig::SampleInt(FName Name, FVoxelXoroshiro& Random, int32 Default) const
{
    const FVoxelIntProvider* Provider = FindInt(Name);
    return Provider ? Provider->Sample(Random) : Default;
}

int32 FVoxelTreePlacerConfig::GetMaxInt(FName Name, int32 Default) const
{
    const FVoxelIntProvider* Provider = FindInt(Name);
    return Provider ? Provider->Max : Default;
}

double FVoxelTreePlacerConfig::GetNumber(FName Name, double Default) const
{
    for (const TPair<FName, double>& Pair : Numbers)
        if (Pair.Key == Name) return Pair.Value;
    return Default;
}

FVoxelTreeBuilder::FVoxelTreeBuilder(
    const FVoxelTreeConfig& InConfig, const FVoxelPlantEnvironment& InEnv, FVoxelXoroshiro& InRandom)
    : Config(InConfig), Env(InEnv), Random(InRandom)
{
}

uint16 FVoxelTreeBuilder::GetState(FIntVector Cell) const
{
    if (const int32* Index = FindCell(Cell)) return Cells[*Index].State;
    return Env.Context.GetState(Cell.X, Cell.Y, Cell.Z);
}

bool FVoxelTreeBuilder::IsFree(FIntVector Cell) const
{
    if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return false;
    if (const int32* Index = FindCell(Cell)) return Cells[*Index].Kind == ECell::Leaf;
    return Env.Predicates.GetSet(Config.FreeSet)
        .Contains(Env.Table.Get(Env.Context.GetState(Cell.X, Cell.Y, Cell.Z)).Block);
}

bool FVoxelTreeBuilder::IsLog(FIntVector Cell) const
{
    const int32* Index = FindCell(Cell);
    return Index && Cells[*Index].Kind == ECell::Log;
}

bool FVoxelTreeBuilder::IsLeaf(FIntVector Cell) const
{
    const int32* Index = FindCell(Cell);
    return Index && Cells[*Index].Kind == ECell::Leaf;
}

void FVoxelTreeBuilder::Set(FIntVector Cell, uint16 State, ECell Kind)
{
    if (int32* Index = FindCell(Cell))
    {
        Cells[*Index].State = State;
        Cells[*Index].Kind = Kind;
        return;
    }
    IndexOf.Add(Cell, Cells.Add({Cell, State, Kind}));
}

bool FVoxelTreeBuilder::SetLog(FIntVector Cell, int32 Axis)
{
    if (!IsLog(Cell) && !IsFree(Cell)) return false;
    Set(Cell, Config.LogStates[FMath::Clamp(Axis, 0, 2)], ECell::Log);
    return true;
}

bool FVoxelTreeBuilder::SetLeaf(FIntVector Cell)
{
    if (FindCell(Cell) || !IsFree(Cell)) return IsLeaf(Cell);
    // The state's distance is set when the tree is emitted.
    Set(Cell, Env.Providers.Sample(Config.FoliageProvider, Random, Cell, Env.Context), ECell::Leaf);
    return true;
}

bool FVoxelTreeBuilder::SetRoot(FIntVector Cell, uint16 State)
{
    if (IsLog(Cell) || Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return false;
    Set(Cell, State, ECell::Root);
    return true;
}

void FVoxelTreeBuilder::SetGroundBelow(FIntVector Cell)
{
    if (Config.BelowTrunkProvider == INDEX_NONE || IsLog(Cell)) return;
    const uint16 State = Env.Providers.Sample(Config.BelowTrunkProvider, Random, Cell, Env.Context);
    if (State != GetState(Cell)) Set(Cell, State, ECell::Ground);
}

bool FVoxelTreeBuilder::IsAir(FIntVector Cell) const
{
    return Cell.Z >= VoxelConstants::MinZ && Cell.Z <= VoxelConstants::MaxZ && Env.Table.Get(GetState(Cell)).IsAir();
}

bool FVoxelTreeBuilder::GetKind(FIntVector Cell, ECell& OutKind) const
{
    const int32* Index = FindCell(Cell);
    if (Index) OutKind = Cells[*Index].Kind;
    return Index != nullptr;
}

bool FVoxelTreeBuilder::SetDecoration(FIntVector Cell, uint16 State)
{
    if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return false;
    if (const int32* Index = FindCell(Cell))
    {
        FCell& Own = Cells[*Index];
        if (Own.Kind != ECell::Ground && Own.Kind != ECell::Decoration) return false;
        Own.State = State;
        return true;
    }
    Set(Cell, State, ECell::Decoration);
    return true;
}

void FVoxelTreeBuilder::ReplaceWithDecoration(int32 Index, uint16 State)
{
    Cells[Index].State = State;
    Cells[Index].Kind = ECell::Decoration;
}

void FVoxelTreeBuilder::Drop(int32 Index)
{
    IndexOf.Remove(Cells[Index].Position);
    Cells[Index].Kind = ECell::Dropped;
}

void FVoxelTreeBuilder::GetCells(ECell Kind, TArray<FIntVector>& Out) const
{
    Out.Reset();
    for (const FCell& Cell : Cells)
        if (Cell.Kind == Kind) Out.Add(Cell.Position);
    Algo::StableSortBy(Out, [](const FIntVector& Cell) { return Cell.Z; });
}

bool VoxelTreeFeature::Compile(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags,
    const FVoxelBlockTable& Table, FVoxelBlockPredicates& Predicates, FVoxelStateProviders& Providers,
    FVoxelTreeConfig& Out, FString& OutSkip, FString* OutError)
{
    const auto NodeOf = [&](const TCHAR* Key) -> int32
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        return Value ? Graph.GetNode(*Value) : INDEX_NONE;
    };
    const auto Provider = [&](const TCHAR* Key, int32& OutProvider, bool bRequired) -> bool
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        if (!Value) return !bRequired || TreeFail(OutError, FString::Printf(TEXT("tree without %s"), Key));
        return Providers.Compile(Graph, *Value, Tags, Predicates, OutProvider, OutSkip, OutError);
    };

    // The placers: a type not built yet skips the tree.
    const int32 TrunkNode = NodeOf(TEXT("trunk_placer")), FoliageNode = NodeOf(TEXT("foliage_placer"));
    if (TrunkNode == INDEX_NONE || FoliageNode == INDEX_NONE)
        return TreeFail(OutError, TEXT("tree without its trunk or foliage placer"));
    const EVoxelTrunkPlacer TrunkType = VoxelTrunkPlacers::FindType(Graph.GetType(TrunkNode));
    const EVoxelFoliagePlacer FoliageType = VoxelFoliagePlacers::FindType(Graph.GetType(FoliageNode));
    if (TrunkType == EVoxelTrunkPlacer::Count || FoliageType == EVoxelFoliagePlacer::Count)
    {
        OutSkip =
            FString(TrunkType == EVoxelTrunkPlacer::Count ? Graph.GetType(TrunkNode) : Graph.GetType(FoliageNode));
        return true;
    }
    Out.Trunk.Type = uint8(TrunkType);
    Out.Foliage.Type = uint8(FoliageType);
    if (!CompilePlacerFields(Graph, TrunkNode, Tags, Predicates, Out.Trunk, OutError) ||
        !CompilePlacerFields(Graph, FoliageNode, Tags, Predicates, Out.Foliage, OutError))
        return false;

    // minimum_size (the wiki's defaults).
    const int32 SizeNode = NodeOf(TEXT("minimum_size"));
    Out.Size.bThreeLayers =
        SizeNode != INDEX_NONE && Graph.GetType(SizeNode) == TEXT("minecraft:three_layers_feature_size");
    Out.Size.Limit = TreeInt(Graph, SizeNode, TEXT("limit"), 1);
    Out.Size.UpperLimit = TreeInt(Graph, SizeNode, TEXT("upper_limit"), 1);
    Out.Size.LowerSize = TreeInt(Graph, SizeNode, TEXT("lower_size"), 0);
    Out.Size.MiddleSize = TreeInt(Graph, SizeNode, TEXT("middle_size"), 1);
    Out.Size.UpperSize = TreeInt(Graph, SizeNode, TEXT("upper_size"), 1);
    Out.Size.MinClippedHeight = TreeInt(Graph, SizeNode, TEXT("min_clipped_height"), -1);

    // The root placer (mangrove only in 26.3).
    if (const int32 RootNode = NodeOf(TEXT("root_placer")); RootNode != INDEX_NONE)
    {
        if (Graph.GetType(RootNode) != TEXT("minecraft:mangrove_root_placer") || !VoxelRootPlacers::IsMangroveBuilt())
        {
            OutSkip = FString(Graph.GetType(RootNode));
            return true;
        }
        FVoxelMangroveRoots& Roots = Out.Roots;
        Roots.bEnabled = true;
        const auto Sub = [&](int32 Parent, const TCHAR* Key) -> int32
        {
            const FVoxelFeatureValue* Value = Parent == INDEX_NONE ? nullptr : Graph.FindField(Parent, Key);
            return Value ? Graph.GetNode(*Value) : INDEX_NONE;
        };
        const auto RootProvider = [&](int32 Parent, const TCHAR* Key, int32& OutProvider) -> bool
        {
            const FVoxelFeatureValue* Value = Parent == INDEX_NONE ? nullptr : Graph.FindField(Parent, Key);
            if (!Value) return TreeFail(OutError, FString::Printf(TEXT("mangrove_root_placer without %s"), Key));
            return Providers.Compile(Graph, *Value, Tags, Predicates, OutProvider, OutSkip, OutError);
        };
        const int32 Above = Sub(RootNode, TEXT("above_root_placement")),
                    Placement = Sub(RootNode, TEXT("mangrove_root_placement"));
        if (Placement == INDEX_NONE) return TreeFail(OutError, TEXT("mangrove_root_placer without its placement"));
        FString Error;
        const FVoxelFeatureValue* Offset = Graph.FindField(RootNode, TEXT("trunk_offset_y"));
        if (!Offset || !VoxelNumberProviders::CompileInt(Graph, *Offset, Roots.TrunkOffsetY, &Error))
            return TreeFail(OutError, TEXT("mangrove_root_placer without trunk_offset_y"));
        if (!RootProvider(RootNode, TEXT("root_provider"), Roots.RootProvider) ||
            (OutSkip.IsEmpty() && !RootProvider(Placement, TEXT("muddy_roots_provider"), Roots.MuddyRootProvider)) ||
            (OutSkip.IsEmpty() && Above != INDEX_NONE &&
                !RootProvider(Above, TEXT("above_root_provider"), Roots.AboveRootProvider)))
            return false;
        if (!OutSkip.IsEmpty()) return true;
        Roots.AboveRootChance = Above == INDEX_NONE
            ? 0.f
            : float(Graph.FindField(Above, TEXT("above_root_placement_chance"))
                      ? Graph.FindField(Above, TEXT("above_root_placement_chance"))->Number
                      : 0.0);
        const FVoxelFeatureValue* Skew = Graph.FindField(Placement, TEXT("random_skew_chance"));
        Roots.RandomSkewChance = Skew ? float(Skew->Number) : 0.f;
        Roots.MaxRootWidth = TreeInt(Graph, Placement, TEXT("max_root_width"), 0);
        Roots.MaxRootLength = TreeInt(Graph, Placement, TEXT("max_root_length"), 0);
        FVoxelBlockSet Through, Muddy;
        if (!VoxelPredicateReading::ReadBlockSet(
                Graph, Graph.FindField(Placement, TEXT("can_grow_through")), Tags, Through, OutError) ||
            !VoxelPredicateReading::ReadBlockSet(
                Graph, Graph.FindField(Placement, TEXT("muddy_roots_in")), Tags, Muddy, OutError))
            return false;
        Roots.CanGrowThroughSet = Predicates.AddSet(MoveTemp(Through));
        Roots.MuddyRootsInSet = Predicates.AddSet(MoveTemp(Muddy));
    }

    // The states: the trunk's per axis and the leaves' per distance and waterlogging.
    if (!Provider(TEXT("trunk_provider"), Out.TrunkProvider, true) ||
        (OutSkip.IsEmpty() && !Provider(TEXT("foliage_provider"), Out.FoliageProvider, true)) ||
        (OutSkip.IsEmpty() && !Provider(TEXT("below_trunk_provider"), Out.BelowTrunkProvider, false)))
        return false;
    if (!OutSkip.IsEmpty()) return true;
    TArray<uint16> States;
    Providers.GetStates(Out.TrunkProvider, States);
    if (States.IsEmpty()) return TreeFail(OutError, TEXT("tree without a trunk state"));
    static const FName Axis(TEXT("axis")), Distance(TEXT("distance")), Waterlogged(TEXT("waterlogged"));
    static const FName AxisNames[3] = {TEXT("x"), TEXT("z"), TEXT("y")};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const int32 State = Table.WithValue(States[0], Axis, AxisNames[Index]);
        Out.LogStates[Index] = State == INDEX_NONE ? States[0] : uint16(State);
    }
    States.Reset();
    Providers.GetStates(Out.FoliageProvider, States);
    for (const uint16 Leaf : States)
    {
        TArray<uint16>& Variants = Out.LeafStates.Add(Leaf);
        for (int32 Steps = 1; Steps <= 6; ++Steps)
            for (const TCHAR* Wet : {TEXT("false"), TEXT("true")})
            {
                const int32 AtDistance = Table.WithValue(Leaf, Distance, FName(*FString::FromInt(Steps)));
                const int32 Variant =
                    AtDistance == INDEX_NONE ? INDEX_NONE : Table.WithValue(uint16(AtDistance), Waterlogged, Wet);
                Variants.Add(uint16(Variant != INDEX_NONE ? Variant : AtDistance != INDEX_NONE ? AtDistance : Leaf));
            }
    }

    // Logs and leaves go into air or replaceable_by_trees.
    const FVoxelBlockSet* Replaceable = Tags.Find(TEXT("minecraft:replaceable_by_trees"), OutError);
    if (!Replaceable) return false;
    FVoxelBlockSet Free = *Replaceable;
    Free.Add(0);
    Out.FreeSet = Predicates.AddSet(MoveTemp(Free));
    Out.bIgnoreVines = TreeInt(Graph, Node, TEXT("ignore_vines"), 0) != 0;

    // The decorators: a type not built yet (or one whose provider is not) is left out and counted.
    if (const FVoxelFeatureValue* Decorators = Graph.FindField(Node, TEXT("decorators")))
        for (int32 Item = 0; Decorators->Type == EVoxelFeatureValueType::List && Item < Decorators->Count; ++Item)
        {
            const int32 DecoratorNode = Graph.GetNode(Graph.Items[Decorators->Index + Item]);
            const FStringView DecoratorType = Graph.GetType(DecoratorNode);
            const EVoxelTreeDecorator Type = VoxelTreeDecorators::FindType(DecoratorType);
            FVoxelTreeDecoratorConfig Decorator;
            FString DecoratorSkip;
            if (Type != EVoxelTreeDecorator::Count &&
                !VoxelTreeDecorators::Compile(
                    Graph, DecoratorNode, Type, Tags, Table, Predicates, Providers, Decorator, DecoratorSkip, OutError))
                return false;
            if (Type == EVoxelTreeDecorator::Count || !DecoratorSkip.IsEmpty())
                Out.SkippedDecorators.Add(FString(DecoratorType));
            else
                Out.Decorators.Add(MoveTemp(Decorator));
        }

    const int32 TreeReach =
        FMath::Max(VoxelTrunkPlacers::GetReach(Out.Trunk) + VoxelFoliagePlacers::GetReach(Out.Foliage),
            Out.Roots.bEnabled ? VoxelRootPlacers::GetReach(Out.Roots) : 0);
    Out.Reach = TreeReach;
    for (const FVoxelTreeDecoratorConfig& Decorator : Out.Decorators)
        Out.Reach = FMath::Max(Out.Reach, VoxelTreeDecorators::GetReach(Decorator, Out, TreeReach));
    ++Out.Reach;
    if (Out.Reach > VoxelConstants::ColumnSize)
        return TreeFail(OutError,
            FString::Printf(TEXT("a tree reaching %d blocks, past the 3 x 3's margin of %d"), Out.Reach,
                VoxelConstants::ColumnSize));
    return true;
}

int32 VoxelTreeFeature::GetFreeHeight(const FVoxelTreeConfig& Config, const FVoxelTreeBuilder& Builder, int32 Height)
{
    const FVoxelFeatureSize& Size = Config.Size;
    const auto GetWidth = [&Size, Height](int32 Layer)
    {
        if (!Size.bThreeLayers) return Layer < Size.Limit ? Size.LowerSize : Size.UpperSize;
        if (Layer < Size.Limit) return Size.LowerSize;
        return Layer >= Height - Size.UpperLimit ? Size.UpperSize : Size.MiddleSize;
    };
    const TConstArrayView<FIntPoint> Footprint = VoxelTrunkPlacers::GetFootprint(EVoxelTrunkPlacer(Config.Trunk.Type));
    const auto IsLayerFree = [&](int32 Layer)
    {
        const int32 Width = GetWidth(Layer);
        for (const FIntPoint& Column : Footprint)
            for (int32 DY = -Width; DY <= Width; ++DY)
                for (int32 DX = -Width; DX <= Width; ++DX)
                    if (!Builder.IsFree(Builder.Origin + FIntVector(Column.X + DX, Column.Y + DY, Layer))) return false;
        return true;
    };
    int32 Free = 0;
    while (Free <= Height + 1 && IsLayerFree(Free))
        ++Free;
    if (Free >= Height + 2) return Height;
    if (Size.MinClippedHeight >= 0 && Free - 2 >= Size.MinClippedHeight) return Free - 2;
    return INDEX_NONE;
}

void VoxelTreeFeature::GetLeafDistances(const FVoxelTreeBuilder& Builder, TArray<uint8>& Out)
{
    // A BFS from the tree's own logs through its own face-adjacent leaves, up to 6 steps; 0 marks a leaf to drop.
    using ECell = FVoxelTreeBuilder::ECell;
    const TArray<FVoxelTreeBuilder::FCell>& Cells = Builder.Cells;
    TMap<FIntVector, int32> Leaves;
    Leaves.Reserve(Cells.Num());
    for (int32 Index = 0; Index < Cells.Num(); ++Index)
        if (Cells[Index].Kind == ECell::Leaf) Leaves.Add(Cells[Index].Position, Index);
    Out.Init(0, Cells.Num());
    TArray<int32> Queue;
    Queue.Reserve(Leaves.Num());
    for (const FVoxelTreeBuilder::FCell& Cell : Cells)
        if (Cell.Kind == ECell::Log)
            for (const auto& Step : TreeSteps)
                if (const int32* Leaf = Leaves.Find(Cell.Position + FIntVector(Step[0], Step[1], Step[2])))
                    if (!Out[*Leaf])
                    {
                        Out[*Leaf] = 1;
                        Queue.Add(*Leaf);
                    }
    for (int32 Head = 0; Head < Queue.Num(); ++Head)
    {
        const int32 Index = Queue[Head];
        if (Out[Index] >= 6) continue;
        for (const auto& Step : TreeSteps)
            if (const int32* Leaf = Leaves.Find(Cells[Index].Position + FIntVector(Step[0], Step[1], Step[2])))
                if (!Out[*Leaf])
                {
                    Out[*Leaf] = uint8(Out[Index] + 1);
                    Queue.Add(*Leaf);
                }
    }
}

bool VoxelTreeFeature::Place(
    const FVoxelTreeConfig& Config, FIntVector Origin, FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env)
{
    static const FName BaseHeight(TEXT("base_height")), RandA(TEXT("height_rand_a")), RandB(TEXT("height_rand_b"));
    FVoxelTreeBuilder Builder(Config, Env, Random);
    Builder.Origin = Origin;
    int32 Height = Config.Trunk.SampleInt(BaseHeight, Random);
    Height += Random.NextInt(Config.Trunk.SampleInt(RandA, Random) + 1);
    Height += Random.NextInt(Config.Trunk.SampleInt(RandB, Random) + 1);
    if (Config.Roots.bEnabled && !VoxelRootPlacers::PlaceMangrove(Config.Roots, Builder)) return false;
    Height = GetFreeHeight(Config, Builder, Height);
    if (Height == INDEX_NONE || Builder.Origin.Z + Height + 1 > VoxelConstants::MaxZ) return false;
    Builder.Height = Height;
    TArray<FVoxelFoliageAttachment> Attachments;
    VoxelTrunkPlacers::Place(Config.Trunk, Builder, Attachments);
    VoxelFoliagePlacers::Place(Config.Foliage, Builder, Attachments);

    TArray<uint8> Distances;
    GetLeafDistances(Builder, Distances);
    using ECell = FVoxelTreeBuilder::ECell;
    if (!Config.Decorators.IsEmpty())
    {
        // The leaves too far from the logs go first, so the decorators see the tree as it is emitted.
        for (int32 Index = 0; Index < Distances.Num(); ++Index)
            if (Builder.Cells[Index].Kind == ECell::Leaf && !Distances[Index]) Builder.Drop(Index);
        VoxelTreeDecorators::Place(Config, Builder);
    }
    const FVoxelDecorationContext& Context = Env.Context;
    for (int32 Index = 0; Index < Builder.Cells.Num(); ++Index)
    {
        const FVoxelTreeBuilder::FCell& Cell = Builder.Cells[Index];
        const FIntVector At = Cell.Position;
        const uint16 Was = Context.GetState(At.X, At.Y, At.Z);
        switch (Cell.Kind)
        {
        case ECell::Log:
            Env.Emit({At, Cell.State, EVoxelWriteMode::IfReplaceable, uint16(Config.FreeSet)});
            break;
        case ECell::Leaf:
        {
            if (!Distances[Index]) break;
            const TArray<uint16>* Variants = Config.LeafStates.Find(Cell.State);
            // Waterlogged where the cell held water: a source, or a block holding it (seagrass, a waterlogged state).
            const int32 Wet = Env.Survival.IsWaterSource(Was) ? 1 : 0;
            const uint16 State = Variants ? (*Variants)[(Distances[Index] - 1) * 2 + Wet] : Cell.State;
            Env.Emit({At, State, EVoxelWriteMode::IfReplaceable, uint16(Config.FreeSet)});
            break;
        }
        case ECell::Root:
        case ECell::Ground:
        case ECell::Decoration:
            // The ground, the roots and the decorations land only where the cell still holds what this Decorate saw.
            Env.Emit({At, Cell.State, EVoxelWriteMode::IfUnchanged, Was});
            break;
        case ECell::Dropped:
            break;
        }
    }
    return true;
}
