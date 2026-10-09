#include "Algo/Find.h"
#include "Density/VoxelDensityGraph.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Spline/VoxelSpline.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    const TCHAR* const WorldgenTestPresets[] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};

    // A file's one JSON value; wrapped in an array as the reader does, so a bare number parses too.
    TSharedPtr<FJsonValue> LoadWorldgenTestJson(const FString& File)
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

    // The first difference between two JSON values, with its path; empty when they are equal. Numbers compare exactly.
    FString DiffWorldgenTestJson(
        const TSharedPtr<FJsonValue>& Expected, const TSharedPtr<FJsonValue>& Actual, const FString& Path)
    {
        if (!Expected.IsValid() || !Actual.IsValid())
        {
            return Path + TEXT(": missing");
        }
        if (Expected->Type != Actual->Type)
        {
            return FString::Printf(
                TEXT("%s: JSON type %d, wrote %d"), *Path, int32(Expected->Type), int32(Actual->Type));
        }
        switch (Expected->Type)
        {
        case EJson::Number:
            return Expected->AsNumber() == Actual->AsNumber()
                ? FString()
                : FString::Printf(TEXT("%s: %.17g, wrote %.17g"), *Path, Expected->AsNumber(), Actual->AsNumber());
        case EJson::String:
            return Expected->AsString() == Actual->AsString()
                ? FString()
                : FString::Printf(TEXT("%s: %s, wrote %s"), *Path, *Expected->AsString(), *Actual->AsString());
        case EJson::Boolean:
            return Expected->AsBool() == Actual->AsBool() ? FString() : Path + TEXT(": boolean differs");
        case EJson::Array:
        {
            const TArray<TSharedPtr<FJsonValue>>& ExpectedArray = Expected->AsArray();
            const TArray<TSharedPtr<FJsonValue>>& ActualArray = Actual->AsArray();
            if (ExpectedArray.Num() != ActualArray.Num())
            {
                return FString::Printf(TEXT("%s: %d entries, wrote %d"), *Path, ExpectedArray.Num(), ActualArray.Num());
            }
            for (int32 Index = 0; Index < ExpectedArray.Num(); ++Index)
            {
                FString Difference = DiffWorldgenTestJson(
                    ExpectedArray[Index], ActualArray[Index], FString::Printf(TEXT("%s/%d"), *Path, Index));
                if (!Difference.IsEmpty())
                {
                    return Difference;
                }
            }
            return FString();
        }
        case EJson::Object:
        {
            const TSharedPtr<FJsonObject> ExpectedObject = Expected->AsObject();
            const TSharedPtr<FJsonObject> ActualObject = Actual->AsObject();
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : ActualObject->Values)
            {
                if (!ExpectedObject->HasField(Field.Key))
                {
                    return FString::Printf(TEXT("%s: wrote an extra field %s"), *Path, *Field.Key);
                }
            }
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : ExpectedObject->Values)
            {
                FString Difference = DiffWorldgenTestJson(
                    Field.Value, ActualObject->TryGetField(Field.Key), Path + TEXT("/") + Field.Key);
                if (!Difference.IsEmpty())
                {
                    return Difference;
                }
            }
            return FString();
        }
        default:
            return FString();
        }
    }

    // The ids of every file under data/minecraft/worldgen/<Folder>: "minecraft:" + its path without ".json".
    TArray<FString> ListWorldgenTestIds(const FString& Root, const TCHAR* Folder)
    {
        FString Directory = FPaths::Combine(Root, TEXT("data/minecraft/worldgen"), Folder);
        FPaths::NormalizeDirectoryName(Directory);
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.json"), true, false);
        TArray<FString> Ids;
        for (FString File : Files)
        {
            FPaths::NormalizeFilename(File);
            Ids.Add(TEXT("minecraft:") + File.RightChop(Directory.Len() + 1).LeftChop(5));
        }
        Ids.Sort();
        return Ids;
    }

    FString GetWorldgenTestFunctionName(const FVoxelDensityGraph& Graph, int32 Node)
    {
        const FVoxelDensityFunction* Function =
            Graph.Functions.FindByPredicate([Node](const FVoxelDensityFunction& Each) { return Each.Node == Node; });
        return Function ? Function->Name : FString::Printf(TEXT("(node %d)"), Node);
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelWorldgenPresetsTest, "Unnamed.Voxel.Worldgen.Presets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelWorldgenPresetsTest::RunTest(const FString& Parameters)
{
    const FString Root = VoxelWorldgen::GetReferenceRoot();
    for (const TCHAR* Name : WorldgenTestPresets)
    {
        FVoxelTerrainPreset Preset;
        FString Error;
        const double Start = FPlatformTime::Seconds();
        const bool bRead = VoxelWorldgen::ReadPreset(Root, Name, Preset, Error);
        const double ReadMs = (FPlatformTime::Seconds() - Start) * 1000;
        if (!TestTrue(FString::Printf(TEXT("%s reads and validates (%s)"), Name, *Error), bRead))
        {
            continue;
        }
        TestTrue(FString::Printf(TEXT("%s: sea level 63, Y -64 to 319, stone and water"), Name),
            Preset.SeaLevel == 63 && Preset.MinY == -64 && Preset.Height == 384 &&
                Preset.DefaultBlock == TEXT("minecraft:stone") && Preset.DefaultFluid == TEXT("minecraft:water"));
        const FVoxelTerrainPreset::FRouter& Router = Preset.Router;
        const FVoxelTerrainPreset::FAquifers& Aquifers = Preset.Aquifers;
        const int32 Entries[] = {Router.Continents, Router.Erosion, Router.Ridges, Router.Temperature,
            Router.Vegetation, Router.Depth, Router.ChunkSurfaceLevel, Router.FinalDensity, Aquifers.Barrier,
            Aquifers.Exclusion, Aquifers.FluidLevelFloodedness, Aquifers.FluidLevelSpread, Aquifers.Lava,
            Aquifers.SurfaceLevel};
        TestFalse(FString::Printf(TEXT("%s: every router and aquifer entry is set"), Name),
            Algo::Find(Entries, INDEX_NONE) != nullptr);
        TArray<FString> Labels;
        for (const FVoxelTerrainPreset::FDebugView& View : Preset.DebugViews)
        {
            Labels.Add(View.Label);
        }
        TestEqual(FString::Printf(TEXT("%s: debug views"), Name), FString::Join(Labels, TEXT(" ")),
            FString(TEXT("N T V C E D W PV PS")));
        const FVoxelDensityGraph& Graph = Preset.Graph;
        AddInfo(
            FString::Printf(TEXT("%s: %d functions, %d nodes, %d noises, %d splines with %d points, read in %.1f ms"),
                Name, Graph.Functions.Num(), Graph.Nodes.Num(), Graph.Noises.Num(), Graph.Splines.Num(),
                Graph.SplinePoints.Num(), ReadMs));
    }

    // Which file each world type's entry points come from.
    FVoxelTerrainPreset Overworld;
    FVoxelTerrainPreset LargeBiomes;
    FVoxelTerrainPreset Amplified;
    FString Error;
    if (!VoxelWorldgen::ReadPreset(Root, TEXT("overworld"), Overworld, Error) ||
        !VoxelWorldgen::ReadPreset(Root, TEXT("large_biomes"), LargeBiomes, Error) ||
        !VoxelWorldgen::ReadPreset(Root, TEXT("amplified"), Amplified, Error))
    {
        return false;
    }
    TestEqual(TEXT("overworld continents"), Overworld.Router.Continents,
        Overworld.Graph.FindFunction(TEXT("minecraft:overworld/continents")));
    TestEqual(TEXT("large biomes continents"), LargeBiomes.Router.Continents,
        LargeBiomes.Graph.FindFunction(TEXT("minecraft:overworld_large_biomes/continents")));
    TestEqual(TEXT("large biomes share the overworld's ridges"), LargeBiomes.Router.Ridges,
        LargeBiomes.Graph.FindFunction(TEXT("minecraft:overworld/ridges")));
    TestEqual(TEXT("amplified final density"), Amplified.Router.FinalDensity,
        Amplified.Graph.FindFunction(TEXT("minecraft:overworld_amplified/final_density")));

    // Overworld nodes, each checked by hand against its JSON.
    const FVoxelDensityGraph& Graph = Overworld.Graph;
    const auto At = [&Graph](int32 Index) -> const FVoxelDensityNode&
    {
        static const FVoxelDensityNode Missing{EVoxelDensityOp::Count};
        return Graph.Nodes.IsValidIndex(Index) ? Graph.Nodes[Index] : Missing;
    };
    const auto IsConstant = [&At](int32 Index, double Value)
    { return At(Index).Op == EVoxelDensityOp::Constant && At(Index).Params[0] == Value; };

    const FVoxelDensityNode& Y = At(Graph.FindFunction(TEXT("minecraft:y")));
    TestTrue(TEXT("y: a gradient on y from (-4064, -4064) to (4062, 4062)"),
        Y.Op == EVoxelDensityOp::Gradient && Y.Axis == 1 && Y.Params[0] == -4064 && Y.Params[1] == -4064 &&
            Y.Params[2] == 4062 && Y.Params[3] == 4062);

    const FVoxelDensityNode& ShiftX = At(Graph.FindFunction(TEXT("minecraft:shift_x")));
    TestTrue(TEXT("shift_x: cache(shift_a(offset))"),
        ShiftX.Op == EVoxelDensityOp::Cache && At(ShiftX.Inputs[0]).Op == EVoxelDensityOp::ShiftA &&
            Graph.Noises.IsValidIndex(At(ShiftX.Inputs[0]).Ref) &&
            Graph.Noises[At(ShiftX.Inputs[0]).Ref].Name == TEXT("minecraft:offset"));

    const FVoxelDensityNode& Final = At(Overworld.Router.FinalDensity);
    const FVoxelDensityNode& Lower = At(Final.Inputs[0]);
    const FVoxelDensityNode& Squeeze = At(Lower.Inputs[0]);
    const FVoxelDensityNode& Cells = At(Squeeze.Inputs[0]);
    TestTrue(TEXT("final density: add(min(squeeze(interpolated 4 x 8), caves/noodle), beardifier)"),
        Final.Op == EVoxelDensityOp::Add && Lower.Op == EVoxelDensityOp::Min &&
            Squeeze.Op == EVoxelDensityOp::Squeeze && Cells.Op == EVoxelDensityOp::Interpolated &&
            Cells.Params[0] == 4 && Cells.Params[1] == 8 &&
            Lower.Inputs[1] == Graph.FindFunction(TEXT("minecraft:overworld/caves/noodle")) &&
            At(Final.Inputs[1]).Op == EVoxelDensityOp::Beardifier);

    // min(sub(-0.225, erosion), max(sub(depth, 0.9), 0)), reading the router's own erosion and depth nodes.
    const FVoxelDensityNode& Exclusion = At(Overworld.Aquifers.Exclusion);
    const FVoxelDensityNode& Left = At(Exclusion.Inputs[0]);
    const FVoxelDensityNode& Right = At(Exclusion.Inputs[1]);
    const FVoxelDensityNode& Deep = At(Right.Inputs[0]);
    TestTrue(TEXT("aquifer exclusion"),
        Exclusion.Op == EVoxelDensityOp::Min && Left.Op == EVoxelDensityOp::Sub && IsConstant(Left.Inputs[0], -0.225) &&
            Left.Inputs[1] == Overworld.Router.Erosion && Right.Op == EVoxelDensityOp::Max &&
            Deep.Op == EVoxelDensityOp::Sub && Deep.Inputs[0] == Overworld.Router.Depth &&
            IsConstant(Deep.Inputs[1], 0.9) && IsConstant(Right.Inputs[1], 0));

    const FVoxelDensityNode& Barrier = At(Overworld.Aquifers.Barrier);
    TestTrue(TEXT("aquifer barrier: noise aquifer_barrier, xz 1, y 0.5, unshifted"),
        Barrier.Op == EVoxelDensityOp::Noise && Graph.Noises.IsValidIndex(Barrier.Ref) &&
            Graph.Noises[Barrier.Ref].Name == TEXT("minecraft:aquifer_barrier") && Barrier.Params[0] == 1 &&
            Barrier.Params[1] == 0.5 && Barrier.Inputs[0] == INDEX_NONE && Barrier.Inputs[1] == INDEX_NONE);

    const FVoxelDensityNode* Select = Graph.Nodes.FindByPredicate([](const FVoxelDensityNode& Node)
        { return Node.Op == EVoxelDensityOp::IntervalSelect && Node.ListCount == 5; });
    TestTrue(TEXT("spaghetti_2d: 5 functions between thresholds -0.75, -0.5, 0.5, 0.75"),
        Select && Graph.ListValues[Select->Ref] == -0.75 && Graph.ListValues[Select->Ref + 1] == -0.5 &&
            Graph.ListValues[Select->Ref + 2] == 0.5 && Graph.ListValues[Select->Ref + 3] == 0.75);

    const int32 Continentalness = Graph.FindNoise(TEXT("minecraft:continentalness"));
    TestTrue(TEXT("continentalness: octave -9, amplitude 0.8880832896205223, modifiers 1 1 2 2 2 1 1 1 1"),
        Continentalness != INDEX_NONE && Graph.Noises[Continentalness].BaseOctave == -9 &&
            Graph.Noises[Continentalness].BaseAmplitude == 0.8880832896205223 &&
            Graph.Noises[Continentalness].AmplitudeModifiers == TArray<double>({1, 1, 2, 2, 2, 1, 1, 1, 1}));

    const FVoxelDensityNode* Offset =
        Graph.Nodes.FindByPredicate([](const FVoxelDensityNode& Node) { return Node.Op == EVoxelDensityOp::Spline; });
    TestTrue(TEXT("splines read the router's continents node: a reference is one shared node"),
        Offset && Graph.Splines[Offset->Ref].Coordinate == Overworld.Router.Continents);

    // Files no overworld entry reaches: a bare-number function, and noises that leave out the count or the modifiers.
    FVoxelDensityGraph Extra;
    const FString Functions[] = {TEXT("minecraft:zero")};
    const FString Noises[] = {TEXT("minecraft:small_patch"), TEXT("minecraft:gravel")};
    const bool bExtra = VoxelWorldgen::ReadReferences(Root, Functions, Noises, Extra, Error);
    TestTrue(FString::Printf(TEXT("zero, small_patch and gravel read (%s)"), *Error), bExtra);
    if (bExtra)
    {
        TestTrue(TEXT("zero is the constant 0"),
            Extra.Nodes[Extra.FindFunction(TEXT("minecraft:zero"))].Op == EVoxelDensityOp::Constant &&
                Extra.Nodes[Extra.FindFunction(TEXT("minecraft:zero"))].Params[0] == 0);
        TestTrue(TEXT("small_patch: one octave at 3"),
            Extra.Noises[Extra.FindNoise(TEXT("minecraft:small_patch"))].AmplitudeModifiers == TArray<double>({3}));
        TestTrue(TEXT("gravel: four octaves at 1"),
            Extra.Noises[Extra.FindNoise(TEXT("minecraft:gravel"))].AmplitudeModifiers == TArray<double>({1, 1, 1, 1}));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelWorldgenRoundTripTest, "Unnamed.Voxel.Worldgen.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelWorldgenRoundTripTest::RunTest(const FString& Parameters)
{
    // Every function and noise file in the reference, read into one graph and written back, equals its file.
    const FString Root = VoxelWorldgen::GetReferenceRoot();
    const FString Worldgen = FPaths::Combine(Root, TEXT("data/minecraft/worldgen"));
    const TArray<FString> FunctionIds = ListWorldgenTestIds(Root, TEXT("density_function"));
    const TArray<FString> NoiseIds = ListWorldgenTestIds(Root, TEXT("noise"));
    TestEqual(TEXT("density function files"), FunctionIds.Num(), 51);
    TestEqual(TEXT("noise files"), NoiseIds.Num(), 62);
    FVoxelDensityGraph Graph;
    FString Error;
    const bool bRead = VoxelWorldgen::ReadReferences(Root, FunctionIds, NoiseIds, Graph, Error);
    if (!TestTrue(FString::Printf(TEXT("every file reads (%s)"), *Error), bRead))
    {
        return false;
    }
    int32 Differences = 0;
    const auto Compare = [this, &Differences](const TSharedPtr<FJsonValue>& Expected,
                             const TSharedPtr<FJsonValue>& Actual, const FString& Path)
    {
        const FString Difference = DiffWorldgenTestJson(Expected, Actual, Path);
        if (!Difference.IsEmpty())
        {
            AddError(Difference);
            ++Differences;
        }
    };
    for (const FString& Id : FunctionIds)
    {
        const FString File = FPaths::Combine(Worldgen, TEXT("density_function"), Id.RightChop(10) + TEXT(".json"));
        Compare(LoadWorldgenTestJson(File), VoxelWorldgen::WriteValue(Graph, Graph.FindFunction(Id), true), Id);
    }
    for (const FString& Id : NoiseIds)
    {
        const FString File = FPaths::Combine(Worldgen, TEXT("noise"), Id.RightChop(10) + TEXT(".json"));
        Compare(LoadWorldgenTestJson(File), VoxelWorldgen::WriteNoise(Graph.Noises[Graph.FindNoise(Id)]), Id);
    }
    // Each preset's router, aquifers and debug views, against the parts of its settings file they come from.
    for (const TCHAR* Name : WorldgenTestPresets)
    {
        FVoxelTerrainPreset Preset;
        if (!VoxelWorldgen::ReadPreset(Root, Name, Preset, Error))
        {
            AddError(Error);
            continue;
        }
        const auto Write = [&Preset](int32 Node) { return VoxelWorldgen::WriteValue(Preset.Graph, Node, false); };
        const FVoxelTerrainPreset::FRouter& Router = Preset.Router;
        const FVoxelTerrainPreset::FAquifers& Aquifers = Preset.Aquifers;
        const TSharedRef<FJsonObject> RouterJson = MakeShared<FJsonObject>();
        RouterJson->SetField(TEXT("continents"), Write(Router.Continents));
        RouterJson->SetField(TEXT("erosion"), Write(Router.Erosion));
        RouterJson->SetField(TEXT("ridges"), Write(Router.Ridges));
        RouterJson->SetField(TEXT("temperature"), Write(Router.Temperature));
        RouterJson->SetField(TEXT("vegetation"), Write(Router.Vegetation));
        RouterJson->SetField(TEXT("depth"), Write(Router.Depth));
        RouterJson->SetField(TEXT("chunk_surface_level"), Write(Router.ChunkSurfaceLevel));
        RouterJson->SetField(TEXT("final_density"), Write(Router.FinalDensity));
        const TSharedRef<FJsonObject> AquifersJson = MakeShared<FJsonObject>();
        AquifersJson->SetField(TEXT("barrier"), Write(Aquifers.Barrier));
        AquifersJson->SetField(TEXT("exclusion"), Write(Aquifers.Exclusion));
        AquifersJson->SetField(TEXT("fluid_level_floodedness"), Write(Aquifers.FluidLevelFloodedness));
        AquifersJson->SetField(TEXT("fluid_level_spread"), Write(Aquifers.FluidLevelSpread));
        AquifersJson->SetField(TEXT("lava"), Write(Aquifers.Lava));
        AquifersJson->SetField(TEXT("surface_level"), Write(Aquifers.SurfaceLevel));
        TArray<TSharedPtr<FJsonValue>> Views;
        for (const FVoxelTerrainPreset::FDebugView& View : Preset.DebugViews)
        {
            const TSharedRef<FJsonObject> ViewJson = MakeShared<FJsonObject>();
            ViewJson->SetField(TEXT("function"), Write(View.Node));
            ViewJson->SetStringField(TEXT("label"), View.Label);
            Views.Add(MakeShared<FJsonValueObject>(ViewJson));
        }
        const TSharedPtr<FJsonValue> Settings =
            LoadWorldgenTestJson(FPaths::Combine(Worldgen, TEXT("noise_settings"), FString(Name) + TEXT(".json")));
        const TSharedPtr<FJsonObject> SettingsObject = Settings ? Settings->AsObject() : nullptr;
        if (!TestNotNull(FString::Printf(TEXT("%s settings load"), Name), SettingsObject.Get()))
        {
            continue;
        }
        Compare(SettingsObject->TryGetField(TEXT("noise_router")), MakeShared<FJsonValueObject>(RouterJson),
            FString(Name) + TEXT("/noise_router"));
        Compare(SettingsObject->TryGetField(TEXT("aquifers")), MakeShared<FJsonValueObject>(AquifersJson),
            FString(Name) + TEXT("/aquifers"));
        Compare(SettingsObject->TryGetField(TEXT("debug_functions")), MakeShared<FJsonValueArray>(Views),
            FString(Name) + TEXT("/debug_functions"));
    }
    TestEqual(TEXT("files written back unchanged"), Differences, 0);
    AddInfo(FString::Printf(TEXT("%d functions and %d noises in one graph: %d nodes, %d splines, %d spline points"),
        Graph.Functions.Num(), Graph.Noises.Num(), Graph.Nodes.Num(), Graph.Splines.Num(), Graph.SplinePoints.Num()));

    // Field order: the reader and writer share one field table, so a swapped field would still round-trip. These nodes,
    // found by their values, pin each op's slots as the header documents them.
    const auto At = [&Graph](int32 Index) -> const FVoxelDensityNode&
    {
        static const FVoxelDensityNode Missing{EVoxelDensityOp::Count};
        return Graph.Nodes.IsValidIndex(Index) ? Graph.Nodes[Index] : Missing;
    };
    const auto IsConstant = [&At](int32 Index, double Value)
    { return At(Index).Op == EVoxelDensityOp::Constant && At(Index).Params[0] == Value; };
    const auto HasNode = [&Graph](const auto& Match) { return Graph.Nodes.ContainsByPredicate(Match); };
    const int32 Pillars = Graph.FindFunction(TEXT("minecraft:overworld/caves/pillars"));
    const int32 Toggle = Graph.FindFunction(TEXT("minecraft:overworld/ore_vein/toggle"));
    const int32 Preliminary = Graph.FindFunction(TEXT("minecraft:overworld/preliminary_surface_level"));
    TestTrue(TEXT("gradient: from coordinate, from value, to coordinate, to value"),
        HasNode(
            [](const FVoxelDensityNode& Node)
            {
                return Node.Op == EVoxelDensityOp::Gradient && Node.Params[0] == -10 && Node.Params[1] == 0.3 &&
                    Node.Params[2] == 30 && Node.Params[3] == 0;
            }));
    TestTrue(TEXT("lerp: alpha, first, second"),
        HasNode(
            [&](const FVoxelDensityNode& Node)
            {
                return Node.Op == EVoxelDensityOp::Lerp && At(Node.Inputs[0]).Op == EVoxelDensityOp::Gradient &&
                    At(Node.Inputs[0]).Params[0] == 240 && IsConstant(Node.Inputs[1], -0.078125) &&
                    At(Node.Inputs[2]).Op == EVoxelDensityOp::Clamp;
            }));
    TestTrue(TEXT("range_choice: input, when in range, when out of range; min inclusive, max exclusive"),
        HasNode(
            [&](const FVoxelDensityNode& Node)
            {
                return Node.Op == EVoxelDensityOp::RangeChoice && Node.Inputs[0] == Pillars &&
                    IsConstant(Node.Inputs[1], -1000000) && Node.Inputs[2] == Pillars && Node.Params[0] == -1000000 &&
                    Node.Params[1] == 0.03;
            }));
    TestTrue(TEXT("clamp: input; min, max"),
        HasNode(
            [&](const FVoxelDensityNode& Node)
            {
                return Node.Op == EVoxelDensityOp::Clamp && At(Node.Inputs[0]).Op == EVoxelDensityOp::Abs &&
                    At(Node.Inputs[0]).Inputs[0] == Toggle && Node.Params[0] == 0.4 && Node.Params[1] == 0.6;
            }));
    TestTrue(TEXT("old_blended_noise: xz scale, y scale, xz factor, y factor, smear scale multiplier"),
        HasNode(
            [](const FVoxelDensityNode& Node)
            {
                return Node.Op == EVoxelDensityOp::OldBlendedNoise && Node.Params[0] == 0.25 &&
                    Node.Params[1] == 0.125 && Node.Params[2] == 80 && Node.Params[3] == 160 && Node.Params[4] == 8;
            }));
    TestTrue(TEXT("find_top_surface: density, upper bound; lower bound, cell height"),
        HasNode(
            [&](const FVoxelDensityNode& Node)
            {
                return Node.Op == EVoxelDensityOp::FindTopSurface && At(Node.Inputs[0]).Op == EVoxelDensityOp::Add &&
                    Node.Params[0] == -64 && Node.Params[1] == 8;
            }));
    TestTrue(TEXT("interpolated: input; cell size xz, cell size y"),
        HasNode(
            [&](const FVoxelDensityNode& Node)
            {
                return Node.Op == EVoxelDensityOp::Interpolated && Node.Inputs[0] == Preliminary &&
                    Node.Params[0] == 16 && Node.Params[1] == 1;
            }));

    // Strictness: malformed files are refused with a reason, from files held in memory.
    TMap<FString, FString> Files;
    Files.Add(TEXT("density_function/b.json"), TEXT("{\"type\":\"minecraft:abs\",\"input\":\"minecraft:a\"}"));
    Files.Add(TEXT("noise/n.json"),
        TEXT("{\"base_octave\":0,\"base_amplitude\":1,\"octave_count\":2,\"amplitude_modifiers\":[1,1,1]}"));
    const auto Load = [&Files](const FString& Path, FString& OutText)
    {
        const FString* Text = Files.Find(Path);
        if (Text)
        {
            OutText = *Text;
        }
        return Text != nullptr;
    };
    Files.Add(TEXT("noise/m.json"), TEXT("{\"base_octave\":0,\"base_amplitude\":1,\"amplitude_modifiers\":[1,2]}"));
    const FString A[] = {TEXT("minecraft:a")};
    struct FWorldgenTestCase
    {
        const TCHAR* Text;
        const TCHAR* Reason;
    };
    const FWorldgenTestCase Cases[] = {
        {TEXT("{\"type\":\"minecraft:wobble\"}"), TEXT("unknown type minecraft:wobble")},
        {TEXT("{\"type\":\"minecraft:abs\",\"input\":0,\"extra\":1}"), TEXT("unexpected field extra")},
        {TEXT("{\"type\":\"minecraft:clamp\",\"input\":0,\"min\":0}"), TEXT("max must be a number")},
        {TEXT("{\"type\":\"minecraft:abs\",\"input\":\"minecraft:b\"}"), TEXT("references itself")},
        {TEXT("{\"type\":\"minecraft:abs\",\"input\":\"minecraft:missing\"}"), TEXT("cannot read the file")},
        {TEXT("{\"type\":\"minecraft:abs\",\"input\":\"minecraft:../a\"}"), TEXT("not a density function id")},
        {TEXT("{\"type\":\"minecraft:interval_select\",\"input\":0,\"functions\":[0,1],\"thresholds\":[]}"),
            TEXT("n - 1 thresholds")},
        {TEXT("{\"type\":\"minecraft:noise\",\"noise\":\"minecraft:n\",\"xz_scale\":1,\"y_scale\":1}"),
            TEXT("octave_count entries")},
        {TEXT("{\"type\":\"minecraft:spline\",\"spline\":{\"coordinate\":0,\"points\":[]}}"), TEXT("needs points")},
        {TEXT("{\"type\":\"minecraft:gradient\",\"axis\":\"w\",\"from_coordinate\":0,\"from_value\":0,"
              "\"to_coordinate\":1,\"to_value\":1}"),
            TEXT("axis must be")},
        {TEXT("{\"type\":\"minecraft:constant\",\"argument\":1}"), TEXT("unknown type")},
        {TEXT("{\"type\":\"minecraft:ABS\",\"input\":0}"), TEXT("unknown type minecraft:ABS")},
        {TEXT("{\"type\":\"minecraft:abs\",\"INPUT\":0}"), TEXT("unexpected field INPUT")},
        {TEXT("{\"type\":\"minecraft:gradient\",\"axis\":\"Y\",\"from_coordinate\":0,\"from_value\":0,"
              "\"to_coordinate\":1,\"to_value\":1}"),
            TEXT("axis must be")},
        {TEXT("{\"type\":\"minecraft:noise\",\"noise\":\"minecraft:m\",\"xz_scale\":1,\"y_scale\":1}"),
            TEXT("1 when it is left out")},
    };
    for (const FWorldgenTestCase& Case : Cases)
    {
        Files.Add(TEXT("density_function/a.json"), Case.Text);
        FVoxelDensityGraph Bad;
        FString Reason;
        const bool bAccepted = VoxelWorldgen::ReadReferences(Load, A, {}, Bad, Reason);
        TestFalse(FString::Printf(TEXT("refused: %s"), Case.Text), bAccepted);
        TestTrue(FString::Printf(TEXT("says \"%s\": %s"), Case.Reason, *Reason), Reason.Contains(Case.Reason));
    }
    Files.Add(TEXT("density_function/a.json"), TEXT("{\"type\":\"minecraft:abs\",\"input\":0.5}"));
    // Node 0 is the constant 0.5, node 1 the abs reading it; the bad graphs below start from this one.
    FVoxelDensityGraph Good;
    const bool bGood = VoxelWorldgen::ReadReferences(Load, A, {}, Good, Error);
    if (!TestTrue(FString::Printf(TEXT("a well-formed file in memory reads (%s)"), *Error),
            bGood && Good.Nodes.Num() == 2 && Good.Functions.Num() == 1))
    {
        return false;
    }

    // Validate refuses graphs the reader never makes, each with its reason.
    const auto Refuses = [this](const TCHAR* What, const FVoxelDensityGraph& Bad, const TCHAR* Expected)
    {
        FString Reason;
        TestFalse(FString::Printf(TEXT("%s is refused"), What), Bad.Validate(&Reason));
        TestTrue(FString::Printf(TEXT("%s says \"%s\": %s"), What, Expected, *Reason), Reason.Contains(Expected));
    };
    FVoxelDensityGraph Forward;
    Forward.Nodes.SetNum(2);
    Forward.Nodes[0].Op = EVoxelDensityOp::Abs;
    Forward.Nodes[0].Inputs[0] = 1;
    Refuses(TEXT("an input after its node"), Forward, TEXT("not an earlier node"));

    FVoxelDensityGraph NoNoise = Good;
    NoNoise.Nodes[1].Op = EVoxelDensityOp::Noise;
    NoNoise.Nodes[1].Inputs[0] = INDEX_NONE;
    Refuses(TEXT("a noise node without a noise"), NoNoise, TEXT("noise -1 is unknown"));

    FVoxelDensityGraph Axis = Good;
    Axis.Nodes[1] = FVoxelDensityNode{EVoxelDensityOp::Gradient, 3};
    Refuses(TEXT("a gradient on axis 3"), Axis, TEXT("axis 3"));

    FVoxelDensityGraph Thresholds = Good;
    FVoxelDensityNode& Select = Thresholds.Nodes.Add_GetRef(FVoxelDensityNode{EVoxelDensityOp::IntervalSelect});
    Select.Inputs[0] = 0;
    Select.Ref = 0;
    Select.ListCount = 3;
    Thresholds.ListNodes = {0, 1, 1};
    Thresholds.ListValues = {0.5, 0.5, 0};
    Refuses(TEXT("thresholds that do not increase"), Thresholds, TEXT("thresholds do not increase"));

    FVoxelDensityGraph Nested = Good;
    Nested.Splines = {FVoxelDensitySpline{0, 0, 1}};
    Nested.SplinePoints = {FVoxelDensitySplinePoint{0, 0, 0, 0}};
    Refuses(TEXT("a spline nesting itself"), Nested, TEXT("not an earlier spline"));

    FVoxelDensityGraph Late = Good;
    Late.Nodes[0] = FVoxelDensityNode{EVoxelDensityOp::Spline};
    Late.Nodes[0].Ref = 0;
    Late.Splines = {FVoxelDensitySpline{1, 0, 1}};
    Late.SplinePoints = {FVoxelDensitySplinePoint{}};
    Refuses(TEXT("a spline reading a later node"), Late, TEXT("coordinate 1 is not an earlier node"));

    FVoxelDensityGraph Twice = Good;
    const FVoxelDensityFunction First = Twice.Functions[0];
    Twice.Functions.Add(First);
    Refuses(TEXT("a function named twice"), Twice, TEXT("duplicate"));

    FVoxelDensityGraph Silent = Good;
    Silent.Noises.Add(FVoxelDensityNoise{TEXT("minecraft:silent"), 0, 1, {}});
    Refuses(TEXT("a noise without octaves"), Silent, TEXT("without octaves"));

    // BuildSpline refuses an unknown spline and points out of order.
    FVoxelSpline Baked;
    TArray<int32> Coordinates;
    FString Reason;
    TestFalse(TEXT("BuildSpline refuses an unknown spline"), Good.BuildSpline(0, Baked, Coordinates, &Reason));
    TestTrue(FString::Printf(TEXT("... saying so: %s"), *Reason), Reason.Contains(TEXT("unknown")));
    FVoxelDensityGraph Unsorted = Good;
    Unsorted.Splines = {FVoxelDensitySpline{0, 0, 2}};
    Unsorted.SplinePoints = {
        FVoxelDensitySplinePoint{1, 0, 0, INDEX_NONE}, FVoxelDensitySplinePoint{0, 0, 0, INDEX_NONE}};
    Reason.Reset();
    TestFalse(TEXT("BuildSpline refuses locations out of order"), Unsorted.BuildSpline(0, Baked, Coordinates, &Reason));
    TestFalse(TEXT("... with a reason"), Reason.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelWorldgenSplinesTest, "Unnamed.Voxel.Worldgen.Splines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelWorldgenSplinesTest::RunTest(const FString& Parameters)
{
    // The reference's terrain splines, read and baked into FVoxelSpline, against Mojang's own values from the jar
    // (facts/spline_samples.json, dumped by tools/WorldgenFacts.java).
    const FString Root = VoxelWorldgen::GetReferenceRoot();
    const TSharedPtr<FJsonValue> Facts = LoadWorldgenTestJson(FPaths::Combine(Root, TEXT("facts/spline_samples.json")));
    const TSharedPtr<FJsonObject>* FactsObject = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
    if (!TestTrue(TEXT("facts load"),
            Facts && Facts->TryGetObject(FactsObject) && (*FactsObject)->TryGetArrayField(TEXT("splines"), Entries)))
    {
        return false;
    }
    TestEqual(TEXT("splines in the facts"), Entries->Num(), 9);

    int32 Samples = 0;
    int32 Misses = 0;
    double Worst = 0;
    for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
    {
        const TSharedPtr<FJsonObject> Entry = EntryValue->AsObject();
        const FString Id = Entry->GetStringField(TEXT("function"));
        FVoxelDensityGraph Graph;
        FString Error;
        const FString Ids[] = {Id};
        if (!VoxelWorldgen::ReadReferences(Root, Ids, {}, Graph, Error))
        {
            AddError(Error);
            continue;
        }

        // The function's spline node, found depth first from its root without entering other named functions.
        TArray<int32> Splines;
        TArray<int32> Pending = {Graph.FindFunction(Id)};
        while (!Pending.IsEmpty())
        {
            const FVoxelDensityNode& Node = Graph.Nodes[Pending.Pop()];
            if (Node.Op == EVoxelDensityOp::Spline)
            {
                Splines.Add(Node.Ref);
                continue;
            }
            for (const int32 Input : Node.Inputs)
            {
                if (Input != INDEX_NONE && GetWorldgenTestFunctionName(Graph, Input).StartsWith(TEXT("(")))
                {
                    Pending.Add(Input);
                }
            }
        }
        if (!TestEqual(FString::Printf(TEXT("%s holds one spline"), *Id), Splines.Num(), 1))
        {
            continue;
        }
        FVoxelSpline Spline;
        TArray<int32> Coordinates;
        if (!Graph.BuildSpline(Splines[0], Spline, Coordinates, &Error))
        {
            AddError(FString::Printf(TEXT("%s: %s"), *Id, *Error));
            continue;
        }
        TArray<FString> Names;
        for (const int32 Coordinate : Coordinates)
        {
            Names.Add(GetWorldgenTestFunctionName(Graph, Coordinate));
        }
        TArray<FString> Expected;
        for (const TSharedPtr<FJsonValue>& Name : Entry->GetArrayField(TEXT("coordinates")))
        {
            Expected.Add(Name->AsString());
        }
        if (!TestEqual(FString::Printf(TEXT("%s coordinates in first use"), *Id), FString::Join(Names, TEXT(" ")),
                FString::Join(Expected, TEXT(" "))))
        {
            continue;
        }

        TArray<float> Inputs;
        for (const TSharedPtr<FJsonValue>& SampleValue : Entry->GetArrayField(TEXT("samples")))
        {
            const TArray<TSharedPtr<FJsonValue>>& Sample = SampleValue->AsArray();
            Inputs.Reset();
            for (int32 Index = 0; Index + 1 < Sample.Num(); ++Index)
            {
                Inputs.Add(float(Sample[Index]->AsNumber()));
            }
            const float Want = float(Sample.Last()->AsNumber());
            const float Got = Spline.Evaluate(Inputs);
            const double Difference = FMath::Abs(double(Got) - double(Want));
            Worst = FMath::Max(Worst, Difference);
            ++Samples;
            if (Difference > 1e-5 * FMath::Max(1.0, FMath::Abs(double(Want))) && ++Misses <= 5)
            {
                AddError(FString::Printf(TEXT("%s at (%s): %.9g, Mojang %.9g"), *Id,
                    *FString::JoinBy(Inputs, TEXT(", "), [](float Input) { return FString::SanitizeFloat(Input); }),
                    Got, Want));
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d samples, worst difference %.3g"), Samples, Worst));
    TestEqual(TEXT("samples"), Samples, 9 * 512);
    TestEqual(TEXT("samples off Mojang's values"), Misses, 0);
    return true;
}

#endif
