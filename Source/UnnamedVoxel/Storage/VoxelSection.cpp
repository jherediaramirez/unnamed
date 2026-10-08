#include "Storage/VoxelSection.h"

namespace
{
    // log2 of the indices per 64-bit word for a width: 4 bits -> 16 per word, 8 -> 8, 16 -> 4.
    FORCEINLINE int32 GetSlotShift(int32 Bits)
    {
        return Bits == 4 ? 4 : Bits == 8 ? 3 : 2;
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
    // Distinct states in first-seen order. Runs of one state are the norm, so remembering the last one found keeps
    // the palette search off most cells.
    TArray<uint16, TInlineAllocator<16>> Found;
    uint16 Last = States[0];
    Found.Add(Last);
    for (const uint16 State : States)
        if (State != Last)
        {
            Last = State;
            if (!Found.Contains(State)) Found.Add(State);
        }

    FVoxelSection Section;
    if (Found.Num() == 1)
    {
        Section = FVoxelSection(Found[0]);
        return Section;
    }
    Section.Encode(States, Found, GetWidthFor(Found.Num()));
    return Section;
}

void FVoxelSection::Encode(TConstArrayView<uint16> States, TConstArrayView<uint16> NewPalette, int32 NewBits)
{
    Bits = uint8(NewBits);
    Uniform = 0;
    // Exact allocations: a column holds 24 sections and the world thousands of columns.
    Palette.Empty(Bits == 16 ? 0 : NewPalette.Num());
    if (Bits != 16) Palette.Append(NewPalette.GetData(), NewPalette.Num());
    Words.Empty(Volume * Bits / 64);
    Words.AddZeroed(Volume * Bits / 64);
    NonAir = 0;
    int32 LastIndex = 0;
    for (int32 Index = 0; Index < Volume; ++Index)
    {
        const uint16 State = States[Index];
        NonAir += int32(State != 0);
        if (Bits == 16)
        {
            SetRaw(Index, State);
            continue;
        }
        if (Palette[LastIndex] != State) LastIndex = Palette.IndexOfByKey(State);
        SetRaw(Index, uint32(LastIndex));
    }
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

uint16 FVoxelSection::Get(int32 Index) const
{
    if (Bits == 0) return Uniform;
    const uint32 Raw = GetRaw(Index);
    return Bits == 16 ? uint16(Raw) : Palette[Raw];
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
    for (int32 Index = 0; Index < Volume; ++Index)
        Out[Index] = Get(Index);
}

void FVoxelSection::Compact()
{
    if (Bits == 0) return;
    TArray<uint16> Dense;
    Dense.SetNumUninitialized(Volume);
    Expand(Dense);
    *this = FromStates(Dense);
}
