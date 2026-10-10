#include "Decoration/VoxelRuleTests.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelFeatureGraph.h"

namespace
{
    bool RuleFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    FStringView GetString(const FVoxelFeatureGraph& Graph, int32 Node, const TCHAR* Key)
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        return Value && Value->Type == EVoxelFeatureValueType::String && Graph.Strings.IsValidIndex(Value->Index)
            ? FStringView(Graph.Strings[Value->Index])
            : FStringView();
    }

    // Children of a rule list field, or of a single rule field.
    void GetRules(const FVoxelFeatureGraph& Graph, int32 Node, const TCHAR* Key, TArray<int32>& Out)
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        if (!Value) return;
        if (Value->Type == EVoxelFeatureValueType::List)
            for (int32 Item = 0; Item < Value->Count; ++Item)
                Out.Add(Graph.GetNode(Graph.Items[Value->Index + Item]));
        else
            Out.Add(Graph.GetNode(*Value));
    }

    // Z values where some height_match in the tree starts or stops matching.
    void CollectBreaks(const FVoxelFeatureGraph& Graph, int32 Node, TArray<int32>& Out)
    {
        if (Node == INDEX_NONE) return;
        if (Graph.GetType(Node) == TEXT("minecraft:height_match"))
        {
            const FVoxelFeatureValue* Min = Graph.FindField(Node, TEXT("min_inclusive"));
            const FVoxelFeatureValue* Max = Graph.FindField(Node, TEXT("max_inclusive"));
            if (Min) Out.Add(int32(Min->Number));
            if (Max) Out.Add(int32(FMath::Min(Max->Number + 1., double(MAX_int32))));
        }
        TArray<int32> Children;
        GetRules(Graph, Node, TEXT("rules"), Children);
        GetRules(Graph, Node, TEXT("rule"), Children);
        for (const int32 Child : Children)
            CollectBreaks(Graph, Child, Out);
    }

    bool Evaluate(const FVoxelFeatureGraph& Graph, int32 Node, int32 Z, FVoxelBlockTags& Tags, FVoxelBlockSet& Out,
        FString* OutError)
    {
        if (Node == INDEX_NONE || Graph.Nodes[Node].Kind != EVoxelFeatureKind::RuleTest)
            return RuleFail(OutError, TEXT("not a rule test"));
        const int32 Limit = Tags.GetBlockLimit();
        const FStringView Type = Graph.GetType(Node);
        if (Type == TEXT("minecraft:always_true"))
        {
            Out.Init(Limit, true);
            return true;
        }
        if (Type == TEXT("minecraft:block_match"))
        {
            Out.Init(Limit, false);
            const int32 Block = Tags.FindBlock(GetString(Graph, Node, TEXT("block")));
            if (Block != INDEX_NONE) Out.Add(Block);
            return true;
        }
        if (Type == TEXT("minecraft:tag_match"))
        {
            const FVoxelBlockSet* Set = Tags.Find(GetString(Graph, Node, TEXT("tag")), OutError);
            if (!Set) return false;
            Out = *Set;
            return true;
        }
        if (Type == TEXT("minecraft:height_match"))
        {
            const FVoxelFeatureValue* Min = Graph.FindField(Node, TEXT("min_inclusive"));
            const FVoxelFeatureValue* Max = Graph.FindField(Node, TEXT("max_inclusive"));
            if (!Min || !Max) return RuleFail(OutError, TEXT("height_match without its bounds"));
            Out.Init(Limit, Z >= Min->Number && Z <= Max->Number);
            return true;
        }
        TArray<int32> Children;
        if (Type == TEXT("minecraft:not"))
        {
            GetRules(Graph, Node, TEXT("rule"), Children);
            if (Children.Num() != 1 || !Evaluate(Graph, Children[0], Z, Tags, Out, OutError)) return false;
            FVoxelBlockSet All;
            All.Init(Limit, true);
            for (int32 Word = 0; Word < Out.Words.Num(); ++Word)
                Out.Words[Word] = ~Out.Words[Word] & All.Words[Word];
            return true;
        }
        const bool bAll = Type == TEXT("minecraft:all_of");
        if (bAll || Type == TEXT("minecraft:any_of"))
        {
            GetRules(Graph, Node, TEXT("rules"), Children);
            Out.Init(Limit, bAll);
            for (const int32 Child : Children)
            {
                FVoxelBlockSet Part;
                if (!Evaluate(Graph, Child, Z, Tags, Part, OutError)) return false;
                for (int32 Word = 0; Word < Out.Words.Num(); ++Word)
                    Out.Words[Word] = bAll ? Out.Words[Word] & Part.Words[Word] : Out.Words[Word] | Part.Words[Word];
            }
            return true;
        }
        return RuleFail(OutError, FString::Printf(TEXT("rule test %s is not compiled yet"), *FString(Type)));
    }
}

