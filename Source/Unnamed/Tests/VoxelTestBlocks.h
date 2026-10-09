#pragma once

#include "CoreMinimal.h"
#include "Blocks/VoxelBlockTable.h"
#include "Storage/VoxelColumn.h"

// A small hand-built block table for voxel tests: every face code, layer and shape is known, and nothing depends on
// the imported registry. Face codes use a different texture per state and face, so a test can tell faces apart.
namespace VoxelTestBlocks
{
    enum : uint16
    {
        Air = 0,
        Stone,
        Dirt,
        Glass,
        Leaves,
        SlabBottom,
        SlabTop,
        WaterSource,
        // Level 3: amount 5, so its surface is at 5/9.
        WaterFlowing,
        // Three snow layers: a box 3/8 high; only its bottom covers its square.
        Snow3,
        Count
    };

    inline FVoxelBlockTable Make()
    {
        FVoxelBlockTable Table;
        auto Add = [&Table](uint16 State, FName Name, EVoxelShape Shape, EVoxelLayer Layer, uint16 Block)
        {
            FVoxelStateInfo Info;
            Info.Shape = Shape;
            Info.Layer = Layer;
            Info.Block = Block;
            Info.bSolid = Shape != EVoxelShape::Fluid;
            Info.bCastsOcclusion = Shape == EVoxelShape::Full && Layer != EVoxelLayer::Translucent;
            Info.bLeaves = Name == TEXT("leaves");
            if (Layer == EVoxelLayer::Opaque)
                Info.OpaqueFaces = Shape == EVoxelShape::Full ? 0x3F
                    : Shape == EVoxelShape::SlabBottom        ? uint8(1 << int32(EVoxelFace::Down))
                    : Shape == EVoxelShape::SlabTop           ? uint8(1 << int32(EVoxelFace::Up))
                    : Shape == EVoxelShape::Layers            ? uint8(1 << int32(EVoxelFace::Down))
                                                              : 0;
            for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
                Info.Faces[Face] = uint32(State * 8 + Face) | (Shape == EVoxelShape::Fluid ? 0 : VoxelFaces::CullBit);
            // A slab's inner face (the bottom slab's top, the top slab's bottom) has no cull face, as in the reference.
            if (Shape == EVoxelShape::SlabBottom) Info.Faces[int32(EVoxelFace::Up)] &= ~VoxelFaces::CullBit;
            if (Shape == EVoxelShape::SlabTop) Info.Faces[int32(EVoxelFace::Down)] &= ~VoxelFaces::CullBit;
            // A snow layer's top has none either: it sits below the cell's top, so nothing above hides it.
            if (Shape == EVoxelShape::Layers) Info.Faces[int32(EVoxelFace::Up)] &= ~VoxelFaces::CullBit;
            Table.SetState(State, Info);
            Table.SetDefaultState(Name, State);
            return Info;
        };
        Add(Stone, TEXT("stone"), EVoxelShape::Full, EVoxelLayer::Opaque, 1);
        Add(Dirt, TEXT("dirt"), EVoxelShape::Full, EVoxelLayer::Opaque, 2);
        Add(Glass, TEXT("glass"), EVoxelShape::Full, EVoxelLayer::Translucent, 3);
        Add(Leaves, TEXT("leaves"), EVoxelShape::Full, EVoxelLayer::Cutout, 4);
        Add(SlabBottom, TEXT("slab"), EVoxelShape::SlabBottom, EVoxelLayer::Opaque, 5);
        Add(SlabTop, TEXT("slab_top"), EVoxelShape::SlabTop, EVoxelLayer::Opaque, 5);
        FVoxelStateInfo Water = Add(WaterSource, TEXT("water"), EVoxelShape::Fluid, EVoxelLayer::Water, 6);
        Water.FluidAmount = 8;
        Table.SetState(WaterSource, Water);
        Water.FluidAmount = 5;
        Table.SetState(WaterFlowing, Water);
        FVoxelStateInfo Snow = Add(Snow3, TEXT("snow"), EVoxelShape::Layers, EVoxelLayer::Opaque, 7);
        Snow.Layers = 3;
        Table.SetState(Snow3, Snow);
        return Table;
    }

    // A column built from a dense buffer filled by Fill(X, Y, Z) -> state (local X and Y, world Z).
    template <typename FillType>
    TSharedRef<FVoxelColumn> MakeColumn(const FVoxelBlockTable& Table, FIntPoint Position, FillType&& Fill)
    {
        TArray<uint16> Dense;
        Dense.SetNumZeroed(FVoxelColumn::DenseVolume);
        for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
            for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
                for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                    Dense[FVoxelColumn::GetDenseIndex(X, Y, Z)] = Fill(X, Y, Z);
        TSharedRef<FVoxelColumn> Column = MakeShared<FVoxelColumn>(Position);
        Column->Build(Dense, Table);
        return Column;
    }
}
