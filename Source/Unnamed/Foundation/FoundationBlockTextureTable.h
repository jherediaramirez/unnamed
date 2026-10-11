#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FoundationBlockTextureTable.generated.h"

class UFoundationBlockAssets;
class UTexture2D;

// The per-texture lookup the block materials read: texel i holds (layer, next layer, blend, array) for texture i of
// UFoundationBlockAssets::Textures, the array 1 for the 32x32 flow array (water_flow, lava_flow) and 0 for the block
// array, so a face samples the array its texture is in whatever side it is (a sloped fluid top draws the flow texture).
// Static textures never change; animated ones are rewritten each client tick (20 per second) from their 26.3 frame
// tables. The block materials and a future chunk mesher's materials share it, so animation never touches instance or
// vertex data.
UCLASS()
class UNNAMED_API UFoundationBlockTextureTable : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(const UFoundationBlockAssets* Assets);
    // Writes the animated textures' rows for this tick; no work when the tick has not changed.
    void SetTick(int64 Tick);
    int64 GetTick() const { return Tick; }
    UTexture2D* GetTexture() const { return Texture; }
    int32 GetTextureCount() const { return Rows.Num() / 4; }
    // The layer a texture shows now (for icons, which draw one frame without blending).
    int32 GetCurrentLayer(int32 TextureIndex) const;

private:
    void Upload();

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> Texture;
    UPROPERTY(Transient)
    TObjectPtr<const UFoundationBlockAssets> Assets;
    // RGBA32F per texture.
    TArray<float> Rows;
    TArray<int32> Animated;
    int64 Tick = -1;
};
