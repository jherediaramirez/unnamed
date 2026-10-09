#include "Terrain/VoxelFlatTerrain.h"
#include "Blocks/VoxelBlockTable.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Storage/VoxelColumn.h"
#include "VoxelConstants.h"
#include "VoxelStats.h"

FVoxelFlatTerrain::FVoxelFlatTerrain(TConstArrayView<FVoxelFlatLayer> Layers, const FVoxelBlockTable& InTable)
    : Table(InTable), Surface(VoxelConstants::MinZ - 1), bDry(true)
{
    for (const FVoxelFlatLayer& Layer : Layers)
        for (int32 Block = 0; Block < Layer.Count && Stack.Num() < VoxelConstants::Height; ++Block)
            Stack.Add(Layer.State);
    while (!Stack.IsEmpty() && Table.Get(Stack.Last()).IsAir())
        Stack.Pop();
    for (int32 Index = Stack.Num() - 1; Index >= 0; --Index)
    {
        const FVoxelStateInfo& Info = Table.Get(Stack[Index]);
        if (Info.bSolid)
        {
            Surface = VoxelConstants::MinZ + Index;
            break;
        }
        bDry &= !Info.IsFluid();
    }
}

int32 FVoxelFlatTerrain::GetSurfaceHeight(int32, int32) const
{
    return Surface;
}

bool FVoxelFlatTerrain::IsDry(int32, int32) const
{
    return bDry;
}

void FVoxelFlatTerrain::Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelFlatTerrain_Generate);
    SCOPE_CYCLE_COUNTER(STAT_VoxelGenerateColumn);
    // Init, not SetNumZeroed: callers reuse Dense, and SetNumZeroed clears only elements it adds.
    Dense.Init(0, FVoxelColumn::DenseVolume);
    constexpr int32 Size = VoxelConstants::ColumnSize;
    for (int32 Index = 0; Index < Stack.Num(); ++Index)
        for (int32 LocalY = 0; LocalY < Size; ++LocalY)
            for (int32 LocalX = 0; LocalX < Size; ++LocalX)
                Dense[FVoxelColumn::GetDenseIndex(LocalX, LocalY, VoxelConstants::MinZ + Index)] = Stack[Index];
    Out = FVoxelColumn(Position);
    Out.Build(Dense, Table);
}
