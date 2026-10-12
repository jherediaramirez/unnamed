#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Curves/CurveFloat.h"
#include "Blocks/VoxelBlockTable.h"
#include "Engine/Level.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "UnnamedProjectSettings.h"
#include "Voxel/VoxelLook.h"

namespace
{
    // The exposure an FExposure gives over the metered EV100s that matter (caves to past the cap), as one number.
    double DescribeExposure(const VoxelLook::FExposure& Exposure)
    {
        double Sum = 0;
        for (double Metered = 2; Metered <= 15; Metered += .25)
            Sum += FMath::Log2(VoxelLook::ExposureAt(Exposure, Metered)) * Metered;
        return Sum;
    }

    // What ApplyLook writes, as named numbers, so a test can see which ones a dial moved.
    TArray<TPair<const TCHAR*, double>> DescribeLookTargets(const VoxelLook::FTargets& Targets)
    {
        return {{TEXT("volume exposure"),
                    Targets.Exposure.Bias + 10 * Targets.Exposure.MinEV + 100 * Targets.Exposure.MaxEV +
                        1000 * Targets.Exposure.LocalGreyBias},
            {TEXT("exposure curve"), DescribeExposure(Targets.Exposure)},
            {TEXT("local contrast"), Targets.LocalContrast.Get(-1.f)}, {TEXT("film"), double(Targets.bFilm)},
            {TEXT("sky light"), Targets.SkyLightIntensity}, {TEXT("sky luminance"), Targets.SkyLuminance},
            {TEXT("fog colour"), Targets.FogColorScale}, {TEXT("low glow"), Targets.LowGlow},
            {TEXT("water specular"), Targets.WaterSpecular}, {TEXT("speed up"), Targets.SpeedUp}};
    }

