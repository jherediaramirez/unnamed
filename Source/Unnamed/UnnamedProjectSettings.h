#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"
#include "UnnamedProjectSettings.generated.h"

class UInputAction;
class UInputMappingContext;

// A world the menu may open. The browser only ever sends Id; the map path stays native.
USTRUCT()
struct FUnnamedWorldEntry
{
    GENERATED_BODY()

    // Stable catalog ID, e.g. "super-flat". Never a path or command.
    UPROPERTY(Config, EditAnywhere, Category = "World")
    FName Id;

    UPROPERTY(Config, EditAnywhere, Category = "World", meta = (AllowedClasses = "/Script/Engine.World"))
    FSoftObjectPath Map;

    // Text the Singleplayer list shows for this world.
    UPROPERTY(Config, EditAnywhere, Category = "World")
    FString Name;

    UPROPERTY(Config, EditAnywhere, Category = "World")
    FString Summary;

    UPROPERTY(Config, EditAnywhere, Category = "World")
    FString Status;
};

// The one place for map, world and input asset references (Project Settings > Game > Unnamed).
// Editor Python reads the same values through ProjectPaths.py.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Unnamed"))
class UNNAMED_API UUnnamedProjectSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UUnnamedProjectSettings();

    static const UUnnamedProjectSettings& Get() { return *GetDefault<UUnnamedProjectSettings>(); }

    const FUnnamedWorldEntry* FindWorld(const FString& Id) const;

    // Long package name ("/Game/Menu/L_Menu") for travel and editor scripts.
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetMenuMapPackage();

    // Package of a catalog world, or empty for an unknown ID.
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetWorldMapPackage(const FString& WorldId);

    // Object path ("/Game/Input/...IMC_OnFoot.IMC_OnFoot") of a named input asset, or empty.
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetInputAssetPath(FName AssetName);

    UPROPERTY(Config, EditAnywhere, Category = "Maps", meta = (AllowedClasses = "/Script/Engine.World"))
    FSoftObjectPath MenuMap;

    // Playable Singleplayer entries, in list order. The first is selected by default.
    UPROPERTY(Config, EditAnywhere, Category = "Maps")
    TArray<FUnnamedWorldEntry> Worlds;

    UPROPERTY(Config, EditAnywhere, Category = "Input")
    TSoftObjectPtr<UInputMappingContext> OnFootContext;

    UPROPERTY(Config, EditAnywhere, Category = "Input")
    TSoftObjectPtr<UInputMappingContext> FoundationActionsContext;

    UPROPERTY(Config, EditAnywhere, Category = "Input")
    TSoftObjectPtr<UInputAction> MoveAction;

    UPROPERTY(Config, EditAnywhere, Category = "Input")
    TSoftObjectPtr<UInputAction> LookAction;

    UPROPERTY(Config, EditAnywhere, Category = "Input")
    TSoftObjectPtr<UInputAction> SprintAction;

    UPROPERTY(Config, EditAnywhere, Category = "Input")
    TSoftObjectPtr<UInputAction> CrouchAction;

    UPROPERTY(Config, EditAnywhere, Category = "Input")
    TSoftObjectPtr<UInputAction> JumpAction;
};
