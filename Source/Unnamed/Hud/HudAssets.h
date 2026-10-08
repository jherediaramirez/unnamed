#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HudAssets.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct FHudSpriteAsset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere)
    FName Id;
    UPROPERTY(EditAnywhere)
    FString SourcePath;
    UPROPERTY(EditAnywhere)
    FString SourceSha256;
    UPROPERTY(EditAnywhere)
    TSoftObjectPtr<UTexture2D> Texture;
    UPROPERTY(EditAnywhere)
    FIntPoint NativeSize = FIntPoint::ZeroValue;
    UPROPERTY(EditAnywhere)
    FIntPoint FrameSize = FIntPoint::ZeroValue;
    UPROPERTY(EditAnywhere)
    TArray<int32> Frames;
    UPROPERTY(EditAnywhere)
    TArray<int32> FrameTicks;
    // Original MCMeta, including GUI nine-slice borders/stretch_inner and interpolation.
    UPROPERTY(EditAnywhere)
    FString MetadataJson;
    UPROPERTY(EditAnywhere)
    FString Role;
    UPROPERTY(EditAnywhere)
    FString ImportPreset;
};

USTRUCT(BlueprintType)
struct FHudGlyph
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere)
    int32 Code = 0;
    UPROPERTY(EditAnywhere)
    FIntPoint Origin = FIntPoint::ZeroValue;
    UPROPERTY(EditAnywhere)
    int32 InkWidth = 0;
    UPROPERTY(EditAnywhere)
    int32 Advance = 0;
};

// Cooked registry. Source art remains untouched in Reference; only presentation data lives here.
UCLASS(BlueprintType)
class UNNAMED_API UHudAssets : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere)
    FString RegistryJson;
    UPROPERTY(EditAnywhere)
    TArray<FHudSpriteAsset> Sprites;
    UPROPERTY(EditAnywhere)
    TArray<FHudGlyph> Glyphs;
    UPROPERTY(EditAnywhere)
    int32 FontHeight = 8;
    UPROPERTY(EditAnywhere)
    int32 FontAscent = 7;
};
