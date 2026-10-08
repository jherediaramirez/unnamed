#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Random/VoxelRandom.h"
#include "Random/VoxelSeed.h"

// Expected values come from an independent Python implementation of the published algorithms and, for FNV-1a, the
// published test vectors. They decide every saved world: never update them to match changed code.
namespace
{
    constexpr uint64 SplitMixZero[] = {0xE220A8397B1DCDAFull, 0x6E789E6AA1B965F4ull, 0x06C45D188009454Full,
        0xF88BB8A8724C81ECull, 0x1B39896A51A8749Bull};
    constexpr uint64 SplitMix1234567[] = {0x599ED017FB08FC85ull, 0x2C73F08458540FA5ull, 0x883EBCE5A3F27C77ull,
        0x3FBEF740E9177B3Full, 0xE3B8346708CB5ECDull};
    constexpr uint64 XoroshiroRaw12[] = {0x0000000000060001ull, 0x000260C000660007ull, 0x180ACC04718606D3ull,
        0x9E226D35036FC4C7ull, 0x849BC9AC6B960BE4ull};
    constexpr uint64 XoroshiroSeed0[] = {0x6F68E1E7E2646EE1ull, 0xBF971B7F454094ADull, 0x48F2DE556F30DE38ull,
        0x6EA7C59F89BBFC75ull, 0x765437C08F02E2F5ull};
    constexpr uint64 XoroshiroSeed42[] = {0xE88AF6CAEF1D3C23ull, 0x54A303B2A5A54931ull, 0xF370812CCD646345ull,
        0x345839C63F9ABB35ull, 0x57C3B20E1A93EB7Full};

    // Compile-time proofs: the salts and hashes are constexpr, so the build itself checks them.
    static_assert(VoxelRandom::Fnv1a64("") == 0xCBF29CE484222325ull, "FNV-1a 64 published vector");
    static_assert(VoxelRandom::Fnv1a64("a") == 0xAF63DC4C8601EC8Cull, "FNV-1a 64 published vector");
    static_assert(VoxelRandom::Fnv1a64("foobar") == 0x85944171F73967E8ull, "FNV-1a 64 published vector");
    static_assert(VoxelRandom::Salt("terrain") == 0x2F8CB1AD18E0E985ull, "terrain salt");
    static_assert(VoxelRandom::Salt("caves") == 0xA93103C8118171FDull, "caves salt");
    static_assert(VoxelRandom::Salt("ores") == 0x254E1B28F51E191Bull, "ores salt");
    static_assert(VoxelRandom::Salt("structures") == 0xE0E974D598C5B64Dull, "structures salt");
    static_assert(VoxelRandom::Hash64(0, VoxelRandom::Salt("terrain"), 0, 0, 0) == 0xF756A1D47B9D7B7Aull, "Hash64");
    static_assert(VoxelRandom::Hash64(12345, VoxelRandom::Salt("terrain"), 1, 0, 0) == 0x04BF7EE056C12BCCull, "Hash64");
    static_assert(
        VoxelRandom::Hash64(uint64(-1), VoxelRandom::Salt("terrain"), -5, 7, 300) == 0x3D5E8141844980C2ull, "Hash64");
    static_assert(VoxelRandom::Hash64(uint64(MAX_int64), VoxelRandom::Salt("ores"), MIN_int32, MAX_int32, -64) ==
            0xCC6FD03706F12FC5ull,
        "Hash64 at the coordinate extremes");

