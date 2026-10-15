#include "Voxel/VoxelLook.h"
#include "Blocks/VoxelBlockTable.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "Foundation/FoundationProfile.h"
#include "GameFramework/PlayerController.h"
#include "UnnamedProjectSettings.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Foundation/FoundationGrid.h"
#include "Sky/VoxelEnvironment.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelTwilight.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelWorldSubsystem.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

namespace
{
    // Every default is the struct's, which is the map's rig (Unnamed.Voxel.Look checks the map against it).
    TAutoConsoleVariable<float> CVarLookDayEV(TEXT("voxel.Look.DayEV"), FVoxelLook().DayEV,
        TEXT("Look: daylight exposure offset in EV, + darker. Caves keep today's exposure."));
    TAutoConsoleVariable<float> CVarLookCap(TEXT("voxel.Look.Cap"), FVoxelLook().Cap,
        TEXT("Look: without SkyPin, the daylight pin in EV100 (at most the map's cap, 12.5); every view metered at it "
             "or above gets the "
             "daylight exposure, so a lower pin keeps more daylight views (forest, canopy) the same. Views metered 1.5 "
             "EV or more below it adapt as today."));
    TAutoConsoleVariable<float> CVarLookSky(TEXT("voxel.Look.Sky"), FVoxelLook().Sky,
        TEXT("Look: x the sky's and its aerial perspective's luminance (and so the haze and the sky light's capture), "
             "and the edge fog's own colour."));
    TAutoConsoleVariable<float> CVarLookSkyFill(
        TEXT("voxel.Look.SkyFill"), FVoxelLook().SkyFill, TEXT("Look: x the sky light's intensity (the map's 3)."));
    TAutoConsoleVariable<int32> CVarLookLocalExposure(TEXT("voxel.Look.LocalExposure"), FVoxelLook().LocalExposure,
        TEXT("Look: 1 the project's local exposure contrast (0.8); 0 contrast scales 1, no local exposure."));
    TAutoConsoleVariable<int32> CVarLookFilm(TEXT("voxel.Look.Film"), FVoxelLook().Film,
        TEXT("Look: 0 the engine's filmic curve; 1 near-straight colours (Slope 1.10, Toe 0.12, Shoulder 0.70, "
             "WhiteClip 0.04)."));
    TAutoConsoleVariable<float> CVarLookLowGlow(TEXT("voxel.Look.LowGlow"), FVoxelLook().LowGlow,
        TEXT("Look: x the glow of emitters of light level 1 to 7 (glow lichen 7); brighter emitters keep theirs."));
    TAutoConsoleVariable<float> CVarLookWaterSpecular(
        TEXT("voxel.Look.WaterSpecular"), FVoxelLook().WaterSpecular, TEXT("Look: the water's specular."));
    TAutoConsoleVariable<int32> CVarLookSkyPin(TEXT("voxel.Look.SkyPin"), FVoxelLook().SkyPin,
        TEXT("Look: 1 pins the daylight exposure while the camera has open sky (rays up through the blocks; leaves let "
             "them through), so a canopy keeps the daylight look and caves adapt as today; 0 the metering alone."));
    TAutoConsoleVariable<float> CVarLookSkyPinAdapt(TEXT("voxel.Look.SkyPinAdapt"), FVoxelLook().SkyPinAdapt,
        TEXT("Look: with SkyPin, how many f-stops brighter than daylight a darker view under open sky (a forest floor) "
             "may still adapt; 0 pins it fully."));
    TAutoConsoleVariable<float> CVarLookSpeedUp(TEXT("voxel.Look.SpeedUp"), FVoxelLook().SpeedUp,
        TEXT("Look: eye adaptation towards a brighter view (leaving a cave or a canopy), in f-stops per second."));
    TAutoConsoleVariable<float> CVarLookCaveFloor(TEXT("voxel.Look.CaveFloor"), FVoxelLook().CaveFloor,
        TEXT("Look: the ambient floor after tone mapping at light 0, x vanilla 26.3's at Brightness 50 (0.56 about "
             "Moody, 1 Default): a wall no light reaches shows as a dim copy of its texture; 0 off (black, as "
             "before)."));
}

