#include "Worlds/UnnamedSavedWorlds.h"
#include "Blocks/VoxelBlockTable.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Random/VoxelSeed.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnnamedFiles.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"
#include "Voxel/VoxelBlocks.h"

namespace
{
    // The Create screen's limits (its name and seed fields hold 128 characters).
    constexpr int32 MaxWorldNameChars = 128, MaxWorldSeedChars = 128;
    constexpr int32 MaxWorldDraftChars = 65536, MaxWorldFileBytes = 262144;
    constexpr int32 MaxWorldIdChars = 48, MaxWorldIdStemChars = 32;
    constexpr int32 MaxWorldFlatLayers = 384, MaxWorldFlatHeight = 384, MaxWorldOptions = 128;
    const TCHAR* const WorldFileName = TEXT("world.json");

    // The Create screen's values, kept verbatim.
    const TCHAR* const WorldGameModes[] = {TEXT("Survival"), TEXT("Hardcore"), TEXT("Creative")};
    const TCHAR* const WorldDifficulties[] = {TEXT("Peaceful"), TEXT("Easy"), TEXT("Normal"), TEXT("Hard")};
    const TCHAR* const WorldTypes[] = {
        TEXT("Default"), TEXT("Superflat"), TEXT("Large Biomes"), TEXT("AMPLIFIED"), TEXT("Single Biome")};
    // Ids of retired catalog worlds, never reused (Docs/NamingConventions.md), and Windows device names.
    const TCHAR* const ReservedWorldIds[] = {TEXT("foundation"), TEXT("super-flat"), TEXT("interaction-lab"),
        TEXT("con"), TEXT("prn"), TEXT("aux"), TEXT("nul"), TEXT("com1"), TEXT("com2"), TEXT("com3"), TEXT("com4"),
        TEXT("com5"), TEXT("com6"), TEXT("com7"), TEXT("com8"), TEXT("com9"), TEXT("lpt1"), TEXT("lpt2"), TEXT("lpt3"),
        TEXT("lpt4"), TEXT("lpt5"), TEXT("lpt6"), TEXT("lpt7"), TEXT("lpt8"), TEXT("lpt9")};

    FString& WorldRootOverride()
    {
        static FString Root;
        return Root;
    }

    template <int32 Count> bool IsWorldChoice(const FString& Value, const TCHAR* const (&Choices)[Count])
    {
        for (const TCHAR* Choice : Choices)
            if (Value.Equals(Choice, ESearchCase::CaseSensitive)) return true;
        return false;
    }

    bool IsWorldIdChar(TCHAR Char)
    {
        return (Char >= 'a' && Char <= 'z') || (Char >= '0' && Char <= '9');
    }

    // Rule and experiment ids: lowercase snake_case.
    bool IsWorldOptionId(const FString& Value)
    {
        if (Value.IsEmpty() || Value.Len() > 64) return false;
        for (const TCHAR Char : Value)
            if (!IsWorldIdChar(Char) && Char != '_') return false;
        return true;
    }

    // Biome and block ids: "minecraft:" and a lowercase snake_case name.
    bool IsWorldResourceId(const FString& Value)
    {
        return Value.StartsWith(TEXT("minecraft:"), ESearchCase::CaseSensitive) && IsWorldOptionId(Value.RightChop(10));
    }

    bool FailWorld(FString& OutError, const FString& Message)
    {
        OutError = Message;
        return false;
    }

    FString GetWorldFolder(const FString& Id)
    {
        return UnnamedSavedWorlds::GetRoot() / Id;
    }

    FString GetWorldFile(const FString& Id)
    {
        return GetWorldFolder(Id) / WorldFileName;
    }

    // Strict reads: a value must already have the type. FJsonValue's TryGet* convert (any number reads as a boolean,
    // any text as a boolean or number), which would let a malformed draft through.
    TSharedPtr<FJsonValue> GetWorldField(const FJsonObject& Source, const TCHAR* Field, EJson Type)
    {
        const TSharedPtr<FJsonValue> Value = Source.TryGetField(Field);
        return Value && Value->Type == Type ? Value : nullptr;
    }

