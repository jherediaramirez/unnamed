#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Foundation/FoundationAuthority.h"
#include "FoundationInteractionView.generated.h"

class AFoundationLab;
class AFoundationPawn;
struct FFoundationPreview;
class FFoundationBreakEffects;
class FFoundationExperienceOrbView;
class FFoundationMobView;
class FFoundationItemEntityView;
class UInstancedStaticMeshComponent;
class ULineBatchComponent;
class UFoundationBlockTextureTable;
class UMaterialInstanceDynamic;
class UStaticMesh;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
namespace VoxelFluidSurface
{
    struct FSurface;
}

// The destination preview's switch (foundation.Place.Preview): 0 none, as vanilla, the default; 1 the destination box,
// its fill and the placed and rejected flashes.
namespace FoundationPlace
{
    constexpr int32 DefaultPreviewMode = 0;
    UNNAMED_API int32 GetPreviewMode();
    // Whether the view draws a preview's destination box in a mode (only a placement has one).
    UNNAMED_API bool ShouldDrawPreview(int32 Mode, const FFoundationPreview& Preview);
}

// The held item's switch (foundation.Hand.Fit): 1 placed as vanilla's first-person frames show it (probe P1) and drawn
// at a fixed FOV whatever Options > FOV says, the default; 0 the provisional pose, drawn at the view's FOV.
namespace FoundationHand
{
    // The held item's vertical FOV under the fit: P1's, so its frames hold at any FOV setting.
    constexpr double FixedVerticalFov = 70;
    UNNAMED_API bool IsFitOn();
    // A held block's transform under the fit: block/block.json's firstperson_righthand display on the cube mesh.
    UNNAMED_API FTransform MakeHeldBlockDisplay();

    // The held item's eating motion (foundation.Hand.EatMotion): 1, the default, vanilla's eat transform below; 0 the
    // project's ease from step 10 (into a fixed pose in 0.1 s, a bob every 4 steps).
    UNNAMED_API int32 GetEatMotion();
    // Vanilla's eat transform of the main hand (value report, Eating / Hand motion, eating.hand_transform) as a
    // column-vector matrix in its view space (x right, y up, z toward the viewer; 1 is a block): M[i][j] is row i,
    // column j (the report's column-major element j x 4 + i). With f = Remaining + 1 (Remaining: the use's remaining
    // steps less the partial step) and p = f / Duration, an early jiggle 0.1 |cos(f pi / 4)| up while p < 0.8, then
    // the exponent-27 approach s = 1 - p^27: moved (0.6 s, -0.5 s, 0), turned 90 s about y, 10 s about x and 30 s
    // about z. A closed form fitted to the report's 33 right-hand matrices (within 2.1e-4, float rounding).
    UNNAMED_API FMatrix MakeEatMatrix(double Remaining, int32 Duration);
    // The hand pivot's view-space transform (cm, Unreal axes) while eating: the eat transform applied outside the
    // pivot, as vanilla's arm transform sits inside it (100 cm per unit, as the fitted pivot 71.9, 56.2, -52.2 is
    // vanilla's 0.72, 0.56, -0.52).
    UNNAMED_API FTransform MakeEatPivot(const FVector& PivotCm, double Remaining, int32 Duration);
}

