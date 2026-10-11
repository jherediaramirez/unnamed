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

void FVoxelTreeCellIndex::Add(FIntVector Cell, int32 Index)
{
    // At most half full.
    if ((Used + 1) * 2 > Keys.Num()) Grow();
    const uint64 Key = Pack(Cell);
    uint64* Data = Keys.GetData();
    const int32 Mask = Keys.Num() - 1;
    int32 Slot = int32((Key * 0x9E3779B97F4A7C15ull) >> Shift);
    while (Data[Slot] != Key && Data[Slot] != Empty)
        Slot = (Slot + 1) & Mask;
    if (Data[Slot] == Empty)
    {
        Data[Slot] = Key;
        ++Used;
    }
    Values.GetData()[Slot] = Index;
}

void FVoxelTreeCellIndex::Grow()
{
    TArray<uint64> OldKeys = MoveTemp(Keys);
    TArray<int32> OldValues = MoveTemp(Values);
    const int32 Size = FMath::Max(256, OldKeys.Num() * 2);
    Keys.Init(Empty, Size);
    Values.SetNumUninitialized(Size);
    Shift = 64 - FMath::FloorLog2(uint32(Size));
    Used = 0;
    const int32 Mask = Size - 1;
    // Removed cells are not carried over.
    for (int32 Old = 0; Old < OldKeys.Num(); ++Old)
    {
        if (OldKeys[Old] == Empty || OldValues[Old] == INDEX_NONE) continue;
        int32 Slot = int32((OldKeys[Old] * 0x9E3779B97F4A7C15ull) >> Shift);
        while (Keys[Slot] != Empty)
            Slot = (Slot + 1) & Mask;
        Keys[Slot] = OldKeys[Old];
        Values[Slot] = OldValues[Old];
        ++Used;
    }
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
    return IsFreeBelow(Cell);
}

bool FVoxelTreeBuilder::IsFreeBelow(FIntVector Cell) const
{
    return Cell.Z >= VoxelConstants::MinZ && Cell.Z <= VoxelConstants::MaxZ &&
        Env.Predicates.GetSet(Config.FreeSet)
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
    Append(Cell, State, Kind);
}

// SetLog, SetLeaf and SetDecoration look the cell up once (S9d); each keeps the checks it had.
bool FVoxelTreeBuilder::SetLog(FIntVector Cell, int32 Axis)
{
    const uint16 State = Config.LogStates[FMath::Clamp(Axis, 0, 2)];
    if (int32* Index = FindCell(Cell))
    {
        // IsLog, or IsFree on the tree's own leaf.
        FCell& Own = Cells[*Index];
        const bool bInside = Cell.Z >= VoxelConstants::MinZ && Cell.Z <= VoxelConstants::MaxZ;
        if (Own.Kind != ECell::Log && !(bInside && Own.Kind == ECell::Leaf)) return false;
        Own.State = State;
        Own.Kind = ECell::Log;
        return true;
    }
    if (!IsFreeBelow(Cell)) return false;
    Append(Cell, State, ECell::Log);
    return true;
}

