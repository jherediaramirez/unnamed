#pragma once

#include "CoreMinimal.h"
#include "Hud/HudModel.h"

class FFoundationAuthority;
class FVoxelWorldClock;
struct FFoundationVitals;

// What a chat command acts on: the one local player (step 15: single player, no permissions, no other entities).
struct FFoundationCommandContext
{
    FFoundationAuthority* Authority = nullptr;
    int32 PlayerId = 0;
    FFoundationVitals* Vitals = nullptr;
    // The project has no username yet: "Player" stands in (flagged in Docs/Commands.md).
    FString PlayerName = TEXT("Player");
    // The player's feet (world cm), and what F3 converts them with: the Minecraft origin and the world's rebase.
    FVector FeetCm = FVector::ZeroVector, MinecraftOriginCm = FVector::ZeroVector;
    FIntVector RebaseCm = FIntVector::ZeroValue;
    // Moves the player's feet to a world position (cm); the pawn's TeleportFeet.
    TFunction<void(const FVector& FeetCm)> Teleport;
    // The world clock; none in the Playground, where /time reports vanilla's no-default-clock error.
    FVoxelWorldClock* Clock = nullptr;
    // The opened saved world's "Allow Cheats" (Hardcore forces it off); the Playground and the dev world allow them.
    // Off, every command answers as an unknown one, as vanilla hides commands above the player's permission level.
    bool bCommands = true;
};

// The chat's slash commands, parsed as vanilla's (brigadier) and answered in vanilla's words from the 26.3
// en_us.json: /gamemode, /give, /tp (/teleport), /time and /kill. Gameplay goes through the authority, the vitals
// and the world clock; nothing here draws.
namespace FoundationCommands
{
    // Runs one chat line starting with '/'; returns the chat lines it prints (feedback in white, errors in red with
    // vanilla's gray context line). A command that changes nothing (the mode already set) prints nothing, as vanilla.
    UNNAMED_API TArray<TArray<FHudChatRun>> Run(const FString& Line, FFoundationCommandContext& Context);

    // The vanilla strings the commands print, by en_us.json key (Unnamed.Foundation.Commands.Lang pins them).
    struct FLangString
    {
        const TCHAR* Key;
        const TCHAR* Text;
    };
    UNNAMED_API TConstArrayView<FLangString> GetLangStrings();
    // A vanilla string with its %s (and %1$s) arguments filled in order.
    UNNAMED_API FString Format(const TCHAR* Key, TConstArrayView<FString> Args = {});
}
