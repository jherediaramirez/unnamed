#include "Terrain/VoxelDensityTerrain.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Random/VoxelRandom.h"
#include "Storage/VoxelColumn.h"
#include "Surface/VoxelSurfaceContext.h"
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
    const FVoxelBlockTable& InTable, const FVoxelDensityTerrainSettings& InSettings, const FVoxelBiomeTable* BiomeTable,
    const FVoxelSurfaceInputs* SurfaceInputs)
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
        (Settings.bCaves && !Aquifer.Initialize(Preset, Seed, &Reason)) ||
        !SpawnFinder.Initialize(Preset, Seed, &Reason) ||
        (BiomeTable && !InitializeBiomes(*BiomeTable, Preset, Reason)))
    {
        Error = Preset.Name + TEXT(": ") + Reason;
        return;
    }
    // Compiled even with bSurfaceRules off, so a per-call override (the bench) can turn them on.
    if (SurfaceInputs)
    {
        if (!BiomeTable)
            Reason = TEXT("surface rules need biomes");
        else if (SurfaceRules.Compile(*SurfaceInputs, InTable, Biomes.GetTable(), Seed, &Reason))
            Freeze.Initialize(InTable, &Reason);
        if (!Freeze.IsInitialized())
        {
            Error = Preset.Name + TEXT(": ") + Reason;
            return;
        }
    }
    // The carvers (S10) need caves (their fluids come from the aquifer), the rules (they carve the ruled blocks) and
    // the decoration data; compiled even with bCarvers off, so a per-call override (the bench, tests) can turn them on.
    if (Settings.Features && Settings.bCaves && SurfaceRules.IsCompiled())
    {
        if (Carvers.Initialize(*Settings.Features, Seed, &CarverError))
            CarveCache = MakeUnique<FVoxelCarvePathCache>();
        else if (Settings.bCarvers)
        {
            Error = Preset.Name + TEXT(": ") + CarverError;
            return;
        }
    }
    else
        CarverError = TEXT("the carvers need caves, the surface rules and the decoration data");
    // Sharing only saves work, so a column shape the cache cannot hold just goes without it.
    if (Settings.bShareCorners)
    {
        CornerCache = MakeUnique<FVoxelCornerCache>();
        if (!Column.MakeCornerCache(*CornerCache)) CornerCache.Reset();
    }
}

FVoxelDensityTerrain::~FVoxelDensityTerrain() = default;

bool FVoxelDensityTerrain::InitializeBiomes(
    const FVoxelBiomeTable& BiomeTable, const FVoxelTerrainPreset& Preset, FString& OutError)
{
    if (!BiomeTable.GrassColormap.IsEmpty() && !Tints.Initialize(BiomeTable, &OutError)) return false;
    if (Settings.FixedBiome.IsEmpty()) return Biomes.Initialize(BiomeTable, Preset, Seed, &OutError);
    const int32 Biome = BiomeTable.FindBiome(Settings.FixedBiome);
    if (Biome == INDEX_NONE)
    {
        OutError = FString::Printf(
            TEXT("%s is not one of the world type's %d biomes"), *Settings.FixedBiome, BiomeTable.Biomes.Num());
        return false;
    }
    return Biomes.InitializeFixed(BiomeTable, Biome, &OutError);
}

void FVoxelDensityTerrain::ResetCaches() const
{
    if (CornerCache) CornerCache->Reset();
    if (CarveCache) CarveCache->Reset();
}

int64 FVoxelDensityTerrain::GetCornerCacheBytes() const
{
    return CornerCache ? CornerCache->GetBytes() : 0;
}

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
    Column.Generate(Position.X * Size, Position.Y * Size, bSkip, Scratch->Column, Out, Stats, {}, CornerCache.Get());
    ReturnScratch(MoveTemp(Scratch));
}

