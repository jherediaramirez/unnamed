#include "Decoration/VoxelBlockPredicates.h"
#include "Blocks/VoxelBlockTable.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelFeatureGraph.h"

namespace
{
    bool PredicateFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    // The strings a field holds: one string, or a list of them.
    TArray<FStringView> ReadStrings(const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue* Value)
    {
        TArray<FStringView> Out;
        if (!Value) return Out;
        if (Value->Type == EVoxelFeatureValueType::String)
            Out.Add(Graph.Strings[Value->Index]);
        else if (Value->Type == EVoxelFeatureValueType::List)
            for (int32 Item = 0; Item < Value->Count; ++Item)
            {
                const FVoxelFeatureValue& Entry = Graph.Items[Value->Index + Item];
                if (Entry.Type == EVoxelFeatureValueType::String) Out.Add(Graph.Strings[Entry.Index]);
            }
        return Out;
    }
}

int32 VoxelPredicateReading::ReadState(
    const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value, const FVoxelBlockTable& Table)
{
    if (Value.Type == EVoxelFeatureValueType::String) return Table.MakeState(Graph.Strings[Value.Index], {});
    const int32 Node = Graph.GetNode(Value);
    const FVoxelFeatureValue* Id = Node == INDEX_NONE ? nullptr : Graph.FindField(Node, TEXT("id"));
    if (!Id || Id->Type != EVoxelFeatureValueType::String) return INDEX_NONE;
    TArray<TPair<FName, FName>> Properties;
    if (const FVoxelFeatureValue* PropertiesValue = Graph.FindField(Node, TEXT("properties")))
    {
        const int32 PropertiesNode = Graph.GetNode(*PropertiesValue);
        if (PropertiesNode == INDEX_NONE) return INDEX_NONE;
        const FVoxelFeatureNode& Holder = Graph.Nodes[PropertiesNode];
        for (int32 Field = 0; Field < Holder.FieldCount; ++Field)
        {
            const FVoxelFeatureField& Pair = Graph.Fields[Holder.FirstField + Field];
            if (Pair.Value.Type != EVoxelFeatureValueType::String) return INDEX_NONE;
            Properties.Add({FName(Graph.Strings[Pair.Key]), FName(Graph.Strings[Pair.Value.Index])});
        }
    }
    return Table.MakeState(Graph.Strings[Id->Index], Properties);
}

FIntVector VoxelPredicateReading::ReadOffset(const FVoxelFeatureGraph& Graph, int32 Node, const TCHAR* Key)
{
    const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
    if (!Value || Value->Type != EVoxelFeatureValueType::List || Value->Count != 3) return FIntVector::ZeroValue;
    const auto At = [&](int32 Index) { return int32(Graph.Items[Value->Index + Index].Number); };
    // The reference's y is up; our Y is its z.
    return FIntVector(At(0), At(2), At(1));
}

bool VoxelPredicateReading::ReadBlockSet(const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue* Value,
    FVoxelBlockTags& Tags, FVoxelBlockSet& Out, FString* OutError)
{
    Out.Init(Tags.GetBlockLimit(), false);
    for (const FStringView Id : ReadStrings(Graph, Value))
        if (Id.StartsWith(TEXT("#")))
        {
            const FVoxelBlockSet* Tag = Tags.Find(Id.RightChop(1), OutError);
            if (!Tag) return false;
            for (int32 Word = 0; Word < Out.Words.Num() && Word < Tag->Words.Num(); ++Word)
                Out.Words[Word] |= Tag->Words[Word];
        }
        else if (const int32 Block = Tags.FindBlock(Id); Block != INDEX_NONE)
            Out.Add(Block);
    return true;
}

