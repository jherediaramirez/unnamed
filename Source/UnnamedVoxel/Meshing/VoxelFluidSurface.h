#pragma once

#include "CoreMinimal.h"
#include "Blocks/VoxelBlockTable.h"

// The drawn surface of a fluid cell (water and sky W3), shared by the voxel mesher and the Playground's fluid meshes so
// the two draw alike. Heights follow the wiki ("Water", Height): amount / 9, or the whole cell under more fluid. A
// surface's four corners come from the four cells meeting there (the wiki: the fill "depends on the neighboring fluid
// blocks above each upper corner of the block"). The rule is ours, to be tuned against vanilla captures:
// - a corner where one of the cells is drawn full (fluid above it) is full;
// - else, where one is a source, it is at a source's height, so a source keeps its plateau;
// - else it is the mean of the flowing cells' heights.
// Cells holding another fluid or none don't count. Neighbours share their corners, so tops meet without gaps and the
// sides between two cells of one fluid stay hidden; a side facing out follows its two top corners. Modes
// (voxel.Fluids.SlopedTops): 0 draws every surface flat at its own height (stepped boxes); 1 applies the rule only at
// corners whose cells' levels differ, so equal levels (every generated fluid) stay flat; 2 applies it at every corner.
namespace VoxelFluidSurface
{
    // A cell as the surface reads it, in one byte: 0 holds no fluid; else the amount (1-8) in bits 0-3, bit 4 when it
    // falls (levels 8-15), the fluid (EVoxelFluid) in bits 5-6, and bit 7 when fluid is directly above it.
    constexpr uint8 AmountMask = 15;
    constexpr uint8 FallingBit = 1 << 4;
    constexpr int32 FluidShift = 5;
    constexpr uint8 FluidMask = 3 << FluidShift;
    constexpr uint8 FullBit = 1 << 7;
    // The bits two cells' levels compare by.
    constexpr uint8 LevelMask = AmountMask | FallingBit;
    // Fluid is the fluid state itself (a model's held fluid, not the model).
    UNNAMEDVOXEL_API uint8 Pack(const FVoxelStateInfo& Fluid, bool bFluidAbove);

    // A surface whose Flow is within this along both axes doesn't run anywhere (a saddle): its top keeps the still
    // texture.
    constexpr float FlatFlow = 1e-4f;

    struct FSurface
    {
        // Heights within the cell at its corners (-X -Y), (+X -Y), (+X +Y), (-X +Y).
        float Corners[4] = {};
        // Downhill across the surface, in blocks of drop per block along X and Y; zero where it doesn't tilt.
        FVector2f Flow = FVector2f::ZeroVector;
    };

    // The surface of the cell at the centre of Ring, the 3 x 3 packed cells around it at its height, indexed
    // [(DY + 1) * 3 + DX + 1]. False when the cell draws as in mode 0: flat at its own height, or no top at all (fluid
    // above it).
    UNNAMEDVOXEL_API bool Make(const uint8 Ring[9], int32 Mode, FSurface& Out);

    // Which corner (FSurface::Corners) a point of the cell's top lies on, from where it sits in the cell.
    constexpr int32 GetCorner(bool bHighX, bool bHighY)
    {
        return bHighY ? (bHighX ? 2 : 3) : (bHighX ? 1 : 0);
    }

    // The flow texture's UVs at the four corners of a top whose surface runs downhill along Flow (nonzero): the texture
    // turned so its v runs downhill, as it runs down a side, centred on the cell. In the 32 x 32 flow texture's units
    // before the block materials halve them (one block spans half of it), so the corners stay within the texture.
    UNNAMEDVOXEL_API void GetFlowUVs(const FVector2f& Flow, FVector2f Out[4]);
}
