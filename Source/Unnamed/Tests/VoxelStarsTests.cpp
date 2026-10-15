#if WITH_DEV_AUTOMATION_TESTS
#include "Clock/VoxelWorldClock.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RealtimeMeshBuilder.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "RealtimeMeshSimple.h"
#include "Sky/VoxelEnvironment.h"
#include "Voxel/VoxelStars.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelStarsVisibilityTest, "Unnamed.Voxel.Atmosphere.Stars.Visibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelStarsVisibilityTest::RunTest(const FString& Parameters)
{
    VoxelStars::FSettings Settings{true, true, true, .3f};
    FVoxelEnvironment Environment;
    Environment.Night = 1.f;
    for (int32 Mask = 0; Mask < 8; ++Mask)
    {
        Settings.bEnabled = (Mask & 1) != 0;
        Settings.bCycle = (Mask & 2) != 0;
        Settings.bNight = (Mask & 4) != 0;
        TestEqual(FString::Printf(TEXT("only Stars + Cycle + Night enables the field (%d)"), Mask),
            VoxelStars::Visibility(Environment, Settings), Mask == 7 ? 1.f : 0.f);
    }
    Settings = {true, true, true, .3f};
    Environment.Night = 0.f;
    TestEqual(TEXT("day has no stars"), VoxelStars::Visibility(Environment, Settings), 0.f);
    Environment.Night = .5f;
    TestEqual(TEXT("sun on horizon: still hidden"), VoxelStars::Visibility(Environment, Settings), 0.f);
    Environment.Night = .75f;
    TestNearlyEqual(TEXT("mid fade: half strength"), VoxelStars::Visibility(Environment, Settings), .5f, 1e-6f);
    float Previous = 0.f;
    for (int32 Step = 0; Step <= 100; ++Step)
    {
        Environment.Night = Step / 100.f;
        const float Visible = VoxelStars::Visibility(Environment, Settings);
        TestTrue(TEXT("fade bounded and monotonic"), Visible >= Previous && Visible >= 0.f && Visible <= 1.f);
        TestTrue(TEXT("no brightness jump per 1 percent of night"), Visible - Previous < .031f);
        Previous = Visible;
    }
    FVoxelWorldClock Clock;
    Clock.SetTotal(18000);
    const auto Night = VoxelEnvironment::Evaluate(Clock, true);
    TestEqual(TEXT("existing clock's midnight gives full field"), VoxelStars::Visibility(Night, Settings), 1.f);
    Clock.SetTotal(6000);
    TestEqual(TEXT("existing clock's noon gives no field"),
        VoxelStars::Visibility(VoxelEnvironment::Evaluate(Clock, true), Settings), 0.f);
    Environment = Night;
    Environment.SmoothTicks += 90000;
    Environment.MoonPhase = 4;
    TestEqual(TEXT("time and phase never animate star intensity independently of Night"),
        VoxelStars::Visibility(Environment, Settings), VoxelStars::Visibility(Night, Settings));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelStarsFieldTest, "Unnamed.Voxel.Atmosphere.Stars.Field",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelStarsFieldTest::RunTest(const FString& Parameters)
{
    const TArray<VoxelStars::FStar> Field = VoxelStars::BuildField();
    const TArray<VoxelStars::FStar> Again = VoxelStars::BuildField();
    if (!TestEqual(TEXT("bounded one-batch field"), Field.Num(), 768)) return false;
    TestTrue(TEXT("first direction is stable"),
        Field[0].Direction.Equals(FVector3f(-.53145577f, .22259657f, -.81731606f), 1e-6f));
    TestTrue(TEXT("second direction is stable"),
        Field[1].Direction.Equals(FVector3f(.30082491f, .45945930f, .83570421f), 1e-6f));
    TestTrue(TEXT("last direction is stable"),
        Field[767].Direction.Equals(FVector3f(-.36786140f, -.05531424f, -.92823398f), 1e-6f));
    TestEqual(TEXT("fixed first brightness"), Field[0].Luminance, uint8(195));
    TestNearlyEqual(TEXT("fixed first size"), Field[0].HalfWidth, .0012361942f, 1e-9f);
    int32 Upper = 0;
    int32 Octants[8] = {};
    for (int32 Index = 0; Index < Field.Num(); ++Index)
    {
        const VoxelStars::FStar& Star = Field[Index];
        TestTrue(TEXT("field repeat independent of runtime random state"),
            Star.Direction == Again[Index].Direction && Star.HalfWidth == Again[Index].HalfWidth &&
                Star.Luminance == Again[Index].Luminance);
        TestNearlyEqual(TEXT("on unit sphere"), Star.Direction.SizeSquared(), 1.f, 1e-5f);
        TestTrue(TEXT("small bounded angular width"), Star.HalfWidth >= .0009599f && Star.HalfWidth <= .0017454f);
        TestTrue(TEXT("bounded static brightness"), Star.Luminance >= 150);
        Upper += Star.Direction.Z > 0;
        ++Octants[(Star.Direction.X > 0 ? 1 : 0) | (Star.Direction.Y > 0 ? 2 : 0) | (Star.Direction.Z > 0 ? 4 : 0)];
        FVector3f Corners[4];
        VoxelStars::GetCorners(Star, Corners);
        const FVector3f Center = (Corners[0] + Corners[1] + Corners[2] + Corners[3]) * .25f;
        TestTrue(
            TEXT("quad stays on its celestial direction"), Center.Equals(Star.Direction * VoxelStars::RadiusCm, 2.f));
        const FVector3f Side = Corners[1] - Corners[0];
        const FVector3f Up = Corners[3] - Corners[0];
        TestNearlyEqual(TEXT("square: equal side lengths"), Side.Size(), Up.Size(), 2.f);
        TestTrue(TEXT("square: orthogonal sides"),
            FMath::Abs(FVector3f::DotProduct(Side.GetSafeNormal(), Up.GetSafeNormal())) < .0002f);
        TestTrue(TEXT("square: plane faces observer"),
            FMath::Abs(FVector3f::DotProduct(Side.GetSafeNormal(), Star.Direction)) < .0002f);
        TestTrue(
            TEXT("square: opposite corners close"), (Corners[0] + Corners[2]).Equals(Corners[1] + Corners[3], 4.f));
    }
    TestTrue(TEXT("roughly half the sphere above the horizon"), Upper > 340 && Upper < 430);
    for (int32 Count : Octants)
        TestTrue(TEXT("field distributed in every octant"), Count >= 60 && Count <= 130);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelStarsComponentTest, "Unnamed.Voxel.Atmosphere.Stars.Component",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelStarsComponentTest::RunTest(const FString& Parameters)
{
    UVoxelStars* Stars = NewObject<UVoxelStars>();
    FVoxelEnvironment Environment;
    Environment.Night = 1.f;
    VoxelStars::FSettings Settings;
    Stars->Update(Environment, FVector::ZeroVector, Settings);
    TestFalse(TEXT("off by default"), Stars->IsVisible());
    TestNull(TEXT("disabled field allocates no mesh"), Stars->GetRealtimeMesh());
    Settings = {true, true, true, .3f};
    const FVector Eye(17000, -8500, 6400);
    Stars->Update(Environment, Eye, Settings);
    if (!TestTrue(TEXT("enabled field draws"), Stars->IsVisible())) return false;
    URealtimeMeshSimple* Mesh = Cast<URealtimeMeshSimple>(Stars->GetRealtimeMesh());
    if (!TestNotNull(TEXT("one mesh built"), Mesh)) return false;
    TestTrue(TEXT("field centred on camera"), Stars->GetComponentLocation().Equals(Eye));
    TestEqual(TEXT("no collision"), Stars->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestFalse(TEXT("no shadows"), bool(Stars->CastShadow));
    TestFalse(TEXT("no ray tracing"), bool(Stars->bVisibleInRayTracing));
    TestFalse(TEXT("no distance field lighting"), bool(Stars->bAffectDistanceFieldLighting));
    TestFalse(TEXT("no dynamic indirect lighting"), bool(Stars->bAffectDynamicIndirectLighting));
    int32 Vertices = 0, Triangles = 0;
    Mesh->ProcessMesh(FRealtimeMeshBufferSetKey::Create(0, TEXT("Stars")),
        [&Vertices, &Triangles](const RealtimeMesh::FRealtimeMeshStreamSet& Streams)
        {
            if (const auto* Stream = Streams.Find(RealtimeMesh::FRealtimeMeshStreams::Position))
                Vertices = Stream->Num();
            if (const auto* Stream = Streams.Find(RealtimeMesh::FRealtimeMeshStreams::Triangles))
                Triangles = Stream->Num();
        });
    TestEqual(TEXT("four corners per star"), Vertices, 768 * 4);
    TestEqual(TEXT("two triangles per star"), Triangles, 768 * 2);
    TestFalse(TEXT("star section casts no shadows"),
        Mesh->GetSectionConfig(
                FRealtimeMeshSectionKey::CreateForPolyGroup(FRealtimeMeshBufferSetKey::Create(0, TEXT("Stars")), 0))
            .bCastsShadow);
    UMaterialInterface* Material = Stars->GetMaterial(0);
    if (!TestNotNull(TEXT("generated star material bound"), Material)) return false;
    UMaterial* Base = Material->GetMaterial();
    TestEqual(TEXT("additive sky overlay"), Base->GetBlendMode(), BLEND_Additive);
    TestTrue(TEXT("unlit sky"), Base->GetShadingModels().HasShadingModel(MSM_Unlit));
    TestFalse(TEXT("terrain occludes stars through ordinary depth testing"), bool(Base->bDisableDepthTest));
    TestFalse(TEXT("stars are not extinguished by distant terrain fog"), bool(Base->bUseTranslucencyVertexFog));
    TestEqual(TEXT("stars draw after motion blur, so fast turns never streak them"), Base->TranslucencyPass.GetValue(),
        MTP_AfterMotionBlur);
    UMaterialInstanceDynamic* StarDynamic = Cast<UMaterialInstanceDynamic>(Material);
    if (!TestNotNull(TEXT("star dynamic material"), StarDynamic)) return false;
    TestTrue(TEXT("default tint preserves the existing stars"),
        StarDynamic->K2_GetVectorParameterValue(TEXT("Tint")).Equals(FLinearColor::White));
    Settings.bEnabled = false;
    Stars->Update(Environment, Eye, Settings);
    TestFalse(TEXT("live toggle hides stars"), Stars->IsVisible());
    Settings.bEnabled = true;
    const FVector Moved = Eye + FVector(100000, 50000, -3000);
    Stars->Update(Environment, Moved, Settings);
    TestTrue(TEXT("live toggle restores stars"), Stars->IsVisible());
    TestEqual(TEXT("toggles reuse mesh"), Stars->GetRealtimeMesh(), static_cast<URealtimeMesh*>(Mesh));
    TestTrue(TEXT("camera translation keeps sky centred"), Stars->GetComponentLocation().Equals(Moved));
    TestTrue(TEXT("view translation does not rotate sky"), Stars->GetComponentRotation().IsZero());
    Environment.Night = 0.f;
    Stars->Update(Environment, Moved, Settings);
    TestFalse(TEXT("dawn hides the field"), Stars->IsVisible());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTwilightBackdropTest, "Unnamed.Voxel.Atmosphere.Twilight.Backdrop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTwilightBackdropTest::RunTest(const FString& Parameters)
{
    UStaticMeshComponent* Backdrop = VoxelStars::CreateTwilightBackdrop(GetTransientPackage());
    if (!TestNotNull(TEXT("backdrop can load its owned material and engine cube"), Backdrop)) return false;
    TestFalse(TEXT("backdrop starts hidden"), Backdrop->IsVisible());
    const FVector Eye(12000, -4000, 8200);
    VoxelStars::UpdateTwilightBackdrop(*Backdrop, Eye, .05f);
    TestTrue(TEXT("twilight strength shows backdrop"), Backdrop->IsVisible());
    TestTrue(TEXT("backdrop follows the camera"), Backdrop->GetComponentLocation().Equals(Eye));
    const FBoxSphereBounds Bounds = Backdrop->CalcBounds(Backdrop->GetComponentTransform());
    TestTrue(TEXT("every cube face beyond streamed terrain"), Bounds.BoxExtent.GetMin() >= VoxelStars::RadiusCm * .99);
    TestEqual(TEXT("backdrop has no collision"), Backdrop->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestFalse(TEXT("backdrop has no shadows"), bool(Backdrop->CastShadow));
    TestFalse(TEXT("backdrop has no ray tracing"), bool(Backdrop->bVisibleInRayTracing));
    TestFalse(TEXT("backdrop has no indirect lighting"), bool(Backdrop->bAffectDynamicIndirectLighting));
    TestFalse(TEXT("backdrop has no distance fields"), bool(Backdrop->bAffectDistanceFieldLighting));
    UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Backdrop->GetMaterial(0));
    if (!TestNotNull(TEXT("backdrop has a dynamic material"), Material)) return false;
    TestEqual(TEXT("backdrop strength reaches shader"), Material->K2_GetScalarParameterValue(TEXT("Intensity")), .05f);
    TestTrue(TEXT("backdrop navy tint"),
        Material->K2_GetVectorParameterValue(TEXT("Tint")).Equals(FLinearColor(.02f, .04f, .12f)));
    TestFalse(TEXT("terrain still occludes backdrop"), bool(Material->GetMaterial()->bDisableDepthTest));
    TestTrue(TEXT("cube visible from inside"), bool(Material->GetMaterial()->TwoSided));
    UVoxelStars* Stars = NewObject<UVoxelStars>();
    FVoxelEnvironment Environment;
    Environment.Night = 1.f;
    Stars->Update(Environment, Eye, VoxelStars::FSettings());
    TestNull(TEXT("backdrop with Stars 0 does not allocate a star mesh"), Stars->GetRealtimeMesh());
    TestTrue(TEXT("backdrop with Stars 0 stays visible"), Backdrop->IsVisible());
    VoxelStars::UpdateTwilightBackdrop(*Backdrop, Eye, 0.f);
    TestFalse(TEXT("twilight off hides the backdrop"), Backdrop->IsVisible());
    Stars->Update(Environment, Eye, {true, true, true, .3f});
    TestTrue(TEXT("stars remain independent with no twilight backdrop"), Stars->IsVisible());
    TestTrue(TEXT("backdrop sorts before stars and nearby water/glass"),
        Backdrop->TranslucencySortPriority < Stars->TranslucencySortPriority && Stars->TranslucencySortPriority < 0);
    const FVector Moved = Eye + FVector(100000, -50000, 3000);
    VoxelStars::UpdateTwilightBackdrop(*Backdrop, Moved, .02f);
    TestTrue(TEXT("backdrop reuses its material across toggles"), Backdrop->GetMaterial(0) == Material);
    TestTrue(TEXT("backdrop remains camera-centred after moving"), Backdrop->GetComponentLocation().Equals(Moved));
    TestEqual(TEXT("updated strength reaches shader"), Material->K2_GetScalarParameterValue(TEXT("Intensity")), .02f);
    VoxelStars::UpdateTwilightBackdrop(*Backdrop, Moved, -1.f);
    TestFalse(TEXT("nonpositive strength hides backdrop"), Backdrop->IsVisible());
    return true;
}
#endif