FVoxelLook FVoxelLook::Read()
{
    FVoxelLook Look;
    Look.DayEV = CVarLookDayEV.GetValueOnGameThread();
    Look.Cap = CVarLookCap.GetValueOnGameThread();
    Look.Sky = CVarLookSky.GetValueOnGameThread();
    Look.SkyFill = CVarLookSkyFill.GetValueOnGameThread();
    Look.LocalExposure = CVarLookLocalExposure.GetValueOnGameThread();
    Look.Film = CVarLookFilm.GetValueOnGameThread();
    Look.LowGlow = CVarLookLowGlow.GetValueOnGameThread();
    Look.WaterSpecular = CVarLookWaterSpecular.GetValueOnGameThread();
    Look.SkyPin = CVarLookSkyPin.GetValueOnGameThread();
    Look.SpeedUp = CVarLookSpeedUp.GetValueOnGameThread();
    Look.SkyPinAdapt = CVarLookSkyPinAdapt.GetValueOnGameThread();
    Look.CaveFloor = CVarLookCaveFloor.GetValueOnGameThread();
    return Look;
}

VoxelLook::FExposure VoxelLook::SolveExposure(double DayEV, double Cap)
{
    // The map's own range and compensation; the curve does the rest. A pin above the map's cap would leave views
    // between them view-dependent, and one inside the ramp's reach of the floor would move the floor.
    FExposure Exposure;
    const double Pin = FMath::Clamp(Cap, MapMinEV + PinRampStops, MapMaxEV);
    if (DayEV == 0 && Pin >= MapMaxEV) return Exposure;
    // Pinned from the pin up: the compensation cancels the metering, Pin - MapMaxEV - DayEV at the pin, up to -DayEV at
    // the map's cap and flat beyond (the engine clamps the metering there). Today's below the ramp.
    Exposure.Curve.Add(FVector2D(Pin - PinRampStops, 0));
    Exposure.Curve.Add(FVector2D(Pin, Pin - MapMaxEV - DayEV));
    if (Pin < MapMaxEV) Exposure.Curve.Add(FVector2D(MapMaxEV, -DayEV));
    return Exposure;
}

double VoxelLook::ExposureAt(const FExposure& Exposure, double MeteredEV)
{
    double Compensation = 0;
    const TArray<FVector2D>& Keys = Exposure.Curve;
    if (!Keys.IsEmpty())
    {
        if (MeteredEV <= Keys[0].X)
            Compensation = Keys[0].Y;
        else if (MeteredEV >= Keys.Last().X)
            Compensation = Keys.Last().Y;
        else
            for (int32 Index = 1; Index < Keys.Num(); ++Index)
                if (MeteredEV <= Keys[Index].X)
                {
                    const double Alpha = (MeteredEV - Keys[Index - 1].X) / (Keys[Index].X - Keys[Index - 1].X);
                    Compensation = FMath::Lerp(Keys[Index - 1].Y, Keys[Index].Y, Alpha);
                    break;
                }
    }
    return FMath::Pow(2., Exposure.Bias + Compensation - FMath::Clamp(MeteredEV, Exposure.MinEV, Exposure.MaxEV));
}

VoxelLook::FExposure VoxelLook::SolveSkyPinned(double DayEV, double Pin, double AdaptStops)
{
    FExposure Exposure;
    Pin = FMath::Clamp(Pin, 0., 1.);
    Exposure.MinEV = FMath::Lerp(MapMinEV, MapMaxEV - FMath::Clamp(AdaptStops, 0., MapMaxEV - MapMinEV), Pin);
    Exposure.Bias = MapBias - DayEV * Pin;
    return Exposure;
}

bool VoxelLook::BlocksSky(const FVoxelStateInfo& Info)
{
    return Info.bCastsOcclusion && !Info.bLeaves;
}

