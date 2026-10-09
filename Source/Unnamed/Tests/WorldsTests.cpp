#if WITH_DEV_AUTOMATION_TESTS
#include "Blocks/VoxelBlockTable.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Menu/MenuApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Random/VoxelSeed.h"
#include "UnnamedProjectSettings.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "Worlds/UnnamedSavedWorlds.h"
#include "Menu/MenuGameMode.h"
#include "Menu/MenuPlayerController.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationGameMode.h"
#include "Foundation/FoundationPawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldRoutingTest, "Unnamed.Worlds.RoutingAndIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldRoutingTest::RunTest(const FString& Parameters)
{
    UMenuApplication* Service = NewObject<UMenuApplication>();
    Service->Initialize(nullptr, nullptr);
    // Design references, retired worlds, paths, commands and the real ID without a menu controller.
    for (const FString Id : {TEXT("fictional-forest"), TEXT("fictional-build"), TEXT("super-flat"), TEXT("foundation"),
             TEXT("interaction-lab"), TEXT("/Game/Maps/L_Playground"), TEXT("playground?listen"),
             TEXT("open /Game/Menu/L_Menu"), TEXT(""), TEXT("playground")})
    {
        TSharedPtr<FJsonObject> Reply;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Service->RequestOpenWorld(Id)), Reply);
        TestTrue(TEXT("Structured travel reply"), Reply.IsValid());
        if (!Reply) return false;
        TestFalse(TEXT("Unknown IDs or missing controller cannot travel: ") + Id, Reply->GetBoolField(TEXT("ok")));
        TestFalse(TEXT("Rejected request never queues travel"), Reply->GetBoolField(TEXT("pendingWorld")));
        TestFalse(TEXT("Rejected request never issues travel"), Reply->GetBoolField(TEXT("travelIssued")));
        Service->Tick();
    }
    TestTrue(TEXT("Playground asset exists"),
        FPackageName::DoesPackageExist(UUnnamedProjectSettings::GetWorldMapPackage(TEXT("playground"))));
    TestTrue(TEXT("Menu asset retained"), FPackageName::DoesPackageExist(UUnnamedProjectSettings::GetMenuMapPackage()));
    const AFoundationGameMode* WorldMode = GetDefault<AFoundationGameMode>();
    TestTrue(TEXT("World plays the first-person pawn"), WorldMode->DefaultPawnClass == AFoundationPawn::StaticClass());
    TestTrue(TEXT("World uses its own controller, not the menu's"),
        WorldMode->PlayerControllerClass == AFoundationController::StaticClass());
    TestTrue(TEXT("Existing menu keeps its controller"),
        GetDefault<AMenuGameMode>()->PlayerControllerClass == AMenuPlayerController::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldCatalogTest, "Unnamed.Worlds.Catalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldCatalogTest::RunTest(const FString& Parameters)
{
    const UUnnamedProjectSettings& Project = UUnnamedProjectSettings::Get();
    TestEqual(TEXT("One world for every feature"), Project.Worlds.Num(), 1);
    TestNotNull(TEXT("Exact catalog ID resolves"), Project.FindWorld(TEXT("playground")));
    TestNull(TEXT("IDs are case-sensitive"), Project.FindWorld(TEXT("Playground")));
    TestNull(TEXT("Paths are never IDs"), Project.FindWorld(TEXT("/Game/Maps/L_Playground")));
    for (const TCHAR* Retired : {TEXT("foundation"), TEXT("super-flat"), TEXT("interaction-lab")})
        TestNull(TEXT("Retired world stays retired: ") + FString(Retired), Project.FindWorld(Retired));
    for (const FUnnamedWorldEntry& World : Project.Worlds)
        TestTrue(TEXT("Catalog map exists: ") + World.Id.ToString(),
            FPackageName::DoesPackageExist(World.Map.GetLongPackageName()));
    TestNotNull(TEXT("Dev grid material loads"), Project.DevGridMaterial.LoadSynchronous());
    TestTrue(TEXT("Every input asset loads"),
        Project.OnFootContext.LoadSynchronous() && Project.FoundationActionsContext.LoadSynchronous() &&
            Project.MoveAction.LoadSynchronous() && Project.LookAction.LoadSynchronous() &&
            Project.SprintAction.LoadSynchronous() && Project.CrouchAction.LoadSynchronous() &&
            Project.JumpAction.LoadSynchronous());

    UMenuApplication* Service = NewObject<UMenuApplication>();
    TSharedPtr<FJsonObject> State;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Service->GetState()), State);
    const TArray<TSharedPtr<FJsonValue>>* Worlds = nullptr;
    TestTrue(TEXT("State lists the catalog for the page"), State && State->TryGetArrayField(TEXT("worlds"), Worlds));
    if (!Worlds) return false;
    TestEqual(TEXT("Page receives every catalog world"), Worlds->Num(), Project.Worlds.Num());
    for (int32 Index = 0; Index < Worlds->Num(); ++Index)
        TestEqual(TEXT("Catalog order and IDs"), (*Worlds)[Index]->AsObject()->GetStringField(TEXT("id")),
            Project.Worlds[Index].Id.ToString());
    const TArray<TSharedPtr<FJsonValue>>* Saved = nullptr;
    TestTrue(
        TEXT("Saved worlds are listed apart from the catalog"), State->TryGetArrayField(TEXT("savedWorlds"), Saved));
    return true;
}