void FVoxelBlockSet::Init(int32 BlockLimit, bool bAll)
{
    Words.Init(bAll ? ~uint64(0) : 0, (BlockLimit + 63) / 64);
    if (bAll && (BlockLimit & 63)) Words.Last() = (uint64(1) << (BlockLimit & 63)) - 1;
}

FVoxelBlockTags::FVoxelBlockTags(const FVoxelFeatureGraph& InGraph, const FVoxelBlockTable& InTable)
    : Graph(InGraph), Table(InTable), BlockLimit(InTable.GetBlockLimit())
{
}

int32 FVoxelBlockTags::FindBlock(FStringView Id)
{
    const int32 Block = Table.FindBlock(Id);
    if (Block == INDEX_NONE) Missing.Add(FString(Id));
    return Block;
}

const FVoxelBlockSet* FVoxelBlockTags::Find(FStringView Tag, FString* OutError)
{
    int32 Index = Graph.FindTag(Tag);
    if (Index == INDEX_NONE && !Tag.StartsWith(TEXT("minecraft:")))
        Index = Graph.FindTag(FString(TEXT("minecraft:")) + FString(Tag));
    if (Index == INDEX_NONE && Tag.StartsWith(TEXT("minecraft:"))) Index = Graph.FindTag(Tag.RightChop(10));
    if (Index == INDEX_NONE)
    {
        RuleFail(OutError, FString::Printf(TEXT("no block tag %s"), *FString(Tag)));
        return nullptr;
    }
    if (const FVoxelBlockSet* Found = Sets.Find(Index)) return Found;
    TArray<FString> Blocks;
    if (!Graph.ExpandTag(Index, Blocks, OutError)) return nullptr;
    FVoxelBlockSet Set;
    Set.Init(BlockLimit, false);
    for (const FString& Id : Blocks)
    {
        const int32 Block = FindBlock(Id);
        if (Block != INDEX_NONE) Set.Add(Block);
    }
    return &Sets.Add(Index, MoveTemp(Set));
}

bool FVoxelRuleTest::Compile(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags, FString* OutError)
{
    // Bands start at MinZ and at every break inside the world.
    TArray<int32> Breaks;
    CollectBreaks(Graph, Node, Breaks);
    TArray<int32> Starts = {VoxelConstants::MinZ};
    for (const int32 Break : Breaks)
        if (Break > VoxelConstants::MinZ && Break <= VoxelConstants::MaxZ) Starts.AddUnique(Break);
    Starts.Sort();
    if (Starts.Num() > 255) return RuleFail(OutError, TEXT("rule test with too many height bands"));
    Sets.SetNum(Starts.Num());
    for (int32 Band = 0; Band < Starts.Num(); ++Band)
    {
        if (!Evaluate(Graph, Node, Starts[Band], Tags, Sets[Band], OutError)) return false;
        const int32 End = Band + 1 < Starts.Num() ? Starts[Band + 1] : VoxelConstants::MaxZ + 1;
        for (int32 Z = Starts[Band]; Z < End; ++Z)
            BandOfZ[Z - VoxelConstants::MinZ] = uint8(Band);
    }
    return true;
}

bool FVoxelRuleTest::CanMatch(int32 Block) const
{
    for (const FVoxelBlockSet& Set : Sets)
        if (Set.Contains(Block)) return true;
    return false;
}
