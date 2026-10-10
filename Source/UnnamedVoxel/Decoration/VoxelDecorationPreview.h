#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

class FVoxelColumn;
class FVoxelDecorator;
class IVoxelTerrain;

// voxel.Decoration.Preview (phase 6 S2, captures only): streaming builds each column through the oracle,
// FVoxelDecorator::DecorateAlone, with the generated columns it reads kept in a small cache. A column then equals the
// decorated area's, at about the cost of 9 Decorates on top of Generate. S3's streaming stages replace it.
class UNNAMEDVOXEL_API FVoxelDecorationPreview
{
public:
    FVoxelDecorationPreview(TSharedRef<const IVoxelTerrain> InTerrain, TSharedRef<const FVoxelDecorator> InDecorator,
        int32 InCacheColumns = 2048);

    // Any thread.
    void Build(FIntPoint Column, FVoxelColumn& Out) const;

private:
    TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe> GetSnapshot(FIntPoint Column) const;

    TSharedRef<const IVoxelTerrain> Terrain;
    TSharedRef<const FVoxelDecorator> Decorator;
    int32 CacheColumns;
    // Generated columns, oldest first out; Generate is pure, so a column built twice by two workers is the same.
    mutable FCriticalSection Lock;
    mutable TMap<FIntPoint, TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>> Cache;
    mutable TArray<FIntPoint> Order;
};
