#if WITH_DEV_AUTOMATION_TESTS
#include "Menu/MenuPreferences.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    TSharedPtr<FJsonObject> ReadPreferences(const FString& Json)
    {
        TSharedPtr<FJsonObject> Result;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Result);
        return Result;
    }

    FString WritePreferences(const TSharedPtr<FJsonObject>& Object)
    {
        FString Json;
        FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
        return Json;
    }

    FString TestPath()
    {
        return FPaths::ProjectSavedDir() / TEXT("Automation/MenuPreferences") /
            FGuid::NewGuid().ToString(EGuidFormats::Digits) / TEXT("preferences.json");
    }

    UMenuPreferences* Service(const FString& Path, bool Persistent = true)
    {
        auto* Result = NewObject<UMenuPreferences>(NewObject<UGameInstance>());
        Result->InitializeForTesting(Path, Persistent);
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPreferenceContractTest, "Unnamed.Menu.Preferences.Contract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMenuPreferenceContractTest::RunTest(const FString& Parameters)
{
    auto* Preferences = Service(TestPath(), false);
    auto State = ReadPreferences(Preferences->GetState());
    TestTrue(TEXT("Schema ready"), State->GetBoolField(TEXT("ready")));
    TestEqual(TEXT("101 independently owned preferences"), State->GetObjectField(TEXT("settings"))->Values.Num(), 101);
    FString Json;
    FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Menu/Web/preferences-schema.menuprefs")));
    const auto Schema = ReadPreferences(Json);
    for (const auto& Entry : Schema->GetObjectField(TEXT("settings"))->Values)
    {
        const auto Definition = Entry.Value->AsObject();
        const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
        TArray<TSharedPtr<FJsonValue>> Samples;
        if (Definition->TryGetArrayField(TEXT("values"), Choices))
            Samples = *Choices;
        else
        {
            Samples.Add(MakeShared<FJsonValueNumber>(Definition->GetNumberField(TEXT("min"))));
            Samples.Add(MakeShared<FJsonValueNumber>(Definition->GetNumberField(TEXT("max"))));
            Samples.Add(Definition->Values[TEXT("default")]);
        }
        for (const auto& Value : Samples)
        {
            const auto Patch = MakeShared<FJsonObject>(), Settings = MakeShared<FJsonObject>();
            Settings->SetField(Entry.Key, Value);
            Patch->SetObjectField(TEXT("settings"), Settings);
            TestTrue(*FString::Printf(TEXT("Accept %s"), *Entry.Key),
                ReadPreferences(Preferences->ApplyChanges(WritePreferences(Patch)))->GetBoolField(TEXT("ok")));
            TestTrue(*FString::Printf(TEXT("Readable by future native consumer: %s"), *Entry.Key),
                Preferences->FindPreference(FString(*Entry.Key)).IsValid());
        }
    }
    const auto Before = WritePreferences(ReadPreferences(Preferences->GetState())->GetObjectField(TEXT("settings")));
    for (const FString& Invalid : {TEXT("{\"settings\":{\"fov\":71,\"fullscreen\":true}}"),
             TEXT("{\"settings\":{\"fov\":71,\"toggleSprint\":true}}"),
             TEXT("{\"settings\":{\"preferredGraphicsBackend\":\"Default\"}}"),
             TEXT("{\"settings\":{\"soundDevice\":\"System Default\"}}"),
             TEXT("{\"settings\":{\"rotateWithMinecart\":true}}"), TEXT("{\"settings\":{\"fov\":\"90\"}}"),
             TEXT("{\"settings\":{\"fov\":29}}"), TEXT("{\"settings\":{\"fov\":110.5}}"),
             TEXT("{\"settings\":{\"fov\":70.5}}"), TEXT("{\"language\":\"unknown\"}"), TEXT("{\"skipWarning\":1}"),
             TEXT("{\"packs\":[\"high_contrast\",\"high_contrast\"]}"), TEXT("{\"packs\":[\"unknown\"]}"),
             TEXT("{\"settings\":{\"highContrast\":false},\"packs\":[\"high_contrast\"]}"), TEXT("{\"worlds\":[]}"),
             TEXT("{\"settings\":[]}")})
    {
        const auto Rejected = ReadPreferences(Preferences->ApplyChanges(Invalid));
        TestFalse(*FString::Printf(TEXT("Reject %s"), *Invalid), Rejected->GetBoolField(TEXT("ok")));
        TestEqual(
            TEXT("Rejected patch is atomic"), WritePreferences(Rejected->GetObjectField(TEXT("settings"))), Before);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPreferencePersistenceTest, "Unnamed.Menu.Preferences.Persistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMenuPreferencePersistenceTest::RunTest(const FString& Parameters)
{
    const FString Path = TestPath();
    auto* First = Service(Path);
    const FString Patch = TEXT("{\"settings\":{\"fov\":88,\"sound_master\":0,\"allowFriendRequests\":false,"
                               "\"friendsList\":false},\"language\":\"fr_fr\",\"packs\":[\"programmer_art\",\"high_"
                               "contrast\"],\"skipWarning\":true,\"telemetryOptInExtra\":true}");
    TestTrue(TEXT("Save acknowledged"), ReadPreferences(First->ApplyChanges(Patch))->GetBoolField(TEXT("ok")));
    auto* Reloaded = Service(Path);
    const auto State = ReadPreferences(Reloaded->GetState());
    const auto Settings = State->GetObjectField(TEXT("settings"));
    TestEqual(TEXT("Value survives recreation"), Settings->GetIntegerField(TEXT("fov")), 88);
    TestEqual(TEXT("Zero survives"), Settings->GetNumberField(TEXT("sound_master")), 0.);
    TestFalse(TEXT("False child survives disabling parent"), Settings->GetBoolField(TEXT("allowFriendRequests")));
    TestEqual(TEXT("Language survives"), State->GetStringField(TEXT("language")), FString(TEXT("fr_fr")));
    TestEqual(TEXT("Pack order survives"), State->GetArrayField(TEXT("packs"))[0]->AsString(),
        FString(TEXT("programmer_art")));
    TestTrue(TEXT("Shared high contrast agrees"), Settings->GetBoolField(TEXT("highContrast")));
    TestTrue(TEXT("Warning preference survives"), State->GetBoolField(TEXT("skipWarning")));
    TestTrue(TEXT("Telemetry choice survives"), State->GetBoolField(TEXT("telemetryOptInExtra")));
    TestTrue(TEXT("Disable shared high contrast"),
        ReadPreferences(Reloaded->ApplyChanges(TEXT("{\"settings\":{\"highContrast\":false}}")))
            ->GetBoolField(TEXT("ok")));
    TestEqual(
        TEXT("Unrelated pack retained"), ReadPreferences(Reloaded->GetState())->GetArrayField(TEXT("packs")).Num(), 1);
    TestTrue(TEXT("Preset saved"),
        ReadPreferences(Reloaded->ApplyChanges(TEXT("{\"settings\":{\"graphicsPreset\":\"Fast\"}}")))
            ->GetBoolField(TEXT("ok")));
    const auto Fast = ReadPreferences(Service(Path)->GetState())->GetObjectField(TEXT("settings"));
    TestEqual(TEXT("Preset survives reload"), Fast->GetStringField(TEXT("graphicsPreset")), FString(TEXT("Fast")));
    TestFalse(TEXT("Preset cannot own functional blur"), Fast->HasField(TEXT("menuBackgroundBlurriness")));
    const auto Custom = ReadPreferences(Reloaded->ApplyChanges(TEXT("{\"settings\":{\"cloudStatus\":\"Fancy\"}}")))
                            ->GetObjectField(TEXT("settings"));
    TestEqual(
        TEXT("Quality edit becomes Custom"), Custom->GetStringField(TEXT("graphicsPreset")), FString(TEXT("Custom")));
    FString SavedBefore;
    FFileHelper::LoadFileToString(SavedBefore, *Path);
    auto* Temporary = Service(Path, false);
    Temporary->ApplyChanges(TEXT("{\"settings\":{\"fov\":100}}"));
    FString SavedAfter;
    FFileHelper::LoadFileToString(SavedAfter, *Path);
    TestEqual(TEXT("Editor/session mode leaves disk unchanged"), SavedAfter, SavedBefore);
    TestEqual(TEXT("Session mode starts with defaults"),
        ReadPreferences(Service(Path, false)->GetState())
            ->GetObjectField(TEXT("settings"))
            ->GetIntegerField(TEXT("fov")),
        110);
    const FString Sentinel = FPaths::GetPath(Path) / TEXT("unrelated-save.sav");
    FFileHelper::SaveStringToFile(TEXT("keep"), *Sentinel);
    TestTrue(TEXT("Explicit reset succeeds"), ReadPreferences(Reloaded->ResetPreferences())->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("Reset persists defaults"),
        ReadPreferences(Service(Path)->GetState())->GetObjectField(TEXT("settings"))->GetIntegerField(TEXT("fov")),
        110);
    FFileHelper::LoadFileToString(SavedAfter, *Sentinel);
    TestEqual(TEXT("Unrelated save preserved"), SavedAfter, FString(TEXT("keep")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPreferenceFailureTest, "Unnamed.Menu.Preferences.FailureRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMenuPreferenceFailureTest::RunTest(const FString& Parameters)
{
    const FString Path = TestPath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    for (const FString& Invalid :
        {TEXT("not json"), TEXT("{\"version\":99}"), TEXT("{\"version\":1,\"settings\":{\"fov\":-1}}")})
    {
        FFileHelper::SaveStringToFile(Invalid, *Path);
        auto* Protected = Service(Path);
        TestFalse(
            TEXT("Invalid document protected"), ReadPreferences(Protected->GetState())->GetBoolField(TEXT("editable")));
        TestFalse(TEXT("Mutation rejected"),
            ReadPreferences(Protected->ApplyChanges(TEXT("{\"settings\":{\"fov\":90}}")))->GetBoolField(TEXT("ok")));
        FString Error, Contents;
        TestTrue(TEXT("Untouched protected file permits exit"), Protected->FlushForExit(Error));
        FFileHelper::LoadFileToString(Contents, *Path);
        TestEqual(TEXT("Invalid original is not overwritten"), Contents, Invalid);
        TestTrue(TEXT("Explicit reset restores editable state"),
            ReadPreferences(Protected->ResetPreferences())->GetBoolField(TEXT("editable")));
    }
    TArray<FString> Backups;
    IFileManager::Get().FindFiles(Backups, *(Path + TEXT(".protected-*")), true, false);
    TestEqual(TEXT("Each rejected original retained separately"), Backups.Num(), 3);
    const FString RecoveryPath = TestPath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(RecoveryPath), true);
    FFileHelper::SaveStringToFile(TEXT("{\"version\":1,\"settings\":{\"fov\":93}}"), *(RecoveryPath + TEXT(".bak")));
    const auto Recovered = ReadPreferences(Service(RecoveryPath)->GetState());
    TestTrue(TEXT("Backup recovery succeeds"), Recovered->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("Last good value recovered"),
        Recovered->GetObjectField(TEXT("settings"))->GetIntegerField(TEXT("fov")), 93);
    TestEqual(TEXT("Missing fields receive defaults"), Recovered->GetObjectField(TEXT("settings"))->Values.Num(), 101);
    const FString Blocker = FPaths::GetPath(Path) / TEXT("regular-file");
    FFileHelper::SaveStringToFile(TEXT("block"), *Blocker);
    auto* Failed = Service(Blocker / TEXT("preferences.json"));
    const auto Rejected = ReadPreferences(Failed->ApplyChanges(TEXT("{\"settings\":{\"fov\":90}}")));
    TestFalse(TEXT("Unwritable destination never acknowledges saving"), Rejected->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("Failed save retains accepted state"),
        Rejected->GetObjectField(TEXT("settings"))->GetIntegerField(TEXT("fov")), 110);
    FString Error;
    TestFalse(TEXT("Failed save prevents normal quit"), Failed->FlushForExit(Error));
    TestFalse(TEXT("Failure has useful error"), Error.IsEmpty());
    return true;
}
#endif
