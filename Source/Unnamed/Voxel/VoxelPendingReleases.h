#pragma once

#include "CoreMinimal.h"

// Unloaded columns whose chunks wait to be released (far LOD step 1b). A border crossing at drawn radius 24 unloads
// about 61 columns, and emptying all their meshes in that frame took about 0.5 ms of its budget. Their chunks are
// already hidden (beyond the drawn radius), so they stay in the column map and are released later, oldest first, in
// the time later frames leave. A column that gets a mesh again first stops waiting: its chunk is live and reused, and
// never released over its new mesh. A column added while it waits keeps its place, so it is released once. One added
// again after it stopped waiting may pop at its earlier entry (still queued), and its later entry is skipped: still
// once.
template <typename T> class TVoxelPendingReleases
{
public:
    void Add(T Item)
    {
        bool bAlreadyWaiting = false;
        Waiting.Add(Item, &bAlreadyWaiting);
        if (bAlreadyWaiting) return;
        Queue.Add(Item);
        Peak = FMath::Max(Peak, Waiting.Num());
    }
    // It no longer waits.
    void Remove(T Item) { Waiting.Remove(Item); }
    bool IsWaiting(T Item) const { return Waiting.Contains(Item); }
    // Takes the oldest still waiting; false when none waits.
    bool Pop(T& Out)
    {
        bool bFound = false;
        while (!bFound && Head < Queue.Num())
        {
            const T Item = Queue[Head++];
            if (Waiting.Remove(Item))
            {
                Out = Item;
                bFound = true;
            }
        }
        // The queue keeps the entries of items removed while they waited; they are skipped, and dropped here.
        if (Head == Queue.Num() || (Head > 64 && Head * 2 > Queue.Num()))
        {
            Queue.RemoveAt(0, Head, EAllowShrinking::No);
            Head = 0;
        }
        return bFound;
    }
    void Reset()
    {
        Queue.Reset();
        Waiting.Reset();
        Head = 0;
    }
    int32 Num() const { return Waiting.Num(); }
    // The most that waited at once since the start.
    int32 GetPeak() const { return Peak; }

private:
    TArray<T> Queue;
    int32 Head = 0;
    TSet<T> Waiting;
    int32 Peak = 0;
};