    bool ReadWorldString(const FJsonObject& Source, const TCHAR* Field, FString& Out)
    {
        const TSharedPtr<FJsonValue> Value = GetWorldField(Source, Field, EJson::String);
        if (Value) Out = Value->AsString();
        return Value.IsValid();
    }

    bool ReadWorldBool(const FJsonObject& Source, const TCHAR* Field, bool& Out)
    {
        const TSharedPtr<FJsonValue> Value = GetWorldField(Source, Field, EJson::Boolean);
        if (Value) Out = Value->AsBool();
        return Value.IsValid();
    }

    // A whole JSON number within Low..High.
    bool ReadWorldInteger(const TSharedPtr<FJsonValue>& Value, double Low, double High, int32& Out)
    {
        if (!Value || Value->Type != EJson::Number) return false;
        const double Number = Value->AsNumber();
        if (!FMath::IsFinite(Number) || FMath::Frac(Number) != 0 || Number < Low || Number > High) return false;
        Out = int32(Number);
        return true;
    }

    // An object of snake_case ids to booleans (experiments) or to booleans and int32 integers (game rules), copied so
    // nothing unchecked is ever saved.
    bool ReadWorldOptions(
        const FJsonObject& Source, const TCHAR* Field, bool bIntegers, TSharedPtr<FJsonObject>& Out, FString& OutError)
    {
        Out = MakeShared<FJsonObject>();
        const TSharedPtr<FJsonValue> Object = GetWorldField(Source, Field, EJson::Object);
        if (!Object) return FailWorld(OutError, FString::Printf(TEXT("The world's %s are missing."), Field));
        if (Object->AsObject()->Values.Num() > MaxWorldOptions)
            return FailWorld(OutError, FString::Printf(TEXT("The world has too many %s."), Field));
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Object->AsObject()->Values)
        {
            int32 Integer = 0;
            if (!IsWorldOptionId(Entry.Key))
                return FailWorld(OutError, FString::Printf(TEXT("\"%s\" is not a valid id."), *Entry.Key.Left(64)));
            if (Entry.Value && Entry.Value->Type == EJson::Boolean)
                Out->SetBoolField(Entry.Key, Entry.Value->AsBool());
            else if (bIntegers && ReadWorldInteger(Entry.Value, MIN_int32, MAX_int32, Integer))
                Out->SetNumberField(Entry.Key, Integer);
            else
                return FailWorld(OutError, FString::Printf(TEXT("%s has an invalid value."), *Entry.Key));
        }
        return true;
    }

    // A world name as the list shows it: tabs become spaces, line breaks go, the ends are trimmed; other control
    // characters are refused.
    bool CleanWorldName(FString Name, FString& Out, FString& OutError)
    {
        Name.ReplaceCharInline('\t', ' ');
        Name = Name.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));
        Name.TrimStartAndEndInline();
        for (const TCHAR Char : Name)
            if (Char < 32 || Char == 127)
                return FailWorld(OutError, TEXT("The world name has a character it cannot use."));
        if (Name.IsEmpty()) return FailWorld(OutError, TEXT("Enter a name for the world."));
        if (Name.Len() > MaxWorldNameChars)
            return FailWorld(OutError, TEXT("A world name can be at most 128 characters."));
        Out = Name;
        return true;
    }

    // The Create screen's settings, shared by drafts and world.json, checked field by field.
    bool ReadWorldSettings(const FJsonObject& Source, FUnnamedSavedWorld& Out, FString& OutError)
    {
        FString Name;
        if (!ReadWorldString(Source, TEXT("name"), Name)) return FailWorld(OutError, TEXT("The world has no name."));
        if (!CleanWorldName(Name, Out.Name, OutError)) return false;

        if (!ReadWorldString(Source, TEXT("seed"), Out.SeedText))
            return FailWorld(OutError, TEXT("The world has no seed."));
        if (Out.SeedText.Len() > MaxWorldSeedChars)
            return FailWorld(OutError, TEXT("A seed can be at most 128 characters."));

        if (!ReadWorldString(Source, TEXT("gameMode"), Out.GameMode) || !IsWorldChoice(Out.GameMode, WorldGameModes))
            return FailWorld(OutError, TEXT("The game mode is not one this game has."));
        if (!ReadWorldString(Source, TEXT("difficulty"), Out.Difficulty) ||
            !IsWorldChoice(Out.Difficulty, WorldDifficulties))
            return FailWorld(OutError, TEXT("The difficulty is not one this game has."));
        if (!ReadWorldString(Source, TEXT("type"), Out.Type) || !IsWorldChoice(Out.Type, WorldTypes))
            return FailWorld(OutError, TEXT("The world type is not one this game has."));
        if (!ReadWorldBool(Source, TEXT("commands"), Out.bCommands) ||
            !ReadWorldBool(Source, TEXT("structures"), Out.bStructures) ||
            !ReadWorldBool(Source, TEXT("bonus"), Out.bBonusChest))
            return FailWorld(OutError, TEXT("A world switch is missing."));
        if (!ReadWorldString(Source, TEXT("biome"), Out.Biome) || !IsWorldResourceId(Out.Biome) ||
            !ReadWorldString(Source, TEXT("flatBiome"), Out.FlatBiome) || !IsWorldResourceId(Out.FlatBiome))
            return FailWorld(OutError, TEXT("The world's biome is not a valid id."));

        const TSharedPtr<FJsonValue> Layers = GetWorldField(Source, TEXT("flat"), EJson::Array);
        if (!Layers || Layers->AsArray().Num() > MaxWorldFlatLayers)
            return FailWorld(OutError, TEXT("The Superflat layers could not be read."));
        Out.FlatLayers.Reset();
        for (const TSharedPtr<FJsonValue>& Value : Layers->AsArray())
        {
            FUnnamedSavedWorld::FFlatLayer& Read = Out.FlatLayers.AddDefaulted_GetRef();
            if (!Value || Value->Type != EJson::Object ||
                !ReadWorldString(*Value->AsObject(), TEXT("block"), Read.Block) || !IsWorldResourceId(Read.Block) ||
                !ReadWorldInteger(Value->AsObject()->TryGetField(TEXT("height")), 1, MaxWorldFlatHeight, Read.Height))
                return FailWorld(OutError, TEXT("A Superflat layer is not a block id with a height from 1 to 384."));
        }
        return ReadWorldOptions(Source, TEXT("experiments"), false, Out.Experiments, OutError) &&
            ReadWorldOptions(Source, TEXT("rules"), true, Out.Rules, OutError);
    }

    void WriteWorldSettings(const FUnnamedSavedWorld& World, FJsonObject& Out)
    {
        Out.SetStringField(TEXT("name"), World.Name);
        Out.SetStringField(TEXT("seed"), World.SeedText);
        Out.SetStringField(TEXT("gameMode"), World.GameMode);
        Out.SetStringField(TEXT("difficulty"), World.Difficulty);
        Out.SetBoolField(TEXT("commands"), World.bCommands);
        Out.SetStringField(TEXT("type"), World.Type);
        Out.SetBoolField(TEXT("structures"), World.bStructures);
        Out.SetBoolField(TEXT("bonus"), World.bBonusChest);
        Out.SetStringField(TEXT("biome"), World.Biome);
        Out.SetStringField(TEXT("flatBiome"), World.FlatBiome);
        TArray<TSharedPtr<FJsonValue>> Layers;
        for (const FUnnamedSavedWorld::FFlatLayer& Layer : World.FlatLayers)
        {
            const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("block"), Layer.Block);
            Entry->SetNumberField(TEXT("height"), Layer.Height);
            Layers.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Out.SetArrayField(TEXT("flat"), Layers);
        Out.SetObjectField(TEXT("experiments"), World.Experiments ? World.Experiments : MakeShared<FJsonObject>());
        Out.SetObjectField(TEXT("rules"), World.Rules ? World.Rules : MakeShared<FJsonObject>());
    }

    // world.json's "clocks" key, as vanilla's world_clocks.dat (scout N 5 (f)): an object per clock id, with
    // total_ticks, paused and rate, each left out at its default. Only the overworld's clock exists here; other ids are
    // ignored.
    const TCHAR* const WorldClocksField = TEXT("clocks");
    const TCHAR* const OverworldClockId = TEXT("minecraft:overworld");
    // A JSON number holds integers exactly up to 2^53 (FVoxelWorldClock::MaxTotalTicks).
    constexpr double MaxWorldClockTicks = 9007199254740991.;
    constexpr double MaxWorldClockRate = 1000.;

    bool ReadWorldClock(const FJsonObject& Source, FUnnamedSavedWorld& Out, FString& OutError)
    {
        const TSharedPtr<FJsonValue> Clocks = Source.TryGetField(WorldClocksField);
        if (!Clocks) return true;
        const TSharedPtr<FJsonValue> Overworld =
            Clocks->Type == EJson::Object ? Clocks->AsObject()->TryGetField(OverworldClockId) : nullptr;
        if (Clocks->Type != EJson::Object || (Overworld && Overworld->Type != EJson::Object))
            return FailWorld(OutError, TEXT("The world's time could not be read."));
        if (!Overworld) return true;
        const FJsonObject& Fields = *Overworld->AsObject();
        FUnnamedSavedWorld::FClock Clock;
        const TSharedPtr<FJsonValue> Total = Fields.TryGetField(TEXT("total_ticks"));
        const TSharedPtr<FJsonValue> Paused = Fields.TryGetField(TEXT("paused"));
        const TSharedPtr<FJsonValue> Rate = Fields.TryGetField(TEXT("rate"));
        const double Ticks = Total && Total->Type == EJson::Number ? Total->AsNumber() : 0.;
        const double Speed = Rate && Rate->Type == EJson::Number ? Rate->AsNumber() : 1.;
        if ((Total &&
                (Total->Type != EJson::Number || !FMath::IsFinite(Ticks) || FMath::Frac(Ticks) != 0 || Ticks < 0 ||
                    Ticks > MaxWorldClockTicks)) ||
            (Paused && Paused->Type != EJson::Boolean) ||
            (Rate &&
                (Rate->Type != EJson::Number || !FMath::IsFinite(Speed) || float(Speed) <= 0.f ||
                    Speed > MaxWorldClockRate)))
            return FailWorld(OutError, TEXT("The world's time could not be read."));
        Clock.TotalTicks = int64(Ticks);
        Clock.bPaused = Paused && Paused->AsBool();
        Clock.Rate = float(Speed);
        Out.Clock = Clock;
        return true;
    }

    void WriteWorldClock(const FUnnamedSavedWorld& World, FJsonObject& Out)
    {
        if (!World.Clock) return;
        const TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
        // Written as their exact digits rather than through a double.
        if (World.Clock->TotalTicks != 0)
            Fields->SetField(TEXT("total_ticks"),
                MakeShared<FJsonValueNumberString>(FString::Printf(TEXT("%lld"), World.Clock->TotalTicks)));
        if (World.Clock->bPaused) Fields->SetBoolField(TEXT("paused"), true);
        if (World.Clock->Rate != 1.f)
        {
            // The shortest text that reads back as the same float.
            FString Rate = FString::SanitizeFloat(World.Clock->Rate);
            if (float(FCString::Atod(*Rate)) != World.Clock->Rate)
                Rate = FString::Printf(TEXT("%.9g"), World.Clock->Rate);
            Fields->SetField(TEXT("rate"), MakeShared<FJsonValueNumberString>(Rate));
        }
        const TSharedRef<FJsonObject> Clocks = MakeShared<FJsonObject>();
        Clocks->SetObjectField(OverworldClockId, Fields);
        Out.SetObjectField(WorldClocksField, Clocks);
    }

    FString PrintWorldJson(const TSharedRef<FJsonObject>& Object)
    {
        FString Text;
        FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
        return Text + TEXT("\n");
    }
}

