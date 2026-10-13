#pragma once

#include "CoreMinimal.h"

class UPostProcessComponent;

// A temporary cool terrain minimum at the sun/moon handover. It runs before DOF, so the world's after-DOF water
// and glass blend over the lifted terrain. The older after-tone-mapping cave floor keeps its separate material.
namespace VoxelTwilight
{
    inline constexpr const TCHAR* MaterialName = TEXT("M_AtmosphereTwilight");
    inline constexpr float FloorScale = .08f;
    inline constexpr float FloorTop = 1.f;
    inline constexpr float FloorBottom = .5f;
    inline constexpr float FloorNS = .7f;
    inline constexpr float FloorEW = .65f;

    // Shader oracle: Color is un-pre-exposed scene-linear HDR, BaseLinear is the GBuffer's linear base colour.
    // InverseExposure is EyeAdaptationInverse (1/exposure): the floor stays stable as the view adapts. The engine
    // handles pre-exposure for both PostProcessInput0 and the material output. Alpha and brighter channels survive.
    UNNAMED_API FLinearColor EvaluateFloor(const FLinearColor& Color, const FLinearColor& BaseLinear,
        const FVector& Normal, double DepthCm, float Strength, double FadeStartCm, double FadeEndCm,
        float InverseExposure = 1.f);

    // The owner attaches/registers and retains this component. Creation allocates one disabled post process with
    // one dynamic material. Update changes parameters and enablement; it never changes exposure or the world clock.
    UNNAMED_API UPostProcessComponent* CreateFloor(UObject* Outer);
    UNNAMED_API void UpdateFloor(UPostProcessComponent& Component, float Strength, float FadeStartCm, float FadeEndCm);
}
