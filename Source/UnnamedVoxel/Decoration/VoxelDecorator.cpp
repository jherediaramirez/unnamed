#include "Decoration/VoxelDecorator.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelFeatureOrder.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelTerrain.h"
#include "VoxelConstants.h"

namespace
{
    // Each try of a count modifier takes its own stream from (the stream it came from, its index), so a skipped or
    // failed try never shifts the ones after it (scout E's early-out; Factorio 2.0.34's lesson).
    constexpr uint64 TrySalt = VoxelRandom::Salt("decoration_try");

    bool DecoratorFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    // What the heightmaps count about a state (FVoxelColumn's rules): a write that changes none of it leaves them.
    uint8 GetHeightClass(const FVoxelBlockTable& Table, uint16 State)
    {
        const FVoxelStateInfo& Info = Table.Get(State);
        return uint8(State != 0) | uint8(Info.bSolid) << 1 | uint8(Info.IsFluid()) << 2 | uint8(Info.bLeaves) << 3;
    }

    int32 FloorColumn(int32 Block)
    {
        return Block >> 4;
    }

    // The placed features S8's types complete that S11 and S12 own (scout I's question 1).
    bool IsLaterPlant(FStringView Id)
    {
        static const TCHAR* const Later[] = {TEXT("minecraft:lush_caves_vegetation"),
            TEXT("minecraft:lush_caves_ceiling_vegetation"), TEXT("minecraft:lush_caves_clay"),
            TEXT("minecraft:cave_vines"), TEXT("minecraft:spore_blossom"), TEXT("minecraft:sculk_vein"),
            TEXT("minecraft:ice_patch")};
        for (const TCHAR* Name : Later)
            if (Id == Name) return true;
        return false;
    }

    bool ReadHeightmap(FStringView Name, EVoxelDecorationHeightmap& Out)
    {
        static const TCHAR* const Names[] = {TEXT("WORLD_SURFACE_WG"), TEXT("OCEAN_FLOOR_WG"), TEXT("WORLD_SURFACE"),
            TEXT("OCEAN_FLOOR"), TEXT("MOTION_BLOCKING"), TEXT("MOTION_BLOCKING_NO_LEAVES")};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
            if (Name == Names[Index])
            {
                Out = EVoxelDecorationHeightmap(Index);
                return true;
            }
        return false;
    }

    double DecoratorNumber(const FVoxelFeatureGraph& Graph, int32 Node, const TCHAR* Key, double Default = 0)
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        return Value && (Value->Type == EVoxelFeatureValueType::Number || Value->Type == EVoxelFeatureValueType::Bool)
            ? Value->Number
            : Default;
    }

    FStringView DecoratorString(const FVoxelFeatureGraph& Graph, int32 Node, const TCHAR* Key)
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        return Value && Value->Type == EVoxelFeatureValueType::String ? FStringView(Graph.Strings[Value->Index])
                                                                      : FStringView();
    }
}

FVoxelDecorator::FVoxelDecorator() = default;
FVoxelDecorator::~FVoxelDecorator() = default;

bool FVoxelDecorator::Initialize(const FVoxelFeatureGraph& Graph, const FVoxelBlockTable& InTable,
    const FVoxelBiomeTable& Biomes, uint64 InWorldSeed, const FVoxelDecorationOptions& InOptions, FString* OutError)
{
    Table = nullptr;
    Features.Reset();
    Ores.Reset();
    TargetLists.Reset();
    FeaturesOfBiome.Reset();
    Skipped.Reset();
    MissingBlocks.Reset();
    Plants.Reset();
    Placed.Reset();
    UpperOf.Reset();
    PlantOfNode.Reset();
    PlacedOfNode.Reset();
    FVoxelFeatureOrder Order;
    if (!Order.Build(Graph, OutError)) return false;
    FVoxelBlockTags Tags(Graph, InTable);
    Table = &InTable;
    WorldSeed = InWorldSeed;
    Options = InOptions;
    if (Options.bPlants)
    {
        const bool bDrop = Options.Mutation == EVoxelDecorationMutation::DroppedSupportTag;
        if (!Survival.Initialize(InTable, Tags, OutError, bDrop ? TEXT("minecraft:supports_vegetation") : TEXT("")))
        {
            Table = nullptr;
            return false;
        }
        Predicates.Initialize(InTable, Survival);
        Providers.Initialize(InTable, Predicates, WorldSeed);
        CountNoise =
            FVoxelGradientNoise(VoxelFeatureSeeds::GetPatternSeed(WorldSeed, VoxelFeatureSeeds::VanillaPatternSeed));
    }

    TMap<int32, int32> FeatureOfEntry;
    for (int32 Step = 0; Step < Order.Steps.Num(); ++Step)
        for (int32 Index = 0; Index < Order.Steps[Step].Num(); ++Index)
        {
            const int32 Entry = Order.Steps[Step][Index];
            if (Index > 255)
            {
                Table = nullptr;
                return DecoratorFail(OutError,
                    FString::Printf(TEXT("step %d lists more than 256 placed features (the "
                                         "merge key holds 8 bits)"),
                        Step));
            }
            const FString& Id = Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)][Entry].Id;
            if (Options.Filter && !Options.Filter(Id)) continue;
            if (Options.bPlants && !Options.bLaterPlants && IsLaterPlant(Id))
            {
                ++Skipped.FindOrAdd(TEXT("S11 and S12's placed features (scout I's question 1)"));
                continue;
            }
            FFeature Feature;
            Feature.Id = Id;
            Feature.Step = Step;
            Feature.IndexInStep = Index;
            Feature.Key = FVoxelWriteBucket::MakeKey(Step, Index);
            Feature.Salt = VoxelFeatureSeeds::GetFeatureSalt(Id);
            FString Skip;
            if (!CompileFeature(Graph, Entry, Tags, Feature, Skip, OutError))
            {
                Table = nullptr;
                if (OutError) *OutError = FString::Printf(TEXT("placed feature %s: %s"), *Id, **OutError);
                return false;
            }
            if (!Skip.IsEmpty())
            {
                ++Skipped.FindOrAdd(Skip);
                continue;
            }
            FeatureOfEntry.Add(Entry, Features.Num());
            Features.Add(MoveTemp(Feature));
        }

    if (Options.Mutation == EVoxelDecorationMutation::SwappedOrder)
    {
        // The first two compiled features of one step trade places and keys.
        for (int32 Index = 0; Index + 1 < Features.Num(); ++Index)
            if (Features[Index].Step == Features[Index + 1].Step)
            {
                Swap(Features[Index].Key, Features[Index + 1].Key);
                Swap(Features[Index].IndexInStep, Features[Index + 1].IndexInStep);
                Features.Swap(Index, Index + 1);
                for (TPair<int32, int32>& Pair : FeatureOfEntry)
                    Pair.Value = Pair.Value == Index ? Index + 1 : Pair.Value == Index + 1 ? Index : Pair.Value;
                break;
            }
    }

    TMap<FString, int32> GraphBiome;
    for (int32 Biome = 0; Biome < Graph.Biomes.Num(); ++Biome)
        GraphBiome.Add(Graph.Biomes[Biome].Biome, Biome);
    FeaturesOfBiome.SetNum(Biomes.Biomes.Num());
    for (int32 Biome = 0; Biome < Biomes.Biomes.Num(); ++Biome)
    {
        FeaturesOfBiome[Biome].Init(false, Features.Num());
        const int32* Found = GraphBiome.Find(Biomes.Biomes[Biome].Name);
        if (!Found) continue;
        for (const TArray<int32>& Step : Graph.Biomes[*Found].Steps)
            for (const int32 Entry : Step)
                if (const int32* Feature = FeatureOfEntry.Find(Entry)) FeaturesOfBiome[Biome][*Feature] = true;
    }
    MissingBlocks = Tags.GetMissing();
    PlantOfNode.Empty();
    PlacedOfNode.Empty();
    return true;
}

