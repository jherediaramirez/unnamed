#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Foundation/FoundationAuthority.h"
#include "FoundationInteractionView.generated.h"

class AFoundationLab;
class AFoundationPawn;
class FFoundationBreakEffects;
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
    bool IsHeldItemVisible() const;
    // The held item's extruded sprite (items S4): its sprite ("item/iron_pickaxe"; None for blocks and the empty
    // hand) and component, child of the hand pivot.
    FName GetHeldSprite() const { return HeldSprite; }
    const UStaticMeshComponent* GetHeldItem() const { return HeldItem; }
    // Centimetres per model block (16 pixels) in the hand's space, which is scaled down so the items keep the size of
    // the old cube tools at the pivot. Provisional, like the pivot: probe P1 fits both.
    static constexpr double HeldModelBlockCm = 50;
    double GetLastViewMs() const { return LastViewMs; }
    // The breaking effects (foundation.Break.*; null before BeginPlay).
    FFoundationBreakEffects* GetBreakEffects() const { return BreakEffects.Get(); }

private:
    friend class FFoundationBreakEffects;
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
    // M_HeldItem with the 16 or 32 pixel sprite array, made on first use; null before CreateItemMaterials.py ran.
    UMaterialInterface* GetItemMaterial(const class UFoundationItemAssets& Items, bool b32);

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
    // foundation.Held.Glow as last set on ItemMaterials.
    float AppliedGlow = -1;
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
    TArray<FTargetVisual> TargetVisuals;
    int64 LastEvent = 0;
    int64 DrawnRevision = 0;
    uint32 LastPressSerial = 0;
    double SwingAge = 10, MiningSwing = 0;
    bool bOutlineVisible = false, bPreviewVisible = false;
    double LastViewMs = 0;
    TWeakObjectPtr<AFoundationPawn> HeldOwner;
    TSharedPtr<FFoundationBreakEffects> BreakEffects;
};
