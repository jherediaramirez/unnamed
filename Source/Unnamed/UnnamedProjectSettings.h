#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"
#include "UnnamedProjectSettings.generated.h"

class UInputAction;
class UInputMappingContext;
class UMaterialInterface;

// A world the menu may open. The browser only ever sends Id; the map path stays native.
USTRUCT()
struct FUnnamedWorldEntry
{
    GENERATED_BODY()

    // Stable catalog ID, e.g. "playground". Never a path or command.
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

    // Package of the generated voxel world's map ("/Game/Maps/L_Generated").
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetGeneratedMapPackage();

    // Object path ("/Game/Input/...IMC_OnFoot.IMC_OnFoot") of a named input asset, or empty.
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetInputAssetPath(FName AssetName);

    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetHudRegistryPath();
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetHudTexturePackage(const FString& RelativeTexture);

    UPROPERTY(Config, EditAnywhere, Category = "Hud")
    FString HudRoot = TEXT("/Game/Hud");

    // Package of a generated block asset under BlockRoot, e.g. "DA_Blocks" (the registry), "T_BlockTextures",
    // "M_BlockOpaque". Written by Scripts/Editor/ImportBlocks.py and CreateBlockMaterials.py.
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetBlockAssetPackage(const FString& AssetName);
    // Object path of the same asset ("/Game/Blocks/DA_Blocks.DA_Blocks").
    static FSoftObjectPath GetBlockAssetPath(const FString& AssetName);

    UPROPERTY(Config, EditAnywhere, Category = "Blocks")
    FString BlockRoot = TEXT("/Game/Blocks");

    // Package of a generated item asset under ItemRoot, e.g. "DA_Items" (the registry) or "Gui/item/iron_pickaxe".
    // Written by Scripts/Editor/ImportItems.py.
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetItemAssetPackage(const FString& AssetName);
    static FSoftObjectPath GetItemAssetPath(const FString& AssetName);

    UPROPERTY(Config, EditAnywhere, Category = "Items")
    FString ItemRoot = TEXT("/Game/Items");

    // Package of a breaking-effects asset under BreakRoot, e.g. "DA_BreakAssets" or "Sounds/dig/stone1". Written by
    // Scripts/Editor/ImportBreakAssets.py and CreateBreakMaterials.py (Docs/Breaking.md).
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetBreakAssetPackage(const FString& AssetName);
    static FSoftObjectPath GetBreakAssetPath(const FString& AssetName);

    UPROPERTY(Config, EditAnywhere, Category = "Breaking")
    FString BreakRoot = TEXT("/Game/Breaking");

    // Package of a sound asset under SoundRoot, e.g. "DA_Sounds" or "Sounds/random/pop". Written by
    // Scripts/Editor/ImportSounds.py from Reference/minecraft-sounds-26.3 (Docs/Breaking.md).
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetSoundAssetPackage(const FString& AssetName);
    static FSoftObjectPath GetSoundAssetPath(const FString& AssetName);

    UPROPERTY(Config, EditAnywhere, Category = "Sounds")
    FString SoundRoot = TEXT("/Game/Sounds");

    // Package of an entity asset under EntityRoot, e.g. "T_ExperienceOrb" or "M_ExperienceOrb". Written by
    // Scripts/Editor/CreateEntityAssets.py from Reference/minecraft-entities-26.3 (Docs/Items.md, "Experience orbs").
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetEntityAssetPackage(const FString& AssetName);
    static FSoftObjectPath GetEntityAssetPath(const FString& AssetName);

    UPROPERTY(Config, EditAnywhere, Category = "Entities")
    FString EntityRoot = TEXT("/Game/Entities");

    // Package of a player body asset under PlayerBodyRoot, e.g. "SK_BetterSteve" or "BS_Walk_F". Written by
    // Scripts/Editor/ImportBetterSteve.py from Art/Characters/BetterSteve (Docs/Foundation/Architecture.md).
    UFUNCTION(BlueprintPure, Category = "Unnamed|Paths")
    static FString GetPlayerBodyAssetPackage(const FString& AssetName);
    static FSoftObjectPath GetPlayerBodyAssetPath(const FString& AssetName);

    UPROPERTY(Config, EditAnywhere, Category = "Player Body")
    FString PlayerBodyRoot = TEXT("/Game/Characters/BetterSteve");

    UPROPERTY(Config, EditAnywhere, Category = "Maps", meta = (AllowedClasses = "/Script/Engine.World"))
    FSoftObjectPath MenuMap;

    // Playable Singleplayer entries, in list order. The first is selected by default.
    UPROPERTY(Config, EditAnywhere, Category = "Maps")
    TArray<FUnnamedWorldEntry> Worlds;

    // The procedural map every generated world opens: an AVoxelWorld, the player start and the daylight rig, made by
    // Scripts/Editor/CreateGeneratedMap.py. Not a catalog world: generated worlds come from Create New World.
    UPROPERTY(Config, EditAnywhere, Category = "Maps", meta = (AllowedClasses = "/Script/Engine.World"))
    FSoftObjectPath GeneratedMap;

    // The prototype surface of every lab solid and block: a world-aligned 1 m grid tinted by its "Color"
    // parameter. Created by Scripts/Editor/CreateDevGridMaterial.py.
    UPROPERTY(Config, EditAnywhere, Category = "Maps")
    TSoftObjectPtr<UMaterialInterface> DevGridMaterial;

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
