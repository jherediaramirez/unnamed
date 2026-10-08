#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Foundation/FoundationActions.h"
#include "FoundationLab.generated.h"

class FFoundationAuthority;
class UFoundationProfile;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UTextRenderComponent;

// Dev grid tints ("Color" on the surface material), one per kind of fixture so the world reads at a glance.
namespace FoundationTint
{
    inline const FLinearColor Ground(.30f, .34f, .42f);
    inline const FLinearColor Course(.85f, .52f, .18f);
    inline const FLinearColor Structure(.45f, .50f, .55f);
    inline const FLinearColor Wall(.62f, .64f, .68f);
    inline const FLinearColor Water(.08f, .25f, .85f);
}

struct FFoundationPrimitive
{
    FBox Box;
    FString Label;
    EFoundationTargetKind Kind;
    bool bInteractive = false;
};
UCLASS()
class UNNAMED_API AFoundationLab : public AActor
{
    GENERATED_BODY()
public:
    AFoundationLab();
    virtual void BeginPlay() override;
    // The reproducible layout, built at BeginPlay: the ground and the movement course.
    virtual void BuildLayout();

    // Ground: one flat 2 km x 2 km slab whose top is z = 0 (cm), centred on the origin.
    static constexpr double GroundHalfExtentCm = 100000;
    // Where the movement course's local (0, 0, 0) sits. Probes place the player relative to it.
    FVector GetCourseOrigin() const { return CourseOrigin; }
    // The surface every solid and block shares: the project's dev grid, falling back to the engine material.
    UMaterialInterface* GetSurfaceMaterial() const { return Material; }
    TArray<FBox> CollisionBoxes;
    TArray<FFoundationPrimitive> Primitives;
    // Still water volumes: no collision or targeting, and no swimming physics yet.
    TArray<FBox> WaterBoxes;
    // Nearest primitive along the view ray, with reach checked against the profile.
    virtual FFoundationTarget TraceTarget(FVector Eye, FVector Direction, const UFoundationProfile& Profile) const;
    // Eyes: strictly below a water surface. Body: overlapping water by more than a touch.
    bool IsInWater(FVector Point) const;
    bool IsInWater(const FBox& Body) const;
    // The movement collision provider: every box that can touch Region. This lab's immutable cuboids;
    // the Playground adds its committed blocks.
    virtual void GatherCollision(const FBox& Region, TArray<FBox>& Out) const;
    // World rebasing: logical geometry moves with the world.
    virtual void ApplyLogicalOffset(const FVector& Offset);
    // Where (re)spawning puts the feet.
    virtual FVector GetSafeSpawn(const UFoundationProfile& Profile) const { return FVector::ZeroVector; }
    // Feet below this height (cm) return to the safe spawn: the lab's floor is at 0, a voxel world goes far deeper.
    virtual double GetFallOutZ() const { return -1000; }
    // The gameplay interaction authority, when this lab has one (only the Playground does).
    virtual FFoundationAuthority* GetAuthority() { return nullptr; }

protected:
    // A solid, immutable cuboid with an optional caption. The Playground overrides this so every solid is
    // also an interaction static: targetable, occluding and, when grid aligned, a placement anchor.
    virtual void AddSolid(const FString& Label, FVector Minimum, FVector Maximum, FLinearColor Color, bool bCaption);
    void AddGround();
    // Steps, ceilings, gaps, ledges, stairs, a drop and a water column around CourseOrigin.
    void BuildMovementCourse();
    void AddWater(FString Label, FVector Minimum, FVector Maximum);
    UStaticMeshComponent* AddMesh(const FBox& Box, FLinearColor Color, bool bCollides);
    UTextRenderComponent* AddLabel(const FString& Label, const FBox& Box);
    UStaticMesh* GetCube() const { return Cube; }

    FVector CourseOrigin = FVector::ZeroVector;

private:
    UPROPERTY()
    TObjectPtr<UStaticMesh> Cube;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> Material;
};