double VoxelLook::SkyOpenness(const FVector& CameraBlocks, TFunctionRef<bool(const FIntVector&)> BlocksSkyAt)
{
    // Straight up, then eight at 45 degrees around it; each steps a quarter block, checking each new cell once.
    const double Side = UE_DOUBLE_INV_SQRT_2;
    int32 Open = 0, Rays = 0;
    for (int32 Ray = 0; Ray < 9; ++Ray)
    {
        const double Angle = (Ray - 1) * UE_DOUBLE_PI / 4;
        const FVector Direction =
            Ray == 0 ? FVector::UpVector : FVector(FMath::Cos(Angle) * Side, FMath::Sin(Angle) * Side, Side);
        ++Rays;
        bool bOpen = true;
        FIntVector Last(MAX_int32);
        for (double Along = 0; Along <= SkyRayBlocks && bOpen; Along += .25)
        {
            const FVector At = CameraBlocks + Direction * Along;
            const FIntVector Cell(FMath::FloorToInt32(At.X), FMath::FloorToInt32(At.Y), FMath::FloorToInt32(At.Z));
            if (Cell.Z > VoxelConstants::MaxZ) break;
            if (Cell == Last) continue;
            Last = Cell;
            bOpen = !BlocksSkyAt(Cell);
        }
        Open += bOpen;
    }
    return double(Open) / Rays;
}

double VoxelLook::SkyPinFor(double Openness)
{
    return FMath::SmoothStep(SkyPinFrom, SkyPinFull, Openness);
}

FLinearColor VoxelLook::CaveFloorDisplay(const FLinearColor& Color, const FLinearColor& BaseLinear,
    const FVector& Normal, double DepthCm, float Strength, double FadeStartCm, double FadeEndCm)
{
    const FVector N = Normal.GetSafeNormal();
    const FVector A = N.GetAbs();
    const double Face = A.Z * (N.Z > 0 ? FloorTop : FloorBottom) + A.Y * FloorNS + A.X * FloorEW;
    const double Fade = 1 - FMath::Clamp((DepthCm - FadeStartCm) / FMath::Max(FadeEndCm - FadeStartCm, 1.), 0., 1.);
    auto Channel = [&](float Scene, float Base)
    {
        const double B = FMath::Clamp(double(Base), 0., 1.);
        const double Srgb = B < .0031308 ? B * 12.92 : 1.055 * FMath::Pow(B, 1 / 2.4) - .055;
        return float(FMath::Max(double(Scene), Strength * Face * Fade * Srgb));
    };
    return FLinearColor(
        Channel(Color.R, BaseLinear.R), Channel(Color.G, BaseLinear.G), Channel(Color.B, BaseLinear.B), Color.A);
}

VoxelLook::FTargets VoxelLook::Solve(const FVoxelLook& Look, double Pin)
{
    FTargets Targets;
    Targets.Exposure =
        Look.SkyPin == 1 ? SolveSkyPinned(Look.DayEV, Pin, Look.SkyPinAdapt) : SolveExposure(Look.DayEV, Look.Cap);
    Targets.SpeedUp = Look.SpeedUp;
    if (Look.LocalExposure == 0) Targets.LocalContrast = 1.f;
    Targets.bFilm = Look.Film == 1;
    Targets.SkyLightIntensity = SkyLightIntensity * Look.SkyFill;
    Targets.SkyLuminance = Look.Sky;
    Targets.FogColorScale = Look.Sky;
    Targets.LowGlow = Look.LowGlow;
    Targets.WaterSpecular = Look.WaterSpecular;
    Targets.CaveFloor = FMath::Max(0.f, Look.CaveFloor);
    return Targets;
}