bool FVoxelDecorator::CompileFeature(const FVoxelFeatureGraph& Graph, int32 Entry, FVoxelBlockTags& Tags, FFeature& Out,
    FString& OutSkip, FString* OutError)
{
    const int32 Node = Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)][Entry].Node;
    const FVoxelFeatureValue* FeatureValue = Graph.FindField(Node, TEXT("feature"));
    const int32 FeatureNode = FeatureValue ? Graph.GetNode(*FeatureValue) : INDEX_NONE;
    if (FeatureNode == INDEX_NONE) return DecoratorFail(OutError, TEXT("no feature"));
    const FStringView Type = Graph.GetType(FeatureNode);
    if (Type != TEXT("minecraft:ore"))
    {
        if (!Options.bPlants)
        {
            OutSkip = FString(Type);
            return true;
        }
        // S8: a vegetation feature, run once every node it reaches is built.
        int32 Plant = INDEX_NONE;
        if (!CompilePlant(Graph, FeatureNode, Tags, Plant, OutSkip, OutError)) return false;
        if (!OutSkip.IsEmpty()) return true;
        if (!CompileModifiers(Graph, Node, Tags, false, Out.Modifiers, OutSkip, OutError)) return false;
        if (!OutSkip.IsEmpty()) return true;
        Out.Plant = Plant;
        return true;
    }

    // The placement: only the five modifiers the ores use are compiled so far.
    const FVoxelFeatureValue* Placement = Graph.FindField(Node, TEXT("placement"));
    for (int32 Item = 0; Placement && Item < Placement->Count; ++Item)
    {
        const int32 ModifierNode = Graph.GetNode(Graph.Items[Placement->Index + Item]);
        const FStringView ModifierType = Graph.GetType(ModifierNode);
        FModifier Modifier;
        if (ModifierType == TEXT("minecraft:count"))
        {
            const FVoxelFeatureValue* Count = Graph.FindField(ModifierNode, TEXT("count"));
            Modifier.Type = FModifier::EType::Count;
            if (!Count || !VoxelNumberProviders::CompileInt(Graph, *Count, Modifier.Provider, OutError)) return false;
        }
        else if (ModifierType == TEXT("minecraft:in_square"))
            Modifier.Type = FModifier::EType::InSquare;
        else if (ModifierType == TEXT("minecraft:height_range"))
        {
            const FVoxelFeatureValue* Height = Graph.FindField(ModifierNode, TEXT("height"));
            Modifier.Type = FModifier::EType::HeightRange;
            if (!Height || !VoxelNumberProviders::CompileHeight(Graph, *Height, Modifier.Provider, OutError))
                return false;
        }
        else if (ModifierType == TEXT("minecraft:rarity_filter"))
        {
            const FVoxelFeatureValue* Chance = Graph.FindField(ModifierNode, TEXT("chance"));
            Modifier.Type = FModifier::EType::RarityFilter;
            Modifier.Chance = Chance ? FMath::Max(1, int32(Chance->Number)) : 1;
        }
        else if (ModifierType == TEXT("minecraft:biome"))
            Modifier.Type = FModifier::EType::Biome;
        else
        {
            OutSkip = FString::Printf(TEXT("modifier %s"), *FString(ModifierType));
            return true;
        }
        Out.Modifiers.Add(Modifier);
    }
    Out.bPruneAtBiome = !Out.Modifiers.IsEmpty() && Out.Modifiers.Last().Type == FModifier::EType::Biome;

    FOre Ore;
    TArray<FVoxelOreTarget> Targets;
    if (!VoxelOreFeature::Compile(Graph, FeatureNode, Tags, *Table, Ore.Config, Targets, OutSkip, OutError))
        return false;
    if (!OutSkip.IsEmpty()) return true;
    Ore.Reach = VoxelOreFeature::GetReach(Ore.Config.Size);
    for (const FVoxelOreTarget& Target : Targets)
        Ore.bTargetsAir |= Target.Rule.CanMatch(0);
    Ore.Config.Targets = TargetLists.Add(MoveTemp(Targets));
    Out.Ore = Ores.Add(Ore);
    return true;
}

