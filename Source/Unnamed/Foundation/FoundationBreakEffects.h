#pragma once

#include "CoreMinimal.h"
// FoundationAuthority.h needs it first.
#include "Containers/StaticArray.h"
#include "Foundation/FoundationAuthority.h"
#include "InstanceDataTypes.h"
#include "Math/RandomStream.h"

class UFoundationBreakAssets;
class UFoundationInteractionView;
class UFoundationSoundAssets;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FFoundationBreakBlock;
struct FFoundationBreakSoundEvent;
struct FFoundationInteraction;
struct FFoundationSoundEvent;

// The breaking feel (Docs/Breaking.md): the target outline, the crack overlay, textured break and hit particles,
// break and hit sounds, the swing clock and the break pop. Presentation only, like the view that owns it: it reads
// the authority's events, the block world and the local player's interaction snapshot, and never changes gameplay
// state. Each effect has a foundation.Break.* switch; 0 keeps the view's own original path, so with every switch at 0
// nothing here draws, plays or loads.
namespace FoundationBreak
{
    // The switches, read once per frame.
    struct FSwitches
    {
        // 0 the view's outline, 1 vanilla (black 40%, 3 px at 1920 wide scaled by width/1920, the shape's outer edges),
        // 2 thinner (black 30%, 1 px, every box's 12 edges), 3 none.
        int32 Outline = 0;
        // 0 the view's crack lines, 1 vanilla's stages, 2 carved.
        int32 Cracks = 0;
        bool bCrackSmooth = false;
        // 0 the view's cubes, 1 textured break particles and hit chips.
        int32 Particles = 0;
        bool bParticleCollision = false;
        bool bParticleSpray = false;
        // A resting particle falls again when its support turns to air. Particles 1 only, so not in AnyOn.
        bool bParticleSupport = false;
        // The particles' life (ParticleLife below). Particles 1 only, so not in AnyOn.
        int32 ParticleLife = 0;
        // 0 silent, 1 vanilla, 2 vanilla with hit pitch variety and at most two hit voices.
        int32 Sounds = 0;
        // 0 the view's swing, 1 a 6-tick swing with hits every 4 steps, 2 hits on each swing's contact tick.
        int32 Swing = 0;
        bool bPop = false;
        // foundation.Place.Sound: 0 silent, 1 the placed block's place sound (its break's volume and pitch, as
        // vanilla).
        int32 PlaceSound = 0;

        bool AnyOn() const;
    };
    FSwitches ReadSwitches();

    constexpr int32 Stages = 10;
    // T_BreakCracks: carved stages in layers 0-9, vanilla's own in PlainLayer + stage.
    constexpr int32 PlainLayer = 10;
    constexpr uint8 NeutralGrey = 128;
    constexpr uint8 CrackGrey = 60, LitGrey = 170, ShadowGrey = 90;
    constexpr double TexelsPerFace = 16;

    // Stage = clamp(floor(progress x 10), 0, 9): stage 0 shows from the first mining tick.
    int32 CrackStage(double Progress);
    // The smooth growth's blend toward the next stage: fract(progress x 10), 0 at the last stage.
    double CrackFade(double Progress);
    // Section 4's carved look on one 16 x 16 stage (Crack: alpha > 127, row-major, y down). A crack texel is 60; the
    // texel just right of or below a crack, if not a crack itself, is 170 (lit side, light from the up-left in face
    // texture space); the texel just left of or above one is 90 (shadow side), and shadow wins where both apply.
    // Everything else is 128 (unchanged under the 2x multiply). Neighbours past the border do not wrap.
    void CarveStage(TConstArrayView<bool> Crack, TArrayView<uint8> OutGrey);
    // Vanilla's own stage as greys: a crack texel keeps its grey (Grey, row-major), every other texel is 128.
    void PlainStage(TConstArrayView<bool> Crack, TConstArrayView<uint8> Grey, TArrayView<uint8> OutGrey);