int32 FVoxelDensityTerrain::GetSurfaceHeight(int32 X, int32 Y) const
{
    if (!IsValid()) return VoxelConstants::MinZ;
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const FIntPoint Position(FMath::FloorToInt32(X / double(Size)), FMath::FloorToInt32(Y / double(Size)));
    TUniquePtr<FScratch> Scratch = TakeScratch();
    Column.Generate(Position.X * Size, Position.Y * Size, Settings.bSkip, Scratch->Column, Scratch->Density, nullptr,
        {}, CornerCache.Get());
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

void FVoxelDensityTerrain::GetSurfaceHeights(FIntPoint Position, TArrayView<int32> Out) const
{
    constexpr int32 Size = VoxelConstants::ColumnSize;
    check(Out.Num() == Size * Size);
    if (!IsValid())
    {
        for (int32& Height : Out)
            Height = VoxelConstants::MinZ;
        return;
    }
    TUniquePtr<FScratch> Scratch = TakeScratch();
    Column.Generate(Position.X * Size, Position.Y * Size, Settings.bSkip, Scratch->Column, Scratch->Density, nullptr,
        {}, CornerCache.Get());
    const float* Density = Scratch->Density.GetData();
    for (int32 Index = 0; Index < Size * Size; ++Index)
    {
        int32 Result = VoxelConstants::MinZ;
        for (int32 Z = VoxelConstants::MaxZ; Z >= VoxelConstants::MinZ; --Z)
            if (Density[(Z - VoxelConstants::MinZ) * Size * Size + Index] > 0)
            {
                Result = Z;
                break;
            }
        Out[Index] = Result;
    }
    ReturnScratch(MoveTemp(Scratch));
}

bool FVoxelDensityTerrain::FindSpawn(FIntVector& OutCell, bool& bOutDry) const
{
    if (!IsValid()) return false;
    const FIntPoint Spot = SpawnFinder.FindClimateSpot();
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const FIntPoint Center(FMath::FloorToInt32(Spot.X / double(Size)), FMath::FloorToInt32(Spot.Y / double(Size)));
    if (const TOptional<FIntVector> Land = FindDryRing(Center, 0, FVoxelSpawnFinder::SpawnRadius))
    {
        OutCell = *Land;
        bOutDry = true;
        return true;
    }
    OutCell = FIntVector(Spot.X, Spot.Y, SeaLevel);
    bOutDry = false;
    return true;
}

TOptional<FIntVector> FVoxelDensityTerrain::FindDryRing(FIntPoint Around, int32 FirstRing, int32 LastRing) const
{
    constexpr int32 Size = VoxelConstants::ColumnSize;
    return FVoxelSpawnFinder::SearchRings(
        Around, FirstRing, LastRing,
        [this](FIntPoint Position) -> TOptional<FIntVector>
        {
            int32 Heights[Size * Size];
            GetSurfaceHeights(Position, Heights);
            for (int32 Index = 0; Index < Size * Size; ++Index)
                // Water fills to SeaLevel - 1, so a top block there or higher has air above it.
                if (Heights[Index] >= SeaLevel - 1)
                    return FIntVector(
                        Position.X * Size + Index % Size, Position.Y * Size + Index / Size, Heights[Index] + 1);
            return {};
        },
        true);
}

void FVoxelDensityTerrain::Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const
{
    Generate(Position, Out, Dense, nullptr);
}

void FVoxelDensityTerrain::GenerateSnapshot(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const
{
    FVoxelDensityTerrainOverrides Overrides;
    Overrides.bFreeze = false;
    Generate(Position, Out, Dense, nullptr, Overrides);
}

void FVoxelDensityTerrain::FinishColumn(FVoxelColumn& Final) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelDensityTerrain_FinishColumn);
    // Where Generate runs the freeze pass: with the rules, over the column's biomes.
    if (IsValid() && SurfaceRules.IsCompiled() && Settings.bSurfaceRules && !Final.GetBiomes().IsEmpty())
        Freeze.ApplyToColumn(Final, Biomes.GetTable(), Climate, SeaLevel);
}

