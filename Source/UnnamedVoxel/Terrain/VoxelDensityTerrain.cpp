#include "Terrain/VoxelDensityTerrain.h"
#include "Blocks/VoxelBlockTable.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Random/VoxelRandom.h"
#include "Storage/VoxelColumn.h"
#include "VoxelConstants.h"
#include "VoxelStats.h"

namespace
{
    constexpr uint64 DensityTerrainSoilSalt = VoxelRandom::Salt("density-terrain/soil");
    constexpr uint64 DensityTerrainLayerSalt = VoxelRandom::Salt("density-terrain/layers");

    // Larger than squeeze ever returns, so min() never picks it.
    constexpr double DensityTerrainNeutralCave = 64;

    // Probability that a cell is the lower material where two layers blend: 1 at Bottom, 0 at Top.
    FORCEINLINE bool DensityTerrainIsLowerLayer(uint64 Seed, int32 X, int32 Y, int32 Z, int32 Bottom, int32 Top)
    {
        if (Z <= Bottom) return true;
        if (Z >= Top) return false;
        const float Chance = float(Top - Z) / float(Top - Bottom);
        return VoxelRandom::ToUnitFloat(VoxelRandom::Hash64(Seed, DensityTerrainLayerSalt, X, Y, Z)) < Chance;
    }

    FVoxelNoiseSettings DensityTerrainSoilNoise()
    {
        FVoxelNoiseSettings Result;
        Result.Fractal = EVoxelFractal::Fbm;
        Result.Frequency = 1. / 12;
        Result.Octaves = 2;
        return Result;
    }

    bool DensityTerrainEndsWith(const FString& Name, const TCHAR* Suffix)
    {
        return Name.EndsWith(Suffix);
    }
} // namespace

int32 FVoxelDensityTerrain::RemoveCaves(FVoxelDensityGraph& Graph)
{
    TArray<int32> SlopedCheese;
    int32 Rewritten = 0;
    for (const FVoxelDensityFunction& Function : Graph.Functions)
    {
        if (DensityTerrainEndsWith(Function.Name, TEXT("/sloped_cheese"))) SlopedCheese.Add(Function.Node);
        if (DensityTerrainEndsWith(Function.Name, TEXT("/caves/noodle")))
        {
            FVoxelDensityNode& Node = Graph.Nodes[Function.Node];
            Node = FVoxelDensityNode();
            Node.Op = EVoxelDensityOp::Constant;
            Node.Params[0] = DensityTerrainNeutralCave;
            ++Rewritten;
        }
    }
    for (FVoxelDensityNode& Node : Graph.Nodes)
        if (Node.Op == EVoxelDensityOp::RangeChoice && SlopedCheese.Contains(Node.Inputs[0]))
        {
            const int32 Input = Node.Inputs[0];
            Node = FVoxelDensityNode();
            Node.Op = EVoxelDensityOp::Cache;
            Node.Inputs[0] = Input;
            ++Rewritten;
        }
    return Rewritten;
}

FVoxelDensityTerrain::FVoxelDensityTerrain(const FVoxelTerrainPreset& Preset, uint64 InSeed,
    const FVoxelBlockTable& InTable, const FVoxelDensityTerrainSettings& InSettings)
    : Seed(InSeed),
      Table(InTable),
      Settings(InSettings),
      Blocks(FVoxelTerrainBlocks::Find(InTable)),
      Soil(InSeed, DensityTerrainSoilSalt, DensityTerrainSoilNoise()),
      SeaLevel(Preset.SeaLevel)
{
    if (Preset.MinY != VoxelConstants::MinZ || Preset.Height != VoxelConstants::Height)
    {
        Error = FString::Printf(TEXT("%s spans y %d to %d; the world spans %d to %d"), *Preset.Name, Preset.MinY,
            Preset.MinY + Preset.Height - 1, VoxelConstants::MinZ, VoxelConstants::MaxZ);
        return;
    }
    FVoxelDensityGraph Graph = Preset.Graph;
    if (!Settings.bCaves && RemoveCaves(Graph) == 0)
    {
        Error = Preset.Name + TEXT(": no cave functions to turn off");
        return;
    }
    FinalDensity = Preset.Router.FinalDensity;
    for (const FVoxelTerrainPreset::FDebugView& View : Preset.DebugViews)
        if (View.Label == TEXT("PS")) Surface = View.Node;
    if (Surface == INDEX_NONE)
    {
        Error = Preset.Name + TEXT(": no PS view for the surface estimate");
        return;
    }
    FString Reason;
    if (!Evaluator.Compile(Graph, Seed, {FinalDensity}, &Reason) ||
        !SurfaceEvaluator.Compile(Preset.Graph, Seed, {Surface}, &Reason) ||
        !Column.Initialize(Evaluator, FinalDensity, Preset.MinY, Preset.Height, &Reason) ||
        (Settings.bCaves && !Aquifer.Initialize(Preset, Seed, &Reason)))
        Error = Preset.Name + TEXT(": ") + Reason;
}

