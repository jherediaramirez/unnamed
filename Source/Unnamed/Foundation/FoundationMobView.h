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
// (DA_Mobs, from Reference/minecraft-entities-26.3/facts/entity_models.json): one transient mesh per (mob type, part)
// built at the first draw, drawn by one instanced component per part, one instance per mob within DrawCm, between the
// last two reference steps. 1 model pixel is 6.25 cm; the model's y points down and its feet stand at y 24. On top of
// the rest pose: the legs' swing and the head's turn [survival.r10.model_motion], the death roll
// [survival.r10.death_roll] and the red overlay while HurtTime or DeathTime > 0 [hurt.overlay]. A frozen mob is hidden.
// The sound hook plays the authority's MobSound events at pitch 1 + 0.2 (U - V) [survival.r5.play_calls].
class FFoundationMobView
{
public:
    explicit FFoundationMobView(UFoundationInteractionView& InView) : View(InView) {}

    void Update(const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, double Alpha);
    // A MobSound event (the view's HandleEvents).
    void OnSound(const FFoundationEvent& Event);
    // Probes: mobs drawn this frame and the overlay value each got; the sound hook calls.
    int32 GetDrawnCount() const { return Drawn; }
    const TArray<float>& GetDrawnOverlays() const { return Overlays; }
    int32 Sounds = 0;
    FString Describe() const;

    // Provisional, as the item entities: mobs farther than this are not drawn; at most MaxMobs per type.
    static constexpr double DrawCm = 6400;
    static constexpr int32 MaxMobs = 128;

    // Model pixels (Mojang: x to the mob's left, y down, z behind its head) to Unreal cm in the mob's frame (X ahead,
    // Y to its right, Z up), feet at 0: (-z, -x, 24 - y) x 6.25.
    static FVector ToUnreal(const FVector& Pixels, bool bPoint = true);
    // A part's local transform in the mob's frame: its pivot, then its rotation (radians, Mojang's Z, then Y, then X)
    // plus the animation's.
    static FTransform MakePartTransform(const FFoundationMobPartAsset& Part, const FVector& ExtraRotation);
    // The part's mesh vertices: each face's corners in Unreal cm about the pivot (tests read them).
    static void GetPartQuads(
        const FFoundationMobPartAsset& Part, TArray<FVector>& OutPositions, TArray<FVector2D>& OutUVs);
    // The mob's root in the world: its interpolated feet, yaw and death roll.
    static FTransform MakeRootTransform(const FFoundationMob& Mob, const FVector& OriginCm, double Alpha);
    // The animation's added rotation for a part of a mob (radians, Mojang axes), interpolated by Alpha.
    static FVector GetPartAnimation(const FFoundationMob& Mob, FName Part, double Alpha);

private:
    struct FPartDraw
    {
        UInstancedStaticMeshComponent* Component = nullptr;
        TArray<FPrimitiveInstanceId> Ids;
        int32 Shown = 0;
    };
    struct FTypeDraw
    {
        const FFoundationMobModelAsset* Model = nullptr;
        TArray<FPartDraw> Parts;
        bool bTried = false;
    };
    bool Prepare(EFoundationMobType Type);

    UFoundationInteractionView& View;
    TStaticArray<FTypeDraw, int32(EFoundationMobType::Count)> Types;
    int32 Drawn = 0;
    TArray<float> Overlays;
};