void FVoxelDensityTerrain::Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense,
    FVoxelDensityTerrainTimings* Timings, const FVoxelDensityTerrainOverrides& Overrides) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelDensityTerrain_Generate);
    SCOPE_CYCLE_COUNTER(STAT_VoxelGenerateColumn);
    Out = FVoxelColumn(Position);
    if (!IsValid())
    {
        // Init, not SetNumZeroed: callers reuse Dense, and SetNumZeroed clears only elements it adds.
        Dense.Init(0, FVoxelColumn::DenseVolume);
        Out.Build(Dense, Table);
        return;
    }
    // Both fills below write every block.
    Dense.SetNumUninitialized(FVoxelColumn::DenseVolume, EAllowShrinking::No);
    constexpr int32 Size = VoxelConstants::ColumnSize;
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
        Column.Generate(X0, Z0, Overrides.bSkip.Get(Settings.bSkip), Scratch->Column, Scratch->Density, &Stats,
            Settings.bCaves ? Aquifer.GetKeepEmpty(Scratch->Aquifer) : TConstArrayView<uint8>(),
            Overrides.bShareCorners.Get(true) ? CornerCache.Get() : nullptr);
    }
    const double AfterDensity = FPlatformTime::Seconds();
    if (Settings.bCaves)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_AquiferFill);
        SCOPE_CYCLE_COUNTER(STAT_VoxelAquiferColumn);
        Aquifer.Fill(X0, Z0, Scratch->Density, Scratch->Aquifer, Scratch->Fluids);
    }
    const double AfterAquifer = FPlatformTime::Seconds();
    // Before the blocks, which the surface rules choose by biome.
    if (Biomes.IsInitialized())
    {
        FVoxelBiomeStorage Storage;
        Biomes.FillColumn(Position, Scratch->Biome, Storage, Overrides.BiomeFill.Get(EVoxelBiomeFill::Pruned));
        Out.SetBiomes(MoveTemp(Storage));
    }
    const double Middle = FPlatformTime::Seconds();
    FVoxelDensityTerrainTimings SurfaceTimings;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_FillColumn);
        SCOPE_CYCLE_COUNTER(STAT_VoxelFillColumn);
        if (SurfaceRules.IsCompiled() && Overrides.bSurfaceRules.Get(Settings.bSurfaceRules) &&
            !Out.GetBiomes().IsEmpty())
            FillWithRules(Position, Out, Dense, *Scratch, Overrides, SurfaceTimings);
        else
            FillDressed(X0, Z0, Dense, *Scratch);
        Out.Build(Dense, Table);
    }
    ReturnScratch(MoveTemp(Scratch));
    if (Timings)
    {
        Timings->DensityMs = (AfterDensity - AfterPrepare) * 1000;
        Timings->AquiferMs = ((AfterPrepare - Start) + (AfterAquifer - AfterDensity)) * 1000;
        Timings->BiomesMs = (Middle - AfterAquifer) * 1000;
        Timings->BlocksMs = (FPlatformTime::Seconds() - Middle) * 1000;
        Timings->SurfaceMs = SurfaceTimings.SurfaceMs;
        Timings->PreliminaryMs = SurfaceTimings.PreliminaryMs;
        Timings->FreezeMs = SurfaceTimings.FreezeMs;
        Timings->VeinMs = SurfaceTimings.VeinMs;
        Timings->VeinCells = SurfaceTimings.VeinCells;
        Timings->VeinCorners = SurfaceTimings.VeinCorners;
        Timings->VeinBlocks = SurfaceTimings.VeinBlocks;
        Timings->VeinGaps = SurfaceTimings.VeinGaps;
        Timings->VeinGiven = SurfaceTimings.VeinGiven;
        Timings->SurfaceDeep = SurfaceTimings.SurfaceDeep;
        Timings->SurfaceInterior = SurfaceTimings.SurfaceInterior;
        Timings->SurfaceFull = SurfaceTimings.SurfaceFull;
        Timings->SurfaceInteriorRuns = SurfaceTimings.SurfaceInteriorRuns;
        Timings->SurfaceSteps = SurfaceTimings.SurfaceSteps;
        Timings->CarversMs = SurfaceTimings.CarversMs;
        Timings->Carve = SurfaceTimings.Carve;
        Timings->CarveCandidates = SurfaceTimings.CarveCandidates;
        Timings->CarveAir = SurfaceTimings.CarveAir;
        Timings->CarveWater = SurfaceTimings.CarveWater;
        Timings->CarveLava = SurfaceTimings.CarveLava;
        Timings->CarveBarriers = SurfaceTimings.CarveBarriers;
        Timings->Density = Stats;
    }
}

