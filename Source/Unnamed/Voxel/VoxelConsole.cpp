#include "Voxel/VoxelConsole.h"
#include "Async/Fundamental/Scheduler.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Generation/VoxelArea.h"
#include "Meshing/VoxelOreLooks.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Random/VoxelSeed.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelClimateDebug.h"
#include "Voxel/VoxelFlyCamera.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelWorldSettings.h"
#include "Voxel/VoxelWorldSubsystem.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelBench.h"
#include "VoxelCarverBench.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

// Defaults match UVoxelWorldSettings; the settings object overwrites them at startup.
TAutoConsoleVariable<int32> VoxelConsole::CVarViewRadius(
    TEXT("voxel.ViewRadius"), 12, TEXT("Columns drawn around the camera, as a radius in 16-block columns."));
TAutoConsoleVariable<int32> VoxelConsole::CVarUnloadMargin(
    TEXT("voxel.UnloadMargin"), 2, TEXT("Extra columns beyond the view radius before a column unloads (hysteresis)."));
TAutoConsoleVariable<float> VoxelConsole::CVarApplyBudgetMs(
    TEXT("voxel.ApplyBudgetMs"), 2.f, TEXT("Game-thread milliseconds per frame for applying finished chunk work."));
TAutoConsoleVariable<int32> VoxelConsole::CVarMaxTasksInFlight(TEXT("voxel.MaxTasksInFlight"), 0,
    TEXT("Generation and meshing tasks allowed in flight; 0 means twice the task workers."));
TAutoConsoleVariable<bool> VoxelConsole::CVarAmbientOcclusion(
    TEXT("voxel.AmbientOcclusion"), true, TEXT("Per-vertex ambient occlusion in chunk meshes."));
TAutoConsoleVariable<bool> VoxelConsole::CVarGreedyMeshing(TEXT("voxel.GreedyMeshing"), true,
    TEXT("Merge coplanar faces into larger quads; off emits one quad per face, for debugging."));
TAutoConsoleVariable<int32> VoxelConsole::CVarTintBlend(TEXT("voxel.TintBlend"), 2,
    TEXT("Biome blend radius for grass, foliage and water tints, 0-8 (26.3's default 2); 0 gives each block its own "
         "biome's colour, -1 keeps every block's plains tint. A change remeshes every loaded column."));
TAutoConsoleVariable<bool> VoxelConsole::CVarEdgeWalls(TEXT("voxel.Debug.EdgeWalls"), false,
    TEXT("Debug: mesh the columns at the edge of the view against air, showing the cut through the world."));
TAutoConsoleVariable<bool> VoxelConsole::CVarDecoration(TEXT("voxel.Decoration"), true,
    TEXT(
        "Decoration (phase 6): 1 decorates streamed columns (Generate, Decorate, Finalize; three rings past the view), "
        "voxel.Bench Columns and the gates (ores since S2); 0 keeps them as Generate leaves them, the control for "
        "comparisons. A change rebuilds the world."));
TAutoConsoleVariable<bool> VoxelConsole::CVarDecorationFreezeLast(TEXT("voxel.Decoration.FreezeLast"), true,
    TEXT("With decoration: 1 runs the freeze pass at Finalize, after the merge, so snow lands on decoration (S3); 0 "
         "runs "
         "it inside the snapshot, before decoration (S2's order, a control). A change rebuilds the world."));
TAutoConsoleVariable<bool> VoxelConsole::CVarDecorationPlants(TEXT("voxel.Decoration.Plants"), true,
    TEXT("With decoration: 1 runs the vegetation features (phase 6, S8: plants, patches, disks) in streamed worlds and "
         "every bench; 0 runs the ores alone, the control that reproduces the goldens and hashes from before S8. A "
         "change rebuilds the world."));
TAutoConsoleVariable<bool> VoxelConsole::CVarDecorationTrees(TEXT("voxel.Decoration.Trees"), true,
    TEXT(
        "With plants: 1 runs the tree feature (phase 6, S9a: trunks, foliage, mangrove roots; S9b's decorators, fallen "
        "trees and huge mushrooms not yet) in streamed worlds and every bench; 0 skips trees, the control that "
        "reproduces S8's goldens and hashes. A change rebuilds the world."));
TAutoConsoleVariable<bool> VoxelConsole::CVarOreVeins(TEXT("voxel.OreVeins"), true,
    TEXT(
        "Ore veins (phase 6, S4): 1 runs 26.3's ore_vein rules in Generate (copper veins in granite from Z 0 to 49, "
        "iron veins in tuff from Z -60 to -9) in streamed worlds and every bench (each takes noveins as well); 0 gives "
        "the blocks from before S4, the control. A change rebuilds the world."));
TAutoConsoleVariable<int32> VoxelConsole::CVarOreHost(TEXT("voxel.Look.OreHost"), 0,
    TEXT("Ore look L1, render-only: 0 vanilla (stone ores on grey stone everywhere); 1 a stone or deepslate ore in "
         "granite, diorite, andesite or tuff draws its ore pixels over that rock (the host most common among its "
         "neighbours); 2 the same with the base rock's tones mapped onto the host's (palette swap). A change remeshes "
         "every loaded column."));
TAutoConsoleVariable<int32> VoxelConsole::CVarOreGlint(TEXT("voxel.Look.OreGlint"), 0,
    TEXT("Ore look L2 level 1, render-only: 0 vanilla; 1 ore textures keep their ore pixels' share and colour in the "
         "mips, and metal and gem pixels are glossy (light catches them; no light, no glint); 3 debug, every ore face "
         "flat magenta. A change remeshes every loaded column."));

