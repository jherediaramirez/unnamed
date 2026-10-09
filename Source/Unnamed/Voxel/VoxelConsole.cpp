#include "Voxel/VoxelConsole.h"
#include "Async/Fundamental/Scheduler.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Generation/VoxelArea.h"
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
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelBench.h"
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
TAutoConsoleVariable<bool> VoxelConsole::CVarEdgeWalls(TEXT("voxel.Debug.EdgeWalls"), false,
    TEXT("Debug: mesh the columns at the edge of the view against air, showing the cut through the world."));

int32 VoxelConsole::GetMaxTasksInFlight()
{
    const int32 Configured = CVarMaxTasksInFlight.GetValueOnAnyThread();
    return Configured > 0 ? Configured : FMath::Max(2, 2 * int32(LowLevelTasks::FScheduler::Get().GetNumWorkers()));
}

FString VoxelConsole::DescribeInfo()
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

    // The rings around a camera at the origin (Streaming/VoxelStreamingPlanner.h).
    const FVoxelStreamingRings Rings(FIntPoint::ZeroValue, FMath::Clamp(CVarViewRadius.GetValueOnAnyThread(), 1, 64),
        CVarUnloadMargin.GetValueOnAnyThread());
    TArray<FIntPoint> Needed;
    VoxelStreaming::GetNeededColumns(Rings, 0, Needed);
    int32 InView = 0;
    for (const FIntPoint Column : Needed)
        InView += Rings.IsInView(Column);
    Out += FString::Printf(TEXT("  View radius %d: %d columns in view, %d generated; unload beyond %d columns\n"),
        Rings.ViewRadius, InView, Needed.Num(), Rings.GetUnloadDistance());
    Out += FString::Printf(TEXT("  Task workers %u, tasks in flight %d\n"),
        LowLevelTasks::FScheduler::Get().GetNumWorkers(), GetMaxTasksInFlight());
    Out += FString::Printf(TEXT("  Mesh backend: RealtimeMeshComponent (%s)"),
        FModuleManager::Get().IsModuleLoaded(TEXT("RealtimeMeshComponent")) ? TEXT("loaded") : TEXT("not loaded"));
    return Out;
}

namespace
{
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

    FAutoConsoleCommandWithOutputDevice VoxelInfoCommand(TEXT("voxel.Info"),
        TEXT("Prints the voxel world's settings, console variables and limits."),
        FConsoleCommandWithOutputDeviceDelegate::CreateLambda(
            [](FOutputDevice& Output) { PrintLines(Output, VoxelConsole::DescribeInfo()); }));

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
             "(default radius 4, preset overworld) with height-bound skipping on and off, and reports each stage."),
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
                    if (!VoxelClimateDebug::GetSampler(PresetName, Seed.Value, Error) ||
                        !VoxelWorldgen::ReadPreset(VoxelWorldgen::GetReferenceRoot(), PresetName, Preset, Error) ||
                        !VoxelWorldgen::ReadBiomeParameters(
                            VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), Biomes, Error))
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    FVoxelDensityTerrainSettings Settings;
                    Settings.bCaves = !(Args.Num() > 3 && Args[3] == TEXT("nocaves"));
                    const FVoxelDensityTerrain Terrain(Preset, uint64(Seed.Value), *Table, Settings, &Biomes);
                    if (!Terrain.IsValid())
                    {
                        PrintLines(Output, Terrain.GetError());
                        return;
                    }
                    PrintLines(Output,
                        FString::Printf(TEXT("Voxel column bench, %s, caves %s, seed %s:"), *PresetName,
                            Settings.bCaves ? TEXT("on") : TEXT("off"), *Seed.Describe()) +
                            TEXT("\n") + VoxelBench::RunColumns(*Table, Terrain, &Terrain, Options, Radius));
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
                    if (!VoxelClimateDebug::GetSampler(PresetName, Seed.Value, Error) ||
                        !VoxelWorldgen::ReadPreset(VoxelWorldgen::GetReferenceRoot(), PresetName, Preset, Error) ||
                        !VoxelWorldgen::ReadBiomeParameters(
                            VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), Biomes, Error))
                    {
                        PrintLines(Output, Error);
                        return;
                    }
                    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
                    // A fourth argument "nocaves" benches the terrain without caves and aquifers (session 3's).
                    FVoxelDensityTerrainSettings Settings;
                    Settings.bCaves = !(Args.Num() > 3 && Args[3] == TEXT("nocaves"));
                    const FVoxelDensityTerrain Terrain(Preset, uint64(Seed.Value), *Table, Settings, &Biomes);
                    if (!Terrain.IsValid())
                    {
                        PrintLines(Output, Terrain.GetError());
                        return;
                    }
                    PrintLines(Output,
                        FString::Printf(TEXT("Voxel density bench, %s, caves %s, seed %s:"), *PresetName,
                            Settings.bCaves ? TEXT("on") : TEXT("off"), *Seed.Describe()) +
                            TEXT("\n") + VoxelBench::RunDensity(Terrain, Radius));
                    return;
                }
                PrintLines(Output,
                    TEXT("Usage: voxel.Bench Noise [millions] | voxel.Bench Columns [radius] [preset|standin] "
                         "[nocaves] | voxel.Bench "
                         "Climate [radius] [preset] | voxel.Bench Density [radius] [preset] [nocaves]"));
            }));
}