FVoxelDensityTerrain::~FVoxelDensityTerrain() = default;

TUniquePtr<FVoxelDensityTerrain::FScratch> FVoxelDensityTerrain::TakeScratch() const
{
    {
        FScopeLock Lock(&PoolLock);
        if (!Pool.IsEmpty()) return Pool.Pop(EAllowShrinking::No);
    }
    return MakeUnique<FScratch>();
}

void FVoxelDensityTerrain::ReturnScratch(TUniquePtr<FScratch> Scratch) const
{
    FScopeLock Lock(&PoolLock);
    Pool.Add(MoveTemp(Scratch));
}

int32 FVoxelDensityTerrain::EstimateSurface(int32 X, int32 Y) const
{
    if (!IsValid()) return VoxelConstants::MinZ;
    TUniquePtr<FScratch> Scratch = TakeScratch();
    const double PX = X, PY = 0, PZ = Y;
    SurfaceEvaluator.Evaluate(&PX, &PY, &PZ, 1, Scratch->Point);
    const int32 Result = FMath::FloorToInt32(SurfaceEvaluator.GetValues(Scratch->Point, Surface)[0]);
    ReturnScratch(MoveTemp(Scratch));
    return Result;
}

bool FVoxelDensityTerrain::IsDry(int32 X, int32 Y) const
{
    return EstimateSurface(X, Y) >= SeaLevel;
}

bool FVoxelDensityTerrain::ConfirmDry(int32 X, int32 Y) const
{
    // Water fills to SeaLevel - 1, so a top block there or higher has air above it.
    return GetSurfaceHeight(X, Y) >= SeaLevel - 1;
}

void FVoxelDensityTerrain::GenerateDensity(
    FIntPoint Position, TArray<float>& Out, bool bSkip, FVoxelColumnDensityStats* Stats) const
{
    check(IsValid());
    TUniquePtr<FScratch> Scratch = TakeScratch();
    constexpr int32 Size = VoxelConstants::ColumnSize;
    Column.Generate(Position.X * Size, Position.Y * Size, bSkip, Scratch->Column, Out, Stats);
    ReturnScratch(MoveTemp(Scratch));
}

int32 FVoxelDensityTerrain::GetSurfaceHeight(int32 X, int32 Y) const
{
    if (!IsValid()) return VoxelConstants::MinZ;
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const FIntPoint Position(FMath::FloorToInt32(X / double(Size)), FMath::FloorToInt32(Y / double(Size)));
    TUniquePtr<FScratch> Scratch = TakeScratch();
    Column.Generate(Position.X * Size, Position.Y * Size, Settings.bSkip, Scratch->Column, Scratch->Density);
    const int32 LocalX = X - Position.X * Size, LocalY = Y - Position.Y * Size;
    int32 Result = VoxelConstants::MinZ;
    for (int32 Z = VoxelConstants::MaxZ; Z >= VoxelConstants::MinZ; --Z)
        if (Scratch->Density[((Z - VoxelConstants::MinZ) * Size + LocalY) * Size + LocalX] > 0)
        {
            Result = Z;
            break;
        }
    ReturnScratch(MoveTemp(Scratch));
    return Result;
}

void FVoxelDensityTerrain::Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const
{
    Generate(Position, Out, Dense, nullptr);
}