    // The menu's "particles" pref: All 1, Decreased 0.67, Minimal 0.33 (anything else counts as All).
    double ParticleAmount(const FString& Pref);
    // Break particles per outline box: round(64 x amount), so 64, 43 and 21.
    int32 BreakParticlesPerBox(double Amount);
    // Hit chips: one per hit, off at Minimal only.
    bool HitChipsOn(double Amount);

    constexpr double StepSeconds = 1. / 20;
    constexpr int32 SwingSteps = 6;
    constexpr int32 ContactStep = 3;
    constexpr int32 HitSteps = 4;
    // Whether a hit (its sound and chip) fires at Step. Swing 2: each swing's contact tick, SwingStart + 3 + 6k.
    // Otherwise vanilla's measured cadence: every 4 steps from the mining session's first step.
    bool IsHitStep(int32 SwingMode, int64 MiningStart, int64 SwingStart, int64 Step);
    // The hit steps in (AfterStep, UpToStep], at most MaxCatchUp of the latest (a long hitch never bursts hits).
    constexpr int32 MaxCatchUp = 4;
    void CollectHitSteps(int32 SwingMode, int64 MiningStart, int64 SwingStart, int64 AfterStep, int64 UpToStep,
        TArray<int64, TInlineAllocator<4>>& Out);

    // The swing clock S4's held-item pose reads: a press starts a 6-tick swing; while the attack is held on a block
    // (mining, or the delay between breaks) it repeats with no rest. Counted in reference steps, not frame time.
    struct FSwingClock
    {
        int64 StartStep = MIN_int64 / 2;
        int32 DurationSteps = SwingSteps;
        bool bRepeating = false;
        // Phase in [0, 1) at Step plus Alpha of the next step, or -1 when no swing runs.
        double GetPhase(int64 Step, double Alpha) const;
    };

    // The break pop's scale over its 80 ms: 1 - t^2, t = Seconds / 0.08 clamped to [0, 1].
    constexpr double PopSeconds = .08;
    double PopScale(double Seconds);

    // World size of Pixels screen pixels at view depth DepthCm, for a horizontal field of view and a viewport width.
    double PixelsToWorld(double Pixels, double DepthCm, double HorizontalFovDegrees, double ViewportWidth);

    // Outline 1's width rule: BasePixels x max(1, ViewportWidth / 1920). Vanilla measured 3 px at 1920 x 1080 (VP1);
    // the scaling above 1920 is an assumption until a 1440p vanilla measurement.
    constexpr double OutlineReferenceWidth = 1920;
    double OutlinePixels(double BasePixels, double ViewportWidth);

    // One outer edge of a shape, from A to B along one axis (B the larger end).
    struct FOutlineEdge
    {
        FVector A = FVector::ZeroVector, B = FVector::ZeroVector;
    };
    // The outer edges of the union of axis-aligned boxes. A grid of every box's distinct coordinates per axis (merged
    // within 1e-3 cm); a cell is solid when its centre is inside a box. A grid segment is an edge when the four cells
    // around it hold 1 or 3 solid ones, or 2 on a diagonal; consecutive edge segments on one line join into one edge.
    // A cube, a slab or two stacked half slabs give 12; a straight stair 18.
    TArray<FOutlineEdge> MergeOutlineEdges(TConstArrayView<FBox> Boxes);

    // The outline strips' mesh: /Engine/BasicShapes/Cube, 100 cm.
    constexpr double OutlineStripMeshCm = 100;
    // An outline strip's instance transform: Length along Axis, a square cross-section across it, centred on Centre.
    // The square's side is w / k, w = PixelsToWorld(Pixels) at Centre's view depth and k = (|r_U| + |r_V|) / |r_UV|
    // (r = Centre - Camera, U and V the other two axes): a square seen at angle phi about its axis projects
    // side x (|cos phi| + |sin phi|) wide, so the drawn silhouette is Pixels wide from any side.
    FTransform OutlineStrip(const FVector& Centre, int32 Axis, double Length, const FVector& Camera,
        const FVector& Forward, double Pixels, double HorizontalFovDegrees, double ViewportWidth);

