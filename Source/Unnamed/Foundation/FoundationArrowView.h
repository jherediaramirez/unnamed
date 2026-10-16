#pragma once

#include "CoreMinimal.h"
// FoundationAuthority.h needs it first.
#include "Containers/StaticArray.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Foundation/FoundationAuthority.h"

class AFoundationPawn;
class UFoundationInteractionView;
class UInstancedStaticMeshComponent;

// The arrows' look (survival step 19c): presentation only, like the view that owns it. Each arrow within DrawCm of the
// player is one instance of the arrow#main model (DA_Projectiles, ImportProjectiles.py: its parts merged into one mesh,
// on M_Mob with T_Arrow), drawn between the last two reference steps, its nose along its yaw and pitch (a stuck arrow
// keeps its last).
class FFoundationArrowView
{
public:
    explicit FFoundationArrowView(UFoundationInteractionView& InView) : View(InView) {}

    // Draws this frame's arrows; Alpha is the fraction of a reference step since the last one.
    void Update(const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, double Alpha);
    // Probes: arrows drawn this frame.
    int32 GetDrawnCount() const { return Drawn; }

    // As the item entities: arrows farther than this are not drawn; at most MaxArrows, nearest first (provisional).
    static constexpr double DrawCm = 6400;
    static constexpr int32 MaxArrows = 256;

    // The mesh, material and instances, made on the first Update with an arrow; false without DA_Projectiles (or its
    // texture), tried once.
    bool Prepare();
    // The model's corners in the arrow's frame (cm; its nose along +X, tests read them): each part's faces through
    // the part's pivot and rotation, as FFoundationMobView draws a mob's parts.
    static void GetModelQuads(
        const struct FFoundationMobModelAsset& Model, TArray<FVector>& OutPositions, TArray<FVector2D>& OutUVs);

private:
    UFoundationInteractionView& View;
    UInstancedStaticMeshComponent* Arrows = nullptr;
    TArray<FPrimitiveInstanceId> Ids;
    // The arrow each slot drew last frame (its id), for the motion vectors.
    TArray<int32> SlotArrows;
    int32 Shown = 0;
    bool bTried = false;
    int32 Drawn = 0;
};