bool FVoxelDecorator::CompileModifiers(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags, bool bNested,
    TArray<FModifier>& Out, FString& OutSkip, FString* OutError)
{
    const auto Int = [&](int32 ModifierNode, const TCHAR* Key, FVoxelIntProvider& Provider) -> bool
    {
        const FVoxelFeatureValue* Value = Graph.FindField(ModifierNode, Key);
        FString Error;
        if (Value && VoxelNumberProviders::CompileInt(Graph, *Value, Provider, &Error)) return true;
        OutSkip = Value ? Error : FString::Printf(TEXT("modifier without %s"), Key);
        return false;
    };
    const FVoxelFeatureValue* Placement = Graph.FindField(Node, TEXT("placement"));
    for (int32 Item = 0; Placement && Item < Placement->Count; ++Item)
    {
        const int32 ModifierNode = Graph.GetNode(Graph.Items[Placement->Index + Item]);
        const FStringView ModifierType = Graph.GetType(ModifierNode);
        FModifier Modifier;
        if (ModifierType == TEXT("minecraft:count"))
        {
            Modifier.Type = FModifier::EType::Count;
            if (!Int(ModifierNode, TEXT("count"), Modifier.Provider)) return true;
        }
        else if (ModifierType == TEXT("minecraft:in_square"))
            Modifier.Type = FModifier::EType::InSquare;
        else if (ModifierType == TEXT("minecraft:height_range"))
        {
            const FVoxelFeatureValue* Height = Graph.FindField(ModifierNode, TEXT("height"));
            Modifier.Type = FModifier::EType::HeightRange;
            FString Error;
            if (!Height || !VoxelNumberProviders::CompileHeight(Graph, *Height, Modifier.Provider, &Error))
            {
                OutSkip = Error;
                return true;
            }
        }
        else if (ModifierType == TEXT("minecraft:rarity_filter"))
        {
            Modifier.Type = FModifier::EType::RarityFilter;
            Modifier.Chance = FMath::Max(1, int32(DecoratorNumber(Graph, ModifierNode, TEXT("chance"), 1)));
        }
        else if (ModifierType == TEXT("minecraft:biome") && !bNested)
            Modifier.Type = FModifier::EType::Biome;
        else if (ModifierType == TEXT("minecraft:heightmap") ||
            ModifierType == TEXT("minecraft:surface_relative_threshold_filter"))
        {
            Modifier.Type = ModifierType == TEXT("minecraft:heightmap") ? FModifier::EType::Heightmap
                                                                        : FModifier::EType::SurfaceRelative;
            if (!ReadHeightmap(DecoratorString(Graph, ModifierNode, TEXT("heightmap")), Modifier.Map))
                return DecoratorFail(OutError, TEXT("a heightmap modifier without a known heightmap"));
            Modifier.Below = int32(DecoratorNumber(Graph, ModifierNode, TEXT("min_inclusive"), MIN_int32));
            Modifier.Above = int32(DecoratorNumber(Graph, ModifierNode, TEXT("max_inclusive"), MAX_int32));
        }
        else if (ModifierType == TEXT("minecraft:offset"))
        {
            // The reference's x, y (up) and z are our X, Z and Y.
            Modifier.Type = FModifier::EType::Offset;
            if (!Int(ModifierNode, TEXT("x"), Modifier.Provider) || !Int(ModifierNode, TEXT("y"), Modifier.OffsetZ) ||
                !Int(ModifierNode, TEXT("z"), Modifier.OffsetY))
                return true;
        }
        else if (ModifierType == TEXT("minecraft:block_predicate_filter"))
        {
            const FVoxelFeatureValue* Predicate = Graph.FindField(ModifierNode, TEXT("predicate"));
            if (!Predicate) return DecoratorFail(OutError, TEXT("block_predicate_filter without a predicate"));
            Modifier.Type = FModifier::EType::Filter;
            if (!Predicates.Compile(Graph, Graph.GetNode(*Predicate), Tags, Modifier.Predicate, OutSkip, OutError))
                return false;
            if (!OutSkip.IsEmpty()) return true;
        }
        else if (ModifierType == TEXT("minecraft:noise_threshold_count"))
        {
            Modifier.Type = FModifier::EType::NoiseThresholdCount;
            Modifier.Level = DecoratorNumber(Graph, ModifierNode, TEXT("noise_level"));
            Modifier.Below = int32(DecoratorNumber(Graph, ModifierNode, TEXT("below_noise")));
            Modifier.Above = int32(DecoratorNumber(Graph, ModifierNode, TEXT("above_noise")));
        }
        else if (ModifierType == TEXT("minecraft:surface_water_depth_filter") && Options.bTrees)
        {
            // The water over the floor (WORLD_SURFACE minus OCEAN_FLOOR) is at most max_water_depth, as the field's
            // name says (the wiki's strict "less than" would refuse every tree on land, whose data says 0).
            Modifier.Type = FModifier::EType::SurfaceWaterDepth;
            Modifier.Above = int32(DecoratorNumber(Graph, ModifierNode, TEXT("max_water_depth")));
        }
        else if (ModifierType == TEXT("minecraft:environment_scan") && Options.bTrees)
        {
            Modifier.Type = FModifier::EType::EnvironmentScan;
            const FStringView Direction = DecoratorString(Graph, ModifierNode, TEXT("direction_of_search"));
            if (Direction != TEXT("up") && Direction != TEXT("down"))
                return DecoratorFail(OutError, TEXT("environment_scan without an up or down direction"));
            Modifier.ScanStep = Direction == TEXT("up") ? 1 : -1;
            Modifier.ScanSteps = int32(DecoratorNumber(Graph, ModifierNode, TEXT("max_steps")));
            const FVoxelFeatureValue* Target = Graph.FindField(ModifierNode, TEXT("target_condition"));
            if (!Target) return DecoratorFail(OutError, TEXT("environment_scan without a target_condition"));
            if (!Predicates.Compile(Graph, Graph.GetNode(*Target), Tags, Modifier.Predicate, OutSkip, OutError))
                return false;
            if (!OutSkip.IsEmpty()) return true;
            if (const FVoxelFeatureValue* Allowed = Graph.FindField(ModifierNode, TEXT("allowed_search_condition")))
            {
                if (!Predicates.Compile(Graph, Graph.GetNode(*Allowed), Tags, Modifier.ScanAllowed, OutSkip, OutError))
                    return false;
                if (!OutSkip.IsEmpty()) return true;
            }
        }
        else if (ModifierType == TEXT("minecraft:noise_based_count"))
        {
            Modifier.Type = FModifier::EType::NoiseBasedCount;
            Modifier.Ratio = DecoratorNumber(Graph, ModifierNode, TEXT("noise_to_count_ratio"));
            Modifier.Factor = DecoratorNumber(Graph, ModifierNode, TEXT("noise_factor"), 1);
            Modifier.NoiseOffset = DecoratorNumber(Graph, ModifierNode, TEXT("noise_offset"));
        }
        else
        {
            OutSkip = bNested && ModifierType == TEXT("minecraft:biome")
                ? FString(TEXT("biome in a nested placed feature"))
                : FString::Printf(TEXT("modifier %s"), *FString(ModifierType));
            return true;
        }
        Out.Add(Modifier);
    }
    return true;
}

bool FVoxelDecorator::CompilePlaced(const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value,
    FVoxelBlockTags& Tags, int32& OutPlaced, FString& OutSkip, FString* OutError)
{
    const int32 Node = Graph.GetNode(Value);
    if (Node == INDEX_NONE || Graph.Nodes[Node].Kind != EVoxelFeatureKind::PlacedFeature)
        return DecoratorFail(OutError, TEXT("not a placed feature"));
    if (const int32* Found = PlacedOfNode.Find(Node))
    {
        OutPlaced = *Found;
        return true;
    }
    const FVoxelFeatureValue* FeatureValue = Graph.FindField(Node, TEXT("feature"));
    const int32 FeatureNode = FeatureValue ? Graph.GetNode(*FeatureValue) : INDEX_NONE;
    if (FeatureNode == INDEX_NONE) return DecoratorFail(OutError, TEXT("no feature"));
    FPlaced Entry;
    if (!CompilePlant(Graph, FeatureNode, Tags, Entry.Plant, OutSkip, OutError)) return false;
    if (!OutSkip.IsEmpty()) return true;
    if (!CompileModifiers(Graph, Node, Tags, true, Entry.Modifiers, OutSkip, OutError)) return false;
    if (!OutSkip.IsEmpty()) return true;
    OutPlaced = Placed.Add(MoveTemp(Entry));
    PlacedOfNode.Add(Node, OutPlaced);
    return true;
}

