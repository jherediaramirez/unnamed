#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Voxel/VoxelLightProbe.h"

class ACameraActor;
class AVoxelWorld;
class FLightShotsExposure;
class IConsoleVariable;

// The torch light probe's captures (sessions T0 and T1: -VoxelShotLights with -VoxelShot=<folder>,
// Scripts/CaptureVoxel.ps1 -Lights; results in Docs/Records/2026-10-10-Torch-Light-Probe.md). It runs once the first
// view is on screen, before the -VoxelShot shots, in the probe cave (FindProbeView: the largest dark cave near the
// centre, found from the `cave` view of AVoxelWorld::GetCaveShotViews), except the leak test:
// - Glowing cubes (T0): glowstone, sea lantern, shroomlight, ochre froglight and jack o'lantern in turn, placed on the
//   cave floor through the authority's block world. The mean sRGB of wall patches 2, 5 and 10 blocks from the cube,
//   every frame for 150 frames once the cube is on screen (screenshots at 0, 30 and 120 frames), then an away view
//   with the cube off screen and its lit wall still in view. GPU time before and after.
// - Local lights (T1, VoxelLightProbe's pool): GPU time at 0, 1, 10 and 100 lights for megalights, vsm and none, in
//   three rounds of alternating order, with one ProfileGPU dump per case in the log. Then per-pixel noise over 30
//   frames, the leak test (one light 1 m over a cave roofed by three blocks of rock, measured on the cave floor
//   below) and the edit test (mining the wall cell next to a light, then frames until the niche's back is lit).
// The view's eye adaptation settles once on the cave, then is frozen, so the numbers measure light rather than the
// eye adapting. MegaLights is switched per case by console (r.MegaLights.EnableForProject) and put back at the end,
// as is every edited cell. Writes lights.txt (the tables), lights.csv (every patch value) and lights-*.png.
class FVoxelLightShots
{
public:
    explicit FVoxelLightShots(const FString& InFolder);
    ~FVoxelLightShots();
    // Runs one frame; false once the probe has finished, restored what it changed and written its files.
    bool Tick(AVoxelWorld& World);

private:
    struct FStep
    {
        FString Name;
        TFunction<void()> Begin;
        // Frames after Begin. With bEdit they start once the edited columns are back on screen.
        int32 Frames = 0;
        bool bEdit = false;
        // Sums the GPU, game-thread and frame times of every frame.
        bool bTimed = false;
        // Frames from this one on are read back and handed to OnFrame.
        int32 CaptureFrom = TNumericLimits<int32>::Max();
        TFunction<void(int32 Frame, const TArray<FColor>& Pixels)> OnFrame;
        // Every frame from the one after Begin on, an edit's wait included.
        TFunction<void()> OnTick;
        TFunction<void()> End;
    };
    // One torch edit's pool updates (-VoxelShotTorchGates): their game-thread ms summed, how many, and the light
    // components the pool made for them.
    struct FEditCost
    {
        double Ms = 0;
        int32 Updates = 0;
        int32 Created = 0;
    };
    struct FTimes
    {
        double GpuMs = 0, GameMs = 0, FrameMs = 0;
        int32 Frames = 0;
    };
    // A block face to measure: a point just off its centre (blocks) and its normal.
    struct FPatch
    {
        FString Name;
        FVector Point = FVector::ZeroVector;
        FVector Normal = FVector::ZeroVector;
        bool IsSet() const { return !Normal.IsZero(); }
    };
    // Per-frame values of one patch: mean sRGB per frame (0-255; R, G, B and their mean).
    using FSeries = TArray<FVector4>;
    struct FCase
    {
        FString Name;
        VoxelLightProbe::EMode Mode = VoxelLightProbe::EMode::None;
        int32 Count = 0;
        bool bMegaLights = false;
        // Torches through the block world, lit by the world's light pool, instead of probe lights; bSpread takes
        // them from the spread floor cells.
        bool bTorches = false;
        bool bSpread = false;
    };

