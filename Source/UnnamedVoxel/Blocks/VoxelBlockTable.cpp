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
