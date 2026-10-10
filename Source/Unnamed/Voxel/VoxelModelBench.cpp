#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Generation/VoxelArea.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Meshing/VoxelMesher.h"
#include "Random/VoxelRandom.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelConsole.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelConstants.h"

// voxel.BenchModels: the cost of model quads in the mesher (phase 6, S6; scout D's R11 and the cross fast-path gate).
// The decorated overworld of seed 263 (the goldens' seed) is meshed as generated, then with a plant on every grass
// block's top (a jungle's density or more), with models on and with voxel.Models 0. Each column's 24 sections are
// gathered and meshed on one thread, the three cases in turn, the order rotating from column to column so drift
// spreads over all three.
namespace
{
    using FBenchColumn = TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>;

    struct FModelBenchCase
    {
        const TCHAR* Name;
        double TotalMs = 0, WorstMs = 0;
        int64 Quads = 0, ModelQuads = 0, Sections = 0;
        double SectionMs = 0;
        SIZE_T Bytes = 0;
    };

    void MeshBenchColumn(const FVoxelBlockTable& Table, const IVoxelTerrain& Terrain, const TArray<FBenchColumn>& Set,
        int32 Side, int32 Index, const FVoxelMeshOptions& Options, FVoxelPaddedSection& Padded, FVoxelMeshData& Data,
        FModelBenchCase& Out)
    {
        FVoxelNeighborhood Neighborhood;
        const int32 X = Index % Side, Y = Index / Side;
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
                if (X + DX >= 0 && X + DX < Side && Y + DY >= 0 && Y + DY < Side)
                    Neighborhood.Set(DX, DY, Set[(Y + DY) * Side + X + DX].Get());
        const double Start = FPlatformTime::Seconds();
        for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
        {
            if (Set[Index]->GetSection(Section).IsEmpty()) continue;
            const double SectionStart = FPlatformTime::Seconds();
            VoxelMesher::GatherPadded(Neighborhood, Section, Padded);
            VoxelMesher::MeshSection(Table, Padded, Section, Options, Data, Terrain.GetTints(), &Neighborhood);
            if (Data.IsEmpty()) continue;
            Out.SectionMs += (FPlatformTime::Seconds() - SectionStart) * 1000.;
            ++Out.Sections;
            Out.Quads += Data.GetQuadCount();
            Out.ModelQuads += Data.Layers[int32(EVoxelLayer::Model)].GetQuadCount();
            // The bytes the mesh holds (the scratch's capacity is reused, so count what was written).
            for (const FVoxelMeshLayer& Layer : Data.Layers)
                Out.Bytes += Layer.Positions.Num() * sizeof(FVector3f) +
                    (Layer.BlockUVs.Num() + Layer.Codes.Num() + Layer.TintFaces.Num()) * sizeof(FVector2f) +
                    Layer.Colors.Num() * sizeof(FColor) + Layer.Indices.Num() * sizeof(uint32);
        }
        const double Ms = (FPlatformTime::Seconds() - Start) * 1000.;
        Out.TotalMs += Ms;
        Out.WorstMs = FMath::Max(Out.WorstMs, Ms);
    }