void FVoxelDensityTerrain::Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense,
    FVoxelDensityTerrainTimings* Timings, const bool* bSkipOverride) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelDensityTerrain_Generate);
    SCOPE_CYCLE_COUNTER(STAT_VoxelGenerateColumn);
    // Init, not SetNumZeroed: callers reuse Dense, and SetNumZeroed clears only elements it adds.
    Dense.Init(0, FVoxelColumn::DenseVolume);
    Out = FVoxelColumn(Position);
    if (!IsValid())
    {
        Out.Build(Dense, Table);
        return;
    }
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const int32 SeaTop = SeaLevel - 1;
    TUniquePtr<FScratch> Scratch = TakeScratch();

    const int32 X0 = Position.X * Size, Z0 = Position.Y * Size;
    const double Start = FPlatformTime::Seconds();
    if (Settings.bCaves)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_AquiferPrepare);
        Aquifer.Prepare(X0, Z0, Scratch->Aquifer);
    }
    const double AfterPrepare = FPlatformTime::Seconds();
    FVoxelColumnDensityStats Stats;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_DensityColumn);
        SCOPE_CYCLE_COUNTER(STAT_VoxelDensityColumn);
        Column.Generate(X0, Z0, bSkipOverride ? *bSkipOverride : Settings.bSkip, Scratch->Column, Scratch->Density,
            &Stats, Settings.bCaves ? Aquifer.GetKeepEmpty(Scratch->Aquifer) : TConstArrayView<uint8>());
    }
    const double AfterDensity = FPlatformTime::Seconds();
    if (Settings.bCaves)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_AquiferFill);
        SCOPE_CYCLE_COUNTER(STAT_VoxelAquiferColumn);
        Aquifer.Fill(X0, Z0, Scratch->Density, Scratch->Aquifer, Scratch->Fluids);
    }
    const double Middle = FPlatformTime::Seconds();
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_FillColumn);
        SCOPE_CYCLE_COUNTER(STAT_VoxelFillColumn);
        const TArray<float>& Density = Scratch->Density;
        const TArray<EVoxelAquiferFluid>& Fluids = Scratch->Fluids;
        for (int32 LocalY = 0; LocalY < Size; ++LocalY)
            for (int32 LocalX = 0; LocalX < Size; ++LocalX)
            {
                const int32 X = X0 + LocalX, Y = Z0 + LocalY;
                const int32 SoilDepth =
                    3 + FMath::Clamp(FMath::FloorToInt32((Soil.Sample(X + .5, Y + .5) + 1) * 1.5f), 0, 2);
                // Blocks below the top of the current solid run; -1 above solid ground.
                int32 Depth = -1;
                // With caves, only the first surface from the sky is dressed; cave floors stay bare rock.
                bool bDressed = false, bBare = false;
                uint16 Top = Blocks.Grass, Under = Blocks.Dirt;
                for (int32 Z = VoxelConstants::MaxZ; Z >= VoxelConstants::MinZ; --Z)
                {
                    const int32 Index = ((Z - VoxelConstants::MinZ) * Size + LocalY) * Size + LocalX;
                    const EVoxelAquiferFluid Fluid = Settings.bCaves ? Fluids[Index]
                        : Z <= SeaTop                                ? EVoxelAquiferFluid::Water
                                                                     : EVoxelAquiferFluid::Air;
                    uint16 State;
                    if (Density[Index] > 0 || Fluid == EVoxelAquiferFluid::Barrier)
                    {
                        if (Depth < 0)
                        {
                            // A new surface: its dressing depends on how it sits against the sea.
                            bBare = Settings.bCaves && bDressed;
                            Top = Blocks.Grass;
                            Under = Blocks.Dirt;
                            if (Z < SeaTop - 6)
                                Top = Under = Blocks.Gravel;
                            else if (Z <= SeaTop + 2)
                                Top = Under = Blocks.Sand;
                            bDressed = true;
                        }
                        ++Depth;
                        if (DensityTerrainIsLowerLayer(Seed, X, Y, Z, VoxelConstants::MinZ, VoxelConstants::MinZ + 5))
                            State = Blocks.Bedrock;
                        else if (Depth < SoilDepth && !bBare)
                            State = Depth == 0 ? Top : Under;
                        else if (DensityTerrainIsLowerLayer(Seed, X, Y, Z, 0, 8))
                            State = Blocks.Deepslate;
                        else
                            State = Blocks.Stone;
                    }
                    else
                    {
                        Depth = -1;
                        State = Fluid == EVoxelAquiferFluid::Water ? Blocks.Water
                            : Fluid == EVoxelAquiferFluid::Lava    ? Blocks.Lava
                                                                   : uint16(0);
                    }
                    Dense[FVoxelColumn::GetDenseIndex(LocalX, LocalY, Z)] = State;
                }
            }
        Out.Build(Dense, Table);
    }
    ReturnScratch(MoveTemp(Scratch));
    if (Timings)
    {
        Timings->DensityMs = (AfterDensity - AfterPrepare) * 1000;
        Timings->AquiferMs = ((AfterPrepare - Start) + (Middle - AfterDensity)) * 1000;
        Timings->BlocksMs = (FPlatformTime::Seconds() - Middle) * 1000;
        Timings->Density = Stats;
    }
}