void FVoxelDensityTerrain::FillDressed(int32 X0, int32 Z0, TArray<uint16>& Dense, const FScratch& Scratch) const
{
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const int32 SeaTop = SeaLevel - 1;
    const TArray<float>& Density = Scratch.Density;
    const TArray<EVoxelAquiferFluid>& Fluids = Scratch.Fluids;
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
}

void FVoxelDensityTerrain::FillWithRules(FIntPoint Position, const FVoxelColumn& Out, TArray<uint16>& Dense,
    FScratch& Scratch, const FVoxelDensityTerrainOverrides& Overrides, FVoxelDensityTerrainTimings& Timings) const
{
    const bool bShortcut = Overrides.bSurfaceShortcut.Get(Settings.bSurfaceShortcut);
    const bool bFreeze = Overrides.bFreeze.Get(true);
    constexpr int32 Size = VoxelConstants::ColumnSize, Height = VoxelConstants::Height;
    const int32 SeaTop = SeaLevel - 1;
    const int32 X0 = Position.X * Size, Y0 = Position.Y * Size;
    // What each block holds before the rules (the aquifer's barriers are stone), and each block column's top non-air
    // block for the steep test. Layer by layer, so the dense buffers are read and written in order; Kinds stays one
    // run per block column, as the rules read it. Raw pointers and local copies keep range checks and reloads out of
    // the loop.
    constexpr int32 Area = Size * Size;
    Scratch.Kinds.SetNumUninitialized(Area * Height);
    EVoxelSurfaceKind* const Kinds = Scratch.Kinds.GetData();
    const float* const Density = Scratch.Density.GetData();
    const EVoxelAquiferFluid* const Fluids = Settings.bCaves ? Scratch.Fluids.GetData() : nullptr;
    uint16* const States = Dense.GetData();
    const uint16 Stone = Blocks.Stone, Water = Blocks.Water, Lava = Blocks.Lava;
    int32 Heights[Area];
    for (int32& Top : Heights)
        Top = VoxelConstants::MinZ - 1;
    for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
    {
        const int32 Local = Z - VoxelConstants::MinZ, First = Local * Area;
        const EVoxelAquiferFluid Open = Z <= SeaTop ? EVoxelAquiferFluid::Water : EVoxelAquiferFluid::Air;
        for (int32 Cell = 0; Cell < Area; ++Cell)
        {
            const int32 Index = First + Cell;
            const EVoxelAquiferFluid Fluid = Fluids ? Fluids[Index] : Open;
            EVoxelSurfaceKind Kind = EVoxelSurfaceKind::Air;
            uint16 State = 0;
            if (Density[Index] > 0 || Fluid == EVoxelAquiferFluid::Barrier)
            {
                Kind = EVoxelSurfaceKind::Solid;
                State = Stone;
            }
            else if (Fluid == EVoxelAquiferFluid::Water)
            {
                Kind = EVoxelSurfaceKind::Water;
                State = Water;
            }
            else if (Fluid == EVoxelAquiferFluid::Lava)
            {
                Kind = EVoxelSurfaceKind::Lava;
                State = Lava;
            }
            Kinds[Cell * Height + Local] = Kind;
            States[Index] = State;
            if (Kind != EVoxelSurfaceKind::Air) Heights[Cell] = Z;
        }
    }

    const double Start = FPlatformTime::Seconds();
    TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_SurfaceRules);
    // The preliminary surface at the column's four corners, lerped across it as the jar does.
    double Corners[4];
    {
        const double PX[4] = {double(X0), double(X0 + Size), double(X0), double(X0 + Size)};
        const double PY[4] = {0, 0, 0, 0};
        const double PZ[4] = {double(Y0), double(Y0), double(Y0 + Size), double(Y0 + Size)};
        SurfaceEvaluator.Evaluate(PX, PY, PZ, 4, Scratch.Point);
        const TConstArrayView<float> Values = SurfaceEvaluator.GetValues(Scratch.Point, Surface);
        for (int32 Corner = 0; Corner < 4; ++Corner)
            Corners[Corner] = Values[Corner];
    }
    const double AfterPreliminary = FPlatformTime::Seconds();
    FVoxelSurfaceChunk& Chunk = Scratch.SurfaceChunk;
    SurfaceRules.PrepareChunk(Chunk, &Out.GetBiomes(), bShortcut, Overrides.bOreVeins.Get(Settings.bOreVeins),
        Overrides.bOreVeinShortcut.Get(Settings.bOreVeinShortcut));
    const double BeforeVeins = FPlatformTime::Seconds();
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_OreVeins);
        SurfaceRules.PrepareVeins(Chunk, X0, Y0, Scratch.Kinds);
    }
    Timings.VeinMs = (FPlatformTime::Seconds() - BeforeVeins) * 1000;
    if (Chunk.bVeins)
    {
        Timings.VeinCells = Chunk.Veins.OpenCells;
        Timings.VeinCorners = Chunk.Veins.Corners;
        Timings.VeinBlocks = Chunk.Veins.Blocks;
        Timings.VeinGaps = Chunk.Veins.Gaps;
        Timings.VeinGiven = Chunk.Veins.Given;
    }
    constexpr int32 Stride = Size * Size;
    for (int32 LocalY = 0; LocalY < Size; ++LocalY)
        for (int32 LocalX = 0; LocalX < Size; ++LocalX)
        {
            FVoxelSurfaceColumn SurfaceColumn;
            SurfaceRules.PrepareColumn(SurfaceColumn, X0 + LocalX, Y0 + LocalY,
                VoxelSurfaceContext::LerpPreliminary(
                    double(LocalX) / Size, double(LocalY) / Size, Corners[0], Corners[1], Corners[2], Corners[3]),
                VoxelSurfaceContext::IsSteep(MakeArrayView(Heights), Size, LocalX, LocalY));
            const int32 First = FVoxelColumn::GetDenseIndex(LocalX, LocalY, VoxelConstants::MinZ);
            SurfaceRules.ApplyColumn(Chunk, SurfaceColumn, LocalX, LocalY,
                TConstArrayView<EVoxelSurfaceKind>(&Scratch.Kinds[(LocalY * Size + LocalX) * Height], Height),
                TArrayView<uint16>(&Dense[First], Dense.Num() - First), Stride, Heights[LocalY * Size + LocalX]);
        }
    const double AfterRules = FPlatformTime::Seconds();
    // The carvers cut the ruled blocks (wiki: carvers run after the surface), before the freeze.
    if (Fluids && Carvers.IsInitialized() && Overrides.bCarvers.Get(Settings.bCarvers))
        Carve(Position, Dense, Scratch, MakeArrayView(Heights), Overrides, Timings);
    const double AfterCarve = FPlatformTime::Seconds();
    if (bFreeze)
        Freeze.Apply(Position, Dense, Out.GetBiomes(), Biomes.GetTable(), Climate, SeaLevel, MakeArrayView(Heights));
    const double End = FPlatformTime::Seconds();
    Timings.CarversMs = (AfterCarve - AfterRules) * 1000;
    Timings.SurfaceMs = (End - Start) * 1000 - Timings.CarversMs;
    Timings.PreliminaryMs = (AfterPreliminary - Start) * 1000;
    Timings.FreezeMs = (End - AfterCarve) * 1000;
    Timings.SurfaceDeep = Chunk.DeepBlocks;
    Timings.SurfaceInterior = Chunk.InteriorBlocks;
    Timings.SurfaceFull = Chunk.FullBlocks;
    Timings.SurfaceInteriorRuns = Chunk.InteriorRuns;
    Timings.SurfaceSteps = Chunk.Steps;
}