    // One play: a file of the event, its volume and pitch. Break: 1.0 and 0.8, hit: 0.25 and 0.5, each times the
    // sound type's own volume and pitch and the file's (sounds.json). Sounds 2 adds +-3% to a hit's pitch.
    struct FSoundPlay
    {
        int32 File = INDEX_NONE;
        float Volume = 0, Pitch = 0;
    };
    FSoundPlay PickSound(const FFoundationBreakBlock& Block, const FFoundationBreakSoundEvent& Event, bool bHit,
        int32 SoundsMode, FRandomStream& Random);
    // One play of a sound event (DA_Sounds): a file at random (every entry of the reference weighs 1), the caller's
    // volume and pitch times the file's own (sounds.json).
    FSoundPlay PickEventSound(const FFoundationSoundEvent& Event, float Volume, float Pitch, FRandomStream& Random);

    // A spawned particle, in centimetres and seconds. Patch: the texel offset (0-12) of its 4 x 4 patch.
    struct FParticleSpawn
    {
        FVector Location = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
        double Life = 0, Size = 0;
        uint8 PatchU = 0, PatchV = 0;
    };
    constexpr double GravityCm = 1600;
    constexpr double BreakSizeCm = 10, HitSizeCm = 6;
    // The per-axis velocity spread, 0.3 blocks/s, so the particles do not land in the spawn grid (Jesus, 2026-10-10;
    // the first spec had 0.1).
    constexpr double JitterCm = 30;
    // A particle's life (foundation.Break.ParticleLife), one draw of Random in every mode. 0: 1.5-2.3 s uniform (until
    // 2026-10-13). 1 and 2: 0.2 s / (0.1 + 0.9u), so 0.2-2.0 s, median 0.36 s, 1 in 6 past 0.8 s: a fit to VP1's
    // black-box counts (9 fragments left at 0.8 s at All, the last gone at about 2 s), unverified, since VP1 could not
    // measure the spawn count or single lives.
    double ParticleLife(int32 LifeMode, FRandomStream& Random);
    // The quad's scale at Age: life 1 shrinks it to 0 over the last ShrinkSeconds; 0 and 2 pop out at full size, as
    // vanilla.
    constexpr double ShrinkSeconds = .1;
    double LifeScale(int32 LifeMode, double Age, double Life);
    // A 4 x 4 x 4 grid inside Box, BreakParticlesPerBox(Amount) of its points picked at random: velocity
    // (point - centre) x 1.5 + uniform +-JitterCm, plus 1 up (blocks/s), plus Push (cm/s); ParticleLife(LifeMode).
    void SpawnBreakParticles(const FBox& Box, double Amount, const FVector& Push, FRandomStream& Random,
        TArray<FParticleSpawn>& Out, int32 LifeMode = 0);
    // A hit chip at the hit point: 0.4 blocks/s out of the face (0.6 with spray), then gravity.
    FParticleSpawn SpawnHitChip(
        const FVector& Point, const FVector& Normal, bool bSpray, FRandomStream& Random, int32 LifeMode = 0);
    // P4's push for a break: 1.5 blocks/s along the mined face's normal (none for a pop) and 0.5 away from the camera.
    FVector SprayPush(const FVector& FaceNormal, const FVector& AwayFromCamera);
    // Eating crumbs (step 10b; value report, Eating / Crumb particles and duration, eating.crumbs): 5 item particles
    // per periodic effect, 16 at completion, with velocity ((U - 0.5) x 0.1, U x 0.1 + 0.1, 0) blocks per step in the
    // view's space (across, up, forward), turned with the view, then 0.05 up. Provisional (the report gives none): the
    // spawn point, 0.8 blocks ahead of the eye, (U - 0.5) x 0.3 across and 0.3 to 0.9 below, turned with the view; half
    // a hit chip's size; a hit chip's life (ParticleLife); none drawn nearer the camera than CrumbNearCm.
    constexpr int32 CrumbsPerEffect = 5, CrumbsAtCompletion = 16;
    constexpr double CrumbSizeCm = HitSizeCm / 2, CrumbForwardBlocks = .8, CrumbNearCm = 40;
    FParticleSpawn SpawnCrumb(const FVector& EyeCm, const FRotator& View, FRandomStream& Random, int32 LifeMode = 0);

