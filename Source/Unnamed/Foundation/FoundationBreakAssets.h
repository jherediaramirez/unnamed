#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FoundationBreakAssets.generated.h"

class USoundAttenuation;
class USoundConcurrency;
class USoundWave;
class UTexture2DArray;

// One file of a sound event (sounds.json): its imported wave, volume and pitch.
USTRUCT(BlueprintType)
struct FFoundationBreakSoundFile
{
    GENERATED_BODY()
    // sounds.json's name, such as "dig/stone1".
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    TObjectPtr<USoundWave> Wave;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    float Volume = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    float Pitch = 1;
};

// A sound event, such as "block.stone.break"; a play picks one of its files at random.
USTRUCT(BlueprintType)
struct FFoundationBreakSoundEvent
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    TArray<FFoundationBreakSoundFile> Files;
};

// A block's breaking facts: its sound type (facts/block_sounds.csv) and its model's particle texture
// (block_particles.json).
USTRUCT(BlueprintType)
struct FFoundationBreakBlock
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    FName Id;
    // Indices into Events; INDEX_NONE is silent (water's intentionally_empty).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    int32 BreakEvent = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    int32 HitEvent = INDEX_NONE;
    // The place event (block_sounds.csv's place). A block whose place files the block reference lacks has its break
    // event here instead (66 blocks in 26.3, such as deepslate; BreakImportData.py).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    int32 PlaceEvent = INDEX_NONE;
    // The sound type's own volume and pitch (metal's pitch is 1.5).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    float Volume = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    float Pitch = 1;
    // The default state's particle texture, a name in DA_Blocks' Textures, and whether a tinted face of the model
    // uses it (grass block's dirt does not; leaves do).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    FName Particle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    bool bParticleTinted = false;
    // States whose particle texture differs from the default's: state ids, with their textures and tint flags.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    TArray<int32> OverrideStates;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    TArray<FName> OverrideParticles;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    TArray<bool> OverrideTinted;
};

// The breaking effects' imported data (/Game/Breaking/DA_BreakAssets, made by Scripts/Editor/ImportBreakAssets.py
// from Reference/minecraft-blocks-26.3; Docs/Breaking.md).
UCLASS(BlueprintType)
class UNNAMED_API UFoundationBreakAssets : public UDataAsset
{
    GENERATED_BODY()
public:
    // SHA-256 of the reference inputs the import read (BreakImportData.py), checked when the import is rerun.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    FString SourceSha256;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    TArray<FFoundationBreakSoundEvent> Events;
    // Every block of the reference's manifest, in its order.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    TArray<FFoundationBreakBlock> Blocks;
    // 16 x 16 x 20: the carved stages in layers 0-9, vanilla's own stages in 10-19 (nearest, one mip, linear).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    TObjectPtr<UTexture2DArray> Cracks;
    // Linear over 16 blocks.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    TObjectPtr<USoundAttenuation> Attenuation;
    // At most two hit voices (Sounds 2).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking")
    TObjectPtr<USoundConcurrency> HitConcurrency;
};
