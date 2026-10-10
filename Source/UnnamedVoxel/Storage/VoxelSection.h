#pragma once

#include "CoreMinimal.h"

// 16 x 16 x 16 block states, palette-compressed as the reference stores them: a list of the states the section uses,
// plus packed indices into it. A section of one state (all air, all stone) is that state alone, with no index array.
// Indices are 4 bits for up to 16 states and 8 for up to 256; beyond that the section stores 16-bit state ids directly.
// Widths divide 64, so an index never straddles two words.
class UNNAMEDVOXEL_API FVoxelSection
{
public:
    static constexpr int32 Size = 16;
    static constexpr int32 Volume = Size * Size * Size;

    // Cells run X fastest, then Y, then Z.
    static constexpr int32 GetIndex(int32 X, int32 Y, int32 Z) { return (Z * Size + Y) * Size + X; }

    // All air.
    FVoxelSection() = default;
    explicit FVoxelSection(uint16 Fill);
    // Compresses Volume dense states into the narrowest form.
    static FVoxelSection FromStates(TConstArrayView<uint16> States);

    uint16 Get(int32 Index) const
    {
        if (Bits == 0) return Uniform;
        // 16 indices per word at 4 bits, 8 at 8, 4 at 16.
        const int32 Shift = Bits == 4 ? 4 : Bits == 8 ? 3 : 2;
        const uint32 Raw =
            uint32(Words.GetData()[Index >> Shift] >> ((Index & ((1 << Shift) - 1)) * Bits)) & ((1u << Bits) - 1);
        return Bits == 16 ? uint16(Raw) : Palette.GetData()[Raw];
    }
    uint16 Get(int32 X, int32 Y, int32 Z) const { return Get(GetIndex(X, Y, Z)); }
    // Widens the indices when the palette fills up.
    void Set(int32 Index, uint16 State);
    // Writes all Volume states, in index order.
    void Expand(TArrayView<uint16> Out) const;

    bool IsUniform() const { return Bits == 0; }
    // The one state of a uniform section.
    uint16 GetUniformState() const { return Uniform; }
    bool IsEmpty() const { return Bits == 0 && Uniform == 0; }
    int32 GetNonAirCount() const { return NonAir; }
    int32 GetPaletteSize() const { return Bits == 0 ? 1 : Bits == 16 ? 0 : Palette.Num(); }
    int32 GetBitsPerIndex() const { return Bits; }
    SIZE_T GetAllocatedSize() const { return Palette.GetAllocatedSize() + Words.GetAllocatedSize(); }

    // Drops palette entries no cell uses and narrows the indices. Generated sections are already compact; edits
    // (phase 10) can leave unused entries behind.
    void Compact();

private:
    static int32 GetWidthFor(int32 DistinctStates);
    void Encode(TConstArrayView<uint16> States, TConstArrayView<uint16> NewPalette, int32 NewBits);
    // Encode with every state's palette index already in Lookup's low 16 bits (FromStates' stamp table).
    void EncodeStamped(
        TConstArrayView<uint16> States, TConstArrayView<uint16> NewPalette, int32 NewBits, const uint32* Lookup);
    uint32 GetRaw(int32 Index) const;
    void SetRaw(int32 Index, uint32 Value);

    // Bits 0: every cell is Uniform. 4 or 8: Words hold indices into Palette. 16: Words hold state ids.
    TArray<uint16> Palette;
    TArray<uint64> Words;
    uint16 Uniform = 0;
    uint8 Bits = 0;
    int32 NonAir = 0;
};