bool FVoxelDecorator::CompilePlant(const FVoxelFeatureGraph& Graph, int32 Node, FVoxelBlockTags& Tags, int32& OutPlant,
    FString& OutSkip, FString* OutError)
{
    if (const int32* Found = PlantOfNode.Find(Node))
    {
        OutPlant = *Found;
        return true;
    }
    const FStringView Type = Graph.GetType(Node);
    FVoxelPlantFeature Plant;
    const auto Provider = [&](const TCHAR* Key, int32& Out) -> bool
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        if (!Value) return DecoratorFail(OutError, FString::Printf(TEXT("%s without %s"), *FString(Type), Key));
        if (!Providers.Compile(Graph, *Value, Tags, Predicates, Out, OutSkip, OutError)) return false;
        if (!OutSkip.IsEmpty()) return true;
        // Two-cell plants place their upper half with their lower.
        static const FName Half(TEXT("half")), Lower(TEXT("lower")), Upper(TEXT("upper"));
        TArray<uint16> States;
        Providers.GetStates(Out, States);
        for (const uint16 State : States)
            if (Table->GetValue(State, Half) == Lower)
                if (const int32 UpperState = Table->WithValue(State, Half, Upper); UpperState != INDEX_NONE)
                    UpperOf.Add(State, uint16(UpperState));
        return true;
    };
    const auto Predicate = [&](const TCHAR* Key, int32& Out) -> bool
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        if (!Value) return DecoratorFail(OutError, FString::Printf(TEXT("%s without %s"), *FString(Type), Key));
        return Predicates.Compile(Graph, Graph.GetNode(*Value), Tags, Out, OutSkip, OutError);
    };
    const auto Int = [&](const TCHAR* Key, FVoxelIntProvider& Out) -> bool
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        FString Error;
        if (Value && VoxelNumberProviders::CompileInt(Graph, *Value, Out, &Error)) return true;
        OutSkip = Value ? Error : FString::Printf(TEXT("%s without %s"), *FString(Type), Key);
        return true;
    };
    // With trees on, a selector entry of a type S9d builds places nothing (counted once the selector compiles); so does
    // a tree whose trunk, foliage or root placer is not built (a fit not solved yet), so the selector's other trees
    // still place.
    TArray<FString> Stubs;
    const auto TakeStub = [&](int32& Index) -> bool
    {
        static const TCHAR* const LaterTypes[] = {
            TEXT("minecraft:fallen_tree"), TEXT("minecraft:huge_brown_mushroom"), TEXT("minecraft:huge_red_mushroom")};
        if (!Options.bTrees || OutSkip.IsEmpty()) return false;
        bool bStub = OutSkip.EndsWith(TEXT("_trunk_placer")) || OutSkip.EndsWith(TEXT("_foliage_placer")) ||
            OutSkip.EndsWith(TEXT("_root_placer"));
        for (const TCHAR* LaterType : LaterTypes)
            bStub |= OutSkip == LaterType;
        if (!bStub) return false;
        Stubs.Add(OutSkip);
        OutSkip.Reset();
        Index = INDEX_NONE;
        return true;
    };
    const auto PlacedList = [&](const FVoxelFeatureValue* List, const TCHAR* Inner) -> bool
    {
        for (int32 Item = 0; List && List->Type == EVoxelFeatureValueType::List && Item < List->Count; ++Item)
        {
            const FVoxelFeatureValue& Entry = Graph.Items[List->Index + Item];
            const FVoxelFeatureValue* Value = Inner ? Graph.FindField(Graph.GetNode(Entry), Inner) : &Entry;
            int32 Index = INDEX_NONE;
            if (!Value)
                return DecoratorFail(OutError, FString::Printf(TEXT("%s entry without %s"), *FString(Type), Inner));
            if (!CompilePlaced(Graph, *Value, Tags, Index, OutSkip, OutError)) return false;
            if (!OutSkip.IsEmpty() && !TakeStub(Index)) return true;
            Plant.Selector.Placed.Add(Index);
            if (Inner)
            {
                const int32 EntryNode = Graph.GetNode(Entry);
                Plant.Selector.Chances.Add(float(DecoratorNumber(Graph, EntryNode, TEXT("chance"))));
                const int32 Weight = int32(DecoratorNumber(Graph, EntryNode, TEXT("weight")));
                Plant.Selector.Weights.Add(Weight);
                Plant.Selector.TotalWeight += Weight;
            }
        }
        return true;
    };

    if (Type == TEXT("minecraft:simple_block"))
    {
        Plant.Kind = EVoxelPlantFeature::SimpleBlock;
        if (!Provider(TEXT("to_place"), Plant.Provider)) return false;
    }
    else if (Type == TEXT("minecraft:block_column"))
    {
        Plant.Kind = EVoxelPlantFeature::BlockColumn;
        Plant.Direction = VoxelPredicateReading::ReadFace(DecoratorString(Graph, Node, TEXT("direction")));
        Plant.bPrioritizeTip = DecoratorNumber(Graph, Node, TEXT("prioritize_tip")) != 0;
        if (Plant.Direction == INDEX_NONE) return DecoratorFail(OutError, TEXT("block_column without a direction"));
        if (!Predicate(TEXT("allowed_placement"), Plant.Allowed)) return false;
        const FVoxelFeatureValue* Layers = Graph.FindField(Node, TEXT("layers"));
        for (int32 Item = 0; OutSkip.IsEmpty() && Layers && Item < Layers->Count; ++Item)
        {
            const int32 LayerNode = Graph.GetNode(Graph.Items[Layers->Index + Item]);
            const FVoxelFeatureValue* Height = Graph.FindField(LayerNode, TEXT("height"));
            const FVoxelFeatureValue* LayerProvider = Graph.FindField(LayerNode, TEXT("provider"));
            FVoxelBlockColumnLayer Layer;
            FString Error;
            if (!Height || !LayerProvider) return DecoratorFail(OutError, TEXT("block_column layer without height"));
            if (!VoxelNumberProviders::CompileInt(Graph, *Height, Layer.Height, &Error))
                OutSkip = Error;
            else if (!Providers.Compile(Graph, *LayerProvider, Tags, Predicates, Layer.Provider, OutSkip, OutError))
                return false;
            Plant.Layers.Add(Layer);
        }
    }
    else if (Type == TEXT("minecraft:disk"))
    {
        Plant.Kind = EVoxelPlantFeature::Disk;
        Plant.HalfHeight = int32(DecoratorNumber(Graph, Node, TEXT("half_height")));
        if (!Provider(TEXT("state_provider"), Plant.Provider) ||
            (OutSkip.IsEmpty() && !Predicate(TEXT("target"), Plant.Target)) ||
            (OutSkip.IsEmpty() && !Int(TEXT("radius"), Plant.Radius)))
            return false;
        Plant.TargetSet = OutSkip.IsEmpty() ? Predicates.GetPlainSet(Plant.Target) : INDEX_NONE;
    }
    else if (Type == TEXT("minecraft:vegetation_patch"))
    {
        Plant.Kind = EVoxelPlantFeature::VegetationPatch;
        if (DecoratorString(Graph, Node, TEXT("surface")) != TEXT("floor"))
        {
            OutSkip = TEXT("vegetation_patch on a ceiling (S11)");
            return true;
        }
        const FStringView Replaceable = DecoratorString(Graph, Node, TEXT("replaceable"));
        const FVoxelBlockSet* Set =
            Tags.Find(Replaceable.StartsWith(TEXT("#")) ? Replaceable.RightChop(1) : Replaceable, OutError);
        if (!Set) return DecoratorFail(OutError, TEXT("vegetation_patch without a replaceable tag"));
        Plant.ReplaceableSet = Predicates.AddSet(*Set);
        Plant.ExtraBottomChance = float(DecoratorNumber(Graph, Node, TEXT("extra_bottom_block_chance")));
        Plant.ExtraEdgeChance = float(DecoratorNumber(Graph, Node, TEXT("extra_edge_column_chance")));
        Plant.VegetationChance = float(DecoratorNumber(Graph, Node, TEXT("vegetation_chance")));
        Plant.VerticalRange = int32(DecoratorNumber(Graph, Node, TEXT("vertical_range")));
        const FVoxelFeatureValue* Vegetation = Graph.FindField(Node, TEXT("vegetation_feature"));
        if (!Vegetation) return DecoratorFail(OutError, TEXT("vegetation_patch without vegetation"));
        if (!Provider(TEXT("ground_state"), Plant.Provider) ||
            (OutSkip.IsEmpty() && !Int(TEXT("depth"), Plant.Depth)) ||
            (OutSkip.IsEmpty() && !Int(TEXT("xz_radius"), Plant.Radius)) ||
            (OutSkip.IsEmpty() && !CompilePlaced(Graph, *Vegetation, Tags, Plant.Vegetation, OutSkip, OutError)))
            return false;
    }
    else if (Type == TEXT("minecraft:multiface_growth"))
    {
        // Glow lichen (sculk vein is one of the later plants). Each face's state has that face alone set.
        Plant.Kind = EVoxelPlantFeature::Multiface;
        const FStringView Block = DecoratorString(Graph, Node, TEXT("block"));
        static const FName FaceNames[6] = {
            TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south"), TEXT("down"), TEXT("up")};
        static const FName True(TEXT("true")), Waterlogged(TEXT("waterlogged"));
        for (int32 Face = 0; Face < 6; ++Face)
        {
            const int32 Dry = Table->MakeState(Block, {{FaceNames[Face], True}});
            const int32 Wet = Dry == INDEX_NONE ? INDEX_NONE : Table->WithValue(uint16(Dry), Waterlogged, True);
            if (Wet == INDEX_NONE)
            {
                OutSkip = FString::Printf(TEXT("multiface_growth of %s"), *FString(Block));
                return true;
            }
            Plant.FaceStates[Face][0] = uint16(Dry);
            Plant.FaceStates[Face][1] = uint16(Wet);
        }
        const bool bWall = DecoratorNumber(Graph, Node, TEXT("can_place_on_wall")) != 0;
        Plant.Faces = uint8((bWall ? 0b1111 : 0) |
            (DecoratorNumber(Graph, Node, TEXT("can_place_on_floor")) != 0 ? 1 << int32(EVoxelFace::Down) : 0) |
            (DecoratorNumber(Graph, Node, TEXT("can_place_on_ceiling")) != 0 ? 1 << int32(EVoxelFace::Up) : 0));
        FVoxelBlockSet Base;
        if (!VoxelPredicateReading::ReadBlockSet(
                Graph, Graph.FindField(Node, TEXT("can_be_placed_on")), Tags, Base, OutError))
            return false;
        Plant.BaseSet = Predicates.AddSet(MoveTemp(Base));
        Plant.SpreadChance = float(DecoratorNumber(Graph, Node, TEXT("chance_of_spreading"), 0.5));
    }
    else if (Type == TEXT("minecraft:tree") && Options.bTrees)
    {
        Plant.Kind = EVoxelPlantFeature::Tree;
        FVoxelTreeConfig Tree;
        if (!VoxelTreeFeature::Compile(Graph, Node, Tags, *Table, Predicates, Providers, Tree, OutSkip, OutError))
            return false;
        if (!OutSkip.IsEmpty()) return true;
        for (const FString& Decorator : Tree.SkippedDecorators)
            ++SkippedDecorators.FindOrAdd(Decorator);
        Plant.Tree = Trees.Add(MoveTemp(Tree));
    }
    else if (Type == TEXT("minecraft:random_selector"))
    {
        Plant.Selector.Op = EVoxelSelectorOp::Random;
        const FVoxelFeatureValue* Default = Graph.FindField(Node, TEXT("default"));
        if (!Default) return DecoratorFail(OutError, TEXT("random_selector without a default"));
        if (!PlacedList(Graph.FindField(Node, TEXT("features")), TEXT("feature")) ||
            (OutSkip.IsEmpty() && !CompilePlaced(Graph, *Default, Tags, Plant.Selector.Default, OutSkip, OutError)))
            return false;
        TakeStub(Plant.Selector.Default);
    }
    else if (Type == TEXT("minecraft:simple_random_selector") || Type == TEXT("minecraft:sequence"))
    {
        Plant.Selector.Op =
            Type == TEXT("minecraft:sequence") ? EVoxelSelectorOp::Sequence : EVoxelSelectorOp::SimpleRandom;
        if (!PlacedList(Graph.FindField(Node, TEXT("features")), nullptr)) return false;
    }
    else if (Type == TEXT("minecraft:weighted_random_selector"))
    {
        Plant.Selector.Op = EVoxelSelectorOp::WeightedRandom;
        if (!PlacedList(Graph.FindField(Node, TEXT("features")), TEXT("data"))) return false;
        if (OutSkip.IsEmpty() && Plant.Selector.TotalWeight <= 0)
            return DecoratorFail(OutError, TEXT("weighted_random_selector without weights"));
    }
    else if (Type == TEXT("minecraft:random_boolean_selector"))
    {
        Plant.Selector.Op = EVoxelSelectorOp::RandomBoolean;
        for (const TCHAR* Key : {TEXT("feature_true"), TEXT("feature_false")})
        {
            const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
            int32 Index = INDEX_NONE;
            if (!Value) return DecoratorFail(OutError, TEXT("random_boolean_selector without both features"));
            if (OutSkip.IsEmpty() && !CompilePlaced(Graph, *Value, Tags, Index, OutSkip, OutError)) return false;
            Plant.Selector.Placed.Add(Index);
        }
    }
    else if (Type == TEXT("minecraft:no_op"))
        Plant.Selector.Op = EVoxelSelectorOp::NoOp;
    else
    {
        OutSkip = FString(Type);
        return true;
    }
    if (!OutSkip.IsEmpty()) return true;
    if (!Stubs.IsEmpty())
    {
        // A selector of nothing but S9b's entries stays skipped, as before S9a.
        bool bAnyBuilt = Plant.Selector.Default != INDEX_NONE;
        for (const int32 Entry : Plant.Selector.Placed)
            bAnyBuilt |= Entry != INDEX_NONE;
        if (!bAnyBuilt)
        {
            OutSkip = Stubs[0];
            return true;
        }
        for (const FString& Stub : Stubs)
            ++Placeholders.FindOrAdd(Stub);
    }
    OutPlant = Plants.Add(MoveTemp(Plant));
    PlantOfNode.Add(Node, OutPlant);
    return true;
}

