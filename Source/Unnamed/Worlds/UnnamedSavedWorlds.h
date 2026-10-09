#pragma once

#include "CoreMinimal.h"

class FJsonObject;

// A world made by Create New World, saved as world.json in its own folder under UnnamedSavedWorlds::GetRoot(). The
// fields keep the Create screen's own keys and values ("gameMode": "Hardcore", "type": "Large Biomes"), so Re-Create
// refills the screen exactly; the game reads them through the helpers here.
struct UNNAMED_API FUnnamedSavedWorld
{
    // world.json's layout. A file from a newer version is refused, never rewritten.
    static constexpr int32 FormatVersion = 1;
    // The terrain generator a world was made with: 1 is phase 2's stand-in height terrain, 2 phase 4's density terrain
    // from the 26.3 graph (Superflat stays flat under both). Terrain is not saved, only the seed and these settings, so
    // a new generator must keep the old one to keep old worlds' land.
    static constexpr int32 CurrentGenerator = 2;

    // One Superflat layer as the screen lists it: a block id ("minecraft:dirt") and its height.
    struct FFlatLayer
    {
        FString Block;
        int32 Height = 1;
    };

    // The folder name and travel option. Made once from the name, then never changed: a saved id.
    FString Id;
    FString Name;
    // The seed text as typed (blank when it was random) and the 64-bit seed it made.
    FString SeedText;
    int64 Seed = 0;
    FString GameMode = TEXT("Survival");
    FString Difficulty = TEXT("Normal");
    bool bCommands = false;
    FString Type = TEXT("Default");
    bool bStructures = true;
    bool bBonusChest = false;
    FString Biome = TEXT("minecraft:plains");
    FString FlatBiome = TEXT("minecraft:plains");
    // Top layer first, as the screen lists them.
    TArray<FFlatLayer> FlatLayers;
    // Game rules (id -> boolean or integer) and experiments (id -> boolean), kept for the systems that will use them.
    TSharedPtr<FJsonObject> Rules, Experiments;
    // UTC.
    FDateTime Created, LastPlayed;
    int32 Generator = CurrentGenerator;

    bool IsHardcore() const { return GameMode == TEXT("Hardcore"); }
    bool IsSuperflat() const { return Type == TEXT("Superflat"); }
    // A boolean game rule, or Default when the world does not set it.
    bool GetRule(const FString& RuleId, bool bDefault) const;
    // The Create screen's draft for Re-Create: these settings, with a random seed written out as its number so the
    // new world matches.
    TSharedRef<FJsonObject> ToDraft() const;
};

// The saved-world store, on the game thread. Every id that arrives from the page passes IsValidId before it becomes a
// path, so no request can reach outside the root.
namespace UnnamedSavedWorlds
{
    // Saved/Worlds, or the folder given by -WorldsDir=.
    UNNAMED_API FString GetRoot();
#if WITH_DEV_AUTOMATION_TESTS
    // Points the store at a test folder; empty restores the normal root.
    UNNAMED_API void SetRootForTesting(const FString& Root);
#endif

    // Lowercase letters and digits in dash-separated runs, at most 48 characters.
    UNNAMED_API bool IsValidId(const FString& Id);
    // A new id from a world name: lowercase ASCII letters and digits joined by dashes ("My World!" -> "my-world"),
    // "world" when nothing is left, then "-2", "-3"... until it is free. Never a catalog world, a retired world id or a
    // Windows device name.
    UNNAMED_API FString MakeId(const FString& Name, TFunctionRef<bool(const FString&)> IsTaken);

    // Checks the Create screen's draft (JSON) and fills Out, without an id, dates or a random seed yet. Hardcore
    // forces Hard, no commands and no bonus chest, as the screen shows it.
    UNNAMED_API bool ParseDraft(const FString& DraftJson, FUnnamedSavedWorld& Out, FString& OutError);
    // ParseDraft, then a fresh id, a random seed when the text was blank, the dates, and the first save.
    UNNAMED_API bool Create(const FString& DraftJson, FUnnamedSavedWorld& Out, FString& OutError);
    UNNAMED_API bool Load(const FString& Id, FUnnamedSavedWorld& Out, FString& OutError);
    UNNAMED_API bool Save(const FUnnamedSavedWorld& World, FString& OutError);
    // Every readable world, most recently played first. Unreadable folders are logged and left alone.
    UNNAMED_API TArray<FUnnamedSavedWorld> List();
    // Gives a world a new name; its id, and so its folder, never change.
    UNNAMED_API bool Rename(const FString& Id, const FString& Name, FString& OutError);
    // Marks a world as played now.
    UNNAMED_API bool Touch(const FString& Id, FString& OutError);
    // Deletes a world's folder for good.
    UNNAMED_API bool Delete(const FString& Id, FString& OutError);

    // world.json's text and back; reading checks every field and refuses newer formats and generators.
    UNNAMED_API FString ToJson(const FUnnamedSavedWorld& World);
    UNNAMED_API bool FromJson(const FString& Json, FUnnamedSavedWorld& Out, FString& OutError);
}
