#pragma once

#include "CoreMinimal.h"
// FoundationAuthority.h needs it first.
#include "Containers/StaticArray.h"
#include "Foundation/FoundationAuthority.h"

class AFoundationPawn;
class UFoundationInteractionView;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;

// The item entities' look (items S8a): presentation only, like the view that owns it. Each drop within DrawCm of the
// local player is drawn with the held item's meshes in its model's "ground" display: a block item as its block's
// default state on the block materials (block/block.json's ground, scale 0.25), any other item as its extruded sprite
// lit by the scene (the item model's ground, scale 0.5). It bobs and spins (FoundationItemEntities' provisional look),
// drawn between the last two reference steps; a frozen drop (its column not loaded) is hidden. A whole stack picked up
// by the local player flies to the player's centre.
class FFoundationItemEntityView
{
public:
    explicit FFoundationItemEntityView(UFoundationInteractionView& InView) : View(InView) {}

    // Draws this frame's drops; Alpha is the fraction of a reference step since the last one.
    void Update(const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, double Alpha, double DeltaTime);
    // A PickedUp event (the view's HandleEvents): the pickup sound, and a whole stack's flight.
    void OnPickedUp(const FFoundationEvent& Event, const AFoundationPawn* Pawn);
    // Drops drawn this frame, and visuals flying to the player (probes).
    int32 GetDrawnCount() const { return Drawn; }
    int32 GetFlyingCount() const;
    // Each drawn drop's visible parts: their world bounds and meshes (probes).
    FString Describe() const;

    // Drops farther than this from the player are not drawn (provisional; vanilla scales item render distance with
    // the entity distance option); at most MaxVisuals are drawn, nearest first.
    static constexpr double DrawCm = 6400;
    static constexpr int32 MaxVisuals = 256;

private:
    struct FVisual
    {
        USceneComponent* Root = nullptr;
        // One instance per part of the block's model (a cube has one), or the sprite.
        TArray<UInstancedStaticMeshComponent*> Blocks;
        UStaticMeshComponent* Sprite = nullptr;
        // The item shown (INDEX_NONE: none yet), and whether it draws anything.
        int32 Item = INDEX_NONE;
        bool bDrawn = false;
        int32 EntityId = 0;
        // The look's lift above the drop's feet: 0.25 x the ground display's scale (provisional, P3).
        double LiftCm = 0;
        // Flying to the player after a whole pickup: seconds flown (negative when not), and where it started.
        double FlyAge = -1;
        FVector FlyFrom = FVector::ZeroVector;
        bool bUsed = false;
    };
    FVisual& Acquire(int32 EntityId);
    void Release(FVisual& Visual);
    // Shows an item on a visual, its parts visible; false when it has nothing to draw (no model, the assets not
    // loaded).
    bool SetItem(FVisual& Visual, const FFoundationAuthority& Authority, int32 Item);

    UFoundationInteractionView& View;
    TArray<FVisual> Visuals;
    TMap<int32, int32> ByEntity;
    // M_HeldItem without the hand's glow, for the 16 and 32 pixel sprite arrays.
    TStaticArray<UMaterialInstanceDynamic*, 2> SpriteMaterials{InPlace, nullptr};
    int32 Drawn = 0;
};