bool FVoxelTreeBuilder::SetLeaf(FIntVector Cell)
{
    if (const int32* Index = FindCell(Cell)) return Cells[*Index].Kind == ECell::Leaf;
    if (!IsFreeBelow(Cell)) return false;
    // The state's distance is set when the tree is emitted.
    Append(Cell, Env.Providers.Sample(Config.FoliageProvider, Random, Cell, Env.Context), ECell::Leaf);
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
    Append(Cell, State, ECell::Decoration);
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
    // A counting sort by Z: stable, so cells keep their order within a layer, as a stable sort by Z does.
    Out.Reset();
    const TConstArrayView<FCell> Read = TConstArrayView<FCell>(Cells).Slice(
        FMath::Min(DecoratedBegin, Cells.Num()), FMath::Max(0, FMath::Min(DecoratedEnd, Cells.Num()) - DecoratedBegin));
    int32 MinZ = MAX_int32, MaxZ = MIN_int32, Count = 0;
    for (const FCell& Cell : Read)
        if (Cell.Kind == Kind)
        {
            MinZ = FMath::Min(MinZ, Cell.Position.Z);
            MaxZ = FMath::Max(MaxZ, Cell.Position.Z);
            ++Count;
        }
    if (!Count) return;
    TArray<int32, TInlineAllocator<128>> Next;
    Next.Init(0, MaxZ - MinZ + 2);
    for (const FCell& Cell : Read)
        if (Cell.Kind == Kind) ++Next[Cell.Position.Z - MinZ + 1];
    for (int32 Layer = 1; Layer < Next.Num(); ++Layer)
        Next[Layer] += Next[Layer - 1];
    Out.SetNumUninitialized(Count);
    for (const FCell& Cell : Read)
        if (Cell.Kind == Kind) Out[Next[Cell.Position.Z - MinZ]++] = Cell.Position;
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
    if (!SetLogStates(Table, Providers, Out, OutError)) return false;
    static const FName Distance(TEXT("distance")), Waterlogged(TEXT("waterlogged"));
    TArray<uint16> States;
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

    if (!SetFreeSet(Tags, Predicates, Out, OutError)) return false;
    Out.bIgnoreVines = TreeInt(Graph, Node, TEXT("ignore_vines"), 0) != 0;
    if (!CompileDecorators(Graph, Graph.FindField(Node, TEXT("decorators")), Tags, Table, Predicates, Providers,
            Out.Decorators, Out.SkippedDecorators, OutError))
        return false;

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

bool VoxelTreeFeature::SetLogStates(
    const FVoxelBlockTable& Table, const FVoxelStateProviders& Providers, FVoxelTreeConfig& Out, FString* OutError)
{
    TArray<uint16> States;
    Providers.GetStates(Out.TrunkProvider, States);
    if (States.IsEmpty()) return TreeFail(OutError, TEXT("tree without a trunk state"));
    static const FName Axis(TEXT("axis"));
    static const FName AxisNames[3] = {TEXT("x"), TEXT("z"), TEXT("y")};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const int32 State = Table.WithValue(States[0], Axis, AxisNames[Index]);
        Out.LogStates[Index] = State == INDEX_NONE ? States[0] : uint16(State);
    }
    return true;
}

bool VoxelTreeFeature::SetFreeSet(
    FVoxelBlockTags& Tags, FVoxelBlockPredicates& Predicates, FVoxelTreeConfig& Out, FString* OutError)
{
    // Logs and leaves go into air or replaceable_by_trees.
    const FVoxelBlockSet* Replaceable = Tags.Find(TEXT("minecraft:replaceable_by_trees"), OutError);
    if (!Replaceable) return false;
    FVoxelBlockSet Free = *Replaceable;
    Free.Add(0);
    Out.FreeSet = Predicates.AddSet(MoveTemp(Free));
    return true;
}

