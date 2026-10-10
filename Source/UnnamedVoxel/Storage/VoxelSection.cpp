#include "Storage/VoxelSection.h"

namespace
{
    // log2 of the indices per 64-bit word for a width: 4 bits -> 16 per word, 8 -> 8, 16 -> 4.
    FORCEINLINE int32 GetSlotShift(int32 Bits)
    {
        return Bits == 4 ? 4 : Bits == 8 ? 3 : 2;
    }

    // Per thread, for every state id: the generation that last stamped it (high 16 bits) and its palette index (low
    // 16 bits). A pass stamps the states it meets with a fresh generation, so nothing has to be cleared between passes.
    uint32* GetStateTable(uint32& OutGeneration)
    {
        thread_local TArray<uint32> Table;
        thread_local uint32 Generation = 0;
        if (Table.IsEmpty()) Table.SetNumZeroed(1 << 16);
        Generation = (Generation + 1) & 0xFFFF;
        if (Generation == 0)
        {
            FMemory::Memzero(Table.GetData(), Table.Num() * sizeof(uint32));
            Generation = 1;
        }
        OutGeneration = Generation << 16;
        return Table.GetData();
    }

    // Packs Volume states into Words a whole word at a time, as SetRaw would place them: index I at bit
    // (I % PerWord) * Bits of word I / PerWord. Below 16 bits each state becomes its palette index from Lookup's low
    // 16 bits. Returns the non-air count.
    template <int32 Bits> int32 EncodeWords(const uint16* States, const uint32* Lookup, uint64* Words)
    {
        constexpr int32 PerWord = 64 / Bits;
        int32 NonAir = 0;
        for (int32 Word = 0; Word < FVoxelSection::Volume / PerWord; ++Word)
        {
            uint64 Value = 0;
            for (int32 Slot = 0; Slot < PerWord; ++Slot)
            {
                const uint16 State = *States++;
                NonAir += int32(State != 0);
                const uint32 Raw = Bits == 16 ? uint32(State) : Lookup[State] & 0xFFFF;
                Value |= uint64(Raw) << (Slot * Bits);
            }
            *Words++ = Value;
        }
        return NonAir;
    }

    // The reverse: every index of Words through the palette (below 16 bits) into Out.
    template <int32 Bits> void ExpandWords(const uint64* Words, const uint16* Palette, uint16* Out)
    {
        constexpr int32 PerWord = 64 / Bits;
        constexpr uint64 Mask = (uint64(1) << Bits) - 1;
        for (int32 Word = 0; Word < FVoxelSection::Volume / PerWord; ++Word)
        {
            uint64 Value = Words[Word];
            for (int32 Slot = 0; Slot < PerWord; ++Slot, Value >>= Bits)
                *Out++ = Bits == 16 ? uint16(Value & Mask) : Palette[Value & Mask];
        }
    }
}

FVoxelSection::FVoxelSection(uint16 Fill) : Uniform(Fill), NonAir(Fill ? Volume : 0) {}

int32 FVoxelSection::GetWidthFor(int32 DistinctStates)
{
    return DistinctStates <= 1 ? 0 : DistinctStates <= 16 ? 4 : DistinctStates <= 256 ? 8 : 16;
}

FVoxelSection FVoxelSection::FromStates(TConstArrayView<uint16> States)
{
    check(States.Num() == Volume);
    // Distinct states in first-seen order, each stamped with its palette index for the encoding.
    uint32 Generation = 0;
    uint32* Table = GetStateTable(Generation);
    TArray<uint16, TInlineAllocator<16>> Found;
    for (const uint16 State : States)
        if ((Table[State] & 0xFFFF0000u) != Generation)
        {
            Table[State] = Generation | uint32(Found.Num());
            Found.Add(State);
        }

    FVoxelSection Section;
    if (Found.Num() == 1)
    {
        Section = FVoxelSection(Found[0]);
        return Section;
    }
    Section.EncodeStamped(States, Found, GetWidthFor(Found.Num()), Table);
    return Section;
}

