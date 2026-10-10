#include "Decoration/Features/VoxelPatchFeatures.h"
#include "Blocks/VoxelBlockTable.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Decoration/VoxelBlockPredicates.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelStateProviders.h"
#include "VoxelConstants.h"

namespace
{
    FIntVector GetStep(int32 Face)
    {
        FIntVector Step = FIntVector::ZeroValue;
        Step[VoxelFaces::GetAxis(Face)] = VoxelFaces::IsPositive(Face) ? 1 : -1;
        return Step;
    }

    void EmitAt(const FVoxelPlantEnvironment& Env, FIntVector Cell, uint16 State, EVoxelWriteSupport Support)
    {
        Env.Emit({Cell, State, EVoxelWriteMode::IfUnchanged, Env.Context.GetState(Cell.X, Cell.Y, Cell.Z), Support});
    }

    EVoxelWriteSupport GetSupport(const FVoxelPlantEnvironment& Env, uint16 State)
    {
        return Env.Survival.GetRule(State).Kind == EVoxelSurvival::Always ? EVoxelWriteSupport::None
                                                                          : EVoxelWriteSupport::Plant;
    }
}

bool VoxelPatchFeatures::PlacePlant(const FVoxelPlantEnvironment& Env, FIntVector Cell, uint16 State)
{
    if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return false;
    const FVoxelDecorationContext& Context = Env.Context;
    if (!Env.Survival.Survives(
            State, Cell.X, Cell.Y, Cell.Z, [&Context](int32 X, int32 Y, int32 Z) { return Context.GetState(X, Y, Z); }))
        return false;
    if (const uint16* Upper = Env.UpperOf.Find(State))
    {
        // A two-cell plant: both halves or neither, the upper cell free (air; water for a water plant).
        const FIntVector Above = Cell + FIntVector(0, 0, 1);
        const uint16 AboveState = Context.GetState(Above.X, Above.Y, Above.Z);
        const bool bFree = AboveState == 0 ||
            (Env.Table.Get(*Upper).HeldFluid != 0 && Env.Table.Get(AboveState).IsFluid() &&
                Env.Survival.IsWaterSource(AboveState));
        if (!bFree || Above.Z > VoxelConstants::MaxZ) return false;
        EmitAt(Env, Cell, State, EVoxelWriteSupport::PlantPair);
        EmitAt(Env, Above, *Upper, EVoxelWriteSupport::Plant);
        return true;
    }
    EmitAt(Env, Cell, State, GetSupport(Env, State));
    return true;
}

bool VoxelPatchFeatures::PlaceSimpleBlock(
    const FVoxelPlantFeature& Feature, FIntVector Position, FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env)
{
    const uint16 State = Env.Providers.Sample(Feature.Provider, Random, Position, Env.Context);
    return PlacePlant(Env, Position, State);
}

void VoxelPatchFeatures::TrimColumn(TArray<int32>& Heights, int32 FreeRun, bool bPrioritizeTip)
{
    int32 Excess = -FMath::Max(0, FreeRun);
    for (const int32 Height : Heights)
        Excess += Height;
    // With prioritize_tip the base layers give up their cells first, so the tip stays; else the top goes.
    for (int32 Step = 0; Excess > 0 && Step < Heights.Num(); ++Step)
    {
        int32& Height = Heights[bPrioritizeTip ? Step : Heights.Num() - 1 - Step];
        const int32 Cut = FMath::Min(Height, Excess);
        Height -= Cut;
        Excess -= Cut;
    }
}

bool VoxelPatchFeatures::PlaceBlockColumn(
    const FVoxelPlantFeature& Feature, FIntVector Position, FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env)
{
    TArray<int32> Heights;
    int32 Total = 0;
    for (const FVoxelBlockColumnLayer& Layer : Feature.Layers)
        Total += Heights.Add_GetRef(FMath::Max(0, Layer.Height.Sample(Random)));
    if (Total == 0) return false;
    const FIntVector Step = GetStep(Feature.Direction);
    int32 Free = 0;
    while (Free < Total && Env.Predicates.Test(Feature.Allowed, Env.Context, Position + Step * Free))
        ++Free;
    if (Free == 0) return false;
    TrimColumn(Heights, Free, Feature.bPrioritizeTip);
    FIntVector Cell = Position;
    for (int32 Layer = 0; Layer < Heights.Num(); ++Layer)
        for (int32 Index = 0; Index < Heights[Layer]; ++Index, Cell += Step)
        {
            if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return true;
            const uint16 State = Env.Providers.Sample(Feature.Layers[Layer].Provider, Random, Cell, Env.Context);
            EmitAt(Env, Cell, State, GetSupport(Env, State));
        }
    return true;
}

