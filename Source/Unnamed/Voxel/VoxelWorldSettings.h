#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VoxelWorldSettings.generated.h"

// Generator and streaming defaults (Project Settings > Game > Unnamed Voxel). Each tunable mirrors a voxel.* console
// variable (VoxelConsole): these values are pushed at startup and on every edit, and runtime code reads the
// variables, so a console override (`voxel.ViewRadius 16`) wins for the session without touching the saved defaults.
// A deliberate change belongs in [/Script/Unnamed.VoxelWorldSettings] in Config/DefaultGame.ini.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Unnamed Voxel"))
class UNNAMED_API UVoxelWorldSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    static const UVoxelWorldSettings& Get() { return *GetDefault<UVoxelWorldSettings>(); }

    virtual void PostInitProperties() override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
    // Writes every tunable into its console variable at project-setting priority; higher-priority overrides
    // (console, command line, ini) keep their values.
    void PushToConsoleVariables() const;

    // Seed text for a world opened without one: the editor and the dev test area. Created worlds keep their own.
    UPROPERTY(Config, EditAnywhere, Category = "Generation")
    FString DevSeed = TEXT("Unnamed");

    // voxel.ViewRadius: columns kept loaded around the player, as a radius in 16-block columns.
    UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = 2, ClampMax = 64))
    int32 ViewRadius = 12;

    // voxel.UnloadMargin: a column unloads only beyond ViewRadius + this, so walking along a border never thrashes.
    UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 8))
    int32 UnloadMargin = 2;

    // voxel.TestAreaRadius: the fixed test area's radius in columns; 4 builds 9 x 9 columns.
    UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = 0, ClampMax = 16))
    int32 TestAreaRadius = 4;

    // voxel.ApplyBudgetMs: game-thread time per frame for applying finished chunk work.
    UPROPERTY(Config, EditAnywhere, Category = "Budgets", meta = (ClampMin = 0.1, ClampMax = 16, Units = "ms"))
    float ApplyBudgetMs = 2.f;

    // voxel.MaxTasksInFlight: generation and meshing tasks in flight; 0 means twice the task workers.
    UPROPERTY(Config, EditAnywhere, Category = "Budgets", meta = (ClampMin = 0, ClampMax = 512))
    int32 MaxTasksInFlight = 0;

    // voxel.AmbientOcclusion: per-vertex ambient occlusion in chunk meshes.
    UPROPERTY(Config, EditAnywhere, Category = "Meshing")
    bool bAmbientOcclusion = true;

    // voxel.GreedyMeshing: merge coplanar faces into larger quads; off emits one quad per face, for debugging.
    UPROPERTY(Config, EditAnywhere, Category = "Meshing")
    bool bGreedyMeshing = true;
};
