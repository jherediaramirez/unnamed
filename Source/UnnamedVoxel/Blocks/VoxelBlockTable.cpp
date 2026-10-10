#include "Blocks/VoxelBlockTable.h"
#include "Random/VoxelRandom.h"

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

void FVoxelBlockTable::SetBlockProperties(int32 Block, uint16 FirstState, TArray<FVoxelBlockProperty> Properties)
{
    FBlockProperties& Entry = BlockProperties.FindOrAdd(Block);
    Entry.FirstState = FirstState;
    Entry.Properties = MoveTemp(Properties);
    Entry.Strides.SetNum(Entry.Properties.Num());
    int32 Stride = 1;
    for (int32 Index = Entry.Properties.Num() - 1; Index >= 0; --Index)
    {
        Entry.Strides[Index] = Stride;
        Stride *= FMath::Max(1, Entry.Properties[Index].Values.Num());
    }
}

int32 FVoxelBlockTable::WithValue(uint16 State, FName Property, FName Value) const
{
    const FBlockProperties* Entry = BlockProperties.Find(Get(State).Block);
    if (!Entry || State < Entry->FirstState) return INDEX_NONE;
    for (int32 Index = 0; Index < Entry->Properties.Num(); ++Index)
    {
        const FVoxelBlockProperty& P = Entry->Properties[Index];
        if (P.Name != Property) continue;
        const int32 Wanted = P.Values.IndexOfByKey(Value);
        if (Wanted == INDEX_NONE) return INDEX_NONE;
        const int32 Current = (State - Entry->FirstState) / Entry->Strides[Index] % P.Values.Num();
        return State + (Wanted - Current) * Entry->Strides[Index];
    }
    return INDEX_NONE;
}

FName FVoxelBlockTable::GetValue(uint16 State, FName Property) const
{
    const FBlockProperties* Entry = BlockProperties.Find(Get(State).Block);
    if (!Entry || State < Entry->FirstState) return NAME_None;
    for (int32 Index = 0; Index < Entry->Properties.Num(); ++Index)
        if (Entry->Properties[Index].Name == Property && !Entry->Properties[Index].Values.IsEmpty())
            return Entry->Properties[Index]
                .Values[(State - Entry->FirstState) / Entry->Strides[Index] % Entry->Properties[Index].Values.Num()];
    return NAME_None;
}

int32 FVoxelBlockTable::MakeState(FStringView Block, TConstArrayView<TPair<FName, FName>> Properties) const
{
    if (Block.StartsWith(TEXT("minecraft:"))) Block.RightChopInline(10);
    int32 State = FindState(FName(Block));
    if (State == 0 && Block != TEXT("air")) return INDEX_NONE;
    for (const TPair<FName, FName>& Pair : Properties)
    {
        State = WithValue(uint16(State), Pair.Key, Pair.Value);
        if (State == INDEX_NONE) return INDEX_NONE;
    }
    return State;
}

int32 FVoxelBlockTable::AddModel(TConstArrayView<FVoxelModelQuad> Quads, bool bAmbientOcclusion)
{
    const int32 First = ModelQuads.Num();
    for (FVoxelModelQuad Quad : Quads)
    {
        // Flat: all four corners on one plane across the facing axis; then within 1/16 of the side it faces, or of
        // the side behind it.
        Quad.OcclusionFront = -1;
        if (bAmbientOcclusion)
        {
            const int32 Axis = VoxelFaces::GetAxis(Quad.Facing);
            const float Plane = Quad.Positions[0][Axis];
            bool bFlat = true;
            for (const FVector3f& Corner : Quad.Positions)
                bFlat &= FMath::IsNearlyEqual(Corner[Axis], Plane, 1e-4f);
            const bool bPositive = VoxelFaces::IsPositive(Quad.Facing);
            constexpr float Near = 1.f / 16 + 1e-4f;
            if (bFlat && (bPositive ? 1 - Plane : Plane) <= Near)
                Quad.OcclusionFront = int8(Quad.Facing);
            else if (bFlat && (bPositive ? Plane : 1 - Plane) <= Near)
                Quad.OcclusionFront = FVoxelModelQuad::OwnCell;
        }
        ModelQuads.Add(Quad);
    }
    return ModelRanges.Add(FIntPoint(First, Quads.Num()));
}

int32 FVoxelBlockTable::AddModelParts(TConstArrayView<TConstArrayView<int32>> Parts)
{
    const int32 First = PartData.Num();
    for (const TConstArrayView<int32>& Alternatives : Parts)
    {
        PartData.Add(Alternatives.Num());
        PartData.Append(Alternatives.GetData(), Alternatives.Num());
    }
    return First;
}

namespace
{
    constexpr uint64 ModelOffsetSalt = VoxelRandom::Salt("model offset");
    constexpr uint64 ModelPickSalt = VoxelRandom::Salt("model alternative");
}

FVector3f VoxelModels::GetOffset(const FVoxelStateInfo& Info, int32 X, int32 Y)
{
    if (Info.Offset == EVoxelOffset::None) return FVector3f::ZeroVector;
    // Sixteen steps per axis, from one positional hash with no seed and no height.
    const uint64 Bits = VoxelRandom::Hash64(0, ModelOffsetSalt, X, Y);
    auto Step = [Bits](int32 Shift) { return float((Bits >> Shift) & 15) / 15.f; };
    return FVector3f((Step(0) * 2 - 1) * Info.OffsetHorizontal, (Step(4) * 2 - 1) * Info.OffsetHorizontal,
        Info.Offset == EVoxelOffset::HorizontalAndDown ? Step(8) * Info.OffsetDown : 0.f);
}

int32 VoxelModels::PickAlternative(int32 X, int32 Y, int32 Z, int32 Part, int32 Count)
{
    if (Count <= 1) return 0;
    const uint64 Bits = VoxelRandom::Mix64(VoxelRandom::Hash64(0, ModelPickSalt, X, Y, Z) ^ uint64(Part));
    return int32((Bits >> 32) % uint64(Count));
}
