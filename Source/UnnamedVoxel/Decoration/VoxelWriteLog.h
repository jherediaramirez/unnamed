#pragma once

#include "CoreMinimal.h"
#include "VoxelConstants.h"

// How a logged write lands when Finalize replays it against the merged target column (phase 6 plan 3.1).
enum class EVoxelWriteMode : uint8
{
    // Always (lakes, geodes).
    Force,
    // Only over a block in the tag the argument names (trees over replaceable_by_trees, plants over replaceable).
    IfReplaceable,
    // An ore's target list (the argument): the first target whose rule test matches the block there decides the
    // state; no match, no write.
    IfTargets,
    Count
};

// An optional condition on a write's support, checked at replay against the merged target column (scout C,
// recommendation 7): a vertical support lies in the same column, so the check is exact. Only the slot exists in S2;
// plants (S8) add "the block below is in tag X".
enum class EVoxelWriteSupport : uint8
{
    None,
    Count = 8
};

// One write into one target column, 8 bytes: the cell, the mode, the support, the state and the mode's argument.
struct FVoxelWrite
{
    // Bits 0-3 local X, 4-7 local Y, 8-16 Z - MinZ, 17-18 mode, 19-21 support, 22-31 the support's argument.
    uint32 Packed = 0;
    uint16 State = 0;
    // IfReplaceable: a tag index; IfTargets: a target list index (the decorator's).
    uint16 Argument = 0;

    static FVoxelWrite Make(int32 X, int32 Y, int32 Z, uint16 State, EVoxelWriteMode Mode, uint16 Argument,
        EVoxelWriteSupport Support = EVoxelWriteSupport::None, int32 SupportArgument = 0)
    {
        FVoxelWrite Write;
        Write.Packed = uint32(X) | uint32(Y) << 4 | uint32(Z - VoxelConstants::MinZ) << 8 | uint32(Mode) << 17 |
            uint32(Support) << 19 | uint32(SupportArgument) << 22;
        Write.State = State;
        Write.Argument = Argument;
        return Write;
    }

    int32 GetX() const { return Packed & 15; }
    int32 GetY() const { return Packed >> 4 & 15; }
    int32 GetZ() const { return int32(Packed >> 8 & 511) + VoxelConstants::MinZ; }
    // Index in a dense column (FVoxelColumn::GetDenseIndex).
    int32 GetDenseIndex() const { return int32(Packed & 0x1FFFF); }
    EVoxelWriteMode GetMode() const { return EVoxelWriteMode(Packed >> 17 & 3); }
    EVoxelWriteSupport GetSupport() const { return EVoxelWriteSupport(Packed >> 19 & 7); }
    int32 GetSupportArgument() const { return int32(Packed >> 22); }
};
static_assert(sizeof(FVoxelWrite) == 8, "Writes are logged by the thousand per column");

// The writes one Decorate aimed at one target column, already in replay order: runs of one placed feature each, in
// (step, order index) order, each run's writes in the order the feature made them.
struct UNNAMEDVOXEL_API FVoxelWriteBucket
{
    struct FRun
    {
        // Step << 8 | index in the step: the merge key.
        uint16 Key = 0;
        // One past the run's last write; a run begins where the one before it ends.
        int32 End = 0;
    };

    TArray<FVoxelWrite> Writes;
    TArray<FRun> Runs;

    static uint16 MakeKey(int32 Step, int32 IndexInStep) { return uint16(Step << 8 | IndexInStep); }

    void Add(uint16 Key, const FVoxelWrite& Write)
    {
        if (Runs.IsEmpty() || Runs.Last().Key != Key) Runs.Add({Key, Writes.Num()});
        Writes.Add(Write);
        Runs.Last().End = Writes.Num();
    }
    bool IsEmpty() const { return Writes.IsEmpty(); }
    SIZE_T GetAllocatedSize() const { return Writes.GetAllocatedSize() + Runs.GetAllocatedSize(); }
};

// Everything one column's Decorate produced: one bucket per target column of its 3x3 (scout C, recommendation 2), so
// each bucket is freed when its target finalizes.
struct UNNAMEDVOXEL_API FVoxelWriteLog
{
    // Bucket index of the target column at offset (DX, DY) from the source, each -1..1.
    static constexpr int32 GetBucket(int32 DX, int32 DY) { return (DY + 1) * 3 + DX + 1; }

    FIntPoint Source = FIntPoint::ZeroValue;
    FVoxelWriteBucket Buckets[9];
    // Writes that fell outside the 3x3 and were dropped (Decoration.Reach wants none).
    int32 Dropped = 0;

    int32 GetWriteCount() const;
    SIZE_T GetAllocatedSize() const;
};

namespace VoxelWriteMerge
{
    // Visits every write of the buckets aimed at one target column in replay order: by key (step, then order index),
    // then source offset, then sequence. Sources are indexed by the source's offset from the target
    // (FVoxelWriteLog::GetBucket of the source's DX, DY), which is the order sources win ties in: dz then dx in the
    // reference's axes; null for none. Each bucket is already sorted by key, so this is a 9-way merge over runs, not
    // a sort.
    template <typename FVisit> void Merge(TConstArrayView<const FVoxelWriteBucket*> Sources, FVisit&& Visit)
    {
        int32 RunIndex[9] = {};
        const int32 SourceCount = FMath::Min(Sources.Num(), 9);
        for (;;)
        {
            uint32 Key = MAX_uint32;
            for (int32 Source = 0; Source < SourceCount; ++Source)
                if (Sources[Source] && RunIndex[Source] < Sources[Source]->Runs.Num())
                    Key = FMath::Min(Key, uint32(Sources[Source]->Runs[RunIndex[Source]].Key));
            if (Key == MAX_uint32) return;
            for (int32 Source = 0; Source < SourceCount; ++Source)
            {
                const FVoxelWriteBucket* Bucket = Sources[Source];
                if (!Bucket || RunIndex[Source] >= Bucket->Runs.Num() || Bucket->Runs[RunIndex[Source]].Key != Key)
                    continue;
                const int32 Run = RunIndex[Source]++;
                const int32 Begin = Run ? Bucket->Runs[Run - 1].End : 0;
                for (int32 Write = Begin; Write < Bucket->Runs[Run].End; ++Write)
                    Visit(Bucket->Writes[Write]);
            }
        }
    }
}