    FStep& AddStep(const FString& Name, int32 Frames, TFunction<void()> Begin = nullptr);
    void Build(AVoxelWorld& World);
    void AddCubeSteps(AVoxelWorld& World);
    void FindProbeView();
    void AddProbeSteps(AVoxelWorld& World);
    // With bTorch, the leak site's cases are leak-floor and leak-torch-1 (a torch on the column's top, lit by the
    // pool).
    void AddLeakSteps(AVoxelWorld& World, bool bTorch = false);
    void AddEditSteps(AVoxelWorld& World);
    // -VoxelShotTorchGates: the look pairs, the edit cost, the timed cases and the torch leak test.
    void AddTorchGateSteps(AVoxelWorld& World);
    void AddTorchLookSteps(AVoxelWorld& World);
    void AddTorchEditSteps(AVoxelWorld& World);
    // -VoxelShotTorchRoom: finds the room's place under the spawn and adds the room's steps; AddTorchRoomTables writes
    // its patch table, RestoreRoomVariables puts back the pool's intensity and temperature.
    void AddTorchRoomSteps(AVoxelWorld& World);
    void AddTorchRoomTables();
    void RestoreRoomVariables();
    // The room's last step: the torches, the variables, MegaLights and every written cell back.
    void AddTorchRoomCleanup(AVoxelWorld& World);
    // -VoxelShotTorchLook: the look pairs in the built room (Eye, Rotation; OneTorch stands when they start) and the
    // probe cave.
    void AddTorchLookPairSteps(AVoxelWorld& World, const FVector& Eye, const FRotator& Rotation,
        const TArray<FIntVector>& OneTorch, const TArray<FIntVector>& FourTorches);
    // Count covered cave floor cells within Radius blocks of the view's eye, spread out (farthest first).
    TArray<FIntVector> FindSpreadTorchCells(int32 Count, double Radius) const;
    // A case's lights: torches on its floor cells (the probe's ones are taken back), or the probe's lights at
    // Positions (the torches are taken back).
    void ShowTorchCase(AVoxelWorld& World, const FCase& Case, const TArray<FVector>& Positions);
    // The probe lights shown and the pool's picks while it is live.
    int32 CountAllShown(const AVoxelWorld& World) const;
    TArray<FVector> GetCasePositions(const FCase& Case) const;
    void SetExposureSpeed(float Speed);
    void AddTorchTables(AVoxelWorld& World);
    void OnScreenshot(int32 InWidth, int32 InHeight, const TArray<FColor>& Pixels);
    void SetView(const FVector& Blocks, const FRotator& Rotation);
    void SetMegaLights(bool bOn);
    void ShowCase(AVoxelWorld& World, const FCase& Case, const TArray<FVector>& Positions);
    // Writes a cell through the authority's block world, keeping its first state for the clean-up.
    void SetCell(AVoxelWorld& World, const FIntVector& Cell, FFoundationBlockState State);
    // A point (blocks) in the screenshot of a view from From with this rotation, and its depth; false behind it.
    bool Project(const FVector& From, const FRotator& Rotation, const FVector& Blocks, FVector2D& OutPixel,
        double& OutDepth) const;
    bool IsOnScreen(const FVector2D& Pixel, double Margin) const;
    // Of these lights (cm), those whose reach meets the view's frustum (the ones the renderer draws).
    int32 CountInView(TConstArrayView<FVector> Positions) const;
    // The patch's mean sRGB in the current view; W < 0 when it is not wholly on screen.
    FVector4 Measure(const FPatch& Patch, const TArray<FColor>& Pixels) const;
    void Record(const FString& Step, int32 Frame, const FPatch& Patch, const FVector4& Value);
    void SavePng(const FString& Name, const TArray<FColor>& Pixels) const;
    float GetExposure() const;
    void NoteExposure(const FString& Step);
    void Line(const FString& Text);
    void Finish(AVoxelWorld& World);