    // A lit furnace's, smoker's or blast furnace's display callback (T1 step 8b2b; survival2.r15.particles,
    // survival2.r15.exact_callback_chances), in R15's block units: the furnace one smoke and one flame, the smoker and
    // the blast furnace one smoke each. The furnace and blast furnace spawn on the front, the cell's centre + facing x
    // 0.52, lateral U[-0.3, 0.3) along the face and y U[0, 6/16) (the blast furnace 9/16); the smoker at (0.5, 1.1,
    // 0.5). The velocity is uniform per axis within R15's constructed bounds (blocks per tick, in world axes, as
    // measured), the life uniform in whole ticks (smoke 8-39, flame 12-43). Provisional (provisional-values.md, T1
    // 8b2b): the uniform shapes and the quad sizes.
    enum class EFurnaceBlock : uint8
    {
        Furnace,
        Smoker,
        BlastFurnace
    };
    enum class EFurnaceParticle : uint8
    {
        Smoke,
        Flame
    };
    struct FFurnaceParticleSpawn
    {
        EFurnaceParticle Kind = EFurnaceParticle::Smoke;
        FParticleSpawn Spawn;
    };
    constexpr double FurnaceSmokeSizeCm = 12, FurnaceFlameSizeCm = 8;
    void SpawnFurnaceParticles(EFurnaceBlock Block, const FBox& CellBox, EFoundationFace Facing, FRandomStream& Random,
        TArray<FFurnaceParticleSpawn>& Out);
    // A particle's layer in T_FurnaceParticles at Age of Life: the smoke steps through layers 0-7
    // (particles/smoke.json's order, generic_7 to generic_0) in 8 equal parts of its life (provisional); the flame is
    // layer 8.
    constexpr int32 FurnaceSmokeFrames = 8, FurnaceFlameLayer = 8;
    int32 GetFurnaceParticleLayer(EFurnaceParticle Kind, double Age, double Life);

    // One particle's flight for DeltaTime: gravity, and with collision it lands on a solid box and lies there.
    // Solid returns the collision boxes (world centimetres) of the cell holding a point. With bSupport a resting
    // particle checks the point just under it every step and falls again once no box holds it (its block was broken).
    struct FParticleState
    {
        FVector Location = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
        bool bResting = false;
    };
    void StepParticle(FParticleState& Particle, double DeltaTime, bool bCollision,
        TFunctionRef<void(const FVector& Point, TArray<FBox>& OutBoxes)> Solid, bool bSupport = false);
    // The flight after the spawn (provisional, T1 8b2b): no gravity and no collision; the velocity keeps
    // FurnaceDragPerTick of itself each tick and the smoke gains FurnaceSmokeRisePerTick blocks per tick upward each
    // tick.
    constexpr double FurnaceDragPerTick = .96, FurnaceSmokeRisePerTick = .001;
    void StepFurnaceParticle(FParticleState& Particle, EFurnaceParticle Kind, double DeltaTime);

    // What a sound would play, for tests and probes (the probes run with -nosound).
    struct FSoundLogEntry
    {
        double Time = 0;
        FName Event;
        FName File;
        float Volume = 0, Pitch = 0;
        FVector Location = FVector::ZeroVector;
        bool bHit = false;
        bool bPlace = false;
        // Whether the file's wave is loaded, so it plays where there is an audio device (Codex review 11, finding 3).
        bool bLoaded = false;
    };
}

// The breaking effects of one UFoundationInteractionView. A plain helper the view owns: every component and material
// it makes is registered on the view's owner and kept in the view's Owned list; all are made in Initialize, hidden.
// The break assets (/Game/Breaking) load the first frame any switch is on.
class UNNAMED_API FFoundationBreakEffects
{
public:
    explicit FFoundationBreakEffects(UFoundationInteractionView& InView);

    // From the view's BeginPlay, after its block assets: the pools.
    void Initialize(const FFoundationAuthority& Authority);
    // First thing in the view's tick: the switches, the menu prefs and the camera; loads the assets on first use.
    void BeginFrame(const FFoundationAuthority& Authority);
    const FoundationBreak::FSwitches& GetSwitches() const { return Switches; }