int32 VoxelPredicateReading::ReadFace(FStringView Name)
{
    static const TCHAR* const Names[] = {
        TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south"), TEXT("down"), TEXT("up")};
    for (int32 Face = 0; Face < UE_ARRAY_COUNT(Names); ++Face)
        if (Name == Names[Face]) return Face;
    return INDEX_NONE;
}

void FVoxelBlockPredicates::Initialize(const FVoxelBlockTable& InTable, const FVoxelPlantSurvival& InSurvival)
{
    *this = FVoxelBlockPredicates();
    Table = &InTable;
    Survival = &InSurvival;
    WaterBlock = InTable.FindBlock(TEXT("water"));
    LavaBlock = InTable.FindBlock(TEXT("lava"));
}

uint8 FVoxelBlockPredicates::GetFluidBits(uint16 State) const
{
    const FVoxelStateInfo& Info = Table->Get(State);
    if (Info.HeldFluid) return WaterSource;
    if (!Info.IsFluid()) return 0;
    const bool bSource = Info.FluidAmount == 8;
    if (Info.Block == WaterBlock) return bSource ? WaterSource : WaterFlowing;
    if (Info.Block == LavaBlock) return bSource ? LavaSource : LavaFlowing;
    return 0;
}

bool FVoxelBlockPredicates::Compile(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags, int32& OutRoot,
    FString& OutSkip, FString* OutError)
{
    using namespace VoxelPredicateReading;
    if (Node == INDEX_NONE || Graph.Nodes[Node].Kind != EVoxelFeatureKind::BlockPredicate)
        return PredicateFail(OutError, TEXT("not a block predicate"));
    const FStringView Type = Graph.GetType(Node);
    FNode Out;
    Out.Offset = ReadOffset(Graph, Node, TEXT("offset"));
    if (Type == TEXT("minecraft:true"))
        Out.Op = EOp::True;
    else if (Type == TEXT("minecraft:matching_blocks"))
    {
        FVoxelBlockSet Set;
        Set.Init(Tags.GetBlockLimit(), false);
        for (const FStringView Id : ReadStrings(Graph, Graph.FindField(Node, TEXT("blocks"))))
            if (const int32 Block = Tags.FindBlock(Id); Block != INDEX_NONE) Set.Add(Block);
        Out.Op = EOp::InSet;
        Out.Argument = Sets.Add(MoveTemp(Set));
    }
    else if (Type == TEXT("minecraft:matching_block_tag") || Type == TEXT("minecraft:replaceable"))
    {
        const FVoxelFeatureValue* Tag = Graph.FindField(Node, TEXT("tag"));
        const FStringView Name = Type == TEXT("minecraft:replaceable") ? FStringView(TEXT("minecraft:replaceable"))
            : Tag && Tag->Type == EVoxelFeatureValueType::String       ? FStringView(Graph.Strings[Tag->Index])
                                                                       : FStringView();
        const FVoxelBlockSet* Set = Tags.Find(Name, OutError);
        if (!Set) return PredicateFail(OutError, FString::Printf(TEXT("no tag %s"), *FString(Name)));
        Out.Op = EOp::InSet;
        // Each tag's set once: replaceable is read by many plants.
        if (Name == TEXT("minecraft:replaceable") && ReplaceableSet != INDEX_NONE)
            Out.Argument = ReplaceableSet;
        else
            Out.Argument = Sets.Add(*Set);
        if (Name == TEXT("minecraft:replaceable")) ReplaceableSet = Out.Argument;
    }
    else if (Type == TEXT("minecraft:matching_fluids"))
    {
        Out.Op = EOp::Fluids;
        for (const FStringView Id : ReadStrings(Graph, Graph.FindField(Node, TEXT("fluids"))))
            Out.Argument |= Id == TEXT("minecraft:water") ? WaterSource
                : Id == TEXT("minecraft:flowing_water")   ? WaterFlowing
                : Id == TEXT("minecraft:lava")            ? LavaSource
                : Id == TEXT("minecraft:flowing_lava")    ? LavaFlowing
                                                          : 0;
    }
    else if (Type == TEXT("minecraft:solid"))
        Out.Op = EOp::Solid;
    else if (Type == TEXT("minecraft:has_sturdy_face"))
    {
        const FVoxelFeatureValue* Direction = Graph.FindField(Node, TEXT("direction"));
        Out.Op = EOp::Sturdy;
        Out.Argument = Direction && Direction->Type == EVoxelFeatureValueType::String
            ? ReadFace(Graph.Strings[Direction->Index])
            : INDEX_NONE;
        if (Out.Argument == INDEX_NONE) return PredicateFail(OutError, TEXT("has_sturdy_face without a direction"));
    }
    else if (Type == TEXT("minecraft:would_survive"))
    {
        const FVoxelFeatureValue* State = Graph.FindField(Node, TEXT("state"));
        Out.Op = EOp::WouldSurvive;
        Out.Argument = State ? ReadState(Graph, *State, *Table) : INDEX_NONE;
        // Saplings are not in the block table (the block reference has none), yet every tree's placement asks whether
        // one would survive. A sapling survives where vegetation does (minecraft.wiki, "Sapling": dirt-like blocks and
        // farmland), so its would_survive tests the block below against supports_vegetation, as the plants' survival
        // table does (S8).
        const int32 StateNode = State ? Graph.GetNode(*State) : INDEX_NONE;
        const FVoxelFeatureValue* StateId = State && State->Type == EVoxelFeatureValueType::String ? State
            : StateNode != INDEX_NONE ? Graph.FindField(StateNode, TEXT("id"))
                                      : nullptr;
        if (Out.Argument == INDEX_NONE && StateId && StateId->Type == EVoxelFeatureValueType::String &&
            Graph.Strings[StateId->Index].EndsWith(TEXT("_sapling")))
        {
            const FVoxelBlockSet* Vegetation = Tags.Find(TEXT("minecraft:supports_vegetation"), OutError);
            if (!Vegetation) return false;
            Out.Op = EOp::InSet;
            Out.Offset.Z -= 1;
            Out.Argument = AddSet(*Vegetation);
        }
        if (Out.Argument == INDEX_NONE)
        {
            OutSkip = TEXT("would_survive of a state the table lacks");
            return true;
        }
    }
    else if (Type == TEXT("minecraft:inside_world_bounds"))
        Out.Op = EOp::InsideWorld;
    else if (Type == TEXT("minecraft:all_of") || Type == TEXT("minecraft:any_of") || Type == TEXT("minecraft:not"))
    {
        Out.Op = Type == TEXT("minecraft:all_of") ? EOp::AllOf
            : Type == TEXT("minecraft:any_of")    ? EOp::AnyOf
                                                  : EOp::Not;
        TArray<int32> Kids;
        const auto AddChild = [&](const FVoxelFeatureValue& Value) -> bool
        {
            int32 Child = INDEX_NONE;
            if (!Compile(Graph, Graph.GetNode(Value), Tags, Child, OutSkip, OutError)) return false;
            Kids.Add(Child);
            return true;
        };
        if (Out.Op == EOp::Not)
        {
            const FVoxelFeatureValue* Inner = Graph.FindField(Node, TEXT("predicate"));
            if (!Inner || !AddChild(*Inner))
                return Inner ? false : PredicateFail(OutError, TEXT("not without a predicate"));
        }
        else if (const FVoxelFeatureValue* List = Graph.FindField(Node, TEXT("predicates"));
            List && List->Type == EVoxelFeatureValueType::List)
        {
            for (int32 Item = 0; Item < List->Count; ++Item)
                if (!AddChild(Graph.Items[List->Index + Item])) return false;
        }
        if (!OutSkip.IsEmpty()) return true;
        Out.First = Children.Num();
        Out.Count = Kids.Num();
        Children.Append(Kids);
    }
    else
    {
        OutSkip = FString::Printf(TEXT("predicate %s"), *FString(Type));
        return true;
    }
    OutRoot = Nodes.Add(Out);
    return true;
}

bool FVoxelBlockPredicates::Test(int32 Root, const FVoxelDecorationContext& Context, FIntVector Position) const
{
    const FNode& Node = Nodes.GetData()[Root];
    const FIntVector Cell = Position + Node.Offset;
    switch (Node.Op)
    {
    case EOp::True:
        return true;
    case EOp::InSet:
        return Sets.GetData()[Node.Argument].Contains(Table->Get(Context.GetState(Cell.X, Cell.Y, Cell.Z)).Block);
    case EOp::Fluids:
        return (GetFluidBits(Context.GetState(Cell.X, Cell.Y, Cell.Z)) & Node.Argument) != 0;
    case EOp::Solid:
        return Table->Get(Context.GetState(Cell.X, Cell.Y, Cell.Z)).bSolid;
    case EOp::Sturdy:
        return (Survival->GetSturdyFaces(Context.GetState(Cell.X, Cell.Y, Cell.Z)) >> Node.Argument & 1) != 0;
    case EOp::WouldSurvive:
        return Survival->Survives(uint16(Node.Argument), Cell.X, Cell.Y, Cell.Z,
            [&Context](int32 X, int32 Y, int32 Z) { return Context.GetState(X, Y, Z); });
    case EOp::InsideWorld:
        return Cell.Z >= VoxelConstants::MinZ && Cell.Z <= VoxelConstants::MaxZ;
    case EOp::AllOf:
        for (int32 Child = 0; Child < Node.Count; ++Child)
            if (!Test(Children[Node.First + Child], Context, Cell)) return false;
        return true;
    case EOp::AnyOf:
        for (int32 Child = 0; Child < Node.Count; ++Child)
            if (Test(Children[Node.First + Child], Context, Cell)) return true;
        return false;
    case EOp::Not:
        return !Test(Children[Node.First], Context, Cell);
    }
    return false;
}

int32 FVoxelBlockPredicates::GetPlainSet(int32 Root) const
{
    return Nodes.IsValidIndex(Root) && Nodes[Root].Op == EOp::InSet && Nodes[Root].Offset == FIntVector::ZeroValue
        ? Nodes[Root].Argument
        : INDEX_NONE;
}