    // The targets that differ between two looks, joined.
    FString ChangedLookTargets(const FVoxelLook& A, const FVoxelLook& B)
    {
        const TArray<TPair<const TCHAR*, double>> Before = DescribeLookTargets(VoxelLook::Solve(A));
        const TArray<TPair<const TCHAR*, double>> After = DescribeLookTargets(VoxelLook::Solve(B));
        TArray<FString> Changed;
        for (int32 Index = 0; Index < Before.Num(); ++Index)
            if (!FMath::IsNearlyEqual(Before[Index].Value, After[Index].Value, 1e-6)) Changed.Add(Before[Index].Key);
        return FString::Join(Changed, TEXT(", "));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FVoxelLookTest, "Unnamed.Voxel.Look", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLookTest::RunTest(const FString& Parameters)
{
    // Today's rig: bias -1, EV100 6 to 12.5, local exposure middle grey bias +2 (CreateGeneratedMap.py), no curve.
    const VoxelLook::FExposure Today = VoxelLook::SolveExposure(0, 12.5);
    TestTrue(TEXT("SolveExposure(0, 12.5) is today's -1 / 6 / 12.5 / +2 without a curve"),
        Today.Bias == -1 && Today.MinEV == 6 && Today.MaxEV == 12.5 && Today.LocalGreyBias == 2 &&
            Today.Curve.IsEmpty());

    // Any offset and pin: the floor stays 2^-7, views at the pin or brighter get 2^-(13.5 + DayEV), views metered
    // PinRampStops below the pin or darker (caves) get today's exposure.
    FRandomStream Random(20261012);
    int32 Wrong = 0;
    for (int32 Case = 0; Case < 200; ++Case)
    {
        const double DayEV = Random.FRandRange(-1.f, 2.f), Cap = Random.FRandRange(7.5f, 12.5f);
        const VoxelLook::FExposure Exposure = VoxelLook::SolveExposure(DayEV, Cap);
        const double Day = FMath::Pow(2., -(13.5 + DayEV));
        TArray<FString> Problems;
        if (!FMath::IsNearlyEqual(VoxelLook::ExposureAt(Exposure, -10), FMath::Pow(2., -7.), 1e-12))
            Problems.Add(TEXT("floor"));
        for (double Metered = Cap; Metered <= 16; Metered += .25)
            if (!FMath::IsNearlyEqual(VoxelLook::ExposureAt(Exposure, Metered) / Day, 1., 1e-9))
            {
                Problems.Add(FString::Printf(TEXT("daylight at EV %.2f"), Metered));
                break;
            }
        for (double Metered = -2; Metered <= Cap - VoxelLook::PinRampStops; Metered += .25)
            if (!FMath::IsNearlyEqual(
                    VoxelLook::ExposureAt(Exposure, Metered) / VoxelLook::ExposureAt(Today, Metered), 1., 1e-9))
            {
                Problems.Add(FString::Printf(TEXT("cave at EV %.2f"), Metered));
                break;
            }
        if (!Problems.IsEmpty())
        {
            AddError(
                FString::Printf(TEXT("DayEV %.3f pin %.3f: %s off"), DayEV, Cap, *FString::Join(Problems, TEXT(", "))));
            ++Wrong;
        }
    }
    TestEqual(TEXT("cases off the floor, the daylight or today's cave exposure"), Wrong, 0);

    // The variables' defaults are the struct's.
    TestTrue(TEXT("voxel.Look.* read as their defaults"), FVoxelLook::Read() == FVoxelLook());

    // Each dial moves only its own targets; the map's volume keeps its exposure values, the curve does the rest.
    // The look before the pick, the dials' origin: each dial's move is seen from it.
    FVoxelLook Defaults;
    Defaults.DayEV = 0;
    Defaults.Sky = 1;
    Defaults.SkyPin = 0;
    Defaults.SkyPinAdapt = 0;
    const FVoxelLook Picked;
    TestTrue(TEXT("the defaults are Jesus's pick: DayEV 0.6, SkyPin with 2 stops, sky 1.637, the rest as before"),
        Picked.DayEV == .6f && Picked.SkyPin == 1 && Picked.SkyPinAdapt == 2.f && Picked.Sky == 1.637f &&
            Picked.Cap == 12.5f && Picked.SkyFill == 1.f && Picked.LocalExposure == 1 && Picked.Film == 0 &&
            Picked.LowGlow == 1.f && Picked.WaterSpecular == .5f && Picked.SpeedUp == 2.f);
    auto With = [&Defaults](TFunctionRef<void(FVoxelLook&)> Change)
    {
        FVoxelLook Look = Defaults;
        Change(Look);
        return Look;
    };
    const TPair<FVoxelLook, const TCHAR*> Dials[] = {
        {With([](FVoxelLook& L) { L.DayEV = .6f; }), TEXT("exposure curve")},
        {With([](FVoxelLook& L) { L.Cap = 11.f; }), TEXT("exposure curve")},
        {With([](FVoxelLook& L) { L.Sky = 1.5f; }), TEXT("sky luminance, fog colour")},
        {With([](FVoxelLook& L) { L.SkyFill = .333f; }), TEXT("sky light")},
        {With([](FVoxelLook& L) { L.LocalExposure = 0; }), TEXT("local contrast")},
        {With([](FVoxelLook& L) { L.Film = 1; }), TEXT("film")},
        {With([](FVoxelLook& L) { L.LowGlow = .1f; }), TEXT("low glow")},
        {With([](FVoxelLook& L) { L.WaterSpecular = .25f; }), TEXT("water specular")},
        {With([](FVoxelLook& L) { L.SpeedUp = 6.f; }), TEXT("speed up")},
        // SkyPin alone moves nothing until the camera has open sky (the pin; below).
        // SkyPin alone moves nothing until the camera has open sky; SkyPinAdapt only acts with SkyPin (below).
        {With([](FVoxelLook& L) { L.SkyPin = 1; }), TEXT("")},
        {With([](FVoxelLook& L) { L.SkyPinAdapt = 2.f; }), TEXT("")}};
    // With open sky (pin 1): SkyPin moves the volume's exposure, and SkyPinAdapt moves it again on top of SkyPin.
    auto ChangedAtPin = [](const FVoxelLook& A, const FVoxelLook& B)
    { return DescribeLookTargets(VoxelLook::Solve(A, 1)) != DescribeLookTargets(VoxelLook::Solve(B, 1)); };
    const FVoxelLook SkyPinned = With([](FVoxelLook& L) { L.SkyPin = 1; });
    TestTrue(TEXT("with open sky, SkyPin and then SkyPinAdapt each move the exposure"),
        ChangedAtPin(Defaults, SkyPinned) &&
            ChangedAtPin(SkyPinned,
                With(
                    [](FVoxelLook& L)
                    {
                        L.SkyPin = 1;
                        L.SkyPinAdapt = 2.f;
                    })));
    for (const TPair<FVoxelLook, const TCHAR*>& Dial : Dials)
        TestEqual(TEXT("targets one dial moves"), ChangedLookTargets(Defaults, Dial.Key), FString(Dial.Value));
    // A lower pin alone: the forest's metering (EV100 11, scout T's cause 3) gets the daylight exposure, not 2.8x it.
    const VoxelLook::FExposure Pinned = VoxelLook::Solve(With([](FVoxelLook& L) { L.Cap = 11.f; })).Exposure;
    TestTrue(TEXT("a pin at 11 gives a view metered at 11 or 12 the daylight exposure, keeps the floor and caves"),
        FMath::IsNearlyEqual(VoxelLook::ExposureAt(Pinned, 11), FMath::Pow(2., -13.5), 1e-12) &&
            FMath::IsNearlyEqual(VoxelLook::ExposureAt(Pinned, 12), FMath::Pow(2., -13.5), 1e-12) &&
            FMath::IsNearlyEqual(VoxelLook::ExposureAt(Pinned, 20), VoxelLook::ExposureAt(Today, 20), 1e-12) &&
            FMath::IsNearlyEqual(VoxelLook::ExposureAt(Pinned, 9), VoxelLook::ExposureAt(Today, 9), 1e-12) &&
            VoxelLook::ExposureAt(Today, 11) > 2.8 * FMath::Pow(2., -13.5));

    // SkyPin: no open sky (pin 0) is today's at every metering, caves included; open sky (pin 1) gives every view the
    // daylight exposure, a canopy metered like a cave (EV100 6.7) included; a half pin lies between them (darker
    // daylight).
    int32 PinWrong = 0;
    for (int32 Case = 0; Case < 50; ++Case)
    {
        const double DayEV = Random.FRandRange(-1.f, 2.f);
        const VoxelLook::FExposure None = VoxelLook::SolveSkyPinned(DayEV, 0);
        const VoxelLook::FExposure Full = VoxelLook::SolveSkyPinned(DayEV, 1);
        const VoxelLook::FExposure Half = VoxelLook::SolveSkyPinned(DayEV, .5);
        for (double Metered = -2; Metered <= 16; Metered += .25)
        {
            const double TodayAt = VoxelLook::ExposureAt(Today, Metered);
            const double DayAt = FMath::Pow(2., -(13.5 + DayEV));
            const double HalfAt = VoxelLook::ExposureAt(Half, Metered);
            if (!FMath::IsNearlyEqual(VoxelLook::ExposureAt(None, Metered) / TodayAt, 1., 1e-9) ||
                !FMath::IsNearlyEqual(VoxelLook::ExposureAt(Full, Metered) / DayAt, 1., 1e-9) ||
                (DayEV >= 0 &&
                    (HalfAt > FMath::Max(TodayAt, DayAt) * (1 + 1e-9) ||
                        HalfAt < FMath::Min(TodayAt, DayAt) * (1 - 1e-9))))
            {
                AddError(FString::Printf(TEXT("SkyPin DayEV %.3f at EV %.2f off"), DayEV, Metered));
                ++PinWrong;
                break;
            }
        }
    }
    TestEqual(TEXT("SkyPin cases off today's (pin 0) or the daylight exposure (pin 1)"), PinWrong, 0);
    // SkyPinAdapt: under open sky a darker view may still brighten, by at most that many stops; caves keep today's.
    const VoxelLook::FExposure Adapt2 = VoxelLook::SolveSkyPinned(.6, 1, 2);
    const double Day06 = FMath::Pow(2., -14.1);
    TestTrue(TEXT("SkyPinAdapt 2: daylight at EV 12.5 and over, 2 stops brighter at most (EV 10.5 and under)"),
        FMath::IsNearlyEqual(VoxelLook::ExposureAt(Adapt2, 13) / Day06, 1., 1e-9) &&
            FMath::IsNearlyEqual(VoxelLook::ExposureAt(Adapt2, 11.5) / Day06, 2., 1e-9) &&
            FMath::IsNearlyEqual(VoxelLook::ExposureAt(Adapt2, 6.7) / Day06, 4., 1e-9) &&
            FMath::IsNearlyEqual(
                VoxelLook::ExposureAt(VoxelLook::SolveSkyPinned(.6, 0, 2), 8) / VoxelLook::ExposureAt(Today, 8), 1.,
                1e-9));
    TestTrue(TEXT("Solve uses the pin only with SkyPin 1"),
        DescribeExposure(VoxelLook::Solve(With([](FVoxelLook& L) { L.SkyPin = 1; }), 1).Exposure) ==
                DescribeExposure(VoxelLook::SolveSkyPinned(0, 1)) &&
            DescribeExposure(VoxelLook::Solve(Defaults, 1).Exposure) == DescribeExposure(Today));

    // The sky test: full opaque cubes block the rays; leaves, glass and air let them through.
    FVoxelStateInfo Stone, Leaves, Glass, Air;
    Stone.bCastsOcclusion = Leaves.bCastsOcclusion = true;
    Leaves.bLeaves = true;
    TestTrue(TEXT("stone blocks the sky; leaves, glass and air don't"),
        VoxelLook::BlocksSky(Stone) && !VoxelLook::BlocksSky(Leaves) && !VoxelLook::BlocksSky(Glass) &&
            !VoxelLook::BlocksSky(Air));
    const FVector Eye(.5, .5, .5);
    auto Openness = [&Eye](TFunctionRef<const FVoxelStateInfo&(const FIntVector&)> At)
    { return VoxelLook::SkyOpenness(Eye, [&At](const FIntVector& Cell) { return VoxelLook::BlocksSky(At(Cell)); }); };
    // Open ground: stone below the eye only.
    const double Open =
        Openness([&](const FIntVector& Cell) -> const FVoxelStateInfo& { return Cell.Z < 0 ? Stone : Air; });
    // A cave: stone everywhere but a 3 x 3 x 3 pocket around the eye.
    const double Cave = Openness([&](const FIntVector& Cell) -> const FVoxelStateInfo&
        { return FMath::Abs(Cell.X) <= 1 && FMath::Abs(Cell.Y) <= 1 && FMath::Abs(Cell.Z) <= 1 ? Air : Stone; });
    // A forest: a leaf canopy 3 to 6 blocks over the eye, everywhere.
    const double Canopy = Openness([&](const FIntVector& Cell) -> const FVoxelStateInfo&
        { return Cell.Z < 0 ? Stone : (Cell.Z >= 3 && Cell.Z <= 6 ? Leaves : Air); });
    // A cave mouth: a stone roof over the eye's side of X = 1, from 2 blocks up; the rays east get out.
    const double Mouth = Openness([&](const FIntVector& Cell) -> const FVoxelStateInfo&
        { return Cell.Z < 0 || (Cell.X <= 0 && Cell.Z >= 2) ? Stone : Air; });
    TestTrue(FString::Printf(TEXT("open sky: open ground %.3f = 1, cave %.3f = 0, canopy %.3f = 1, cave mouth %.3f "
                                  "between, with a pin between"),
                 Open, Cave, Canopy, Mouth),
        Open == 1 && Cave == 0 && Canopy == 1 && Mouth > 0 && Mouth < 1 && VoxelLook::SkyPinFor(Mouth) > 0 &&
            VoxelLook::SkyPinFor(Mouth) < 1 && VoxelLook::SkyPinFor(0) == 0 && VoxelLook::SkyPinFor(1) == 1);

    // The post-process writes: defaults clear the film, local-exposure and curve overrides the map never sets.
    FPostProcessSettings Post;
    UCurveFloat* Curve = NewObject<UCurveFloat>(GetTransientPackage());
    VoxelLook::ApplyToPostProcess(VoxelLook::Solve(Defaults), Post, Curve);
    TestTrue(TEXT("defaults write the map's exposure only"),
        Post.bOverride_AutoExposureBias && Post.AutoExposureBias == -1.f && Post.AutoExposureMinBrightness == 6.f &&
            Post.AutoExposureMaxBrightness == 12.5f && Post.LocalExposureMiddleGreyBias == 2.f &&
            !Post.bOverride_AutoExposureBiasCurve && !Post.AutoExposureBiasCurve && !Post.bOverride_FilmSlope &&
            !Post.bOverride_FilmToe && !Post.bOverride_FilmShoulder && !Post.bOverride_FilmWhiteClip &&
            !Post.bOverride_LocalExposureHighlightContrastScale && !Post.bOverride_LocalExposureShadowContrastScale);
    VoxelLook::ApplyToPostProcess(VoxelLook::Solve(With(
                                      [](FVoxelLook& L)
                                      {
                                          L.DayEV = .6f;
                                          L.Cap = 11.f;
                                      })),
        Post, Curve);
    TestTrue(TEXT("DayEV 0.6 and a pin at 11 write the curve: (9.5, 0), (11, -2.1), (12.5, -0.6), linear"),
        Post.bOverride_AutoExposureBiasCurve && Post.AutoExposureBiasCurve == Curve &&
            Curve->FloatCurve.GetNumKeys() == 3 && FMath::IsNearlyEqual(Curve->GetFloatValue(9.5f), 0.f, 1e-5f) &&
            FMath::IsNearlyEqual(Curve->GetFloatValue(10.25f), -1.05f, 1e-5f) &&
            FMath::IsNearlyEqual(Curve->GetFloatValue(11.f), -2.1f, 1e-5f) &&
            FMath::IsNearlyEqual(Curve->GetFloatValue(14.f), -.6f, 1e-5f) &&
            FMath::IsNearlyEqual(Curve->GetFloatValue(2.f), 0.f, 1e-5f));
    VoxelLook::ApplyToPostProcess(VoxelLook::Solve(With([](FVoxelLook& L) { L.Film = 1; })), Post, Curve);
    TestTrue(TEXT("Film 1 writes the four film settings and clears the curve"),
        Post.bOverride_FilmSlope && Post.FilmSlope == 1.10f && Post.FilmToe == .12f && Post.FilmShoulder == .70f &&
            Post.FilmWhiteClip == .04f && !Post.bOverride_AutoExposureBiasCurve);
    VoxelLook::ApplyToPostProcess(VoxelLook::Solve(With([](FVoxelLook& L) { L.LocalExposure = 0; })), Post, Curve);
    TestTrue(TEXT("LocalExposure 0 sets both contrast scales to 1 and clears the film"),
        Post.bOverride_LocalExposureHighlightContrastScale && Post.LocalExposureHighlightContrastScale == 1.f &&
            Post.LocalExposureShadowContrastScale == 1.f && !Post.bOverride_FilmSlope);

    // Each variable reaches only its own field.
    struct FVariable
    {
        const TCHAR* Name;
        const TCHAR* Value;
        TFunction<void(FVoxelLook&)> Set;
    };
    const FVariable Variables[] = {{TEXT("voxel.Look.DayEV"), TEXT("0.3"), [](FVoxelLook& L) { L.DayEV = .3f; }},
        {TEXT("voxel.Look.Cap"), TEXT("11"), [](FVoxelLook& L) { L.Cap = 11.f; }},
        {TEXT("voxel.Look.Sky"), TEXT("1.5"), [](FVoxelLook& L) { L.Sky = 1.5f; }},
        {TEXT("voxel.Look.SkyFill"), TEXT("0.5"), [](FVoxelLook& L) { L.SkyFill = .5f; }},
        {TEXT("voxel.Look.LocalExposure"), TEXT("0"), [](FVoxelLook& L) { L.LocalExposure = 0; }},
        {TEXT("voxel.Look.Film"), TEXT("1"), [](FVoxelLook& L) { L.Film = 1; }},
        {TEXT("voxel.Look.LowGlow"), TEXT("0.1"), [](FVoxelLook& L) { L.LowGlow = .1f; }},
        {TEXT("voxel.Look.WaterSpecular"), TEXT("0.25"), [](FVoxelLook& L) { L.WaterSpecular = .25f; }},
        {TEXT("voxel.Look.SkyPin"), TEXT("0"), [](FVoxelLook& L) { L.SkyPin = 0; }},
        {TEXT("voxel.Look.SpeedUp"), TEXT("6"), [](FVoxelLook& L) { L.SpeedUp = 6.f; }},
        {TEXT("voxel.Look.SkyPinAdapt"), TEXT("1"), [](FVoxelLook& L) { L.SkyPinAdapt = 1.f; }}};
    for (const FVariable& Each : Variables)
    {
        IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Each.Name);
        if (!TestNotNull(Each.Name, Variable)) continue;
        const FString Saved = Variable->GetString();
        Variable->Set(Each.Value, ECVF_SetByCode);
        FVoxelLook Expected;
        Each.Set(Expected);
        TestTrue(FString::Printf(TEXT("%s %s moves its own field only"), Each.Name, Each.Value),
            FVoxelLook::Read() == Expected);
        Variable->Set(*Saved, ECVF_SetByCode);
    }
    TestTrue(TEXT("voxel.Look.* restored"), FVoxelLook::Read() == FVoxelLook());

    // The defaults are the map: L_Generated's volume, sky light and atmosphere, and MPC_VoxelLook, as authored.
    const VoxelLook::FTargets Targets = VoxelLook::Solve(Picked);
    UWorld* Map = Cast<UWorld>(UUnnamedProjectSettings::Get().GeneratedMap.TryLoad());
    if (TestNotNull(TEXT("L_Generated"), Map) && TestNotNull(TEXT("L_Generated's level"), Map->PersistentLevel.Get()))
    {
        const APostProcessVolume* Volume = nullptr;
        const ASkyLight* SkyLight = nullptr;
        const ASkyAtmosphere* Atmosphere = nullptr;
        for (const AActor* Actor : Map->PersistentLevel->Actors)
        {
            if (const APostProcessVolume* Each = Cast<APostProcessVolume>(Actor); Each && Each->bUnbound) Volume = Each;
            if (const ASkyLight* Each = Cast<ASkyLight>(Actor)) SkyLight = Each;
            if (const ASkyAtmosphere* Each = Cast<ASkyAtmosphere>(Actor)) Atmosphere = Each;
        }
        if (TestNotNull(TEXT("the map's unbound volume"), Volume))
        {
            const FPostProcessSettings& Authored = Volume->Settings;
            TestTrue(TEXT("the map's exposure is the default look's"),
                FMath::IsNearlyEqual(Authored.AutoExposureBias, float(Targets.Exposure.Bias), 1e-4f) &&
                    FMath::IsNearlyEqual(Authored.AutoExposureMinBrightness, float(Targets.Exposure.MinEV), 1e-4f) &&
                    FMath::IsNearlyEqual(Authored.AutoExposureMaxBrightness, float(Targets.Exposure.MaxEV), 1e-4f) &&
                    FMath::IsNearlyEqual(
                        Authored.LocalExposureMiddleGreyBias, float(Targets.Exposure.LocalGreyBias), 1e-4f) &&
                    Authored.bOverride_AutoExposureSpeedUp &&
                    FMath::IsNearlyEqual(Authored.AutoExposureSpeedUp, Targets.SpeedUp, 1e-4f) &&
                    !Authored.bOverride_AutoExposureBiasCurve);
            TestTrue(TEXT("the map sets no film or local contrast (the defaults clear them)"),
                !Authored.bOverride_FilmSlope && !Authored.bOverride_FilmToe && !Authored.bOverride_FilmShoulder &&
                    !Authored.bOverride_FilmWhiteClip && !Authored.bOverride_LocalExposureHighlightContrastScale &&
                    !Authored.bOverride_LocalExposureShadowContrastScale);
        }
        if (TestNotNull(TEXT("the map's sky light"), SkyLight) && SkyLight->GetLightComponent())
            TestEqual(TEXT("the map's sky light is the default look's"), SkyLight->GetLightComponent()->Intensity,
                Targets.SkyLightIntensity);
        if (TestNotNull(TEXT("the map's sky atmosphere"), Atmosphere) && Atmosphere->GetComponent())
            TestTrue(TEXT("the map's sky luminance is the default look's"),
                Atmosphere->GetComponent()->SkyAndAerialPerspectiveLuminanceFactor.Equals(
                    FLinearColor(Targets.SkyLuminance, Targets.SkyLuminance, Targets.SkyLuminance), 1e-6f));
    }
    TestEqual(TEXT("the default look scales the wall fog's colour with the sky"), Targets.FogColorScale,
        Targets.SkyLuminance);
    const auto* Collection = Cast<UMaterialParameterCollection>(
        UUnnamedProjectSettings::GetBlockAssetPath(VoxelLook::CollectionName).TryLoad());
    if (TestNotNull(TEXT("MPC_VoxelLook"), Collection))
    {
        auto Default = [Collection](const TCHAR* Name)
        {
            for (const FCollectionScalarParameter& Each : Collection->ScalarParameters)
                if (Each.ParameterName == Name) return Each.DefaultValue;
            return -1.f;
        };
        TestEqual(TEXT("MPC_VoxelLook's LowGlow is the default look's"), Default(VoxelLook::LowGlowParameter),
            Targets.LowGlow);
        TestEqual(TEXT("MPC_VoxelLook's WaterSpecular is the default look's"),
            Default(VoxelLook::WaterSpecularParameter), Targets.WaterSpecular);
    }
    const IConsoleVariable* DefaultBias =
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.Bias"));
    TestTrue(TEXT("the local grey bias assumes the engine's default compensation +1"),
        DefaultBias && DefaultBias->GetFloat() == 1.f);
    return true;
}

#endif