bool FVoxelDecorator::IsAboveEverything(
    const FOre& Ore, FIntPoint Column, FIntVector Position, const FVoxelDecorationContext& Context) const
{
    // A blob wholly above the highest block of every column it overlaps, or outside the world, replaces nothing.
    const int32 Low = Position.Z - Ore.Reach.Down;
    int32 Top = VoxelConstants::MinZ - 1;
    if (Position.Z + Ore.Reach.Up >= VoxelConstants::MinZ)
        for (int32 DY = FloorColumn(Position.Y - Ore.Reach.Horizontal) - Column.Y;
            DY <= FloorColumn(Position.Y + Ore.Reach.Horizontal) - Column.Y; ++DY)
            for (int32 DX = FloorColumn(Position.X - Ore.Reach.Horizontal) - Column.X;
                DX <= FloorColumn(Position.X + Ore.Reach.Horizontal) - Column.X; ++DX)
                if (FMath::Abs(DX) <= 1 && FMath::Abs(DY) <= 1)
                    Top = FMath::Max(Top, Context.GetTopZ(FVoxelWriteLog::GetBucket(DX, DY)));
    return Low > Top;
}

template <typename FPrune, typename FVisit>
void FVoxelDecorator::RunModifiers(TConstArrayView<FModifier> Modifiers, bool bPruneAtBiome, int32 FeatureIndex,
    int32 Modifier, FIntVector Position, uint64 Seed, const FVoxelDecorationContext& Context, FPrune&& Prune,
    FVisit&& Visit) const
{
    if (Modifier == Modifiers.Num())
    {
        Visit(Position, Seed);
        return;
    }
    const FModifier& Step = Modifiers[Modifier];
    FVoxelXoroshiro Random(Seed);
    // Each try of a count takes its own stream (TrySalt), as S2's count does.
    const auto FanOut = [&](int32 Count)
    {
        for (int32 Try = 0; Try < Count; ++Try)
            RunModifiers(Modifiers, bPruneAtBiome, FeatureIndex, Modifier + 1, Position,
                VoxelRandom::Hash64(Seed, TrySalt, Try, 0), Context, Prune, Visit);
    };
    switch (Step.Type)
    {
    case FModifier::EType::Count:
        FanOut(Step.Provider.Sample(Random));
        return;
    case FModifier::EType::NoiseThresholdCount:
        // 26.3: the pattern noise at (x / 200, z / 200); below the level gives the below count.
        FanOut(CountNoise.Sample(Position.X / 200.0, Position.Y / 200.0) < Step.Level ? Step.Below : Step.Above);
        return;
    case FModifier::EType::NoiseBasedCount:
        FanOut(int32(FMath::CeilToDouble(
            (CountNoise.Sample(Position.X / Step.Factor, Position.Y / Step.Factor) + Step.NoiseOffset) * Step.Ratio)));
        return;
    case FModifier::EType::Heightmap:
        // Vanilla's heightmap value: one above the top block; an empty column gives nothing.
        Position.Z = Context.GetHeight(Step.Map, Position.X, Position.Y) + 1;
        if (Position.Z <= VoxelConstants::MinZ) return;
        break;
    case FModifier::EType::SurfaceRelative:
    {
        const int32 Relative = Position.Z - (Context.GetHeight(Step.Map, Position.X, Position.Y) + 1);
        if (Relative < Step.Below || Relative > Step.Above) return;
        break;
    }
    case FModifier::EType::SurfaceWaterDepth:
        if (Context.GetHeight(EVoxelDecorationHeightmap::WorldSurface, Position.X, Position.Y) -
                Context.GetHeight(EVoxelDecorationHeightmap::OceanFloor, Position.X, Position.Y) >
            Step.Above)
            return;
        break;
    case FModifier::EType::EnvironmentScan:
    {
        // The start cell is tested first, then each step moves one cell. A cell walked onto that is not the target
        // must meet the allowed condition, when there is one, for the scan to go on: the wiki ends the scan on such a
        // cell only when no target is found there, so the target itself need not meet it. Ours where the wiki is
        // silent: the start cell is not held to the allowed condition.
        for (int32 Steps = 0; !Predicates.Test(Step.Predicate, Context, Position); ++Steps)
        {
            if (Steps > 0 && Step.ScanAllowed != INDEX_NONE && !Predicates.Test(Step.ScanAllowed, Context, Position))
                return;
            Position.Z += Step.ScanStep;
            if (Steps >= Step.ScanSteps || Position.Z < VoxelConstants::MinZ || Position.Z > VoxelConstants::MaxZ)
                return;
        }
        break;
    }
    case FModifier::EType::Offset:
        Position.X += Step.Provider.Sample(Random);
        Position.Z += Step.OffsetZ.Sample(Random);
        Position.Y += Step.OffsetY.Sample(Random);
        break;
    case FModifier::EType::Filter:
        if (!Predicates.Test(Step.Predicate, Context, Position)) return;
        break;
    case FModifier::EType::InSquare:
        Position.X += Random.NextInt(VoxelConstants::ColumnSize);
        Position.Y += Random.NextInt(VoxelConstants::ColumnSize);
        break;
    case FModifier::EType::HeightRange:
        Position.Z = Step.Provider.Sample(Random);
        break;
    case FModifier::EType::RarityFilter:
        if (Random.NextInt(Step.Chance) != 0) return;
        break;
    case FModifier::EType::Biome:
    {
        // The position's own biome must list the feature (the column's 3x3 only decided that it runs).
        if (bPruneAtBiome && Prune(Position)) return;
        const int32 Biome = Context.GetBiome(Position.X, Position.Y, Position.Z);
        if (!FeaturesOfBiome.IsValidIndex(Biome) || !FeaturesOfBiome[Biome][FeatureIndex]) return;
        RunModifiers(Modifiers, bPruneAtBiome, FeatureIndex, Modifier + 1, Position, Seed, Context, Prune, Visit);
        return;
    }
    }
    RunModifiers(Modifiers, bPruneAtBiome, FeatureIndex, Modifier + 1, Position, Random.Next(), Context, Prune, Visit);
}