    void RunModelBench(const TArray<FString>& Args, FOutputDevice& Output)
    {
        const int32 Radius = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 8) : 4;
        const int32 Rounds = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 10) : 3;
        const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
        FString Error;
        FVoxelTerrainPreset Preset;
        FVoxelBiomeTable Biomes;
        FVoxelSurfaceInputs Surface;
        FVoxelFeatureGraph Graph;
        if (!VoxelTerrainFactory::ReadInputs(
                VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), Preset, Biomes, Surface, Error) ||
            !VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error))
        {
            Output.Log(TEXT("voxel.BenchModels: ") + Error);
            return;
        }
        const TSharedRef<FVoxelDensityTerrain> Terrain =
            MakeShared<FVoxelDensityTerrain>(Preset, 263, *Table, FVoxelDensityTerrainSettings(), &Biomes, &Surface);
        FVoxelDecorator Decorator;
        // Plants (S8) as voxel.Decoration.Plants says, as every bench does.
        FVoxelDecorationOptions DecorationOptions;
        DecorationOptions.bPlants = VoxelConsole::CVarDecorationPlants.GetValueOnGameThread();
        if (!Terrain->IsValid() ||
            !Decorator.Initialize(
                Graph, *Table, Terrain->GetBiomeSource()->GetTable(), Terrain->GetSeed(), DecorationOptions, &Error))
        {
            Output.Log(TEXT("voxel.BenchModels: ") + (Error.IsEmpty() ? Terrain->GetError() : Error));
            return;
        }
        FVoxelArea Area;
        FVoxelArea::Build(*Table, *Terrain, FIntPoint::ZeroValue, Radius, FVoxelMeshOptions(), true, Area, &Decorator);

        // The planted copies: a plant on every grass block whose top is open. Mostly short grass, some ferns, tall
        // grass (two cells) and flowers, picked by position.
        const int32 GrassBlock = Table->FindBlock(TEXT("grass_block"));
        const uint16 Plants[] = {Table->FindState(TEXT("short_grass")), Table->FindState(TEXT("fern")),
            Table->FindState(TEXT("dandelion")), Table->FindState(TEXT("poppy"))};
        const FFoundationBlockRegistry& Registry = *FFoundationBlockRegistry::GetDefault();
        const FFoundationBlockType* Tall = Registry.FindBlock(TEXT("tall_grass"));
        const uint16 TallLower = Tall ? uint16(Tall->DefaultStateId) : 0;
        const uint16 TallUpper = Tall ? Registry.WithValue(Tall->GetDefaultState(), TEXT("half"), TEXT("upper")).Id : 0;
        int64 Planted = 0;
        TArray<FBenchColumn> Generated = Area.Columns, Grown;
        for (const FBenchColumn& Column : Area.Columns)
        {
            TSharedRef<FVoxelColumn, ESPMode::ThreadSafe> Copy = MakeShared<FVoxelColumn, ESPMode::ThreadSafe>(*Column);
            const FIntPoint Origin = Column->GetPosition() * VoxelConstants::ColumnSize;
            for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
                for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                {
                    const int32 Top = Column->GetHeight(EVoxelHeightmap::WorldSurface, X, Y);
                    if (Top < VoxelConstants::MinZ || Top + 2 > VoxelConstants::MaxZ) continue;
                    if (Table->Get(Column->GetState(X, Y, Top)).Block != GrassBlock) continue;
                    if (Column->GetState(X, Y, Top + 1) || Column->GetState(X, Y, Top + 2)) continue;
                    const uint32 Pick =
                        uint32(VoxelRandom::Hash64(0, VoxelRandom::Salt("model bench"), Origin.X + X, Origin.Y + Y) >>
                            40) %
                        100;
                    if (Pick < 8 && TallLower)
                    {
                        Copy->SetState(X, Y, Top + 1, TallLower, *Table);
                        Copy->SetState(X, Y, Top + 2, TallUpper, *Table);
                    }
                    else
                        Copy->SetState(X, Y, Top + 1,
                            Plants[Pick < 80    ? 0
                                    : Pick < 90 ? 1
                                    : Pick < 95 ? 2
                                                : 3],
                            *Table);
                    ++Planted;
                }
            Grown.Add(Copy);
        }

        FVoxelMeshOptions On, Off;
        Off.bModels = false;
        FModelBenchCase Cases[3] = {
            {TEXT("generated")}, {TEXT("planted, models on")}, {TEXT("planted, voxel.Models 0")}};
        const TArray<FBenchColumn>* Sets[3] = {&Generated, &Grown, &Grown};
        const FVoxelMeshOptions* Options[3] = {&On, &On, &Off};
        TUniquePtr<FVoxelPaddedSection> Padded = MakeUnique<FVoxelPaddedSection>();
        FVoxelMeshData Data;
        const int32 Side = 2 * Radius + 1;
        for (int32 Round = 0; Round < Rounds; ++Round)
            for (int32 Index = 0; Index < Area.Columns.Num(); ++Index)
                for (int32 Turn = 0; Turn < 3; ++Turn)
                {
                    const int32 Case = (Index + Round + Turn) % 3;
                    MeshBenchColumn(
                        *Table, *Terrain, *Sets[Case], Side, Index, *Options[Case], *Padded, Data, Cases[Case]);
                }
        const double Runs = double(Rounds) * Area.Columns.Num();
        TArray<FString> Lines;
        Lines.Add(
            FString::Printf(TEXT("Model bench, overworld seed 263, decorated, %d columns x %d rounds, one thread; ")
                                TEXT("%lld plants (%.1f per column):"),
                Area.Columns.Num(), Rounds, Planted, Planted / double(Area.Columns.Num())));
        for (const FModelBenchCase& Case : Cases)
            Lines.Add(FString::Printf(TEXT("  %-24s mesh %.3f ms per column (worst %.3f), %.3f ms per meshed section; ")
                                          TEXT("%.1f quads (%.1f model) and %.1f KB of mesh per column"),
                Case.Name, Case.TotalMs / Runs, Case.WorstMs, Case.Sections ? Case.SectionMs / Case.Sections : 0.,
                Case.Quads / Runs, Case.ModelQuads / Runs, Case.Bytes / Runs / 1024.));
        const double Added = (Cases[1].TotalMs - Cases[2].TotalMs) / Runs;
        Lines.Add(
            FString::Printf(TEXT("  model quads cost %.3f ms per planted column (cross fast path gate: 0.2 ms): %s"),
                Added, Added > .2 ? TEXT("over") : TEXT("under")));
        for (const FString& Line : Lines)
            Output.Log(Line);
    }

    FAutoConsoleCommandWithArgsAndOutputDevice VoxelBenchModelsCommand(TEXT("voxel.BenchModels"),
        TEXT("voxel.BenchModels [Radius=4] [Rounds=3]: mesh time, quads and bytes per column for the generated world, ")
            TEXT("then with a plant on every grass block, models on and off (one thread)."),
        FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&RunModelBench));
}
