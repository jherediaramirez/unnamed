#include "Voxel/VoxelConsole.h"
#include "Async/Fundamental/Scheduler.h"
#include "Modules/ModuleManager.h"
#include "Voxel/VoxelWorldSettings.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

// Defaults match UVoxelWorldSettings; the settings object overwrites them at startup.
TAutoConsoleVariable<int32> VoxelConsole::CVarViewRadius(
    TEXT("voxel.ViewRadius"), 12, TEXT("Columns kept loaded around the player, as a radius in 16-block columns."));
TAutoConsoleVariable<int32> VoxelConsole::CVarUnloadMargin(
    TEXT("voxel.UnloadMargin"), 2, TEXT("Extra columns beyond the view radius before a column unloads (hysteresis)."));
TAutoConsoleVariable<int32> VoxelConsole::CVarTestAreaRadius(
    TEXT("voxel.TestAreaRadius"), 4, TEXT("Radius in columns of the fixed test area; 4 builds 9 x 9 columns."));
TAutoConsoleVariable<float> VoxelConsole::CVarApplyBudgetMs(
    TEXT("voxel.ApplyBudgetMs"), 2.f, TEXT("Game-thread milliseconds per frame for applying finished chunk work."));
TAutoConsoleVariable<int32> VoxelConsole::CVarMaxTasksInFlight(TEXT("voxel.MaxTasksInFlight"), 0,
    TEXT("Generation and meshing tasks allowed in flight; 0 means twice the task workers."));
TAutoConsoleVariable<bool> VoxelConsole::CVarAmbientOcclusion(
    TEXT("voxel.AmbientOcclusion"), true, TEXT("Per-vertex ambient occlusion in chunk meshes."));
TAutoConsoleVariable<bool> VoxelConsole::CVarGreedyMeshing(TEXT("voxel.GreedyMeshing"), true,
    TEXT("Merge coplanar faces into larger quads; off emits one quad per face, for debugging."));

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
    Out += FString::Printf(TEXT("  Dev seed: \"%s\" (used when a world has none)\n"), *Settings.DevSeed);

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

    const int32 View = CVarViewRadius.GetValueOnAnyThread();
    const int32 TestArea = CVarTestAreaRadius.GetValueOnAnyThread();
    Out += FString::Printf(TEXT("  Load %d x %d columns, unload beyond radius %d; test area %d x %d columns\n"),
        2 * View + 1, 2 * View + 1, View + CVarUnloadMargin.GetValueOnAnyThread(), 2 * TestArea + 1, 2 * TestArea + 1);
    Out += FString::Printf(TEXT("  Task workers %u, tasks in flight %d\n"),
        LowLevelTasks::FScheduler::Get().GetNumWorkers(), GetMaxTasksInFlight());
    Out += FString::Printf(TEXT("  Mesh backend: RealtimeMeshComponent (%s)"),
        FModuleManager::Get().IsModuleLoaded(TEXT("RealtimeMeshComponent")) ? TEXT("loaded") : TEXT("not loaded"));
    return Out;
}

namespace
{
    FAutoConsoleCommandWithOutputDevice InfoCommand(TEXT("voxel.Info"),
        TEXT("Prints the voxel world's settings, console variables and limits."),
        FConsoleCommandWithOutputDeviceDelegate::CreateLambda(
            [](FOutputDevice& Output)
            {
                const FString Info = VoxelConsole::DescribeInfo();
                TArray<FString> Lines;
                Info.ParseIntoArrayLines(Lines);
                for (const FString& Line : Lines)
                {
                    Output.Log(Line);
                    UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Line);
                }
            }));
}