bool FUnnamedSavedWorld::GetRule(const FString& RuleId, bool bDefault) const
{
    return Rules && Rules->HasTypedField<EJson::Boolean>(RuleId) ? Rules->GetBoolField(RuleId) : bDefault;
}

TSharedRef<FJsonObject> FUnnamedSavedWorld::ToDraft() const
{
    const TSharedRef<FJsonObject> Draft = MakeShared<FJsonObject>();
    WriteWorldSettings(*this, *Draft);
    if (SeedText.TrimStartAndEnd().IsEmpty()) Draft->SetStringField(TEXT("seed"), FString::Printf(TEXT("%lld"), Seed));
    return Draft;
}

FString UnnamedSavedWorlds::GetRoot()
{
    if (!WorldRootOverride().IsEmpty()) return WorldRootOverride();
    FString Root;
    if (FParse::Value(FCommandLine::Get(), TEXT("WorldsDir="), Root) && !Root.IsEmpty())
        return FPaths::ConvertRelativePathToFull(Root);
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Worlds"));
}

#if WITH_DEV_AUTOMATION_TESTS
void UnnamedSavedWorlds::SetRootForTesting(const FString& Root)
{
    WorldRootOverride() = Root.IsEmpty() ? Root : FPaths::ConvertRelativePathToFull(Root);
}
#endif

