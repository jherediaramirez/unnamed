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
};
