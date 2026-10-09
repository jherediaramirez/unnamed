#pragma once

#include "CoreMinimal.h"

// The six faces of a cell in the block world's order (EFoundationFace). The voxel core cannot see the game module, so
// it repeats the order; VoxelBlocks.cpp checks the two agree.
enum class EVoxelFace : uint8
{
    West, // -X
    East, // +X
    North, // -Y
    South, // +Y
    Down, // -Z
    Up // +Z
};

namespace VoxelFaces
{
    constexpr int32 Count = 6;
    // The axis a face looks along (0 X, 1 Y, 2 Z) and whether it looks toward +.
    constexpr int32 GetAxis(int32 Face)
    {
        return Face >> 1;
    }

    constexpr bool IsPositive(int32 Face)
    {
        return (Face & 1) != 0;
    }

    constexpr int32 GetOpposite(int32 Face)
    {
        return Face ^ 1;
    }

    // From the block registry's face codes (FoundationBlockFaces): the face is hidden by a full neighbour.
    constexpr uint32 CullBit = 1u << 14;
    // Index of the overlay code after the six face codes.
    constexpr int32 OverlayIndex = 6;
}

// How a state fills its cell; mirrors the registry's EFoundationBlockShape.
enum class EVoxelShape : uint8
{
    Air,
    Full,
    SlabBottom,
    SlabTop,
    Fluid,
    // Snow layers: a box Layers/8 high from the cell's floor.
    Layers
};

// The mesh pass, and material slot, a state's faces draw in.
enum class EVoxelLayer : uint8
{
    Opaque,
    Cutout,
    Translucent,
    Water,
    Count
};

// What generation and meshing need to know about one block state.
struct FVoxelStateInfo
{
    EVoxelShape Shape = EVoxelShape::Air;
    EVoxelLayer Layer = EVoxelLayer::Opaque;
    // One bit per EVoxelFace: faces that cover their whole square with an opaque surface. A neighbour's face against
    // one of them is hidden.
    uint8 OpaqueFaces = 0;
    // Has a collision box: blocks movement and counts for the motion-blocking heightmaps.
    bool bSolid = false;
    // Darkens neighbouring vertices (ambient occlusion): full cubes that are not see-through.
    bool bCastsOcclusion = false;
    bool bLeaves = false;
    uint8 LightEmission = 0;
    // Fluids: the amount 1-8 (a source holds 8); 0 for anything else.
    uint8 FluidAmount = 0;
    // Layers: how many eighths of the cell the box fills, 1-7.
    uint8 Layers = 0;
    // The block this state belongs to. Faces between two states of one see-through block are hidden (glass, ice).
    uint16 Block = 0;
    // Packed sRGB 0xRRGGBB, for faces whose code carries the tint bit.
    uint32 Tint = 0;
    // Six face codes in EVoxelFace order, then the overlay code (FoundationBlockFaces).
    uint32 Faces[7] = {};

    bool IsAir() const { return Shape == EVoxelShape::Air; }
    bool IsFluid() const { return Shape == EVoxelShape::Fluid; }
    // The height of a fluid's surface within its cell when no fluid is above (a source fills 8/9).
    float GetFluidHeight() const { return FluidAmount / 9.f; }
};

// Every block state the world can hold, indexed by state id; state 0 is air. Built once on the game thread from the
// block registry (VoxelBlocks::MakeTable), then shared read-only by every worker.
class UNNAMEDVOXEL_API FVoxelBlockTable
{
public:
    FVoxelBlockTable();

    // Unknown ids read as air.
    const FVoxelStateInfo& Get(uint16 State) const { return State < States.Num() ? States[State] : States[0]; }
    int32 GetStateCount() const { return States.Num(); }
    void SetState(uint16 State, const FVoxelStateInfo& Info);

    // A block's default state by id ("stone"); 0 (air) when the block is unknown.
    uint16 FindState(FName Block) const;
    void SetDefaultState(FName Block, uint16 State);
    // A state's snowy=true variant (grass block, podzol, mycelium under snow); State itself when it has none.
    uint16 GetSnowyState(uint16 State) const;
    void SetSnowyState(uint16 State, uint16 Snowy);

private:
    TArray<FVoxelStateInfo> States;
    TMap<FName, uint16> DefaultStates;
    TMap<uint16, uint16> SnowyStates;
};
