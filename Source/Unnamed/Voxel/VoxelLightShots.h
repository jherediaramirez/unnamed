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
        TFunction<void()> End;
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
    };

    FStep& AddStep(const FString& Name, int32 Frames, TFunction<void()> Begin = nullptr);
    void Build(AVoxelWorld& World);
    void AddCubeSteps(AVoxelWorld& World);
    void FindProbeView();
    void AddProbeSteps(AVoxelWorld& World);
    void AddLeakSteps(AVoxelWorld& World);
    void AddEditSteps(AVoxelWorld& World);
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
    // Of the first Count probe lights, those whose reach meets the view's frustum (the ones the renderer draws).
    int32 CountInView(int32 Count) const;
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
