#include "Random/VoxelSeed.h"
#include "Containers/StringConv.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"
#include "Random/VoxelRandom.h"

namespace
{
    // A decimal integer with an optional sign; false for anything else and on overflow. Written out rather than
    // FCString::Atoi64, which accepts trailing text and saturates silently.
    bool TryParseInteger(FStringView Text, int64& OutValue)
    {
        int32 Index = 0;
        const bool bNegative = Text[0] == TEXT('-');
        if (Text[0] == TEXT('+') || bNegative) ++Index;
        if (Index == Text.Len()) return false;
        // The largest magnitude: 2^63 for negative numbers, 2^63 - 1 for positive ones.
        const uint64 Limit = bNegative ? uint64(MAX_int64) + 1 : uint64(MAX_int64);
        uint64 Magnitude = 0;
        for (; Index < Text.Len(); ++Index)
        {
            const TCHAR Char = Text[Index];
            if (Char < TEXT('0') || Char > TEXT('9')) return false;
            const uint64 Digit = uint64(Char - TEXT('0'));
            if (Magnitude > (Limit - Digit) / 10) return false;
            Magnitude = Magnitude * 10 + Digit;
        }
        OutValue = bNegative ? int64(0 - Magnitude) : int64(Magnitude);
        return true;
    }
}

FVoxelSeed FVoxelSeed::Parse(FStringView Text)
{
    const FStringView Trimmed = Text.TrimStartAndEnd();
    FVoxelSeed Seed;
    if (Trimmed.IsEmpty())
    {
        Seed.Value = MakeRandom();
        Seed.Source = EVoxelSeedSource::Random;
    }
    else if (TryParseInteger(Trimmed, Seed.Value))
        Seed.Source = EVoxelSeedSource::Number;
    else
    {
        Seed.Value = HashText(Trimmed);
        Seed.Source = EVoxelSeedSource::Text;
    }
    return Seed;
}

int64 FVoxelSeed::HashText(FStringView Text)
{
    const auto Utf8 = StringCast<UTF8CHAR>(Text.GetData(), Text.Len());
    const uint64 Hash = VoxelRandom::Fnv1a64(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    return int64(VoxelRandom::Mix64(Hash));
}

int64 FVoxelSeed::MakeRandom()
{
    const FGuid Guid = FGuid::NewGuid();
    uint64 State = (uint64(Guid.A) << 32 | Guid.B) ^ (uint64(Guid.C) << 32 | Guid.D) ^ FPlatformTime::Cycles64();
    return int64(VoxelRandom::SplitMix64(State));
}

FString FVoxelSeed::Describe() const
{
    const TCHAR* SourceName = Source == EVoxelSeedSource::Random ? TEXT("random")
        : Source == EVoxelSeedSource::Number                     ? TEXT("number")
                                                                 : TEXT("text");
    return FString::Printf(TEXT("%lld (%s)"), Value, SourceName);
}
