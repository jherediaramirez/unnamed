#pragma once

#include "CoreMinimal.h"

class FVoxelBlockTable;

// Ore looks, render-only (scout E's look calls L1 and L2 level 1; Docs/VoxelWorld.md, "Ore looks"). The 16 stone and
// deepslate ores can draw from T_OreLooks instead of their vanilla texture: on the rock around them
// (voxel.Look.OreHost: 1 composite, 2 palette swap) and with ore-aware mips and gloss (voxel.Look.OreGlint 1). Blocks,
// columns and saves never change; with both switches at 0 the mesh is the vanilla one, vertex for vertex.
namespace VoxelOreLooks
{
    // FVoxelStateInfo::Ore: 0 for none, else 1 + Ore * 2 + Base, with ores in OreNames order and Base 0 for stone, 1
    // for deepslate. FVoxelStateInfo::Rock: 0 for none, 1 for the ores' bases (stone, deepslate), 2 to 5 for the hosts
    // granite, diorite, andesite and tuff.
    constexpr int32 OreCount = 8;
    // Hosts: 0 the ore's own base (the vanilla texture), 1 granite, 2 diorite, 3 andesite, 4 tuff.
    constexpr int32 HostCount = 5;

    // T_OreLooks' layers, in Scripts/OreLooks.py's slot order: (Mode * HostCount + Host) * 16 + Ore * 2 + Base. The
    // first two modes keep 26.3's mips; the ore-mip modes keep the ore pixels' share and colour at every level and
    // carry the gloss class in alpha.
    enum class EMode : int32
    {
        Composite,
        Swap,
        CompositeOreMips,
        SwapOreMips,
        Count
    };
    constexpr int32 SlotCount = int32(EMode::Count) * HostCount * OreCount * 2;
    constexpr int32 GetSlot(EMode Mode, int32 Host, int32 OreBase)
    {
        return (int32(Mode) * HostCount + Host) * OreCount * 2 + OreBase;
    }

    // An ore face's look word takes the place of its overlay code (ore states have none): LookBit without the
    // overlay's present bit (15), the slot in bits 0 to 9 and the material's level from LevelShift (0 the slot's
    // texture, 1 with gloss, DebugMask flat magenta). Words stay below 2^15: the GPU interpolates the channel as a
    // float, and near 2^23 that is off by one, which picked a neighbouring slot.
    constexpr uint32 LookBit = 1u << 14;
    constexpr int32 LevelShift = 10;
    // voxel.Look.OreGlint's debug value: every ore face flat magenta, the mask for the look calls' changed pixels.
    constexpr int32 DebugMask = 3;

    // "coal", "iron", "copper", "gold", "redstone", "emerald", "lapis", "diamond".
    extern UNNAMEDVOXEL_API const TCHAR* const OreNames[OreCount];

    // Sets FVoxelStateInfo::Ore and Rock in Table from the 26.3 block ids; every other state keeps 0. Only full opaque
    // cubes without an overlay take an ore class.
    UNNAMEDVOXEL_API void Classify(FVoxelBlockTable& Table);

    // The host of the ore at index Index of an FVoxelPaddedSection's States: the rock class most common among its 6
    // face neighbours or, when none of them is a rock, among its 20 edge and corner neighbours. A tie, no rock, or
    // the base winning gives 0 (vanilla).
    UNNAMEDVOXEL_API int32 PickHost(const FVoxelBlockTable& Table, const uint16* States, int32 Index);

    // The overlay channel's word for a face of ore class Ore on host Host under voxel.Look.OreHost and OreGlint, or 0
    // to draw the face vanilla: always with both switches at 0, and with only OreHost on where the host is the base.
    UNNAMEDVOXEL_API uint32 GetLookWord(uint8 Ore, int32 Host, int32 OreHost, int32 OreGlint);
}
