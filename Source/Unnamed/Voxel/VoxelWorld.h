#pragma once

#include "CoreMinimal.h"
#include "Clock/VoxelWorldClock.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationLab.h"
#include "Random/VoxelSeed.h"
#include "Worlds/UnnamedSavedWorlds.h"
#include "VoxelWorld.generated.h"

class ACameraActor;
class FVoxelFlight;
class FVoxelLightShots;
class FVoxelLightTable;
class FVoxelLights;
class UExponentialHeightFogComponent;
class UPointLightComponent;
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
// adds an "edits" shot; -VoxelShotView=CX,CY,CZ,TX,TY,TZ adds a "view" shot from a camera to a target, in blocks);
// -VoxelFlight=<folder> flies phase 3's acceptance flight (FVoxelFlight), then quits. -VoxelTime=<ticks or marker>
// pins the world clock's day (FVoxelWorldClock) for shots and flights.
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
    // The F3 line: the world clock (FVoxelWorldClock::Describe).
    virtual FString DescribeDebug() const override;

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
    // The terrain the world streams, or null before BeginPlay.
    TSharedPtr<const IVoxelTerrain> GetTerrain() const { return Terrain; }
    // The biome index (into the terrain's biome table) of a world cell, read from its loaded column; INDEX_NONE when
    // the column is not generated or the terrain has no biomes.
    int32 GetBiome(const FIntVector& Cell) const;
    // The player stands on the spawn and every column in view is on screen.
    bool IsReady() const;
    // The seed, then how the first view got on screen (UVoxelWorldSubsystem::GetFirstViewReport).
    FString GetBuildReport() const;
    // The -VoxelShotCaves views (name, camera and target in cm), searched near the streamed area's centre on first
    // use; empty when none was found.
    const TArray<TTuple<FString, FVector, FVector>>& GetCaveShotViews();
    // The torch light pool (session T3): the streaming hands it each column's light sources; null before BeginPlay.
    FVoxelLights* GetLights() const { return Lights.Get(); }
    // The light table the column meshes scan their light sources with (voxel.LightMinLevel); null before BeginPlay.
    TSharedPtr<const FVoxelLightTable> GetLightTable() const { return LightTable; }
    // The voxel.TorchGlow level the columns are meshed with (the torch models' glow; 14 unless lowered), as read at the
    // last build or voxel.Regenerate.
    int32 GetMeshTorchGlow() const { return MeshTorchGlow; }
    // The world clock (DN1): the 20 Hz game tick and the day clock, advanced at the top of Tick; voxel.Time drives it.
    FVoxelWorldClock& GetClock() { return Clock; }
    const FVoxelWorldClock& GetClock() const { return Clock; }
    // The saved world's advance_time rule (default on), read at BuildLayout: off stops the day clock, not the game
    // tick.
    bool IsTimeAdvancing() const { return bAdvanceTime; }

    // World position of cell (0, 0, 0)'s minimum corner; world rebasing moves it.
    FVector OriginCm = FVector::ZeroVector;

private:
    void LoadMaterials();
    // Rules, registry, bounds, the streamed storage and the saved world's supply rules.
    void SetupAuthority();
    // The clock's tick length and cap from the pawn's profile, the saved world's clock and advance_time rule, then
    // -VoxelTime=<ticks or marker>, which pins the day clock (set and paused) for shots and flights.
    void SetupClock();
    // Writes the day clock into the saved world's world.json, read afresh so the menu's changes stay; never for the dev
    // seed or a pinned session.
    void SaveClock() const;
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
    // -VoxelShotGarden: a flat plot near the spawn with every model block (rows, a pool for the water-holding ones, a
    // glow lichen overhang, a dripstone column, tall grass across a section border, a patch plot for the distance
    // shots), and a grass field around a bare clearing, written through the authority's block world.
    void ApplyShotGarden();

    FVoxelSeed Seed;
    mutable TOptional<FUnnamedSavedWorld> SavedWorld;
    mutable bool bSavedWorldRead = false;
    TSharedPtr<const FVoxelBlockTable> Table;
    // What the columns are meshed with: Table with the torch models' glow at voxel.TorchGlow (MakeTorchGlowTable),
    // remade at every Regenerate. Everything else (terrain, light table, fluids) reads Table.
    TSharedPtr<const FVoxelBlockTable> MeshTable;
    int32 MeshTorchGlow = 14;
    TSharedPtr<const IVoxelTerrain> Terrain;
    FFoundationAuthority Authority;
    FVoxelWorldClock Clock;
    bool bAdvanceTime = true;
    // -VoxelTime pinned the day clock: the session never saves its time.
    bool bTimePinned = false;
    // Per state id: a model state with collision boxes (cactus, bamboo, lily pads, carpets), so GatherCollision passes
    // the others (grass, flowers) with one lookup.
    TArray<bool> ModelCollides;
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
    // -VoxelShotLights: the torch light probe, run before the shots.
    TSharedPtr<FVoxelLightShots> LightShots;
    // The torch lights: the light table for voxel.LightMinLevel at LightMinLevel, and the pool, whose point lights
    // LightComponents holds for the GC.
    TSharedPtr<const FVoxelLightTable> LightTable;
    int32 LightMinLevel = 0;
    TSharedPtr<FVoxelLights> Lights;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UPointLightComponent>> LightComponents;

    // -VoxelShot: the shot being taken, frames spent on it, its summed GPU time and the finished shots' means.
    FString ShotFolder;
    // Where the spawn search starts (-VoxelSpawnAt=X,Y, to shoot a chosen place); the origin by default. It moves only
    // the spawn, never a block.
    FIntPoint SpawnSearchCenter = FIntPoint::ZeroValue;
    // -VoxelSpawnExact (shots): every candidate is checked against its real column instead of the estimate first, so
    // the spawn can land on low dry ground the estimate calls sea, such as a swamp's.
    bool bSpawnExact = false;
    // -VoxelShotView=CX,CY,CZ,TX,TY,TZ (shots): an extra "view" shot from a camera to a target, both in blocks.
    TOptional<TPair<FVector, FVector>> ShotView;
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
    // -VoxelShotVeins: a view per ore vein type (vein-copper, vein-iron) onto one of the four cut faces (the edge
    // walls), aimed at the 32 x 18 window where that vein's ore and filler mix most. -VoxelShotVeinsAt=F,A,Z,F,A,Z aims
    // them at fixed windows instead (face 0 west, 1 east, 2 north, 3 south; A along the face; Z): the control run with
    // veins off reuses the first run's. The windows' block counts go to build.txt.
    struct FShotVeinTarget
    {
        int32 Face = 0;
        int32 Along = 0;
        int32 Z = 0;
    };
    bool bShotVeins = false;
    bool bShotVeinsSearched = false;
    TArray<FShotVeinTarget> ShotVeinTargets;
    FString ShotVeinReport;
    void FindVeinShots();
    int32 ShotEditStage = 0;
    FIntPoint ShotEditCorner = FIntPoint::ZeroValue;
    int32 ShotEditTop = 0;
    double ShotEditSeconds = 0;
    int32 ShotEditFrames = 0;
    FString ShotEditReport;
    // -VoxelShotGarden: the plot's minimum corner (its floor's height as Z), the field's clearing and the section
    // border the post crosses.
    bool bShotGarden = false;
    FIntVector GardenOrigin = FIntVector::ZeroValue;
    FIntVector GardenClearing = FIntVector::ZeroValue;
    int32 GardenBorderZ = 0;
    UPROPERTY(Transient)
    TObjectPtr<ACameraActor> ShotCamera;
};
