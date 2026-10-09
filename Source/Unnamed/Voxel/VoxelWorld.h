#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationLab.h"
#include "Random/VoxelSeed.h"
#include "Worlds/UnnamedSavedWorlds.h"
#include "VoxelWorld.generated.h"

class ACameraActor;
class FVoxelFlight;
class UExponentialHeightFogComponent;
class UStaticMeshComponent;
class USkyAtmosphereComponent;
class FVoxelBlockTable;
class IVoxelTerrain;
class UFoundationBlockTextureTable;
class UFoundationInteractionView;
class UMaterialInterface;
class UVoxelWorldSubsystem;

// The voxel world's level actor (/Game/Maps/L_Generated) and the map's AFoundationLab. UVoxelWorldSubsystem streams the
// columns around the camera (phase 3); this actor decides what it streams and how it plays: the saved world's seed and
// terrain, the chunk materials, the Foundation pawn's collision (read from block data, not meshes), a spawn on dry
// ground, and the world's game mode and difficulty. It owns the world's FFoundationAuthority, whose block storage is
// the streamed columns (FVoxelBlockStorage), so the pawn mines, places and aims at generated terrain as in the
// Playground; edits last for the session (phase 10 saves them). Creative worlds build from an endless supply with
// the creative inventory; Survival spends what it mines and starts with the loadout's tools.
//
// The menu opens a saved world as L_Generated?VoxelWorld=<id>: its seed and type pick the terrain. Without one the map
// shows the dev seed. -VoxelSeed=<text> overrides either seed; -VoxelShot=<folder> saves screenshots once the view is
// on screen, then quits (-VoxelShotEdits first carves and builds across a column corner through the authority and
// adds an "edits" shot); -VoxelFlight=<folder> flies phase 3's acceptance flight (FVoxelFlight), then quits.
//
// The map's height fog becomes the edge fog: clear until 1.5 columns before voxel.ViewRadius and opaque at it, so
// the edge of the streamed world fades out (until phase 9's far LOD shows what lies beyond). Below the horizon, past
// the edge, a fog floor under the world's bottom takes the fog's color too, rather than the dark ground of the sky.
// Before the edge, the sky's aerial perspective thickens with distance, so the land pales as it recedes.
UCLASS()
class UNNAMED_API AVoxelWorld : public AFoundationLab
{
    GENERATED_BODY()
public:
    AVoxelWorld();
    virtual void BuildLayout() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;
    // Block boxes from the streamed columns. A column not generated yet is solid, so nothing walks or falls into it.
    virtual void GatherCollision(const FBox& Region, TArray<FBox>& Out) const override;
    virtual FFoundationTarget TraceTarget(
        FVector Eye, FVector Direction, const UFoundationProfile& Profile) const override;
    // From the blocks: a fluid cell holds water up to its surface (FluidAmount / 9 of the cell), or to its top when
    // more fluid lies above.
    virtual bool IsInWater(FVector Point) const override;
    virtual bool IsInWater(const FBox& Body) const override;
    virtual FFoundationAuthority* GetAuthority() override { return &Authority; }
    virtual FVector GetSafeSpawn(const UFoundationProfile& Profile) const override;
    virtual double GetFallOutZ() const override;
    virtual void ApplyLogicalOffset(const FVector& Offset) override;
    // A saved world's game mode (Hardcore plays as Survival on Hard), difficulty and natural regeneration rule.
    virtual void ConfigurePlayer(FFoundationVitals& Vitals) const override;

    // The saved world this map opened with ?VoxelWorld=<id>, read on first use; null for the dev seed or when the
    // world could not be read (logged).
    const FUnnamedSavedWorld* GetSavedWorld() const;