bool UnnamedSavedWorlds::IsValidId(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > MaxWorldIdChars || Id[0] == '-' || Id[Id.Len() - 1] == '-') return false;
    for (int32 Index = 0; Index < Id.Len(); ++Index)
        if (Id[Index] == '-' ? Id[Index - 1] == '-' : !IsWorldIdChar(Id[Index])) return false;
    return true;
}

FString UnnamedSavedWorlds::MakeId(const FString& Name, TFunctionRef<bool(const FString&)> IsTaken)
{
    FString Stem;
    for (const TCHAR Raw : Name)
    {
        const TCHAR Char = FChar::ToLower(Raw);
        if (IsWorldIdChar(Char))
            Stem.AppendChar(Char);
        else if (!Stem.IsEmpty() && Stem[Stem.Len() - 1] != '-')
            Stem.AppendChar('-');
        if (Stem.Len() >= MaxWorldIdStemChars) break;
    }
    while (Stem.EndsWith(TEXT("-")))
        Stem.LeftChopInline(1);
    if (Stem.IsEmpty()) Stem = TEXT("world");
    auto IsFree = [&IsTaken](const FString& Candidate)
    {
        return !IsWorldChoice(Candidate, ReservedWorldIds) && !UUnnamedProjectSettings::Get().FindWorld(Candidate) &&
            !IsTaken(Candidate);
    };
    FString Id = Stem;
    for (int32 Suffix = 2; !IsFree(Id); ++Suffix)
        Id = FString::Printf(TEXT("%s-%d"), *Stem, Suffix);
    return Id;
}

