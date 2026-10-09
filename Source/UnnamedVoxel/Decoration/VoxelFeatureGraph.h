#pragma once

#include "CoreMinimal.h"

// What a node of the feature graph is. A field's schema (the reader's) names the kind its child must be; the kind also
// says which key holds the node's type in the reference: "predicate_type" for rule tests, "processor_type" for template
// processors, none for the untyped kinds (placed features, block states, vertical anchors, block state properties and
// other objects), and "type" for the rest.
enum class EVoxelFeatureKind : uint8
{
    PlacedFeature, // {"feature", "placement"}
    Feature,
    Modifier, // a placement modifier
    BlockPredicate,
    RuleTest, // an ore target's or a template rule's test
    StateProvider,
    BlockState, // {"id", "properties"}; also a provider where a provider is expected
    IntProvider,
    FloatProvider,
    HeightProvider,
    VerticalAnchor, // {"absolute"}, {"above_bottom"}, {"below_top"} or {"relative_to_sea_level"}
    TrunkPlacer,
    FoliagePlacer,
    RootPlacer,
    FeatureSize,
    TreeDecorator,
    Carver,
    Processor, // a template processor (templates are deferred to phase 7; read, not used)
    BlockEntityModifier,
    Properties, // a block state's properties: names to values, all strings
    Object, // any other untyped object, named by its schema ("ore_target", "weighted_state")
    Count
};

// The named entries of the reference's decoration folders, each a registry of its own.
enum class EVoxelFeatureRegistry : uint8
{
    PlacedFeature, // worldgen/placed_feature
    Feature, // worldgen/feature
    Provider, // worldgen/block_state_provider
    Carver, // worldgen/carver
    Count
};

enum class EVoxelFeatureValueType : uint8
{
    Number, // Number
    Bool, // Number, 0 or 1
    String, // Index: Strings
    Node, // Index: Nodes
    Ref, // Registry and Index: an entry named by its id
    List, // Index and Count: Items
};

struct FVoxelFeatureValue
{
    EVoxelFeatureValueType Type = EVoxelFeatureValueType::Number;
    EVoxelFeatureRegistry Registry = EVoxelFeatureRegistry::PlacedFeature;
    int32 Index = INDEX_NONE;
    int32 Count = 0;
    double Number = 0;
};

struct FVoxelFeatureField
{
    // Strings: the field's name.
    int32 Key = INDEX_NONE;
    FVoxelFeatureValue Value;
};

struct FVoxelFeatureNode
{
    EVoxelFeatureKind Kind = EVoxelFeatureKind::Object;
    // Strings: the full type id ("minecraft:ore") for typed kinds, the schema's name for objects, or INDEX_NONE.
    int32 Type = INDEX_NONE;
    int32 FirstField = 0;
    int32 FieldCount = 0;
};

struct FVoxelFeatureEntry
{
    FString Id;
    int32 Node = INDEX_NONE;
};

// A block tag as its file lists it: block ids, and "#"-prefixed tags it includes.
struct FVoxelBlockTag
{
    FString Id;
    TArray<FString> Values;
};

// One biome's decoration lists: placed feature entries per generation step, and carver entries.
struct FVoxelBiomeDecoration
{
    FString Biome;
    TArray<TArray<int32>> Steps;
    TArray<int32> Carvers;
};

// The 26.3 decoration data the overworld biomes reach, as plain data: one flat table of typed nodes whose fields hold
// numbers, strings, child nodes, lists, or entries named by id. The reader keeps the reference's shape exactly (it
// writes the nodes back as the files' JSON), and later sessions compile the types they run from it, as the surface
// evaluator compiles its rules. Ids stay strings here; blocks are looked up when a feature type is compiled.
class UNNAMEDVOXEL_API FVoxelFeatureGraph
{
public:
    static constexpr int32 StepCount = 11;

    TArray<FVoxelFeatureNode> Nodes;
    TArray<FVoxelFeatureField> Fields;
    // List items, each list's items in a row.
    TArray<FVoxelFeatureValue> Items;
    TArray<FString> Strings;
    TArray<FVoxelFeatureEntry> Entries[int32(EVoxelFeatureRegistry::Count)];
    TArray<FVoxelBlockTag> Tags;
    // In the order of the biome tag is_overworld.
    TArray<FVoxelBiomeDecoration> Biomes;

    // The index of Text in Strings, added if new.
    int32 AddString(FStringView Text);
    // An entry's index, added with no node if new.
    int32 AddEntry(EVoxelFeatureRegistry Registry, FStringView Id);
    int32 AddTag(FStringView Id);

    // Entry and tag indices, or INDEX_NONE.
    int32 FindEntry(EVoxelFeatureRegistry Registry, FStringView Id) const;
    int32 FindTag(FStringView Id) const;
    // A field of Node by name, or null.
    const FVoxelFeatureValue* FindField(int32 Node, FStringView Key) const;
    // The node's type string ("minecraft:ore"), or empty.
    FStringView GetType(int32 Node) const;
    // The node a value leads to: Node itself, or a Ref's entry node; INDEX_NONE otherwise.
    int32 GetNode(const FVoxelFeatureValue& Value) const;

    // Every block id a tag holds, its included tags expanded, in first-seen order without repeats.
    bool ExpandTag(int32 Tag, TArray<FString>& OutBlocks, FString* OutError = nullptr) const;

    // Checks every index and count, that child nodes come before the nodes that hold them (so nodes form no cycle),
    // that each entry's node has its registry's kind, that refs point at entries with nodes, that biome lists hold
    // StepCount steps of placed feature entries, and that tag includes resolve without a cycle.
    bool Validate(FString* OutError = nullptr) const;

    static const TCHAR* GetStepName(int32 Step);
    static const TCHAR* GetKindName(EVoxelFeatureKind Kind);
    // The registry's folder under worldgen/ ("placed_feature").
    static const TCHAR* GetRegistryFolder(EVoxelFeatureRegistry Registry);

private:
    TMap<FString, int32> StringIndex;
    TMap<FString, int32> EntryIndex[int32(EVoxelFeatureRegistry::Count)];
    TMap<FString, int32> TagIndex;
};
