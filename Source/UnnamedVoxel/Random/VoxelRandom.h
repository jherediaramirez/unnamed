#pragma once

#include "CoreMinimal.h"

// Stateless, counter-based randomness. Every random value in generation is a pure function of (world seed, subsystem
// salt, coordinates), so any thread can build any column in any order and get the same blocks. A stream
// (FVoxelXoroshiro) lives only inside one computation that needs several values, seeded from such a hash; no stream is
// ever shared between columns. Every constant here decides saved worlds, so tests pin them against independent vectors.
namespace VoxelRandom
{
    constexpr uint64 GoldenGamma = 0x9E3779B97F4A7C15ull;

    constexpr uint64 RotateLeft(uint64 Value, int32 Shift)
    {
        return Value << Shift | Value >> (64 - Shift);
    }

    // The SplitMix64 finalizer (Steele, Lea and Flood, 2014): a bijective mixer in which every input bit flips each
    // output bit with probability about one half.
    constexpr uint64 Mix64(uint64 Value)
    {
        Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
        Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
        return Value ^ (Value >> 31);
    }

    // SplitMix64: advances State and returns its next output. Expands one seed into several independent ones.
    constexpr uint64 SplitMix64(uint64& State)
    {
        State += GoldenGamma;
        return Mix64(State);
    }

    // 64-bit FNV-1a, for names and seed text.
    constexpr uint64 Fnv1a64(const uint8* Bytes, int32 Count)
    {
        uint64 Hash = 0xCBF29CE484222325ull;
        for (int32 Index = 0; Index < Count; ++Index)
            Hash = (Hash ^ Bytes[Index]) * 0x100000001B3ull;
        return Hash;
    }

    constexpr uint64 Fnv1a64(const char* Text)
    {
        uint64 Hash = 0xCBF29CE484222325ull;
        for (; *Text; ++Text)
            Hash = (Hash ^ uint8(*Text)) * 0x100000001B3ull;
        return Hash;
    }

    // A subsystem's salt from its name, at compile time: constexpr uint64 Terrain = VoxelRandom::Salt("terrain").
    // Mixed, because FNV-1a alone leaves similar names with similar low bits.
    constexpr uint64 Salt(const char* Name)
    {
        return Mix64(Fnv1a64(Name));
    }

    // The positional hash every random choice starts from. Seed, salt and the coordinates are each folded in through a
    // full Mix64 round, so neighbouring cells, neighbouring seeds and similar salts give unrelated values. 2D callers
    // leave Z at 0 under their own salt.
    constexpr uint64 Hash64(uint64 Seed, uint64 SubsystemSalt, int32 X, int32 Y, int32 Z = 0)
    {
        uint64 Hash = Mix64(Seed ^ Mix64(SubsystemSalt + GoldenGamma));
        Hash = Mix64(Hash ^ (uint64(uint32(X)) | uint64(uint32(Y)) << 32));
        return Mix64(Hash ^ uint64(uint32(Z)));
    }

    // Values from 64 random bits; they use the high bits.
    // [0, 1) with 24 bits of precision.
    constexpr float ToUnitFloat(uint64 Bits)
    {
        return float(Bits >> 40) * (1.f / 16777216.f);
    }

    // [0, 1) with 53 bits of precision.
    constexpr double ToUnitDouble(uint64 Bits)
    {
        return double(Bits >> 11) * (1. / 9007199254740992.);
    }

    // [0, Bound) for Bound > 0 by multiply-high. There is no rejection loop, so it stays a pure function of Bits; the
    // bias is below Bound / 2^32.
    constexpr int32 ToIndex(uint64 Bits, int32 Bound)
    {
        return int32((uint64(uint32(Bits >> 32)) * uint64(Bound)) >> 32);
    }

    // [Min, Max], both included.
    constexpr int32 ToRange(uint64 Bits, int32 Min, int32 Max)
    {
        return Min + ToIndex(Bits, Max - Min + 1);
    }
}

// Xoroshiro128++ (Blackman and Vigna, 2019), the generator the reference uses for its noise: a small, fast stream for
// one computation that needs several values, such as one feature or one structure layout. Seed it from
// VoxelRandom::Hash64 and never keep one across columns.
struct FVoxelXoroshiro
{
    uint64 S0 = 0;
    uint64 S1 = 0;

    // Expands Seed with SplitMix64, so neighbouring or low-entropy seeds still start far apart.
    constexpr explicit FVoxelXoroshiro(uint64 Seed)
    {
        uint64 State = Seed;
        S0 = VoxelRandom::SplitMix64(State);
        S1 = VoxelRandom::SplitMix64(State);
        // The all-zero state is the generator's one fixed point.
        if ((S0 | S1) == 0) S1 = VoxelRandom::GoldenGamma;
    }

    // A raw state, for the reference test vectors.
    constexpr FVoxelXoroshiro(uint64 InS0, uint64 InS1) : S0(InS0), S1(InS1) {}

    constexpr uint64 Next()
    {
        const uint64 A = S0;
        uint64 B = S1;
        const uint64 Result = VoxelRandom::RotateLeft(A + B, 17) + A;
        B ^= A;
        S0 = VoxelRandom::RotateLeft(A, 49) ^ B ^ (B << 21);
        S1 = VoxelRandom::RotateLeft(B, 28);
        return Result;
    }

    float NextFloat() { return VoxelRandom::ToUnitFloat(Next()); }
    double NextDouble() { return VoxelRandom::ToUnitDouble(Next()); }
    bool NextChance(float Probability) { return NextFloat() < Probability; }

    // [0, Bound) for Bound > 0, exactly uniform (Lemire's multiply-high with rejection).
    int32 NextInt(int32 Bound)
    {
        uint64 Product = uint64(uint32(Next() >> 32)) * uint64(Bound);
        if (uint32(Product) < uint32(Bound))
        {
            const uint32 Threshold = (0u - uint32(Bound)) % uint32(Bound);
            while (uint32(Product) < Threshold)
                Product = uint64(uint32(Next() >> 32)) * uint64(Bound);
        }
        return int32(Product >> 32);
    }

    // [Min, Max], both included.
    int32 NextRange(int32 Min, int32 Max) { return Min + NextInt(Max - Min + 1); }
};
