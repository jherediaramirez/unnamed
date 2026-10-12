#pragma once

#include "CoreMinimal.h"

class ACameraActor;
class AVoxelWorld;

// The eye adaptation between sun and shade (the brightness pass; Jesus, 2026-10-12: "Go from light to dark places
// quickly you are left with this blinding light"): -VoxelShotTransition=<sun camera, sun target, shade camera, shade
// target> (12 numbers, in blocks) with -VoxelShot=<folder>, Scripts/CaptureVoxel.ps1 -Transition. It runs once the
// first view is on screen, before the -VoxelShot shots. The camera settles on the sun view at the shots' fast
// adaptation, then adapts at the map's own speeds while it cuts into the shade and back out, with a frame at set times
// after each cut. Writes transition.txt (the speeds, the look, each frame's time after its cut) and
// transition-<in|out>-<ms>.png.
class FVoxelTransitionShots
{
public:
    // Null without -VoxelShotTransition, or with a warning when it is not 12 numbers.
    static TSharedPtr<FVoxelTransitionShots> Make(const FString& InFolder);
    FVoxelTransitionShots(const FString& InFolder, const FVector& SunCamera, const FVector& SunTarget,
        const FVector& ShadeCamera, const FVector& ShadeTarget);
    // Runs one frame; false once finished (the files written and the view given back).
    bool Tick(AVoxelWorld& World);

private:
    enum class EStage : uint8
    {
        Start,
        Settle,
        // The settled sun frame is written; then the cut into the shade.
        Cut,
        Run,
        Done
    };
    // Puts the camera on a view (0 sun, 1 shade).
    void Place(AVoxelWorld& World, int32 View);
    // The shot camera's adaptation: the shots' fast speed, or the map's own.
    void SetFast(bool bFast);
    void Shoot(const FString& Name, double Seconds);
    bool IsShotPending() const;
    void Finish(AVoxelWorld& World);

    FString Folder;
    FVector Cameras[2];
    FVector Targets[2];
    EStage Stage = EStage::Start;
    TWeakObjectPtr<ACameraActor> Camera;
    int32 Frames = 0;
    // 0: cut into the shade; 1: cut back out into the sun.
    int32 Leg = 0;
    double LegStart = 0;
    int32 Next = 0;
    FString LastShot;
    uint64 LastShotFrame = 0;
    TArray<FString> Report;
};