// The carvers (phase 6, S10), off until S11 turns them on with the re-records.
TAutoConsoleVariable<bool> VoxelConsole::CVarCarvers(TEXT("voxel.Carvers"), false,
    TEXT("Carvers (phase 6, S10): 1 cuts 26.3's three carvers (cave, cave_extra_underground, canyon) into Generate, "
         "between the surface rules and the freeze, their cells filled by the aquifer, in worlds made after the change "
         "(voxel.Regenerate rebuilds one) and the Columns and Density benches (each takes carvers and nocarvers as "
         "well); 0, the default, gives the blocks from before S10."));

int32 VoxelConsole::GetTintBlend()
{
    return FMath::Clamp(CVarTintBlend.GetValueOnGameThread(), -1, FVoxelMeshOptions::MaxTintBlend);
}

int32 VoxelConsole::GetOreHost()
{
    const int32 Value = CVarOreHost.GetValueOnAnyThread();
    return Value == 1 || Value == 2 ? Value : 0;
}

int32 VoxelConsole::GetOreGlint()
{
    const int32 Value = CVarOreGlint.GetValueOnAnyThread();
    return Value == 1 || Value == VoxelOreLooks::DebugMask ? Value : 0;
}

int32 VoxelConsole::GetMaxTasksInFlight()
{
    const int32 Configured = CVarMaxTasksInFlight.GetValueOnAnyThread();
    return Configured > 0 ? Configured : FMath::Max(2, 2 * int32(LowLevelTasks::FScheduler::Get().GetNumWorkers()));
}

FString VoxelConsole::DescribeInfo(const UWorld* World)
{
    const UVoxelWorldSettings& Settings = UVoxelWorldSettings::Get();
    FString Out = FString::Printf(TEXT("Voxel world: Z %d..%d in %d sections of %d, sea level %d, columns %d x %d\n"),
        VoxelConstants::MinZ, VoxelConstants::MaxZ, VoxelConstants::SectionCount, VoxelConstants::SectionSize,
        VoxelConstants::SeaLevel, VoxelConstants::ColumnSize, VoxelConstants::ColumnSize);
    Out += FString::Printf(TEXT("  Dev seed: \"%s\" -> %s (used when a world has none)\n"), *Settings.DevSeed,
        *FVoxelSeed::Parse(Settings.DevSeed).Describe());

    // Every voxel.* variable, sorted, so new ones show up here without touching this function.
    TArray<TPair<FString, IConsoleVariable*>> Variables;
    IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
        FConsoleObjectVisitor::CreateLambda(
            [&Variables](const TCHAR* Name, IConsoleObject* Object)
            {
                if (IConsoleVariable* Variable = Object->AsVariable()) Variables.Emplace(Name, Variable);
            }),
        TEXT("voxel."));
    Variables.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });
    for (const TPair<FString, IConsoleVariable*>& Variable : Variables)
        Out += FString::Printf(TEXT("  %s = %s (set by %s)\n"), *Variable.Key, *Variable.Value->GetString(),
            GetConsoleVariableSetByName(Variable.Value->GetFlags()));

    // The rings around a camera at the origin (Streaming/VoxelStreamingPlanner.h): with decoration, three past the
    // view.
    const FVoxelStreamingRings Rings(FIntPoint::ZeroValue, FMath::Clamp(CVarViewRadius.GetValueOnAnyThread(), 1, 64),
        CVarUnloadMargin.GetValueOnAnyThread(),
        CVarDecoration.GetValueOnAnyThread() ? FVoxelStreamingRings::MaxStages : 1);
    TArray<FIntPoint> Needed;
    VoxelStreaming::GetNeededColumns(Rings, 0, Needed);
    int32 PerRing[FVoxelStreamingRings::MaxStages + 1] = {};
    for (const FIntPoint Column : Needed)
        ++PerRing[Rings.GetRing(Column)];
    Out += FString::Printf(TEXT("  View radius %d: %d columns in view, %d generated; unload beyond %d columns\n"),
        Rings.ViewRadius, PerRing[0], Needed.Num(), Rings.GetUnloadDistance());
    if (Rings.Stages > 1)
        Out += FString::Printf(TEXT("  Decoration's rings: %d in view, %d final, %d decorated, %d generated\n"),
            PerRing[0], PerRing[0] + PerRing[1], PerRing[0] + PerRing[1] + PerRing[2], Needed.Num());
    Out += FString::Printf(TEXT("  Task workers %u, tasks in flight %d\n"),
        LowLevelTasks::FScheduler::Get().GetNumWorkers(), GetMaxTasksInFlight());
    Out += FString::Printf(TEXT("  Mesh backend: RealtimeMeshComponent (%s)"),
        FModuleManager::Get().IsModuleLoaded(TEXT("RealtimeMeshComponent")) ? TEXT("loaded") : TEXT("not loaded"));
    if (const UVoxelWorldSubsystem* Subsystem = UVoxelWorldSubsystem::Get(World))
        Out += TEXT("\n") + Subsystem->DescribeStages();
    if (const AVoxelWorld* Voxels = AVoxelWorld::Find(World))
        Out += FString::Printf(TEXT("\n  World clock: %s | %lld game ticks dropped by the per-frame cap"),
            *Voxels->GetClock().Describe(Voxels->IsTimeAdvancing()), Voxels->GetClock().GetDroppedTicks());
    return Out;
}

