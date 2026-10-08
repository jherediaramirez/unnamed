#pragma once

#include "CoreMinimal.h"

/**
 * Exact C++ port of Minecraft Java 26.3 block-texture mipmap generation
 * (net.minecraft.client.renderer.texture.MipmapGenerator.generateMipLevels for a block sprite frame).
 * Verified bit-for-bit against vanilla output (see sandbox/mc-26.3-creative/mipmap).
 */
namespace FoundationBlockMipmaps
{
    // 26.3 MipmapStrategy (texture .mcmeta "mipmap_strategy").
    enum class EStrategy : uint8
    {
        Auto,
        Mean,
        Cutout,
        StrictCutout,
        DarkCutout
    };

    // Vanilla's default "alpha_cutoff_bias" (TextureMetadataSection.DEFAULT_ALPHA_CUTOFF_BIAS).
    constexpr float DefaultAlphaCutoffBias = 0.0f;

    // "auto", "mean", "cutout", "strict_cutout", "dark_cutout" (case-sensitive, like vanilla); anything else is Auto.
    UNNAMED_API EStrategy ParseStrategy(const FString& Name);

    // MipmapGenerator.generateMipLevels for one block sprite frame (not an item texture). Pixels: Width*Height RGBA8
    // (bytes R, G, B, A; rows top to bottom). Returns MipCount levels, level 0 first, each RGBA8. Level 0 is returned
    // UNCHANGED (the original pixels). Levels 1+ are exactly what vanilla generates, including vanilla's level-0
    // preprocessing (solidify / dark fill) applied to a copy before downsampling, AUTO resolved from the pixels the
    // way vanilla resolves it, and alpha-coverage scaling for the cutout strategies.
    //
    // Level N has size (Width >> N) x (Height >> N); generation stops early (fewer than MipCount levels are returned)
    // once a dimension would reach 0, since vanilla cannot create such an image. Invalid input returns an empty array.
    UNNAMED_API TArray<TArray<uint8>> Generate(TConstArrayView<uint8> Pixels, int32 Width, int32 Height, int32 MipCount,
        EStrategy Strategy, float AlphaCutoffBias = DefaultAlphaCutoffBias);
}
