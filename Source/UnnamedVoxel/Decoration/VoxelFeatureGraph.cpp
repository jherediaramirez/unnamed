#include "Decoration/VoxelFeatureGraph.h"

namespace
{
    bool Fail(FString* OutError, FString Message)
    {
        if (OutError) *OutError = MoveTemp(Message);
        return false;
    }

    int32 AddIndexed(TMap<FString, int32>& Index, FStringView Text, int32 NextIndex)
    {
        FString Key(Text);
        if (const int32* Found = Index.Find(Key)) return *Found;
        Index.Add(MoveTemp(Key), NextIndex);
        return NextIndex;
    }

    // One value of Owner's fields: indices in range, child nodes before Owner (so the graph has no cycles through
    // nodes), and a list's items before ItemLimit, which for a list inside a list is the outer list's start.
    bool CheckValue(const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value, int32 Owner, int32 ItemLimit,
        FString* OutError)
    {
        switch (Value.Type)
        {
        case EVoxelFeatureValueType::Number:
            return true;
        case EVoxelFeatureValueType::Bool:
            return Value.Number == 0 || Value.Number == 1 ||
                Fail(OutError, FString::Printf(TEXT("node %d: a bad bool"), Owner));
        case EVoxelFeatureValueType::String:
            return Graph.Strings.IsValidIndex(Value.Index) ||
                Fail(OutError, FString::Printf(TEXT("node %d: a bad string"), Owner));
        case EVoxelFeatureValueType::Node:
            return (Value.Index >= 0 && Value.Index < Owner) ||
                Fail(OutError, FString::Printf(TEXT("node %d: a child that is not before it"), Owner));
        case EVoxelFeatureValueType::Ref:
        {
            if (Value.Registry >= EVoxelFeatureRegistry::Count)
                return Fail(OutError, FString::Printf(TEXT("node %d: a ref to no registry"), Owner));
            const TArray<FVoxelFeatureEntry>& List = Graph.Entries[int32(Value.Registry)];
            return (List.IsValidIndex(Value.Index) && Graph.Nodes.IsValidIndex(List[Value.Index].Node)) ||
                Fail(OutError, FString::Printf(TEXT("node %d: a bad or unread ref"), Owner));
        }
        case EVoxelFeatureValueType::List:
            if (Value.Index < 0 || Value.Count < 0 || Value.Index + Value.Count > ItemLimit)
                return Fail(OutError, FString::Printf(TEXT("node %d: a bad list range"), Owner));
            for (int32 Item = Value.Index; Item < Value.Index + Value.Count; ++Item)
                if (!CheckValue(Graph, Graph.Items[Item], Owner, Value.Index, OutError)) return false;
            return true;
        }
        return Fail(OutError, FString::Printf(TEXT("node %d: a bad value type"), Owner));
    }

    int32 FindIndexed(const TMap<FString, int32>& Index, FStringView Text)
    {
        const int32* Found = Index.Find(FString(Text));
        return Found ? *Found : INDEX_NONE;
    }
}

int32 FVoxelFeatureGraph::AddString(FStringView Text)
{
    const int32 Index = AddIndexed(StringIndex, Text, Strings.Num());
    if (Index == Strings.Num()) Strings.Emplace(Text);
    return Index;
}

int32 FVoxelFeatureGraph::AddEntry(EVoxelFeatureRegistry Registry, FStringView Id)
{
    TArray<FVoxelFeatureEntry>& List = Entries[int32(Registry)];
    const int32 Index = AddIndexed(EntryIndex[int32(Registry)], Id, List.Num());
    if (Index == List.Num()) List.Add({FString(Id), INDEX_NONE});
    return Index;
}

int32 FVoxelFeatureGraph::AddTag(FStringView Id)
{
    const int32 Index = AddIndexed(TagIndex, Id, Tags.Num());
    if (Index == Tags.Num()) Tags.Add({FString(Id), {}});
    return Index;
}

int32 FVoxelFeatureGraph::FindEntry(EVoxelFeatureRegistry Registry, FStringView Id) const
{
    return FindIndexed(EntryIndex[int32(Registry)], Id);
}

