#include "Voxel/VoxelTransitionShots.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Foundation/FoundationProfile.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Voxel/VoxelLook.h"
#include "Voxel/VoxelWorld.h"
#include "VoxelLog.h"

namespace
{
    constexpr double TransitionCellCm = UFoundationProfile::BlockCm;
    // As the -VoxelShot shots: the settle frames and their fast adaptation, in f-stops per second.
    constexpr int32 TransitionSettleFrames = 150;
    constexpr float TransitionFastSpeed = 50.f;
    // Frames to wait for a requested screenshot file before moving on anyway.
    constexpr int32 TransitionWriteFrames = 120;
    // The frames after each cut, in seconds.
    constexpr double TransitionOffsets[] = {0, .25, .5, 1, 1.5, 2, 3, 5};
}

TSharedPtr<FVoxelTransitionShots> FVoxelTransitionShots::Make(const FString& InFolder)
{
    FString Text;
    if (InFolder.IsEmpty() || !FParse::Value(FCommandLine::Get(), TEXT("VoxelShotTransition="), Text, false))
        return nullptr;
    TArray<FString> Parts;
    Text.ParseIntoArray(Parts, TEXT(","));
    bool bNumeric = Parts.Num() == 12;
    for (const FString& Part : Parts)
        bNumeric = bNumeric && Part.IsNumeric();
    if (!bNumeric)
    {
        UE_LOG(LogUnnamedVoxel, Warning,
            TEXT("-VoxelShotTransition=%s is not sun camera, sun target, shade camera, shade target (12 numbers); no "
                 "transition shots"),
            *Text);
        return nullptr;
    }
    auto At = [&Parts](int32 First)
    {
        return FVector(
            FCString::Atod(*Parts[First]), FCString::Atod(*Parts[First + 1]), FCString::Atod(*Parts[First + 2]));
    };
    return MakeShared<FVoxelTransitionShots>(InFolder, At(0), At(3), At(6), At(9));
}

FVoxelTransitionShots::FVoxelTransitionShots(const FString& InFolder, const FVector& SunCamera,
    const FVector& SunTarget, const FVector& ShadeCamera, const FVector& ShadeTarget)
    : Folder(InFolder), Cameras{SunCamera, ShadeCamera}, Targets{SunTarget, ShadeTarget}
{
}

