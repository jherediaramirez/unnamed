#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelFeatureOrder.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    FString FeatureTestData()
    {
        return FPaths::Combine(VoxelWorldgen::GetReferenceRoot(), TEXT("data/minecraft"));
    }

    TSharedPtr<FJsonValue> LoadFeatureTestJson(const FString& File)
    {
        FString Text;
        TArray<TSharedPtr<FJsonValue>> Values;
        if (!FFileHelper::LoadFileToString(Text, *File) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("[") + Text + TEXT("]")), Values) ||
            Values.Num() != 1)
        {
            return nullptr;
        }
        return Values[0];
    }

    FString FeatureTestJsonText(const TSharedPtr<FJsonValue>& Value)
    {
        FString Text;
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
        if (!Value.IsValid() || !FJsonSerializer::Serialize(Value, TEXT(""), Writer)) return TEXT("(none)");
        return Text;
    }

    // The files of a folder under data/minecraft, recursively, as paths relative to it without ".json".
    TArray<FString> FeatureTestFiles(const FString& Folder)
    {
        TArray<FString> Found;
        const FString Root = FPaths::Combine(FeatureTestData(), Folder);
        IFileManager::Get().FindFilesRecursive(Found, *Root, TEXT("*.json"), true, false);
        for (FString& File : Found)
        {
            FPaths::MakePathRelativeTo(File, *(Root + TEXT("/")));
            File.RemoveFromEnd(TEXT(".json"));
        }
        Found.Sort();
        return Found;
    }

    // Reads the reference with some files replaced (Text) or missing (an empty string).
    bool ReadFeaturesWith(const TMap<FString, FString>& Replaced, FVoxelFeatureGraph& Out, FString& OutError)
    {
        const FString Data = FeatureTestData();
        return VoxelWorldgen::ReadFeatures(
            [&Data, &Replaced](const FString& Path, FString& OutText)
            {
                if (const FString* Found = Replaced.Find(Path))
                {
                    OutText = *Found;
                    return !Found->IsEmpty();
                }
                return FFileHelper::LoadFileToString(OutText, *FPaths::Combine(Data, Path));
            },
            Out, OutError);
    }

    // A reference file with one change made to its JSON object, as text.
    FString ChangedFeatureFile(const FString& Path, TFunctionRef<void(FJsonObject&)> Change)
    {
        const TSharedPtr<FJsonValue> Json = LoadFeatureTestJson(FPaths::Combine(FeatureTestData(), Path));
        if (!Json.IsValid() || Json->Type != EJson::Object) return FString();
        Change(*Json->AsObject());
        return FeatureTestJsonText(Json);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFeaturesReadTest, "Unnamed.Voxel.Features.Read",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFeaturesReadTest::RunTest(const FString& Parameters)
{
    // The whole reachable set reads, with the reference's counts, and each biome's lists as its file gives them.
    FVoxelFeatureGraph Graph;
    FString Error;
    const double Start = FPlatformTime::Seconds();
    const bool bRead = VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error);
    const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000;
    if (!TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error), bRead)) return false;
    AddInfo(FString::Printf(TEXT("read in %.1f ms: %d nodes, %d fields, %d list items, %d strings"), Milliseconds,
        Graph.Nodes.Num(), Graph.Fields.Num(), Graph.Items.Num(), Graph.Strings.Num()));
    TestEqual(TEXT("placed features"), Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)].Num(), 210);
    TestEqual(TEXT("features"), Graph.Entries[int32(EVoxelFeatureRegistry::Feature)].Num(), 182);
    TestEqual(TEXT("block state providers"), Graph.Entries[int32(EVoxelFeatureRegistry::Provider)].Num(), 8);
    TestEqual(TEXT("carvers"), Graph.Entries[int32(EVoxelFeatureRegistry::Carver)].Num(), 3);
    TestEqual(TEXT("block tags"), Graph.Tags.Num(), FeatureTestFiles(TEXT("tags/block")).Num());
    TestEqual(TEXT("block tags in the reference"), Graph.Tags.Num(), 127);
    for (int32 Registry = 0; Registry < int32(EVoxelFeatureRegistry::Count); ++Registry)
    {
        const TCHAR* Folder = FVoxelFeatureGraph::GetRegistryFolder(EVoxelFeatureRegistry(Registry));
        for (const FString& File : FeatureTestFiles(FString(TEXT("worldgen/")) + Folder))
            TestTrue(FString::Printf(TEXT("%s/%s is read"), Folder, *File),
                Graph.FindEntry(EVoxelFeatureRegistry(Registry), TEXT("minecraft:") + File) != INDEX_NONE);
    }
    for (const TCHAR* Tag : VoxelWorldgen::GetCodeTags())
        TestTrue(FString::Printf(TEXT("code tag %s is read"), Tag),
            Graph.FindTag(FString(TEXT("minecraft:")) + Tag) != INDEX_NONE);
    TestEqual(TEXT("biomes"), Graph.Biomes.Num(), 56);
    int32 Differences = 0;
    for (const FVoxelBiomeDecoration& Biome : Graph.Biomes)
    {
        const TSharedPtr<FJsonValue> File = LoadFeatureTestJson(
            FPaths::Combine(FeatureTestData(), TEXT("worldgen/biome"), Biome.Biome.RightChop(10) + TEXT(".json")));
        const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Carvers = nullptr;
        if (!File.IsValid() || !File->AsObject()->TryGetArrayField(TEXT("features"), Steps) ||
            !File->AsObject()->TryGetArrayField(TEXT("carvers"), Carvers) || Steps->Num() != Biome.Steps.Num())
        {
            AddError(FString::Printf(TEXT("%s: cannot compare its lists"), *Biome.Biome));
            continue;
        }
        TArray<FString> Expected, Actual;
        for (int32 Step = 0; Step < Steps->Num(); ++Step)
        {
            for (const TSharedPtr<FJsonValue>& Id : (*Steps)[Step]->AsArray())
                Expected.Add(FString::Printf(TEXT("%d %s"), Step, *Id->AsString()));
            for (const int32 Entry : Biome.Steps[Step])
                Actual.Add(FString::Printf(
                    TEXT("%d %s"), Step, *Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)][Entry].Id));
        }
        for (const TSharedPtr<FJsonValue>& Id : *Carvers)
            Expected.Add(TEXT("carver ") + Id->AsString());
        for (const int32 Entry : Biome.Carvers)
            Actual.Add(TEXT("carver ") + Graph.Entries[int32(EVoxelFeatureRegistry::Carver)][Entry].Id);
        if (Expected != Actual)
        {
            ++Differences;
            AddError(FString::Printf(TEXT("%s: lists differ from its file"), *Biome.Biome));
        }
    }
    TestEqual(TEXT("biomes whose lists differ from their files"), Differences, 0);
    // Tags expand without a missing include.
    TArray<FString> Blocks;
    TestTrue(TEXT("replaceable_by_trees expands"),
        Graph.ExpandTag(Graph.FindTag(TEXT("minecraft:replaceable_by_trees")), Blocks, &Error) && Blocks.Num() > 10);
    // Validate refuses a node that holds itself, and a tag that includes itself through another.
    FVoxelFeatureGraph Broken;
    Broken.Fields.Add({Broken.AddString(TEXT("feature")),
        {EVoxelFeatureValueType::Node, EVoxelFeatureRegistry::PlacedFeature, 0, 0, 0}});
    Broken.Nodes.Add({EVoxelFeatureKind::PlacedFeature, INDEX_NONE, 0, 1});
    TestFalse(TEXT("a node holding itself is refused"), Broken.Validate());
    FVoxelFeatureGraph Cycle;
    const int32 First = Cycle.AddTag(TEXT("minecraft:a"));
    const int32 Second = Cycle.AddTag(TEXT("minecraft:b"));
    Cycle.Tags[First].Values = {TEXT("#minecraft:b")};
    Cycle.Tags[Second].Values = {TEXT("minecraft:stone"), TEXT("#minecraft:a")};
    TestFalse(TEXT("a tag including itself is refused"), Cycle.ExpandTag(First, Blocks, &Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFeaturesStrictTest, "Unnamed.Voxel.Features.Strict",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFeaturesStrictTest::RunTest(const FString& Parameters)
{
    // Each broken file fails the read with an error naming the file and the field.
    struct FCase
    {
        const TCHAR* Label;
        FString Path;
        FString Text;
        TArray<FString> Expected;
    };
    const FString Placed = TEXT("worldgen/placed_feature/ore_iron_upper.json");
    const FString Ore = TEXT("worldgen/feature/ore_iron.json");
    TArray<FCase> Cases;
    Cases.Add({TEXT("unknown field"), Placed,
        ChangedFeatureFile(Placed, [](FJsonObject& Object) { Object.SetNumberField(TEXT("bogus"), 1); }),
        {Placed, TEXT("bogus"), TEXT("unexpected field")}});
    Cases.Add({TEXT("unknown field deep in a list"), Placed,
        ChangedFeatureFile(Placed, [](FJsonObject& Object)
            { Object.GetArrayField(TEXT("placement"))[0]->AsObject()->SetBoolField(TEXT("extra"), true); }),
        {Placed, TEXT("placement[0].extra"), TEXT("unexpected field")}});
    Cases.Add({TEXT("unknown type"), Ore,
        ChangedFeatureFile(
            Ore, [](FJsonObject& Object) { Object.SetStringField(TEXT("type"), TEXT("minecraft:ore_blob")); }),
        {Ore, TEXT("unknown feature type \"minecraft:ore_blob\"")}});
    Cases.Add({TEXT("wrong kind of value"), Ore,
        ChangedFeatureFile(Ore, [](FJsonObject& Object) { Object.SetStringField(TEXT("size"), TEXT("9")); }),
        {Ore, TEXT("size"), TEXT("unexpected a string")}});
    Cases.Add({TEXT("an int that is not whole"), Ore,
        ChangedFeatureFile(Ore, [](FJsonObject& Object) { Object.SetNumberField(TEXT("size"), 9.5); }),
        {Ore, TEXT("size"), TEXT("expected an int")}});
    Cases.Add({TEXT("missing field"), Ore,
        ChangedFeatureFile(
            Ore, [](FJsonObject& Object) { Object.RemoveField(TEXT("discard_chance_on_air_exposure")); }),
        {Ore, TEXT("missing field \"discard_chance_on_air_exposure\"")}});
    Cases.Add({TEXT("wrong value in a nested provider"), Placed,
        ChangedFeatureFile(Placed, [](FJsonObject& Object)
            { Object.GetArrayField(TEXT("placement"))[0]->AsObject()->SetStringField(TEXT("count"), TEXT("x")); }),
        {Placed, TEXT("placement[0].count"), TEXT("unexpected a string")}});
    Cases.Add({TEXT("bad id"), Placed,
        ChangedFeatureFile(
            Placed, [](FJsonObject& Object) { Object.SetStringField(TEXT("feature"), TEXT("Minecraft:Ore")); }),
        {Placed, TEXT("feature"), TEXT("bad id")}});
    Cases.Add({TEXT("a feature with no file"), Placed,
        ChangedFeatureFile(Placed,
            [](FJsonObject& Object) { Object.SetStringField(TEXT("feature"), TEXT("minecraft:does_not_exist")); }),
        {TEXT("worldgen/feature/does_not_exist.json"), TEXT("cannot read the file")}});
    Cases.Add({TEXT("a missing tag file"), TEXT("tags/block/logs.json"), FString(),
        {TEXT("tags/block/logs.json"), TEXT("cannot read the file")}});
    Cases.Add({TEXT("a bad tag value"), TEXT("tags/block/logs.json"),
        TEXT("{\"values\": [\"#minecraft:oak_logs\", 5]}"), {TEXT("tags/block/logs.json"), TEXT("values[1]")}});
    Cases.Add({TEXT("a vertical anchor with two fields"), Placed,
        ChangedFeatureFile(Placed,
            [](FJsonObject& Object)
            {
                Object.GetArrayField(TEXT("placement"))[2]
                    ->AsObject()
                    ->GetObjectField(TEXT("height"))
                    ->GetObjectField(TEXT("max_inclusive"))
                    ->SetNumberField(TEXT("below_top"), 0);
            }),
        {Placed, TEXT("placement[2].height.max_inclusive"), TEXT("one field")}});
    // Types and words match only as written (the engine's own string comparisons ignore case).
    Cases.Add({TEXT("a type in another case"), Ore,
        ChangedFeatureFile(
            Ore, [](FJsonObject& Object) { Object.SetStringField(TEXT("type"), TEXT("minecraft:Ore")); }),
        {Ore, TEXT("unknown feature type \"minecraft:Ore\"")}});
    Cases.Add({TEXT("a word in another case"), Placed,
        ChangedFeatureFile(Placed,
            [](FJsonObject& Object)
            {
                const TSharedRef<FJsonObject> Heightmap = MakeShared<FJsonObject>();
                Heightmap->SetStringField(TEXT("type"), TEXT("minecraft:heightmap"));
                Heightmap->SetStringField(TEXT("heightmap"), TEXT("world_surface_wg"));
                TArray<TSharedPtr<FJsonValue>> Placement = Object.GetArrayField(TEXT("placement"));
                Placement[1] = MakeShared<FJsonValueObject>(Heightmap);
                Object.SetArrayField(TEXT("placement"), Placement);
            }),
        {Placed, TEXT("placement[1].heightmap"), TEXT("unexpected value \"world_surface_wg\"")}});
    for (const FCase& Case : Cases)
    {
        TMap<FString, FString> Replaced = {{Case.Path, Case.Text}};
        FVoxelFeatureGraph Graph;
        FString Error;
        if (!TestFalse(
                FString::Printf(TEXT("%s fails the read"), Case.Label), ReadFeaturesWith(Replaced, Graph, Error)))
            continue;
        for (const FString& Part : Case.Expected)
            TestTrue(FString::Printf(TEXT("%s: the error names \"%s\" (%s)"), Case.Label, *Part, *Error),
                Error.Contains(Part, ESearchCase::CaseSensitive));
    }
    // The unchanged files still read through the same path.
    FVoxelFeatureGraph Graph;
    FString Error;
    const bool bRead = ReadFeaturesWith({}, Graph, Error);
    TestTrue(FString::Printf(TEXT("the unchanged reference reads (%s)"), *Error), bRead);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFeaturesRoundTripTest, "Unnamed.Voxel.Features.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFeaturesRoundTripTest::RunTest(const FString& Parameters)
{
    // Every placed feature, feature, provider, carver and block tag, written back, equals its file as JSON.
    FVoxelFeatureGraph Graph;
    FString Error;
    const bool bRead = VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error);
    if (!TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error), bRead)) return false;
    int32 Compared = 0;
    int32 Differences = 0;
    const auto Compare = [this, &Compared, &Differences](const FString& File, const TSharedPtr<FJsonValue>& Written)
    {
        ++Compared;
        const TSharedPtr<FJsonValue> Expected = LoadFeatureTestJson(FPaths::Combine(FeatureTestData(), File));
        if (!Expected.IsValid() || !Written.IsValid() || !FJsonValue::CompareEqual(*Expected, *Written))
        {
            ++Differences;
            AddError(FString::Printf(TEXT("%s differs. File: %s Written: %s"), *File,
                *FeatureTestJsonText(Expected).Left(600), *FeatureTestJsonText(Written).Left(600)));
        }
    };
    for (int32 Registry = 0; Registry < int32(EVoxelFeatureRegistry::Count); ++Registry)
    {
        for (const FVoxelFeatureEntry& Entry : Graph.Entries[Registry])
            Compare(
                FString::Printf(TEXT("worldgen/%s/%s.json"),
                    FVoxelFeatureGraph::GetRegistryFolder(EVoxelFeatureRegistry(Registry)), *Entry.Id.RightChop(10)),
                VoxelWorldgen::WriteFeatureNode(Graph, Entry.Node));
    }
    for (int32 Tag = 0; Tag < Graph.Tags.Num(); ++Tag)
        Compare(FString::Printf(TEXT("tags/block/%s.json"), *Graph.Tags[Tag].Id.RightChop(10)),
            VoxelWorldgen::WriteBlockTag(Graph, Tag));
    TestEqual(TEXT("files compared"), Compared, 210 + 182 + 8 + 3 + 127);
    TestEqual(TEXT("files written back unchanged"), Differences, 0);
    AddInfo(FString::Printf(TEXT("%d files written back"), Compared));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFeaturesOrderTest, "Unnamed.Voxel.Features.Order",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFeaturesOrderTest::RunTest(const FString& Parameters)
{
    // Hand-made lists: the order keeps every list's order, takes the smallest id when several are free, and refuses
    // lists that contradict each other.
    {
        FVoxelFeatureGraph Graph;
        const auto Add = [&Graph](std::initializer_list<const TCHAR*> Ids)
        {
            FVoxelBiomeDecoration& Biome = Graph.Biomes.AddDefaulted_GetRef();
            Biome.Steps.SetNum(FVoxelFeatureGraph::StepCount);
            for (const TCHAR* Id : Ids)
                Biome.Steps[3].Add(Graph.AddEntry(EVoxelFeatureRegistry::PlacedFeature, Id));
        };
        Add({TEXT("m:c"), TEXT("m:e")});
        Add({TEXT("m:b"), TEXT("m:e"), TEXT("m:a")});
        Add({TEXT("m:d")});
        FVoxelFeatureOrder Order;
        FString Error;
        TestTrue(TEXT("hand-made lists sort"), Order.Build(Graph, &Error));
        TArray<FString> Ids;
        for (const int32 Entry : Order.Steps[3])
            Ids.Add(Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)][Entry].Id);
        TestEqual(TEXT("hand-made order"), FString::Join(Ids, TEXT(" ")), FString(TEXT("m:b m:c m:d m:e m:a")));
        TestEqual(TEXT("an entry's index in its step"),
            Order.IndexInStep[Graph.FindEntry(EVoxelFeatureRegistry::PlacedFeature, TEXT("m:e"))], 3);
        Add({TEXT("m:a"), TEXT("m:c")});
        TestFalse(TEXT("contradicting lists are refused"), Order.Build(Graph, &Error));
        TestTrue(FString::Printf(TEXT("the refusal names the step (%s)"), *Error),
            Error.Contains(TEXT("underground_structures")));
    }

    // The reference: every step sorts, every biome's list keeps its order, and of the features free to go next the
    // smallest id goes first.
    FVoxelFeatureGraph Graph;
    FString Error;
    const bool bRead = VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error);
    if (!TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error), bRead)) return false;
    FVoxelFeatureOrder Order;
    const bool bSorted = Order.Build(Graph, &Error);
    if (!TestTrue(FString::Printf(TEXT("the 11 steps sort (%s)"), *Error), bSorted)) return false;
    const TArray<FVoxelFeatureEntry>& Placed = Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)];
    TArray<FString> PerStep;
    int32 Listed = 0;
    for (int32 Step = 0; Step < FVoxelFeatureGraph::StepCount; ++Step)
    {
        PerStep.Add(FString::FromInt(Order.Steps[Step].Num()));
        Listed += Order.Steps[Step].Num();
        // Predecessors from every list, then the smallest-free rule replayed independently.
        TMap<int32, TSet<int32>> Before;
        for (const FVoxelBiomeDecoration& Biome : Graph.Biomes)
            for (int32 Index = 0; Index < Biome.Steps[Step].Num(); ++Index)
            {
                TSet<int32>& Set = Before.FindOrAdd(Biome.Steps[Step][Index]);
                if (Index > 0) Set.Add(Biome.Steps[Step][Index - 1]);
            }
        TSet<int32> Done;
        int32 Broken = 0;
        for (const int32 Entry : Order.Steps[Step])
        {
            FString Smallest;
            for (const TPair<int32, TSet<int32>>& Pair : Before)
            {
                if (Done.Contains(Pair.Key)) continue;
                bool bFree = true;
                for (const int32 Previous : Pair.Value)
                    bFree &= Done.Contains(Previous);
                if (bFree &&
                    (Smallest.IsEmpty() || Placed[Pair.Key].Id.Compare(Smallest, ESearchCase::CaseSensitive) < 0))
                    Smallest = Placed[Pair.Key].Id;
            }
            if (Placed[Entry].Id != Smallest) ++Broken;
            Done.Add(Entry);
        }
        TestEqual(
            FString::Printf(TEXT("%s: features out of the smallest-free order"), FVoxelFeatureGraph::GetStepName(Step)),
            Broken, 0);
        TestEqual(FString::Printf(TEXT("%s: every listed feature is ordered"), FVoxelFeatureGraph::GetStepName(Step)),
            Order.Steps[Step].Num(), Before.Num());
    }
    TestEqual(TEXT("features per step"), FString::Join(PerStep, TEXT(" ")), FString(TEXT("0 4 5 4 4 0 34 7 3 109 1")));
    TestEqual(TEXT("top-level placed features"), Listed, 171);

    // Vanilla's own order (facts/feature_order.json: the jar's FeatureSorter over the overworld's biomes). Every pair
    // the biome lists constrain, directly or through other features, comes in the same order in both. Pairs no list
    // constrains may differ (our ties go by id, vanilla's by its own walk); their number is reported.
    const TSharedPtr<FJsonValue> Vanilla =
        LoadFeatureTestJson(FPaths::Combine(VoxelWorldgen::GetReferenceRoot(), TEXT("facts/feature_order.json")));
    const TArray<TSharedPtr<FJsonValue>>* VanillaSteps = nullptr;
    if (!TestTrue(TEXT("vanilla's order reads"),
            Vanilla.IsValid() && Vanilla->Type == EJson::Object &&
                Vanilla->AsObject()->TryGetArrayField(TEXT("steps"), VanillaSteps) &&
                VanillaSteps->Num() == FVoxelFeatureGraph::StepCount))
        return false;
    int32 Constrained = 0;
    int32 Differing = 0;
    for (int32 Step = 0; Step < FVoxelFeatureGraph::StepCount; ++Step)
    {
        const FString StepName = FVoxelFeatureGraph::GetStepName(Step);
        const TArray<int32>& Ours = Order.Steps[Step];
        const int32 N = Ours.Num();
        TMap<int32, int32> OursAt;
        for (int32 Index = 0; Index < N; ++Index)
            OursAt.Add(Ours[Index], Index);
        TArray<int32> VanillaAt;
        VanillaAt.Init(INDEX_NONE, N);
        const TArray<TSharedPtr<FJsonValue>>& Ids = (*VanillaSteps)[Step]->AsObject()->GetArrayField(TEXT("features"));
        for (int32 Index = 0; Index < Ids.Num(); ++Index)
            if (const int32* At =
                    OursAt.Find(Graph.FindEntry(EVoxelFeatureRegistry::PlacedFeature, Ids[Index]->AsString())))
                VanillaAt[*At] = Index;
        if (!TestTrue(FString::Printf(TEXT("%s: vanilla orders the same features"), *StepName),
                Ids.Num() == N && !VanillaAt.Contains(INDEX_NONE)))
            continue;
        // Must[I * N + J]: ours[I] comes before ours[J] in every order the lists allow, the transitive closure of the
        // lists' orders. Ours is one such order, so only I < J is set; rows close from the last one up.
        TArray<bool> Must;
        Must.Init(false, N * N);
        for (const FVoxelBiomeDecoration& Biome : Graph.Biomes)
            for (int32 Index = 1; Index < Biome.Steps[Step].Num(); ++Index)
                Must[OursAt[Biome.Steps[Step][Index - 1]] * N + OursAt[Biome.Steps[Step][Index]]] = true;
        for (int32 I = N - 1; I >= 0; --I)
            for (int32 J = I + 1; J < N; ++J)
                if (Must[I * N + J])
                    for (int32 K = J + 1; K < N; ++K)
                        Must[I * N + K] = Must[I * N + K] || Must[J * N + K];
        int32 Broken = 0;
        for (int32 I = 0; I < N; ++I)
            for (int32 J = I + 1; J < N; ++J)
            {
                const bool bAgrees = VanillaAt[I] < VanillaAt[J];
                Constrained += Must[I * N + J] ? 1 : 0;
                Broken += Must[I * N + J] && !bAgrees ? 1 : 0;
                Differing += !Must[I * N + J] && !bAgrees ? 1 : 0;
            }
        TestEqual(FString::Printf(TEXT("%s: constrained pairs vanilla orders otherwise"), *StepName), Broken, 0);
    }
    AddInfo(FString::Printf(
        TEXT("Vanilla's order: %d constrained pairs agree; %d unconstrained pairs differ"), Constrained, Differing));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFeaturesSeedsTest, "Unnamed.Voxel.Features.Seeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFeaturesSeedsTest::RunTest(const FString& Parameters)
{
    // Pinned against an independent implementation (FNV-1a 64 over UTF-8, then SplitMix64's finalizer, and
    // VoxelRandom::Hash64); changing any of these changes every decorated world.
    TestEqual(TEXT("salt of ore_iron_upper"), VoxelFeatureSeeds::GetFeatureSalt(TEXT("minecraft:ore_iron_upper")),
        0x25a74218bc3ebf3bull);
    TestEqual(TEXT("salt of patch_grass_plain"), VoxelFeatureSeeds::GetFeatureSalt(TEXT("minecraft:patch_grass_plain")),
        0xf78bc38342982885ull);
    TestEqual(TEXT("salt of trees_plains"), VoxelFeatureSeeds::GetFeatureSalt(TEXT("minecraft:trees_plains")),
        0xb7bff5545929f758ull);
    TestEqual(
        TEXT("salt hashes UTF-8 bytes"), VoxelFeatureSeeds::GetFeatureSalt(TEXT("minecraft:é")), 0x8311a0a4af31ab4full);
    TestEqual(TEXT("decoration salt"), VoxelFeatureSeeds::DecorationSalt, 0xe4c273a6158b7c9bull);
    TestEqual(TEXT("pattern salt"), VoxelFeatureSeeds::PatternSalt, 0xdbe28d7832048740ull);
    TestEqual(TEXT("vanilla's pattern seed"), VoxelFeatureSeeds::VanillaPatternSeed, int64(2345));
    TestEqual(TEXT("stream seed"),
        VoxelFeatureSeeds::GetStreamSeed(
            263, VoxelFeatureSeeds::GetFeatureSalt(TEXT("minecraft:ore_iron_upper")), -3, 5, 6),
        0x5c591f8fef303bc1ull);
    TestEqual(TEXT("stream seed, negative world seed"),
        VoxelFeatureSeeds::GetStreamSeed(
            uint64(int64(-4000000007)), VoxelFeatureSeeds::GetFeatureSalt(TEXT("minecraft:trees_plains")), 12, -7, 9),
        0xf154c5f5a37e1de0ull);
    TestEqual(TEXT("pattern seed"), VoxelFeatureSeeds::GetPatternSeed(263, 2345), 0x0d799d69af85e8f2ull);
    TestEqual(TEXT("pattern seed, 64-bit data seed"), VoxelFeatureSeeds::GetPatternSeed(263, -4000000007),
        0x863d06734cbe3b87ull);
    // Patterns move with the world seed (vanilla's sit alike in every world).
    TestNotEqual(TEXT("world seed changes the pattern"), VoxelFeatureSeeds::GetPatternSeed(264, 2345),
        VoxelFeatureSeeds::GetPatternSeed(263, 2345));
    return true;
}

#endif
