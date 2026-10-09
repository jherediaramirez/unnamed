#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Foundation/FoundationAuthority.h"
#include "FoundationInteractionView.generated.h"

class AFoundationLab;
class AFoundationPawn;
class UInstancedStaticMeshComponent;
class ULineBatchComponent;
class UFoundationBlockTextureTable;
class UMaterialInstanceDynamic;
class UStaticMesh;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

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

    int32 GetInstanceCount() const;
    int32 GetSectionMeshCount() const { return Sections.Num(); }
    // Block animation: the texture table every block material samples (null until the block assets load).
    UFoundationBlockTextureTable* GetTextureTable() const { return TextureTable; }
    // Custom data floats per block instance: six face codes, the overlay code, the packed tint, the light level.
    static constexpr int32 CustomDataFloats = 9;
    static void MakeCustomData(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, TArrayView<float> Out);
    // The world revision whose changes are all drawn.
    int64 GetDrawnRevision() const { return DrawnRevision; }
    bool IsOutlineVisible() const { return bOutlineVisible; }
    bool IsPreviewVisible() const { return bPreviewVisible; }
    bool IsHeldItemVisible() const;
    double GetLastViewMs() const { return LastViewMs; }

private:
    struct FSectionKey
    {
        FIntVector Section;
        uint8 Mesh = 0;
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
        TMap<FIntVector, FPrimitiveInstanceId> Instances;
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
    UInstancedStaticMeshComponent* MakeBlockComponent(USceneComponent* Parent, int32 Mesh, int32 Layer);
    void AddInstance(const FFoundationBlockWorld& World, const FIntVector& Cell, FFoundationBlockState State);
    void RemoveInstance(const FIntVector& Cell);
    // Water draws full height under water; changes above a water cell redraw it.
    void RefreshWaterBelow(const FFoundationBlockWorld& World, const FIntVector& Cell);
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

    TMap<FSectionKey, FSectionMesh> Sections;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> SectionComponents;
    // Which section mesh each drawn cell is in.
    TMap<FIntVector, FSectionKey> DrawnKeys;
    // One runtime mesh per FoundationBlockMeshes kind, one material per render layer plus water.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> BlockMeshes;
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
    // The first-person held item: one pivot with a cube for blocks and simple tool shapes.
    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> HandPivot;
    // The held block: one textured instance, redrawn when the held state changes.
    UPROPERTY(Transient)
    TObjectPtr<UInstancedStaticMeshComponent> HeldBlock;
    FFoundationBlockState HeldState;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> HeldPickaxe;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> HeldSword;
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
};