bool UnnamedSavedWorlds::ParseDraft(const FString& DraftJson, FUnnamedSavedWorld& Out, FString& OutError)
{
    Out = FUnnamedSavedWorld();
    TSharedPtr<FJsonObject> Draft;
    if (DraftJson.Len() > MaxWorldDraftChars ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(DraftJson), Draft) || !Draft)
        return FailWorld(OutError, TEXT("The new world's settings could not be read."));
    if (!ReadWorldSettings(*Draft, Out, OutError)) return false;
    // As the Create screen shows Hardcore: Hard, no commands, no bonus chest.
    if (Out.IsHardcore())
    {
        Out.Difficulty = TEXT("Hard");
        Out.bCommands = Out.bBonusChest = false;
    }
    // A Superflat world can only stack blocks this game has.
    if (Out.IsSuperflat())
    {
        const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
        for (const FUnnamedSavedWorld::FFlatLayer& Layer : Out.FlatLayers)
        {
            const FString Block = Layer.Block.RightChop(10);
            if (Block != TEXT("air") && !Table->FindState(FName(*Block)))
                return FailWorld(OutError,
                    FString::Printf(TEXT("The Superflat layer %s is not a block this game has yet."), *Layer.Block));
        }
    }
    return true;
}

bool UnnamedSavedWorlds::Create(const FString& DraftJson, FUnnamedSavedWorld& Out, FString& OutError)
{
    if (!ParseDraft(DraftJson, Out, OutError)) return false;
    // A blank seed draws its random seed here, once; the world keeps it.
    const FVoxelSeed Seed = FVoxelSeed::Parse(Out.SeedText);
    Out.Seed = Seed.Value;
    Out.Id = MakeId(Out.Name,
        [](const FString& Candidate) { return IFileManager::Get().DirectoryExists(*GetWorldFolder(Candidate)); });
    Out.Created = Out.LastPlayed = FDateTime::UtcNow();
    Out.Generator = FUnnamedSavedWorld::CurrentGenerator;
    if (!IFileManager::Get().MakeDirectory(*GetWorldFolder(Out.Id), true))
        return FailWorld(OutError, TEXT("The world's folder could not be created."));
    if (!Save(Out, OutError)) return false;
    UE_LOG(LogUnnamedWorlds, Display, TEXT("Created world %s (\"%s\", %s, %s, %s, seed %s)"), *Out.Id, *Out.Name,
        *Out.Type, *Out.GameMode, *Out.Difficulty, *Seed.Describe());
    return true;
}