// The held item's light (foundation.Held.Light): 1, the default, lights it by the light where the player stands, as
// vanilla's hand takes the light level at the player (block light and sky light, the brighter of the two), so it is
// dark in a dark cave and partly lit near a torch; 0 the fixed glow foundation.Held.Glow (items S4) whatever the light.
// Only in a generated world; the Playground keeps the fixed glow.
namespace FoundationHeldLight
{
    constexpr int32 DefaultMode = 1;
    // How often the level at the eye is probed, and how fast the shown level eases to it (levels per second).
    constexpr double ProbeSeconds = .25;
    constexpr double LevelsPerSecond = 30;
    // Vanilla's rendered brightness (display, 0 to 1) of a light level 0 to 15: the floor plus the curve
    // f / (4 - 3 f), f = level / 15, scaled to reach 1 at 15. The floor is the wiki's "full darkness is about 5%
    // brightness" (minecraft.wiki/w/Light, Rendered brightness; Overworld, lowest brightness setting); the curve is
    // unverified (no published source found; the wiki's anchor puts level 7 slightly above 25%, the curve gives 22%).
    constexpr double Floor = .05;
    UNNAMED_API double Brightness(double Level);
    // The held material's LightGlow for a level: the linear on-screen factor (the display brightness to the power
    // 2.2) x today's daylight factor (foundation.Held.Glow x the daylight exposure, 2^-(13.5 + DayEV)), so level 15
    // in daylight draws as the fixed glow did.
    UNNAMED_API double LightGlow(double Level, double Glow, double DayEV);
    UNNAMED_API int32 ReadMode();
}

