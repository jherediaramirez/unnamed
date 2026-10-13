#pragma once

#include "CoreMinimal.h"
#include "RealtimeMeshComponent.h"
#include "VoxelStars.generated.h"

struct FVoxelEnvironment;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

namespace VoxelStars
{
    inline constexpr int32 Count = 768;
    // Beyond streamed terrain. The camera-centred field never moves closer to a mountain or a cave wall.
    inline constexpr float RadiusCm = 10000000.f;

    struct FSettings
    {
        bool bEnabled = false;
        bool bCycle = false;
        bool bNight = false;
        float Brightness = .3f;
    };

    struct FStar
    {
        FVector3f Direction;
        // Tangent-plane half width, as a fraction of the field radius (0.055 to 0.10 degrees).
        float HalfWidth = 0.f;
        uint8 Luminance = 0;
    };

    UNNAMED_API FSettings ReadSettings();
    // The existing environment is the only time input. No flicker, animation, or second clock.
    UNNAMED_API float Visibility(const FVoxelEnvironment& Environment, const FSettings& Settings);
    // A fixed celestial field, independent of world seed and runtime/global random state.
    UNNAMED_API TArray<FStar> BuildField();
    UNNAMED_API void GetCorners(const FStar& Star, FVector3f (&Out)[4]);
    // A tiny navy addition during the light handover, independently driven by ReadNight's TwilightSky. The cube's
    // ordinary depth test and far translucent sort keep terrain, water and glass in front. Shares the star material.
    UNNAMED_API UStaticMeshComponent* CreateTwilightBackdrop(UObject* Outer);
    UNNAMED_API void UpdateTwilightBackdrop(UStaticMeshComponent& Backdrop, const FVector& Eye, float Strength);
}

// One batch of square quads, built on first visible use, retained across toggles. Normal depth testing hides the
// stars behind terrain. It neither casts shadows nor enters ray tracing, Lumen cards, collision or navigation.
UCLASS(ClassGroup = Voxel)
class UNNAMED_API UVoxelStars : public URealtimeMeshComponent
{
    GENERATED_BODY()
public:
    UVoxelStars();
    void Update(const FVoxelEnvironment& Environment, const FVector& Eye, const VoxelStars::FSettings& Settings);
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
    bool CreateField();
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> StarMaterial;
    bool bTriedMaterial = false;
    float AppliedIntensity = -1.f;
};