bool VoxelTreeFeature::CompileDecorators(const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue* List,
    FVoxelBlockTags& Tags, const FVoxelBlockTable& Table, FVoxelBlockPredicates& Predicates,
    FVoxelStateProviders& Providers, TArray<FVoxelTreeDecoratorConfig>& Out, TArray<FString>& OutSkipped,
    FString* OutError)
{
    // A type not built yet (or one whose provider is not) is left out and counted.
    for (int32 Item = 0; List && List->Type == EVoxelFeatureValueType::List && Item < List->Count; ++Item)
    {
        const int32 DecoratorNode = Graph.GetNode(Graph.Items[List->Index + Item]);
        const FStringView DecoratorType = Graph.GetType(DecoratorNode);
        const EVoxelTreeDecorator Type = VoxelTreeDecorators::FindType(DecoratorType);
        FVoxelTreeDecoratorConfig Decorator;
        FString DecoratorSkip;
        if (Type != EVoxelTreeDecorator::Count &&
            !VoxelTreeDecorators::Compile(
                Graph, DecoratorNode, Type, Tags, Table, Predicates, Providers, Decorator, DecoratorSkip, OutError))
            return false;
        if (Type == EVoxelTreeDecorator::Count || !DecoratorSkip.IsEmpty())
            OutSkipped.Add(FString(DecoratorType));
        else
            Out.Add(MoveTemp(Decorator));
    }
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
    // A footprint that fills its box (one column, or 2 x 2): the squares around its columns are together that box
    // widened, so each cell is checked once (S9d: a 2 x 2 trunk checked most cells four times).
    FIntPoint Low(MAX_int32, MAX_int32), High(MIN_int32, MIN_int32);
    for (const FIntPoint& Column : Footprint)
    {
        Low = FIntPoint(FMath::Min(Low.X, Column.X), FMath::Min(Low.Y, Column.Y));
        High = FIntPoint(FMath::Max(High.X, Column.X), FMath::Max(High.Y, Column.Y));
    }
    const bool bBox = !Footprint.IsEmpty() && (High.X - Low.X + 1) * (High.Y - Low.Y + 1) == Footprint.Num();
    const auto IsLayerFree = [&](int32 Layer)
    {
        const int32 Width = GetWidth(Layer);
        if (bBox)
        {
            for (int32 Y = Low.Y - Width; Y <= High.Y + Width; ++Y)
                for (int32 X = Low.X - Width; X <= High.X + Width; ++X)
                    if (!Builder.IsFree(Builder.Origin + FIntVector(X, Y, Layer))) return false;
            return true;
        }
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
    // The tree's leaf at a position, or INDEX_NONE: a dense grid of leaf indices over the box of its logs and leaves
    // (S9d: one lookup per step, cheaper than the builder's index at a tree's thousands of steps).
    FIntVector Low(MAX_int32), High(MIN_int32);
    for (const FVoxelTreeBuilder::FCell& Cell : Cells)
        if (Cell.Kind == ECell::Log || Cell.Kind == ECell::Leaf)
        {
            Low = FIntVector(FMath::Min(Low.X, Cell.Position.X), FMath::Min(Low.Y, Cell.Position.Y),
                FMath::Min(Low.Z, Cell.Position.Z));
            High = FIntVector(FMath::Max(High.X, Cell.Position.X), FMath::Max(High.Y, Cell.Position.Y),
                FMath::Max(High.Z, Cell.Position.Z));
        }
    Out.Init(0, Cells.Num());
    if (Low.X > High.X) return;
    const FIntVector Extent = High - Low + FIntVector(1);
    TArray<int32> Grid;
    Grid.Init(INDEX_NONE, Extent.X * Extent.Y * Extent.Z);
    for (int32 Index = 0; Index < Cells.Num(); ++Index)
        if (Cells[Index].Kind == ECell::Leaf)
        {
            const FIntVector At = Cells[Index].Position - Low;
            Grid[(At.Z * Extent.Y + At.Y) * Extent.X + At.X] = Index;
        }
    const auto FindLeaf = [&Grid, Low, Extent](FIntVector Position) -> int32
    {
        const FIntVector At = Position - Low;
        if (uint32(At.X) >= uint32(Extent.X) || uint32(At.Y) >= uint32(Extent.Y) || uint32(At.Z) >= uint32(Extent.Z))
            return INDEX_NONE;
        return Grid.GetData()[(At.Z * Extent.Y + At.Y) * Extent.X + At.X];
    };
    TArray<int32> Queue;
    Queue.Reserve(Cells.Num());
    for (const FVoxelTreeBuilder::FCell& Cell : Cells)
        if (Cell.Kind == ECell::Log)
            for (const auto& Step : TreeSteps)
            {
                const int32 Leaf = FindLeaf(Cell.Position + FIntVector(Step[0], Step[1], Step[2]));
                if (Leaf != INDEX_NONE && !Out[Leaf])
                {
                    Out[Leaf] = 1;
                    Queue.Add(Leaf);
                }
            }
    for (int32 Head = 0; Head < Queue.Num(); ++Head)
    {
        const int32 Index = Queue[Head];
        if (Out[Index] >= 6) continue;
        for (const auto& Step : TreeSteps)
        {
            const int32 Leaf = FindLeaf(Cells[Index].Position + FIntVector(Step[0], Step[1], Step[2]));
            if (Leaf != INDEX_NONE && !Out[Leaf])
            {
                Out[Leaf] = uint8(Out[Index] + 1);
                Queue.Add(Leaf);
            }
        }
    }
}

bool VoxelTreeFeature::Place(const FVoxelTreeConfig& Config, FIntVector Origin, FVoxelXoroshiro& Random,
    const FVoxelPlantEnvironment& Env, FVoxelTreeTimes* Times)
{
    static const FName BaseHeight(TEXT("base_height")), RandA(TEXT("height_rand_a")), RandB(TEXT("height_rand_b"));
    // Adds the time since the last lap to Stage (only when timed).
    uint64 Last = Times ? FPlatformTime::Cycles64() : 0;
    const auto Lap = [Times, &Last](int32 Stage)
    {
        if (!Times) return;
        const uint64 Now = FPlatformTime::Cycles64();
        Times->Cycles[Stage] += Now - Last;
        ++Times->Calls[Stage];
        Last = Now;
    };
    FVoxelTreeBuilder Builder(Config, Env, Random);
    Builder.Origin = Origin;
    int32 Height = Config.Trunk.SampleInt(BaseHeight, Random);
    Height += Random.NextInt(Config.Trunk.SampleInt(RandA, Random) + 1);
    Height += Random.NextInt(Config.Trunk.SampleInt(RandB, Random) + 1);
    if (Config.Roots.bEnabled && !VoxelRootPlacers::PlaceMangrove(Config.Roots, Builder))
    {
        Lap(FVoxelTreeTimes::Size);
        return false;
    }
    Height = GetFreeHeight(Config, Builder, Height);
    Lap(FVoxelTreeTimes::Size);
    if (Height == INDEX_NONE || Builder.Origin.Z + Height + 1 > VoxelConstants::MaxZ) return false;
    Builder.Height = Height;
    TArray<FVoxelFoliageAttachment> Attachments;
    VoxelTrunkPlacers::Place(Config.Trunk, Builder, Attachments);
    Lap(FVoxelTreeTimes::Trunk);
    VoxelFoliagePlacers::Place(Config.Foliage, Builder, Attachments);
    Lap(FVoxelTreeTimes::Foliage);

    TArray<uint8> Distances;
    GetLeafDistances(Builder, Distances);
    using ECell = FVoxelTreeBuilder::ECell;
    if (!Config.Decorators.IsEmpty())
    {
        // The leaves too far from the logs go first, so the decorators see the tree as it is emitted.
        for (int32 Index = 0; Index < Distances.Num(); ++Index)
            if (Builder.Cells[Index].Kind == ECell::Leaf && !Distances[Index]) Builder.Drop(Index);
        Lap(FVoxelTreeTimes::Distances);
        VoxelTreeDecorators::Place(Config, Builder, Times);
        if (Times) Last = FPlatformTime::Cycles64();
    }
    else
        Lap(FVoxelTreeTimes::Distances);
    const FVoxelDecorationContext& Context = Env.Context;
    // The last foliage state's variants: a tree's leaves mostly share one.
    uint16 VariantsOf = 0;
    const TArray<uint16>* Variants = nullptr;
    bool bVariants = false;
    for (int32 Index = 0; Index < Builder.Cells.Num(); ++Index)
    {
        const FVoxelTreeBuilder::FCell& Cell = Builder.Cells[Index];
        const FIntVector At = Cell.Position;
        // The cell's state before this tree, read only where it is used (S9d).
        const auto GetWas = [&Context, At] { return Context.GetState(At.X, At.Y, At.Z); };
        switch (Cell.Kind)
        {
        case ECell::Log:
            Env.Emit({At, Cell.State, EVoxelWriteMode::IfReplaceable, uint16(Config.FreeSet)});
            break;
        case ECell::Leaf:
        {
            if (!Distances[Index]) break;
            if (!bVariants || Cell.State != VariantsOf)
            {
                Variants = Config.LeafStates.Find(Cell.State);
                VariantsOf = Cell.State;
                bVariants = true;
            }
            // Waterlogged where the cell held water: a source, or a block holding it (seagrass, a waterlogged state).
            const int32 Wet = Env.Survival.IsWaterSource(GetWas()) ? 1 : 0;
            const uint16 State = Variants ? (*Variants)[(Distances[Index] - 1) * 2 + Wet] : Cell.State;
            Env.Emit({At, State, EVoxelWriteMode::IfReplaceable, uint16(Config.FreeSet)});
            break;
        }
        case ECell::Root:
        case ECell::Ground:
        case ECell::Decoration:
            // The ground, the roots and the decorations land only where the cell still holds what this Decorate saw.
            Env.Emit({At, Cell.State, EVoxelWriteMode::IfUnchanged, GetWas()});
            break;
        case ECell::Dropped:
            break;
        }
    }
    Lap(FVoxelTreeTimes::Emit);
    return true;
}