FString UnnamedSavedWorlds::ToJson(const FUnnamedSavedWorld& World)
{
    const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("version"), FUnnamedSavedWorld::FormatVersion);
    Object->SetNumberField(TEXT("generator"), World.Generator);
    WriteWorldSettings(World, *Object);
    // A JSON number is a double: the 64-bit seed is written as text.
    Object->SetStringField(TEXT("seedValue"), FString::Printf(TEXT("%lld"), World.Seed));
    Object->SetStringField(TEXT("created"), World.Created.ToIso8601());
    Object->SetStringField(TEXT("lastPlayed"), World.LastPlayed.ToIso8601());
    WriteWorldClock(World, *Object);
    return PrintWorldJson(Object);
}

bool UnnamedSavedWorlds::FromJson(const FString& Json, FUnnamedSavedWorld& Out, FString& OutError)
{
    Out = FUnnamedSavedWorld();
    TSharedPtr<FJsonObject> Object;
    int32 Version = 0;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object ||
        !ReadWorldInteger(Object->TryGetField(TEXT("version")), 1, MAX_int32, Version) ||
        !ReadWorldInteger(Object->TryGetField(TEXT("generator")), 1, MAX_int32, Out.Generator))
        return FailWorld(OutError, TEXT("world.json could not be read."));
    if (Version > FUnnamedSavedWorld::FormatVersion || Out.Generator > FUnnamedSavedWorld::CurrentGenerator)
        return FailWorld(OutError, TEXT("This world was saved by a newer version of the game."));
    if (!ReadWorldSettings(*Object, Out, OutError)) return false;
    FString SeedValue, Created, LastPlayed;
    if (!ReadWorldString(*Object, TEXT("seedValue"), SeedValue) || !LexTryParseString(Out.Seed, *SeedValue) ||
        FString::Printf(TEXT("%lld"), Out.Seed) != SeedValue)
        return FailWorld(OutError, TEXT("world.json's seed could not be read."));
    if (!ReadWorldString(*Object, TEXT("created"), Created) || !FDateTime::ParseIso8601(*Created, Out.Created) ||
        !ReadWorldString(*Object, TEXT("lastPlayed"), LastPlayed) ||
        !FDateTime::ParseIso8601(*LastPlayed, Out.LastPlayed))
        return FailWorld(OutError, TEXT("world.json's dates could not be read."));
    return ReadWorldClock(*Object, Out, OutError);
}

