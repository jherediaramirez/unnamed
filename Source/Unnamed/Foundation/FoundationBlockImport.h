#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FoundationBlockImport.generated.h"

class UTexture2D;
class UTexture2DArray;

// Editor-only steps of Scripts/Editor/ImportBlocks.py that editor Python cannot do itself: creating texture
// array assets, decoding the pinned PNGs, generating 26.3 mipmaps and filling source data. Every function
// returns an error message, or empty on success; in a game build they do nothing.
UCLASS()
class UNNAMED_API UFoundationBlockImport : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // A new, empty texture array or 2D texture asset at a long package name ("/Game/Blocks/T_BlockTextures").
    UFUNCTION(BlueprintCallable, Category = "Unnamed|Blocks")
    static UTexture2DArray* CreateTextureArray(const FString& PackageName);
    UFUNCTION(BlueprintCallable, Category = "Unnamed|Blocks")
    static UTexture2D* CreateTexture(const FString& PackageName);

    // Fills Array: layer i is frame FrameIndices[i] of PngPaths[i] (square frames of FrameSize stacked
    // vertically), with MipCount levels made by 26.3's MipmapGenerator using MipStrategies[i]. Mip 0 is the
    // PNG's pixels unchanged. Uncompressed BGRA8, sRGB, nearest filtering, the mips left as built. A path
    // of "white" makes a plain white layer.
    UFUNCTION(BlueprintCallable, Category = "Unnamed|Blocks")
    static FString BuildTextureArray(UTexture2DArray* Array, const TArray<FString>& PngPaths,
        const TArray<int32>& FrameIndices, const TArray<FString>& MipStrategies, int32 FrameSize, int32 MipCount);

    // Every layer's mip 0 of a 16x16 array in a grid of Columns tiles, one mip, for Slate item icons.
    UFUNCTION(BlueprintCallable, Category = "Unnamed|Blocks")
    static FString BuildIconAtlas(UTexture2D* Atlas, UTexture2DArray* Array, int32 Columns);

    // SHA-256 (hex) of a layer's mip-0 RGBA bytes, as Reference/minecraft-blocks-26.3/textures.json records them.
    UFUNCTION(BlueprintCallable, Category = "Unnamed|Blocks")
    static FString HashLayer(UTexture2DArray* Array, int32 Layer);

    // Average of a layer's visible mip-0 pixels, optionally multiplied by a tint (break particles).
    UFUNCTION(BlueprintCallable, Category = "Unnamed|Blocks")
    static FColor AverageLayerColor(UTexture2DArray* Array, int32 Layer, FColor Tint);

    // A container of registered tags by name; unknown names are an error.
    UFUNCTION(BlueprintCallable, Category = "Unnamed|Blocks")
    static FGameplayTagContainer MakeTags(const TArray<FString>& Names, FString& OutError);

    // Reads a texture array's mip-0 layer as RGBA8 (editor data). False when unavailable.
    static bool ReadLayer(UTexture2DArray* Array, int32 Layer, TArray<uint8>& OutRGBA);
};