bool VoxelPatchFeatures::PlaceDisk(
    const FVoxelPlantFeature& Feature, FIntVector Position, FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env)
{
    const int32 Radius = FMath::Max(0, Feature.Radius.Sample(Random));
    bool bPlaced = false;
    for (int32 DX = -Radius; DX <= Radius; ++DX)
        for (int32 DY = -Radius; DY <= Radius; ++DY)
        {
            if (DX * DX + DY * DY > Radius * Radius) continue;
            for (int32 DZ = Feature.HalfHeight; DZ >= -Feature.HalfHeight; --DZ)
            {
                const FIntVector Cell = Position + FIntVector(DX, DY, DZ);
                if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) continue;
                if (!Env.Predicates.Test(Feature.Target, Env.Context, Cell)) continue;
                const uint16 State = Env.Providers.Sample(Feature.Provider, Random, Cell, Env.Context);
                if (Feature.TargetSet != INDEX_NONE)
                    Env.Emit({Cell, State, EVoxelWriteMode::IfReplaceable, uint16(Feature.TargetSet),
                        EVoxelWriteSupport::None});
                else
                    EmitAt(Env, Cell, State, EVoxelWriteSupport::None);
                bPlaced = true;
            }
        }
    return bPlaced;
}

bool VoxelPatchFeatures::PlaceVegetationPatch(const FVoxelPlantFeature& Feature, FIntVector Position,
    FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env, TArray<FIntVector>& OutVegetation)
{
    const FVoxelDecorationContext& Context = Env.Context;
    const FVoxelBlockSet& Replaceable = Env.Predicates.GetSet(Feature.ReplaceableSet);
    const auto IsGround = [&](int32 X, int32 Y, int32 Z)
    {
        const uint16 State = Context.GetState(X, Y, Z);
        return State != 0 && Replaceable.Contains(Env.Table.Get(State).Block);
    };
    // The footprint reaches one past the drawn radius: scout I's pale moss probe (69 blocks per origin, farthest 3.6
    // to 6.4 blocks) fits radii 3 to 5 from xz_radius 2 to 4, not 2 to 4.
    const int32 RadiusX = FMath::Max(0, Feature.Radius.Sample(Random)) + 1;
    const int32 RadiusY = FMath::Max(0, Feature.Radius.Sample(Random)) + 1;
    bool bPlaced = false;
    for (int32 DX = -RadiusX; DX <= RadiusX; ++DX)
        for (int32 DY = -RadiusY; DY <= RadiusY; ++DY)
        {
            const bool bEdgeX = FMath::Abs(DX) == RadiusX, bEdgeY = FMath::Abs(DY) == RadiusY;
            if (bEdgeX && bEdgeY) continue;
            if ((bEdgeX || bEdgeY) && Random.NextFloat() >= Feature.ExtraEdgeChance) continue;
            const int32 X = Position.X + DX, Y = Position.Y + DY;
            int32 Floor = INDEX_NONE;
            for (int32 Z = Position.Z + Feature.VerticalRange; Z >= Position.Z - Feature.VerticalRange; --Z)
                if (Env.Table.Get(Context.GetState(X, Y, Z)).IsAir() && IsGround(X, Y, Z - 1))
                {
                    Floor = Z - 1;
                    break;
                }
            if (Floor == INDEX_NONE) continue;
            const int32 Deep =
                FMath::Max(0, Feature.Depth.Sample(Random)) + (Random.NextFloat() < Feature.ExtraBottomChance ? 1 : 0);
            bool bGround = false;
            for (int32 Index = 0; Index < Deep && IsGround(X, Y, Floor - Index); ++Index)
            {
                const FIntVector Cell(X, Y, Floor - Index);
                EmitAt(
                    Env, Cell, Env.Providers.Sample(Feature.Provider, Random, Cell, Context), EVoxelWriteSupport::None);
                bGround = true;
            }
            bPlaced |= bGround;
            if (bGround && Random.NextFloat() < Feature.VegetationChance)
                OutVegetation.Add(FIntVector(X, Y, Floor + 1));
        }
    return bPlaced;
}