void VoxelLook::ApplyToPostProcess(const FTargets& Targets, FPostProcessSettings& Settings, UCurveFloat* Curve)
{
    Settings.bOverride_AutoExposureBias = Settings.bOverride_AutoExposureMinBrightness =
        Settings.bOverride_AutoExposureMaxBrightness = Settings.bOverride_LocalExposureMiddleGreyBias = true;
    Settings.AutoExposureBias = float(Targets.Exposure.Bias);
    Settings.AutoExposureMinBrightness = float(Targets.Exposure.MinEV);
    Settings.AutoExposureMaxBrightness = float(Targets.Exposure.MaxEV);
    Settings.LocalExposureMiddleGreyBias = float(Targets.Exposure.LocalGreyBias);
    // The map sets it too (CreateGeneratedMap.py), at the default.
    Settings.bOverride_AutoExposureSpeedUp = true;
    Settings.AutoExposureSpeedUp = Targets.SpeedUp;
    const bool bCurve = Curve && !Targets.Exposure.Curve.IsEmpty();
    if (bCurve)
    {
        FRichCurve& Keys = Curve->FloatCurve;
        Keys.Reset();
        for (const FVector2D& Key : Targets.Exposure.Curve)
            Keys.SetKeyInterpMode(Keys.AddKey(float(Key.X), float(Key.Y)), RCIM_Linear);
        Keys.PreInfinityExtrap = Keys.PostInfinityExtrap = RCCE_Constant;
    }
    Settings.bOverride_AutoExposureBiasCurve = bCurve;
    Settings.AutoExposureBiasCurve = bCurve ? Curve : nullptr;
    // Unset, the project's own (the map overrides neither).
    Settings.bOverride_LocalExposureHighlightContrastScale = Settings.bOverride_LocalExposureShadowContrastScale =
        Targets.LocalContrast.IsSet();
    if (Targets.LocalContrast)
        Settings.LocalExposureHighlightContrastScale = Settings.LocalExposureShadowContrastScale =
            *Targets.LocalContrast;
    Settings.bOverride_FilmSlope = Settings.bOverride_FilmToe = Settings.bOverride_FilmShoulder =
        Settings.bOverride_FilmWhiteClip = Targets.bFilm;
    if (Targets.bFilm)
    {
        Settings.FilmSlope = FilmSlope;
        Settings.FilmToe = FilmToe;
        Settings.FilmShoulder = FilmShoulder;
        Settings.FilmWhiteClip = FilmWhiteClip;
    }
}

double AVoxelWorld::UpdateSkyPin()
{
    const double Now = GetWorld()->GetTimeSeconds();
    const double Delta = FMath::Max(0., Now - SkyPinSeconds);
    SkyPinSeconds = Now;
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    if (Now >= SkyProbeDue && Table && Controller && Controller->PlayerCameraManager)
    {
        // The game thread's own reads of final columns (GetState); a column not loaded blocks the rays, so a camera
        // whose surroundings aren't streamed yet reads as a cave (today's exposure) rather than as open sky.
        SkyProbeDue = Now + VoxelLook::SkyProbeSeconds;
        const FVector Camera =
            (Controller->PlayerCameraManager->GetCameraLocation() - OriginCm) / UFoundationProfile::BlockCm;
        const double Start = FPlatformTime::Seconds();
        const UVoxelWorldSubsystem* Streaming = GetStreaming();
        const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
        const double Openness = VoxelLook::SkyOpenness(Camera,
            [this, Pipeline](const FIntVector& Cell)
            {
                const FIntPoint Column(FoundationGrid::FloorDivide(Cell.X, VoxelConstants::ColumnSize),
                    FoundationGrid::FloorDivide(Cell.Y, VoxelConstants::ColumnSize));
                return !Pipeline || !Pipeline->FindColumn(Column) || VoxelLook::BlocksSky(Table->Get(GetState(Cell)));
            });
        SkyProbeMicros += (FPlatformTime::Seconds() - Start) * 1e6;
        ++SkyProbes;
        SkyPinTarget = VoxelLook::SkyPinFor(Openness);
        // The first probe sets the pin outright; later ones ease it.
        if (SkyProbes == 1) SkyPinNow = SkyPinTarget;
        if (SkyProbes % 50 == 1)
            UE_LOG(LogUnnamedVoxel, Log, TEXT("Look: sky pin %.2f (open sky %.2f at %s); probe %.1f us (mean of %d)"),
                SkyPinTarget, Openness, *Camera.ToCompactString(), SkyProbeMicros / SkyProbes, SkyProbes);
    }
    SkyPinNow = FMath::FInterpConstantTo(SkyPinNow, SkyPinTarget, Delta, VoxelLook::SkyPinPerSecond);
    return SkyPinNow;
}