void FVoxelSection::Encode(TConstArrayView<uint16> States, TConstArrayView<uint16> NewPalette, int32 NewBits)
{
    // Each state takes its first index in NewPalette.
    uint32 Generation = 0;
    uint32* Table = GetStateTable(Generation);
    for (int32 Index = 0; Index < NewPalette.Num(); ++Index)
        if ((Table[NewPalette[Index]] & 0xFFFF0000u) != Generation)
            Table[NewPalette[Index]] = Generation | uint32(Index);
    EncodeStamped(States, NewPalette, NewBits, Table);
}

void FVoxelSection::EncodeStamped(
    TConstArrayView<uint16> States, TConstArrayView<uint16> NewPalette, int32 NewBits, const uint32* Lookup)
{
    Bits = uint8(NewBits);
    Uniform = 0;
    // Exact allocations: a column holds 24 sections and the world thousands of columns.
    Palette.Empty(Bits == 16 ? 0 : NewPalette.Num());
    if (Bits != 16) Palette.Append(NewPalette.GetData(), NewPalette.Num());
    Words.Empty(Volume * Bits / 64);
    Words.SetNumUninitialized(Volume * Bits / 64);
    NonAir = Bits == 4 ? EncodeWords<4>(States.GetData(), Lookup, Words.GetData())
        : Bits == 8    ? EncodeWords<8>(States.GetData(), Lookup, Words.GetData())
                       : EncodeWords<16>(States.GetData(), Lookup, Words.GetData());
}

uint32 FVoxelSection::GetRaw(int32 Index) const
{
    const int32 Shift = GetSlotShift(Bits);
    const uint64 Word = Words[Index >> Shift];
    return uint32(Word >> ((Index & ((1 << Shift) - 1)) * Bits)) & ((1u << Bits) - 1);
}

void FVoxelSection::SetRaw(int32 Index, uint32 Value)
{
    const int32 Shift = GetSlotShift(Bits);
    const int32 Offset = (Index & ((1 << Shift) - 1)) * Bits;
    const uint64 Mask = ((1ull << Bits) - 1) << Offset;
    uint64& Word = Words[Index >> Shift];
    Word = (Word & ~Mask) | (uint64(Value) << Offset);
}

void FVoxelSection::Set(int32 Index, uint16 State)
{
    const uint16 Old = Get(Index);
    if (Old == State) return;
    NonAir += int32(State != 0) - int32(Old != 0);
    if (Bits == 16)
    {
        SetRaw(Index, State);
        return;
    }
    int32 PaletteIndex = Bits == 0 ? INDEX_NONE : Palette.IndexOfByKey(State);
    if (PaletteIndex == INDEX_NONE)
    {
        const int32 Capacity = Bits == 0 ? 1 : 1 << Bits;
        const int32 Used = Bits == 0 ? 1 : Palette.Num();
        if (Used >= Capacity)
        {
            // Full: re-encode one width wider, with the new state already in place.
            TArray<uint16> Dense;
            Dense.SetNumUninitialized(Volume);
            Expand(Dense);
            Dense[Index] = State;
            TArray<uint16> NewPalette;
            if (Bits == 0)
                NewPalette = {Uniform, State};
            else
            {
                NewPalette = Palette;
                NewPalette.Add(State);
            }
            Encode(Dense, NewPalette, GetWidthFor(NewPalette.Num()));
            return;
        }
        PaletteIndex = Palette.Add(State);
    }
    SetRaw(Index, uint32(PaletteIndex));
}

void FVoxelSection::Expand(TArrayView<uint16> Out) const
{
    check(Out.Num() >= Volume);
    if (Bits == 0)
    {
        for (int32 Index = 0; Index < Volume; ++Index)
            Out[Index] = Uniform;
        return;
    }
    if (Bits == 4)
        ExpandWords<4>(Words.GetData(), Palette.GetData(), Out.GetData());
    else if (Bits == 8)
        ExpandWords<8>(Words.GetData(), Palette.GetData(), Out.GetData());
    else
        ExpandWords<16>(Words.GetData(), Palette.GetData(), Out.GetData());
}

void FVoxelSection::Compact()
{
    if (Bits == 0) return;
    TArray<uint16> Dense;
    Dense.SetNumUninitialized(Volume);
    Expand(Dense);
    *this = FromStates(Dense);
}