namespace
{
    // The decoration data read from the reference and compiled for a bench terrain's biomes.
    TSharedPtr<FVoxelDecorator> MakeBenchDecorator(const FVoxelDensityTerrain& Terrain, const FVoxelBlockTable& Table,
        const FVoxelDecorationOptions& Options, FString& OutError, const bool* bFreezeLast = nullptr)
    {
        FVoxelFeatureGraph Graph;
        if (!Terrain.GetBiomeSource())
        {
            OutError = TEXT("decoration needs a terrain with biomes");
            return nullptr;
        }
        if (!VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, OutError)) return nullptr;
        TSharedPtr<FVoxelDecorator> Decorator = MakeShared<FVoxelDecorator>();
        // Every bench and gate puts the freeze where voxel.Decoration.FreezeLast says, unless told.
        FVoxelDecorationOptions Placed = Options;
        Placed.bFreezeLast = bFreezeLast ? *bFreezeLast : VoxelConsole::CVarDecorationFreezeLast.GetValueOnGameThread();
        // And runs the plants as voxel.Decoration.Plants says (a caller may still turn them off).
        Placed.bPlants = Options.bPlants && VoxelConsole::CVarDecorationPlants.GetValueOnGameThread();
        Placed.bTrees = Options.bTrees && VoxelConsole::CVarDecorationTrees.GetValueOnGameThread();
        if (!Decorator->Initialize(
                Graph, Table, Terrain.GetBiomeSource()->GetTable(), Terrain.GetSeed(), Placed, &OutError))
            return nullptr;
        return Decorator;
    }

    // Bench terrains run the ore veins as voxel.OreVeins says, unless the command says noveins.
    bool BenchOreVeins(const TArray<FString>& Args)
    {
        return VoxelConsole::CVarOreVeins.GetValueOnGameThread() && !Args.Contains(TEXT("noveins"));
    }

    // Bench terrains carve as voxel.Carvers says (S10), or as the command's carvers or nocarvers says; carving needs
    // the decoration data, read here.
    bool BenchCarvers(FVoxelDensityTerrainSettings& Settings, const TArray<FString>& Args, FString& OutError)
    {
        Settings.bCarvers = !Args.Contains(TEXT("nocarvers")) &&
            (Args.Contains(TEXT("carvers")) || VoxelConsole::CVarCarvers.GetValueOnGameThread());
        if (!Settings.bCarvers) return true;
        const TSharedRef<FVoxelFeatureGraph> Features = MakeShared<FVoxelFeatureGraph>();
        if (!VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), *Features, OutError)) return false;
        Settings.Features = Features;
        return true;
    }

    // Each line to the console that ran the command and to the log, so headless runs keep the output.
    void PrintLines(FOutputDevice& Output, const FString& Text)
    {
        TArray<FString> Lines;
        Text.ParseIntoArrayLines(Lines);
        for (const FString& Line : Lines)
        {
            Output.Log(Line);
            UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Line);
        }
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice VoxelInfoCommand(TEXT("voxel.Info"),
        TEXT("Prints the voxel world's settings, console variables and limits, and, in a voxel world, its streaming "
             "stages: rings, per-stage times and counts, snapshots and buckets held, regenerations."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>&, UWorld* World, FOutputDevice& Output)
            { PrintLines(Output, VoxelConsole::DescribeInfo(World)); }));

    FAutoConsoleCommandWithWorldArgsAndOutputDevice VoxelSeedCommand(TEXT("voxel.Seed"),
        TEXT("voxel.Seed [text]: shows the 64-bit world seed a seed text makes (blank: random; whole numbers as "
             "typed; other text hashed) and, in a voxel world, rebuilds it with that seed for the session. Without "
             "text, the current seed."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
            {
                AVoxelWorld* Voxels = AVoxelWorld::Find(World);
                if (Args.IsEmpty())
                {
                    const FString Dev = UVoxelWorldSettings::Get().DevSeed;
                    PrintLines(Output,
                        Voxels
                            ? TEXT("World seed ") + Voxels->GetSeed().Describe()
                            : FString::Printf(TEXT("Dev seed \"%s\" -> %s"), *Dev, *FVoxelSeed::Parse(Dev).Describe()));
                    return;
                }
                const FString Text = FString::Join(Args, TEXT(" "));
                const FVoxelSeed Seed = FVoxelSeed::Parse(Text);
                PrintLines(Output,
                    FString::Printf(TEXT("Seed \"%s\" -> %s%s"), *Text, *Seed.Describe(),
                        Voxels ? TEXT(": rebuilding the world") : TEXT("")));
                if (Voxels) Voxels->Regenerate(Seed);
            }));

    FAutoConsoleCommandWithWorldAndArgs VoxelRegenerateCommand(TEXT("voxel.Regenerate"),
        TEXT("Rebuilds the voxel world with its seed, picking up voxel.GreedyMeshing and voxel.AmbientOcclusion."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
            [](const TArray<FString>&, UWorld* World)
            {
                if (AVoxelWorld* Voxels = AVoxelWorld::Find(World)) Voxels->Regenerate(Voxels->GetSeed());
            }));

    FAutoConsoleCommandWithWorldAndArgs VoxelFlyCommand(TEXT("voxel.Fly"),
        TEXT("voxel.Fly [blocks per second]: toggles a free camera detached from the player, who holds still; the "
             "streaming follows the camera. W, A, S, D fly along the view, Space and Left Ctrl rise and sink, Left "
             "Shift goes four times as fast. Default 40 blocks per second; a speed while flying changes it."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
            [](const TArray<FString>& Args, UWorld* World)
            {
                const double Speed = Args.IsEmpty() ? 0. : FMath::Clamp(FCString::Atod(*Args[0]), 0., 10000.);
                const AVoxelFlyCamera* Fly = AVoxelFlyCamera::Toggle(World, Speed);
                UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"),
                    Fly ? *FString::Printf(TEXT("voxel.Fly: flying at %.0f blocks per second"), Fly->BlocksPerSecond)
                        : TEXT("voxel.Fly: back to the player"));
            }));

    FAutoConsoleCommand VoxelBenchCommand(TEXT("voxel.Bench"),
        TEXT("voxel.Bench Noise [millions]: times the deterministic core's primitives on this thread (default 1 "
             "million samples each), then 3D fBm across every task worker.\n"
             "voxel.Bench Columns [radius] [preset|standin] [nocaves]: generates and meshes (2 radius + 1)^2 columns "
             "off "
             "screen (default radius 4, preset overworld with caves), on this thread and then on every worker, reports "
             "each stage, and hashes the columns' blocks in two load orders.\n"
             "voxel.Bench Climate [radius] [preset]: times the terrain climate pass of (2 radius + 1)^2 columns "
             "(default radius 8, preset overworld), on this thread and then on every worker.\n"
             "voxel.Bench Density [radius] [preset] [nocaves]: generates (2 radius + 1)^2 columns of the density "
             "terrain "
             "(default radius 4, preset overworld) with height-bound skipping on and off, and reports each stage.\n"
             "voxel.Bench Carvers [radius] [preset]: generates (2 radius + 1)^2 columns (default radius 4, preset "
             "overworld) with the carvers off and on, alternating, on this thread; reports the time they add, their "
             "split and counts, and the blocks that differ with the path cache off (S10's gates)."),
        FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>& Args, FOutputDevice& Output)
            {
                const FVoxelSeed Seed = FVoxelSeed::Parse(UVoxelWorldSettings::Get().DevSeed);
                if (!Args.IsEmpty() && Args[0] == TEXT("Noise"))
                {
                    const double Millions = Args.Num() > 1 ? FMath::Clamp(FCString::Atod(*Args[1]), .01, 50.) : 1.;
                    const TArray<FVoxelBenchResult> Results = VoxelBench::RunNoise(int32(Millions * 1e6), Seed.Value);
                    PrintLines(Output,
                        FString::Printf(TEXT("Voxel bench, %d samples per primitive, seed %s:"),
                            int32(Results[0].Samples), *Seed.Describe()) +
                            TEXT("\n") + VoxelBench::Describe(Results));
                    return;
                }
                if (!Args.IsEmpty() && Args[0] == TEXT("Columns"))
                {
                    const int32 Radius = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 0, 16) : 4;
                    const FString PresetName = Args.Num() > 2 ? Args[2] : TEXT("overworld");
                    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
                    FVoxelMeshOptions Options;
                    Options.bGreedy = VoxelConsole::CVarGreedyMeshing.GetValueOnGameThread();
                    Options.bAmbientOcclusion = VoxelConsole::CVarAmbientOcclusion.GetValueOnGameThread();
                    Options.TintBlend = VoxelConsole::GetTintBlend();
                    Options.OreHost = VoxelConsole::GetOreHost();
                    Options.OreGlint = VoxelConsole::GetOreGlint();
                    // "standin" benches generator 1's terrain; anything else names a preset of generator 2's.
                    if (PresetName == TEXT("standin"))
                    {
                        const FVoxelHeightTerrain Terrain(Seed.Value, *Table);
                        PrintLines(Output,
                            FString::Printf(TEXT("Voxel column bench, stand-in terrain, seed %s:"), *Seed.Describe()) +
                                TEXT("\n") + VoxelBench::RunColumns(*Table, Terrain, nullptr, Options, Radius));
                        return;
                    }
                    FString Error;
                    FVoxelTerrainPreset Preset;
                    FVoxelBiomeTable Biomes;
                    FVoxelSurfaceInputs Surface;
                    if (!VoxelClimateDebug::GetSampler(PresetName, Seed.Value, Error) ||
                        !VoxelTerrainFactory::ReadInputs(
                            VoxelWorldgen::GetReferenceRoot(), PresetName, Preset, Biomes, Surface, Error))
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    FVoxelDensityTerrainSettings Settings;
                    Settings.bCaves = !(Args.Num() > 3 && Args[3] == TEXT("nocaves"));
                    Settings.bOreVeins = BenchOreVeins(Args);
                    if (!BenchCarvers(Settings, Args, Error))
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    const FVoxelDensityTerrain Terrain(Preset, uint64(Seed.Value), *Table, Settings, &Biomes, &Surface);
                    if (!Terrain.IsValid())
                    {
                        PrintLines(Output, Terrain.GetError());
                        return;
                    }
                    if (Settings.bCarvers)
                        PrintLines(Output,
                            Terrain.GetCarvers().IsInitialized()
                                ? FString(TEXT("Carvers on (S10)"))
                                : TEXT("Carvers asked for but off: ") + Terrain.GetCarverError());
                    TSharedPtr<FVoxelDecorator> Decorator;
                    if (VoxelConsole::CVarDecoration.GetValueOnGameThread())
                    {
                        Decorator = MakeBenchDecorator(Terrain, *Table, {}, Error);
                        if (!Decorator)
                        {
                            PrintLines(Output, Error);
                            return;
                        }
                    }
                    PrintLines(Output,
                        FString::Printf(TEXT("Voxel column bench, %s, caves %s, ore veins %s, decoration %s, seed %s:"),
                            *PresetName, Settings.bCaves ? TEXT("on") : TEXT("off"),
                            Settings.bOreVeins ? TEXT("on") : TEXT("off"), Decorator ? TEXT("on") : TEXT("off"),
                            *Seed.Describe()) +
                            TEXT("\n") +
                            VoxelBench::RunColumns(*Table, Terrain, &Terrain, Options, Radius, Decorator.Get()));
                    return;
                }
                if (!Args.IsEmpty() && Args[0] == TEXT("Carvers"))
                {
                    // The carvers' bench (S10): the same terrain carves or not per call, so both sides share a run. It
                    // keeps every carved column for the cache-off comparison, so the radius stops at 8 (57 MB).
                    const int32 Radius = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 0, 8) : 4;
                    const FString PresetName = Args.Num() > 2 ? Args[2] : TEXT("overworld");
                    FString Error;
                    FVoxelTerrainPreset Preset;
                    FVoxelBiomeTable Biomes;
                    FVoxelSurfaceInputs Surface;
                    const TSharedRef<FVoxelFeatureGraph> Features = MakeShared<FVoxelFeatureGraph>();
                    if (!VoxelClimateDebug::GetSampler(PresetName, Seed.Value, Error) ||
                        !VoxelTerrainFactory::ReadInputs(
                            VoxelWorldgen::GetReferenceRoot(), PresetName, Preset, Biomes, Surface, Error) ||
                        !VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), *Features, Error))
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
                    FVoxelDensityTerrainSettings Settings;
                    Settings.bOreVeins = BenchOreVeins(Args);
                    Settings.Features = Features;
                    const FVoxelDensityTerrain Terrain(Preset, uint64(Seed.Value), *Table, Settings, &Biomes, &Surface);
                    if (!Terrain.IsValid() || !Terrain.GetCarvers().IsInitialized())
                    {
                        PrintLines(Output, Terrain.IsValid() ? Terrain.GetCarverError() : Terrain.GetError());
                        return;
                    }
                    PrintLines(Output,
                        FString::Printf(TEXT("Voxel carver bench, %s, ore veins %s, seed %s:"), *PresetName,
                            Settings.bOreVeins ? TEXT("on") : TEXT("off"), *Seed.Describe()) +
                            TEXT("\n") + VoxelBench::RunCarvers(Terrain, Radius));
                    return;
                }
                if (!Args.IsEmpty() && Args[0] == TEXT("Climate"))
                {
                    const int32 Radius = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 0, 64) : 8;
                    const FString Preset = Args.Num() > 2 ? Args[2] : TEXT("overworld");
                    FString Error;
                    const TSharedPtr<const FVoxelClimateSampler> Sampler =
                        VoxelClimateDebug::GetSampler(Preset, Seed.Value, Error);
                    if (!Sampler)
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    PrintLines(Output,
                        FString::Printf(
                            TEXT("Voxel climate bench, %s, %d columns, seed %s (ns per sample = per column):"), *Preset,
                            (2 * Radius + 1) * (2 * Radius + 1), *Seed.Describe()) +
                            TEXT("\n") + VoxelBench::Describe(VoxelBench::RunClimate(*Sampler, Radius)));
                    return;
                }
                if (!Args.IsEmpty() && Args[0] == TEXT("Density"))
                {
                    const int32 Radius = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 0, 32) : 4;
                    const FString PresetName = Args.Num() > 2 ? Args[2] : TEXT("overworld");
                    FString Error;
                    // The sampler's check keeps the name to the reference's three presets.
                    FVoxelTerrainPreset Preset;
                    FVoxelBiomeTable Biomes;
                    FVoxelSurfaceInputs Surface;
                    if (!VoxelClimateDebug::GetSampler(PresetName, Seed.Value, Error) ||
                        !VoxelTerrainFactory::ReadInputs(
                            VoxelWorldgen::GetReferenceRoot(), PresetName, Preset, Biomes, Surface, Error))
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
                    // A fourth argument "nocaves" benches the terrain without caves and aquifers (session 3's).
                    FVoxelDensityTerrainSettings Settings;
                    Settings.bCaves = !(Args.Num() > 3 && Args[3] == TEXT("nocaves"));
                    Settings.bOreVeins = BenchOreVeins(Args);
                    if (!BenchCarvers(Settings, Args, Error))
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    const FVoxelDensityTerrain Terrain(Preset, uint64(Seed.Value), *Table, Settings, &Biomes, &Surface);
                    if (!Terrain.IsValid())
                    {
                        PrintLines(Output, Terrain.GetError());
                        return;
                    }
                    if (Settings.bCarvers)
                        PrintLines(Output,
                            Terrain.GetCarvers().IsInitialized()
                                ? FString(TEXT("Carvers on (S10)"))
                                : TEXT("Carvers asked for but off: ") + Terrain.GetCarverError());
                    PrintLines(Output,
                        FString::Printf(TEXT("Voxel density bench, %s, caves %s, ore veins %s, seed %s:"), *PresetName,
                            Settings.bCaves ? TEXT("on") : TEXT("off"), Settings.bOreVeins ? TEXT("on") : TEXT("off"),
                            *Seed.Describe()) +
                            TEXT("\n") + VoxelBench::RunDensity(Terrain, Radius));
                    return;
                }
                if (!Args.IsEmpty() && Args[0] == TEXT("Decorate"))
                {
                    if (Args.Num() > 1 && !FCString::IsNumeric(*Args[1]))
                    {
                        PrintLines(
                            Output, TEXT("Usage: voxel.Bench Decorate [radius] [preset] [nocaves] [noveins] [at=X:Y]"));
                        return;
                    }
                    const int32 Radius = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 0, 16) : 4;
                    const FString PresetName =
                        Args.Num() > 2 && !Args[2].StartsWith(TEXT("at=")) ? Args[2] : TEXT("overworld");
                    // at=X:Y (blocks; a colon, since -ExecCmds splits at commas): the area's centre column instead of
                    // the origin's (S9e: one biome's columns).
                    FIntPoint Center = FIntPoint::ZeroValue;
                    for (const FString& Arg : Args)
                    {
                        FString X, Y;
                        if (Arg.StartsWith(TEXT("at=")) && Arg.Mid(3).Split(TEXT(":"), &X, &Y))
                            Center =
                                FIntPoint(FMath::DivideAndRoundDown(FCString::Atoi(*X), VoxelConstants::ColumnSize),
                                    FMath::DivideAndRoundDown(FCString::Atoi(*Y), VoxelConstants::ColumnSize));
                    }
                    FString Error;
                    FVoxelTerrainPreset Preset;
                    FVoxelBiomeTable Biomes;
                    FVoxelSurfaceInputs Surface;
                    if (!VoxelClimateDebug::GetSampler(PresetName, Seed.Value, Error) ||
                        !VoxelTerrainFactory::ReadInputs(
                            VoxelWorldgen::GetReferenceRoot(), PresetName, Preset, Biomes, Surface, Error))
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
                    FVoxelDensityTerrainSettings Settings;
                    Settings.bCaves = !(Args.Num() > 3 && Args[3] == TEXT("nocaves"));
                    Settings.bOreVeins = BenchOreVeins(Args);
                    const FVoxelDensityTerrain Terrain(Preset, uint64(Seed.Value), *Table, Settings, &Biomes, &Surface);
                    FVoxelDecorationOptions NoEarlyOut;
                    NoEarlyOut.bEarlyOut = false;
                    const TSharedPtr<FVoxelDecorator> Decorator =
                        Terrain.IsValid() ? MakeBenchDecorator(Terrain, *Table, {}, Error) : nullptr;
                    const TSharedPtr<FVoxelDecorator> Exhaustive =
                        Decorator ? MakeBenchDecorator(Terrain, *Table, NoEarlyOut, Error) : nullptr;
                    if (!Terrain.IsValid() || !Exhaustive)
                    {
                        PrintLines(Output, Terrain.IsValid() ? Error : Terrain.GetError());
                        return;
                    }
                    PrintLines(Output,
                        FString::Printf(TEXT("Voxel decoration bench, %s, caves %s, ore veins %s, seed %s:"),
                            *PresetName, Settings.bCaves ? TEXT("on") : TEXT("off"),
                            Settings.bOreVeins ? TEXT("on") : TEXT("off"), *Seed.Describe()) +
                            TEXT("\n") + VoxelBench::RunDecorate(*Decorator, *Exhaustive, Terrain, Radius, Center));
                    return;
                }
                if (!Args.IsEmpty() && Args[0] == TEXT("VeinProbe"))
                {
                    // The probe's three seeds, then Extra more (1, 2, ...) for our spread.
                    const int32 Extra = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 0, 200) : 0;
                    TArray<int64> Seeds = {0, 263, -4000000007ll};
                    for (int32 Index = 1; Index <= Extra; ++Index)
                        Seeds.Add(Index);
                    FString Error;
                    FVoxelTerrainPreset Preset;
                    FVoxelBiomeTable Biomes;
                    FVoxelSurfaceInputs Surface;
                    if (!VoxelTerrainFactory::ReadInputs(
                            VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), Preset, Biomes, Surface, Error))
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
                    TArray<TUniquePtr<FVoxelSurfaceEvaluator>> Rules;
                    TArray<const FVoxelOreVeins*> Veins;
                    for (const int64 VeinSeed : Seeds)
                    {
                        Rules.Add(MakeUnique<FVoxelSurfaceEvaluator>());
                        if (!Rules.Last()->Compile(Surface, *Table, Biomes, uint64(VeinSeed), &Error))
                        {
                            PrintLines(Output, Error);
                            return;
                        }
                        Veins.Add(&Rules.Last()->GetOreVeins());
                    }
                    const FVoxelDensityGraph& Graph = Veins[0]->GetEvaluator().GetGraph();
                    PrintLines(Output,
                        TEXT("Voxel ore vein probe (facts/ore_vein_samples.json's, on our veins):\n") +
                            VoxelBench::RunVeinProbe(Veins, Seeds,
                                Graph.FindFunction(TEXT("minecraft:overworld/ore_vein/toggle")),
                                Graph.FindFunction(TEXT("minecraft:overworld/ore_vein/mask"))));
                    return;
                }
                if (!Args.IsEmpty() && Args[0] == TEXT("FreezeDiff"))
                {
                    // noveins may come anywhere; the others are positional.
                    TArray<FString> Positional = Args;
                    Positional.Remove(TEXT("noveins"));
                    if ((Positional.Num() > 1 && !FCString::IsNumeric(*Positional[1])) || Positional.Num() == 4)
                    {
                        PrintLines(Output, TEXT("Usage: voxel.Bench FreezeDiff [radius] [preset] [x y] [noveins]"));
                        return;
                    }
                    const int32 Radius = Positional.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Positional[1]), 0, 16) : 4;
                    const FString PresetName = Positional.Num() > 2 ? Positional[2] : TEXT("overworld");
                    const TOptional<FIntPoint> Center = Positional.Num() > 4
                        ? TOptional<FIntPoint>(
                              FIntPoint(FCString::Atoi(*Positional[3]), FCString::Atoi(*Positional[4])))
                        : TOptional<FIntPoint>();
                    FString Error;
                    FVoxelTerrainPreset Preset;
                    FVoxelBiomeTable Biomes;
                    FVoxelSurfaceInputs Surface;
                    if (!VoxelClimateDebug::GetSampler(PresetName, Seed.Value, Error) ||
                        !VoxelTerrainFactory::ReadInputs(
                            VoxelWorldgen::GetReferenceRoot(), PresetName, Preset, Biomes, Surface, Error))
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
                    FVoxelDensityTerrainSettings Settings;
                    Settings.bOreVeins = BenchOreVeins(Args);
                    const FVoxelDensityTerrain Terrain(Preset, uint64(Seed.Value), *Table, Settings, &Biomes, &Surface);
                    const bool bLast = true, bFirst = false;
                    const TSharedPtr<FVoxelDecorator> Last =
                        Terrain.IsValid() ? MakeBenchDecorator(Terrain, *Table, {}, Error, &bLast) : nullptr;
                    const TSharedPtr<FVoxelDecorator> First =
                        Last ? MakeBenchDecorator(Terrain, *Table, {}, Error, &bFirst) : nullptr;
                    if (!First)
                    {
                        PrintLines(Output, Terrain.IsValid() ? Error : Terrain.GetError());
                        return;
                    }
                    PrintLines(Output,
                        FString::Printf(TEXT("Voxel freeze bench, %s, seed %s:"), *PresetName, *Seed.Describe()) +
                            TEXT("\n") + VoxelBench::RunFreezeDiff(*Last, *First, Terrain, Radius, Center));
                    return;
                }
                PrintLines(Output,
                    TEXT("Usage: voxel.Bench Noise [millions] | voxel.Bench Columns [radius] [preset|standin] "
                         "[nocaves] [noveins] | voxel.Bench Climate [radius] [preset] | voxel.Bench Density [radius] "
                         "[preset] [nocaves] [noveins] | voxel.Bench Decorate [radius] [preset] [nocaves] [noveins] "
                         "[at=X:Y] | "
                         "voxel.Bench FreezeDiff [radius] [preset] [x y] [noveins]"));
            }));

    FAutoConsoleCommand VoxelOreHistogramCommand(TEXT("voxel.OreHistogram"),
        TEXT("voxel.OreHistogram [radius] [preset] [nocaves] [nodiscard] [noveins]: decorates (2 radius + 1)^2 columns "
             "of the dev seed (default radius 8, at most 16; preset overworld) and writes the blocks the ore features "
             "placed, per Z and per state (final minus generated), and the blocks the ore veins placed, on their own "
             "(\"vein \" columns: generated with veins minus without), to "
             "<project>/Saved/Voxel/OreHistogram-<preset>-r<radius>[-nocaves][-nodiscard][-noveins].csv "
             "(Scripts/OreHistogram.py draws it)."),
        FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>& Args, FOutputDevice& Output)
            {
                const FVoxelSeed Seed = FVoxelSeed::Parse(UVoxelWorldSettings::Get().DevSeed);
                // The flags may come anywhere; the radius and the preset are the others, in that order.
                TArray<FString> Positional;
                for (const FString& Arg : Args)
                    if (Arg != TEXT("nocaves") && Arg != TEXT("nodiscard") && Arg != TEXT("noveins"))
                        Positional.Add(Arg);
                if (!Positional.IsEmpty() && !FCString::IsNumeric(*Positional[0]))
                {
                    PrintLines(
                        Output, TEXT("Usage: voxel.OreHistogram [radius] [preset] [nocaves] [nodiscard] [noveins]"));
                    return;
                }
                const int32 Radius = !Positional.IsEmpty() ? FMath::Clamp(FCString::Atoi(*Positional[0]), 0, 16) : 8;
                const FString PresetName = Positional.Num() > 1 ? Positional[1] : TEXT("overworld");
                const bool bCaves = !Args.Contains(TEXT("nocaves")), bDiscard = !Args.Contains(TEXT("nodiscard"));
                FString Error;
                FVoxelTerrainPreset Preset;
                FVoxelBiomeTable Biomes;
                FVoxelSurfaceInputs Surface;
                if (!VoxelClimateDebug::GetSampler(PresetName, Seed.Value, Error) ||
                    !VoxelTerrainFactory::ReadInputs(
                        VoxelWorldgen::GetReferenceRoot(), PresetName, Preset, Biomes, Surface, Error))
                {
                    PrintLines(Output, Error);
                    return;
                }
                const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
                FVoxelDensityTerrainSettings Settings;
                Settings.bCaves = bCaves;
                Settings.bOreVeins = BenchOreVeins(Args);
                const FVoxelDensityTerrain Terrain(Preset, uint64(Seed.Value), *Table, Settings, &Biomes, &Surface);
                FVoxelDecorationOptions Options;
                Options.bDiscard = bDiscard;
                const TSharedPtr<FVoxelDecorator> Decorator =
                    Terrain.IsValid() ? MakeBenchDecorator(Terrain, *Table, Options, Error) : nullptr;
                if (!Decorator)
                {
                    PrintLines(Output, Terrain.IsValid() ? Error : Terrain.GetError());
                    return;
                }
                FVoxelDecoratedArea Area;
                FVoxelDecoratedArea::Build(*Decorator, Terrain, FIntPoint::ZeroValue, Radius, true, false, Area);
                // Placed blocks: each final column against its snapshot, per state and Z.
                TArray<TPair<uint16, FString>> States = Decorator->GetOreStates();
                TArray<int64> Counts;
                Counts.SetNumZeroed(States.Num() * VoxelConstants::Height);
                TMap<uint16, int32> StateIndex;
                for (int32 Index = 0; Index < States.Num(); ++Index)
                    StateIndex.Add(States[Index].Key, Index);
                // The veins' blocks, after the features' columns: each generated column against itself without veins.
                TMap<uint16, int32> VeinIndex;
                if (Settings.bOreVeins)
                    for (const FVoxelSurfaceRule& Rule : Surface.Rules.Rules)
                    {
                        if (Rule.Op != EVoxelSurfaceRuleOp::OreVein) continue;
                        for (const int32 Id : {Rule.Block, Rule.RawOreBlock, Rule.FillerBlock})
                        {
                            const FString& Name = Surface.Rules.Strings[Id];
                            const uint16 State = Table->FindState(FName(Name.RightChop(Name.Find(TEXT(":")) + 1)));
                            if (State == 0 || VeinIndex.Contains(State)) continue;
                            VeinIndex.Add(State, States.Num());
                            States.Add({State, TEXT("vein ") + Name});
                        }
                    }
                Counts.SetNumZeroed(States.Num() * VoxelConstants::Height);
                FVoxelDensityTerrainOverrides NoVeins;
                NoVeins.bOreVeins = false;
                TArray<uint16> Dense;
                for (const TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>& Final : Area.Columns)
                {
                    FVoxelColumn Generated;
                    Terrain.Generate(Final->GetPosition(), Generated, Dense);
                    if (!VeinIndex.IsEmpty())
                    {
                        FVoxelColumn Plain;
                        Terrain.Generate(Final->GetPosition(), Plain, Dense, nullptr, NoVeins);
                        for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
                            for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
                                for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                                {
                                    const uint16 State = Generated.GetState(X, Y, Z);
                                    if (State == Plain.GetState(X, Y, Z)) continue;
                                    if (const int32* Vein = VeinIndex.Find(State))
                                        ++Counts[(Z - VoxelConstants::MinZ) * States.Num() + *Vein];
                                }
                    }
                    for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
                        for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
                            for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                            {
                                const uint16 After = Final->GetState(X, Y, Z), Before = Generated.GetState(X, Y, Z);
                                if (After == Before) continue;
                                const int32 Row = (Z - VoxelConstants::MinZ) * States.Num();
                                if (const int32* Added = StateIndex.Find(After)) ++Counts[Row + *Added];
                                if (const int32* Removed = StateIndex.Find(Before)) --Counts[Row + *Removed];
                            }
                }
                FString Csv = TEXT("z");
                for (const TPair<uint16, FString>& State : States)
                    Csv += TEXT(",") + State.Value;
                Csv += TEXT("\n");
                for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
                {
                    Csv += FString::FromInt(Z);
                    for (int32 Index = 0; Index < States.Num(); ++Index)
                        Csv +=
                            FString::Printf(TEXT(",%lld"), Counts[(Z - VoxelConstants::MinZ) * States.Num() + Index]);
                    Csv += TEXT("\n");
                }
                const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved") /
                    FString::Printf(TEXT("Voxel/OreHistogram-%s-r%d%s%s%s.csv"), *PresetName, Radius,
                        bCaves ? TEXT("") : TEXT("-nocaves"), bDiscard ? TEXT("") : TEXT("-nodiscard"),
                        Settings.bOreVeins ? TEXT("") : TEXT("-noveins")));
                if (!FFileHelper::SaveStringToFile(Csv, *Path))
                {
                    PrintLines(Output, FString::Printf(TEXT("voxel.OreHistogram: cannot write %s"), *Path));
                    return;
                }
                FString Report =
                    FString::Printf(TEXT("Ore histogram, %s, %d columns, caves %s, discard %s, ore veins %s, seed %s: "
                                         "%s\n"),
                        *PresetName, Area.Columns.Num(), bCaves ? TEXT("on") : TEXT("off"),
                        bDiscard ? TEXT("on") : TEXT("off"), Settings.bOreVeins ? TEXT("on") : TEXT("off"),
                        *Seed.Describe(), *Path);
                for (int32 Index = 0; Index < States.Num(); ++Index)
                {
                    int64 Total = 0;
                    for (int32 Z = 0; Z < VoxelConstants::Height; ++Z)
                        Total += Counts[Z * States.Num() + Index];
                    Report += FString::Printf(TEXT("  %-32s %9lld blocks, %8.1f per column\n"), *States[Index].Value,
                        Total, double(Total) / FMath::Max(1, Area.Columns.Num()));
                }
                PrintLines(Output, Report);
            }));
}

// The world clock's commands (water and sky DN1), in their own block.
namespace
{
    FAutoConsoleCommandWithWorldArgsAndOutputDevice VoxelTimeCommand(TEXT("voxel.Time"),
        TEXT("voxel.Time set <time>|day|noon|night|midnight, add <time>, query time|day|gametime, pause, resume, rate "
             "<rate>: the voxel world's clock, as /time. A time is whole ticks, or a number with d (24,000 ticks), s "
             "(20) or t. A marker moves the clock forward to its next tick of the day; the rate runs above 0 to 1000."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
            {
                AVoxelWorld* Voxels = AVoxelWorld::Find(World);
                FString Reply = TEXT("voxel.Time: no voxel world is running.");
                if (Voxels) Voxels->GetClock().RunCommand(Args, Reply);
                PrintLines(Output, Reply);
            }));
}