    static constexpr int32 MaxLights = 100;
    // Frames a timed step waits before summing: the GPU and game-thread times read back are a frame or more old.
    static constexpr int32 TimedSkip = 3;
    FString Folder;
    // -VoxelLightsIntensity=<cd> overrides the probe lights' intensity.
    double IntensityCd = VoxelLightProbe::FSettings().IntensityCd;
    // -VoxelShotTorches=<N> (session T3): instead of the probe, `voxel.LightProbe N torches` in the probe cave, lit by
    // the world's light pool, one screenshot (lights-torches.png) and voxel.LightsInfo; MegaLights stays as the run
    // set it.
    int32 TorchCount = 0;
    // -VoxelShotTorchGates (session T3): the torch light pool's gate cases instead of T0/T1's, written to torches.txt
    // (AddTorchGateSteps; Scripts/CaptureVoxel.ps1 -TorchGates).
    bool bTorchGates = false;
    // -VoxelShotTorchRoom (session T3, probe P4): the look reference for the torch light. A 9 x 4 x 9 air room in an
    // 11 x 6 x 11 stone shell, built through the authority's block world 31 blocks under the lowest surface over it
    // near the spawn, seen from a fixed corner at eye height: one torch in the middle of the floor, then four on its
    // quarter points, with MegaLights off and on at several voxel.LightIntensity and voxel.LightTemperature values,
    // then a glowstone in the middle of the ceiling instead; last, one and four torches with voxel.LightFalloff 2 and
    // 8, each at the voxel.LightBrightness a short search finds to match the 1000 cd floor-1 patch. The exposure
    // settles on the one-torch case at 1000 cd and stays frozen for every shot. Writes room.txt (cells, camera, patch
    // table, voxel.LightsInfo), room.csv and room-*.png; every cell and variable it changes is put back
    // (Scripts/CaptureVoxel.ps1 -TorchRoom).
    bool bTorchRoom = false;
    // -VoxelShotTorchLook (session T3, the warm look; Scripts/CaptureVoxel.ps1 -TorchLook): pairs only, at the run's
    // own settings (no sweep), each with MegaLights 0 and 1, in the torch room under the room's frozen exposure: the
    // room with 1 torch, a close crop of that torch (the camera 1.2 blocks from it at the flame's height), the room
    // with 4 torches, and the probe cave with 10. Writes look.txt (per shot: mean luma, saturated and black shares, the
    // settings, voxel.LightsInfo) and look-<view>-ml<0|1>.png; implies -VoxelShotTorchRoom's room, without its shots.
    bool bTorchLookPairs = false;
    TArray<FString> LookRows;
    // The torches standing for the look pairs.
    TArray<FIntVector> LookTorches;
    // The floor and wall patches the room measures, and one formatted table row per shot.
    TArray<FPatch> RoomPatches;
    TArray<FString> RoomRows;
    // The torches standing in the room (0, 1 or 4; -1 before the first shot).
    int32 RoomTorches = -1;
    IConsoleVariable* RoomIntensityVariable = nullptr;
    IConsoleVariable* RoomTemperatureVariable = nullptr;
    float RoomIntensityAtStart = 0;
    float RoomTemperatureAtStart = 0;
    // voxel.LightFalloff and voxel.LightBrightness: the falloff shots set them; put back with the others.
    IConsoleVariable* RoomFalloffVariable = nullptr;
    IConsoleVariable* RoomBrightnessVariable = nullptr;
    float RoomFalloffAtStart = 0;
    float RoomBrightnessAtStart = 0;
    // The brightness searches: per falloff exponent the voxel.LightBrightness to try next (after the last search step,
    // the one its shots use) and the last try as (log brightness, log linear floor-1); the target is the reference
    // shot's floor-1 mean sRGB (-1 until measured).
    TMap<float, float> RoomBrightness;
    TMap<float, FVector2D> RoomSearchLast;
    double RoomTargetFloor1 = -1;
    // The probe cave's floor cells, as FindFloorLightCells orders them (torches-N takes the first N), and the spread
    // ones (torches-64-spread).
    TArray<FIntVector> TorchCells;
    TArray<FIntVector> SpreadCells;
    // The cells torches stood on, checked at the end.
    TSet<FIntVector> TorchSites;
    TArray<FEditCost> PlaceCosts;
    TArray<FEditCost> RemoveCosts;
    FEditCost EditCost;
    int64 EditUpdates = 0;
    int32 EditComponents = 0;
    FString LeakGate;
    TUniquePtr<VoxelLightProbe::FCells> Cells;
    TArray<FStep> Steps;
    int32 StepIndex = 0;
    int32 StepFrame = 0;
    bool bBuilt = false;
    bool bBegun = false;
    bool bFinished = false;
    uint64 BeginFrame = 0;
    // Frames from an edit until its columns were meshed and on screen again.
    int32 EditFrames = 0;
    FTimes Times;
    // The step and frame the next read-back belongs to.
    int32 CaptureStep = INDEX_NONE;
    int32 CaptureFrame = 0;
    FDelegateHandle ScreenshotHandle;
    int32 Width = 0;
    int32 Height = 0;
    TWeakObjectPtr<ACameraActor> Camera;
    FVector CameraBlocks = FVector::ZeroVector;
    FRotator CameraRotation = FRotator::ZeroRotator;
    TSharedPtr<FLightShotsExposure, ESPMode::ThreadSafe> Exposure;
    // The exposure at the end of each measured step, once frozen.
    TArray<TPair<FString, float>> Exposures;
    IConsoleVariable* MegaLightsVariable = nullptr;
    int32 MegaLightsAtStart = 0;
    // Cells the probe changed, with their states before.
    TMap<FIntVector, FFoundationBlockState> Originals;

    // The view every case but the leak test uses, in blocks: the cave view's camera until FindProbeView picks the
    // probe cave's.
    FVector ViewBlocks = FVector::ZeroVector;
    FRotator ViewRotation = FRotator::ZeroRotator;
    bool bCube = false;
    FIntVector CubeCell = FIntVector::ZeroValue;
    TArray<FPatch> CubePatches;
    bool bAway = false;
    FRotator AwayRotation = FRotator::ZeroRotator;
    // The probe lights' positions (cm).
    TArray<FVector> LightPositions;
    TArray<FCase> Cases;

    TArray<FString> Report;
    TArray<FString> Csv;
    TMap<FString, FSeries> Series;
    // Settled patch values by step and patch.
    TMap<FString, FVector4> Settles;
    TMap<FString, TArray<FTimes>> CaseTimes;
    // The N = 0 images (base, floor), and each case's difference from its own: mean luma difference, % of pixels
    // changed by more than 8/255.
    TMap<FString, TArray<FColor>> Images;
    TMap<FString, FVector2D> Differences;
    TMap<FString, int32> Shown;
    // Per-pixel luma sums over a noise step's frames, and each case's mean and 99th-percentile deviation.
    TArray<double> NoiseSum;
    TArray<double> NoiseSquares;
    int32 NoiseFrames = 0;
    // Mean, 99th percentile and lit-pixel mean of the deviation; and the N = 0 cases' per-pixel means.
    TMap<FString, FVector> Noises;
    TMap<FString, TArray<double>> ZeroMeans;
};