void FVoxelDecorator::RunPlaced(
    int32 Index, FIntVector Position, uint64 Seed, const FVoxelPlantEnvironment& Env, FVoxelDecorateStats& Stats) const
{
    const FPlaced& Entry = Placed[Index];
    RunModifiers(
        Entry.Modifiers, false, INDEX_NONE, 0, Position, Seed, Env.Context, [](FIntVector) { return false; },
        [&](FIntVector At, uint64 TrySeed) { PlacePlant(Entry.Plant, At, TrySeed, Env, Stats); });
}

void FVoxelDecorator::PlacePlant(
    int32 Index, FIntVector Position, uint64 Seed, const FVoxelPlantEnvironment& Env, FVoxelDecorateStats& Stats) const
{
    const FVoxelPlantFeature& Plant = Plants[Index];
    FVoxelXoroshiro Random(Seed);
    switch (Plant.Kind)
    {
    case EVoxelPlantFeature::SimpleBlock:
        ++Stats.PlantTries;
        VoxelPatchFeatures::PlaceSimpleBlock(Plant, Position, Random, Env);
        return;
    case EVoxelPlantFeature::BlockColumn:
        ++Stats.PlantTries;
        VoxelPatchFeatures::PlaceBlockColumn(Plant, Position, Random, Env);
        return;
    case EVoxelPlantFeature::Disk:
        ++Stats.PlantTries;
        VoxelPatchFeatures::PlaceDisk(Plant, Position, Random, Env);
        return;
    case EVoxelPlantFeature::Multiface:
        ++Stats.PlantTries;
        VoxelPatchFeatures::PlaceMultiface(Plant, Position, Random, Env);
        return;
    case EVoxelPlantFeature::Tree:
        ++Stats.PlantTries;
        VoxelTreeFeature::Place(Trees[Plant.Tree], Position, Random, Env, &Stats.TreeTimes);
        return;
    case EVoxelPlantFeature::VegetationPatch:
    {
        ++Stats.PlantTries;
        TArray<FIntVector> Vegetation;
        VoxelPatchFeatures::PlaceVegetationPatch(Plant, Position, Random, Env, Vegetation);
        // The vegetation goes on once the whole ground is down, each spot with its own stream.
        for (const FIntVector& Spot : Vegetation)
            RunPlaced(Plant.Vegetation, Spot, Random.Next(), Env, Stats);
        return;
    }
    case EVoxelPlantFeature::Selector:
        if (Plant.Selector.Op == EVoxelSelectorOp::Sequence)
        {
            for (const int32 Entry : Plant.Selector.Placed)
                if (Entry != INDEX_NONE) RunPlaced(Entry, Position, Random.Next(), Env, Stats);
            return;
        }
        if (const int32 Entry = VoxelSelectorFeatures::Pick(Plant.Selector, Random); Entry != INDEX_NONE)
            RunPlaced(Entry, Position, Random.Next(), Env, Stats);
        return;
    }
}