namespace
{
    // The Create screen's default draft (MenuWeb's newDraft) with a seed and two of the game rules.
    const TCHAR* const SavedWorldDraft = TEXT(
        R"({"name":"Test World","gameMode":"Survival","difficulty":"Normal","commands":false,"type":"Default",)"
        R"("seed":"hello","structures":true,"bonus":false,"biome":"minecraft:plains",)"
        R"("flatBiome":"minecraft:plains","flat":[{"block":"minecraft:grass_block","height":1},)"
        R"({"block":"minecraft:dirt","height":2},{"block":"minecraft:bedrock","height":1}],)"
        R"("experiments":{"trade_rebalance":true},"rules":{"natural_health_regeneration":false,"spawn_radius":10}})");

    // A JSON document (the draft by default) with one change.
    FString EditSavedWorldDraft(TFunctionRef<void(FJsonObject&)> Edit, const FString& Base = SavedWorldDraft)
    {
        TSharedPtr<FJsonObject> Draft;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Base), Draft);
        Edit(*Draft);
        FString Json;
        FJsonSerializer::Serialize(Draft.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
        return Json;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWorldSavedTest, "Unnamed.Worlds.Saved", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldSavedTest::RunTest(const FString& Parameters)
{
    // Ids: kebab-case ASCII from the name, never a catalog, retired or device name, unique, safe as a folder.
    const auto Free = [](const FString&) { return false; };
    TestEqual(TEXT("Name to id"), UnnamedSavedWorlds::MakeId(TEXT("My World!"), Free), FString(TEXT("my-world")));
    TestEqual(TEXT("Runs of other characters become one dash"),
        UnnamedSavedWorlds::MakeId(TEXT("  --Hello__World 2--  "), Free), FString(TEXT("hello-world-2")));
    TestEqual(
        TEXT("Nothing usable: world"), UnnamedSavedWorlds::MakeId(TEXT("\u4e16\u754c"), Free), FString(TEXT("world")));
    TestEqual(TEXT("A catalog id is never reused"), UnnamedSavedWorlds::MakeId(TEXT("Playground"), Free),
        FString(TEXT("playground-2")));
    TestEqual(TEXT("A retired id is never reused"), UnnamedSavedWorlds::MakeId(TEXT("Super Flat"), Free),
        FString(TEXT("super-flat-2")));
    TestEqual(TEXT("No Windows device names"), UnnamedSavedWorlds::MakeId(TEXT("CON"), Free), FString(TEXT("con-2")));
    TestEqual(TEXT("Taken ids get the next number"),
        UnnamedSavedWorlds::MakeId(
            TEXT("My World"), [](const FString& Id) { return Id == TEXT("my-world") || Id == TEXT("my-world-2"); }),
        FString(TEXT("my-world-3")));
    TestTrue(TEXT("Long names make short ids"), UnnamedSavedWorlds::MakeId(FString::ChrN(100, 'a'), Free).Len() <= 32);
    for (const TCHAR* Valid : {TEXT("a"), TEXT("my-world-2"), TEXT("w0rld")})
        TestTrue(FString(TEXT("Valid id: ")) + Valid, UnnamedSavedWorlds::IsValidId(Valid));
    for (const TCHAR* Invalid : {TEXT(""), TEXT("-a"), TEXT("a-"), TEXT("a--b"), TEXT("A"), TEXT("a_b"), TEXT("../x"),
             TEXT("a/b"), TEXT("a b"), TEXT("C:"), TEXT("playground?listen")})
        TestFalse(FString(TEXT("Invalid id: ")) + Invalid, UnnamedSavedWorlds::IsValidId(Invalid));
    TestFalse(TEXT("At most 48 characters"), UnnamedSavedWorlds::IsValidId(FString::ChrN(49, 'a')));

    // Drafts: every field is checked; Hardcore forces Hard with no commands or bonus chest.
    FUnnamedSavedWorld World;
    FString Error;
    TestTrue(TEXT("The default draft parses"), UnnamedSavedWorlds::ParseDraft(SavedWorldDraft, World, Error));
    TestTrue(TEXT("...with its settings"),
        World.Name == TEXT("Test World") && World.SeedText == TEXT("hello") && World.FlatLayers.Num() == 3 &&
            World.FlatLayers[0].Block == TEXT("minecraft:grass_block") &&
            !World.GetRule(TEXT("natural_health_regeneration"), true) && World.GetRule(TEXT("absent_rule"), true));
    const FString Hardcore = EditSavedWorldDraft(
        [](FJsonObject& Draft)
        {
            Draft.SetStringField(TEXT("gameMode"), TEXT("Hardcore"));
            Draft.SetStringField(TEXT("difficulty"), TEXT("Easy"));
            Draft.SetBoolField(TEXT("commands"), true);
            Draft.SetBoolField(TEXT("bonus"), true);
        });
    TestTrue(TEXT("Hardcore parses"), UnnamedSavedWorlds::ParseDraft(Hardcore, World, Error));
    TestTrue(TEXT("...as Hard, without commands or a bonus chest"),
        World.IsHardcore() && World.Difficulty == TEXT("Hard") && !World.bCommands && !World.bBonusChest);
    const TPair<const TCHAR*, TFunction<void(FJsonObject&)>> Rejected[] = {
        {TEXT("a blank name"), [](FJsonObject& D) { D.SetStringField(TEXT("name"), TEXT("   ")); }},
        {TEXT("a 129-character name"), [](FJsonObject& D) { D.SetStringField(TEXT("name"), FString::ChrN(129, 'n')); }},
        {TEXT("a control character"),
            [](FJsonObject& D) { D.SetStringField(TEXT("name"), FString(TEXT("a")) + TCHAR(1) + TEXT("b")); }},
        {TEXT("a 129-character seed"), [](FJsonObject& D) { D.SetStringField(TEXT("seed"), FString::ChrN(129, '7')); }},
        {TEXT("an unknown mode"), [](FJsonObject& D) { D.SetStringField(TEXT("gameMode"), TEXT("Spectator")); }},
        {TEXT("an unknown difficulty"), [](FJsonObject& D) { D.SetStringField(TEXT("difficulty"), TEXT("normal")); }},
        {TEXT("an unknown type"), [](FJsonObject& D) { D.SetStringField(TEXT("type"), TEXT("Debug Mode")); }},
        {TEXT("a missing switch"), [](FJsonObject& D) { D.RemoveField(TEXT("structures")); }},
        {TEXT("a bad biome"), [](FJsonObject& D) { D.SetStringField(TEXT("biome"), TEXT("plains")); }},
        {TEXT("a layer of height 0"),
            [](FJsonObject& D)
            {
                const auto Layer = MakeShared<FJsonObject>();
                Layer->SetStringField(TEXT("block"), TEXT("minecraft:stone"));
                Layer->SetNumberField(TEXT("height"), 0);
                D.SetArrayField(TEXT("flat"), {MakeShared<FJsonValueObject>(Layer)});
            }},
        {TEXT("a bad rule id"),
            [](FJsonObject& D) { D.GetObjectField(TEXT("rules"))->SetBoolField(TEXT("Bad-Key"), true); }},
        {TEXT("a text rule value"),
            [](FJsonObject& D) { D.GetObjectField(TEXT("rules"))->SetStringField(TEXT("spawn_radius"), TEXT("ten")); }},
        {TEXT("a rule beyond 32 bits"),
            [](FJsonObject& D) { D.GetObjectField(TEXT("rules"))->SetNumberField(TEXT("spawn_radius"), 1e12); }},
        {TEXT("an experiment number"),
            [](FJsonObject& D) { D.GetObjectField(TEXT("experiments"))->SetNumberField(TEXT("trade_rebalance"), 1); }},
    };
    for (const auto& [Why, Edit] : Rejected)
        TestFalse(FString(TEXT("Rejected: ")) + Why,
            UnnamedSavedWorlds::ParseDraft(EditSavedWorldDraft(Edit), World, Error) || Error.IsEmpty());
    TestFalse(TEXT("Rejected: not JSON"), UnnamedSavedWorlds::ParseDraft(TEXT("{"), World, Error));
    const auto Unobtainium = [](FJsonObject& D)
    {
        const auto Layer = MakeShared<FJsonObject>();
        Layer->SetStringField(TEXT("block"), TEXT("minecraft:unobtainium"));
        Layer->SetNumberField(TEXT("height"), 1);
        D.SetArrayField(TEXT("flat"), {MakeShared<FJsonValueObject>(Layer)});
    };
    TestTrue(TEXT("Unknown Superflat blocks do not matter to a Default world"),
        UnnamedSavedWorlds::ParseDraft(EditSavedWorldDraft(Unobtainium), World, Error));
    TestFalse(TEXT("...but a Superflat world can only use blocks the game has"),
        UnnamedSavedWorlds::ParseDraft(EditSavedWorldDraft(
                                           [&Unobtainium](FJsonObject& D)
                                           {
                                               Unobtainium(D);
                                               D.SetStringField(TEXT("type"), TEXT("Superflat"));
                                           }),
            World, Error));

    // The store, in a throwaway folder.
    const FString Root =
        FPaths::ProjectSavedDir() / TEXT("Automation") / (TEXT("Worlds-") + FGuid::NewGuid().ToString());
    UnnamedSavedWorlds::SetRootForTesting(Root);
    ON_SCOPE_EXIT
    {
        UnnamedSavedWorlds::SetRootForTesting(TEXT(""));
        IFileManager::Get().DeleteDirectory(*Root, false, true);
    };
    FUnnamedSavedWorld First, Second, Blank, Loaded;
    TestTrue(TEXT("Create saves a world"), UnnamedSavedWorlds::Create(SavedWorldDraft, First, Error));
    TestEqual(TEXT("...in a folder named from it"), First.Id, FString(TEXT("test-world")));
    TestEqual(TEXT("...with the text seed's value"), First.Seed, FVoxelSeed::Parse(TEXT("hello")).Value);
    TestTrue(TEXT("...that loads back"), UnnamedSavedWorlds::Load(First.Id, Loaded, Error));
    TestTrue(TEXT("...unchanged"),
        Loaded.Name == First.Name && Loaded.Seed == First.Seed && Loaded.SeedText == First.SeedText &&
            Loaded.GameMode == First.GameMode && Loaded.Type == First.Type && Loaded.FlatLayers.Num() == 3 &&
            Loaded.Generator == FUnnamedSavedWorld::CurrentGenerator &&
            UnnamedSavedWorlds::ToJson(Loaded) == UnnamedSavedWorlds::ToJson(First));
    TestTrue(TEXT("The same name again gets the next id"), UnnamedSavedWorlds::Create(SavedWorldDraft, Second, Error));
    TestEqual(TEXT("...test-world-2"), Second.Id, FString(TEXT("test-world-2")));
    const FString BlankDraft = EditSavedWorldDraft(
        [](FJsonObject& D)
        {
            D.SetStringField(TEXT("name"), TEXT("Blank"));
            D.SetStringField(TEXT("seed"), TEXT(" "));
        });
    TestTrue(TEXT("A blank seed creates a world"), UnnamedSavedWorlds::Create(BlankDraft, Blank, Error));
    TestTrue(TEXT("...whose random seed is saved"),
        UnnamedSavedWorlds::Load(Blank.Id, Loaded, Error) && Loaded.Seed == Blank.Seed);
    TestEqual(TEXT("...and Re-Create's draft carries it as a number"), Blank.ToDraft()->GetStringField(TEXT("seed")),
        FString::Printf(TEXT("%lld"), Blank.Seed));
    TestEqual(TEXT("A typed seed stays as typed for Re-Create"), First.ToDraft()->GetStringField(TEXT("seed")),
        FString(TEXT("hello")));

    // Newest played first; Touch moves a world to the top (after the system clock has surely moved on).
    FPlatformProcess::Sleep(.05f);
    TestTrue(TEXT("Touch marks a world played"), UnnamedSavedWorlds::Touch(First.Id, Error));
    TArray<FUnnamedSavedWorld> Listed = UnnamedSavedWorlds::List();
    TestEqual(TEXT("Three worlds listed"), Listed.Num(), 3);
    TestTrue(TEXT("...the last played first"), Listed.Num() == 3 && Listed[0].Id == First.Id);

    // Renaming keeps the id and folder.
    TestTrue(TEXT("Rename saves a new name"), UnnamedSavedWorlds::Rename(First.Id, TEXT("  Renamed\tWorld "), Error));
    TestTrue(TEXT("...cleaned, under the same id"),
        UnnamedSavedWorlds::Load(First.Id, Loaded, Error) && Loaded.Name == TEXT("Renamed World") &&
            Loaded.Seed == First.Seed);
    TestFalse(TEXT("A blank name is refused"), UnnamedSavedWorlds::Rename(First.Id, TEXT(" "), Error));
    TestFalse(
        TEXT("A missing world cannot be renamed"), UnnamedSavedWorlds::Rename(TEXT("no-such-world"), TEXT("X"), Error));

    // Files from a newer version, or with a broken seed, are refused.
    const FString Json = UnnamedSavedWorlds::ToJson(First);
    TestTrue(TEXT("world.json reads back"), UnnamedSavedWorlds::FromJson(Json, Loaded, Error));
    const TPair<const TCHAR*, TFunction<void(FJsonObject&)>> Refused[] = {
        {TEXT("a newer format"), [](FJsonObject& File) { File.SetNumberField(TEXT("version"), 2); }},
        {TEXT("a newer generator"), [](FJsonObject& File)
            { File.SetNumberField(TEXT("generator"), FUnnamedSavedWorld::CurrentGenerator + 1); }},
        {TEXT("a broken seed"), [](FJsonObject& File) { File.SetStringField(TEXT("seedValue"), TEXT("12abc")); }},
        {TEXT("a seed beyond 64 bits"),
            [](FJsonObject& File) { File.SetStringField(TEXT("seedValue"), TEXT("9223372036854775808")); }},
        {TEXT("a broken date"), [](FJsonObject& File) { File.SetStringField(TEXT("lastPlayed"), TEXT("yesterday")); }},
    };
    for (const auto& [Why, Edit] : Refused)
        TestFalse(FString(TEXT("Refused: ")) + Why,
            UnnamedSavedWorlds::FromJson(EditSavedWorldDraft(Edit, Json), Loaded, Error));

    // Delete removes the folder; bad ids never reach the disk.
    TestTrue(TEXT("Delete removes a world"), UnnamedSavedWorlds::Delete(Second.Id, Error));
    TestFalse(TEXT("...and its folder"), IFileManager::Get().DirectoryExists(*(Root / Second.Id)));
    TestEqual(TEXT("Two worlds left"), UnnamedSavedWorlds::List().Num(), 2);
    TestFalse(TEXT("Deleting it again fails"), UnnamedSavedWorlds::Delete(Second.Id, Error));
    for (const TCHAR* Bad : {TEXT("../Automation"), TEXT(""), TEXT("test-world/.."), TEXT("C:\\Windows")})
        TestFalse(FString(TEXT("Delete refuses ")) + Bad, UnnamedSavedWorlds::Delete(Bad, Error));
    TestTrue(TEXT("The other worlds are untouched"), IFileManager::Get().DirectoryExists(*(Root / First.Id)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldTerrainCheckTest, "Unnamed.Worlds.TerrainCheck",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldTerrainCheckTest::RunTest(const FString& Parameters)
{
    // Saved worlds as the Create screen's default draft makes them, set to a generator and a world type.
    FUnnamedSavedWorld Base;
    FString Error;
    if (!TestTrue(TEXT("The default draft parses"), UnnamedSavedWorlds::ParseDraft(SavedWorldDraft, Base, Error)))
        return false;
    const auto WorldOf = [&Base](int32 Generator, const TCHAR* Type)
    {
        FUnnamedSavedWorld Made = Base;
        Made.Generator = Generator;
        Made.Type = Type;
        return Made;
    };
    // Generator 2 reads the 26.3 preset of its world type from the reference; generator 1 and Superflat do not.
    constexpr int32 PresetGenerator = 2;

    // The reference as shipped builds every generator-2 world type.
    const FString RealRoot = VoxelWorldgen::GetReferenceRoot();
    for (const TCHAR* Type : {TEXT("Default"), TEXT("Large Biomes"), TEXT("AMPLIFIED")})
    {
        Error.Reset();
        const bool bBuilds = VoxelTerrainFactory::Check(WorldOf(PresetGenerator, Type), RealRoot, Error);
        TestTrue(FString::Printf(TEXT("A generator-2 %s world builds from the reference%s"), Type,
                     *(bBuilds ? FString() : TEXT(": ") + Error)),
            bBuilds);
    }

    // An empty folder has no reference: generator 2's presets cannot be read, and the error names the preset.
    const FString EmptyRoot = FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("TerrainCheck");
    IFileManager::Get().DeleteDirectory(*EmptyRoot, false, true);
    IFileManager::Get().MakeDirectory(*EmptyRoot, true);
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*EmptyRoot, false, true);
    };
    const TPair<const TCHAR*, const TCHAR*> Unreadable[] = {
        {TEXT("Default"), TEXT("overworld")}, {TEXT("Large Biomes"), TEXT("large_biomes")}};
    for (const auto& [Type, Preset] : Unreadable)
    {
        Error.Reset();
        TestFalse(FString(TEXT("Without the reference a generator-2 world fails: ")) + Type,
            VoxelTerrainFactory::Check(WorldOf(PresetGenerator, Type), EmptyRoot, Error));
        TestTrue(FString::Printf(TEXT("...and its error names %s (%s)"), Preset, *Error), Error.Contains(Preset));
    }
    // The presets without the biome facts: the parameter list cannot be read, so a generator-2 world is refused with
    // the file named, and a saved one gets no terrain.
    const FString NoBiomesRoot = FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("TerrainCheckNoBiomes");
    IFileManager::Get().DeleteDirectory(*NoBiomesRoot, false, true);
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*NoBiomesRoot, false, true);
    };
    if (TestTrue(TEXT("The reference's data copies without its facts"),
            FPlatformFileManager::Get().GetPlatformFile().CopyDirectoryTree(
                *(NoBiomesRoot / TEXT("data")), *(RealRoot / TEXT("data")), true)))
    {
        for (const TCHAR* Type : {TEXT("Default"), TEXT("Large Biomes"), TEXT("AMPLIFIED")})
        {
            Error.Reset();
            TestFalse(FString(TEXT("Without the biome facts a generator-2 world fails: ")) + Type,
                VoxelTerrainFactory::Check(WorldOf(PresetGenerator, Type), NoBiomesRoot, Error));
            TestTrue(FString::Printf(TEXT("...and its error names the parameter facts (%s)"), *Error),
                Error.Contains(TEXT("biome_parameters.json")));
        }
        const FUnnamedSavedWorld NoBiomesWorld = WorldOf(PresetGenerator, TEXT("Default"));
        Error.Reset();
        TestFalse(TEXT("A saved world whose biomes cannot be read gets no terrain"),
            VoxelTerrainFactory::Make(&NoBiomesWorld, 263, *VoxelBlocks::GetDefault(), NoBiomesRoot, Error).IsValid());
    }

    TestTrue(TEXT("A generator-1 world needs no reference"),
        VoxelTerrainFactory::Check(WorldOf(1, TEXT("Default")), EmptyRoot, Error));
    TestTrue(TEXT("A Superflat world needs no reference"),
        VoxelTerrainFactory::Check(WorldOf(PresetGenerator, TEXT("Superflat")), EmptyRoot, Error));

    // Make: a saved world never falls back to another terrain; the dev seed does, and logs an error.
    const FUnnamedSavedWorld SavedWorld = WorldOf(PresetGenerator, TEXT("Default"));
    Error.Reset();
    const TSharedPtr<const IVoxelTerrain> SavedTerrain =
        VoxelTerrainFactory::Make(&SavedWorld, 263, *VoxelBlocks::GetDefault(), EmptyRoot, Error);
    TestFalse(TEXT("A saved world whose preset cannot be read gets no terrain"), SavedTerrain.IsValid());
    TestTrue(TEXT("...and the reason"), Error.Contains(TEXT("overworld")));
    // The dev seed takes the newest generator unless -VoxelGenerator=<n> says otherwise; generator 1 has no preset to
    // fall back from.
    int32 DevGenerator = FUnnamedSavedWorld::CurrentGenerator;
    FParse::Value(FCommandLine::Get(), TEXT("VoxelGenerator="), DevGenerator);
    if (DevGenerator >= PresetGenerator)
    {
        AddExpectedError(TEXT("using the stand-in terrain"), EAutomationExpectedErrorFlags::Contains, 1);
        Error.Reset();
        const TSharedPtr<const IVoxelTerrain> DevTerrain =
            VoxelTerrainFactory::Make(nullptr, 263, *VoxelBlocks::GetDefault(), EmptyRoot, Error);
        TestTrue(TEXT("The dev seed falls back to the stand-in terrain"), DevTerrain.IsValid());
        TestFalse(TEXT("...and says why"), Error.IsEmpty());
    }
    else
        AddInfo(FString::Printf(TEXT("-VoxelGenerator=%d: the dev seed's fallback is not exercised"), DevGenerator));

    // The preset each world type reads.
    const TPair<const TCHAR*, const TCHAR*> Presets[] = {{TEXT("Large Biomes"), TEXT("large_biomes")},
        {TEXT("AMPLIFIED"), TEXT("amplified")}, {TEXT("Default"), TEXT("overworld")},
        {TEXT("Single Biome"), TEXT("overworld")}};
    for (const auto& [Type, Preset] : Presets)
        TestEqual(
            FString(TEXT("Preset of ")) + Type, FString(VoxelTerrainFactory::GetPresetName(Type)), FString(Preset));
    return true;
}
#endif