void AVoxelWorld::UpdateCaveFloor(float Strength, float TwilightStrength)
{
    const bool bDraw = VoxelLook::CaveFloorDrawn(Strength) || TwilightStrength > 0.f;
    if (!bDraw && !CaveFloorPost && !TwilightFloorPost) return;
    if (!CaveFloorPost)
    {
        UMaterialInterface* Base = Cast<UMaterialInterface>(
            UUnnamedProjectSettings::GetBlockAssetPath(VoxelLook::CaveFloorMaterialName).TryLoad());
        if (!Base)
        {
            UE_LOG(LogUnnamedVoxel, Warning, TEXT("Look: %s is missing (run CreateLookMaterials.py); no cave floor"),
                VoxelLook::CaveFloorMaterialName);
            CaveFloorPost = NewObject<UPostProcessComponent>(this, TEXT("CaveFloorPost"));
            CaveFloorPost->bEnabled = false;
            return;
        }
        CaveFloorPost = NewObject<UPostProcessComponent>(this, TEXT("CaveFloorPost"));
        CaveFloorPost->bUnbound = true;
        CaveFloorPost->SetupAttachment(GetRootComponent());
        CaveFloorPost->RegisterComponent();
        CaveFloorMaterial = UMaterialInstanceDynamic::Create(Base, CaveFloorPost);
        CaveFloorPost->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, CaveFloorMaterial));
    }
    CaveFloorPost->bEnabled = VoxelLook::CaveFloorDrawn(Strength) && CaveFloorMaterial;
    if (TwilightStrength > 0.f && !TwilightFloorPost && !bTriedTwilightFloor)
    {
        bTriedTwilightFloor = true;
        TwilightFloorPost = VoxelTwilight::CreateFloor(this);
        if (TwilightFloorPost)
        {
            TwilightFloorPost->SetupAttachment(GetRootComponent());
            TwilightFloorPost->RegisterComponent();
        }
    }
    if (!CaveFloorPost->bEnabled && !TwilightFloorPost) return;
    // The edge fog's wall: clear to 1.5 columns before the drawn radius, opaque at it (UpdateEdgeFog). The haze keeps
    // the same drawn edge, where the far meshes fade into the sky.
    double FadeStart = 1e6, FadeEnd = 1.1e6;
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    if (Streaming && Streaming->GetPipeline())
    {
        constexpr double ColumnCm = VoxelConstants::ColumnSize * UFoundationProfile::BlockCm;
        FadeEnd = Streaming->GetPipeline()->GetRings().GetDrawnRadius() * ColumnCm;
        FadeStart = FMath::Max(0., FadeEnd - 1.5 * ColumnCm);
    }
    // With the eye in water and the water fog on (its own session's voxel.Look.WaterFog), the floor lies under that
    // fog: it fades over the fog's range, as vanilla fogs its lightmapped walls.
    static const IConsoleVariable* WaterFog = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.Look.WaterFog"));
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    if (WaterFog && WaterFog->GetInt() == 1 && Controller && Controller->PlayerCameraManager &&
        IsInWater(Controller->PlayerCameraManager->GetCameraLocation()))
    {
        FadeStart = VoxelLook::UnderwaterFadeStartBlocks * UFoundationProfile::BlockCm;
        FadeEnd = VoxelLook::UnderwaterFadeEndBlocks * UFoundationProfile::BlockCm;
    }
    const TPair<const TCHAR*, float> Values[] = {{VoxelLook::CaveFloorStrengthParameter, Strength},
        {VoxelLook::CaveFloorFadeStartParameter, float(FadeStart)},
        {VoxelLook::CaveFloorFadeEndParameter, float(FadeEnd)}};
    if (CaveFloorPost->bEnabled)
        for (const TPair<const TCHAR*, float>& Value : Values)
        {
            float Now = 0;
            if (!CaveFloorMaterial->GetScalarParameterValue(FName(Value.Key), Now) || Now != Value.Value)
                CaveFloorMaterial->SetScalarParameterValue(Value.Key, Value.Value);
        }
    if (TwilightFloorPost)
        VoxelTwilight::UpdateFloor(*TwilightFloorPost, TwilightStrength, float(FadeStart), float(FadeEnd));
}