int32 FVoxelFeatureGraph::FindTag(FStringView Id) const
{
    return FindIndexed(TagIndex, Id);
}

const FVoxelFeatureValue* FVoxelFeatureGraph::FindField(int32 Node, FStringView Key) const
{
    const FVoxelFeatureNode& Found = Nodes[Node];
    for (int32 Field = Found.FirstField; Field < Found.FirstField + Found.FieldCount; ++Field)
    {
        if (FStringView(Strings[Fields[Field].Key]) == Key) return &Fields[Field].Value;
    }
    return nullptr;
}

FStringView FVoxelFeatureGraph::GetType(int32 Node) const
{
    const int32 Type = Nodes[Node].Type;
    return Type == INDEX_NONE ? FStringView() : FStringView(Strings[Type]);
}

int32 FVoxelFeatureGraph::GetNode(const FVoxelFeatureValue& Value) const
{
    if (Value.Type == EVoxelFeatureValueType::Node) return Value.Index;
    if (Value.Type == EVoxelFeatureValueType::Ref) return Entries[int32(Value.Registry)][Value.Index].Node;
    return INDEX_NONE;
}

bool FVoxelFeatureGraph::ExpandTag(int32 Tag, TArray<FString>& OutBlocks, FString* OutError) const
{
    OutBlocks.Reset();
    if (!Tags.IsValidIndex(Tag)) return Fail(OutError, FString::Printf(TEXT("no tag %d"), Tag));
    TSet<int32> Seen = {Tag};
    TSet<FString> Added;
    // Includes expand where they stand, so blocks keep the order a reader of the files would meet them in. A tag that
    // includes itself, directly or through others, is an error, as in vanilla.
    TArray<TPair<int32, int32>> Stack = {{Tag, 0}};
    while (Stack.Num())
    {
        TPair<int32, int32>& Top = Stack.Last();
        const TArray<FString>& Values = Tags[Top.Key].Values;
        if (Top.Value == Values.Num())
        {
            Stack.Pop();
            continue;
        }
        const FString& Value = Values[Top.Value++];
        if (!Value.StartsWith(TEXT("#")))
        {
            if (!Added.Contains(Value))
            {
                Added.Add(Value);
                OutBlocks.Add(Value);
            }
            continue;
        }
        const int32 Included = FindTag(FStringView(Value).RightChop(1));
        if (Included == INDEX_NONE)
            return Fail(
                OutError, FString::Printf(TEXT("tag %s includes the unknown tag %s"), *Tags[Top.Key].Id, *Value));
        if (Stack.ContainsByPredicate([Included](const TPair<int32, int32>& Entry) { return Entry.Key == Included; }))
            return Fail(
                OutError, FString::Printf(TEXT("tag %s includes itself through %s"), *Value, *Tags[Top.Key].Id));
        if (Seen.Contains(Included)) continue;
        Seen.Add(Included);
        Stack.Add({Included, 0});
    }
    return true;
}

