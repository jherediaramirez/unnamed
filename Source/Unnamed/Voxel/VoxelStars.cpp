#include "Voxel/VoxelStars.h"
#include "Core/RealtimeMeshBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RealtimeMeshSimple.h"
#include "Sky/VoxelEnvironment.h"
#include "UnnamedProjectSettings.h"
#include "VoxelLog.h"

namespace
{
    TAutoConsoleVariable<bool> CVarAtmosphereStars(TEXT("voxel.Atmosphere.Stars"), true,
        TEXT("1: deterministic square stars, fading with the existing environment's Night. Requires Cycle 1 and "
             "Night 1 (default). 0: no star field."));
    TAutoConsoleVariable<float> CVarAtmosphereStarBrightness(TEXT("voxel.Atmosphere.StarBrightness"), .3f,
        TEXT("Square stars' exposure-compensated emissive brightness at full night, 0..2 (default 0.3)."));

    // Integer mixing, local to each star and attribute: no random stream, frame time, or world-clock ownership.
    uint32 StarHash(uint32 Value)
    {
        Value ^= Value >> 16;
        Value *= 0x7feb352du;
        Value ^= Value >> 15;
        Value *= 0x846ca68bu;
        return Value ^ (Value >> 16);
    }

    double StarUnit(uint32 Index, uint32 Salt)
    {
        return double(StarHash(Index ^ Salt) >> 8) / 16777216.;
    }
}

VoxelStars::FSettings VoxelStars::ReadSettings()
{
    static const IConsoleVariable* Cycle = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.Atmosphere.Cycle"));
    static const IConsoleVariable* Night = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.Atmosphere.Night"));
    return {CVarAtmosphereStars.GetValueOnGameThread(), Cycle && Cycle->GetInt() == 1, Night && Night->GetInt() == 1,
        FMath::Clamp(CVarAtmosphereStarBrightness.GetValueOnGameThread(), 0.f, 2.f)};
}

float VoxelStars::Visibility(const FVoxelEnvironment& Environment, const FSettings& Settings)
{
    if (!Settings.bEnabled || !Settings.bCycle || !Settings.bNight) return 0.f;
    // Smoothly enter through the latter half of twilight; at full night every star has its fixed brightness.
    return float(FMath::SmoothStep(.5, 1., double(Environment.Night)));
}

TArray<VoxelStars::FStar> VoxelStars::BuildField()
{
    TArray<FStar> Field;
    Field.Reserve(Count);
    for (uint32 Index = 0; Index < uint32(Count); ++Index)
    {
        const double Height = 2 * StarUnit(Index, 0x149b671du) - 1;
        const double Angle = UE_DOUBLE_TWO_PI * StarUnit(Index, 0x82d167b3u);
        const double Horizontal = FMath::Sqrt(FMath::Max(0., 1 - Height * Height));
        Field.Add(
            {FVector3f(float(Horizontal * FMath::Cos(Angle)), float(Horizontal * FMath::Sin(Angle)), float(Height)),
                float(FMath::DegreesToRadians(.055 + .045 * StarUnit(Index, 0xd6725a39u))),
                uint8(150 + StarHash(Index ^ 0x327af825u) % 106)});
    }
    return Field;
}

void VoxelStars::GetCorners(const FStar& Star, FVector3f (&Out)[4])
{
    const FVector3f Axis = FMath::Abs(Star.Direction.Z) > .99f ? FVector3f(0, 1, 0) : FVector3f(0, 0, 1);
    const FVector3f Right = FVector3f::CrossProduct(Axis, Star.Direction).GetSafeNormal();
    const FVector3f Up = FVector3f::CrossProduct(Star.Direction, Right);
    const FVector3f Center = Star.Direction * RadiusCm;
    const FVector3f Side = Right * (RadiusCm * Star.HalfWidth);
    const FVector3f Top = Up * (RadiusCm * Star.HalfWidth);
    Out[0] = Center - Side - Top;
    Out[1] = Center + Side - Top;
    Out[2] = Center + Side + Top;
    Out[3] = Center - Side + Top;
}

UStaticMeshComponent* VoxelStars::CreateTwilightBackdrop(UObject* Outer)
{
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UMaterialInterface* Material =
        Cast<UMaterialInterface>(UUnnamedProjectSettings::GetBlockAssetPath(TEXT("M_AtmosphereStars")).TryLoad());
    if (!Cube || !Material)
    {
        UE_LOG(LogUnnamedVoxel, Error,
            TEXT("Twilight: missing Cube or M_AtmosphereStars; run Scripts/Editor/CreateStarMaterial.py"));
        return nullptr;
    }
    UStaticMeshComponent* Backdrop = NewObject<UStaticMeshComponent>(Outer, TEXT("TwilightBackdrop"));
    Backdrop->SetMobility(EComponentMobility::Movable);
    Backdrop->SetStaticMesh(Cube);
    Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Backdrop->SetCanEverAffectNavigation(false);
    Backdrop->SetGenerateOverlapEvents(false);
    Backdrop->SetCastShadow(false);
    Backdrop->SetReceivesDecals(false);
    Backdrop->SetAffectDistanceFieldLighting(false);
    Backdrop->SetAffectDynamicIndirectLighting(false);
    Backdrop->SetVisibleInRayTracing(false);
    Backdrop->SetTranslucentSortPriority(-200);
    Backdrop->SetAbsolute(true, true, true);
    // The engine cube is 100 cm across; every inner face stays at least RadiusCm away from the camera.
    Backdrop->SetWorldScale3D(FVector(2.f * RadiusCm / 100.f));
    UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Material, Backdrop);
    Dynamic->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.02f, .04f, .12f));
    Dynamic->SetScalarParameterValue(TEXT("Intensity"), 0.f);
    Backdrop->SetMaterial(0, Dynamic);
    Backdrop->SetVisibility(false);
    return Backdrop;
}

