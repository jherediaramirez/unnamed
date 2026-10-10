#include "Blocks/VoxelBlockTable.h"

FVoxelBlockTable::FVoxelBlockTable()
{
    States.AddDefaulted();
    DefaultStates.Add(TEXT("air"), 0);
}

void FVoxelBlockTable::SetState(uint16 State, const FVoxelStateInfo& Info)
{
    if (State >= States.Num()) States.SetNum(State + 1);
    States[State] = Info;
    BlockLimit = FMath::Max(BlockLimit, int32(Info.Block) + 1);
}

uint16 FVoxelBlockTable::FindState(FName Block) const
{
    const uint16* State = DefaultStates.Find(Block);
    return State ? *State : 0;
}

int32 FVoxelBlockTable::FindBlock(FStringView Id) const
{
    if (Id.StartsWith(TEXT("minecraft:"))) Id.RightChopInline(10);
    if (Id == TEXT("air")) return 0;
    const uint16 State = FindState(FName(Id));
    return State ? int32(Get(State).Block) : INDEX_NONE;
}

void FVoxelBlockTable::SetDefaultState(FName Block, uint16 State)
{
    DefaultStates.Add(Block, State);
}

uint16 FVoxelBlockTable::GetSnowyState(uint16 State) const
{
    const uint16* Snowy = SnowyStates.Find(State);
    return Snowy ? *Snowy : State;
}

void FVoxelBlockTable::SetSnowyState(uint16 State, uint16 Snowy)
{
    SnowyStates.Add(State, Snowy);
}
