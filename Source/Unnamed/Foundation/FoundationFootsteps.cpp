#include "Foundation/FoundationFootsteps.h"

#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationBreakAssets.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationSoundAssets.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarFootsteps(TEXT("foundation.Player.Footsteps"), 1,
        TEXT("Player footsteps (step 13a2): 1 the block underfoot's step sound at its sound type's volume x 0.15 every "
             "1/0.6 blocks walked (provisional cadence), silent while sneaking or swimming; 0 none, as before."),
        ECVF_Default);
} // namespace

bool FoundationFootsteps::IsOn()
{
    return CVarFootsteps.GetValueOnGameThread() != 0;
}

FoundationFootsteps::EStep FoundationFootsteps::Advance(FState& State, const FFacts& Facts)
{
    if (Facts.bFlying) return EStep::None;
    State.Distance += Facts.HorizontalCm / UFoundationProfile::BlockCm * DistanceScale;
    if (State.Distance <= State.NextStep || !Facts.bGrounded) return EStep::None;
    State.NextStep = FMath::FloorToDouble(State.Distance) + 1;
    return Facts.bSneaking || Facts.bInWater ? EStep::Silent : EStep::Play;
}

bool FoundationFootsteps::FindStepBlocks(const FFoundationBlockWorld& World, const FVector& FeetCm, FStepBlocks& Out)
{
    static const FGameplayTag Combination =
        FGameplayTag::RequestGameplayTag(TEXT("Block.CombinationStepSoundBlocks"), false);
    static const FGameplayTag Fences = FGameplayTag::RequestGameplayTag(TEXT("Block.Fences"), false);
    static const FGameplayTag Walls = FGameplayTag::RequestGameplayTag(TEXT("Block.Walls"), false);
    static const FGameplayTag FenceGates = FGameplayTag::RequestGameplayTag(TEXT("Block.FenceGates"), false);
    static const FGameplayTag Inside = FGameplayTag::RequestGameplayTag(TEXT("Block.InsideStepSoundBlocks"), false);
    Out = FStepBlocks();
    // A lily pad, petals or lichen the feet are in plays instead of the block below.
    const FFoundationBlockState In = World.Get(FoundationGrid::CellAt(FeetCm, World.OriginCm)).State;
    if (!In.IsAir() && Inside.IsValid() && World.GetBlock(In).Tags.HasTag(Inside))
    {
        Out.Primary = In;
        return true;
    }
    FIntVector Cell = FoundationGrid::CellAt(FeetCm - FVector(0, 0, SupportDepthCm), World.OriginCm);
    FFoundationBlockState Support = World.Get(Cell).State;
    if (Support.IsAir())
    {
        // A fence, wall or fence gate is 1.5 blocks tall: its cell is the one below.
        const FIntVector Below = Cell - FIntVector(0, 0, 1);
        const FFoundationBlockState Tall = World.Get(Below).State;
        if (Tall.IsAir()) return false;
        const FGameplayTagContainer& Tags = World.GetBlock(Tall).Tags;
        if (!(Tags.HasTag(Fences) || Tags.HasTag(Walls) || Tags.HasTag(FenceGates))) return false;
        Cell = Below;
        Support = Tall;
    }
    const FFoundationBlockState Above = World.Get(Cell + FIntVector(0, 0, 1)).State;
    if (!Above.IsAir() && Combination.IsValid() && World.GetBlock(Above).Tags.HasTag(Combination))
    {
        Out.Primary = Above;
        Out.Under = Support;
        Out.bCombination = true;
    }
    else
        Out.Primary = Support;
    return true;
}

bool FoundationFootsteps::GetStepSound(const UFoundationBreakAssets& Break, const UFoundationSoundAssets& Sounds,
    int32 Index, bool bUnder, FStepSound& Out)
{
    Out = FStepSound();
    if (!Break.Blocks.IsValidIndex(Index) || !Sounds.Blocks.IsValidIndex(Index)) return false;
    const FFoundationBreakBlock& Type = Break.Blocks[Index];
    const FFoundationSoundBlock& Block = Sounds.Blocks[Index];
    if (Block.Id != Type.Id || !Sounds.Events.IsValidIndex(Block.StepEvent)) return false;
    Out.Event = Sounds.Events[Block.StepEvent].Name;
    Out.Volume = Type.Volume * (bUnder ? UnderVolumeScale : VolumeScale);
    Out.Pitch = Type.Pitch * (bUnder ? UnderPitchScale : PitchScale);
    return true;
}
