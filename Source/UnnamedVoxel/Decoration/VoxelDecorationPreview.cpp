#include "Decoration/VoxelDecorationPreview.h"
#include "Decoration/VoxelDecorator.h"
#include "Misc/ScopeLock.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelTerrain.h"

FVoxelDecorationPreview::FVoxelDecorationPreview(
    TSharedRef<const IVoxelTerrain> InTerrain, TSharedRef<const FVoxelDecorator> InDecorator, int32 InCacheColumns)
    : Terrain(MoveTemp(InTerrain)), Decorator(MoveTemp(InDecorator)), CacheColumns(FMath::Max(25, InCacheColumns))
{
}

TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe> FVoxelDecorationPreview::GetSnapshot(FIntPoint Column) const
{
    {
        FScopeLock Guard(&Lock);
        if (const TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>* Found = Cache.Find(Column)) return *Found;
    }
    thread_local TArray<uint16> Dense;
    TSharedPtr<FVoxelColumn, ESPMode::ThreadSafe> Built = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>(Column);
    Terrain->Generate(Column, *Built, Dense);
    FScopeLock Guard(&Lock);
    if (!Cache.Contains(Column))
    {
        Cache.Add(Column, Built);
        Order.Add(Column);
        if (Order.Num() > CacheColumns)
        {
            Cache.Remove(Order[0]);
            Order.RemoveAt(0, EAllowShrinking::No);
        }
    }
    return Built;
}

void FVoxelDecorationPreview::Build(FIntPoint Column, FVoxelColumn& Out) const
{
    Decorator->DecorateAlone(Column, [this](FIntPoint At) { return GetSnapshot(At); }, Out);
}