void FVoxelDecorator::Decorate(FIntPoint Column, TConstArrayView<const FVoxelColumn*> Snapshots, FVoxelWriteLog& Out,
    FVoxelDecorateStats* Stats) const
{
    check(IsInitialized() && Snapshots.Num() == 9);
    // Only plants read the live heightmaps.
    FVoxelDecorationContext Context(Column, Snapshots, Options.bPlants ? Table : nullptr);
    if (Options.Mutation == EVoxelDecorationMutation::FrozenHeightmaps) Context.FreezeHeightsForTest();
    for (FVoxelWriteBucket& Bucket : Out.Buckets)
        Bucket = FVoxelWriteBucket();
    Out.Source = Column;
    Out.Dropped = 0;
    FVoxelDecorateStats Local;

    // A column runs the features any biome of its 3x3 lists; the biome modifier then checks each position's own.
    // Each column's own quarts (its storage also holds the next column's first row and line, left out here).
    uint64 Seen[4] = {};
    for (const FVoxelColumn* Snapshot : Snapshots)
    {
        const TConstArrayView<uint8> Quarts = Snapshot->GetBiomes().GetQuarts();
        if (Quarts.IsEmpty()) continue;
        constexpr int32 Width = FVoxelBiomeStorage::Width;
        for (int32 Layer = 0; Layer < FVoxelBiomeStorage::Layers; ++Layer)
            for (int32 Y = 0; Y < Width - 1; ++Y)
            {
                const uint8* Row = Quarts.GetData() + FVoxelBiomeStorage::GetIndex(0, Y, Layer);
                for (int32 X = 0; X < Width - 1; ++X)
                    Seen[Row[X] >> 6] |= uint64(1) << (Row[X] & 63);
            }
    }
    TBitArray<> Active(false, Features.Num());
    for (int32 Biome = 0; Biome < FMath::Min(FeaturesOfBiome.Num(), 256); ++Biome)
        if (Seen[Biome >> 6] >> (Biome & 63) & 1)
            Active.CombineWithBitwiseOR(FeaturesOfBiome[Biome], EBitwiseOperatorFlags::MaxSize);

    FVoxelXoroshiro Shared(VoxelRandom::Hash64(WorldSeed, VoxelFeatureSeeds::DecorationSalt, Column.X, Column.Y));
    TArray<FIntVector> Scratch;
    // Plants (S8): each write goes to its target column's bucket under the running feature's key, and into the
    // working copy so later tries and features see it. A write outside the 3x3 is dropped and counted; one outside
    // the world is never made.
    uint16 CurrentKey = 0;
    const auto EmitPlant = [&](const FVoxelPlantWrite& Write)
    {
        const int32 DX = FloorColumn(Write.Cell.X) - Column.X, DY = FloorColumn(Write.Cell.Y) - Column.Y;
        if (Write.Cell.Z < VoxelConstants::MinZ || Write.Cell.Z > VoxelConstants::MaxZ) return;
        if (FMath::Abs(DX) > 1 || FMath::Abs(DY) > 1)
        {
            ++Out.Dropped;
            return;
        }
        Out.Buckets[FVoxelWriteLog::GetBucket(DX, DY)].Add(CurrentKey,
            FVoxelWrite::Make(Write.Cell.X & 15, Write.Cell.Y & 15, Write.Cell.Z, Write.State, Write.Mode,
                Write.Argument, Write.Support));
        Context.SetState(Write.Cell.X, Write.Cell.Y, Write.Cell.Z, Write.State);
        ++Local.Placed;
        ++Local.PlantBlocks;
    };
    const FVoxelPlantEnvironment PlantEnv{*Table, Survival, Predicates, Providers, Context, UpperOf, EmitPlant};
    const FIntVector Start(
        Column.X * VoxelConstants::ColumnSize, Column.Y * VoxelConstants::ColumnSize, VoxelConstants::MinZ);
    for (int32 FeatureIndex = 0; FeatureIndex < Features.Num(); ++FeatureIndex)
    {
        if (!Active[FeatureIndex]) continue;
        const FFeature& Feature = Features[FeatureIndex];
        ++Local.Features;
        const uint64 Seed = Options.Mutation == EVoxelDecorationMutation::SharedStream
            ? Shared.Next()
            : VoxelFeatureSeeds::GetStreamSeed(WorldSeed, Feature.Salt, Column.X, Column.Y, Feature.Step);
        // Scout E's early-out, where the try's height is known: at the biome modifier, before its lookup, or at the
        // feature for placements without one.
        const FOre* PruneOre = Feature.Ore != INDEX_NONE && Options.bEarlyOut && !Ores[Feature.Ore].bTargetsAir
            ? &Ores[Feature.Ore]
            : nullptr;
        const auto Prune = [&](FIntVector Position)
        {
            if (!PruneOre) return false;
            ++Local.Tries;
            if (!IsAboveEverything(*PruneOre, Column, Position, Context)) return false;
            ++Local.Skipped;
            return true;
        };
        CurrentKey = Feature.Key;
        RunModifiers(Feature.Modifiers, Feature.bPruneAtBiome, FeatureIndex, 0, Start, Seed, Context, Prune,
            [&](FIntVector Position, uint64 TrySeed)
            {
                if (Feature.Plant != INDEX_NONE)
                {
                    PlacePlant(Feature.Plant, Position, TrySeed, PlantEnv, Local);
                    return;
                }
                if (Feature.Ore == INDEX_NONE) return;
                const FOre& Ore = Ores[Feature.Ore];
                if (!Feature.bPruneAtBiome && Prune(Position)) return;
                if (!PruneOre) ++Local.Tries;
                FVoxelXoroshiro Random(TrySeed);
                Out.Dropped += VoxelOreFeature::Place(Ore.Config, TargetLists[Ore.Config.Targets], Position, Random,
                    Context, *Table, Options.bDiscard, Scratch,
                    [&](int32 X, int32 Y, int32 Z, uint16 State)
                    {
                        const int32 Bucket =
                            FVoxelWriteLog::GetBucket(FloorColumn(X) - Column.X, FloorColumn(Y) - Column.Y);
                        Out.Buckets[Bucket].Add(Feature.Key,
                            FVoxelWrite::Make(
                                X & 15, Y & 15, Z, State, EVoxelWriteMode::IfTargets, uint16(Ore.Config.Targets)));
                        ++Local.Placed;
                    });
            });
    }

    if (Stats)
    {
        Local.Writes = Out.GetWriteCount();
        Local.Dropped = Out.Dropped;
        Local.WorkingCells = Context.GetWrittenCells();
        Local.WorkingBytes = Context.GetAllocatedSize();
        Local.LogBytes = Out.GetAllocatedSize();
        *Stats = Local;
    }
}

void FVoxelDecorator::Finalize(const FVoxelColumn& Snapshot, TConstArrayView<const FVoxelWriteBucket*> Incoming,
    FVoxelColumn& Out, FVoxelFinalizeStats* Stats, TConstArrayView<int32> ArrivalOrder) const
{
    check(IsInitialized());
    Out = Snapshot;
    FVoxelFinalizeStats Local;
    // Sections a write reaches are expanded once, written in place, then packed again.
    TArray<uint16> Dense[VoxelConstants::SectionCount];
    TBitArray<> HeightCells(false, FVoxelColumn::Area);
    const auto CellAt = [&](int32 Index) -> uint16&
    {
        TArray<uint16>& States = Dense[Index >> 12];
        if (States.IsEmpty())
        {
            States.SetNumUninitialized(FVoxelSection::Volume);
            Out.GetSection(Index >> 12).Expand(States);
        }
        return States.GetData()[Index & (FVoxelSection::Volume - 1)];
    };
    const auto SetCell = [&](int32 Index, uint16 State)
    {
        uint16& Cell = CellAt(Index);
        if (GetHeightClass(*Table, State) != GetHeightClass(*Table, Cell)) HeightCells[Index & 255] = true;
        Cell = State;
    };
    // A two-cell plant's lower half is set at once, then undone if its upper half (the next write) is refused.
    int32 PairIndex = INDEX_NONE;
    uint16 PairOld = 0;
    bool bRefuseNext = false;
    const auto Apply = [&](const FVoxelWrite& Write)
    {
        ++Local.Writes;
        const int32 Index = Write.GetDenseIndex();
        const uint16 Cell = CellAt(Index);
        uint16 State = Cell;
        switch (Write.GetMode())
        {
        case EVoxelWriteMode::Force:
            State = Write.State;
            break;
        case EVoxelWriteMode::IfTargets:
        {
            const int32 Block = Table->Get(Cell).Block;
            for (const FVoxelOreTarget& Target : TargetLists[Write.Argument])
                if (Target.Rule.Test(Block, Write.GetZ()))
                {
                    State = Target.State;
                    break;
                }
            break;
        }
        case EVoxelWriteMode::IfReplaceable:
            // A disk's target set (S8; trees bring their tag in S9).
            if (Predicates.GetSet(Write.Argument).Contains(Table->Get(Cell).Block)) State = Write.State;
            break;
        case EVoxelWriteMode::IfUnchanged:
            if (Cell == Write.Argument) State = Write.State;
            break;
        default:
            break;
        }
        // Plants: the support below, as merged so far (the cell below lies in this column).
        const EVoxelWriteSupport Support = Write.GetSupport();
        if (State != Cell && Support != EVoxelWriteSupport::None)
        {
            const uint16 Below = Write.GetZ() > VoxelConstants::MinZ ? CellAt(Index - FVoxelColumn::Area) : 0;
            if (!Survival.SurvivesOn(Write.State, Below, Cell)) State = Cell;
        }
        const bool bPlant = Write.GetMode() == EVoxelWriteMode::IfUnchanged ||
            Write.GetMode() == EVoxelWriteMode::IfReplaceable || Support != EVoxelWriteSupport::None;
        if (bRefuseNext)
        {
            // The upper half of a refused lower half.
            bRefuseNext = false;
            ++Local.Refused;
            return;
        }
        if (PairIndex != INDEX_NONE)
        {
            const int32 Lower = PairIndex;
            PairIndex = INDEX_NONE;
            if (State == Cell)
            {
                SetCell(Lower, PairOld);
                --Local.Applied;
                Local.Refused += 2;
                return;
            }
        }
        if (State == Cell)
        {
            if (bPlant && Write.State != Cell) ++Local.Refused;
            bRefuseNext = Support == EVoxelWriteSupport::PlantPair;
            return;
        }
        if (Support == EVoxelWriteSupport::PlantPair)
        {
            PairIndex = Index;
            PairOld = Cell;
        }
        ++Local.Applied;
        SetCell(Index, State);
    };
    if (ArrivalOrder.IsEmpty())
        VoxelWriteMerge::Merge(Incoming, Apply);
    else
        for (const int32 Source : ArrivalOrder)
            if (Incoming.IsValidIndex(Source) && Incoming[Source])
                for (const FVoxelWrite& Write : Incoming[Source]->Writes)
                    Apply(Write);
    if (PairIndex != INDEX_NONE)
    {
        // A lower half whose upper half never came (not made by S8's features): undone.
        SetCell(PairIndex, PairOld);
        --Local.Applied;
    }
    for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
        if (!Dense[Section].IsEmpty())
        {
            Out.SetSectionStates(Section, Dense[Section]);
            ++Local.Sections;
        }
    for (TConstSetBitIterator<> Cell(HeightCells); Cell; ++Cell)
    {
        Out.UpdateHeights(Cell.GetIndex() & 15, Cell.GetIndex() >> 4, *Table);
        ++Local.HeightCells;
    }
    if (Stats) *Stats = Local;
}

