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
    const FIntVector Depth = FoundationGrid::CellAt(FeetCm - FVector(0, 0, SupportDepthCm), World.OriginCm);
    FIntVector Cell = Depth;
    FFoundationBlockState Support = World.Get(Cell).State;
    // The player's box, its edges moved in by BoxInsetCm.
    const double Half = GetDefault<UFoundationProfile>()->WidthCm / 2 - BoxInsetCm;
    TArray<FBox> Boxes;
    // Whether a block's collision holds the box up: a box under the footprint whose top is in the 0.2 blocks under the
    // feet. OutTop: the highest such top.
    auto Holds = [&World, &FeetCm, Half, &Boxes](const FIntVector& At, FFoundationBlockState State, double& OutTop)
    {
        Boxes.Reset();
        if (State.IsAir() || !World.GetCollisionBoxes(At, State, Boxes)) return false;
        bool bHolds = false;
        for (const FBox& Box : Boxes)
            if (Box.Min.X < FeetCm.X + Half && Box.Max.X > FeetCm.X - Half && Box.Min.Y < FeetCm.Y + Half &&
                Box.Max.Y > FeetCm.Y - Half && Box.Max.Z > FeetCm.Z - SupportDepthCm &&
                Box.Max.Z <= FeetCm.Z + TopToleranceCm && (!bHolds || Box.Max.Z > OutTop))
            {
                OutTop = Box.Max.Z;
                bHolds = true;
            }
        return bHolds;
    };
    double Top = 0;
    bool bFound = Holds(Cell, Support, Top);
    if (!bFound && Support.IsAir())
    {
        // A fence, wall or fence gate is 1.5 blocks tall: its cell is the one below.
        const FIntVector Below = Cell - FIntVector(0, 0, 1);
        const FFoundationBlockState Tall = World.Get(Below).State;
        const FGameplayTagContainer& Tags = World.GetBlock(Tall).Tags;
        if (!Tall.IsAir() && (Tags.HasTag(Fences) || Tags.HasTag(Walls) || Tags.HasTag(FenceGates)))
        {
            Cell = Below;
            Support = Tall;
            bFound = true;
        }
    }
    if (!bFound)
    {
        // Nothing holds the centre (air, water, a flower): the block under the box with the highest top holds the
        // player up; between equal tops the cell nearest the centre (provisional order).
        const FIntVector Min = FoundationGrid::CellAt(FeetCm - FVector(Half, Half, SupportDepthCm), World.OriginCm);
        const FIntVector Max = FoundationGrid::CellAt(FeetCm + FVector(Half, Half, 0), World.OriginCm);
        double BestTop = 0, Nearest = 0;
        for (int32 Z = Min.Z - 1; Z <= Max.Z; ++Z)
            for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
                for (int32 X = Min.X; X <= Max.X; ++X)
                {
                    const FIntVector Near(X, Y, Z);
                    const FFoundationBlockState State = World.Get(Near).State;
                    double NearTop = 0;
                    if (!Holds(Near, State, NearTop)) continue;
                    const double Distance = FVector::DistSquared2D(
                        FoundationGrid::CellMin(Near, World.OriginCm) + FVector(UFoundationProfile::BlockCm / 2),
                        FeetCm);
                    if (bFound && (NearTop < BestTop || (NearTop == BestTop && Distance >= Nearest))) continue;
                    BestTop = NearTop;
                    Nearest = Distance;
                    Cell = Near;
                    Support = State;
                    bFound = true;
                }
        // A carpet or snow layer standing out of the 0.2 blocks: its support plays under it, as at the centre.
        const FFoundationBlockState Base = World.Get(Cell - FIntVector(0, 0, 1)).State;
        if (bFound && Cell.Z > Depth.Z && !Base.IsAir() && Combination.IsValid() &&
            World.GetBlock(Support).Tags.HasTag(Combination))
        {
            Cell -= FIntVector(0, 0, 1);
            Support = Base;
        }
    }
    // Nothing under the box either: the centre's block as it is (water: silent), or nothing over air.
    if (!bFound && Support.IsAir()) return false;
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
