#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelEdgeFog.h"
#include "Voxel/VoxelPendingReleases.h"
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
    // Far LOD: far meshes applied this frame and their time (part of Applied and ApplyMs).
    int32 FarApplied = 0;
    double FarApplyMs = 0;
    // Planning and jobs split (far LOD step 1b): SetFocus and the chunks' visibility (parts of PlanMs), the pipeline's
    // walk with the needed columns it visited, and releasing unloaded chunks (parts of JobsMs).
    double FocusMs = 0;
    double VisibilityMs = 0;
    double WalkMs = 0;
    int32 Walked = 0;
    double ReleaseMs = 0;
    // Releasing waiting columns' components after the applies (part of TickMs, not ApplyMs).
    double PendingMs = 0;
};
class UMaterialInstanceDynamic;
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
// - Pooling: an unloaded column's component is emptied, hidden and reused for the next column that needs one. The
//   release waits for the time the budget leaves (at least one a frame); a column that gets a mesh again first keeps
//   its component (TVoxelPendingReleases).
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

    // Starts streaming for Owner: columns from Terrain, meshed with Table and drawn with one material per layer; far
    // meshes draw a layer in FarLayerMaterials' entry instead while the haze is on (far LOD step 2), where it has one.
    // InitialFocus stands in for the camera until there is one. Starting again drops everything (voxel.Seed).
    void Start(AVoxelWorld& InOwner, TSharedRef<const FVoxelBlockTable> Table, TSharedRef<const IVoxelTerrain> Terrain,
        TConstArrayView<UMaterialInterface*> LayerMaterials,
        TConstArrayView<UMaterialInstanceDynamic*> FarLayerMaterials, const FVector& InitialFocus);
    void Stop();

    // A world location for the rings to follow instead of the camera (captures pin the player's spawn); unset
    // follows the camera again.
    void SetFocusOverride(const TOptional<FVector>& Location) { FocusOverride = Location; }

    const FVoxelColumnPipeline* GetPipeline() const { return Pipeline.Get(); }
    FVoxelColumnPipeline* GetPipeline() { return Pipeline.Get(); }
    // Every column in view is on screen.
    // The near view: with a far ring, the far columns may still be loading.
    bool IsViewOnScreen() const { return Pipeline && Pipeline->IsNearViewOnScreen(); }
    // How the first view got on screen after Start: stage timings, quads, memory, cards and the apply. Empty until
    // then.
    const FString& GetFirstViewReport() const { return FirstViewReport; }
    // The streaming stages now (voxel.Info): rings, per-stage times and counts, what is held, recompute-on-miss.
    FString DescribeStages() const;
    int32 GetChunkCount() const { return Chunks.Num(); }
    int32 GetSectionMeshCount() const;
    // This frame's chunk work on the game thread, and the worst frame since Start.
    const FVoxelStreamingFrame& GetLastFrame() const { return LastFrame; }
    const FVoxelStreamingFrame& GetWorstFrame() const { return WorstFrame; }
    // Unloaded columns whose components wait to be released, now and at most since Start.
    int32 GetPendingReleaseCount() const { return PendingReleases.Num(); }
    int32 GetPendingReleasePeak() const { return PendingReleases.GetPeak(); }

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
    // Columns out of view stay loaded (hysteresis) but hidden: the fog covers them. After a move that kept the drawn
    // radius, only the columns that crossed it change (far LOD step 1b).
    void UpdateVisibility(const FVoxelStreamingRings& Before);
    // Releases waiting columns' components, oldest first, while the frame is under 85% of the budget (one anyway).
    void ReleasePending(double BudgetMs, double TickStartSeconds);
    // Far LOD: the debug tint's material while voxel.Debug.FarTint is on (else null), and far looks refreshed on
    // screen when voxel.Far.Shadows, the tint or the haze (voxel.Fog.Haze, step 2) change. The fade's distances follow
    // the drawn radius.
    UMaterialInterface* GetFarTintMaterial();
    void UpdateFarLooks();
    UVoxelChunkComponent* AcquireChunk(FIntPoint Column);
    // Empties, hides and pools the column's component; false when it has none.
    bool ReleaseChunk(FIntPoint Column);
    void ReportFirstView();
    void UpdateStats();

    TUniquePtr<FVoxelColumnPipeline> Pipeline;
    TWeakObjectPtr<AVoxelWorld> Owner;
    // voxel.Decoration, voxel.Decoration.FreezeLast and voxel.OreVeins as the pipeline was started with them; a change
    // rebuilds the world.
    bool bDecoration = false;
    bool bFreezeLast = true;
    bool bPlants = true;
    bool bTrees = true;
    bool bOreVeins = true;
    UPROPERTY(Transient)
    TMap<FIntPoint, TObjectPtr<UVoxelChunkComponent>> Chunks;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UVoxelChunkComponent>> Pool;
    // Unloaded columns whose components, still in Chunks, wait to be released.
    TVoxelPendingReleases<FIntPoint> PendingReleases;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> Materials;
    // voxel.Debug.FarTint's material, made when first wanted.
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> FarTintMaterial;
    bool bFarShadows = false;
    bool bFarTint = false;
    // Far LOD step 2: the far fade's material per layer (null where a layer keeps its own), and what far meshes draw in
    // now: the debug tint in every slot, the fade's while the haze is on, or nothing (their own).
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> FarFadeMaterials;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> FarLook;
    bool bFarFade = false;
    bool bFarLookStale = true;
    // The fade's distances as last set on FarFadeMaterials.
    VoxelEdgeFog::FFade FarFade;
    TOptional<FVector> FocusOverride;
    FVector LastFocus = FVector::ZeroVector;
    FApplyCost ApplyCost;
    FApplyCost FarApplyCost;
    // The ready column that last waited a frame because the frame's planning and jobs had used the budget; it goes
    // first when it heads the queue again.
    TOptional<FIntPoint> WaitedColumn;

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
    int32 LogFarApplied = 0;
    double LogFarApplyMs = 0;
    // When the near view was first on screen (far LOD's first-view split); 0 until then.
    double NearViewSeconds = 0;
    FVoxelStreamingFrame LogWorstFrame;
};
