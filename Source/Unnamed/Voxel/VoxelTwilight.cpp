#include "Voxel/VoxelTwilight.h"
#include "Components/PostProcessComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnnamedProjectSettings.h"
#include "VoxelLog.h"

FLinearColor VoxelTwilight::EvaluateFloor(const FLinearColor& Color, const FLinearColor& BaseLinear,
    const FVector& Normal, double DepthCm, float Strength, double FadeStartCm, double FadeEndCm, float InverseExposure)
{
    if (Strength <= 0.f || FMath::Max3(BaseLinear.R, BaseLinear.G, BaseLinear.B) <= 0.f) return Color;
    const double Fade = 1. - FMath::Clamp((DepthCm - FadeStartCm) / FMath::Max(FadeEndCm - FadeStartCm, 1.), 0., 1.);
    if (Fade <= 0.) return Color;
    const FVector N = Normal.GetSafeNormal();
    const FVector A = N.GetAbs();
    const double Face = A.Z * (N.Z > 0 ? FloorTop : FloorBottom) + A.Y * FloorNS + A.X * FloorEW;
    const double Scale = FloorScale * Strength * Face * Fade * FMath::Max(0.f, InverseExposure);
    const auto Channel = [Scale](float Scene, float Base, float Cool)
    { return FMath::Max(Scene, float(Scale * FMath::Clamp(Base, 0.f, 1.f) * Cool)); };
    return FLinearColor(Channel(Color.R, BaseLinear.R, .62f), Channel(Color.G, BaseLinear.G, .72f),
        Channel(Color.B, BaseLinear.B, 1.f), Color.A);
}

UPostProcessComponent* VoxelTwilight::CreateFloor(UObject* Outer)
{
    UMaterialInterface* Base =
        Cast<UMaterialInterface>(UUnnamedProjectSettings::GetBlockAssetPath(MaterialName).TryLoad());
    if (!Base || Base->GetMaterial()->MaterialDomain != MD_PostProcess ||
        Base->GetMaterial()->BlendableLocation != BL_SceneColorBeforeDOF)
    {
        UE_LOG(LogUnnamedVoxel, Error,
            TEXT("Twilight: %s missing or not before DOF; run Scripts/Editor/CreateTwilightMaterial.py"), MaterialName);
        return nullptr;
    }
    UPostProcessComponent* Component = NewObject<UPostProcessComponent>(Outer, TEXT("TwilightFloorPost"));
    Component->bUnbound = true;
    Component->bEnabled = false;
    Component->BlendWeight = 1.f;
    UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Base, Component);
    Component->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, Dynamic));
    return Component;
}

void VoxelTwilight::UpdateFloor(UPostProcessComponent& Component, float Strength, float FadeStartCm, float FadeEndCm)
{
    UMaterialInstanceDynamic* Material = Component.Settings.WeightedBlendables.Array.Num() == 1
        ? Cast<UMaterialInstanceDynamic>(Component.Settings.WeightedBlendables.Array[0].Object)
        : nullptr;
    Component.bEnabled = Strength > 0.f && Material;
    if (!Component.bEnabled) return;
    const TPair<const TCHAR*, float> Values[] = {
        {TEXT("Strength"), Strength}, {TEXT("FadeStart"), FadeStartCm}, {TEXT("FadeEnd"), FadeEndCm}};
    for (const TPair<const TCHAR*, float>& Value : Values)
    {
        float Current = 0.f;
        if (!Material->GetScalarParameterValue(FName(Value.Key), Current) || Current != Value.Value)
            Material->SetScalarParameterValue(Value.Key, Value.Value);
    }
}