void AVoxelWorld::ApplyLook()
{
    const FVoxelLook Look = FVoxelLook::Read();
    const double Pin = Look.SkyPin == 1 ? UpdateSkyPin() : 0;
    // The atmosphere's night (VoxelAtmosphere.h) composes onto the look: zero by day and with the cycle off. With
    // SkyPin 0 the pin is 0, so only the sky darkens, not the exposure.
    const VoxelAtmosphere::FNight Night =
        VoxelAtmosphere::IsCycleOn() ? VoxelAtmosphere::ReadNight(GetEnvironment()) : VoxelAtmosphere::FNight();
    VoxelLook::FTargets Targets = VoxelLook::Solve(Look, Pin);
    VoxelAtmosphere::ComposeNight(Targets, Night, Pin);
    // The floor still refreshes its distance/underwater fade every frame, even when the look targets are unchanged.
    UpdateCaveFloor(Targets.CaveFloor, Targets.TwilightFloor);
    const bool bLookChanged = !AppliedLook || !(*AppliedLook == Look) || !(AppliedNight == Night);
    // A pin step under 0.005 waits, but the pin's end (its target) is always applied, so caves land on today's exactly.
    if (!bLookChanged && (Pin == AppliedPin || (FMath::Abs(Pin - AppliedPin) < .005 && Pin != SkyPinTarget))) return;
    const bool bDayLookChanged = !AppliedLook || !(*AppliedLook == Look);
    AppliedLook = Look;
    AppliedPin = Pin;
    AppliedNight = Night;
    // A pin step moves the exposure only.
    if (!bLookChanged)
    {
        if (LookVolume) VoxelLook::ApplyToPostProcess(Targets, LookVolume->Settings, LookCurve);
        return;
    }
    if (LookVolume)
    {
        if (!LookCurve) LookCurve = NewObject<UCurveFloat>(this, TEXT("LookExposureCurve"));
        VoxelLook::ApplyToPostProcess(Targets, LookVolume->Settings, LookCurve);
    }
    if (SkyFill) SkyFill->SetIntensity(Targets.SkyLightIntensity);
    if (Sky)
        Sky->SetSkyAndAerialPerspectiveLuminanceFactor(
            FLinearColor(Targets.SkyLuminance, Targets.SkyLuminance, Targets.SkyLuminance));
    // UpdateEdgeFog applies it with the rest of the fog.
    EdgeFogColorScale = Targets.FogColorScale;
    EdgeHazeTint = Night.HazeTint;
    if (UMaterialParameterCollectionInstance* Collection =
            LookCollection ? GetWorld()->GetParameterCollectionInstance(LookCollection) : nullptr)
    {
        Collection->SetScalarParameterValue(VoxelLook::LowGlowParameter, Targets.LowGlow);
        Collection->SetScalarParameterValue(VoxelLook::WaterSpecularParameter, Targets.WaterSpecular);
    }
    if (!bDayLookChanged) return;
    UE_LOG(LogUnnamedVoxel, Log,
        TEXT("Look: DayEV %.3f, cap %.3f, sky pin %d, speed up %.2f, sky %.3f, sky fill %.3f, local exposure %d, film "
             "%d, low glow %.3f, water "
             "specular %.3f, cave floor %.2f; exposure bias %.3f, EV100 %.3f to %.3f, local grey bias %.3f, curve %s "
             "(volume %d, sky "
             "light %d, atmosphere %d, collection %d)"),
        Look.DayEV, Look.Cap, Look.SkyPin, Look.SpeedUp, Look.Sky, Look.SkyFill, Look.LocalExposure, Look.Film,
        Look.LowGlow, Look.WaterSpecular, Look.CaveFloor, Targets.Exposure.Bias, Targets.Exposure.MinEV,
        Targets.Exposure.MaxEV, Targets.Exposure.LocalGreyBias,
        Targets.Exposure.Curve.IsEmpty() ? TEXT("none")
                                         : *FString::JoinBy(Targets.Exposure.Curve, TEXT(" "), [](const FVector2D& Key)
                                               { return FString::Printf(TEXT("(%.2f, %.3f)"), Key.X, Key.Y); }),
        int32(LookVolume != nullptr), int32(SkyFill != nullptr), int32(Sky != nullptr),
        int32(LookCollection != nullptr));
}
