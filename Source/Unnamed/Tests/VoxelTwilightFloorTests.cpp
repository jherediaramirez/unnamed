#if WITH_DEV_AUTOMATION_TESTS
#include "Components/PostProcessComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnnamedProjectSettings.h"
#include "Voxel/VoxelTwilight.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTwilightFloorLinearTest, "Unnamed.Voxel.Atmosphere.Twilight.LinearFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTwilightFloorLinearTest::RunTest(const FString& Parameters)
{
    const FLinearColor Black(0, 0, 0), Base(.1f, .1f, .1f), Lit(2.f, .8f, .4f, .65f);
    const FVector Up(0, 0, 1);
    const auto Evaluate = [&](const FVector& Normal, double Depth, float Strength, float ExposureInverse = 1.f)
    { return VoxelTwilight::EvaluateFloor(Black, Base, Normal, Depth, Strength, 6000, 8000, ExposureInverse); };
    const FLinearColor Top = Evaluate(Up, 500, 4.f);
    TestTrue(TEXT("linear HDR oracle at strength four, no sRGB conversion"),
        Top.Equals(FLinearColor(.01984f, .02304f, .032f), 1e-6f));
    TestTrue(TEXT("blue-biased, dim floor keeps texture values"), Top.R < Top.G && Top.G < Top.B && Top.B < Base.B);
    TestTrue(TEXT("north/south faces share their subdued minimum"),
        Evaluate(FVector(0, 1, 0), 500, 4.f).Equals(FLinearColor(.013888f, .016128f, .0224f), 1e-6f) &&
            Evaluate(FVector(0, -1, 0), 500, 4.f).Equals(Evaluate(FVector(0, 1, 0), 500, 4.f)));
    TestTrue(TEXT("east/west faces retain directional contrast"),
        Evaluate(FVector(1, 0, 0), 500, 4.f).Equals(FLinearColor(.012896f, .014976f, .0208f), 1e-6f));
    TestTrue(TEXT("underside is half the top"),
        Evaluate(-Up, 500, 4.f).Equals(FLinearColor(.00992f, .01152f, .016f), 1e-6f));
    TestTrue(TEXT("normal magnitude does not alter the floor"), Evaluate(Up * 7, 500, 4.f).Equals(Top));
    TestTrue(TEXT("missing normal adds nothing"), Evaluate(FVector::ZeroVector, 500, 4.f).Equals(Black));
    const FLinearColor Half = Evaluate(Up, 7000, 4.f);
    TestTrue(TEXT("distance-faded halfway before fog edge"), Half.Equals(FLinearColor(.00992f, .01152f, .016f), 1e-6f));
    TestTrue(TEXT("no floor at or beyond fog edge"),
        Evaluate(Up, 8000, 4.f).Equals(Black) && Evaluate(Up, 9000, 4.f).Equals(Black));
    TestTrue(TEXT("dial halves the floor"), Evaluate(Up, 500, 2.f).Equals(Half));
    TestTrue(TEXT("lit HDR channels and alpha are untouched"),
        VoxelTwilight::EvaluateFloor(Lit, Base, Up, 500, 4, 6000, 8000).Equals(Lit));
    TestTrue(TEXT("black base including sky is untouched"),
        VoxelTwilight::EvaluateFloor(Lit, Black, Up, 500, 4, 6000, 8000).Equals(Lit) &&
            VoxelTwilight::EvaluateFloor(Black, Black, Up, 500, 4, 6000, 8000).Equals(Black));
    const FLinearColor Mixed = VoxelTwilight::EvaluateFloor(
        FLinearColor(.2f, 0, .6f, .4f), FLinearColor(.1f, .2f, .1f), Up, 500, 4, 6000, 8000);
    TestTrue(
        TEXT("max only lifts dark channels and preserves alpha"), Mixed.Equals(FLinearColor(.2f, .04608f, .6f, .4f)));
    for (const float Exposure : {.025f, .1f, 1.f, 4.f})
    {
        const FLinearColor Compensated = Evaluate(Up, 500, 4.f, 1.f / Exposure);
        TestTrue(TEXT("same displayed linear floor across adaptation"),
            FMath::IsNearlyEqual(Compensated.R * Exposure, Top.R, 1e-6f) &&
                FMath::IsNearlyEqual(Compensated.G * Exposure, Top.G, 1e-6f) &&
                FMath::IsNearlyEqual(Compensated.B * Exposure, Top.B, 1e-6f));
    }
    TestTrue(TEXT("no inverse exposure produces no fill"), Evaluate(Up, 500, 4.f, 0.f).Equals(Black));
    TestTrue(TEXT("negative and zero strengths preserve scene"),
        VoxelTwilight::EvaluateFloor(Lit, Base, Up, 500, 0, 6000, 8000).Equals(Lit) &&
            VoxelTwilight::EvaluateFloor(Lit, Base, Up, 500, -1, 6000, 8000).Equals(Lit));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTwilightFloorComponentTest, "Unnamed.Voxel.Atmosphere.Twilight.FloorComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTwilightFloorComponentTest::RunTest(const FString& Parameters)
{
    UPostProcessComponent* Component = VoxelTwilight::CreateFloor(GetTransientPackage());
    if (!TestNotNull(TEXT("owned twilight material loads"), Component)) return false;
    TestFalse(TEXT("starts disabled"), bool(Component->bEnabled));
    TestTrue(TEXT("one unbound post-process blendable"),
        Component->bUnbound && Component->Settings.WeightedBlendables.Array.Num() == 1);
    UMaterialInstanceDynamic* Dynamic =
        Cast<UMaterialInstanceDynamic>(Component->Settings.WeightedBlendables.Array[0].Object);
    if (!TestNotNull(TEXT("one dynamic material"), Dynamic)) return false;
    const UMaterial* Base = Dynamic->GetMaterial();
    TestEqual(TEXT("post-process domain"), Base->MaterialDomain.GetValue(), MD_PostProcess);
    TestEqual(TEXT("terrain floor occurs before water/glass blending"), Base->BlendableLocation.GetValue(),
        BL_SceneColorBeforeDOF);
    TestFalse(TEXT("engine performs pre-exposure conversion"), bool(Base->bDisablePreExposureScale));
    VoxelTwilight::UpdateFloor(*Component, 4.f, 6000.f, 8000.f);
    TestTrue(TEXT("positive strength enables component"), bool(Component->bEnabled));
    TestEqual(TEXT("strength reaches shader"), Dynamic->K2_GetScalarParameterValue(TEXT("Strength")), 4.f);
    TestEqual(TEXT("fade start reaches shader"), Dynamic->K2_GetScalarParameterValue(TEXT("FadeStart")), 6000.f);
    TestEqual(TEXT("fade end reaches shader"), Dynamic->K2_GetScalarParameterValue(TEXT("FadeEnd")), 8000.f);
    VoxelTwilight::UpdateFloor(*Component, 0.f, 6000.f, 8000.f);
    TestFalse(TEXT("zero strength disables component"), bool(Component->bEnabled));
    VoxelTwilight::UpdateFloor(*Component, 2.f, 100.f, 1400.f);
    TestTrue(TEXT("same material reused on return"), Component->Settings.WeightedBlendables.Array[0].Object == Dynamic);
    TestEqual(
        TEXT("underwater fade start reaches shader"), Dynamic->K2_GetScalarParameterValue(TEXT("FadeStart")), 100.f);
    TestEqual(TEXT("underwater fade end reaches shader"), Dynamic->K2_GetScalarParameterValue(TEXT("FadeEnd")), 1400.f);
    for (const TCHAR* Name :
        {TEXT("M_BlockWater"), TEXT("M_BlockTranslucent"), TEXT("M_ChunkWater"), TEXT("M_ChunkTranslucent"),
            TEXT("M_ChunkDepthWater"), TEXT("M_ChunkFarDepthWater"), TEXT("M_ChunkFarWater")})
    {
        const UMaterial* Transparent = Cast<UMaterial>(UUnnamedProjectSettings::GetBlockAssetPath(Name).TryLoad());
        TestTrue(FString::Printf(TEXT("%s still blends after DOF"), Name),
            Transparent && Transparent->TranslucencyPass == MTP_AfterDOF);
    }
    // The shader's coefficient table must agree with the numerical oracle, and its linear result must go through
    // the inverse-exposure node. Shader compilation and render captures remain separate acceptance gates.
    FString Script;
    if (!TestTrue(TEXT("twilight material generator exists"),
            FFileHelper::LoadFileToString(
                Script, *(FPaths::ProjectDir() / TEXT("Scripts/Editor/CreateTwilightMaterial.py")))))
        return false;
    const auto Constant = [&Script](const TCHAR* Name)
    {
        const FString Key = FString(Name) + TEXT(" = ");
        const int32 At = Script.Find(Key, ESearchCase::CaseSensitive);
        return At == INDEX_NONE ? -1.f : FCString::Atof(*Script.Mid(At + Key.Len(), 12));
    };
    TestTrue(TEXT("shader's coefficients agree with HDR oracle"),
        Constant(TEXT("FLOOR_SCALE")) == VoxelTwilight::FloorScale &&
            Constant(TEXT("FLOOR_TOP")) == VoxelTwilight::FloorTop &&
            Constant(TEXT("FLOOR_BOTTOM")) == VoxelTwilight::FloorBottom &&
            Constant(TEXT("FLOOR_NS")) == VoxelTwilight::FloorNS &&
            Constant(TEXT("FLOOR_EW")) == VoxelTwilight::FloorEW);
    TestTrue(TEXT("shader applies inverse exposure to linear floor"),
        Script.Contains(TEXT("return max(Color, floor * max(ExposureInverse, 0.0));")) &&
            Script.Contains(TEXT("unreal.MaterialExpressionEyeAdaptationInverse")));
    return true;
}
#endif