    // A block target's outline boxes for this frame (from DrawSelection). True when the effect draws them (or Outline 3
    // hides them), so the view skips its own lines; false without M_BreakOutline, so the view's lines stay.
    bool SubmitOutline(TConstArrayView<FBox> Boxes);
    // Every frame, before the view draws its cracks: the local player's interaction (null without one). It advances
    // the swing clock (a new PressSerial starts a swing at Interaction->Step), fires the hits of the steps since the
    // last frame (sounds, chips) and places the cracks. Cracks show only when bVisible; hits follow the reference steps
    // either way. True when the effect can draw the cracks (a switch is on and the assets and material loaded), so the
    // view skips its own lines.
    bool UpdateMining(const FFoundationAuthority& Authority, const FFoundationInteraction* Interaction, bool bVisible,
        double DeltaTime);
    // A Broken or Popped event: particles, sound and pop. True when the particles can draw, so they replace the view's
    // cube burst.
    bool OnBlockRemoved(const FFoundationAuthority& Authority, const FFoundationEvent& Event);
    // A Placed event: the placed block's place sound (foundation.Place.Sound).
    void OnBlockPlaced(const FFoundationAuthority& Authority, const FFoundationEvent& Event);
    // A sound event of DA_Sounds (/Game/Sounds, Reference/minecraft-sounds-26.3), such as "entity.item.pickup", at
    // Location with the caller's Volume and Pitch (vanilla's call-site values; pickup: 0.2 and a random 1.6-3.4), the
    // event's attenuation, logged like the block sounds. DA_Sounds loads on the first call. Silent for an unknown event
    // or with foundation.Sound.Events 0.
    void PlayEvent(FName Event, const FVector& Location, float Volume = 1, float Pitch = 1);
    // Eating crumbs (FoundationBreak::SpawnCrumb): Count particles of Item's sprite (T_ItemTextures) from the eye,
    // turned with View. False when they cannot draw (no M_BreakParticle, item sprites or 16 x 16 sprite for Item).
    bool SpawnCrumbs(FName Item, const FVector& EyeCm, const FRotator& View, int32 Count);
    // A lit furnace's display callback (FoundationBreak::SpawnFurnaceParticles): its smoke and flame in a pool of their
    // own, drawn with M_FurnaceParticle (T_FurnaceParticles, ImportFurnaceParticles.py). False when they cannot draw
    // (no M_FurnaceParticle).
    bool SpawnFurnaceParticles(FoundationBreak::EFurnaceBlock Block, const FBox& CellBox, EFoundationFace Facing);
    // The held item's swing (0-1, today's pose curve sin(phase x pi)) from the swing clock, when Swing is on; false
    // keeps the view's own swing.
    bool GetSwing(double& OutSwing) const;
    // Last thing in the view's tick: the outline instances, the particles' flight and the pop.
    void EndFrame(const FFoundationAuthority& Authority, double DeltaTime);

    // Probes and tests.
    const FoundationBreak::FSwingClock& GetSwingClock() const { return Swing; }
    TConstArrayView<FoundationBreak::FSoundLogEntry> GetSoundLog() const { return SoundLog; }
    void ClearSoundLog() { SoundLog.Reset(); }
    int32 GetLiveParticleCount() const;
    // Eating crumbs alive now, and spawned since the effects began.
    int32 GetLiveCrumbCount() const;
    int32 GetCrumbsSpawned() const { return CrumbsSpawned; }
    // Furnace smoke and flames alive now, and spawned since the effects began.
    int32 GetLiveFurnaceParticleCount() const;
    int32 GetFurnaceParticlesSpawned() const { return FurnaceParticlesSpawned; }
    // Live particles inside Box (the probe's count of the ones left on a broken block's top face).
    int32 CountLiveParticles(const FBox& Box) const;
    bool IsCrackVisible() const { return bCrackVisible; }
    bool IsOutlineVisible() const { return bOutlineVisible; }
    bool IsPopVisible() const;
    bool HasAssets() const { return Assets != nullptr; }
    // Break particles the latest OnBlockRemoved spawned (0 when it spawned none).
    int32 GetLastBreakSpawnCount() const { return LastBreakSpawnCount; }
    // True when the last drawn outline used the high-contrast look.
    bool IsHighContrastOutline() const { return bDrawnHighContrast; }

private:
    struct FParticle
    {
        FoundationBreak::FParticleState State;
        double Age = 0, Life = 0, Size = 0;
        bool bLive = false;
    };
    struct FPop
    {
        TArray<UInstancedStaticMeshComponent*, TInlineAllocator<4>> Parts;
        FVector Pivot = FVector::ZeroVector;
        double Age = 0;
        bool bLive = false;
    };