void VoxelStars::UpdateTwilightBackdrop(UStaticMeshComponent& Backdrop, const FVector& Eye, float Strength)
{
    const float Intensity = FMath::Clamp(Strength, 0.f, 1.f);
    UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Backdrop.GetMaterial(0));
    const bool bShow = Intensity > 0.f && Material;
    if (Backdrop.IsVisible() != bShow) Backdrop.SetVisibility(bShow);
    if (!bShow) return;
    Backdrop.SetWorldLocation(Eye);
    Material->SetScalarParameterValue(TEXT("Intensity"), Intensity);
}

UVoxelStars::UVoxelStars()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetMobility(EComponentMobility::Movable);
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCanEverAffectNavigation(false);
    SetGenerateOverlapEvents(false);
    SetCastShadow(false);
    SetAffectDistanceFieldLighting(false);
    SetAffectDynamicIndirectLighting(false);
    SetVisibleInRayTracing(false);
    // Draw before the nearer translucent water/glass, whose normal blends can then cover the sky.
    SetTranslucentSortPriority(-100);
    SetVisibility(false);
}

FBoxSphereBounds UVoxelStars::CalcBounds(const FTransform& LocalToWorld) const
{
    return FBoxSphereBounds(FVector::ZeroVector, FVector(VoxelStars::RadiusCm * 1.01f), VoxelStars::RadiusCm * 1.01f)
        .TransformBy(LocalToWorld);
}

bool UVoxelStars::CreateField()
{
    if (StarMaterial) return true;
    if (bTriedMaterial) return false;
    bTriedMaterial = true;
    UMaterialInterface* Material =
        Cast<UMaterialInterface>(UUnnamedProjectSettings::GetBlockAssetPath(TEXT("M_AtmosphereStars")).TryLoad());
    if (!Material)
    {
        UE_LOG(
            LogUnnamedVoxel, Error, TEXT("Stars: M_AtmosphereStars missing; run Scripts/Editor/CreateStarMaterial.py"));
        return false;
    }
    StarMaterial = UMaterialInstanceDynamic::Create(Material, this);
    URealtimeMeshSimple* Mesh = InitializeRealtimeMesh<URealtimeMeshSimple>();
    Mesh->SetupMaterialSlot(0, TEXT("Stars"), StarMaterial);
    using namespace RealtimeMesh;
    FRealtimeMeshStreamSet Streams;
    TRealtimeMeshBuilderLocal<uint32, FPackedNormal, FVector2DHalf, 1> Builder(Streams);
    Builder.EnableTangents();
    Builder.EnableTexCoords();
    Builder.EnableColors();
    for (const VoxelStars::FStar& Star : VoxelStars::BuildField())
    {
        FVector3f Corners[4];
        VoxelStars::GetCorners(Star, Corners);
        const uint32 First = Builder.NumVertices();
        const FVector3f Tangent = (Corners[1] - Corners[0]).GetSafeNormal();
        const FColor Shade(Star.Luminance, Star.Luminance, Star.Luminance, 255);
        for (int32 Corner = 0; Corner < 4; ++Corner)
            Builder.AddVertex(Corners[Corner])
                .SetNormalAndTangent(-Star.Direction, Tangent)
                .SetTexCoord(FVector2f((Corner == 1 || Corner == 2) ? 1.f : 0.f, Corner >= 2 ? 1.f : 0.f))
                .SetColor(Shade);
        Builder.AddTriangle(First, First + 2, First + 1);
        Builder.AddTriangle(First, First + 3, First + 2);
    }
    const FRealtimeMeshBufferSetKey Key = FRealtimeMeshBufferSetKey::Create(0, TEXT("Stars"));
    // With no poly-group stream, RealtimeMesh creates exactly one section for all triangles.
    Mesh->CreateBufferSet(Key, MoveTemp(Streams));
    FRealtimeMeshSectionConfig Section(0);
    Section.bCastsShadow = false;
    Mesh->UpdateSectionConfig(FRealtimeMeshSectionKey::CreateForPolyGroup(Key, 0), Section);
    UE_LOG(LogUnnamedVoxel, Display, TEXT("Stars: one field, %d square quads, depth tested, no collision/shadows"),
        VoxelStars::Count);
    return true;
}

void UVoxelStars::Update(
    const FVoxelEnvironment& Environment, const FVector& Eye, const VoxelStars::FSettings& Settings)
{
    const float Intensity = VoxelStars::Visibility(Environment, Settings) * FMath::Clamp(Settings.Brightness, 0.f, 2.f);
    const bool bShow = Intensity > 0.f && CreateField();
    if (IsVisible() != bShow) SetVisibility(bShow);
    if (!bShow) return;
    // Keep the field fixed against view rotation; only its origin follows the camera.
    SetWorldLocation(Eye);
    if (AppliedIntensity != Intensity)
    {
        StarMaterial->SetScalarParameterValue(TEXT("Intensity"), Intensity);
        AppliedIntensity = Intensity;
    }
}
