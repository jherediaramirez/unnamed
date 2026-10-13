#if WITH_DEV_AUTOMATION_TESTS
#include "Clock/VoxelWorldClock.h"
#include "Engine/Scene.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Sky/VoxelEnvironment.h"
#include "Voxel/VoxelAtmosphere.h"
#include "Voxel/VoxelLook.h"
#include "Voxel/VoxelTwilight.h"

namespace
{
    FVoxelEnvironment TwilightEnvironmentAt(int64 Ticks)
    {
        FVoxelWorldClock Clock;
        Clock.SetTotal(Ticks);
        return VoxelEnvironment::Evaluate(Clock, true);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTwilightEnvelopeTest, "Unnamed.Voxel.Atmosphere.Twilight.Envelope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTwilightEnvelopeTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("noon needs no bridge"), VoxelAtmosphere::TwilightWeight(TwilightEnvironmentAt(6000)), 0.f);
    TestEqual(
        TEXT("midnight keeps the existing night"), VoxelAtmosphere::TwilightWeight(TwilightEnvironmentAt(18000)), 0.f);
    for (const int64 Ticks : {13000, 13155, 13156, 22844, 22845, 23000})
    {
        const FVoxelEnvironment Environment = TwilightEnvironmentAt(Ticks);
        TestTrue(
            FString::Printf(TEXT("the measured dip and exact light handover at %lld retain a strong bridge"), Ticks),
            VoxelAtmosphere::TwilightWeight(Environment) > .85f);
        TestEqual(TEXT("a new moon gets the same bridge"),
            VoxelAtmosphere::TwilightWeight(TwilightEnvironmentAt(4 * FVoxelWorldClock::TicksPerDay + Ticks)),
            VoxelAtmosphere::TwilightWeight(Environment));
    }
    for (const int64 Dusk : {12500, 13000, 13155, 13156, 13500})
    {
        const int64 Dawn = 36000 - Dusk;
        TestNearlyEqual(FString::Printf(TEXT("dusk %lld mirrors dawn %lld"), Dusk, Dawn),
            double(VoxelAtmosphere::TwilightWeight(TwilightEnvironmentAt(Dusk))),
            double(VoxelAtmosphere::TwilightWeight(TwilightEnvironmentAt(Dawn))), 1e-4);
    }

    // A one-tick sweep includes both light handovers and the day wrap. No discontinuity may exceed half a percent
    // of the fill's full strength; there is no copy of the envelope formula in this test.
    float Previous = VoxelAtmosphere::TwilightWeight(TwilightEnvironmentAt(0));
    float LargestStep = 0.f;
    bool bBounded = true;
    bool bReturnedToNight = false;
    for (int64 Ticks = 1; Ticks <= FVoxelWorldClock::TicksPerDay + 1; ++Ticks)
    {
        const FVoxelEnvironment Environment = TwilightEnvironmentAt(Ticks);
        const float Weight = VoxelAtmosphere::TwilightWeight(Environment);
        bBounded &= FMath::IsFinite(Weight) && Weight >= 0.f && Weight <= 1.f;
        LargestStep = FMath::Max(LargestStep, FMath::Abs(Weight - Previous));
        if (Ticks > 13500 && Ticks < 18000 && Environment.Moonlight == 1.f)
        {
            bReturnedToNight = true;
            bBounded &= Weight == 0.f;
        }
        Previous = Weight;
    }
    TestTrue(
        TEXT("the whole cycle is finite, bounded, and back to the existing night once moonlight is full"), bBounded);
    TestTrue(TEXT("the bridge ends before midnight"), bReturnedToNight);
    TestTrue(TEXT("no fill jump at either light handover or the clock wrap"), LargestStep < .005f);
    AddInfo(FString::Printf(TEXT("maximum twilight weight change per clock tick: %.6f"), LargestStep));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTwilightSkyEnvelopeTest, "Unnamed.Voxel.Atmosphere.Twilight.SkyEnvelope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTwilightSkyEnvelopeTest::RunTest(const FString& Parameters)
{
    TestEqual(
        TEXT("daylight needs no sky bridge"), VoxelAtmosphere::TwilightSkyWeight(TwilightEnvironmentAt(6000)), 0.f);
    TestEqual(TEXT("midnight keeps the existing navy sky"),
        VoxelAtmosphere::TwilightSkyWeight(TwilightEnvironmentAt(18000)), 0.f);
    for (const int64 Ticks : {13500, 22500})
    {
        const FVoxelEnvironment Environment = TwilightEnvironmentAt(Ticks);
        const float Sky = VoxelAtmosphere::TwilightSkyWeight(Environment);
        TestTrue(FString::Printf(TEXT("the secondary sky dip at %lld retains its glow"), Ticks), Sky > .85f);
        TestTrue(FString::Printf(TEXT("sky outlasts the low-moon terrain bridge at %lld"), Ticks),
            Sky > VoxelAtmosphere::TwilightWeight(Environment) + .5f);
        TestEqual(TEXT("the same low-moon sky envelope survives a new moon"),
            VoxelAtmosphere::TwilightSkyWeight(TwilightEnvironmentAt(4 * FVoxelWorldClock::TicksPerDay + Ticks)), Sky);
    }
    float PreviousLateNight = VoxelAtmosphere::TwilightSkyWeight(TwilightEnvironmentAt(13500));
    for (const int64 Ticks : {14000, 15000, 16000, 18000})
    {
        const float Sky = VoxelAtmosphere::TwilightSkyWeight(TwilightEnvironmentAt(Ticks));
        TestTrue(FString::Printf(TEXT("sky glow continues easing towards midnight at %lld"), Ticks),
            Sky < PreviousLateNight);
        PreviousLateNight = Sky;
    }
    for (const int64 Dusk : {12500, 13000, 13500, 14000, 15000, 16000})
    {
        const int64 Dawn = 36000 - Dusk;
        TestNearlyEqual(FString::Printf(TEXT("the sky glow at %lld mirrors dawn %lld"), Dusk, Dawn),
            double(VoxelAtmosphere::TwilightSkyWeight(TwilightEnvironmentAt(Dusk))),
            double(VoxelAtmosphere::TwilightSkyWeight(TwilightEnvironmentAt(Dawn))), 1e-4);
    }

    // Sweep the actual environment across a full day, including midnight, the light handovers and clock wrap.
    // The smoothness and late-night persistence are behavior contracts, not a copy of the envelope formula.
    float Previous = VoxelAtmosphere::TwilightSkyWeight(TwilightEnvironmentAt(0));
    float LargestStep = 0.f;
    bool bBounded = FMath::IsFinite(Previous) && Previous >= 0.f && Previous <= 1.f;
    for (int64 Ticks = 1; Ticks <= FVoxelWorldClock::TicksPerDay + 1; ++Ticks)
    {
        const float Sky = VoxelAtmosphere::TwilightSkyWeight(TwilightEnvironmentAt(Ticks));
        bBounded &= FMath::IsFinite(Sky) && Sky >= 0.f && Sky <= 1.f;
        LargestStep = FMath::Max(LargestStep, FMath::Abs(Sky - Previous));
        Previous = Sky;
    }
    TestTrue(TEXT("sky strength remains finite and bounded through the whole cycle"), bBounded);
    TestTrue(TEXT("sky glow has no abrupt step at either handover, midnight or day wrap"), LargestStep < .005f);
    AddInfo(FString::Printf(TEXT("maximum sky twilight weight change per clock tick: %.6f"), LargestStep));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTwilightCompositionTest, "Unnamed.Voxel.Atmosphere.Twilight.Composition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTwilightCompositionTest::RunTest(const FString& Parameters)
{
    const FVoxelEnvironment Handover = TwilightEnvironmentAt(13155);
    VoxelAtmosphere::FNight Night;
    Night.Night = Handover.Night;
    Night.ExposureStops = 4.f;
    Night.SkyScale = .25f;
    VoxelAtmosphere::FNight Bridged = Night;
    Bridged.TwilightFloor = 4.f * VoxelAtmosphere::TwilightWeight(Handover);
    Bridged.TwilightSky = .05f * VoxelAtmosphere::TwilightSkyWeight(Handover);
    const FLinearColor Black(0, 0, 0), GrayBase(.16f, .16f, .16f);
    const FVector Top(0, 0, 1);
    for (const double Pin : {0., .5, 1.})
    {
        const VoxelLook::FTargets Day = VoxelLook::Solve(FVoxelLook(), Pin);
        VoxelLook::FTargets Before = Day, After = Day;
        VoxelAtmosphere::ComposeNight(Before, Night, Pin);
        VoxelAtmosphere::ComposeNight(After, Bridged, Pin);
        const FString Label = FString::Printf(TEXT("pin %.1f: "), Pin);
        TestEqual(Label + TEXT("exposure minimum stays unchanged"), After.Exposure.MinEV, Before.Exposure.MinEV);
        TestEqual(Label + TEXT("exposure maximum stays unchanged"), After.Exposure.MaxEV, Before.Exposure.MaxEV);
        TestEqual(Label + TEXT("local exposure stays unchanged"), After.Exposure.LocalGreyBias,
            Before.Exposure.LocalGreyBias);
        TestTrue(Label + TEXT("exposure curve stays unchanged"), After.Exposure.Curve == Before.Exposure.Curve);
        TestEqual(Label + TEXT("sky fade stays unchanged"), After.SkyLuminance, Before.SkyLuminance);
        TestEqual(Label + TEXT("haze fade stays unchanged"), After.FogColorScale, Before.FogColorScale);
        TestEqual(Label + TEXT("sky light stays unchanged"), After.SkyLightIntensity, Before.SkyLightIntensity);
        TestEqual(Label + TEXT("cave floor stays unchanged"), After.CaveFloor, Before.CaveFloor);
        TestEqual(Label + TEXT("metering compensation stays unchanged"), After.Exposure.Bias, Before.Exposure.Bias);
        TestNearlyEqual(Label + TEXT("terrain fill receives only the open-sky share"), double(After.TwilightFloor),
            double(Bridged.TwilightFloor) * Pin, 1e-6);
        TestTrue(
            Label + TEXT("the separate sky bridge is not suppressed by the terrain pin"), Bridged.TwilightSky > .04f);

        // Exercise the same final writer as ApplyLook: the new floor must never become an exposure adjustment.
        FPostProcessSettings PostBefore, PostAfter;
        VoxelLook::ApplyToPostProcess(Before, PostBefore, nullptr);
        VoxelLook::ApplyToPostProcess(After, PostAfter, nullptr);
        TestEqual(Label + TEXT("post-process minimum stays unchanged"), PostAfter.AutoExposureMinBrightness,
            PostBefore.AutoExposureMinBrightness);
        TestEqual(Label + TEXT("post-process maximum stays unchanged"), PostAfter.AutoExposureMaxBrightness,
            PostBefore.AutoExposureMaxBrightness);
        TestTrue(Label + TEXT("post-process range is enabled"),
            PostAfter.bOverride_AutoExposureMinBrightness && PostAfter.bOverride_AutoExposureMaxBrightness);
        // The twilight floor is HDR before DOF. The original cave floor stays a separate display-space pass.
        const FLinearColor HdrPixel =
            VoxelTwilight::EvaluateFloor(Black, GrayBase, Top, 500, After.TwilightFloor, 6000, 8000);
        if (Pin == 0.)
        {
            TestEqual(TEXT("enclosed cave retains the original minimum"), After.Exposure.MinEV, Day.Exposure.MinEV);
            TestEqual(TEXT("enclosed cave retains the original maximum"), After.Exposure.MaxEV, Day.Exposure.MaxEV);
            TestTrue(TEXT("enclosed cave receives no HDR twilight fill"), HdrPixel.Equals(Black, 1e-7f));
            const FLinearColor BeforePixel =
                VoxelLook::CaveFloorDisplay(Black, GrayBase, Top, 500, Before.CaveFloor, 6000, 8000);
            const FLinearColor AfterPixel =
                VoxelLook::CaveFloorDisplay(Black, GrayBase, Top, 500, After.CaveFloor, 6000, 8000);
            TestTrue(TEXT("enclosed cave retains precisely the old cave-floor pixel"),
                AfterPixel.Equals(BeforePixel, 1e-7f));
        }
        else
        {
            TestTrue(Label + TEXT("black-scene gray terrain is filled through the exact light handover"),
                HdrPixel.R > 0.f && HdrPixel.G > 0.f && HdrPixel.B > 0.f);
            TestTrue(Label + TEXT("the terrain fill is cool"), HdrPixel.B > HdrPixel.G && HdrPixel.G > HdrPixel.R);
            TestTrue(Label + TEXT("the fill remains faint and finite in HDR"),
                FMath::IsFinite(HdrPixel.B) && HdrPixel.B < GrayBase.B);
        }
    }
    const FLinearColor HdrSource(2.f, 1.5f, .8f, .65f);
    TestTrue(TEXT("zero strength preserves HDR values above one and their alpha"),
        VoxelTwilight::EvaluateFloor(HdrSource, GrayBase, Top, 500, 0.f, 6000, 8000).Equals(HdrSource, 1e-7f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTwilightSwitchesTest, "Unnamed.Voxel.Atmosphere.Twilight.Switches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTwilightSwitchesTest::RunTest(const FString& Parameters)
{
    const TCHAR* Names[] = {TEXT("voxel.Atmosphere.Cycle"), TEXT("voxel.Atmosphere.Night"),
        TEXT("voxel.Atmosphere.Twilight"), TEXT("voxel.Atmosphere.TwilightFloor"),
        TEXT("voxel.Atmosphere.TwilightSky")};
    struct FTwilightSavedCVar
    {
        IConsoleVariable* Variable;
        FString Value;
        EConsoleVariableFlags Flags;
    };
    TArray<FTwilightSavedCVar> Saved;
    for (const TCHAR* Name : Names)
    {
        IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        if (!TestNotNull(Name, Variable)) return false;
        Saved.Add({Variable, Variable->GetString(), Variable->GetFlags()});
    }
    {
        ON_SCOPE_EXIT
        {
            for (const FTwilightSavedCVar& Each : Saved)
            {
                // Console priority also works when automation was launched with an explicit console override.
                // Remove our history level when possible; without history restore value and priority explicitly.
                if ((Each.Flags & ECVF_SetByMask) < ECVF_SetByConsole) Each.Variable->Unset(ECVF_SetByConsole);
                if (Each.Variable->GetString() != Each.Value) Each.Variable->Set(*Each.Value, ECVF_SetByConsole);
                Each.Variable->SetFlags(Each.Flags);
            }
        };
        const auto Set = [&Saved](int32 Index, float Value) { Saved[Index].Variable->Set(Value, ECVF_SetByConsole); };
        Set(3, 4.f);
        Set(4, .05f);
        const FVoxelEnvironment Dusk = TwilightEnvironmentAt(13000);
        for (const int32 Cycle : {0, 1})
            for (const int32 Night : {0, 1})
                for (const int32 Twilight : {0, 1})
                {
                    Set(0, Cycle);
                    Set(1, Night);
                    Set(2, Twilight);
                    const VoxelAtmosphere::FNight Read = VoxelAtmosphere::ReadNight(Dusk);
                    const FString Label =
                        FString::Printf(TEXT("Cycle %d Night %d Twilight %d: "), Cycle, Night, Twilight);
                    if (Cycle && Night && Twilight)
                        TestTrue(Label + TEXT("the measured gap gets terrain and sky fill"),
                            Read.TwilightFloor > 3.f && Read.TwilightSky > .04f);
                    else
                        TestTrue(
                            Label + TEXT("both bridges are off"), Read.TwilightFloor == 0.f && Read.TwilightSky == 0.f);
                    if (!Cycle || !Night)
                        TestTrue(Label + TEXT("the entire night remains off"), Read == VoxelAtmosphere::FNight());
                }
        // All three switches are now on. Reading an unchanged clock gives a stable quantized target.
        const VoxelAtmosphere::FNight Enabled = VoxelAtmosphere::ReadNight(Dusk);
        TestTrue(
            TEXT("a paused snapshot gives the same target every read"), Enabled == VoxelAtmosphere::ReadNight(Dusk));
        TestNearlyEqual(TEXT("quantizing adds no more than half a 1/64 weight step"),
            double(Enabled.TwilightFloor / 4.f), double(VoxelAtmosphere::TwilightWeight(Dusk)), 1. / 128 + 1e-6);
        TestEqual(TEXT("the terrain bridge uses a 1/64 weight"), Enabled.TwilightFloor / 4.f * 64.f,
            FMath::RoundToFloat(Enabled.TwilightFloor / 4.f * 64.f));
        TestNearlyEqual(TEXT("the sky reader quantizes its independent envelope within half a step"),
            double(Enabled.TwilightSky / .05f), double(VoxelAtmosphere::TwilightSkyWeight(Dusk)), 1. / 128 + 1e-6);
        TestNearlyEqual(TEXT("the sky bridge uses a 1/64 weight"), double(Enabled.TwilightSky / .05f * 64.f),
            double(FMath::RoundToFloat(Enabled.TwilightSky / .05f * 64.f)), 1e-5);
        for (const int64 Ticks : {13500, 22500})
        {
            const FVoxelEnvironment Environment = TwilightEnvironmentAt(Ticks);
            const VoxelAtmosphere::FNight At = VoxelAtmosphere::ReadNight(Environment);
            TestTrue(FString::Printf(TEXT("the live reader keeps the late sky glow at %lld"), Ticks),
                At.TwilightSky / .05f > .85f);
            TestTrue(FString::Printf(TEXT("the live sky target cannot reuse the terrain envelope at %lld"), Ticks),
                At.TwilightSky / .05f > At.TwilightFloor / 4.f + .5f);
            TestNearlyEqual(TEXT("the late sky reader quantizes its own envelope"), double(At.TwilightSky / .05f),
                double(VoxelAtmosphere::TwilightSkyWeight(Environment)), 1. / 128 + 1e-6);
        }
        float PreviousLateSky = VoxelAtmosphere::ReadNight(TwilightEnvironmentAt(13500)).TwilightSky;
        for (const int64 Ticks : {14000, 15000, 16000, 18000})
        {
            const VoxelAtmosphere::FNight At = VoxelAtmosphere::ReadNight(TwilightEnvironmentAt(Ticks));
            TestTrue(FString::Printf(TEXT("the quantized sky reader eases towards midnight at %lld"), Ticks),
                At.TwilightSky < PreviousLateSky);
            TestEqual(TEXT("the later sky glow never prolongs terrain fill"), At.TwilightFloor, 0.f);
            PreviousLateSky = At.TwilightSky;
        }
        for (const int64 Ticks : {13155, 13156, 22844, 22845})
        {
            const VoxelAtmosphere::FNight At = VoxelAtmosphere::ReadNight(TwilightEnvironmentAt(Ticks));
            TestTrue(FString::Printf(TEXT("both bridges survive the exact handover at %lld"), Ticks),
                At.TwilightFloor > 3.9f && At.TwilightSky > .047f);
        }
        for (const int64 Ticks : {6000, 18000})
        {
            const VoxelAtmosphere::FNight At = VoxelAtmosphere::ReadNight(TwilightEnvironmentAt(Ticks));
            TestTrue(FString::Printf(TEXT("both bridges are absent at %lld"), Ticks),
                At.TwilightFloor == 0.f && At.TwilightSky == 0.f);
        }
        for (const int32 Cycle : {0, 1})
            for (const int32 Night : {0, 1})
                for (const int32 Twilight : {0, 1})
                {
                    Set(0, Cycle);
                    Set(1, Night);
                    Set(2, Twilight);
                    const VoxelAtmosphere::FNight Late = VoxelAtmosphere::ReadNight(TwilightEnvironmentAt(13500));
                    if (Cycle && Night && Twilight)
                        TestTrue(TEXT("all three switches enable the independent late sky glow"),
                            Late.TwilightSky / .05f > .85f);
                    else
                        TestTrue(TEXT("a disabled switch suppresses the late sky and terrain bridges"),
                            Late.TwilightSky == 0.f && Late.TwilightFloor == 0.f);
                    for (const int64 Ticks : {6000, 18000})
                    {
                        const VoxelAtmosphere::FNight At = VoxelAtmosphere::ReadNight(TwilightEnvironmentAt(Ticks));
                        TestTrue(TEXT("day and midnight receive no bridge under any switch combination"),
                            At.TwilightSky == 0.f && At.TwilightFloor == 0.f);
                    }
                }
        Set(2, 0);
        const VoxelAtmosphere::FNight Disabled = VoxelAtmosphere::ReadNight(Dusk);
        TestEqual(TEXT("switching the bridge off preserves the night blend"), Disabled.Night, Enabled.Night);
        TestEqual(
            TEXT("switching the bridge off preserves night exposure"), Disabled.ExposureStops, Enabled.ExposureStops);
        TestEqual(TEXT("switching the bridge off preserves the sky fade"), Disabled.SkyScale, Enabled.SkyScale);
        Set(2, 1);
        Set(3, 0);
        const VoxelAtmosphere::FNight SkyOnly = VoxelAtmosphere::ReadNight(Dusk);
        TestTrue(TEXT("the terrain dial leaves sky independently enabled"),
            SkyOnly.TwilightFloor == 0.f && SkyOnly.TwilightSky == Enabled.TwilightSky);
        Set(3, 4.f);
        Set(4, 0);
        const VoxelAtmosphere::FNight TerrainOnly = VoxelAtmosphere::ReadNight(Dusk);
        TestTrue(TEXT("the sky dial leaves terrain independently enabled"),
            TerrainOnly.TwilightSky == 0.f && TerrainOnly.TwilightFloor == Enabled.TwilightFloor);
        Set(3, -1);
        Set(4, -1);
        const VoxelAtmosphere::FNight Negative = VoxelAtmosphere::ReadNight(Dusk);
        TestTrue(TEXT("negative dials cannot darken the scene"),
            Negative.TwilightFloor == 0.f && Negative.TwilightSky == 0.f);
        Set(3, 8.f);
        Set(4, 1.f);
        const VoxelAtmosphere::FNight Max = VoxelAtmosphere::ReadNight(Dusk);
        Set(3, 100.f);
        Set(4, 100.f);
        const VoxelAtmosphere::FNight Capped = VoxelAtmosphere::ReadNight(Dusk);
        TestTrue(TEXT("terrain and sky dials keep their documented bounds"),
            Capped.TwilightFloor == Max.TwilightFloor && Capped.TwilightSky == Max.TwilightSky);
    }
    for (const FTwilightSavedCVar& Each : Saved)
    {
        TestEqual(TEXT("console value restored"), Each.Variable->GetString(), Each.Value);
        TestEqual(TEXT("console priority and flags restored"), uint32(Each.Variable->GetFlags()), uint32(Each.Flags));
    }
    return true;
}
#endif