void FVoxelDensityTerrain::Carve(FIntPoint Position, TArray<uint16>& Dense, FScratch& Scratch,
    TArrayView<int32> Heights, const FVoxelDensityTerrainOverrides& Overrides,
    FVoxelDensityTerrainTimings& Timings) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Voxel_Carvers);
    constexpr int32 Size = VoxelConstants::ColumnSize, Area = Size * Size, Height = VoxelConstants::Height;
    Scratch.CarveMask.SetNumUninitialized(FVoxelCarvers::MaskWords, EAllowShrinking::No);
    Carvers.Mark(Position, Scratch.CarveMask, Overrides.bCarveCache.Get(true) ? CarveCache.Get() : nullptr,
        &Timings.Carve, Overrides.CarveReach.Get(FVoxelCarvers::Reach));

    // The marked blocks that can be carved: 26.3 has no carver tag, and its carvers were never seen to cut air,
    // fluids or bedrock (scout K's same-seed diff), so everything else is cut.
    uint16* const States = Dense.GetData();
    const uint16 Water = Blocks.Water, Lava = Blocks.Lava, Bedrock = Blocks.Bedrock;
    TArray<int32>& Candidates = Scratch.CarveBlocks;
    Candidates.Reset();
    const uint64* const Bits = Scratch.CarveMask.GetData();
    for (int32 Word = 0; Word < FVoxelCarvers::MaskWords; ++Word)
        for (uint64 Rest = Bits[Word]; Rest; Rest &= Rest - 1)
        {
            const int32 Index = Word * 64 + int32(FMath::CountTrailingZeros64(Rest));
            const uint16 State = States[Index];
            if (State != 0 && State != Water && State != Lava && State != Bedrock) Candidates.Add(Index);
        }
    Timings.CarveCandidates = Candidates.Num();
    if (Candidates.IsEmpty()) return;

    // Each block column's highest block of positive density before carving, the top of Fill's sky: the first solid
    // kind at or below Heights (a barrier never lies under open sky, so the first solid kind is a dense block).
    int32 SkyTop[Area];
    const EVoxelSurfaceKind* const Kinds = Scratch.Kinds.GetData();
    for (int32 Cell = 0; Cell < Area; ++Cell)
    {
        int32 Z = Heights[Cell];
        while (Z >= VoxelConstants::MinZ && Kinds[Cell * Height + Z - VoxelConstants::MinZ] != EVoxelSurfaceKind::Solid)
            --Z;
        SkyTop[Cell] = Z;
    }
    // What fills each carved block: what the aquifer gives an open block there (S10-Q1 (a)); a barrier stays solid.
    Aquifer.FillCarved(Position.X * Size, Position.Y * Size, Candidates, Scratch.Density, MakeArrayView(SkyTop),
        Scratch.Aquifer, Scratch.CarveFluids);
    for (int32 K = 0; K < Candidates.Num(); ++K)
        switch (Scratch.CarveFluids[K])
        {
        case EVoxelAquiferFluid::Barrier:
            ++Timings.CarveBarriers;
            break;
        case EVoxelAquiferFluid::Water:
            States[Candidates[K]] = Water;
            ++Timings.CarveWater;
            break;
        case EVoxelAquiferFluid::Lava:
            States[Candidates[K]] = Lava;
            ++Timings.CarveLava;
            break;
        default:
            States[Candidates[K]] = 0;
            ++Timings.CarveAir;
        }
    // The freeze starts its search for the top at Heights, so they come down past carved air.
    for (int32 Cell = 0; Cell < Area; ++Cell)
    {
        int32& Top = Heights[Cell];
        while (Top >= VoxelConstants::MinZ && States[FVoxelColumn::GetDenseIndex(Cell % Size, Cell / Size, Top)] == 0)
            --Top;
    }
}
