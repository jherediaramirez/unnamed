#include "Meshing/VoxelFluidSurface.h"

uint8 VoxelFluidSurface::Pack(const FVoxelStateInfo& Fluid, bool bFluidAbove)
{
    if (Fluid.Fluid == EVoxelFluid::None || !Fluid.FluidAmount) return 0;
    return uint8(FMath::Min<int32>(Fluid.FluidAmount, 8)) | (Fluid.FluidLevel >= 8 ? FallingBit : 0) |
        uint8(uint8(Fluid.Fluid) << FluidShift) | (bFluidAbove ? FullBit : 0);
}

bool VoxelFluidSurface::Make(const uint8 Ring[9], int32 Mode, FSurface& Out, bool bOpenCorners)
{
    const uint8 Self = Ring[4];
    // Only a fluid cell has a surface (never an open cell, which a batch of edits can briefly put at the centre).
    if (Mode <= 0 || !(Self & FluidMask) || (Self & FullBit)) return false;
    const float Own = (Self & AmountMask) / 9.f;
    constexpr int32 CornerX[4] = {-1, 1, 1, -1}, CornerY[4] = {-1, -1, 1, 1};
    bool bTilted = false;
    for (int32 Corner = 0; Corner < 4; ++Corner)
    {
        const int32 DX = CornerX[Corner], DY = CornerY[Corner] * 3;
        const uint8 Cells[4] = {Self, Ring[4 + DX], Ring[4 + DY], Ring[4 + DX + DY]};
        bool bDiffer = false, bFull = false, bSource = false;
        int32 Sum = 0, Count = 0, Open = 0;
        for (const uint8 Cell : Cells)
        {
            Open += bOpenCorners && Cell == OpenCell;
            if (!Cell || (Cell & FluidMask) != (Self & FluidMask)) continue;
            bDiffer |= (Cell & LevelMask) != (Self & LevelMask);
            bFull |= (Cell & FullBit) != 0;
            // A source: amount 8, not falling.
            bSource |= (Cell & LevelMask) == 8;
            Sum += Cell & AmountMask;
            ++Count;
        }
        // Open cells lower only a mean of flowing cells (never a full or source corner, so generated fluid holds).
        const bool bOpenLowers = Open && !bFull && !bSource;
        float Height = Own;
        if (Mode >= 2 || bDiffer || bOpenLowers)
            Height = bFull ? 1.f : bSource ? 8 / 9.f : Sum / (9.f * (Count + Open));
        Out.Corners[Corner] = Height;
        bTilted |= Height != Own;
    }
    if (!bTilted) return false;
    // The rise across the cell along X and along Y, each the mean of its two edges.
    const float RiseX = (Out.Corners[1] + Out.Corners[2] - Out.Corners[0] - Out.Corners[3]) / 2;
    const float RiseY = (Out.Corners[2] + Out.Corners[3] - Out.Corners[0] - Out.Corners[1]) / 2;
    Out.Flow = FVector2f(-RiseX, -RiseY);
    return true;
}

void VoxelFluidSurface::GetFlowUVs(const FVector2f& Flow, FVector2f Out[4])
{
    // v runs downhill (Down) and u across it (Across, a quarter turn clockwise seen from above, so the texture turns
    // without mirroring: downhill along +Y gives the still texture's own axes). A block spans half the texture.
    const FVector2f Down = Flow.GetSafeNormal();
    const FVector2f Across(Down.Y, -Down.X);
    constexpr float CornerX[4] = {-.5f, .5f, .5f, -.5f}, CornerY[4] = {-.5f, -.5f, .5f, .5f};
    for (int32 Corner = 0; Corner < 4; ++Corner)
    {
        const FVector2f Offset(CornerX[Corner], CornerY[Corner]);
        Out[Corner] = FVector2f(.5f + .5f * (Offset | Across), .5f + .5f * (Offset | Down)) * 2;
    }
}
