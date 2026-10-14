#pragma once

#include "CoreMinimal.h"
#include "Clock/VoxelWorldClock.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationLab.h"
#include "Random/VoxelSeed.h"
#include "Voxel/VoxelAtmosphere.h"
#include "Voxel/VoxelEdgeFog.h"
#include "Voxel/VoxelLook.h"
#include "Worlds/UnnamedSavedWorlds.h"
#include "VoxelWorld.generated.h"

class ACameraActor;
class APostProcessVolume;
class FVoxelFlight;
class FVoxelFluidShots;
class FVoxelFluidSystem;
class FVoxelLightShots;
class FVoxelLightTable;
class FVoxelLights;
class FVoxelTransitionShots;
class UExponentialHeightFogComponent;
class UMaterialInstanceDynamic;
enum class EVoxelFluid : uint8;
class UPointLightComponent;
class UStaticMeshComponent;
class USkyAtmosphereComponent;
class UDirectionalLightComponent;
struct FVoxelEnvironment;
class FVoxelBlockTable;
class IVoxelTerrain;
class UFoundationBlockTextureTable;
class UFoundationInteractionView;
class UMaterialInterface;
class UCurveFloat;
class UMaterialParameterCollection;
class USkyLightComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UVoxelWorldSubsystem;
class UVoxelStars;

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
// adds an "edits" shot; -VoxelShotView=CX,CY,CZ,TX,TY,TZ adds a "view" shot from a camera to a target, in blocks;
// -VoxelShotFluids=breach|flood first runs a flowing water capture, FVoxelFluidShots);
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
    virtual EFoundationFluid GetFluid(FVector Point) const override;
    virtual EFoundationFluid GetFluid(const FBox& Body) const override;
    virtual double GetFluidAboveCm(FVector Feet) const override;
    virtual FFoundationAuthority* GetAuthority() override { return &Authority; }
    virtual FVector GetSafeSpawn(const UFoundationProfile& Profile) const override;
    virtual double GetFallOutZ() const override;
    virtual void ApplyLogicalOffset(const FVector& Offset) override;
    // A saved world's game mode (Hardcore plays as Survival on Hard), difficulty and natural regeneration rule.
    virtual void ConfigurePlayer(FFoundationVitals& Vitals) const override;
    // The loading screen's state: ready once the player stands on the spawn and the view is on screen; progress is the
    // columns in view that are on screen; the map colours the columns around the spawn by pipeline stage.
    virtual void GetLoadState(FFoundationLoadState& Out, bool bWithMap) const override;
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
    // The edge fog shows: false while shots look from outside the world (SetEdgeFogVisible).
    bool IsEdgeFogVisible() const { return bEdgeFogVisible; }
    // The seed, then how the first view got on screen (UVoxelWorldSubsystem::GetFirstViewReport).
    FString GetBuildReport() const;
    // The -VoxelShotCaves views (name, camera and target in cm), searched near the streamed area's centre on first
    // use; empty when none was found.
    const TArray<TTuple<FString, FVector, FVector>>& GetCaveShotViews();
    // The torch light pool (session T3): the streaming hands it each column's light sources; null before BeginPlay.
    FVoxelLights* GetLights() const { return Lights.Get(); }
    // The light table the column meshes scan their light sources with (voxel.LightMinLevel); null before BeginPlay.
    TSharedPtr<const FVoxelLightTable> GetLightTable() const { return LightTable; }
    // The block table the world streams with; null before BeginPlay.
    TSharedPtr<const FVoxelBlockTable> GetBlockTable() const { return Table; }
    // The voxel.TorchGlow level the columns are meshed with (the torch models' glow; 14 unless lowered), as read at the
    // last build or voxel.Regenerate.
    int32 GetMeshTorchGlow() const { return MeshTorchGlow; }
    // The world clock (DN1): the 20 Hz game tick and the day clock, advanced at the top of Tick; voxel.Time drives it.
    FVoxelWorldClock& GetClock() { return Clock; }
    const FVoxelWorldClock& GetClock() const { return Clock; }
    // The saved world's advance_time rule (default on), read at BuildLayout: off stops the day clock, not the game
    // tick.
    bool IsTimeAdvancing() const { return bAdvanceTime; }
    // The environment now (atmosphere step 1): the clock's day and the sun and moon it places, read without changing
    // the clock.
    FVoxelEnvironment GetEnvironment() const;
    // Flowing water and lava (water and sky W2a): they wake up from the authority's block changes and run after the
    // clock's steps in Tick, while voxel.Fluids is on. Null before BeginPlay.
    FVoxelFluidSystem* GetFluids() const { return Fluids.Get(); }

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
    // The world box of the fluid in a cell (water or lava: vitals count both as water), or false when it holds none.
    // OutLava, when given, is set when the fluid is lava.
    bool GetFluidBox(const FIntVector& Cell, FBox& OutBox, bool* OutLava = nullptr) const;
    bool GetWaterBox(const FIntVector& Cell, FBox& OutBox) const { return GetFluidBox(Cell, OutBox); }
    // The terrain for the current seed (VoxelTerrainFactory::Make). A saved world whose terrain cannot be built gets an
    // empty terrain and leaves for the title screen, with the reason in the log.
    TSharedRef<const IVoxelTerrain> MakeTerrain() const;
    UVoxelWorldSubsystem* GetStreaming() const;
    // The feet cell of the spawn (FVoxelSpawnFinder::Find): the climate search's dry land, or with -VoxelSpawnAt or
    // -VoxelSpawnExact the dry surface nearest the search centre, from the terrain function, so it is known before any
    // column exists. StandOnSpawnBlocks moves the climate search's cell onto the final blocks once they exist.
    FIntVector GetSpawnCell() const;
    bool AreSpawnColumnsGenerated() const;
    // Fits the edge fog to the drawn radius and the haze switch whenever they change (far LOD step 2: the haze while
    // far meshes form the edge, else step 1's wall), and keeps the fog floor under the camera.
    void UpdateEdgeFog();
    // The fog in fluids (VoxelWaterFog.h): on while the camera's eye is in water or lava and voxel.Look.WaterFog is 1.
    void UpdateWaterFog();
    // The fluid at a point: the cell's fluid where the point is under its surface, else None.
    EVoxelFluid GetFluidAt(FVector Point) const;
    // The sky's aerial perspective distance scale for the edge fog as applied.
    float GetEdgeAerialScale() const;
    // The fog floor shows unless the haze's voxel.Fog.FloorDepth is below 0 (a look check without it).
    bool IsFogFloorShown() const;
    void SetEdgeFogVisible(bool bVisible);
    // The look dials (VoxelLook.h): applies the voxel.Look.* variables to the map's rig when they change.
    void ApplyLook();
    // voxel.Look.SkyPin: probes the camera's open sky every VoxelLook::SkyProbeSeconds and eases the pin towards it.
    double UpdateSkyPin();
    // voxel.Atmosphere.Cycle 1: the map's sun follows the environment's sun (direction, and its light fading out below
    // the horizon). The only writer of the sun light; at 0 it puts back the map's rotation and intensity once, and
    // otherwise never touches the light.
    void ApplyCycle();
    // The optional stars and twilight backdrop: independently switched, camera-centred, reading this environment.
    void UpdateStars();
    // voxel.Look.CaveFloor: makes its post-process on first use, switches it, and fades the floor with the fog that
    // hides the far scene (the edge fog; with the eye in water, the water fog while voxel.Look.WaterFog is on).
    // The independent twilight minimum shares that range but runs before translucent water and glass.
    void UpdateCaveFloor(float Strength, float TwilightStrength);
    // On the climate search's dry land: the spawn cell rises or falls to the highest block that collides in its block
    // column (a treetop counts), then rises until the body fits, as the Playground's spawn does.
    void StandOnSpawnBlocks();
    void ReturnPlayerToSpawn() const;
    void UpdateShots();
    // -VoxelLoadingShot: the loading screen's two looks, saved while it shows (see UpdateLoadingShot's stages).
    void UpdateLoadingShot();
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
    // Far LOD step 2: per layer, the far fade's material instance (FarMaterialNames), or null.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> FarLayerMaterials;
    UPROPERTY(Transient)
    TObjectPtr<UFoundationBlockTextureTable> TextureTable;
    mutable TOptional<FIntVector> SpawnCell;
    // The spawn cell came from the climate search on dry land (FVoxelSpawn::bStandOnBlocks).
    mutable bool bSpawnOnBlocks = false;
    // The player was put on the spawn once the columns around it were generated.
    bool bSpawned = false;
    UPROPERTY(Transient)
    TObjectPtr<UExponentialHeightFogComponent> EdgeFog;
    // The edge fog as last applied: the drawn radius, step 2's haze or step 1's wall, the haze's settings and its
    // layer's height over the fog actor (sea level).
    struct FEdgeFogState
    {
        int32 Drawn = INDEX_NONE;
        bool bHaze = false;
        VoxelEdgeFog::FHaze Haze;
        double SeaOffsetCm = 0;
        // x the wall's own colour (voxel.Look.Sky).
        float ColorScale = 1;
        bool operator==(const FEdgeFogState& Other) const = default;
    };
    FEdgeFogState EdgeFogState;
    // The map's own second fog layer and directional in-scattering, which the haze replaces and the wall keeps.
    float MapSecondFogDensity = 0;
    float MapSecondFogFalloff = 0;
    float MapSecondFogOffset = 0;
    FLinearColor MapDirectionalInscattering = FLinearColor::Black;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> FogFloor;
    UPROPERTY(Transient)
    TObjectPtr<USkyAtmosphereComponent> Sky;
    UPROPERTY(Transient)
    TObjectPtr<UVoxelStars> Stars;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> TwilightBackdrop;
    bool bTriedTwilightBackdrop = false;
    bool bEdgeFogVisible = true;
    // The map's rig the look dials drive (its unbound post-process volume and sky light), MPC_VoxelLook, the look as
    // last applied and the edge fog colour's scale from it.
    UPROPERTY(Transient)
    TObjectPtr<APostProcessVolume> LookVolume;
    // voxel.Look.CaveFloor: the unbound post-process that draws M_LookCaveFloor, on only while the dial is above 0.
    UPROPERTY(Transient)
    TObjectPtr<UPostProcessComponent> CaveFloorPost;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> CaveFloorMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UPostProcessComponent> TwilightFloorPost;
    bool bTriedTwilightFloor = false;
    UPROPERTY(Transient)
    TObjectPtr<USkyLightComponent> SkyFill;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollection> LookCollection;
    // M_WaterFog's instance on LookVolume, made when the eye first enters a fluid.
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> WaterFogMaterial;
    // The volume's exposure curve while a look needs one (VoxelLook::SolveExposure).
    UPROPERTY(Transient)
    TObjectPtr<UCurveFloat> LookCurve;
    TOptional<FVoxelLook> AppliedLook;
    double AppliedPin = 0;
    // The map's sun (its one directional light) and its rig as loaded, for voxel.Atmosphere.Cycle 0.
    UPROPERTY(Transient)
    TObjectPtr<UDirectionalLightComponent> SunLight;
    FRotator MapSunRotation = FRotator::ZeroRotator;
    float MapSunIntensity = 0.f;
    FLinearColor MapSunColor = FLinearColor::White;
    // The cycle's light (the sun's, or the moon's at night) as last applied; unset while the map's rig stands.
    struct FAppliedSun
    {
        FRotator Rotation;
        float Intensity = 0.f;
        FLinearColor Color;

        bool operator==(const FAppliedSun& Other) const = default;
    };
    TOptional<FAppliedSun> AppliedSun;
    // The night look as ApplyLook last composed it; all zero by day and with the cycle off.
    VoxelAtmosphere::FNight AppliedNight;
    // SkyPin: the pin now and its target, the next probe's and the last update's time, and the probes' cost.
    double SkyPinNow = 0;
    double SkyPinTarget = 0;
    double SkyProbeDue = 0;
    double SkyPinSeconds = 0;
    double SkyProbeMicros = 0;
    int32 SkyProbes = 0;
    float EdgeFogColorScale = 1;
    TSharedPtr<FVoxelFlight> Flight;
    TSharedPtr<FVoxelFluidSystem> Fluids;
    // -VoxelShotLights: the torch light probe, run before the shots.
    TSharedPtr<FVoxelLightShots> LightShots;
    // -VoxelShotFluids=breach|flood: the flowing water captures (W2a), run before the shots, after the light probe.
    TSharedPtr<FVoxelFluidShots> FluidShots;
    // -VoxelShotTransition: the eye adaptation check between sun and shade, after the fluid capture.
    TSharedPtr<FVoxelTransitionShots> TransitionShots;
    // The torch lights: the light table for voxel.LightMinLevel at LightMinLevel, and the pool, whose point lights
    // LightComponents holds for the GC.
    TSharedPtr<const FVoxelLightTable> LightTable;
    int32 LightMinLevel = 0;
    TSharedPtr<FVoxelLights> Lights;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UPointLightComponent>> LightComponents;

    // -VoxelShot: the shot being taken, frames spent on it, its summed GPU time and the finished shots' means.
    FString ShotFolder;
    // -VoxelLoadingShot=<folder>: loading-a.png (with the map), then loading-b.png (map forced off). Stage: 0 waits for
    // half the view, 1 waits for a's file, 2 forces the map off, 3 requests b with it off, 4 waits for b's file, 5
    // done. The loading screen does not end before stage 5.
    FString LoadingShotFolder;
    int32 LoadingShotStage = 0;
    bool bLoadingShotNoMap = false;
    // Where the spawn search starts (-VoxelSpawnAt=X,Y, to shoot a chosen place); the origin by default. It moves only
    // the spawn, never a block.
    FIntPoint SpawnSearchCenter = FIntPoint::ZeroValue;
    // -VoxelSpawnAt or -VoxelSpawnExact: the nearest-land search from SpawnSearchCenter, as before the climate search,
    // so captures keep their spawn.
    bool bSpawnNearest = false;
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
    // -VoxelShotDrops (items S8a): a block drop and an item drop fall to the floor before each cave view's camera.
    bool bShotDrops = false;
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
    // -VoxelMineProbe (loot step 2a): once the world is ready, a probe player mines a stone block beside the spawn
    // with a stone pickaxe through the authority, under the saved world's game mode (survival without one), and its
    // hotbar goes to the log and build.txt.
    bool bMineProbe = false;
    FString MineProbeReport;
    void RunMineProbe();
    // -VoxelShotGarden: the plot's minimum corner (its floor's height as Z), the field's clearing and the section
    // border the post crosses.
    bool bShotGarden = false;
    FIntVector GardenOrigin = FIntVector::ZeroValue;
    FIntVector GardenClearing = FIntVector::ZeroValue;
    int32 GardenBorderZ = 0;
    UPROPERTY(Transient)
    TObjectPtr<ACameraActor> ShotCamera;
};