// A lab's block-interaction presentation: the Playground's, and a generated world's (AVoxelWorld). It reads the
// block world's change log, the authority's events and targets and the local player's interaction snapshot; it never
// changes gameplay state (draining the change log is reading it). A generated world's chunks draw its blocks, so
// there the view leaves block drawing off and draws only the outline, cracks, held item and effects.
// Blocks draw as instances grouped by 16^3 section, mesh (cube, slab half, water height) and render layer, and
// are tracked by stable instance IDs, never by instance index. Each instance carries its state's face codes,
// overlay, tint and light as custom data (FoundationBlockFaces); the block materials pick each face's texture
// layer from them, so rotated states are remapped on the CPU and instances never rotate. Success effects fire
// once, from committed events; the hand swing on a press is a cosmetic prediction with nothing to reconcile.
UCLASS(ClassGroup = Foundation)
class UNNAMED_API UFoundationInteractionView : public UActorComponent
{
    GENERATED_BODY()
public:
    UFoundationInteractionView();
    // Set before registering. Blocks as instances from the change log (the Playground's blocks; generated worlds
    // draw theirs as chunks), and the Playground's fixtures: the jump-stack pad and the station labels.
    bool bDrawBlocks = true;
    bool bLabFixtures = true;
    virtual void BeginPlay() override;
    virtual void TickComponent(
        float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Every block instance: one per cube, slab, layer or fluid cell, one per part of a model state plus the water it
    // holds.
    int32 GetInstanceCount() const;
    // The cells drawn (a model state with no part, such as a vine with no face, is drawn with no instance).
    int32 GetDrawnCellCount() const { return DrawnKeys.Num(); }
    int32 GetSectionMeshCount() const { return Sections.Num(); }
    // Block animation: the texture table every block material samples (null until the block assets load).
    UFoundationBlockTextureTable* GetTextureTable() const { return TextureTable; }
    // Custom data floats per block instance: six face codes, the overlay code, the packed tint, the light level.
    static constexpr int32 CustomDataFloats = 9;
    static void MakeCustomData(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, TArrayView<float> Out);
    // A sloped fluid cell's custom data (water and sky W3b), as the voxel mesher codes its faces: MakeCustomData's,
    // with the flow texture on a top that runs downhill, and on flowing lava's sides (every lava side under
    // voxel.Fluids.SlopedTops 2).
    static void MakeFluidSurfaceData(const FFoundationBlockRegistry& Registry, FFoundationBlockState State,
        const VoxelFluidSurface::FSurface& Surface, int32 Mode, TArrayView<float> Out);
    // The world revision whose changes are all drawn.
    int64 GetDrawnRevision() const { return DrawnRevision; }
    bool IsOutlineVisible() const { return bOutlineVisible; }
    bool IsPreviewVisible() const { return bPreviewVisible; }
    // Flashes started so far (placed, rejected and interacted boxes); a flash outlives no probe case, so probes count.
    int32 GetStartedFlashCount() const { return StartedFlashes; }
    bool IsHeldItemVisible() const;
    // The held item's extruded sprite (items S4): its sprite ("item/iron_pickaxe"; None for blocks and the empty
    // hand) and component, child of the hand pivot.
    FName GetHeldSprite() const { return HeldSprite; }
    const UStaticMeshComponent* GetHeldItem() const { return HeldItem; }
    // The held item's eating pose, 0 (rest) to 1 (at the mouth), for probes.
    double GetEatBlend() const { return EatBlend; }
    // Centimetres per model block (16 pixels) in the hand's space, which is scaled down so the items keep the size of
    // the old cube tools at the pivot. Provisional, like the pivot: probe P1 fits both.
    static constexpr double HeldModelBlockCm = 50;
    double GetLastViewMs() const { return LastViewMs; }
    // The breaking effects (foundation.Break.*; null before BeginPlay).
    FFoundationBreakEffects* GetBreakEffects() const { return BreakEffects.Get(); }
    // The item entities' look (items S8a; null before BeginPlay).
    FFoundationItemEntityView* GetItemEntityView() const { return ItemEntityView.Get(); }
    // The experience orbs' look (survival step 12; null before BeginPlay).
    FFoundationExperienceOrbView* GetExperienceOrbView() const { return ExperienceOrbView.Get(); }
    FFoundationMobView* GetMobView() const { return MobView.Get(); }

private:
    friend class FFoundationBreakEffects;
    friend class FFoundationExperienceOrbView;
    friend class FFoundationMobView;
    friend class FFoundationItemEntityView;
    struct FSectionKey
    {
        FIntVector Section;
        // A FoundationBlockMeshes kind, or Count + a baked model.
        uint16 Mesh = 0;
        // EFoundationRenderLayer, or 3 for water.
        uint8 Layer = 0;
        bool operator==(const FSectionKey& Other) const = default;
        friend uint32 GetTypeHash(const FSectionKey& Key)
        {
            return HashCombine(GetTypeHash(Key.Section), uint32(Key.Mesh) << 8 | Key.Layer);
        }
    };
    struct FSectionMesh
    {
        UInstancedStaticMeshComponent* Mesh = nullptr;
        // A cell can hold several instances of one mesh (two parts of a state on the same model).
        TMultiMap<FIntVector, FPrimitiveInstanceId> Instances;
    };
    struct FParticle
    {
        UStaticMeshComponent* Mesh = nullptr;
        FVector Velocity = FVector::ZeroVector;
        double Age = 0, Life = 0, Size = 0;
    };
    struct FFloatingText
    {
        UTextRenderComponent* Text = nullptr;
        FVector Start = FVector::ZeroVector;
        double Age = 0, Life = 0;
    };
    struct FFlash
    {
        FBox Box;
        FLinearColor Color;
        double Age = 0, Life = 0;
        bool bCross = false;
    };
    struct FTargetVisual
    {
        UStaticMeshComponent* Body = nullptr;
        UMaterialInstanceDynamic* Material = nullptr;
        UTextRenderComponent* Label = nullptr;
        FString LastText;
        double HitAge = 10;
    };

    AFoundationLab* GetLab() const;
    AFoundationPawn* GetLocalPawn() const;
    UMaterialInstanceDynamic* MakeMaterial(FLinearColor Color, bool bSurface = false);
    UStaticMeshComponent* MakeCube(USceneComponent* Parent, FLinearColor Color, bool bSurface = false);
    void ApplyChanges(FFoundationAuthority& Authority);
    void RebuildBlocks(FFoundationAuthority& Authority);
    void LoadBlockAssets();
    // A FoundationBlockMeshes kind, or Count + a baked model, built on first use (null for a model with no quad), or a
    // sloped fluid surface's mesh (GetFluidSurfaceMesh).
    UStaticMesh* GetBlockMesh(const FFoundationBlockRegistry& Registry, int32 Mesh);
    // The mesh index of a sloped fluid surface's mesh, after the kinds and the baked models; built on first use.
    uint16 GetFluidSurfaceMesh(const FFoundationBlockRegistry& Registry, const VoxelFluidSurface::FSurface& Surface);
    UInstancedStaticMeshComponent* MakeBlockComponent(USceneComponent* Parent, UStaticMesh* Mesh, int32 Layer);
    void AddInstance(const FFoundationBlockWorld& World, const FIntVector& Cell, FFoundationBlockState State);
    void AddPart(const FSectionKey& Key, const FIntVector& Cell, const FTransform& Instance,
        TArrayView<const float> Data, const FFoundationBlockRegistry& Registry);
    void RemoveInstance(const FIntVector& Cell);
    // A fluid cell's look depends on the cell above it (full height under fluid) and, when it slopes, on its 3 x 3
    // ring and the cells above those: a change redraws the drawn fluid cells around it and below them (3 x 3 x 2).
    void RefreshFluidsAround(const FFoundationBlockWorld& World, const FIntVector& Cell);
    void HandleEvents(const FFoundationAuthority& Authority);
    // Lava and burning sounds (step 16) from the local pawn's fire counters.
    void PlayFireSounds(const AFoundationPawn* Pawn);
    // A lit furnace's, smoker's or blast furnace's sound near the local pawn (T1 step 8b, foundation.Furnace.Ambience).
    void PlayFurnaceSounds(const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, float DeltaTime);
    void AddFlash(const FBox& Box, FLinearColor Color, double Life, bool bCross = false);
    void Burst(const FVector& Center, FLinearColor Color, int32 Count, double Size, double Speed, uint32 Seed);
    void ShowNumber(const FVector& Location, const FString& Text, FColor Color);
    void UpdateTargets(const FFoundationAuthority& Authority, double DeltaTime);
    void UpdateLabels(const FFoundationAuthority& Authority);
    void UpdateEffects(double DeltaTime);
    void DrawSelection(const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, bool bVisible);
    void DrawBox(const FBox& Box, FLinearColor Color, float Thickness);
    void DrawCracks(const FFoundationAuthority& Authority, const struct FFoundationInteraction& Interaction);
    void UpdateHeldItem(const FFoundationAuthority& Authority, AFoundationPawn* Pawn, bool bVisible, double DeltaTime);
    // Shows an item's first-person model on the held item component (nothing for None or an item without one).
    void SetHeldItem(FName Id);
    // The meshes that draw a block state (its mesh kind, or one per part of its model, each part's first alternative)
    // and the box they draw in mesh space: the held block and item entities.
    void GetStateMeshes(const FFoundationBlockRegistry& Registry, FFoundationBlockState State,
        TArray<UStaticMesh*, TInlineAllocator<4>>& OutMeshes, FBox& OutDrawn);
    // The block material of a block's render layer (water's for water; opaque for none).
    UMaterialInterface* GetLayerMaterial(const FFoundationBlockType* Type) const;
    // A sprite's extruded mesh at HeldModelBlockCm per model block, built on first use; null without an opacity mask.
    UStaticMesh* GetItemMesh(const struct FFoundationItemTextureAsset& Texture);
    // M_HeldItem with the 16 or 32 pixel sprite array, made on first use; null before CreateItemMaterials.py ran.
    UMaterialInterface* GetItemMaterial(const class UFoundationItemAssets& Items, bool b32);
    // A new M_HeldItem instance on the 16 or 32 pixel sprite array (logged and null when either is missing).
    UMaterialInstanceDynamic* MakeItemMaterial(const class UFoundationItemAssets& Items, bool b32);

    TMap<FSectionKey, FSectionMesh> Sections;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> SectionComponents;
    // Which section meshes each drawn cell is in: one for cubes, one per part and held water for model states.
    TMap<FIntVector, TArray<FSectionKey, TInlineAllocator<2>>> DrawnKeys;
    // One runtime mesh per FoundationBlockMeshes kind and per baked model (built on first use), one material per
    // render layer plus water.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> BlockMeshes;
    // Sloped fluid surfaces' mesh indices in BlockMeshes, by FoundationBlockMeshes::GetFluidSurfaceKey.
    TMap<uint32, uint16> FluidSurfaceMeshes;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> LayerMaterials;
    UPROPERTY(Transient)
    TObjectPtr<UFoundationBlockTextureTable> TextureTable;
    UPROPERTY(Transient)
    TObjectPtr<ULineBatchComponent> Lines;
    UPROPERTY(Transient)
    TObjectPtr<ULineBatchComponent> GridLines;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> MiningOverlay;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> MiningMaterial;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UObject>> Owned;
    // The first-person held item: one pivot with the held block's parts and the held item.
    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> HandPivot;
    // The held block: one textured instance per part (a cube has one), redrawn when the held state changes. Made
    // with the pivot, as many as the model blocks' default states have parts.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> HeldBlocks;
    FFoundationBlockState HeldState;
    // Any other item: its sprite extruded (FoundationItemMesh), one mesh per sprite built on first use, placed by
    // its first-person display transform about the pivot.
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> HeldItem;
    FName HeldItemId;
    FName HeldSprite;
    // foundation.Hand.Fit as last applied to the pivot and the held blocks (-1: not yet).
    int32 AppliedHandFit = -1;
    // foundation.Held.Glow as last set on ItemMaterials.
    float AppliedGlow = -1;
    // foundation.Held.Light: LightGlow as last set on ItemMaterials, the level at the eye as last probed and as shown
    // (eased), the next probe's time, and whether the last frame ran it.
    float AppliedLightGlow = -1;
    double HeldLevelTarget = 15;
    double HeldLevel = 15;
    double HeldProbeDue = 0;
    bool bHeldLightLive = false;
    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UStaticMesh>> ItemMeshes;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> ItemMaterials;
    UPROPERTY(Transient)
    TObjectPtr<UTextRenderComponent> PresetLabel;
    UPROPERTY(Transient)
    TObjectPtr<UTextRenderComponent> SupplyLabel;

    // Fixed effect pools, made in BeginPlay: an effect never registers components mid-play.
    static constexpr int32 MaxParticles = 96;
    static constexpr int32 MaxNumbers = 12;
    TArray<FParticle> Particles;
    TArray<FFloatingText> Numbers;
    TArray<FFlash> Flashes;
    int32 StartedFlashes = 0;
    TArray<FTargetVisual> TargetVisuals;
    int64 LastEvent = 0;
    int64 DrawnRevision = 0;
    uint32 LastPressSerial = 0;
    double SwingAge = 10, MiningSwing = 0;
    // The held item's eating motion (step 10): 0-1 into the pose, and the bob's clock (EatMotion 0); the use step
    // last seen and the time since it changed, for the partial step (EatMotion 1).
    double EatBlend = 0, EatAge = 0;
    int32 EatTicks = -1;
    double EatTickAge = 0;
    bool bOutlineVisible = false, bPreviewVisible = false;
    double LastViewMs = 0;
    TWeakObjectPtr<AFoundationPawn> HeldOwner;
    // The fire counters last heard, and whose.
    TWeakObjectPtr<const AFoundationPawn> FireSoundOwner;
    int32 HeardLavaHurts = 0, HeardBurnHurts = 0, HeardExtinguishes = 0;
    // Seconds not yet spent on the furnaces' 20 Hz display ticks.
    float FurnaceSoundClock = 0;
    TSharedPtr<FFoundationBreakEffects> BreakEffects;
    TSharedPtr<FFoundationItemEntityView> ItemEntityView;
    TSharedPtr<FFoundationExperienceOrbView> ExperienceOrbView;
    TSharedPtr<FFoundationMobView> MobView;
};