    void LoadAssets(const FFoundationAuthority& Authority);
    UWorld* GetWorld() const;
    // A state's meshes as the view draws it (AddInstance): one per part of a model state (its picked alternative), or
    // the shape's mesh kind; each placed relative to the cell's minimum corner (a model's position offset included).
    void GetStateParts(const FFoundationAuthority& Authority, const FIntVector& Cell, FFoundationBlockState State,
        TArray<UStaticMesh*, TInlineAllocator<4>>& OutMeshes, TArray<FTransform, TInlineAllocator<4>>& OutPlaces);
    // The particle texture's layer in T_BlockTextures, and the packed tint (0xFFFFFF: none) for a state.
    void GetParticleLook(
        const FFoundationAuthority& Authority, FFoundationBlockState State, float& OutLayer, float& OutTint) const;
    const FFoundationBreakBlock* FindBreakBlock(
        const FFoundationAuthority& Authority, FFoundationBlockState State) const;
    // A block's break, hit or place event (bPlace wins over bHit), logged and played at Location.
    void PlaySound(const FFoundationAuthority& Authority, FFoundationBlockState State, const FVector& Location,
        bool bHit, bool bPlace = false);
    void Spawn(const FoundationBreak::FParticleSpawn& Spawn, float Layer, float Tint);
    // A particle into a pool (the chips' or the crumbs'), and one frame of a pool's flight.
    static void SpawnInto(UInstancedStaticMeshComponent& Mesh, TArray<FParticle>& Pool,
        TConstArrayView<FPrimitiveInstanceId> Ids, int32& Next, const FoundationBreak::FParticleSpawn& New, float Layer,
        float Tint);
    // NearCm: a live particle nearer the camera than this is drawn at zero scale.
    void UpdatePool(UInstancedStaticMeshComponent* Mesh, TArray<FParticle>& Pool,
        TConstArrayView<FPrimitiveInstanceId> Ids, const FFoundationAuthority& Authority, double DeltaTime,
        double NearCm = 0);
    void UpdateCracks(const FFoundationAuthority& Authority, const FFoundationInteraction& Mining);
    void HideCracks();
    void DrawOutlineStrips();
    void UpdateParticles(const FFoundationAuthority& Authority, double DeltaTime);
    void UpdateFurnaceParticles(double DeltaTime);
    void UpdatePops(double DeltaTime);

    UFoundationInteractionView& View;
    FoundationBreak::FSwitches Switches;
    double Amount = 1;
    bool bHighContrast = false;
    FVector CameraLocation = FVector::ZeroVector;
    FVector CameraForward = FVector::ForwardVector;
    double CameraFov = 90, ViewportWidth = 1280;
    const UFoundationBreakAssets* Assets = nullptr;
    bool bTriedAssets = false;
    // DA_Blocks' texture names -> their first layer.
    TMap<FName, int32> TextureLayers;
    TMap<FName, int32> BlockIndex;

