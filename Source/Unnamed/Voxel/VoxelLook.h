#pragma once

#include "CoreMinimal.h"

class UCurveFloat;
struct FPostProcessSettings;
struct FVoxelStateInfo;

// The look dials (the brightness pass, Docs/VoxelWorld.md "Lighting"): voxel.Look.* variables that AVoxelWorld applies
// to the map's daylight rig at BeginPlay and whenever one changes (AVoxelWorld::ApplyLook). Each dial's default gives
// the map's own values (Jesus's pick, 2026-10-12: B 0.6 with SkyPin and 2 stops of outdoor adaptation, the sky kept as
// bright as before); each dial is one switch away from the look before the pick (DayEV 0, Sky 1, SkyPin 0).
struct UNNAMED_API FVoxelLook
{
    // Daylight exposure offset in EV, + darker. Caves keep today's exposure (SolveExposure).
    float DayEV = .6f;
    // The daylight pin, in EV100: every view metered at it or above gets exactly the daylight exposure, as views at the
    // map's cap (12.5) do; views metered 1.5 EV or more below it adapt as today.
    float Cap = 12.5f;
    // 1: the camera's open sky pins the daylight exposure whatever is on screen (a forest canopy keeps it, caves adapt
    // as today; VoxelLook::SkyOpenness); 0: the metering alone decides (Cap), as today.
    int32 SkyPin = 1;
    // With SkyPin, how many f-stops brighter than daylight a darker view under open sky may still adapt (a forest
    // floor); 0 pins it fully.
    float SkyPinAdapt = 2.f;
    // Eye adaptation towards a brighter view (leaving a cave or a canopy), in f-stops per second; the map's 2.
    float SpeedUp = 2.f;
    // x the sky's and its aerial perspective's luminance, and the edge fog's own colour: 2^DayEV x 1.08 keeps the sky
    // as bright as before the pick (the 20 degree box no darker).
    float Sky = 1.637f;
    // x the sky light's intensity.
    float SkyFill = 1.f;
    // 1 keeps the project's local exposure contrast (Config/DefaultEngine.ini, 0.8); 0 sets both contrast scales to 1.
    int32 LocalExposure = 1;
    // 0 keeps the engine's filmic curve; 1 near-straight (Slope 1.10, Toe 0.12, Shoulder 0.70, WhiteClip 0.04).
    int32 Film = 0;
    // x the glow of emitters of light level 1 to 7 (glow lichen 7), through MPC_VoxelLook.
    float LowGlow = 1.f;
    // The water's specular, through MPC_VoxelLook.
    float WaterSpecular = .5f;

    // The voxel.Look.* variables as set now.
    static FVoxelLook Read();
    bool operator==(const FVoxelLook& Other) const = default;
};

namespace VoxelLook
{
    // Today's daylight rig (Scripts/Editor/CreateGeneratedMap.py), which the dials start from: eye adaptation between
    // EV100 6 and 12.5 at a compensation of -1 (the daylight exposure 2^-13.5, the cave floor's 2^-7), local exposure's
    // middle grey raised by 2, the sky light 3.
    constexpr double MapBias = -1;
    constexpr double MapMinEV = 6;
    constexpr double MapMaxEV = 12.5;
    constexpr double MapLocalGreyBias = 2;
    constexpr float SkyLightIntensity = 3.f;
    // Below the pin, the compensation eases back to today's over this many EV of metering.
    constexpr double PinRampStops = 1.5;
    // SkyPin: rays of this many blocks, every SkyProbeSeconds; the open-sky fraction from which the pin starts and at
    // which it is full; how fast the pin follows, per second.
    constexpr double SkyRayBlocks = 48;
    constexpr double SkyProbeSeconds = .2;
    constexpr double SkyPinFrom = .15;
    constexpr double SkyPinFull = .45;
    constexpr double SkyPinPerSecond = 4;
    // voxel.Look.Film 1's curve (scout T's fit to straight colours over block values).
    constexpr float FilmSlope = 1.10f;
    constexpr float FilmToe = .12f;
    constexpr float FilmShoulder = .70f;
    constexpr float FilmWhiteClip = .04f;
    // The material parameter collection the chunk and block materials read (CreateBlockMaterials.py), and its scalars.
    inline const TCHAR* CollectionName = TEXT("MPC_VoxelLook");
    inline const TCHAR* LowGlowParameter = TEXT("LowGlow");
    inline const TCHAR* WaterSpecularParameter = TEXT("WaterSpecular");