bool FVoxelTransitionShots::Tick(AVoxelWorld& World)
{
    if (Stage == EStage::Done) return false;
    UWorld* Game = World.GetWorld();
    APlayerController* Controller = UGameplayStatics::GetPlayerController(Game, 0);
    if (!Controller || (Stage != EStage::Start && !Camera.IsValid()))
    {
        Report.Add(TEXT("Stopped: no player or camera. RESULT: FAILED"));
        Finish(World);
        return false;
    }
    if (IsShotPending()) return true;
    const double Now = Game->GetTimeSeconds();
    switch (Stage)
    {
    case EStage::Start:
    {
        ACameraActor* Spawned = Game->SpawnActor<ACameraActor>();
        Spawned->GetCameraComponent()->SetFieldOfView(75);
        Spawned->GetCameraComponent()->bConstrainAspectRatio = false;
        Camera = Spawned;
        SetFast(true);
        Place(World, 0);
        Controller->SetViewTarget(Spawned);
        IFileManager::Get().MakeDirectory(*Folder, true);
        // The map's own speeds, which the cuts adapt at.
        for (TActorIterator<APostProcessVolume> It(Game); It; ++It)
            if (It->bUnbound)
                Report.Add(FString::Printf(TEXT("Adaptation (the map's volume): up %.2f, down %.2f f-stops per second; "
                                                "EV100 %.3f to %.3f, bias %.3f"),
                    It->Settings.AutoExposureSpeedUp, It->Settings.AutoExposureSpeedDown,
                    It->Settings.AutoExposureMinBrightness, It->Settings.AutoExposureMaxBrightness,
                    It->Settings.AutoExposureBias));
        const FVoxelLook Look = FVoxelLook::Read();
        Report.Add(FString::Printf(
            TEXT("Look: DayEV %.3f, cap %.3f, sky %.3f, sky fill %.3f"), Look.DayEV, Look.Cap, Look.Sky, Look.SkyFill));
        Stage = EStage::Settle;
        Frames = 0;
        return true;
    }
    case EStage::Settle:
        if (++Frames < TransitionSettleFrames) return true;
        Shoot(TEXT("sun-settled"), 0);
        Stage = EStage::Cut;
        return true;
    case EStage::Cut:
        // From here the camera adapts at the map's speeds: the cut into the shade, its frames, then the cut back out.
        SetFast(false);
        Leg = 0;
        Next = 0;
        Place(World, 1);
        LegStart = Now;
        Stage = EStage::Run;
        return true;
    case EStage::Run:
        if (Next < int32(UE_ARRAY_COUNT(TransitionOffsets)))
        {
            if (Now - LegStart >= TransitionOffsets[Next])
            {
                Shoot(FString::Printf(TEXT("%s-%04d"), Leg == 0 ? TEXT("in") : TEXT("out"),
                          FMath::RoundToInt32(TransitionOffsets[Next] * 1000)),
                    Now - LegStart);
                ++Next;
            }
            return true;
        }
        if (Leg == 0)
        {
            Leg = 1;
            Next = 0;
            Place(World, 0);
            LegStart = Now;
            return true;
        }
        Report.Add(TEXT("RESULT: DONE"));
        Finish(World);
        return false;
    default:
        return false;
    }
}

void FVoxelTransitionShots::Place(AVoxelWorld& World, int32 View)
{
    if (!Camera.IsValid()) return;
    Camera->SetActorLocationAndRotation(
        World.OriginCm + Cameras[View] * TransitionCellCm, (Targets[View] - Cameras[View]).Rotation());
}

void FVoxelTransitionShots::SetFast(bool bFast)
{
    if (!Camera.IsValid()) return;
    FPostProcessSettings& Post = Camera->GetCameraComponent()->PostProcessSettings;
    Post.bOverride_AutoExposureSpeedUp = Post.bOverride_AutoExposureSpeedDown = bFast;
    Post.AutoExposureSpeedUp = Post.AutoExposureSpeedDown = TransitionFastSpeed;
}

void FVoxelTransitionShots::Shoot(const FString& Name, double Seconds)
{
    LastShot = FPaths::Combine(Folder, FString::Printf(TEXT("transition-%s.png"), *Name));
    LastShotFrame = GFrameCounter;
    FScreenshotRequest::RequestScreenshot(LastShot, false, false);
    Report.Add(FString::Printf(TEXT("transition-%s.png: %.3f s after the cut, frame %llu"), *Name, Seconds,
        (unsigned long long)GFrameCounter));
}

bool FVoxelTransitionShots::IsShotPending() const
{
    return !LastShot.IsEmpty() && !IFileManager::Get().FileExists(*LastShot) &&
        GFrameCounter - LastShotFrame < TransitionWriteFrames;
}

void FVoxelTransitionShots::Finish(AVoxelWorld& World)
{
    Stage = EStage::Done;
    if (APlayerController* Controller = UGameplayStatics::GetPlayerController(World.GetWorld(), 0))
        Controller->SetViewTarget(Controller->GetPawn());
    if (Camera.IsValid()) Camera->Destroy();
    IFileManager::Get().MakeDirectory(*Folder, true);
    FFileHelper::SaveStringToFile(
        FString::Join(Report, TEXT("\n")) + TEXT("\n"), *FPaths::Combine(Folder, TEXT("transition.txt")));
    UE_LOG(LogUnnamedVoxel, Display, TEXT("Transition shots: %s"), *FString::Join(Report, TEXT(" | ")));
}
