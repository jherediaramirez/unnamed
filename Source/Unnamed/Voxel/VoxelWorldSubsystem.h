#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "VoxelWorldSubsystem.generated.h"

class AVoxelWorld;

// One frame of streaming on the game thread, in milliseconds: moving the rings, collecting and launching jobs, and
// applying finished meshes (Tick covers all three plus the bookkeeping between them).
struct FVoxelStreamingFrame
{
    double TickMs = 0;
    double PlanMs = 0;
    double JobsMs = 0;
    double ApplyMs = 0;
    int32 Applied = 0;
};
class UMaterialInterface;
class UVoxelChunkComponent;

// Streams the voxel world around the camera (phase 3). Each frame it moves FVoxelColumnPipeline's rings, collects and
// launches its jobs, and hands finished column meshes to chunk components. Idle until AVoxelWorld starts it.
//
// - Focus: the local player's camera, so the rings follow whatever the player looks through (the pawn, or voxel.Fly's
//   camera). Captures pin it with SetFocusOverride.
// - Budget: all of this frame's chunk work on the game thread, planning and polling included, stays within
//   voxel.ApplyBudgetMs. Each column's apply cost is estimated from its quads before it is applied (a line fitted to
//   measured applies), so a frame stops before the budget rather than after it.
// - Pooling: an unloaded column's component is emptied, hidden and reused for the next column that needs one.
// - Diagnostics: `stat Voxel` counters, a log line every second while anything streams, and Insights scopes.
UCLASS()
class UNNAMED_API UVoxelWorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return Pipeline.IsValid(); }
    virtual TStatId GetStatId() const override;

    static UVoxelWorldSubsystem* Get(const UWorld* World);

    // Starts streaming for Owner: columns from Terrain, meshed with Table and drawn with one material per layer.
    // InitialFocus stands in for the camera until there is one. Starting again drops everything (voxel.Seed).
    void Start(AVoxelWorld& InOwner, TSharedRef<const FVoxelBlockTable> Table, TSharedRef<const IVoxelTerrain> Terrain,
        TConstArrayView<UMaterialInterface*> LayerMaterials, const FVector& InitialFocus);
    void Stop();

    // A world location for the rings to follow instead of the camera (captures pin the player's spawn); unset
    // follows the camera again.
    void SetFocusOverride(const TOptional<FVector>& Location) { FocusOverride = Location; }

    const FVoxelColumnPipeline* GetPipeline() const { return Pipeline.Get(); }
    FVoxelColumnPipeline* GetPipeline() { return Pipeline.Get(); }
    // Every column in view is on screen.
    bool IsViewOnScreen() const { return Pipeline && Pipeline->IsViewOnScreen(); }
    // How the first view got on screen after Start: stage timings, quads, memory, cards and the apply. Empty until
    // then.
    const FString& GetFirstViewReport() const { return FirstViewReport; }
    int32 GetChunkCount() const { return Chunks.Num(); }
    int32 GetSectionMeshCount() const;
    // This frame's chunk work on the game thread, and the worst frame since Start.
    const FVoxelStreamingFrame& GetLastFrame() const { return LastFrame; }
    const FVoxelStreamingFrame& GetWorstFrame() const { return WorstFrame; }

private:
    // A least-squares line through measured applies, weighted toward the latest: ms = Fixed + PerQuad * quads.
    struct FApplyCost
    {
        double Weight = 0, Quads = 0, QuadsSquared = 0, Ms = 0, QuadsMs = 0;
        void Add(int32 QuadCount, double Milliseconds);
        double Estimate(int32 QuadCount) const;
    };

    FVoxelStreamingRings MakeRings(const FVector& Focus) const;
    void ApplyReady(double BudgetMs, double TickStartSeconds);
    // Columns out of view stay loaded (hysteresis) but hidden: the fog covers them.
    void UpdateVisibility();
    UVoxelChunkComponent* AcquireChunk(FIntPoint Column);
    void ReleaseChunk(FIntPoint Column);
    void ReportFirstView();
    void UpdateStats();

    TUniquePtr<FVoxelColumnPipeline> Pipeline;
    TWeakObjectPtr<AVoxelWorld> Owner;
    // voxel.Decoration.Preview as the pipeline was started with it; a change rebuilds the world.
    bool bDecorationPreview = false;
    UPROPERTY(Transient)
    TMap<FIntPoint, TObjectPtr<UVoxelChunkComponent>> Chunks;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UVoxelChunkComponent>> Pool;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> Materials;
    TOptional<FVector> FocusOverride;
    FVector LastFocus = FVector::ZeroVector;
    FApplyCost ApplyCost;

    double StartSeconds = 0;
    // The first frame after Start, and the frames until the first view was on screen.
    double FirstTickSeconds = 0;
    int32 FirstViewFrames = 0;
    FVoxelStreamingFrame LastFrame;
    FVoxelStreamingFrame WorstFrame;
    // Until the first view is on screen: its applies, the frames they took and the worst frame.
    double FirstApplyMs = 0;
    double FirstWorstApplyMs = 0;
    int32 FirstApplied = 0;
    int32 FirstApplyFrames = 0;
    FString FirstViewReport;
    // The per-second log line: when it was last written, and what happened since.
    double LogSeconds = 0;
    int32 LogApplied = 0;
    double LogApplyMs = 0;
    FVoxelStreamingFrame LogWorstFrame;
};