void FVoxelDecorator::GenerateSnapshot(
    const IVoxelTerrain& Terrain, FIntPoint Column, FVoxelColumn& Out, TArray<uint16>& Dense) const
{
    if (Options.bFreezeLast)
        Terrain.GenerateSnapshot(Column, Out, Dense);
    else
        Terrain.Generate(Column, Out, Dense);
}

void FVoxelDecorator::FinishColumn(const IVoxelTerrain& Terrain, FVoxelColumn& Column) const
{
    if (Options.bFreezeLast) Terrain.FinishColumn(Column);
}

void FVoxelDecorator::DecorateAlone(const IVoxelTerrain& Terrain, FIntPoint Column, FVoxelColumn& Out) const
{
    TUniquePtr<FVoxelColumn> Snapshots[5][5];
    TArray<uint16> Dense;
    for (int32 DY = -2; DY <= 2; ++DY)
        for (int32 DX = -2; DX <= 2; ++DX)
        {
            Snapshots[DY + 2][DX + 2] = MakeUnique<FVoxelColumn>();
            GenerateSnapshot(Terrain, Column + FIntPoint(DX, DY), *Snapshots[DY + 2][DX + 2], Dense);
        }
    FVoxelWriteLog Logs[9];
    const FVoxelWriteBucket* Incoming[9] = {};
    for (int32 DY = -1; DY <= 1; ++DY)
        for (int32 DX = -1; DX <= 1; ++DX)
        {
            const FVoxelColumn* Around[9];
            for (int32 NY = -1; NY <= 1; ++NY)
                for (int32 NX = -1; NX <= 1; ++NX)
                    Around[FVoxelWriteLog::GetBucket(NX, NY)] = Snapshots[DY + NY + 2][DX + NX + 2].Get();
            FVoxelWriteLog& Log = Logs[FVoxelWriteLog::GetBucket(DX, DY)];
            Decorate(Column + FIntPoint(DX, DY), Around, Log);
            Incoming[FVoxelWriteLog::GetBucket(DX, DY)] = &Log.Buckets[FVoxelWriteLog::GetBucket(-DX, -DY)];
        }
    Finalize(*Snapshots[2][2], Incoming, Out);
    FinishColumn(Terrain, Out);
}

FString FVoxelDecorator::Describe() const
{
    TArray<int32> PerStep;
    PerStep.SetNumZeroed(FVoxelFeatureGraph::StepCount);
    for (const FFeature& Feature : Features)
        ++PerStep[Feature.Step];
    FString Steps;
    for (int32 Step = 0; Step < PerStep.Num(); ++Step)
        if (PerStep[Step])
            Steps += FString::Printf(TEXT("%s%s %d"), Steps.IsEmpty() ? TEXT("") : TEXT(", "),
                FVoxelFeatureGraph::GetStepName(Step), PerStep[Step]);
    int32 SkippedCount = 0;
    for (const TPair<FString, int32>& Pair : Skipped)
        SkippedCount += Pair.Value;
    int32 PlantCount = 0;
    for (const FFeature& Feature : Features)
        PlantCount += Feature.Plant != INDEX_NONE;
    FString Out = FString::Printf(TEXT("Decoration: %d placed features compiled (%d ores, %d plants; %s), %d skipped "
                                       "(types not built yet)"),
        Features.Num(), Ores.Num(), PlantCount, *Steps, SkippedCount);
    if (Options.bPlants)
    {
        // Why, most first: the first reached type or modifier not built.
        TArray<TPair<FString, int32>> Reasons = Skipped.Array();
        Reasons.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
            { return A.Value != B.Value ? A.Value > B.Value : A.Key < B.Key; });
        FString List;
        for (const TPair<FString, int32>& Reason : Reasons)
            List += FString::Printf(TEXT("%s%s %d"), List.IsEmpty() ? TEXT("") : TEXT(", "), *Reason.Key, Reason.Value);
        Out += FString::Printf(TEXT(" [%s]"), *List);
    }
    if (Options.bPlants && Options.bTrees)
    {
        const auto Join = [](const TMap<FString, int32>& Counts)
        {
            TArray<TPair<FString, int32>> Sorted = Counts.Array();
            Sorted.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
                { return A.Value != B.Value ? A.Value > B.Value : A.Key < B.Key; });
            FString List;
            for (const TPair<FString, int32>& Pair : Sorted)
                List += FString::Printf(TEXT("%s%s %d"), List.IsEmpty() ? TEXT("") : TEXT(", "), *Pair.Key, Pair.Value);
            return List;
        };
        Out += FString::Printf(TEXT("; trees (S9a): %d configurations; S9b's selector entries placing nothing [%s]; "
                                    "tree decorators left out [%s]"),
            Trees.Num(), *Join(Placeholders), *Join(SkippedDecorators));
    }
    if (!MissingBlocks.IsEmpty())
        Out += FString::Printf(
            TEXT("; %d block ids not in the table (ores: their targets left out)"), MissingBlocks.Num());
    return Out;
}

TArray<TPair<uint16, FString>> FVoxelDecorator::GetOreStates() const
{
    TArray<TPair<uint16, FString>> States;
    for (const TArray<FVoxelOreTarget>& Targets : TargetLists)
        for (const FVoxelOreTarget& Target : Targets)
            States.AddUnique({Target.State, Target.Id});
    return States;
}

TArray<FVoxelDecorator::FFeatureInfo> FVoxelDecorator::GetFeatureInfos() const
{
    TArray<FFeatureInfo> Infos;
    for (const FFeature& Feature : Features)
        Infos.Add({Feature.Id, Feature.Step, Feature.IndexInStep, Feature.Key});
    return Infos;
}