    // The eye adaptation for a daylight offset and a pin. The exposure at a metered EV100 E is 2^(Bias + Curve(E) - E)
    // with E clamped to [MinEV, MaxEV] for the last term only (ExposureAt): the map's range and compensation, plus an
    // exposure compensation curve over the metered EV100 (linear between its keys, flat beyond them; empty: none).
    // Views metered at the pin or above get 2^-(13.5 + DayEV); views metered PinRampStops below it or darker keep
    // today's exposure, so caves, the cave floor (2^-7) and the torches' frozen exposure don't move.
    struct FExposure
    {
        double Bias = MapBias;
        double MinEV = MapMinEV;
        double MaxEV = MapMaxEV;
        double LocalGreyBias = MapLocalGreyBias;
        // (metered EV100, compensation in EV) keys.
        TArray<FVector2D> Curve;
    };
    UNNAMED_API FExposure SolveExposure(double DayEV, double Cap);
    // The exposure an FExposure gives a view metered at MeteredEV.
    UNNAMED_API double ExposureAt(const FExposure& Exposure, double MeteredEV);
    // SkyPin's eye adaptation for a pin from 0 (no open sky: today's, caves) to 1 (open sky: every view at
    // 2^-(13.5 + DayEV), or up to AdaptStops brighter for a darker view): the floor rises to AdaptStops under the map's
    // cap and the compensation drops by DayEV.
    UNNAMED_API FExposure SolveSkyPinned(double DayEV, double Pin, double AdaptStops = 0);

    // A cell the sky's light doesn't pass: a full opaque cube. Leaves, plants, glass, fluids and air let it through.
    UNNAMED_API bool BlocksSky(const FVoxelStateInfo& Info);
    // The camera's open sky: the fraction of nine rays (straight up, and eight at 45 degrees around it) that go
    // SkyRayBlocks blocks from the camera (in blocks) without meeting a cell that BlocksSky says blocks the sky.
    UNNAMED_API double SkyOpenness(const FVector& CameraBlocks, TFunctionRef<bool(const FIntVector&)> BlocksSkyAt);
    // The pin for an open-sky fraction: 0 to SkyPinFrom, 1 from SkyPinFull, smooth between.
    UNNAMED_API double SkyPinFor(double Openness);

    // Everything ApplyLook writes, for a look.
    struct FTargets
    {
        FExposure Exposure;
        // Local exposure contrast scales to override, or unset (the ini's).
        TOptional<float> LocalContrast;
        bool bFilm = false;
        float SkyLightIntensity = 0;
        // SkyAtmosphere's SkyAndAerialPerspectiveLuminanceFactor, grey.
        float SkyLuminance = 1;
        // x the edge fog's own in-scattering colour (step 1's wall; the haze takes the sky's light, which the factor
        // above already scales).
        float FogColorScale = 1;
        float LowGlow = 1;
        float WaterSpecular = .5f;
        float SpeedUp = 2;
    };
    // Pin is SkyPin's, used when Look.SkyPin is 1.
    UNNAMED_API FTargets Solve(const FVoxelLook& Look, double Pin = 0);
    // Writes a look's exposure, local exposure and film into post-process settings (the map's unbound volume); Curve
    // takes the exposure curve's keys when it has any.
    UNNAMED_API void ApplyToPostProcess(const FTargets& Targets, FPostProcessSettings& Settings, UCurveFloat* Curve);
}
