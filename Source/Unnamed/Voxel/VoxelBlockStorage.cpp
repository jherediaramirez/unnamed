#include "Voxel/VoxelBlockStorage.h"
#include "Voxel/VoxelColumnPipeline.h"

FVoxelBlockStorage::FVoxelBlockStorage(TFunction<FVoxelColumnPipeline*()> InGetPipeline)
    : GetPipeline(MoveTemp(InGetPipeline))
{
}

FFoundationCell FVoxelBlockStorage::Get(const FIntVector& Cell) const
{
    const FVoxelColumnPipeline* Pipeline = GetPipeline();
    uint16 State = 0;
    int64 Stamp = 0;
    if (!Pipeline || !Pipeline->GetCell(Cell, State, Stamp)) return FFoundationCell{};
    return FFoundationCell{FFoundationBlockState{State}, Stamp};
}

void FVoxelBlockStorage::Set(const FIntVector& Cell, const FFoundationCell& Value)
{
    // The block world only writes known cells, so the column is generated.
    FVoxelColumnPipeline* Pipeline = GetPipeline();
    const bool bSet = Pipeline && Pipeline->SetCell(Cell, Value.State.Id, Value.Revision);
    ensureMsgf(bSet, TEXT("Voxel cell %s written outside the generated columns"), *Cell.ToString());
}

void FVoxelBlockStorage::SetMany(TConstArrayView<TPair<FIntVector, FFoundationCell>> Cells)
{
    FVoxelColumnPipeline* Pipeline = GetPipeline();
    TMap<FIntPoint, TArray<FVoxelColumnPipeline::FCellWrite>> ByColumn;
    for (const TPair<FIntVector, FFoundationCell>& Cell : Cells)
        ByColumn.FindOrAdd(FIntPoint(Cell.Key.X >> 4, Cell.Key.Y >> 4))
            .Add({Cell.Key, Cell.Value.State.Id, Cell.Value.Revision});
    for (const TPair<FIntPoint, TArray<FVoxelColumnPipeline::FCellWrite>>& Column : ByColumn)
    {
        const bool bSet = Pipeline && Pipeline->SetCells(Column.Key, Column.Value);
        ensureMsgf(
            bSet, TEXT("Voxel column (%d, %d) written outside the generated columns"), Column.Key.X, Column.Key.Y);
    }
}

void FVoxelBlockStorage::Empty()
{
    if (FVoxelColumnPipeline* Pipeline = GetPipeline()) Pipeline->DropEdits();
}

int32 FVoxelBlockStorage::GetSolidCount() const
{
    const FVoxelColumnPipeline* Pipeline = GetPipeline();
    return Pipeline ? int32(FMath::Min<int64>(Pipeline->CountNonAir(), MAX_int32)) : 0;
}

void FVoxelBlockStorage::ForEachSolid(const FIntVector& Min, const FIntVector& Max,
    TFunctionRef<void(const FIntVector&, const FFoundationCell&)> Visit) const
{
    if (const FVoxelColumnPipeline* Pipeline = GetPipeline())
        Pipeline->ForEachNonAir(Min, Max, [&Visit](const FIntVector& Cell, uint16 State, int64 Stamp)
            { Visit(Cell, FFoundationCell{FFoundationBlockState{State}, Stamp}); });
}

SIZE_T FVoxelBlockStorage::GetAllocatedSize() const
{
    const FVoxelColumnPipeline* Pipeline = GetPipeline();
    return Pipeline ? Pipeline->GetEditBytes() : 0;
}

bool FVoxelBlockStorage::IsLoaded(const FIntVector& Cell) const
{
    const FVoxelColumnPipeline* Pipeline = GetPipeline();
    return Pipeline && Pipeline->IsGenerated(FIntPoint(Cell.X >> 4, Cell.Y >> 4));
}
