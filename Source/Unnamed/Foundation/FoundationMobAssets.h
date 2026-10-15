#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Foundation/FoundationItemAssets.h"
#include "FoundationMobAssets.generated.h"

class UMaterialInterface;
class UTexture2D;

// One face of a model cube, verbatim from Reference/minecraft-entities-26.3/facts/entity_models.json: four corners in
// the part's model pixels (Mojang: x left of the mob, y down, z behind its head), each with its texture UV (0-1).
USTRUCT()
struct FFoundationMobFaceAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Mobs")
    FVector Normal = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Mobs")
    TArray<FVector> Positions;

    UPROPERTY(EditAnywhere, Category = "Mobs")
    TArray<FVector2D> UVs;
};

// A model part (ModelPart): its path below the root ("head", "body"), its pivot and rest rotation (radians, applied
// Z, then Y, then X) relative to its parent, and its cubes' faces.
USTRUCT()
struct FFoundationMobPartAsset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Mobs")
    FName Path;

    UPROPERTY(EditAnywhere, Category = "Mobs")
    FVector Pivot = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Mobs")
    FVector Rotation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Mobs")
    TArray<FFoundationMobFaceAsset> Faces;
};

// One mob's look: its model layer ("cow#main"), its texture (a stand-in, Docs/StandIns.md) and that texture's size.
USTRUCT()
struct FFoundationMobModelAsset
{
    GENERATED_BODY()

    // The entity id ("cow").
    UPROPERTY(EditAnywhere, Category = "Mobs")
    FName Id;

    UPROPERTY(EditAnywhere, Category = "Mobs")
    FName Layer;

    UPROPERTY(EditAnywhere, Category = "Mobs")
    TSoftObjectPtr<UTexture2D> Texture;

    UPROPERTY(EditAnywhere, Category = "Mobs")
    FIntPoint TextureSize = FIntPoint::ZeroValue;

    UPROPERTY(EditAnywhere, Category = "Mobs")
    TArray<FFoundationMobPartAsset> Parts;
};

// DA_Mobs (survival T6 20a), written by Scripts/Editor/ImportMobs.py from Reference/minecraft-entities-26.3: the eight
// mobs' models and their entity loot tables. Unlocked, unlike DA_Items: it belongs to track T6.
UCLASS()
class UNNAMED_API UFoundationMobAssets : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // The reference's SHA256SUMS.txt digest the asset was imported from.
    UPROPERTY(EditAnywhere, Category = "Mobs")
    FString SourceSha256;

    UPROPERTY(EditAnywhere, Category = "Mobs")
    TArray<FFoundationMobModelAsset> Models;

    // The cutout material every mob's texture goes into (its Skin parameter; per-instance custom data 0 is the red
    // overlay [hurt.overlay]).
    UPROPERTY(EditAnywhere, Category = "Mobs")
    TSoftObjectPtr<UMaterialInterface> Material;

    // Entity loot tables, flattened like DA_Items' block tables; Block holds the table's name below entities/ ("cow",
    // "sheep/white").
    UPROPERTY(EditAnywhere, Category = "Mobs")
    TArray<FFoundationLootTableAsset> LootTables;

    // DA_Mobs under EntityRoot, loaded once; null before ImportMobs.py has run.
    static const UFoundationMobAssets* LoadDefault();
    // A mob's main layer (the first model with its id); FindLayers adds the layers drawn over it (the sheep's wool).
    const FFoundationMobModelAsset* FindModel(FName Id) const;
    void FindLayers(FName Id, TArray<const FFoundationMobModelAsset*>& Out) const;
    // Replaces Rules.EntityLootTables with these, items resolved against Rules.Items; how many were skipped.
    int32 AddLoot(FFoundationRules& Rules) const;
};
