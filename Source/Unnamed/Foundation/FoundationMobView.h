#pragma once

#include "CoreMinimal.h"
// FoundationAuthority.h needs it first.
#include "Containers/StaticArray.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Foundation/FoundationAuthority.h"

class AFoundationPawn;
class UFoundationInteractionView;
class UInstancedStaticMeshComponent;
class UStaticMesh;
struct FFoundationMobModelAsset;
struct FFoundationMobPartAsset;

// The mobs' look (survival T6 20a): presentation only, like the view that owns it. Each mob is its vanilla box model
// (DA_Mobs, from Reference/minecraft-entities-26.3/facts/entity_models.json): one transient mesh per (mob type, layer,
// part) built at the first draw, drawn by one instanced component per part, one instance per mob within DrawCm, between
// the last two reference steps. 1 model pixel is 6.25 cm; the model's y points down and its feet stand at y 24. On top
// of the rest pose: the legs' swing and the head's turn [survival.r10.model_motion], the chicken's wings and the
// sheep's grazing head [survival.r10.idle_and_sun] (20b; the sheep's wool is a second layer over its main one), the
// death roll [survival.r10.death_roll] and the red overlay while HurtTime or DeathTime > 0 [hurt.overlay]. A frozen mob
// is hidden. The sound hook plays the authority's MobSound events at pitch 1 + 0.2 (U - V) [survival.r5.play_calls].
class FFoundationMobView
{
public:
    explicit FFoundationMobView(UFoundationInteractionView& InView) : View(InView) {}

    void Update(const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, double Alpha);
    // A MobSound event (the view's HandleEvents).
    void OnSound(const FFoundationEvent& Event);
    // Probes: mobs drawn this frame and the overlay value each got; the sound hook calls.
    int32 GetDrawnCount() const { return Drawn; }
    // Of those, the mobs drawn with at least one mesh and a mesh for every part that has faces (probes: a model loaded
    // without geometry is not whole).
    int32 GetWholeCount() const { return Whole; }
    const TArray<float>& GetDrawnOverlays() const { return Overlays; }
    int32 Sounds = 0;
    FString Describe() const;

    // Provisional, as the item entities: mobs farther than this are not drawn; at most MaxMobs per type.
    static constexpr double DrawCm = 6400;
    static constexpr int32 MaxMobs = 128;

    // Model pixels (Mojang: x to the mob's left, y down, z behind its head) to Unreal cm in the mob's frame (X ahead,
    // Y to its right, Z up), feet at 0: (-z, -x, 24 - y) x 6.25.
    static FVector ToUnreal(const FVector& Pixels, bool bPoint = true);
    // A part's local transform in the mob's frame: its pivot (moved by OffsetPixels, Mojang axes), then its rotation
    // (radians, Mojang's Z, then Y, then X) plus the animation's.
    static FTransform MakePartTransform(const FFoundationMobPartAsset& Part, const FVector& ExtraRotation,
        const FVector& OffsetPixels = FVector::ZeroVector);
    // The part's mesh vertices: each face's corners in Unreal cm about the pivot (tests read them).
    static void GetPartQuads(
        const FFoundationMobPartAsset& Part, TArray<FVector>& OutPositions, TArray<FVector2D>& OutUVs);
    // The mob's root in the world: its interpolated feet, yaw and death roll.
    static FTransform MakeRootTransform(const FFoundationMob& Mob, const FVector& OriginCm, double Alpha);
    // The animation's added rotation for a part of a mob (radians, Mojang axes), interpolated by Alpha, and its pivot's
    // move (model pixels): the quadruped's and the chicken's legs, the chicken's wings, the head's look and the sheep's
    // grazing.
    static FVector GetPartAnimation(
        const FFoundationMob& Mob, FName Part, double Alpha, FVector* OutOffsetPixels = nullptr);

private:
    struct FPartDraw
    {
        UInstancedStaticMeshComponent* Component = nullptr;
        TArray<FPrimitiveInstanceId> Ids;
        int32 Shown = 0;
    };
    // One model layer of a mob type: the main one, then those drawn over it (the sheep's wool and undercoat, tinted
    // by its colour: bWool, bUndercoat).
    struct FLayerDraw
    {
        const FFoundationMobModelAsset* Model = nullptr;
        bool bWool = false, bUndercoat = false;
        TArray<FPartDraw> Parts;
    };
    struct FTypeDraw
    {
        TArray<FLayerDraw> Layers;
        // The mob each slot drew last frame (its id), for the parts' motion vectors.
        TArray<int32> SlotMobs;
        bool bTried = false;
    };
    // 20e1: one draw per type and variant (FoundationMobFarm::GetModelId's model), at Type x VariantCount + Variant.
    static constexpr int32 VariantCount = 3;
    bool Prepare(EFoundationMobType Type, EFoundationMobVariant Variant);

    UFoundationInteractionView& View;
    TStaticArray<FTypeDraw, int32(EFoundationMobType::Count) * VariantCount> Types;
    int32 Drawn = 0, Whole = 0;
    TArray<float> Overlays;
};
