#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Foundation/FoundationBreakAssets.h"
#include "FoundationSoundAssets.generated.h"

class USoundAttenuation;

// A sound event of the sound reference, such as "entity.item.pickup"; a play picks one of its files at random.
USTRUCT(BlueprintType)
struct FFoundationSoundEvent
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
    FName Name;
    // The block reference's files (footsteps share the hit sounds' step files) are its /Game/Breaking waves.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
    TArray<FFoundationBreakSoundFile> Files;
    // Linear over the event's attenuation_distance (sounds.json; 16 blocks unless set, 12 for the chest).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
    TObjectPtr<USoundAttenuation> Attenuation;
};

// A block's step and fall events (facts/block_sounds.csv of the block reference).
USTRUCT(BlueprintType)
struct FFoundationSoundBlock
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
    FName Id;
    // Indices into Events; INDEX_NONE is silent (water's and lava's intentionally_empty).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
    int32 StepEvent = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
    int32 FallEvent = INDEX_NONE;
};

// The sound events beyond breaking (/Game/Sounds/DA_Sounds, made by Scripts/Editor/ImportSounds.py from
// Reference/minecraft-sounds-26.3; Docs/Breaking.md): item pickup and break, eating, player damage, containers, water,
// and every block's footstep and fall. Played through FFoundationBreakEffects::PlayEvent.
UCLASS(BlueprintType)
class UNNAMED_API UFoundationSoundAssets : public UDataAsset
{
    GENERATED_BODY()
public:
    // SHA-256 of the reference inputs the import read (SoundImportData.py), checked when the import is rerun.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
    FString SourceSha256;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
    TArray<FFoundationSoundEvent> Events;
    // Every block of the block reference's manifest, in DA_BreakAssets' Blocks order.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
    TArray<FFoundationSoundBlock> Blocks;
};