    // The first voxel world in a game world, or null.
    static AVoxelWorld* Find(const UWorld* World);
    // Streams the world again for a seed (voxel.Seed, voxel.Regenerate); the player returns to spawn once the columns
    // around it exist.
    void Regenerate(const FVoxelSeed& NewSeed);
    const FVoxelSeed& GetSeed() const { return Seed; }
    // The state at a world cell; air in columns not generated.
    uint16 GetState(const FIntVector& Cell) const;
    // The player stands on the spawn and every column in view is on screen.
    bool IsReady() const;
    // The seed, then how the first view got on screen (UVoxelWorldSubsystem::GetFirstViewReport).
    FString GetBuildReport() const;

    // World position of cell (0, 0, 0)'s minimum corner; world rebasing moves it.
    FVector OriginCm = FVector::ZeroVector;

private:
    void LoadMaterials();
    // Rules, registry, bounds, the streamed storage and the saved world's supply rules.
    void SetupAuthority();
    // The world box of the water in a cell, or false when it holds none.
    bool GetWaterBox(const FIntVector& Cell, FBox& OutBox) const;
    // The terrain for the current seed (VoxelTerrainFactory::Make). A saved world whose terrain cannot be built gets an
    // empty terrain and leaves for the title screen, with the reason in the log.
    TSharedRef<const IVoxelTerrain> MakeTerrain() const;
    UVoxelWorldSubsystem* GetStreaming() const;
    // The feet cell of the spawn: the dry surface nearest the origin, from the terrain function, so it is known
    // before any column exists.
    FIntVector GetSpawnCell() const;
    bool AreSpawnColumnsGenerated() const;
    // Fits the edge fog to the view radius whenever it changes, and keeps the fog floor under the camera.
    void UpdateEdgeFog();
    void SetEdgeFogVisible(bool bVisible);
    void ReturnPlayerToSpawn() const;
    void UpdateShots();
    // -VoxelShotEdits: a pit across the corner of four columns east and south of the spawn, and a pillar of stone
    // and glass on a column border, written through the authority's block world.
    void ApplyShotEdits();

    FVoxelSeed Seed;
    mutable TOptional<FUnnamedSavedWorld> SavedWorld;
    mutable bool bSavedWorldRead = false;
    TSharedPtr<const FVoxelBlockTable> Table;
    TSharedPtr<const IVoxelTerrain> Terrain;
    FFoundationAuthority Authority;
    UPROPERTY(Transient)
    TObjectPtr<UFoundationInteractionView> View;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> LayerMaterials;
    UPROPERTY(Transient)
    TObjectPtr<UFoundationBlockTextureTable> TextureTable;
    mutable TOptional<FIntVector> SpawnCell;
    // The player was put on the spawn once the columns around it were generated.
    bool bSpawned = false;
    UPROPERTY(Transient)
    TObjectPtr<UExponentialHeightFogComponent> EdgeFog;
    int32 EdgeFogRadius = INDEX_NONE;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> FogFloor;
    UPROPERTY(Transient)
    TObjectPtr<USkyAtmosphereComponent> Sky;
    bool bEdgeFogVisible = true;
    TSharedPtr<FVoxelFlight> Flight;

    // -VoxelShot: the shot being taken, frames spent on it, its summed GPU time and the finished shots' means.
    FString ShotFolder;
    int32 ShotIndex = INDEX_NONE;
    int32 ShotFrames = 0;
    double ShotGpuMs = 0;
    FString ShotTimings;
    // -VoxelShotEdits: 0 before the edits, 1 while their columns remesh, 2 once they are on screen.
    bool bShotEdits = false;
    // -VoxelShotCaves: views inside a cave and onto still lava, searched once near the center (names, camera and target
    // in cm); empty when none was found.
    bool bShotCaves = false;
    bool bShotCavesSearched = false;
    TArray<TTuple<FString, FVector, FVector>> ShotCaveViews;
    void FindCaveShots();
    int32 ShotEditStage = 0;
    FIntPoint ShotEditCorner = FIntPoint::ZeroValue;
    int32 ShotEditTop = 0;
    double ShotEditSeconds = 0;
    int32 ShotEditFrames = 0;
    FString ShotEditReport;
    UPROPERTY(Transient)
    TObjectPtr<ACameraActor> ShotCamera;
};