    // Outline: halo and core strips (translucent, one instance per strip).
    UInstancedStaticMeshComponent* OutlineHalo = nullptr;
    UInstancedStaticMeshComponent* OutlineCore = nullptr;
    // Outline 1's after-tone-mapping path (foundation.Break.OutlineTonemapped): the core strips in custom depth only,
    // and the post-process that darkens the final image where they show (M_BreakOutlineShade).
    UInstancedStaticMeshComponent* OutlineMask = nullptr;
    UPostProcessComponent* OutlineShade = nullptr;
    TArray<FBox> OutlineBoxes;
    bool bOutlineSubmitted = false, bOutlineVisible = false, bDrawnHighContrast = false;
    // The colour each strip component last got (NaN: none yet), so the per-instance data is written only on a change.
    FLinearColor LastHaloColor = FLinearColor(NAN, NAN, NAN, NAN), LastCoreColor = FLinearColor(NAN, NAN, NAN, NAN);
    int32 LastBreakSpawnCount = 0;

    // Cracks: one modulate overlay per part (as many as the held block's pool).
    TArray<UStaticMeshComponent*> CrackParts;
    UMaterialInstanceDynamic* CrackMaterial = nullptr;
    bool bCrackVisible = false;
    FFoundationBlockState CrackState;
    FIntVector CrackCell = FIntVector(MAX_int32);

    // Mining session and hits.
    bool bWasMining = false;
    FIntVector MiningCell = FIntVector(MAX_int32);
    EFoundationFace MinedFace = EFoundationFace::None;
    int64 MiningStart = 0, LastHitStep = MIN_int64 / 2;
    // The local player's last step and how far into the next one this frame is.
    int64 LastStep = MIN_int64 / 2;
    double StepAlphaSeconds = 0;
    uint32 LastPressSerial = 0;
    FoundationBreak::FSwingClock Swing;

    // Particles: a fixed pool of instances in one component (camera-facing quads).
    static constexpr int32 MaxParticles = 1024;
    UInstancedStaticMeshComponent* ParticleMesh = nullptr;
    TArray<FParticle> Particles;
    TArray<FPrimitiveInstanceId> ParticleIds;
    int32 NextParticle = 0;
    // Eating crumbs: M_BreakParticle with the item sprites (T_ItemTextures) for its BlockTextures, a pool of their own;
    // the material is made on the first crumb. Each item's sprite layer (INDEX_NONE: none) once looked up.
    static constexpr int32 MaxCrumbs = 256;
    UMaterialInterface* ParticleBaseMaterial = nullptr;
    UInstancedStaticMeshComponent* CrumbMesh = nullptr;
    TArray<FParticle> Crumbs;
    TArray<FPrimitiveInstanceId> CrumbIds;
    int32 NextCrumb = 0, CrumbsSpawned = 0;
    bool bTriedCrumbMaterial = false, bCrumbMaterial = false;
    TMap<FName, int32> CrumbLayers;
    // Its own stream, so crumbs never shift the chips' draws.
    FRandomStream CrumbRandom;
    // Furnace smoke and flames: M_FurnaceParticle (loaded on the first spawn), a pool of their own with each particle's
    // kind and drawn layer (the custom data is written when the layer changes), and their own stream.
    static constexpr int32 MaxFurnaceParticles = 512;
    UInstancedStaticMeshComponent* FurnaceMesh = nullptr;
    TArray<FParticle> FurnaceParticles;
    TArray<FoundationBreak::EFurnaceParticle> FurnaceKinds;
    TArray<int32> FurnaceLayers;
    TArray<FPrimitiveInstanceId> FurnaceIds;
    int32 NextFurnaceParticle = 0, FurnaceParticlesSpawned = 0;
    bool bTriedFurnaceMaterial = false, bFurnaceMaterial = false;
    FRandomStream FurnaceRandom;

    // Pops: two slots, each with as many parts as the held block's pool.
    static constexpr int32 PopSlots = 2;
    TArray<FPop> Pops;

    FRandomStream Random;
    // DA_Sounds and its event names -> indices; its own stream, so playing events never shifts the block sounds' draws.
    const UFoundationSoundAssets* SoundAssets = nullptr;
    bool bTriedSoundAssets = false;
    TMap<FName, int32> SoundEventIndex;
    FRandomStream EventRandom;
    TArray<FoundationBreak::FSoundLogEntry> SoundLog;
    static constexpr int32 MaxSoundLog = 4096;
};
