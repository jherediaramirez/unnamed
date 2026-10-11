#include "Terrain/VoxelSpawnFinder.h"

#include "Biome/VoxelBiomeTable.h"
#include "Terrain/VoxelTerrain.h"

namespace
{
    // A ring of ceil(2 pi r / Step) points around Center at angles 2 pi k / n from +X.
    void AddRing(FIntPoint Center, int32 Radius, int32 Step, TArray<FIntPoint>& Out)
    {
        const int32 Count = FMath::CeilToInt32(UE_DOUBLE_TWO_PI * Radius / Step);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const double Angle = UE_DOUBLE_TWO_PI * Index / Count;
            Out.Add(Center +
                FIntPoint(
                    FMath::RoundToInt32(Radius * FMath::Cos(Angle)), FMath::RoundToInt32(Radius * FMath::Sin(Angle))));
        }
    }
} // namespace

bool FVoxelSpawnFinder::Initialize(const FVoxelTerrainPreset& Preset, uint64 WorldSeed, FString* OutError)
{
    Targets = Preset.SpawnTargets;
    TArray<int32> Roots;
    for (const FTarget& Target : Targets)
        for (const FVoxelTerrainPreset::FSpawnRange& Range : Target)
            Roots.AddUnique(Range.Node);
    bInitialized = Roots.IsEmpty() || Evaluator.Compile(Preset.Graph, WorldSeed, Roots, OutError);
    return bInitialized;
}

FIntPoint FVoxelSpawnFinder::FindClimateSpot() const
{
    check(bInitialized);
    TArray<FIntPoint> Candidates;
    GetFirstCandidates(Candidates);
    FIntPoint Best = Candidates[0];
    int64 BestFitness = MAX_int64;
    PickBest(Candidates, Best, BestFitness);
    Candidates.Reset();
    GetSecondCandidates(Best, Candidates);
    PickBest(Candidates, Best, BestFitness);
    return Best;
}

void FVoxelSpawnFinder::PickBest(TConstArrayView<FIntPoint> Candidates, FIntPoint& Best, int64& BestFitness) const
{
    // The reference's x is our X, its z our Y; the climate is read at its y 0.
    TArray<double> X, Y, Z;
    for (const FIntPoint& Candidate : Candidates)
    {
        X.Add(Candidate.X);
        Y.Add(0);
        Z.Add(Candidate.Y);
    }
    FVoxelDensityScratch Scratch;
    if (Evaluator.IsCompiled()) Evaluator.Evaluate(X.GetData(), Y.GetData(), Z.GetData(), Candidates.Num(), Scratch);
    for (int32 Index = 0; Index < Candidates.Num(); ++Index)
    {
        const int64 Fitness = GetDistancePenalty(Candidates[Index]) +
            GetClimateDistance(Targets, [this, &Scratch, Index](int32 Node)
                { return FVoxelBiomeTable::Quantize(Evaluator.GetValues(Scratch, Node)[Index]); });
        if (Fitness < BestFitness)
        {
            Best = Candidates[Index];
            BestFitness = Fitness;
        }
    }
}

void FVoxelSpawnFinder::GetFirstCandidates(TArray<FIntPoint>& Out)
{
    Out.Add(FIntPoint::ZeroValue);
    for (int32 Radius = 512; Radius <= 2048; Radius += 512)
        AddRing(FIntPoint::ZeroValue, Radius, 512, Out);
}

void FVoxelSpawnFinder::GetSecondCandidates(FIntPoint Center, TArray<FIntPoint>& Out)
{
    for (int32 Radius = 32; Radius <= 512; Radius += 32)
        AddRing(Center, Radius, 32, Out);
}

int64 FVoxelSpawnFinder::GetDistancePenalty(FIntPoint Point)
{
    const int64 Squared = int64(Point.X) * Point.X + int64(Point.Y) * Point.Y;
    return Squared * Squared / 390625;
}

int64 FVoxelSpawnFinder::GetClimateDistance(TConstArrayView<FTarget> InTargets, TFunctionRef<int64(int32 Node)> Value)
{
    int64 Best = InTargets.IsEmpty() ? 0 : MAX_int64;
    for (const FTarget& Target : InTargets)
    {
        int64 Sum = 0;
        for (const FVoxelTerrainPreset::FSpawnRange& Range : Target)
        {
            const int64 Sample = Value(Range.Node);
            const int64 Outside = Sample < Range.Min ? Range.Min - Sample : Sample > Range.Max ? Sample - Range.Max : 0;
            Sum += Outside * Outside;
        }
        Best = FMath::Min(Best, Sum);
    }
    return Best;
}

FVoxelSpawn FVoxelSpawnFinder::Find(const IVoxelTerrain& Terrain, const FVoxelSpawnRequest& Request)
{
    FIntVector Cell;
    bool bDry = false;
    if (!Request.bNearest && Terrain.FindSpawn(Cell, bDry))
    {
        if (bDry) return {Cell, true};
        // Nothing dry near the climate's pick: the nearest land from it, else its water surface.
        if (const TOptional<FIntVector> Land = FindNearestDry(Terrain, {Cell.X, Cell.Y}, FallbackReach, false))
            return {*Land, true};
        return {Cell, false};
    }
    const FIntPoint Center = Request.bNearest ? Request.Center : FIntPoint::ZeroValue;
    if (const TOptional<FIntVector> Land = FindNearestDry(Terrain, Center, Request.ViewReach, Request.bExact))
        return {*Land, false};
    // All sea: stand on the water surface's floor at the centre.
    return {FIntVector(Center.X, Center.Y, Terrain.GetSurfaceHeight(Center.X, Center.Y) + 1), false};
}

TOptional<FIntVector> FVoxelSpawnFinder::FindNearestDry(
    const IVoxelTerrain& Terrain, FIntPoint Center, int32 Reach, bool bExact)
{
    // -VoxelSpawnExact confirms every column within 32 blocks instead (a few seconds at most).
    int32 Confirmations = bExact ? 65 * 65 : 64;
    for (int32 Ring = 0; Ring <= Reach && Confirmations > 0; ++Ring)
        for (int32 Y = -Ring; Y <= Ring; ++Y)
            for (int32 X = -Ring; X <= Ring; ++X)
            {
                if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Ring ||
                    (!bExact && !Terrain.IsDry(Center.X + X, Center.Y + Y)))
                    continue;
                if (Confirmations <= 0 || (--Confirmations, !Terrain.ConfirmDry(Center.X + X, Center.Y + Y))) continue;
                return FIntVector(Center.X + X, Center.Y + Y, Terrain.GetSurfaceHeight(Center.X + X, Center.Y + Y) + 1);
            }
    return {};
}

int32 FVoxelSpawnFinder::GetStandingZ(TFunctionRef<bool(int32 Z)> Collides, int32 Bottom, int32 Top, int32 Fallback)
{
    for (int32 Z = Top; Z >= Bottom; --Z)
        if (Collides(Z)) return Z + 1;
    return Fallback;
}
