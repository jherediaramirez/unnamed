#include "Decoration/VoxelWriteLog.h"

int32 FVoxelWriteLog::GetWriteCount() const
{
    int32 Count = 0;
    for (const FVoxelWriteBucket& Bucket : Buckets)
        Count += Bucket.Writes.Num();
    return Count;
}

SIZE_T FVoxelWriteLog::GetAllocatedSize() const
{
    SIZE_T Bytes = 0;
    for (const FVoxelWriteBucket& Bucket : Buckets)
        Bytes += Bucket.GetAllocatedSize();
    return Bytes;
}
