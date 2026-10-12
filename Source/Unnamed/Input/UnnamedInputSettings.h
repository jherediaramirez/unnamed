#pragma once

#include "CoreMinimal.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "UnnamedInputSettings.generated.h"

UCLASS()
class UNNAMED_API UUnnamedInputSettings : public UEnhancedInputUserSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame, BlueprintReadOnly)
    bool bToggleSprint = false;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    bool bToggleCrouch = false;
    UPROPERTY(SaveGame)
    int32 FoundationSchemaVersion = 0;
    UPROPERTY(SaveGame)
    float DebugTextScale = 1;
    UPROPERTY(SaveGame)
    int32 DebugSections = 31;
    UPROPERTY(SaveGame)
    bool bDebugUnrealCoordinates = false;
    bool SaveChecked(FString& OutError);
    // SaveChecked's write: the bytes SaveGameToSlot would write, put in the slot's file crash-safely
    // (UnnamedFiles::SaveBytesAtomically: a .tmp file, then the previous save kept as .bak).
    bool WriteSlot(const FString& SlotName);
    // A slot's file as the generic save system names it: <Saved>/SaveGames/<slot>.sav (SaveGameSystem.h).
    static FString GetSlotPath(const FString& SlotName);
    // The slot every player's settings use: Enhanced Input's InputSettingsSaveSlotName (Config/DefaultInput.ini),
    // which GetSaveFilename returns by default. Known before any local player exists.
    static FString GetDefaultSlotName();
    // Puts <slot>.sav.bak back when <slot>.sav is missing or empty (a save stopped between its backup and its move).
    static bool RestoreSlot(const FString& SlotName);
};
