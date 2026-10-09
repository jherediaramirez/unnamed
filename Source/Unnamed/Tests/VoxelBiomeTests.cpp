#include "Biome/VoxelBiomeTable.h"
#include "Density/VoxelDensityGraph.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Surface/VoxelSurfaceRules.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // The overworld parameter list in the reference: 56 biomes, 7,594 points.
    constexpr int32 BiomeTestBiomeCount = 56;
    constexpr int32 BiomeTestPointCount = 7594;

    // The parts of the small biome root that the strictness cases replace one at a time.
    const TCHAR* const BiomeTestWater = TEXT("'water_color':'#3f76e4'");
    const TCHAR* const BiomeTestPreset = TEXT("minecraft:overworld");
    const TCHAR* const BiomeTestRow = TEXT("0,-10000,10000,-10000,10000,-10000,10000,-10000,10000,0,0,-10000,10000,0");

    // Single quotes stand for double quotes, which keeps the JSON of the cases readable.
    FString BiomeTestJson(FString Text)
    {
        Text.ReplaceCharInline(TEXT('\''), TEXT('"'));
        return Text;
    }

    // A file's one JSON value; wrapped in an array so a bare value parses too.
    TSharedPtr<FJsonValue> LoadBiomeTestJson(const FString& File)
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

    // A JSON value as condensed text, for messages.
    FString BiomeTestJsonText(const TSharedPtr<FJsonValue>& Value)
    {
        TArray<TSharedPtr<FJsonValue>> Wrapped;
        if (Value.IsValid())
        {
            Wrapped.Add(Value);
        }
        else
        {
            Wrapped.Add(MakeShared<FJsonValueNull>());
        }
        FString Text;
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
        FJsonSerializer::Serialize(Wrapped, Writer);
        return Text.Len() >= 2 ? Text.Mid(1, Text.Len() - 2) : Text;
    }

    // FString == ignores case; ids are case sensitive.
    bool BiomeTestSame(const FString& Actual, const FString& Expected)
    {
        return Actual.Equals(Expected, ESearchCase::CaseSensitive);
    }

    bool BiomeTestHas(const TArray<FString>& List, const FString& Value)
    {
        return List.ContainsByPredicate([&Value](const FString& Each) { return BiomeTestSame(Each, Value); });
    }

    void BiomeTestAddUnique(TArray<FString>& List, const FString& Value)
    {
        if (!BiomeTestHas(List, Value))
        {
            List.Add(Value);
        }
    }

    FString BiomeTestString(const FVoxelSurfaceRules& Rules, int32 Index)
    {
        return Rules.Strings.IsValidIndex(Index) ? Rules.Strings[Index] : FString();
    }

    // The ids of every file under data/minecraft/worldgen/<Folder>: "minecraft:" + its path without ".json".
    TArray<FString> ListBiomeTestIds(const FString& Root, const TCHAR* Folder)
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

    // A reference root of a few files under Saved/Automation/VoxelBiomeTests/<Case>, emptied before use and removed
    // afterwards.
    class FBiomeTestFolder
    {
    public:
        explicit FBiomeTestFolder(const TCHAR* Case)
            : Root(FPaths::ConvertRelativePathToFull(
                  FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/VoxelBiomeTests"), Case)))
        {
            IFileManager::Get().DeleteDirectory(*Root, false, true);
        }

        FBiomeTestFolder(const FBiomeTestFolder&) = delete;
        FBiomeTestFolder& operator=(const FBiomeTestFolder&) = delete;

        ~FBiomeTestFolder()
        {
            IFileManager::Get().DeleteDirectory(*Root, false, true);
            // The folder above too, when nothing else is in it.
            IFileManager::Get().DeleteDirectory(*FPaths::GetPath(Root), false, false);
        }

        // Writes a file below the root; false when that fails.
        bool Write(const TCHAR* Relative, const FString& Text) const
        {
            const FString File = FPaths::Combine(Root, Relative);
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
            return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }

        const FString Root;
    };

    // A top-level array of the facts file, or null.
    const TArray<TSharedPtr<FJsonValue>>* FindBiomeTestArray(const TSharedPtr<FJsonValue>& Facts, const TCHAR* Key)
    {
        const TArray<TSharedPtr<FJsonValue>>* Found = nullptr;
        if (Facts.IsValid() && Facts->Type == EJson::Object && Facts->AsObject()->TryGetArrayField(Key, Found))
        {
            return Found;
        }
        return nullptr;
    }

    // How a point differs from its row of the facts file (biome name, the twelve range ends, the offset), or empty.
    FString DescribeBiomeTestPointDifference(const FVoxelBiomeTable& Table, const FVoxelBiomeParameter& Point,
        const TArray<FString>& FactBiomes, const TArray<TSharedPtr<FJsonValue>>& Row)
    {
        constexpr int32 AxisCount = int32(EVoxelClimateAxis::Count);
        if (Row.Num() != 2 + 2 * AxisCount)
        {
            return FString::Printf(TEXT("the facts row holds %d values"), Row.Num());
        }
        for (const TSharedPtr<FJsonValue>& Cell : Row)
        {
            if (!Cell.IsValid() || Cell->Type != EJson::Number)
            {
                return TEXT("the facts row holds a value that is not a number");
            }
        }
        const int32 FactBiome = int32(Row[0]->AsNumber());
        if (!FactBiomes.IsValidIndex(FactBiome) || !Table.Biomes.IsValidIndex(Point.Biome))
        {
            return FString::Printf(TEXT("biome index %d, facts %d"), Point.Biome, FactBiome);
        }
        if (!BiomeTestSame(Table.Biomes[Point.Biome].Name, FactBiomes[FactBiome]))
        {
            return FString::Printf(TEXT("biome %s, facts %s"), *Table.Biomes[Point.Biome].Name, *FactBiomes[FactBiome]);
        }
        for (int32 Axis = 0; Axis < AxisCount; ++Axis)
        {
            const int64 FactMin = int64(Row[1 + 2 * Axis]->AsNumber());
            const int64 FactMax = int64(Row[2 + 2 * Axis]->AsNumber());
            if (Point.Min[Axis] != FactMin || Point.Max[Axis] != FactMax)
            {
                return FString::Printf(TEXT("axis %d is %lld to %lld, facts %lld to %lld"), Axis, Point.Min[Axis],
                    Point.Max[Axis], FactMin, FactMax);
            }
        }
        const int64 FactOffset = int64(Row.Last()->AsNumber());
        if (Point.Offset != FactOffset)
        {
            return FString::Printf(TEXT("offset %lld, facts %lld"), Point.Offset, FactOffset);
        }
        return FString();
    }

    // One small root: plains.json, one point and the lists around them, with parts replaced (nullptr keeps the valid
    // one), and what the error of reading it must name.
    struct FBiomeTestCase
    {
        const TCHAR* Name;
        // Extra top-level fields of plains.json, each led by a comma.
        const TCHAR* BiomeExtra;
        // The fields of its effects object.
        const TCHAR* BiomeEffects;
        // The facts file's preset and its only row.
        const TCHAR* Preset;
        const TCHAR* Row;
        // Substrings of the error.
        const TCHAR* File;
        const TCHAR* Field;
    };

    // Writes the case's root and reads its overworld list.
    bool ReadBiomeTestRoot(const FBiomeTestCase& Case, FVoxelBiomeTable& Out, FString& OutError)
    {
        const FBiomeTestFolder Folder(Case.Name);
        const FString Plains = BiomeTestJson(
            FString::Printf(TEXT("{'temperature':0.8,'downfall':0.4,'has_precipitation':true%s,'effects':{%s}}"),
                Case.BiomeExtra ? Case.BiomeExtra : TEXT(""), Case.BiomeEffects ? Case.BiomeEffects : BiomeTestWater));
        const FString Facts = BiomeTestJson(FString::Printf(
            TEXT(
                "{'generated_by':'x','preset':'%s','quantization':10000,"
                "'columns':['biome','temperature','humidity','continentalness','erosion','depth','weirdness','offset'],"
                "'biomes':['minecraft:plains'],'points':[[%s]]}"),
            Case.Preset ? Case.Preset : BiomeTestPreset, Case.Row ? Case.Row : BiomeTestRow));
        if (!Folder.Write(TEXT("data/minecraft/worldgen/multi_noise_biome_source_parameter_list/overworld.json"),
                BiomeTestJson(TEXT("{'preset':'minecraft:overworld'}"))) ||
            !Folder.Write(TEXT("data/minecraft/worldgen/biome/plains.json"), Plains) ||
            !Folder.Write(TEXT("facts/biome_parameters.json"), Facts))
        {
            OutError = TEXT("the test root cannot be written");
            return false;
        }
        return VoxelWorldgen::ReadBiomeParameters(Folder.Root, TEXT("overworld"), Out, OutError);
    }

    // A biome's numbers as its JSON file gives them.
    struct FBiomeTestSpot
    {
        const TCHAR* Id;
        float Temperature;
        float Downfall;
        bool bPrecipitation;
        int32 Water;
        int32 Grass;
        int32 Foliage;
        int32 DryFoliage;
        EVoxelTemperatureModifier TemperatureModifier;
        EVoxelGrassColorModifier GrassModifier;
    };

    // One small surface rule root: material_rule/t.json (and u.json, when Other is not empty), read as rule "t".
    struct FSurfaceTestCase
    {
        const TCHAR* Name;
        FString Rule;
        FString Other;
        // Substrings of the error; nullptr skips one.
        const TCHAR* File;
        const TCHAR* Says;
    };

    // A condition rule: a stone block where Condition holds.
    FString MakeSurfaceTestRule(const TCHAR* Condition)
    {
        return BiomeTestJson(
            FString::Printf(TEXT("{'type':'minecraft:condition','if_true':%s,'then_run':{'type':'minecraft:block',"
                                 "'result_state':'minecraft:stone'}}"),
                Condition));
    }

    bool ReadSurfaceTestRoot(const FSurfaceTestCase& Case, FVoxelSurfaceRules& Out, FString& OutError)
    {
        const FBiomeTestFolder Folder(Case.Name);
        if (!Folder.Write(TEXT("data/minecraft/worldgen/material_rule/t.json"), Case.Rule) ||
            (!Case.Other.IsEmpty() && !Folder.Write(TEXT("data/minecraft/worldgen/material_rule/u.json"), Case.Other)))
        {
            OutError = TEXT("the test root cannot be written");
            return false;
        }
        return VoxelWorldgen::ReadSurfaceRules(Folder.Root, TEXT("t"), Out, OutError);
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelBiomeReadTest, "Unnamed.Voxel.Biome.Read",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelBiomeReadTest::RunTest(const FString& Parameters)
{
    const FString Root = VoxelWorldgen::GetReferenceRoot();
    FVoxelBiomeTable Table;
    FString Error;
    const bool bRead = VoxelWorldgen::ReadBiomeParameters(Root, TEXT("overworld"), Table, Error);
    if (!TestTrue(FString::Printf(TEXT("the overworld biome parameters read (%s)"), *Error), bRead))
    {
        return false;
    }
    TestTrue(TEXT("the preset is minecraft:overworld"), BiomeTestSame(Table.Preset, TEXT("minecraft:overworld")));
    TestEqual(TEXT("biomes"), Table.Biomes.Num(), BiomeTestBiomeCount);
    TestEqual(TEXT("points"), Table.Points.Num(), BiomeTestPointCount);
    FString Invalid;
    const bool bValid = Table.Validate(&Invalid);
    TestTrue(FString::Printf(TEXT("the table validates (%s)"), *Invalid), bValid);

    // Every biome is used by at least one point.
    TArray<int32> Uses;
    Uses.SetNumZeroed(Table.Biomes.Num());
    for (const FVoxelBiomeParameter& Point : Table.Points)
    {
        if (Uses.IsValidIndex(Point.Biome))
        {
            ++Uses[Point.Biome];
        }
    }
    for (int32 BiomeIndex = 0; BiomeIndex < Uses.Num(); ++BiomeIndex)
    {
        TestTrue(FString::Printf(TEXT("%s is used by a point"), *Table.Biomes[BiomeIndex].Name), Uses[BiomeIndex] > 0);
    }

    // Every point against facts/biome_parameters.json, read here without the reader.
    const TSharedPtr<FJsonValue> FactsValue =
        LoadBiomeTestJson(FPaths::Combine(Root, TEXT("facts/biome_parameters.json")));
    const TArray<TSharedPtr<FJsonValue>>* FactBiomeList = FindBiomeTestArray(FactsValue, TEXT("biomes"));
    const TArray<TSharedPtr<FJsonValue>>* FactPointList = FindBiomeTestArray(FactsValue, TEXT("points"));
    if (!TestTrue(TEXT("the facts hold biomes and points"), FactBiomeList && FactPointList))
    {
        return false;
    }
    TArray<FString> FactBiomes;
    for (const TSharedPtr<FJsonValue>& FactBiome : *FactBiomeList)
    {
        FactBiomes.Add(FactBiome->AsString());
    }
    TestEqual(TEXT("biomes in the facts"), FactBiomes.Num(), BiomeTestBiomeCount);
    TestEqual(TEXT("points in the facts"), FactPointList->Num(), BiomeTestPointCount);
    TestEqual(TEXT("points against the facts"), Table.Points.Num(), FactPointList->Num());
    int32 Differing = 0;
    const int32 Compared = FMath::Min(Table.Points.Num(), FactPointList->Num());
    for (int32 PointIndex = 0; PointIndex < Compared; ++PointIndex)
    {
        const FString Difference = DescribeBiomeTestPointDifference(
            Table, Table.Points[PointIndex], FactBiomes, (*FactPointList)[PointIndex]->AsArray());
        // The first few say what differs; the count says how many.
        if (!Difference.IsEmpty() && ++Differing <= 5)
        {
            AddError(FString::Printf(TEXT("point %d: %s"), PointIndex, *Difference));
        }
    }
    TestEqual(TEXT("points that differ from the facts"), Differing, 0);
    AddInfo(FString::Printf(TEXT("%d biomes and %d points compared with the facts"), Table.Biomes.Num(), Compared));

    // Spot values, from the biome files.
    const FBiomeTestSpot Spots[] = {
        {TEXT("minecraft:plains"), 0.8f, 0.4f, true, 0x3F76E4, INDEX_NONE, INDEX_NONE, INDEX_NONE,
            EVoxelTemperatureModifier::None, EVoxelGrassColorModifier::None},
        {TEXT("minecraft:badlands"), 2.0f, 0.0f, false, 0x3F76E4, 0x90814D, 0x9E814D, INDEX_NONE,
            EVoxelTemperatureModifier::None, EVoxelGrassColorModifier::None},
        {TEXT("minecraft:swamp"), 0.8f, 0.9f, true, 0x617B64, INDEX_NONE, 0x6A7039, 0x7B5334,
            EVoxelTemperatureModifier::None, EVoxelGrassColorModifier::Swamp},
        {TEXT("minecraft:dark_forest"), 0.7f, 0.8f, true, 0x3F76E4, INDEX_NONE, INDEX_NONE, 0x7B5334,
            EVoxelTemperatureModifier::None, EVoxelGrassColorModifier::DarkForest},
        {TEXT("minecraft:frozen_ocean"), 0.0f, 0.5f, true, 0x3938C9, INDEX_NONE, INDEX_NONE, INDEX_NONE,
            EVoxelTemperatureModifier::Frozen, EVoxelGrassColorModifier::None},
        {TEXT("minecraft:deep_frozen_ocean"), 0.5f, 0.5f, true, 0x3938C9, INDEX_NONE, INDEX_NONE, INDEX_NONE,
            EVoxelTemperatureModifier::Frozen, EVoxelGrassColorModifier::None},
    };
    for (const FBiomeTestSpot& Spot : Spots)
    {
        const int32 Found = Table.FindBiome(Spot.Id);
        if (!TestTrue(FString::Printf(TEXT("%s is in the table"), Spot.Id), Found != INDEX_NONE))
        {
            continue;
        }
        const FVoxelBiome& Spotted = Table.Biomes[Found];
        TestTrue(FString::Printf(TEXT("%s: the id is kept as written"), Spot.Id), BiomeTestSame(Spotted.Name, Spot.Id));
        TestTrue(
            FString::Printf(TEXT("%s: temperature %g, downfall %g"), Spot.Id, Spotted.Temperature, Spotted.Downfall),
            FMath::IsNearlyEqual(Spotted.Temperature, Spot.Temperature, 1e-6f) &&
                FMath::IsNearlyEqual(Spotted.Downfall, Spot.Downfall, 1e-6f));
        TestTrue(FString::Printf(TEXT("%s: precipitation"), Spot.Id), Spotted.bHasPrecipitation == Spot.bPrecipitation);
        TestTrue(FString::Printf(TEXT("%s: colours water %d, grass %d, foliage %d, dry foliage %d"), Spot.Id,
                     Spotted.WaterColor, Spotted.GrassColor, Spotted.FoliageColor, Spotted.DryFoliageColor),
            Spotted.WaterColor == Spot.Water && Spotted.GrassColor == Spot.Grass &&
                Spotted.FoliageColor == Spot.Foliage && Spotted.DryFoliageColor == Spot.DryFoliage);
        TestTrue(FString::Printf(TEXT("%s: temperature modifier"), Spot.Id),
            Spotted.TemperatureModifier == Spot.TemperatureModifier);
        TestTrue(FString::Printf(TEXT("%s: grass colour modifier"), Spot.Id),
            Spotted.GrassColorModifier == Spot.GrassModifier);
    }
    TArray<FString> Frozen;
    for (const FVoxelBiome& Each : Table.Biomes)
    {
        if (Each.TemperatureModifier == EVoxelTemperatureModifier::Frozen)
        {
            Frozen.Add(Each.Name);
        }
    }
    TestTrue(FString::Printf(TEXT("only the two frozen oceans are frozen (%s)"), *FString::Join(Frozen, TEXT(" "))),
        Frozen.Num() == 2 && BiomeTestHas(Frozen, TEXT("minecraft:frozen_ocean")) &&
            BiomeTestHas(Frozen, TEXT("minecraft:deep_frozen_ocean")));

    // ReadBiomes reuses the ids a table holds.
    const FString OnePlains[] = {TEXT("minecraft:plains")};
    const FString TwoPlains[] = {TEXT("minecraft:plains"), TEXT("minecraft:plains")};
    FVoxelBiomeTable Reused;
    FString ReuseError;
    const bool bFirstRead = VoxelWorldgen::ReadBiomes(Root, OnePlains, Reused, ReuseError);
    const bool bSecondRead = VoxelWorldgen::ReadBiomes(Root, OnePlains, Reused, ReuseError);
    TestTrue(FString::Printf(TEXT("plains reads twice (%s)"), *ReuseError), bFirstRead && bSecondRead);
    TestEqual(TEXT("plains read twice is one biome"), Reused.Biomes.Num(), 1);
    FVoxelBiomeTable Listed;
    const bool bListed = VoxelWorldgen::ReadBiomes(Root, TwoPlains, Listed, ReuseError);
    TestTrue(FString::Printf(TEXT("plains listed twice reads (%s)"), *ReuseError), bListed);
    TestEqual(TEXT("plains listed twice is one biome"), Listed.Biomes.Num(), 1);

    // Strictness: small roots under Saved/Automation, each with one thing wrong. Every error names the file and the
    // field.
    const FBiomeTestCase Valid = {TEXT("biome-valid"), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    FVoxelBiomeTable Tiny;
    FString TinyError;
    const bool bTiny = ReadBiomeTestRoot(Valid, Tiny, TinyError);
    TestTrue(FString::Printf(TEXT("the unmodified small root reads (%s)"), *TinyError), bTiny);
    TestEqual(TEXT("small root biomes"), Tiny.Biomes.Num(), 1);
    TestEqual(TEXT("small root points"), Tiny.Points.Num(), 1);
    const FBiomeTestCase Cases[] = {
        {TEXT("biome-music"), TEXT(",'music':{}"), nullptr, nullptr, nullptr, TEXT("biome/plains.json"), TEXT("music")},
        {TEXT("biome-no-water"), nullptr, TEXT(""), nullptr, nullptr, TEXT("biome/plains.json"),
            TEXT("effects.water_color")},
        {TEXT("biome-short-water"), nullptr, TEXT("'water_color':'#12345'"), nullptr, nullptr,
            TEXT("biome/plains.json"), TEXT("effects.water_color")},
        {TEXT("biome-sky"), nullptr, TEXT("'water_color':'#3f76e4','sky_color':'#78a7ff'"), nullptr, nullptr,
            TEXT("biome/plains.json"), TEXT("effects.sky_color")},
        {TEXT("facts-preset"), nullptr, nullptr, TEXT("minecraft:nether"), nullptr, TEXT("facts/biome_parameters.json"),
            TEXT("preset")},
        {TEXT("facts-row"), nullptr, nullptr, nullptr,
            TEXT("0,-10000,10000,-10000,10000,-10000,10000,-10000,10000,0,0,-10000,10000"),
            TEXT("facts/biome_parameters.json"), TEXT("points[0]")},
        {TEXT("biome-icy"), TEXT(",'temperature_modifier':'icy'"), nullptr, nullptr, nullptr, TEXT("biome/plains.json"),
            TEXT("temperature_modifier")},
    };
    for (const FBiomeTestCase& Case : Cases)
    {
        FVoxelBiomeTable Bad;
        FString Reason;
        const bool bAccepted = ReadBiomeTestRoot(Case, Bad, Reason);
        TestFalse(FString::Printf(TEXT("%s is refused"), Case.Name), bAccepted);
        TestTrue(FString::Printf(TEXT("%s: the error names %s: %s"), Case.Name, Case.File, *Reason),
            Reason.Contains(Case.File, ESearchCase::CaseSensitive));
        TestTrue(FString::Printf(TEXT("%s: the error names %s: %s"), Case.Name, Case.Field, *Reason),
            Reason.Contains(Case.Field, ESearchCase::CaseSensitive));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurfaceReadTest, "Unnamed.Voxel.Surface.Read",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSurfaceReadTest::RunTest(const FString& Parameters)
{
    const FString Root = VoxelWorldgen::GetReferenceRoot();
    FVoxelSurfaceRules Rules;
    FString Error;
    const bool bRead = VoxelWorldgen::ReadSurfaceRules(Root, TEXT("overworld"), Rules, Error);
    if (!TestTrue(FString::Printf(TEXT("the overworld surface rules read (%s)"), *Error), bRead))
    {
        return false;
    }
    FString Invalid;
    const bool bValid = Rules.Validate(&Invalid);
    TestTrue(FString::Printf(TEXT("the tree validates (%s)"), *Invalid), bValid);
    TestTrue(TEXT("the root is minecraft:overworld"),
        Rules.Root != INDEX_NONE && Rules.Root == Rules.FindRule(TEXT("minecraft:overworld")));

    // Counts of what overworld.json reaches (a named rule or condition is one node however often it is named), and
    // every op of both enums among them.
    const int32 ExpectedRules[] = {35, 123, 69, 2, 2};
    const int32 ExpectedConditions[] = {43, 32, 2, 10, 2, 5, 4, 5, 3, 1};
    static_assert(int32(UE_ARRAY_COUNT(ExpectedRules)) == int32(EVoxelSurfaceRuleOp::Count), "one count per rule op");
    static_assert(int32(UE_ARRAY_COUNT(ExpectedConditions)) == int32(EVoxelSurfaceConditionOp::Count),
        "one count per condition op");
    int32 RuleCounts[int32(EVoxelSurfaceRuleOp::Count)] = {};
    for (const FVoxelSurfaceRule& Rule : Rules.Rules)
    {
        if (int32(Rule.Op) < int32(EVoxelSurfaceRuleOp::Count))
        {
            ++RuleCounts[int32(Rule.Op)];
        }
    }
    int32 ConditionCounts[int32(EVoxelSurfaceConditionOp::Count)] = {};
    for (const FVoxelSurfaceCondition& Condition : Rules.Conditions)
    {
        if (int32(Condition.Op) < int32(EVoxelSurfaceConditionOp::Count))
        {
            ++ConditionCounts[int32(Condition.Op)];
        }
    }
    TestEqual(TEXT("rules"), Rules.Rules.Num(), 231);
    TestEqual(TEXT("conditions"), Rules.Conditions.Num(), 107);
    for (int32 RuleOp = 0; RuleOp < int32(EVoxelSurfaceRuleOp::Count); ++RuleOp)
    {
        const TCHAR* RuleName = FVoxelSurfaceRules::GetRuleOpName(EVoxelSurfaceRuleOp(RuleOp));
        TestEqual(FString::Printf(TEXT("%s rules"), RuleName), RuleCounts[RuleOp], ExpectedRules[RuleOp]);
        TestTrue(FString::Printf(TEXT("the %s rule op appears"), RuleName), RuleCounts[RuleOp] > 0);
    }
    for (int32 ConditionOp = 0; ConditionOp < int32(EVoxelSurfaceConditionOp::Count); ++ConditionOp)
    {
        const TCHAR* ConditionName = FVoxelSurfaceRules::GetConditionOpName(EVoxelSurfaceConditionOp(ConditionOp));
        TestEqual(FString::Printf(TEXT("%s conditions"), ConditionName), ConditionCounts[ConditionOp],
            ExpectedConditions[ConditionOp]);
        TestTrue(FString::Printf(TEXT("the %s condition op appears"), ConditionName), ConditionCounts[ConditionOp] > 0);
    }

    // The named rules are every file under material_rule; the named conditions every file under material_condition
    // but under_ceiling, which nothing uses.
    const TArray<FString> RuleIds = ListBiomeTestIds(Root, TEXT("material_rule"));
    TestEqual(TEXT("material_rule files"), RuleIds.Num(), 37);
    TestEqual(TEXT("named rules"), Rules.NamedRules.Num(), 37);
    for (const FString& RuleId : RuleIds)
    {
        TestTrue(FString::Printf(TEXT("%s is a named rule"), *RuleId), Rules.FindRule(RuleId) != INDEX_NONE);
    }
    const TArray<FString> ConditionIds = ListBiomeTestIds(Root, TEXT("material_condition"));
    TestEqual(TEXT("material_condition files"), ConditionIds.Num(), 8);
    const TCHAR* const NamedConditions[] = {TEXT("minecraft:deep_under_floor"), TEXT("minecraft:not_under_deep_water"),
        TEXT("minecraft:not_underwater"), TEXT("minecraft:on_ceiling"), TEXT("minecraft:on_floor"),
        TEXT("minecraft:under_floor"), TEXT("minecraft:very_deep_under_floor")};
    TestEqual(TEXT("named conditions"), Rules.NamedConditions.Num(), int32(UE_ARRAY_COUNT(NamedConditions)));
    for (const TCHAR* NamedCondition : NamedConditions)
    {
        TestTrue(FString::Printf(TEXT("%s is a named condition"), NamedCondition),
            Rules.FindCondition(NamedCondition) != INDEX_NONE);
    }
    TestTrue(TEXT("under_ceiling is not read"), Rules.FindCondition(TEXT("minecraft:under_ceiling")) == INDEX_NONE);

    // What the nodes name: noises, biomes, blocks, density functions.
    TArray<FString> NoiseIds;
    TArray<FString> BiomeIds;
    for (const FVoxelSurfaceCondition& Condition : Rules.Conditions)
    {
        if (Condition.Op == EVoxelSurfaceConditionOp::NoiseThreshold)
        {
            BiomeTestAddUnique(NoiseIds, BiomeTestString(Rules, Condition.First));
        }
        else if (Condition.Op == EVoxelSurfaceConditionOp::Biome)
        {
            for (int32 Entry = 0; Entry < Condition.Count; ++Entry)
            {
                BiomeTestAddUnique(BiomeIds, BiomeTestString(Rules, Condition.First + Entry));
            }
        }
    }
    TArray<FString> BlockIds;
    TArray<FString> FunctionIds;
    TArray<FString> Veins;
    int32 OreVeins = 0;
    for (const FVoxelSurfaceRule& Rule : Rules.Rules)
    {
        if (Rule.Op == EVoxelSurfaceRuleOp::Block)
        {
            BiomeTestAddUnique(BlockIds, BiomeTestString(Rules, Rule.Block));
        }
        else if (Rule.Op == EVoxelSurfaceRuleOp::OreVein)
        {
            ++OreVeins;
            const FString Ore = BiomeTestString(Rules, Rule.Block);
            const FString Raw = BiomeTestString(Rules, Rule.RawOreBlock);
            const FString Filler = BiomeTestString(Rules, Rule.FillerBlock);
            BiomeTestAddUnique(BlockIds, Ore);
            BiomeTestAddUnique(BlockIds, Raw);
            BiomeTestAddUnique(BlockIds, Filler);
            BiomeTestAddUnique(FunctionIds, BiomeTestString(Rules, Rule.Density));
            BiomeTestAddUnique(FunctionIds, BiomeTestString(Rules, Rule.Richness));
            BiomeTestAddUnique(FunctionIds, BiomeTestString(Rules, Rule.FillerGap));
            Veins.Add(Ore + TEXT(" ") + Raw + TEXT(" ") + Filler);
            TestTrue(FString::Printf(TEXT("%s: raw ore chance %g"), *Ore, Rule.RawOreChance),
                FMath::IsNearlyEqual(Rule.RawOreChance, 0.02));
        }
    }

    // The noise_threshold noises exist as noise files.
    const TCHAR* const ExpectedNoises[] = {TEXT("minecraft:calcite"), TEXT("minecraft:gravel"), TEXT("minecraft:ice"),
        TEXT("minecraft:packed_ice"), TEXT("minecraft:powder_snow"), TEXT("minecraft:small_patch"),
        TEXT("minecraft:sulfur_cave_gradient"), TEXT("minecraft:surface"), TEXT("minecraft:surface_swamp")};
    TestEqual(TEXT("distinct noises"), NoiseIds.Num(), int32(UE_ARRAY_COUNT(ExpectedNoises)));
    for (const TCHAR* ExpectedNoise : ExpectedNoises)
    {
        TestTrue(FString::Printf(TEXT("%s is a noise_threshold noise"), ExpectedNoise),
            BiomeTestHas(NoiseIds, ExpectedNoise));
    }
    FVoxelDensityGraph NoiseGraph;
    FString NoiseError;
    const bool bNoises = VoxelWorldgen::ReadReferences(Root, {}, NoiseIds, NoiseGraph, NoiseError);
    TestTrue(FString::Printf(TEXT("the noises resolve (%s)"), *NoiseError), bNoises);
    for (const FString& NoiseId : NoiseIds)
    {
        TestTrue(FString::Printf(TEXT("%s is in the graph"), *NoiseId), NoiseGraph.FindNoise(NoiseId) != INDEX_NONE);
    }

    // The ore veins: two, and their density functions resolve.
    TestEqual(TEXT("ore veins"), OreVeins, 2);
    TestTrue(FString::Printf(TEXT("the copper vein (%s)"), *FString::Join(Veins, TEXT(", "))),
        BiomeTestHas(Veins, TEXT("minecraft:copper_ore minecraft:raw_copper_block minecraft:granite")));
    TestTrue(FString::Printf(TEXT("the iron vein (%s)"), *FString::Join(Veins, TEXT(", "))),
        BiomeTestHas(Veins, TEXT("minecraft:deepslate_iron_ore minecraft:raw_iron_block minecraft:tuff")));
    const TCHAR* const ExpectedFunctions[] = {TEXT("minecraft:overworld/ore_vein/copper_density"),
        TEXT("minecraft:overworld/ore_vein/iron_density"), TEXT("minecraft:overworld/ore_vein/richness"),
        TEXT("minecraft:overworld/ore_vein/gap")};
    TestEqual(TEXT("distinct ore vein functions"), FunctionIds.Num(), int32(UE_ARRAY_COUNT(ExpectedFunctions)));
    for (const TCHAR* ExpectedFunction : ExpectedFunctions)
    {
        TestTrue(FString::Printf(TEXT("%s is an ore vein function"), ExpectedFunction),
            BiomeTestHas(FunctionIds, ExpectedFunction));
    }
    FVoxelDensityGraph OreGraph;
    FString OreError;
    const bool bFunctions = VoxelWorldgen::ReadReferences(Root, FunctionIds, {}, OreGraph, OreError);
    TestTrue(FString::Printf(TEXT("the ore vein functions resolve (%s)"), *OreError), bFunctions);
    for (const FString& FunctionId : FunctionIds)
    {
        TestTrue(
            FString::Printf(TEXT("%s is in the graph"), *FunctionId), OreGraph.FindFunction(FunctionId) != INDEX_NONE);
    }

    // Every biome a Biome condition names is one of the list's 56.
    FVoxelBiomeTable Table;
    FString TableError;
    const bool bTable = VoxelWorldgen::ReadBiomeParameters(Root, TEXT("overworld"), Table, TableError);
    if (TestTrue(FString::Printf(TEXT("the biome list reads (%s)"), *TableError), bTable))
    {
        TestEqual(TEXT("biomes in the list"), Table.Biomes.Num(), BiomeTestBiomeCount);
        for (const FString& BiomeId : BiomeIds)
        {
            TestTrue(
                FString::Printf(TEXT("%s is a biome of the list"), *BiomeId), Table.FindBiome(BiomeId) != INDEX_NONE);
        }
    }
    TestEqual(TEXT("distinct biome ids in Biome conditions"), BiomeIds.Num(), 29);

    // The blocks: 32 ids, all in the minecraft namespace.
    TestEqual(TEXT("distinct block ids"), BlockIds.Num(), 32);
    for (const FString& BlockId : BlockIds)
    {
        TestTrue(FString::Printf(TEXT("%s is in the minecraft namespace"), *BlockId),
            BlockId.StartsWith(TEXT("minecraft:"), ESearchCase::CaseSensitive));
    }
    TestTrue(TEXT("powder_snow is a block"), BiomeTestHas(BlockIds, TEXT("minecraft:powder_snow")));
    TestTrue(TEXT("snow_block is a block"), BiomeTestHas(BlockIds, TEXT("minecraft:snow_block")));
    AddInfo(FString::Printf(TEXT("%d rules, %d conditions, %d named rules, %d named conditions, %d blocks, %d biomes, "
                                 "%d noises"),
        Rules.Rules.Num(), Rules.Conditions.Num(), Rules.NamedRules.Num(), Rules.NamedConditions.Num(), BlockIds.Num(),
        BiomeIds.Num(), NoiseIds.Num()));

    // Strictness: small roots under Saved/Automation, each with one thing wrong, read as the rule "t".
    const FSurfaceTestCase Valid = {TEXT("surface-valid"),
        BiomeTestJson(TEXT("{'type':'minecraft:sequence','sequence':["
                           "{'type':'minecraft:condition','if_true':{'type':'minecraft:biome',"
                           "'biome_is':'minecraft:plains'},'then_run':{'type':'minecraft:block',"
                           "'result_state':'minecraft:grass_block'}},"
                           "{'type':'minecraft:block','result_state':'minecraft:stone'}]}")),
        FString(), nullptr, nullptr};
    FVoxelSurfaceRules Tiny;
    FString TinyError;
    const bool bTiny = ReadSurfaceTestRoot(Valid, Tiny, TinyError);
    if (TestTrue(FString::Printf(TEXT("the small valid tree reads (%s)"), *TinyError), bTiny))
    {
        TestEqual(TEXT("small tree rules"), Tiny.Rules.Num(), 4);
        TestEqual(TEXT("small tree conditions"), Tiny.Conditions.Num(), 1);
        TestTrue(TEXT("the small tree's root is the sequence rule t"),
            Tiny.Root != INDEX_NONE && Tiny.Root == Tiny.FindRule(TEXT("minecraft:t")) &&
                Tiny.Rules[Tiny.Root].Op == EVoxelSurfaceRuleOp::Sequence && Tiny.Rules[Tiny.Root].Count == 2);
        TestTrue(TEXT("a bare biome id is a list of one"),
            Tiny.Conditions.Num() == 1 && Tiny.Conditions[0].Op == EVoxelSurfaceConditionOp::Biome &&
                Tiny.Conditions[0].Count == 1 &&
                BiomeTestSame(BiomeTestString(Tiny, Tiny.Conditions[0].First), TEXT("minecraft:plains")));
    }
    const FSurfaceTestCase Cases[] = {
        {TEXT("surface-magic"), BiomeTestJson(TEXT("{'type':'minecraft:magic'}")), FString(),
            TEXT("material_rule/t.json"), TEXT("type")},
        {TEXT("surface-colour"),
            MakeSurfaceTestRule(TEXT("{'type':'minecraft:stone_depth','offset':0,'add_surface_depth':false,"
                                     "'secondary_depth_range':0,'surface_type':'floor','colour':1}")),
            FString(), TEXT("material_rule/t.json"), TEXT("if_true.colour")},
        {TEXT("surface-no-offset"),
            MakeSurfaceTestRule(TEXT("{'type':'minecraft:stone_depth','add_surface_depth':false,"
                                     "'secondary_depth_range':0,'surface_type':'floor'}")),
            FString(), TEXT("material_rule/t.json"), TEXT("if_true.offset")},
        {TEXT("surface-cycle"), BiomeTestJson(TEXT("{'type':'minecraft:sequence','sequence':['minecraft:u']}")),
            BiomeTestJson(TEXT("{'type':'minecraft:sequence','sequence':['minecraft:t']}")), nullptr,
            TEXT("references itself")},
        {TEXT("surface-anchor"),
            MakeSurfaceTestRule(TEXT("{'type':'minecraft:y_above','anchor':{'absolute':1,'below_top':2},"
                                     "'surface_depth_multiplier':0,'add_stone_depth':false}")),
            FString(), TEXT("material_rule/t.json"), TEXT("anchor")},
        {TEXT("surface-is-3d"),
            MakeSurfaceTestRule(TEXT("{'type':'minecraft:noise_threshold','noise':'minecraft:surface',"
                                     "'min_threshold':-0.1,'max_threshold':0.1,'is_3d':'yes'}")),
            FString(), TEXT("material_rule/t.json"), TEXT("is_3d")},
        {TEXT("surface-empty-sequence"), BiomeTestJson(TEXT("{'type':'minecraft:sequence','sequence':[]}")), FString(),
            TEXT("material_rule/t.json"), TEXT("sequence")},
        {TEXT("surface-missing"), MakeSurfaceTestRule(TEXT("'minecraft:missing'")), FString(),
            TEXT("material_condition/missing.json"), nullptr},
    };
    for (const FSurfaceTestCase& Case : Cases)
    {
        FVoxelSurfaceRules Bad;
        FString Reason;
        const bool bAccepted = ReadSurfaceTestRoot(Case, Bad, Reason);
        TestFalse(FString::Printf(TEXT("%s is refused"), Case.Name), bAccepted);
        if (Case.File)
        {
            TestTrue(FString::Printf(TEXT("%s: the error names %s: %s"), Case.Name, Case.File, *Reason),
                Reason.Contains(Case.File, ESearchCase::CaseSensitive));
        }
        if (Case.Says)
        {
            TestTrue(FString::Printf(TEXT("%s: the error says %s: %s"), Case.Name, Case.Says, *Reason),
                Reason.Contains(Case.Says, ESearchCase::CaseSensitive));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurfaceRoundTripTest, "Unnamed.Voxel.Surface.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSurfaceRoundTripTest::RunTest(const FString& Parameters)
{
    // Every named rule and condition of the overworld tree, written back, equals its file in the reference.
    const FString Root = VoxelWorldgen::GetReferenceRoot();
    const FString Worldgen = FPaths::Combine(Root, TEXT("data/minecraft/worldgen"));
    FVoxelSurfaceRules Rules;
    FString Error;
    const bool bRead = VoxelWorldgen::ReadSurfaceRules(Root, TEXT("overworld"), Rules, Error);
    if (!TestTrue(FString::Printf(TEXT("the overworld surface rules read (%s)"), *Error), bRead))
    {
        return false;
    }
    int32 Compared = 0;
    int32 Differences = 0;
    const auto Compare = [this, &Compared, &Differences](
                             const FString& File, const FString& Label, const TSharedPtr<FJsonValue>& Written)
    {
        ++Compared;
        const TSharedPtr<FJsonValue> Expected = LoadBiomeTestJson(File);
        if (!Expected.IsValid())
        {
            AddError(FString::Printf(TEXT("%s: %s is not one JSON value"), *Label, *File));
            ++Differences;
        }
        else if (!Written.IsValid() || !FJsonValue::CompareEqual(*Expected, *Written))
        {
            AddError(FString::Printf(TEXT("%s differs from %s. File: %s Written: %s"), *Label, *File,
                *BiomeTestJsonText(Expected), *BiomeTestJsonText(Written)));
            ++Differences;
        }
    };
    for (const FVoxelSurfaceName& Entry : Rules.NamedRules)
    {
        const FString File = FPaths::Combine(Worldgen, TEXT("material_rule"), Entry.Name.RightChop(10) + TEXT(".json"));
        Compare(File, Entry.Name, VoxelWorldgen::WriteSurfaceRule(Rules, Entry.Index, true));
    }
    const int32 RulesCompared = Compared;
    for (const FVoxelSurfaceName& Entry : Rules.NamedConditions)
    {
        const FString File =
            FPaths::Combine(Worldgen, TEXT("material_condition"), Entry.Name.RightChop(10) + TEXT(".json"));
        Compare(File, Entry.Name, VoxelWorldgen::WriteSurfaceCondition(Rules, Entry.Index, true));
    }
    TestEqual(TEXT("named rules compared"), RulesCompared, 37);
    TestEqual(TEXT("named conditions compared"), Compared - RulesCompared, 7);
    TestEqual(TEXT("files compared"), Compared, 44);
    TestEqual(TEXT("files written back unchanged"), Differences, 0);
    AddInfo(FString::Printf(
        TEXT("%d named rules and %d named conditions written back"), RulesCompared, Compared - RulesCompared));
    return true;
}

#endif
