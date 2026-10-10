#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlocks.h"

class FVoxelColumnPipeline;

// The Foundation block world's storage over the streamed voxel columns (phase 3), so targeting, mining, placing and
// the HUD work on generated terrain through the same authority as the Playground. Cells read from the generated
// columns; a write is an edit (FVoxelColumnPipeline::SetCell), which keeps the block world's revision as its stamp.
// A cell is loaded when its column is generated; the block world knows no cell beyond that, so nothing is placed
// in a column that has not arrived. The pipeline is fetched on every call, so a restarted stream (voxel.Seed) needs
// no new storage; tests hand in their own.
class FVoxelBlockStorage final : public IFoundationBlockStorage
{
public:
    // GetPipeline returns the current pipeline, or null while there is none.
    explicit FVoxelBlockStorage(TFunction<FVoxelColumnPipeline*()> InGetPipeline);

    virtual FFoundationCell Get(const FIntVector& Cell) const override;
    virtual void Set(const FIntVector& Cell, const FFoundationCell& Value) override;
    // Forgets the edits; the terrain comes back as generated.
    virtual void Empty() override;
    // Non-air cells in the generated columns.
    virtual int32 GetSolidCount() const override;
    virtual void ForEachSolid(const FIntVector& Min, const FIntVector& Max,
        TFunctionRef<void(const FIntVector&, const FFoundationCell&)> Visit) const override;
    // The edits; the columns themselves are counted by the streaming stats.
    virtual SIZE_T GetAllocatedSize() const override;
    virtual bool IsLoaded(const FIntVector& Cell) const override;
    // One FVoxelColumnPipeline::SetCells per column, each column's cells in the order given.
    virtual void SetMany(TConstArrayView<TPair<FIntVector, FFoundationCell>> Cells) override;

private:
    TFunction<FVoxelColumnPipeline*()> GetPipeline;
};