bool FVoxelFeatureGraph::Validate(FString* OutError) const
{
    for (int32 Node = 0; Node < Nodes.Num(); ++Node)
    {
        const FVoxelFeatureNode& Found = Nodes[Node];
        if (Found.Kind >= EVoxelFeatureKind::Count || Found.FirstField < 0 || Found.FieldCount < 0 ||
            Found.FirstField + Found.FieldCount > Fields.Num() ||
            (Found.Type != INDEX_NONE && !Strings.IsValidIndex(Found.Type)))
            return Fail(OutError, FString::Printf(TEXT("node %d: bad kind, type or field range"), Node));
        for (int32 Field = Found.FirstField; Field < Found.FirstField + Found.FieldCount; ++Field)
        {
            if (!Strings.IsValidIndex(Fields[Field].Key))
                return Fail(OutError, FString::Printf(TEXT("node %d: a field has a bad key"), Node));
            if (!CheckValue(*this, Fields[Field].Value, Node, Items.Num(), OutError)) return false;
        }
    }
    static constexpr EVoxelFeatureKind RegistryKinds[] = {EVoxelFeatureKind::PlacedFeature, EVoxelFeatureKind::Feature,
        EVoxelFeatureKind::StateProvider, EVoxelFeatureKind::Carver};
    for (int32 Registry = 0; Registry < int32(EVoxelFeatureRegistry::Count); ++Registry)
    {
        for (const FVoxelFeatureEntry& Entry : Entries[Registry])
        {
            const bool bKind = Nodes.IsValidIndex(Entry.Node) &&
                (Nodes[Entry.Node].Kind == RegistryKinds[Registry] ||
                    (RegistryKinds[Registry] == EVoxelFeatureKind::StateProvider &&
                        Nodes[Entry.Node].Kind == EVoxelFeatureKind::BlockState));
            if (!bKind)
                return Fail(OutError,
                    FString::Printf(TEXT("%s %s: no node of kind %s"),
                        GetRegistryFolder(EVoxelFeatureRegistry(Registry)), *Entry.Id,
                        GetKindName(RegistryKinds[Registry])));
        }
    }
    for (const FVoxelBiomeDecoration& Biome : Biomes)
    {
        if (Biome.Steps.Num() != StepCount)
            return Fail(OutError, FString::Printf(TEXT("biome %s: %d steps"), *Biome.Biome, Biome.Steps.Num()));
        for (const TArray<int32>& Step : Biome.Steps)
            for (const int32 Entry : Step)
                if (!Entries[int32(EVoxelFeatureRegistry::PlacedFeature)].IsValidIndex(Entry))
                    return Fail(OutError, FString::Printf(TEXT("biome %s: bad placed feature entry"), *Biome.Biome));
        for (const int32 Entry : Biome.Carvers)
            if (!Entries[int32(EVoxelFeatureRegistry::Carver)].IsValidIndex(Entry))
                return Fail(OutError, FString::Printf(TEXT("biome %s: bad carver entry"), *Biome.Biome));
    }
    TArray<FString> Blocks;
    for (int32 Tag = 0; Tag < Tags.Num(); ++Tag)
    {
        if (!ExpandTag(Tag, Blocks, OutError)) return false;
    }
    return true;
}

const TCHAR* FVoxelFeatureGraph::GetStepName(int32 Step)
{
    // GenerationStep.Decoration, in order.
    static const TCHAR* const Names[StepCount] = {TEXT("raw_generation"), TEXT("lakes"), TEXT("local_modifications"),
        TEXT("underground_structures"), TEXT("surface_structures"), TEXT("strongholds"), TEXT("underground_ores"),
        TEXT("underground_decoration"), TEXT("fluid_springs"), TEXT("vegetal_decoration"),
        TEXT("top_layer_modification")};
    return Step >= 0 && Step < StepCount ? Names[Step] : TEXT("?");
}

const TCHAR* FVoxelFeatureGraph::GetKindName(EVoxelFeatureKind Kind)
{
    static const TCHAR* const Names[] = {TEXT("placed feature"), TEXT("feature"), TEXT("placement modifier"),
        TEXT("block predicate"), TEXT("rule test"), TEXT("block state provider"), TEXT("block state"),
        TEXT("int provider"), TEXT("float provider"), TEXT("height provider"), TEXT("vertical anchor"),
        TEXT("trunk placer"), TEXT("foliage placer"), TEXT("root placer"), TEXT("feature size"), TEXT("tree decorator"),
        TEXT("carver"), TEXT("template processor"), TEXT("block entity modifier"), TEXT("block state properties"),
        TEXT("object")};
    static_assert(UE_ARRAY_COUNT(Names) == int32(EVoxelFeatureKind::Count));
    return Kind < EVoxelFeatureKind::Count ? Names[int32(Kind)] : TEXT("?");
}

const TCHAR* FVoxelFeatureGraph::GetRegistryFolder(EVoxelFeatureRegistry Registry)
{
    static const TCHAR* const Folders[] = {
        TEXT("placed_feature"), TEXT("feature"), TEXT("block_state_provider"), TEXT("carver")};
    static_assert(UE_ARRAY_COUNT(Folders) == int32(EVoxelFeatureRegistry::Count));
    return Registry < EVoxelFeatureRegistry::Count ? Folders[int32(Registry)] : TEXT("?");
}