bool UnnamedSavedWorlds::Save(const FUnnamedSavedWorld& World, FString& OutError)
{
    if (!IsValidId(World.Id)) return FailWorld(OutError, TEXT("The world has no valid id."));
    if (UnnamedFiles::SaveTextAtomically(GetWorldFile(World.Id), ToJson(World)) != UnnamedFiles::ESaveResult::Saved)
        return FailWorld(OutError, TEXT("The world could not be saved. Check that the save folder is writable."));
    return true;
}

bool UnnamedSavedWorlds::Load(const FString& Id, FUnnamedSavedWorld& Out, FString& OutError)
{
    if (!IsValidId(Id)) return FailWorld(OutError, TEXT("That world does not exist."));
    const FString Path = UnnamedFiles::ReadablePath(GetWorldFile(Id));
    FString Json;
    if (!IFileManager::Get().FileExists(*Path)) return FailWorld(OutError, TEXT("That world does not exist."));
    if (IFileManager::Get().FileSize(*Path) > MaxWorldFileBytes || !FFileHelper::LoadFileToString(Json, *Path))
        return FailWorld(OutError, TEXT("world.json could not be read."));
    if (!FromJson(Json, Out, OutError)) return false;
    Out.Id = Id;
    return true;
}

TArray<FUnnamedSavedWorld> UnnamedSavedWorlds::List()
{
    TArray<FString> Folders;
    IFileManager::Get().FindFiles(Folders, *(GetRoot() / TEXT("*")), false, true);
    TArray<FUnnamedSavedWorld> Worlds;
    for (const FString& Folder : Folders)
    {
        FUnnamedSavedWorld World;
        FString Error;
        // A hand-made folder can never shadow a catalog world or reuse a retired id.
        if (!IsValidId(Folder) || IsWorldChoice(Folder, ReservedWorldIds) ||
            UUnnamedProjectSettings::Get().FindWorld(Folder))
            continue;
        if (Load(Folder, World, Error))
            Worlds.Add(MoveTemp(World));
        else
            UE_LOG(LogUnnamedWorlds, Warning, TEXT("Saved world %s is not listed: %s"), *Folder, *Error);
    }
    Worlds.Sort([](const FUnnamedSavedWorld& A, const FUnnamedSavedWorld& B)
        { return A.LastPlayed != B.LastPlayed ? A.LastPlayed > B.LastPlayed : A.Id < B.Id; });
    return Worlds;
}

bool UnnamedSavedWorlds::Rename(const FString& Id, const FString& Name, FString& OutError)
{
    FUnnamedSavedWorld World;
    return Load(Id, World, OutError) && CleanWorldName(Name, World.Name, OutError) && Save(World, OutError);
}

bool UnnamedSavedWorlds::Touch(const FString& Id, FString& OutError)
{
    FUnnamedSavedWorld World;
    if (!Load(Id, World, OutError)) return false;
    World.LastPlayed = FDateTime::UtcNow();
    return Save(World, OutError);
}

bool UnnamedSavedWorlds::Delete(const FString& Id, FString& OutError)
{
    if (!IsValidId(Id) || !IFileManager::Get().DirectoryExists(*GetWorldFolder(Id)))
        return FailWorld(OutError, TEXT("That world no longer exists."));
    if (!IFileManager::Get().DeleteDirectory(*GetWorldFolder(Id), true, true))
        return FailWorld(
            OutError, TEXT("The world could not be deleted. Close anything using its folder and try again."));
    UE_LOG(LogUnnamedWorlds, Display, TEXT("Deleted world %s"), *Id);
    return true;
}
