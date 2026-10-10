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

    // The first Count items in a random order.
    void ShuffleFaces(int32* Items, int32 Count, FVoxelXoroshiro& Random)
    {
        for (int32 Index = Count - 1; Index > 0; --Index)
            Swap(Items[Index], Items[Random.NextInt(Index + 1)]);
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

bool VoxelPatchFeatures::PlaceMultiface(
    const FVoxelPlantFeature& Feature, FIntVector Position, FVoxelXoroshiro& Random, const FVoxelPlantEnvironment& Env)
{
    const FVoxelDecorationContext& Context = Env.Context;
    const FVoxelBlockSet& Base = Env.Predicates.GetSet(Feature.BaseSet);
    const auto GetState = [&Context](int32 X, int32 Y, int32 Z) { return Context.GetState(X, Y, Z); };
    // 0 when Cell is neither air nor a water source (or lies outside the window or the world), else 1 dry, 2 wet.
    const auto GetFree = [&](FIntVector Cell) -> int32
    {
        if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ || !Context.Contains(Cell.X, Cell.Y))
            return 0;
        const uint16 State = Context.GetState(Cell.X, Cell.Y, Cell.Z);
        const FVoxelStateInfo& Info = Env.Table.Get(State);
        return Info.IsAir() ? 1 : Info.IsFluid() && Env.Survival.IsWaterSource(State) ? 2 : 0;
    };
    // The state Cell takes on Face, or 0 when the face does not hold there.
    const auto GetFaceState = [&](FIntVector Cell, int32 Free, int32 Face) -> uint16
    {
        const FIntVector Support = Cell + GetStep(Face);
        const uint16 State = Feature.FaceStates[Face][Free - 1];
        return Base.Contains(Env.Table.Get(Context.GetState(Support.X, Support.Y, Support.Z)).Block) &&
                Env.Survival.HoldsFace(State, Face, Cell.X, Cell.Y, Cell.Z, GetState)
            ? State
            : uint16(0);
    };
    // Tries a free cell's allowed faces in a random order; returns the state placed (0 for none) and its face.
    const auto TryCell = [&](FIntVector Cell, int32 Free, int32& OutFace) -> uint16
    {
        int32 Faces[6], Count = 0;
        for (int32 Face = 0; Face < 6; ++Face)
            if (Feature.Faces >> Face & 1) Faces[Count++] = Face;
        ShuffleFaces(Faces, Count, Random);
        for (int32 Index = 0; Index < Count; ++Index)
            if (const uint16 State = GetFaceState(Cell, Free, Faces[Index]))
            {
                OutFace = Faces[Index];
                return State;
            }
        return 0;
    };

    FIntVector Cell = Position;
    int32 Face = INDEX_NONE;
    const int32 OriginFree = GetFree(Cell);
    uint16 State = OriginFree ? TryCell(Cell, OriginFree, Face) : uint16(0);
    if (!State)
    {
        int32 Directions[6] = {0, 1, 2, 3, 4, 5};
        ShuffleFaces(Directions, 6, Random);
        for (int32 Index = 0; Index < 6 && !State; ++Index)
            for (int32 Steps = 1; Steps <= Feature.SearchSteps && !State; ++Steps)
            {
                Cell = Position + GetStep(Directions[Index]) * Steps;
                const int32 Free = GetFree(Cell);
                if (!Free) break;
                State = TryCell(Cell, Free, Face);
            }
        if (!State) return false;
    }
    // The cell's own support lies beside or above it, often in another column, so the replay checks only that the
    // cell is unchanged.
    EmitAt(Env, Cell, State, EVoxelWriteSupport::None);
    if (Random.NextFloat() < Feature.SpreadChance)
    {
        // One of the four directions off the face's axis.
        int32 Pick = Random.NextInt(4), Direction = 0;
        for (int32 Side = 0; Side < 6; ++Side)
            if (VoxelFaces::GetAxis(Side) != VoxelFaces::GetAxis(Face) && Pick-- == 0)
            {
                Direction = Side;
                break;
            }
        const FIntVector Next = Cell + GetStep(Direction);
        if (const int32 Free = GetFree(Next))
            if (const uint16 Spread = GetFaceState(Next, Free, Face))
                EmitAt(Env, Next, Spread, EVoxelWriteSupport::None);
    }
    return true;
}
