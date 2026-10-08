#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationLab.h"
#include "Random/VoxelSeed.h"
#include "Tasks/Task.h"
#include "Worlds/UnnamedSavedWorlds.h"
#include "VoxelWorld.generated.h"

class ACameraActor;
class FVoxelBlockTable;
class FVoxelColumn;
class IVoxelTerrain;
class UFoundationBlockTextureTable;
class UMaterialInterface;
class UVoxelChunkComponent;

// The voxel world's level actor (/Game/Maps/L_Generated). Phase 2 builds a fixed square of columns around the origin
// (voxel.TestAreaRadius): generated and meshed on workers, then handed to the chunk meshes on the game thread within
// voxel.ApplyBudgetMs per frame. As the map's AFoundationLab it gives the Foundation pawn its collision (read from
// block data, not meshes), a spawn on dry ground and the world's game mode and difficulty. Phase 3 replaces the fixed
// area with streaming around the player.
//
// The menu opens a saved world as L_Generated?VoxelWorld=<id>: its seed and type pick the terrain. Without one the map
// shows the dev seed's test area. -VoxelSeed=<text> overrides either seed; -VoxelShot=<folder> saves screenshots once
// the area is ready, then quits.
UCLASS()
class UNNAMED_API AVoxelWorld : public AFoundationLab
{
    GENERATED_BODY()
public:
    AVoxelWorld();
    virtual void BuildLayout() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GatherCollision(const FBox& Region, TArray<FBox>& Out) const override;
    virtual FVector GetSafeSpawn(const UFoundationProfile& Profile) const override;
    virtual double GetFallOutZ() const override;
    virtual void ApplyLogicalOffset(const FVector& Offset) override;
    // A saved world's game mode (Hardcore plays as Survival on Hard), difficulty and natural regeneration rule.
    virtual void ConfigurePlayer(FFoundationVitals& Vitals) const override;

    // The saved world this map opened with ?VoxelWorld=<id>, read on first use; null for the dev test area or when
    // the world could not be read (logged).
    const FUnnamedSavedWorld* GetSavedWorld() const;

    // The first voxel world in a game world, or null.
    static AVoxelWorld* Find(const UWorld* World);
    // Rebuilds the area for a seed (voxel.Seed, voxel.Regenerate); the player returns to spawn once it is built.
    void Regenerate(const FVoxelSeed& NewSeed);
    const FVoxelSeed& GetSeed() const { return Seed; }
    // The state at a world cell; air outside the built columns.
    uint16 GetState(const FIntVector& Cell) const;
    // Every section of the latest build is on screen.
    bool IsReady() const { return bReady; }
    int32 GetColumnCount() const { return Columns.Num(); }
    int32 GetSectionMeshCount() const;
    // The latest build's timings (FVoxelArea::Describe plus streams and apply).
    const FString& GetBuildReport() const { return BuildReport; }

    // World position of cell (0, 0, 0)'s minimum corner; world rebasing moves it.
    FVector OriginCm = FVector::ZeroVector;

private:
    struct FBuild;

    void LoadMaterials();
    // The terrain for the current seed: a Superflat world's layers, otherwise the height terrain.
    TSharedRef<const IVoxelTerrain> MakeTerrain() const;
    void StartBuild();
    void FinishBuild(const TSharedPtr<FBuild>& Build);
    void ApplyColumns(double BudgetSeconds);
    UVoxelChunkComponent* GetChunk(FIntPoint Column);
    // The feet cell of the spawn: the dry surface nearest the origin, from the terrain function, so it is known
    // before any column exists.
    FIntVector GetSpawnCell() const;
    void ReturnPlayerToSpawn() const;
    void UpdateShots();

    FVoxelSeed Seed;
    mutable TOptional<FUnnamedSavedWorld> SavedWorld;
    mutable bool bSavedWorldRead = false;
    TSharedPtr<const FVoxelBlockTable> Table;
    TSharedPtr<const IVoxelTerrain> Terrain;
    TMap<FIntPoint, TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>> Columns;
    UPROPERTY(Transient)
    TMap<FIntPoint, TObjectPtr<UVoxelChunkComponent>> Chunks;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> LayerMaterials;
    UPROPERTY(Transient)
    TObjectPtr<UFoundationBlockTextureTable> TextureTable;

    UE::Tasks::TTask<TSharedPtr<FBuild>> BuildTask;
    TSharedPtr<FBuild> Applying;
    int32 BuildSerial = 0;
    bool bReady = false;
    FString BuildReport;
    mutable TOptional<FIntVector> SpawnCell;

    // -VoxelShot: the shot being taken, frames spent on it, its summed GPU time and the finished shots' means.
    FString ShotFolder;
    int32 ShotIndex = INDEX_NONE;
    int32 ShotFrames = 0;
    double ShotGpuMs = 0;
    FString ShotTimings;
    UPROPERTY(Transient)
    TObjectPtr<ACameraActor> ShotCamera;
};