    int32 CountBits(uint64 Value)
    {
        int32 Count = 0;
        for (; Value; Value &= Value - 1)
            ++Count;
        return Count;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelRandomVectorsTest, "Unnamed.Voxel.Random.Vectors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelRandomVectorsTest::RunTest(const FString& Parameters)
{
    auto Expect = [this](const TCHAR* What, uint64 Actual, uint64 Expected)
    { TestEqual(FString::Printf(TEXT("%s: 0x%016llX"), What, Actual), Actual, Expected); };

    uint64 State = 0;
    for (const uint64 Expected : SplitMixZero)
        Expect(TEXT("SplitMix64 from 0"), VoxelRandom::SplitMix64(State), Expected);
    State = 1234567;
    for (const uint64 Expected : SplitMix1234567)
        Expect(TEXT("SplitMix64 from 1234567"), VoxelRandom::SplitMix64(State), Expected);

    FVoxelXoroshiro Raw(1, 2);
    for (const uint64 Expected : XoroshiroRaw12)
        Expect(TEXT("Xoroshiro128++ from state (1, 2)"), Raw.Next(), Expected);
    FVoxelXoroshiro Zero(uint64(0));
    for (const uint64 Expected : XoroshiroSeed0)
        Expect(TEXT("Xoroshiro128++ seeded 0"), Zero.Next(), Expected);
    FVoxelXoroshiro FortyTwo(uint64(42));
    for (const uint64 Expected : XoroshiroSeed42)
        Expect(TEXT("Xoroshiro128++ seeded 42"), FortyTwo.Next(), Expected);

    // The runtime path must agree with the constexpr one the static_asserts checked.
    volatile int32 X = -5;
    Expect(TEXT("Hash64 at runtime"), VoxelRandom::Hash64(uint64(-1), VoxelRandom::Salt("terrain"), X, 7, 300),
        0x3D5E8141844980C2ull);
    const uint8 Foobar[] = {'f', 'o', 'o', 'b', 'a', 'r'};
    Expect(TEXT("FNV-1a 64 over bytes"), VoxelRandom::Fnv1a64(Foobar, 6), 0x85944171F73967E8ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSeedTest, "Unnamed.Voxel.Random.Seed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSeedTest::RunTest(const FString& Parameters)
{
    auto Expect = [this](const TCHAR* Text, int64 Value, EVoxelSeedSource Source)
    {
        const FVoxelSeed Seed = FVoxelSeed::Parse(Text);
        TestEqual(FString::Printf(TEXT("\"%s\" value"), Text), Seed.Value, Value);
        TestTrue(FString::Printf(TEXT("\"%s\" source"), Text), Seed.Source == Source);
    };
    // Whole numbers, as typed.
    Expect(TEXT("0"), 0, EVoxelSeedSource::Number);
    Expect(TEXT("-1"), -1, EVoxelSeedSource::Number);
    Expect(TEXT("+42"), 42, EVoxelSeedSource::Number);
    Expect(TEXT("-0042"), -42, EVoxelSeedSource::Number);
    Expect(TEXT("  123  "), 123, EVoxelSeedSource::Number);
    Expect(TEXT("9223372036854775807"), MAX_int64, EVoxelSeedSource::Number);
    Expect(TEXT("-9223372036854775808"), MIN_int64, EVoxelSeedSource::Number);
    // Text, through the pinned hash (values from the Python reference).
    Expect(TEXT("Unnamed"), -3224043261468865476ll, EVoxelSeedSource::Text);
    Expect(TEXT("a"), 198367012849983736ll, EVoxelSeedSource::Text);
    Expect(TEXT("Ñandú \U0001F30B"), -1101374920169030427ll, EVoxelSeedSource::Text);
    // One past the 64-bit range is text, not a wrapped number.
    Expect(TEXT("9223372036854775808"), 5072814224246344824ll, EVoxelSeedSource::Text);
    for (const TCHAR* NotANumber : {TEXT("-"), TEXT("+"), TEXT("12a"), TEXT("1 2"), TEXT("0x10"), TEXT("--1")})
        TestTrue(FString::Printf(TEXT("\"%s\" is text"), NotANumber),
            FVoxelSeed::Parse(NotANumber).Source == EVoxelSeedSource::Text);
    TestEqual(TEXT("Surrounding spaces do not change a text seed"), FVoxelSeed::Parse(TEXT(" Unnamed\t")).Value,
        FVoxelSeed::Parse(TEXT("Unnamed")).Value);
    TestNotEqual(TEXT("Text seeds are case-sensitive"), FVoxelSeed::Parse(TEXT("unnamed")).Value,
        FVoxelSeed::Parse(TEXT("Unnamed")).Value);

    // Blank picks a fresh random seed each time.
    const FVoxelSeed BlankA = FVoxelSeed::Parse(TEXT("")), BlankB = FVoxelSeed::Parse(TEXT("   "));
    TestTrue(TEXT("Blank is random"),
        BlankA.Source == EVoxelSeedSource::Random && BlankB.Source == EVoxelSeedSource::Random);
    TestNotEqual(TEXT("Two random seeds differ"), BlankA.Value, BlankB.Value);
    TestEqual(TEXT("Describe"), FVoxelSeed::Parse(TEXT("-7")).Describe(), FString(TEXT("-7 (number)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelRandomDistributionTest, "Unnamed.Voxel.Random.Distribution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelRandomDistributionTest::RunTest(const FString& Parameters)
{
    constexpr uint64 Salt = VoxelRandom::Salt("distribution");
    constexpr int32 Count = 1 << 18;
    int32 BitCounts[64] = {};
    int64 FlippedBits = 0;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const int32 X = (Index & 511) - 256, Y = (Index >> 9) - 256, Z = Index % 7 - 3;
        const uint64 Hash = VoxelRandom::Hash64(1234, Salt, X, Y, Z);
        for (int32 Bit = 0; Bit < 64; ++Bit)
            BitCounts[Bit] += int32(Hash >> Bit & 1);
        // Avalanche: the neighbouring cell should differ in about half of the bits.
        FlippedBits += CountBits(Hash ^ VoxelRandom::Hash64(1234, Salt, X + 1, Y, Z));
    }
    // 2^18 samples: one standard deviation of a bit's frequency is about 0.001.
    double WorstBit = 0;
    for (const int32 Ones : BitCounts)
        WorstBit = FMath::Max(WorstBit, FMath::Abs(double(Ones) / Count - .5));
    TestTrue(FString::Printf(TEXT("Every bit is set half the time (worst off by %.4f)"), WorstBit), WorstBit < .01);
    const double MeanFlipped = double(FlippedBits) / Count;
    TestTrue(FString::Printf(TEXT("Neighbouring cells differ in about 32 bits (%.2f)"), MeanFlipped),
        FMath::Abs(MeanFlipped - 32) < .5);

    // Uniform integers: each of 10 buckets of 100 000 draws is within about 4 standard deviations (95) of 10 000.
    int32 Buckets[10] = {}, HashBuckets[10] = {};
    FVoxelXoroshiro Stream(uint64(7));
    for (int32 Index = 0; Index < 100000; ++Index)
    {
        ++Buckets[Stream.NextInt(10)];
        ++HashBuckets[VoxelRandom::ToIndex(VoxelRandom::Hash64(7, Salt, Index, 0, 0), 10)];
    }
    for (int32 Bucket = 0; Bucket < 10; ++Bucket)
    {
        TestTrue(FString::Printf(TEXT("NextInt bucket %d: %d"), Bucket, Buckets[Bucket]),
            FMath::Abs(Buckets[Bucket] - 10000) < 400);
        TestTrue(FString::Printf(TEXT("ToIndex bucket %d: %d"), Bucket, HashBuckets[Bucket]),
            FMath::Abs(HashBuckets[Bucket] - 10000) < 400);
    }

    // Ranges.
    bool bInRange = true;
    for (int32 Index = 0; Index < 10000; ++Index)
    {
        const uint64 Bits = VoxelRandom::Hash64(3, Salt, Index, 0, 0);
        const float Unit = VoxelRandom::ToUnitFloat(Bits);
        const double UnitDouble = VoxelRandom::ToUnitDouble(Bits);
        const int32 Ranged = VoxelRandom::ToRange(Bits, -3, 3), Next = Stream.NextRange(5, 9);
        bInRange &= Unit >= 0 && Unit < 1 && UnitDouble >= 0 && UnitDouble < 1 && Ranged >= -3 && Ranged <= 3 &&
            Next >= 5 && Next <= 9 && Stream.NextInt(1) == 0;
    }
    TestTrue(TEXT("Unit values in [0, 1) and integers inside their ranges"), bInRange);
    TestEqual(TEXT("All ones maps just under 1"), VoxelRandom::ToUnitFloat(~0ull), 16777215.f / 16777216.f);
    return true;
}
#endif
