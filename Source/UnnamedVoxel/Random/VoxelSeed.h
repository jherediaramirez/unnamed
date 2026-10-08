#pragma once

#include "CoreMinimal.h"

// How a world's seed was chosen.
enum class EVoxelSeedSource : uint8
{
    // The text was blank: a random seed, picked once when the world is created and then saved with it.
    Random,
    // The text was a whole number in the signed 64-bit range, used as typed.
    Number,
    // Any other text, through FVoxelSeed::HashText.
    Text
};

// A world seed: one signed 64-bit number, as in the reference. Create New World parses the player's text once and
// saves the result; generation only ever sees Value.
struct UNNAMEDVOXEL_API FVoxelSeed
{
    int64 Value = 0;
    EVoxelSeedSource Source = EVoxelSeedSource::Number;

    // Surrounding whitespace is ignored. Blank picks a random seed; an optionally signed decimal integer that fits in
    // 64 bits is used as typed ("+7", "-0042"); anything else, including numbers out of range, is hashed. These are the
    // reference's rules except the hash: 64 bits instead of Java's 32-bit String.hashCode, so text seeds can reach
    // every world.
    static FVoxelSeed Parse(FStringView Text);

    // FNV-1a 64 over the text's UTF-8 bytes, finished with VoxelRandom::Mix64. Tests pin it: changing it would change
    // every text-seeded world.
    static int64 HashText(FStringView Text);

    // A fresh random seed: the one non-deterministic step in world creation, taken once and saved with the world.
    static int64 MakeRandom();

    // "-3224043261468865476 (text)".
    FString Describe() const;

    bool operator==(const FVoxelSeed& Other) const = default;
};
