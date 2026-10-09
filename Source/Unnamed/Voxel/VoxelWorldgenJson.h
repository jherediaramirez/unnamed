#pragma once

#include "CoreMinimal.h"
#include "Algo/Find.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// The strict-JSON pieces the worldgen readers share (VoxelWorldgenReader.cpp, VoxelSurfaceReader.cpp).
namespace VoxelWorldgenJson
{
    inline const TCHAR* const WorldgenIdPrefix = TEXT("minecraft:");

    // Field names match case-sensitively: FString's own comparisons and the engine's JSON lookups ignore case.
    inline bool IsWorldgenKey(const FString& Key, const TCHAR* Name)
    {
        return Key.Equals(Name, ESearchCase::CaseSensitive);
    }

    inline bool HasOnlyWorldgenKeys(const FJsonObject& Object, std::initializer_list<const TCHAR*> Keys)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
        {
            if (!Algo::FindByPredicate(Keys, [&Field](const TCHAR* Key) { return IsWorldgenKey(Field.Key, Key); }))
            {
                return false;
            }
        }
        return true;
    }

    // Whether Object holds a key that is not in Keys (an empty name counts), and the first such key.
    inline bool FindUnexpectedWorldgenKey(
        const FJsonObject& Object, std::initializer_list<const TCHAR*> Keys, FString& OutKey)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
        {
            if (!Algo::FindByPredicate(Keys, [&Field](const TCHAR* Key) { return IsWorldgenKey(Field.Key, Key); }))
            {
                OutKey = Field.Key;
                return true;
            }
        }
        return false;
    }

    // "minecraft:overworld/continents" -> "density_function/overworld/continents.json" for Folder "density_function".
    // Ids are lowercase names and folders, so a reference can never leave the reference folder.
    inline bool WorldgenIdToPath(const FString& Id, const TCHAR* Folder, FString& OutPath)
    {
        if (!Id.StartsWith(WorldgenIdPrefix, ESearchCase::CaseSensitive))
        {
            return false;
        }
        const FString Name = Id.RightChop(FCString::Strlen(WorldgenIdPrefix));
        if (Name.IsEmpty() || Name.StartsWith(TEXT("/")) || Name.Contains(TEXT("..")))
        {
            return false;
        }
        for (const TCHAR Char : Name)
        {
            if (!(FChar::IsLower(Char) || FChar::IsDigit(Char) || Char == TEXT('_') || Char == TEXT('-') ||
                    Char == TEXT('/') || Char == TEXT('.')))
            {
                return false;
            }
        }
        OutPath = FString::Printf(TEXT("%s/%s.json"), Folder, *Name);
        return true;
    }

    inline bool LoadWorldgenFile(const FString& Root, const FString& Path, FString& OutText)
    {
        return FFileHelper::LoadFileToString(OutText, *FPaths::Combine(Root, TEXT("data/minecraft/worldgen"), Path));
    }

    // A file's one JSON value. The text is wrapped in an array, because the engine's reader wants an object or an
    // array at the top and a density function file may be a bare number.
    inline TSharedPtr<FJsonValue> ParseWorldgenJson(const FString& Text)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("[") + Text + TEXT("]")), Values) ||
            Values.Num() != 1 || !Values[0].IsValid())
        {
            return nullptr;
        }
        return Values[0];
    }
} // namespace VoxelWorldgenJson
