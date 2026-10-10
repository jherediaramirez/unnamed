#include "Voxel/VoxelLightShots.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlocks.h"
#include "HAL/IConsoleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "UnrealClient.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelLights.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelWorldSubsystem.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

// Reads the view's eye adaptation exposure (a GPU read-back a few frames old) on the game thread.
class FLightShotsExposure : public FSceneViewExtensionBase
{
public:
    explicit FLightShotsExposure(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}
    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
    {
        Value = InView.GetLastEyeAdaptationExposure();
    }
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
    float Value = 0;
};

namespace
{
    constexpr double LightShotsFieldOfView = 75;
    const FIntVector LightShotsUp(0, 0, 1);

    double LightShotsFocal(int32 Width)
    {
        return Width * .5 / FMath::Tan(FMath::DegreesToRadians(LightShotsFieldOfView / 2));
    }

    // Every face of an opaque cube within Reach cells of Center that borders air: the cube's cell and the normal.
    void LightShotsForEachFace(const VoxelLightProbe::FCells& Cells, const FIntVector& Center, int32 Reach,
        TFunctionRef<void(const FIntVector&, const FIntVector&)> Visit)
    {
        const FIntVector Normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int32 Z = Center.Z - Reach; Z <= Center.Z + Reach; ++Z)
            for (int32 Y = Center.Y - Reach; Y <= Center.Y + Reach; ++Y)
                for (int32 X = Center.X - Reach; X <= Center.X + Reach; ++X)
                {
                    const FIntVector Cell(X, Y, Z);
                    if (!Cells.IsOpaqueCube(Cell)) continue;
                    for (const FIntVector& Normal : Normals)
                        if (Cells.IsAir(Cell + Normal)) Visit(Cell, Normal);
                }
    }

    // Mean of the on-screen values (W >= 0) of frames [From, To).
    FVector4 LightShotsMean(const TArray<FVector4>& Values, int32 From, int32 To)
    {
        FVector4 Sum(0, 0, 0, 0);
        int32 Count = 0;
        for (int32 Frame = FMath::Max(From, 0); Frame < FMath::Min(To, Values.Num()); ++Frame)
            if (Values[Frame].W >= 0)
            {
                Sum += Values[Frame];
                ++Count;
            }
        return Count ? Sum * (1. / Count) : FVector4(0, 0, 0, -1);
    }

    // Temporal standard deviation of the mean sRGB over frames [From, To).
    double LightShotsDeviation(const TArray<FVector4>& Values, int32 From, int32 To)
    {
        const double Mean = LightShotsMean(Values, From, To).W;
        double Squares = 0;
        int32 Count = 0;
        for (int32 Frame = FMath::Max(From, 0); Frame < FMath::Min(To, Values.Num()); ++Frame)
            if (Values[Frame].W >= 0)
            {
                Squares += FMath::Square(Values[Frame].W - Mean);
                ++Count;
            }
        return Count > 1 ? FMath::Sqrt(Squares / (Count - 1)) : 0;
    }

    // Mean difference between consecutive frames over [From, To).
    double LightShotsFrameStep(const TArray<FVector4>& Values, int32 From, int32 To)
    {
        double Sum = 0;
        int32 Count = 0;
        for (int32 Frame = FMath::Max(From, 1); Frame < FMath::Min(To, Values.Num()); ++Frame)
            if (Values[Frame].W >= 0 && Values[Frame - 1].W >= 0)
            {
                Sum += FMath::Abs(Values[Frame].W - Values[Frame - 1].W);
                ++Count;
            }
        return Count ? Sum / Count : 0;
    }

    // The first frame from which the value stays within 2/255 of its settled value (the mean from SettledFrom on),
    // on a three-frame centred mean so single-frame noise does not count.
    int32 LightShotsLag(const TArray<FVector4>& Values, int32 SettledFrom)
    {
        const double Settled = LightShotsMean(Values, SettledFrom, Values.Num()).W;
        if (Settled < 0) return -1;
        for (int32 Frame = Values.Num() - 2; Frame >= 1; --Frame)
        {
            if (Values[Frame - 1].W < 0 || Values[Frame].W < 0 || Values[Frame + 1].W < 0) continue;
            const double Smooth = (Values[Frame - 1].W + Values[Frame].W + Values[Frame + 1].W) / 3;
            if (FMath::Abs(Smooth - Settled) > 2) return Frame + 1;
        }
        return 0;
    }

    double LightShotsLuma(const FColor& Color)
    {
        return .2126 * Color.R + .7152 * Color.G + .0722 * Color.B;
    }
}

FVoxelLightShots::FVoxelLightShots(const FString& InFolder) : Folder(InFolder)
{
    FParse::Value(FCommandLine::Get(), TEXT("VoxelLightsIntensity="), IntensityCd);
    FParse::Value(FCommandLine::Get(), TEXT("VoxelShotTorches="), TorchCount);
    bTorchGates = FParse::Param(FCommandLine::Get(), TEXT("VoxelShotTorchGates"));
    bTorchLookPairs = FParse::Param(FCommandLine::Get(), TEXT("VoxelShotTorchLook"));
    bTorchRoom = FParse::Param(FCommandLine::Get(), TEXT("VoxelShotTorchRoom")) || bTorchLookPairs;
}

FVoxelLightShots::~FVoxelLightShots()
{
    if (bBuilt && !bFinished) SetMegaLights(MegaLightsAtStart != 0);
    if (bBuilt && !bFinished) RestoreRoomVariables();
    if (ScreenshotHandle.IsValid()) UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
}

FVoxelLightShots::FStep& FVoxelLightShots::AddStep(const FString& Name, int32 Frames, TFunction<void()> Begin)
{
    FStep& Step = Steps.AddDefaulted_GetRef();
    Step.Name = Name;
    Step.Frames = Frames;
    Step.Begin = MoveTemp(Begin);
    return Step;
}

bool FVoxelLightShots::Tick(AVoxelWorld& World)
{
    if (bFinished) return false;
    if (bBegun && Steps.IsValidIndex(StepIndex) && Steps[StepIndex].OnTick) Steps[StepIndex].OnTick();
    if (!bBuilt)
    {
        bBuilt = true;
        Build(World);
    }
    const UVoxelWorldSubsystem* Streaming = UVoxelWorldSubsystem::Get(World.GetWorld());
    if (Cells && StepIndex < Steps.Num() - 1 &&
        (World.OriginCm != Cells->OriginCm || !Streaming || Streaming->GetPipeline() != Cells->Pipeline))
    {
        Line(TEXT("Stopped: the world moved or its streaming restarted during the probe."));
        StepIndex = Steps.Num() - 1;
        bBegun = false;
    }
    while (StepIndex < Steps.Num())
    {
        FStep& Step = Steps[StepIndex];
        if (!bBegun)
        {
            bBegun = true;
            StepFrame = 0;
            Times = FTimes();
            EditFrames = 0;
            BeginFrame = GFrameCounter;
            if (Step.Begin) Step.Begin();
            // An edit's frames start once its columns are meshed and on screen again.
            if (Step.bEdit) return true;
        }
        if (Step.bEdit && StepFrame == 0)
        {
            if (!Cells->Pipeline->IsIdle()) return true;
            EditFrames = int32(GFrameCounter - BeginFrame);
        }
        if (StepFrame < Step.Frames) break;
        if (Step.End) Step.End();
        ++StepIndex;
        bBegun = false;
    }
    if (StepIndex >= Steps.Num())
    {
        Finish(World);
        return false;
    }
    const FStep& Step = Steps[StepIndex];
    if (Step.bTimed && StepFrame >= TimedSkip)
    {
        Times.GpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
        Times.GameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
        Times.FrameMs += FApp::GetDeltaTime() * 1000;
        ++Times.Frames;
    }
    if (StepFrame >= Step.CaptureFrom)
    {
        CaptureStep = StepIndex;
        CaptureFrame = StepFrame;
        FScreenshotRequest::RequestScreenshot(false);
    }
    ++StepFrame;
    return true;
}

void FVoxelLightShots::OnScreenshot(int32 InWidth, int32 InHeight, const TArray<FColor>& Pixels)
{
    if (!Steps.IsValidIndex(CaptureStep)) return;
    const int32 Index = CaptureStep;
    CaptureStep = INDEX_NONE;
    Width = InWidth;
    Height = InHeight;
    if (Steps[Index].OnFrame) Steps[Index].OnFrame(CaptureFrame, Pixels);
}

void FVoxelLightShots::Build(AVoxelWorld& World)
{
    Cells = MakeUnique<VoxelLightProbe::FCells>(World);
    MegaLightsVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.EnableForProject"));
    MegaLightsAtStart = MegaLightsVariable ? MegaLightsVariable->GetInt() : 0;
    if (const UGameViewportClient* Viewport = World.GetWorld()->GetGameViewport(); Viewport && Viewport->Viewport)
    {
        Width = Viewport->Viewport->GetSizeXY().X;
        Height = Viewport->Viewport->GetSizeXY().Y;
    }
    const TTuple<FString, FVector, FVector>* CaveView = nullptr;
    for (const TTuple<FString, FVector, FVector>& Candidate : World.GetCaveShotViews())
        if (Candidate.Get<0>() == TEXT("cave")) CaveView = &Candidate;
    APlayerController* Controller = UGameplayStatics::GetPlayerController(&World, 0);
    // The torch room is built under the spawn, so it needs no cave view.
    if (!Cells->IsValid() || (!CaveView && !bTorchRoom) || !MegaLightsVariable || !Controller || Width <= 0)
    {
        Line(FString::Printf(TEXT("No light probe: %s."),
            !CaveView ? TEXT("no cave view in the streamed area") : TEXT("no streaming, player or MegaLights")));
        return;
    }
    ACameraActor* ShotCamera = World.GetWorld()->SpawnActor<ACameraActor>();
    UCameraComponent* Lens = ShotCamera->GetCameraComponent();
    Lens->SetFieldOfView(LightShotsFieldOfView);
    Lens->bConstrainAspectRatio = false;
    Lens->bOverrideAspectRatioAxisConstraint = true;
    Lens->AspectRatioAxisConstraint = AspectRatio_MaintainXFOV;
    FPostProcessSettings& Post = Lens->PostProcessSettings;
    Post.bOverride_AutoExposureSpeedUp = Post.bOverride_AutoExposureSpeedDown = true;
    Post.AutoExposureSpeedUp = Post.AutoExposureSpeedDown = 50;
    Controller->SetViewTarget(ShotCamera);
    Camera = ShotCamera;
    Exposure = FSceneViewExtensions::NewExtension<FLightShotsExposure>();
    ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FVoxelLightShots::OnScreenshot);
    if (bTorchRoom)
    {
        // The look pairs also shoot T1's probe cave.
        if (bTorchLookPairs && CaveView)
        {
            ViewBlocks = Cells->ToBlocks(CaveView->Get<1>());
            ViewRotation = (CaveView->Get<2>() - CaveView->Get<1>()).Rotation();
            FindProbeView();
        }
        AddTorchRoomSteps(World);
        return;
    }
    ViewBlocks = Cells->ToBlocks(CaveView->Get<1>());
    ViewRotation = (CaveView->Get<2>() - CaveView->Get<1>()).Rotation();
    Line(FString::Printf(TEXT("Cave view: camera %s, facing %s (blocks). Light intensity %.0f cd, radius %.0f cm, "
                              "source radius %.0f cm. MegaLights for the project at start: %d."),
        *ViewBlocks.ToCompactString(), *ViewRotation.Vector().ToCompactString(), IntensityCd,
        VoxelLightProbe::FSettings().RadiusCm, VoxelLightProbe::FSettings().SourceRadiusCm, MegaLightsAtStart));

    // Every case but the leak test looks into the probe cave: dark, and big enough for 100 lights.
    FindProbeView();
    if (bTorchGates)
    {
        AddTorchGateSteps(World);
        return;
    }
    if (TorchCount > 0)
    {
        // -VoxelShotTorches: torches through the block world, their lights from the pool, frames counted from the
        // placement until the columns are back on screen.
        AddStep(TEXT("settle"), 90, [this] { SetView(ViewBlocks, ViewRotation); });
        FStep& Torches = AddStep(TEXT("torches"), 30, [this, &World]
            { GEngine->Exec(World.GetWorld(), *FString::Printf(TEXT("voxel.LightProbe %d torches"), TorchCount)); });
        Torches.bEdit = true;
        Torches.CaptureFrom = 29;
        Torches.OnFrame = [this](int32 Frame, const TArray<FColor>& Pixels)
        { SavePng(TEXT("lights-torches"), Pixels); };
        Torches.End = [this, &World]
        {
            Line(FString::Printf(TEXT("Torches: %d placed through the block world, columns on screen after %d frames."),
                TorchCount, EditFrames));
            if (const FVoxelLights* Pool = World.GetLights()) Line(Pool->Describe());
            GEngine->Exec(World.GetWorld(), TEXT("voxel.LightsInfo"));
        };
        AddStep(TEXT("cleanup"), 5, [&World] { GEngine->Exec(World.GetWorld(), TEXT("voxel.LightProbe 0")); });
        return;
    }
    // The eye adapts to the cave once, then stays as it is for every case.
    AddStep(TEXT("settle"), 150,
        [this]
        {
            SetView(ViewBlocks, ViewRotation);
            SetMegaLights(false);
            VoxelLightProbe::Clear();
        });
    AddStep(TEXT("freeze"), 30,
        [this]
        {
            FPostProcessSettings& Frozen = Camera->GetCameraComponent()->PostProcessSettings;
            Frozen.AutoExposureSpeedUp = Frozen.AutoExposureSpeedDown = 0;
        })
        .End = [this] { Line(FString::Printf(TEXT("Exposure frozen at %.6g."), GetExposure())); };
    AddCubeSteps(World);
    AddProbeSteps(World);
    AddLeakSteps(World);
    AddEditSteps(World);
    AddStep(TEXT("cleanup"), 5,
        [this, &World]
        {
            VoxelLightProbe::Clear();
            SetMegaLights(MegaLightsAtStart != 0);
            for (const TPair<FIntVector, FFoundationBlockState>& Original : Originals)
                SetCell(World, Original.Key, Original.Value);
        });
}

void FVoxelLightShots::AddCubeSteps(AVoxelWorld& World)
{
    // The cube: the first floor cell of the camera's cave, in fill order, 3 to 8 blocks from the camera and in plain
    // view well inside the frame.
    for (const FIntVector& Cell : VoxelLightProbe::FindFloorCells(*Cells, VoxelLightProbe::FCells::ToCell(ViewBlocks)))
    {
        const FVector Center = FVector(Cell) + FVector(.5);
        const double Away = FVector::Dist(Center, ViewBlocks);
        FVector2D Pixel;
        double Depth = 0;
        if (Away < 3 || Away > 8 || !Project(ViewBlocks, ViewRotation, Center, Pixel, Depth) ||
            !IsOnScreen(Pixel, 100) || !Cells->IsClear(ViewBlocks, Center))
            continue;
        bCube = true;
        CubeCell = Cell;
        break;
    }
    if (!bCube)
    {
        Line(TEXT("Glowing cubes: no floor cell in view 3 to 8 blocks from the probe camera."));
        return;
    }
    Line(FString::Printf(TEXT("Cube cell %s, %.1f blocks from the camera."), *CubeCell.ToString(),
        FVector::Dist(FVector(CubeCell) + FVector(.5), ViewBlocks)));
    // Wall patches 2, 5 and 10 blocks from the cube's centre: faces turned towards the cube and the camera, in view
    // past the cube, walls before floors and ceilings, then the nearest to the distance, then the nearest to the eye.
    const FVector CubeCenter = FVector(CubeCell) + FVector(.5);
    for (const int32 Distance : {2, 5, 10})
    {
        FPatch Patch;
        double Best = TNumericLimits<double>::Max();
        LightShotsForEachFace(*Cells, CubeCell, 11,
            [&](const FIntVector& Cell, const FIntVector& Normal)
            {
                const FVector Facing(Normal);
                const FVector Point = FVector(Cell) + FVector(.5) + Facing * .51;
                const FVector ToCube = CubeCenter - Point;
                const double Reach = ToCube.Size();
                FVector2D Pixel;
                double Depth = 0;
                if (FMath::Abs(Reach - Distance) > .6 || FVector::DotProduct(Facing, ToCube) < .3 * Reach ||
                    FVector::DotProduct(Facing, ViewBlocks - Point) <= 0 ||
                    !Project(ViewBlocks, ViewRotation, Point, Pixel, Depth) || !IsOnScreen(Pixel, 24) ||
                    !Cells->IsClear(ViewBlocks, Point, nullptr, &CubeCell))
                    return;
                const double Score = (Normal.Z == 0 ? 0 : 100) + FMath::Abs(Reach - Distance) * 10 + Depth * .01;
                if (Score >= Best) return;
                Best = Score;
                Patch = {FString::Printf(TEXT("wall%d"), Distance), Point, Facing};
            });
        if (Patch.IsSet()) CubePatches.Add(Patch);
        Line(Patch.IsSet() ? FString::Printf(TEXT("Patch %s: face at %s, normal %s, %.2f blocks from the cube."),
                                 *Patch.Name, *Patch.Point.ToCompactString(), *Patch.Normal.ToCompactString(),
                                 FVector::Dist(Patch.Point, CubeCenter))
                           : FString::Printf(TEXT("Patch wall%d: none found."), Distance));
    }
    // The away view: the smallest turn that takes the whole cube off screen and keeps the 5-block patch on it (else
    // the 2-block one, else the 10-block one).
    for (const int32 Index : {1, 0, 2})
    {
        if (bAway || !CubePatches.IsValidIndex(Index)) continue;
        for (int32 Turn = 5; Turn <= 120 && !bAway; Turn += 5)
            for (const int32 Sign : {1, -1})
                for (const int32 Pitch : {0, -10, 10, -20, 20})
                {
                    if (bAway) break;
                    const FRotator Rotation = ViewRotation + FRotator(Pitch, Sign * Turn, 0);
                    FVector2D Pixel;
                    double Depth = 0;
                    if (!Project(ViewBlocks, Rotation, CubePatches[Index].Point, Pixel, Depth) ||
                        !IsOnScreen(Pixel, 32))
                        continue;
                    bool bHidden = true;
                    for (int32 I = 0; I <= 4 && bHidden; ++I)
                        for (int32 J = 0; J <= 4 && bHidden; ++J)
                            for (int32 K = 0; K <= 4 && bHidden; ++K)
                                bHidden = !Project(ViewBlocks, Rotation, FVector(CubeCell) + FVector(I, J, K) / 4.,
                                              Pixel, Depth) ||
                                    !IsOnScreen(Pixel, -8);
                    if (!bHidden) continue;
                    bAway = true;
                    AwayRotation = Rotation;
                    Line(FString::Printf(TEXT("Away view: turned %d degrees %s and %d up, %s in view."), Turn,
                        Sign > 0 ? TEXT("right") : TEXT("left"), Pitch, *CubePatches[Index].Name));
                }
    }

    AddStep(TEXT("before-time"), 120 + TimedSkip).bTimed = true;
    Steps.Last().End = [this] { CaseTimes.FindOrAdd(TEXT("cube-before")).Add(Times); };
    FStep& Before = AddStep(TEXT("before"), 30);
    Before.CaptureFrom = 0;
    Before.OnFrame = [this](int32 Frame, const TArray<FColor>& Pixels)
    {
        for (const FPatch& Patch : CubePatches)
            Record(TEXT("before"), Frame, Patch, Measure(Patch, Pixels));
        if (Frame == 29) SavePng(TEXT("lights-before"), Pixels);
    };
    Before.End = [this] { NoteExposure(TEXT("before")); };

    const FFoundationBlockWorld& Blocks = World.GetAuthority()->World;
    for (const TCHAR* Id :
        {TEXT("glowstone"), TEXT("sea_lantern"), TEXT("shroomlight"), TEXT("ochre_froglight"), TEXT("jack_o_lantern")})
    {
        const FFoundationBlockType* Type = Blocks.FindBlock(Id);
        if (!Type)
        {
            Line(FString::Printf(TEXT("%s: not in the registry."), Id));
            continue;
        }
        const FString Name(Id);
        const FFoundationBlockState State = Type->GetDefaultState();
        FStep& Place = AddStep(Name, 151, [this, &World, State] { SetCell(World, CubeCell, State); });
        Place.bEdit = true;
        Place.CaptureFrom = 0;
        Place.OnFrame = [this, Name](int32 Frame, const TArray<FColor>& Pixels)
        {
            for (const FPatch& Patch : CubePatches)
                Record(Name, Frame, Patch, Measure(Patch, Pixels));
            if (Frame == 0 || Frame == 30 || Frame == 120)
                SavePng(FString::Printf(TEXT("lights-%s-f%03d"), *Name, Frame), Pixels);
        };
        Place.End = [this, Name]
        {
            NoteExposure(Name);
            Line(FString::Printf(TEXT("%s: on screen %d frames after the edit."), *Name, EditFrames));
            for (const FPatch& Patch : CubePatches)
            {
                const TArray<FVector4>& Values = Series.FindRef(Name + TEXT("/") + Patch.Name);
                const TArray<FVector4>& Base = Series.FindRef(TEXT("before/") + Patch.Name);
                const FVector4 Settled = LightShotsMean(Values, 121, 151), Was = LightShotsMean(Base, 0, 30);
                Settles.Add(Name + TEXT("/") + Patch.Name, Settled);
                Line(FString::Printf(TEXT("  %s: %.1f -> %.1f mean sRGB (%+.1f; RGB %.0f/%.0f/%.0f), at frame 0 "
                                          "%.1f, 30 %.1f, 120 %.1f; lag %d frames; noise: std %.2f settled, "
                                          "%.2f before, frame-to-frame %.2f"),
                    *Patch.Name, Was.W, Settled.W, Settled.W - Was.W, Settled.X, Settled.Y, Settled.Z,
                    Values.IsValidIndex(0) ? Values[0].W : -1., Values.IsValidIndex(30) ? Values[30].W : -1.,
                    Values.IsValidIndex(120) ? Values[120].W : -1., LightShotsLag(Values, 121),
                    LightShotsDeviation(Values, 121, 151), LightShotsDeviation(Base, 0, 30),
                    LightShotsFrameStep(Values, 121, 151)));
            }
        };
        AddStep(Name + TEXT("-settle"), 60);
        AddStep(Name + TEXT("-time"), 120 + TimedSkip).bTimed = true;
        Steps.Last().End = [this, Name] { CaseTimes.FindOrAdd(TEXT("cube-") + Name).Add(Times); };
        if (bAway)
        {
            FStep& Away = AddStep(Name + TEXT("-away"), 150, [this] { SetView(ViewBlocks, AwayRotation); });
            Away.CaptureFrom = 120;
            Away.OnFrame = [this, Name](int32 Frame, const TArray<FColor>& Pixels)
            {
                for (const FPatch& Patch : CubePatches)
                    Record(Name + TEXT("-away"), Frame - 120, Patch, Measure(Patch, Pixels));
                if (Frame == 149) SavePng(FString::Printf(TEXT("lights-%s-away"), *Name), Pixels);
            };
            Away.End = [this, Name]
            {
                NoteExposure(Name + TEXT("-away"));
                for (const FPatch& Patch : CubePatches)
                {
                    const FVector4 Off = LightShotsMean(Series.FindRef(Name + TEXT("-away/") + Patch.Name), 0, 30);
                    if (Off.W < 0) continue;
                    const double On = Settles.FindRef(Name + TEXT("/") + Patch.Name).W;
                    Line(FString::Printf(TEXT("  %s away: %.1f against %.1f on screen: drop %.1f (%s)"), *Patch.Name,
                        Off.W, On, On - Off.W,
                        On - Off.W > 3 ? TEXT("over 3/255: the cube lights it only while on screen")
                                       : TEXT("within 3/255: the surface cache sees the cube")));
                }
            };
        }
        FStep& Remove = AddStep(Name + TEXT("-off"), 150,
            [this, &World]
            {
                SetView(ViewBlocks, ViewRotation);
                SetCell(World, CubeCell, FFoundationBlockState());
            });
        Remove.bEdit = true;
        Remove.CaptureFrom = 120;
        Remove.OnFrame = [this, Name](int32 Frame, const TArray<FColor>& Pixels)
        {
            for (const FPatch& Patch : CubePatches)
                Record(Name + TEXT("-off"), Frame - 120, Patch, Measure(Patch, Pixels));
        };
        Remove.End = [this, Name]
        {
            for (const FPatch& Patch : CubePatches)
                Line(FString::Printf(TEXT("  %s with the cube removed again: %.1f"), *Patch.Name,
                    LightShotsMean(Series.FindRef(Name + TEXT("-off/") + Patch.Name), 0, 30).W));
        };
        AddStep(Name + TEXT("-off-settle"), 60);
        AddStep(Name + TEXT("-off-time"), 120 + TimedSkip).bTimed = true;
        Steps.Last().End = [this, Name] { CaseTimes.FindOrAdd(TEXT("cubeoff-") + Name).Add(Times); };
    }
}

void FVoxelLightShots::FindProbeView()
{
    // The probe cave: of the covered caves met every 8 blocks within 3 columns of the streamed area's centre (and the
    // cave view's own), the one with the most floor cells. Its camera stands on the floor cell that sees the most of
    // a sample of them within 20 blocks, facing their mean direction. The view starts as the cave view's.
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const FIntPoint Center = Cells->Pipeline->GetRings().Center;
    TSet<FIntVector> Seen;
    TArray<FIntVector> Best =
        VoxelLightProbe::FindFloorCells(*Cells, VoxelLightProbe::FCells::ToCell(ViewBlocks), &Seen);
    int32 Caves = 1;
    for (int32 Y = (Center.Y - 3) * Size; Y < (Center.Y + 4) * Size; Y += 8)
        for (int32 X = (Center.X - 3) * Size; X < (Center.X + 4) * Size; X += 8)
            for (int32 Z = VoxelConstants::MinZ + 6, Top = Cells->GetSurface(X, Y) - 4; Z < Top; ++Z)
            {
                const FIntVector Cell(X, Y, Z);
                if (Seen.Contains(Cell) || !Cells->IsAir(Cell) || !Cells->IsSolid(Cell - LightShotsUp) ||
                    !Cells->IsAir(Cell + LightShotsUp))
                    continue;
                TArray<FIntVector> Floors = VoxelLightProbe::FindFloorCells(*Cells, Cell, &Seen);
                ++Caves;
                if (Floors.Num() > Best.Num()) Best = MoveTemp(Floors);
            }
    const int32 Stride = FMath::Max(1, Best.Num() / 256);
    int32 MostSeen = -1;
    for (int32 Candidate = 0; Candidate < Best.Num(); Candidate += FMath::Max(1, Best.Num() / 48))
    {
        const FVector Eye = FVector(Best[Candidate]) + FVector(.5, .5, 1.6);
        FVector Toward = FVector::ZeroVector;
        int32 Visible = 0;
        for (int32 Index = 0; Index < Best.Num(); Index += Stride)
        {
            const FVector Target = FVector(Best[Index]) + FVector(.5, .5, 1);
            const double Squared = FVector::DistSquared(Eye, Target);
            if (Squared < 4 || Squared > 400 || !Cells->IsClear(Eye, Target)) continue;
            Toward += (Target - Eye).GetSafeNormal();
            ++Visible;
        }
        if (Visible <= MostSeen) continue;
        MostSeen = Visible;
        ViewBlocks = Eye;
        ViewRotation = Toward.IsNearlyZero() ? ViewRotation : Toward.Rotation();
    }
    Line(FString::Printf(TEXT("Probe cave: %d floor cells, the most of %d caves sampled; camera %s facing %s, which "
                              "sees %d of a sample of %d of them within 20 blocks."),
        Best.Num(), Caves, *ViewBlocks.ToCompactString(), *ViewRotation.Vector().ToCompactString(), MostSeen,
        (Best.Num() + Stride - 1) / Stride));
}

void FVoxelLightShots::AddProbeSteps(AVoxelWorld& World)
{
    using VoxelLightProbe::EMode;
    LightPositions =
        VoxelLightProbe::FindFloorLights(*Cells, Cells->ToCm(ViewBlocks), ViewRotation.Vector(), MaxLights);
    Line(FString::Printf(TEXT("Local lights: %d floor cells found for %d lights."), LightPositions.Num(), MaxLights));
    Cases = {{TEXT("base"), EMode::None, 0, false}, {TEXT("floor"), EMode::MegaLights, 0, true}};
    for (const EMode Mode : {EMode::MegaLights, EMode::Vsm, EMode::None})
        for (const int32 Count : {1, 10, 100})
            Cases.Add({FString::Printf(TEXT("%s-%d"), VoxelLightProbe::GetModeName(Mode), Count), Mode, Count,
                Mode == EMode::MegaLights});
    // Three rounds, the middle one in reverse order, so a drift over the run shows rather than biasing one mode.
    for (int32 Round = 0; Round < 3; ++Round)
        for (int32 Order = 0; Order < Cases.Num(); ++Order)
        {
            const FCase Case = Cases[Round == 1 ? Cases.Num() - 1 - Order : Order];
            AddStep(FString::Printf(TEXT("%s-r%d-settle"), *Case.Name, Round + 1), 90,
                [this, &World, Case]
                {
                    SetView(ViewBlocks, ViewRotation);
                    ShowCase(World, Case, LightPositions);
                });
            FStep& Timed = AddStep(FString::Printf(TEXT("%s-r%d"), *Case.Name, Round + 1), 120 + TimedSkip);
            Timed.bTimed = true;
            Timed.End = [this, Case, Round]
            {
                CaseTimes.FindOrAdd(Case.Name).Add(Times);
                if (Round > 0) return;
                // One GPU profile per case, in the log after this marker (Scripts/SummarizeLightProbe.ps1).
                UE_LOG(LogUnnamedVoxel, Display,
                    TEXT("LightShots ProfileGPU %s: %d lights shown, %d in view, MegaLights %d"), *Case.Name,
                    VoxelLightProbe::CountShown(),
                    CountInView(
                        TConstArrayView<FVector>(LightPositions).Left(FMath::Min(Case.Count, LightPositions.Num()))),
                    MegaLightsVariable->GetInt());
                GEngine->Exec(Camera->GetWorld(), TEXT("ProfileGPU"));
            };
            if (Round > 0) continue;
            FStep& Look = AddStep(Case.Name + TEXT("-look"), 11);
            Look.CaptureFrom = 10;
            Look.OnFrame = [this, Case](int32 Frame, const TArray<FColor>& Pixels)
            {
                SavePng(TEXT("lights-probe-") + Case.Name, Pixels);
                if (Case.Count == 0)
                {
                    Images.Add(Case.Name, Pixels);
                    if (Case.Name == TEXT("base")) return;
                }
                // Against the N = 0 image of the same MegaLights setting (the floor's against the base).
                const TArray<FColor>& Zero =
                    Images.FindRef(Case.bMegaLights && Case.Count > 0 ? TEXT("floor") : TEXT("base"));
                if (Zero.Num() != Pixels.Num()) return;
                double Sum = 0;
                int32 Changed = 0;
                for (int32 Index = 0; Index < Pixels.Num(); ++Index)
                {
                    const FColor& A = Pixels[Index];
                    const FColor& B = Zero[Index];
                    Sum += FMath::Abs(LightShotsLuma(A) - LightShotsLuma(B));
                    Changed += FMath::Max3(FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G), FMath::Abs(A.B - B.B)) > 8;
                }
                Differences.Add(Case.Name, FVector2D(Sum / Pixels.Num(), 100. * Changed / Pixels.Num()));
            };
            Look.End = [this, Case]
            {
                NoteExposure(Case.Name);
                Shown.Add(Case.Name, VoxelLightProbe::CountShown());
            };
        }
    // Noise: the per-pixel temporal standard deviation of a still view's luma over 30 frames.
    for (const FCase& Case : Cases)
    {
        FStep& Noise = AddStep(Case.Name + TEXT("-noise"), 120,
            [this, &World, Case]
            {
                SetView(ViewBlocks, ViewRotation);
                ShowCase(World, Case, LightPositions);
                NoiseSum.Reset();
                NoiseSquares.Reset();
                NoiseFrames = 0;
            });
        Noise.CaptureFrom = 90;
        Noise.OnFrame = [this](int32 Frame, const TArray<FColor>& Pixels)
        {
            if (NoiseSum.Num() != Pixels.Num())
            {
                NoiseSum.Init(0, Pixels.Num());
                NoiseSquares.Init(0, Pixels.Num());
                NoiseFrames = 0;
            }
            for (int32 Index = 0; Index < Pixels.Num(); ++Index)
            {
                const double Luma = LightShotsLuma(Pixels[Index]);
                NoiseSum[Index] += Luma;
                NoiseSquares[Index] += Luma * Luma;
            }
            ++NoiseFrames;
        };
        Noise.End = [this, Case]
        {
            if (NoiseFrames < 2) return;
            // A histogram in steps of 1/20 of a level for the 99th percentile.
            TArray<int32> Histogram;
            Histogram.Init(0, 2001);
            double Sum = 0;
            for (int32 Index = 0; Index < NoiseSum.Num(); ++Index)
            {
                const double Mean = NoiseSum[Index] / NoiseFrames;
                const double Deviation =
                    FMath::Sqrt(FMath::Max(0., (NoiseSquares[Index] - NoiseFrames * Mean * Mean) / (NoiseFrames - 1)));
                Sum += Deviation;
                ++Histogram[FMath::Min(FMath::FloorToInt32(Deviation * 20), 2000)];
            }
            int32 Seen = 0, Percentile = 0;
            while (Percentile < 2000 && (Seen += Histogram[Percentile]) < NoiseSum.Num() * .99)
                ++Percentile;
            TArray<double> Means;
            Means.SetNumUninitialized(NoiseSum.Num());
            for (int32 Index = 0; Index < NoiseSum.Num(); ++Index)
                Means[Index] = NoiseSum[Index] / NoiseFrames;
            double LitSum = 0;
            int32 Lit = 0;
            const TArray<double>& Zero = ZeroMeans.FindRef(Case.bMegaLights ? TEXT("floor") : TEXT("base"));
            if (Case.Count > 0 && Zero.Num() == Means.Num())
                for (int32 Index = 0; Index < Means.Num(); ++Index)
                    if (FMath::Abs(Means[Index] - Zero[Index]) > 8)
                    {
                        LitSum += FMath::Sqrt(FMath::Max(
                            0., (NoiseSquares[Index] - NoiseFrames * Means[Index] * Means[Index]) / (NoiseFrames - 1)));
                        ++Lit;
                    }
            if (Case.Count == 0) ZeroMeans.Add(Case.Name, MoveTemp(Means));
            Noises.Add(Case.Name, FVector(Sum / NoiseSum.Num(), Percentile / 20., Lit ? LitSum / Lit : -1.));
        };
    }
}

void FVoxelLightShots::AddLeakSteps(AVoxelWorld& World, bool bTorch)
{
    using VoxelLightProbe::EMode;
    // The leak site: a column whose top three blocks are opaque cubes over a cave, with a camera in the cave 2.5 to 6
    // blocks across from the cave floor below, which it sees. The light hangs 1 m over the column's top.
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const FIntPoint Center = Cells->Pipeline->GetRings().Center;
    FVector LeakCamera = FVector::ZeroVector, LeakLight = FVector::ZeroVector;
    FPatch Patch;
    for (int32 CY = Center.Y - 5; CY <= Center.Y + 5 && !Patch.IsSet(); ++CY)
        for (int32 CX = Center.X - 5; CX <= Center.X + 5 && !Patch.IsSet(); ++CX)
            for (int32 LocalY = 0; LocalY < Size && !Patch.IsSet(); ++LocalY)
                for (int32 LocalX = 0; LocalX < Size && !Patch.IsSet(); ++LocalX)
                {
                    const int32 X = CX * Size + LocalX, Y = CY * Size + LocalY, Top = Cells->GetSurface(X, Y);
                    if (!Cells->IsOpaqueCube({X, Y, Top}) || !Cells->IsOpaqueCube({X, Y, Top - 1}) ||
                        !Cells->IsOpaqueCube({X, Y, Top - 2}) || !Cells->IsAir({X, Y, Top - 3}))
                        continue;
                    int32 FloorZ = Top - 3;
                    while (FloorZ > Top - 8 && Cells->IsAir({X, Y, FloorZ - 1}))
                        --FloorZ;
                    if (!Cells->IsOpaqueCube({X, Y, FloorZ - 1})) continue;
                    const FVector Point(X + .5, Y + .5, FloorZ + .01);
                    double Best = TNumericLimits<double>::Max();
                    for (int32 Z = FloorZ; Z <= Top - 3; ++Z)
                        for (int32 DY = -6; DY <= 6; ++DY)
                            for (int32 DX = -6; DX <= 6; ++DX)
                            {
                                const FIntVector Eye(X + DX, Y + DY, Z);
                                const double Across = FMath::Sqrt(double(DX * DX + DY * DY));
                                const FVector EyePoint = FVector(Eye) + FVector(.5, .5, .6);
                                if (Across < 2.5 || Across > 6 || FMath::Abs(Across - 4) >= Best ||
                                    !Cells->IsAir(Eye) || !Cells->IsCovered(Eye) || EyePoint.Z < Point.Z + .3 ||
                                    !Cells->IsClear(EyePoint, Point))
                                    continue;
                                Best = FMath::Abs(Across - 4);
                                LeakCamera = EyePoint;
                            }
                    if (Best == TNumericLimits<double>::Max()) continue;
                    Patch = {TEXT("leak"), Point, FVector(0, 0, 1)};
                    LeakLight = FVector(X + .5, Y + .5, Top + 2);
                }
    if (!Patch.IsSet())
    {
        Line(TEXT("Leak test: no cave under three blocks of rock with a camera that sees its floor."));
        return;
    }
    Line(FString::Printf(TEXT("Leak test: light at %s over three opaque blocks, cave floor face at %s, camera at %s, "
                              "%.1f blocks from the light to the floor."),
        *LeakLight.ToCompactString(), *Patch.Point.ToCompactString(), *LeakCamera.ToCompactString(),
        FVector::Dist(LeakLight, Patch.Point)));
    const FRotator Rotation = (Patch.Point - LeakCamera).Rotation();
    const TArray<FVector> Light = {Cells->ToCm(LeakLight)};
    // The torch stands on the column's top, a block under where the probe light hangs.
    const FIntVector TorchCell = VoxelLightProbe::FCells::ToCell(LeakLight) - LightShotsUp;
    const TArray<FCase> LeakCases = bTorch
        ? TArray<FCase>{FCase{TEXT("floor"), EMode::MegaLights, 0, true},
              FCase{TEXT("torch-1"), EMode::MegaLights, 1, true, true}}
        : TArray<FCase>{FCase{TEXT("base"), EMode::None, 0, false}, FCase{TEXT("none-1"), EMode::None, 1, false},
              FCase{TEXT("vsm-1"), EMode::Vsm, 1, false}, FCase{TEXT("floor"), EMode::MegaLights, 0, true},
              FCase{TEXT("megalights-1"), EMode::MegaLights, 1, true}};
    if (bTorch)
        Line(FString::Printf(TEXT("Torch leak test: the torch on cell %s, the column's top."), *TorchCell.ToString()));
    for (const FCase& Case : LeakCases)
    {
        const FString Name = TEXT("leak-") + Case.Name;
        FStep& Step = AddStep(Name, 150,
            [this, &World, Case, Light, LeakCamera, Rotation, TorchCell]
            {
                SetView(LeakCamera, Rotation);
                if (!Case.bTorches)
                {
                    VoxelLightProbe::RemoveTorches();
                    ShowCase(World, Case, Light);
                    return;
                }
                SetMegaLights(Case.bMegaLights);
                VoxelLightProbe::Clear();
                TorchSites.Add(TorchCell);
                VoxelLightProbe::PlaceTorches(World, {TorchCell});
            });
        // With torches, every leak step waits for the columns it edits (leak-floor removes the spread torches).
        Step.bEdit = bTorch;
        Step.CaptureFrom = 120;
        Step.OnFrame = [this, Name, Patch](int32 Frame, const TArray<FColor>& Pixels)
        {
            Record(Name, Frame - 120, Patch, Measure(Patch, Pixels));
            if (Frame == 149) SavePng(TEXT("lights-") + Name, Pixels);
        };
        Step.End = [this, Name, Case]
        {
            NoteExposure(Name);
            const TArray<FVector4>& Values = Series.FindRef(Name + TEXT("/leak"));
            const FVector4 Mean = LightShotsMean(Values, 0, 30);
            Settles.Add(Name, Mean);
            const FString Zero = Case.bMegaLights ? TEXT("leak-floor") : TEXT("leak-base");
            Line(FString::Printf(TEXT("  %s: cave floor %.2f mean sRGB (std %.2f)%s"), *Name, Mean.W,
                LightShotsDeviation(Values, 0, 30),
                Case.Count ? *FString::Printf(TEXT(", %+.2f against %s"), Mean.W - Settles.FindRef(Zero).W, *Zero)
                           : TEXT("")));
            if (!Case.bTorches) return;
            // Gate: the torch adds at most 1/255 to the cave floor under three blocks of rock.
            if (!Settles.Contains(Zero))
            {
                LeakGate = FString::Printf(TEXT("Leak gate: FAIL (%s not measured)."), *Zero);
                return;
            }
            const double Added = Mean.W - Settles.FindRef(Zero).W;
            LeakGate = FString::Printf(TEXT("Leak gate: %s %+.2f/255 against %s, gate within 1/255: %s."), *Name, Added,
                *Zero, FMath::Abs(Added) <= 1 ? TEXT("PASS") : TEXT("FAIL"));
        };
    }
    if (bTorch) AddStep(TEXT("leak-restore"), 10, [] { VoxelLightProbe::RemoveTorches(); }).bEdit = true;
}

void FVoxelLightShots::AddEditSteps(AVoxelWorld& World)
{
    using VoxelLightProbe::EMode;
    // The cell to mine: a cube in view 3 to 10 blocks from the camera (a wall's before a floor's or a ceiling's), with
    // another cube behind it whose face the camera will see once it is mined. Its light hangs in the centre of the air
    // cell two out from the face, so the niche's back face is 2.5 blocks from it.
    const FIntVector Ahead = VoxelLightProbe::FCells::ToCell(ViewBlocks + ViewRotation.Vector() * 6);
    FIntVector Wall = FIntVector::ZeroValue;
    FVector LightPoint = FVector::ZeroVector;
    FPatch Patch;
    double Best = TNumericLimits<double>::Max();
    LightShotsForEachFace(*Cells, Ahead, 5,
        [&](const FIntVector& Cell, const FIntVector& Normal)
        {
            if (!Cells->IsOpaqueCube(Cell - Normal) || !Cells->IsAir(Cell + Normal * 2)) return;
            const FVector Facing(Normal);
            const FVector Back = FVector(Cell) + FVector(.5) - Facing * .49;
            const FVector Light = FVector(Cell + Normal * 2) + FVector(.5);
            const double Away = FVector::Dist(Back, ViewBlocks);
            FVector2D Pixel;
            double Depth = 0;
            if (Away < 3 || Away > 10 || FVector::DotProduct(Facing, ViewBlocks - Back) <= 0 ||
                !Project(ViewBlocks, ViewRotation, Back, Pixel, Depth) || !IsOnScreen(Pixel, 48) ||
                !Cells->IsClear(ViewBlocks, Back, &Cell) || !Cells->IsClear(Light, Back, &Cell))
                return;
            const double Score = (Normal.Z == 0 ? 0 : 100) + FMath::Abs(Away - 5);
            if (Score >= Best) return;
            Best = Score;
            Wall = Cell;
            LightPoint = Light;
            Patch = {TEXT("niche"), Back, Facing};
        });
    if (!Patch.IsSet())
    {
        Line(TEXT("Edit test: no cell next to the light whose niche the camera would see."));
        return;
    }
    const FFoundationBlockState Stone = World.GetAuthority()->World.Get(Wall).State;
    Line(FString::Printf(TEXT("Edit test: light at %s, mining %s; the niche's back face is %.2f blocks from the "
                              "light."),
        *LightPoint.ToCompactString(), *Wall.ToString(), FVector::Dist(LightPoint, Patch.Point)));
    const TArray<FVector> Light = {Cells->ToCm(LightPoint)};
    for (const FCase& Case : {FCase{TEXT("none"), EMode::None, 1, false}, FCase{TEXT("vsm"), EMode::Vsm, 1, false},
             FCase{TEXT("megalights"), EMode::MegaLights, 1, true}})
    {
        const FString Name = TEXT("edit-") + Case.Name;
        FStep& Lit = AddStep(Name + TEXT("-lit"), 150,
            [this, &World, Case, Light]
            {
                SetView(ViewBlocks, ViewRotation);
                ShowCase(World, Case, Light);
            });
        Lit.CaptureFrom = 140;
        Lit.OnFrame = [this, Name, Patch](int32 Frame, const TArray<FColor>& Pixels)
        { Record(Name + TEXT("-lit"), Frame - 140, Patch, Measure(Patch, Pixels)); };
        FStep& Mine = AddStep(Name, 150, [this, &World, Wall] { SetCell(World, Wall, FFoundationBlockState()); });
        Mine.bEdit = true;
        Mine.CaptureFrom = 0;
        Mine.OnFrame = [this, Name, Patch](int32 Frame, const TArray<FColor>& Pixels)
        {
            Record(Name, Frame, Patch, Measure(Patch, Pixels));
            if (Frame == 0 || Frame == 149) SavePng(FString::Printf(TEXT("lights-%s-f%03d"), *Name, Frame), Pixels);
        };
        Mine.End = [this, Name]
        {
            NoteExposure(Name);
            const TArray<FVector4>& Values = Series.FindRef(Name + TEXT("/niche"));
            const FVector4 Before = LightShotsMean(Series.FindRef(Name + TEXT("-lit/niche")), 0, 10);
            const FVector4 Settled = LightShotsMean(Values, 120, 150);
            Line(FString::Printf(
                TEXT("  %s: niche on screen %d frames after the edit; %.1f before (where the cube still stood), %.1f "
                     "at frame 0, settled %.1f; settled within 2/255 from frame %d"),
                *Name, EditFrames, Before.W, Values.IsValidIndex(0) ? Values[0].W : -1., Settled.W,
                LightShotsLag(Values, 120)));
        };
        AddStep(Name + TEXT("-restore"), 30, [this, &World, Wall, Stone] { SetCell(World, Wall, Stone); }).bEdit = true;
    }
}

void FVoxelLightShots::SetView(const FVector& Blocks, const FRotator& Rotation)
{
    CameraBlocks = Blocks;
    CameraRotation = Rotation;
    if (Camera.IsValid()) Camera->SetActorLocationAndRotation(Cells->ToCm(Blocks), Rotation);
}

void FVoxelLightShots::SetMegaLights(bool bOn)
{
    if (!MegaLightsVariable || MegaLightsVariable->GetInt() == int32(bOn)) return;
    MegaLightsVariable->Set(bOn ? 1 : 0, ECVF_SetByConsole);
    if (MegaLightsVariable->GetInt() != int32(bOn))
        Line(FString::Printf(
            TEXT("MegaLights did not switch to %d (still %d)."), int32(bOn), MegaLightsVariable->GetInt()));
}

void FVoxelLightShots::ShowCase(AVoxelWorld& World, const FCase& Case, const TArray<FVector>& Positions)
{
    SetMegaLights(Case.bMegaLights);
    VoxelLightProbe::FSettings Settings;
    Settings.Mode = Case.Mode;
    Settings.IntensityCd = IntensityCd;
    VoxelLightProbe::Show(
        World, TArray<FVector>(Positions.GetData(), FMath::Min(Case.Count, Positions.Num())), Settings);
}

void FVoxelLightShots::SetCell(AVoxelWorld& World, const FIntVector& Cell, FFoundationBlockState State)
{
    FFoundationBlockWorld& Blocks = World.GetAuthority()->World;
    if (!Blocks.IsKnown(Cell)) return;
    const FFoundationBlockState Before = Blocks.Get(Cell).State;
    if (!Originals.Contains(Cell)) Originals.Add(Cell, Before);
    if (Before != State) Blocks.Set(Cell, State);
}

bool FVoxelLightShots::Project(
    const FVector& From, const FRotator& Rotation, const FVector& Blocks, FVector2D& OutPixel, double& OutDepth) const
{
    // The camera's frame: X forward, Y right, Z up; the field of view is horizontal.
    const FVector Local = Rotation.UnrotateVector(Blocks - From);
    if (Local.X <= .05) return false;
    const double Focal = LightShotsFocal(Width);
    OutPixel = FVector2D(Width * .5 + Local.Y / Local.X * Focal, Height * .5 - Local.Z / Local.X * Focal);
    OutDepth = Local.X;
    return true;
}

int32 FVoxelLightShots::CountInView(TConstArrayView<FVector> Positions) const
{
    // Spheres of the lights' radius against the view frustum's side planes and the camera plane.
    const double Reach = VoxelLightProbe::FSettings().RadiusCm / 100;
    const double Across = FMath::DegreesToRadians(LightShotsFieldOfView / 2);
    const double Up = FMath::Atan(FMath::Tan(Across) * Height / FMath::Max(Width, 1));
    int32 InView = 0;
    for (const FVector& Position : Positions)
    {
        const FVector Local = ViewRotation.UnrotateVector(Cells->ToBlocks(Position) - ViewBlocks);
        const double Side = FMath::Abs(Local.Y) * FMath::Cos(Across) - Local.X * FMath::Sin(Across);
        const double Height2 = FMath::Abs(Local.Z) * FMath::Cos(Up) - Local.X * FMath::Sin(Up);
        InView += Local.X > -Reach && Side < Reach && Height2 < Reach;
    }
    return InView;
}

bool FVoxelLightShots::IsOnScreen(const FVector2D& Pixel, double Margin) const
{
    return Pixel.X >= Margin && Pixel.Y >= Margin && Pixel.X < Width - Margin && Pixel.Y < Height - Margin;
}

FVector4 FVoxelLightShots::Measure(const FPatch& Patch, const TArray<FColor>& Pixels) const
{
    FVector2D Pixel;
    double Depth = 0;
    if (Pixels.Num() != Width * Height || !Project(CameraBlocks, CameraRotation, Patch.Point, Pixel, Depth))
        return FVector4(0, 0, 0, -1);
    // A square about two fifths of a block across, narrowed by how obliquely the face is seen.
    const double Facing = FMath::Abs(FVector::DotProduct((Patch.Point - CameraBlocks).GetSafeNormal(), Patch.Normal));
    const int32 Half = FMath::Clamp(FMath::FloorToInt32(.2 * LightShotsFocal(Width) / Depth * Facing), 2, 12);
    const int32 Left = FMath::RoundToInt32(Pixel.X) - Half, Top = FMath::RoundToInt32(Pixel.Y) - Half;
    if (Left < 0 || Top < 0 || Left + 2 * Half >= Width || Top + 2 * Half >= Height) return FVector4(0, 0, 0, -1);
    double R = 0, G = 0, B = 0;
    for (int32 Y = Top; Y <= Top + 2 * Half; ++Y)
        for (int32 X = Left; X <= Left + 2 * Half; ++X)
        {
            const FColor& Color = Pixels[Y * Width + X];
            R += Color.R;
            G += Color.G;
            B += Color.B;
        }
    const double Count = FMath::Square(2. * Half + 1);
    return FVector4(R / Count, G / Count, B / Count, (R + G + B) / (3 * Count));
}

void FVoxelLightShots::Record(const FString& Step, int32 Frame, const FPatch& Patch, const FVector4& Value)
{
    FSeries& Values = Series.FindOrAdd(Step + TEXT("/") + Patch.Name);
    while (Values.Num() <= Frame)
        Values.Add(FVector4(0, 0, 0, -1));
    Values[Frame] = Value;
    Csv.Add(FString::Printf(
        TEXT("%s,%d,%s,%.2f,%.2f,%.2f,%.2f"), *Step, Frame, *Patch.Name, Value.X, Value.Y, Value.Z, Value.W));
}

void FVoxelLightShots::SavePng(const FString& Name, const TArray<FColor>& Pixels) const
{
    IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
    if (!Png || Pixels.Num() != Width * Height ||
        !Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
        return;
    FFileHelper::SaveArrayToFile(Png->GetCompressed(), *FPaths::Combine(Folder, Name + TEXT(".png")));
}

float FVoxelLightShots::GetExposure() const
{
    return Exposure ? Exposure->Value : 0.f;
}

void FVoxelLightShots::NoteExposure(const FString& Step)
{
    Exposures.Add(TPair<FString, float>(Step, GetExposure()));
}

void FVoxelLightShots::Line(const FString& Text)
{
    Report.Add(Text);
    UE_LOG(LogUnnamedVoxel, Display, TEXT("LightShots: %s"), *Text);
}

void FVoxelLightShots::Finish(AVoxelWorld& World)
{
    bFinished = true;
    VoxelLightProbe::Clear();
    SetMegaLights(MegaLightsAtStart != 0);
    if (ScreenshotHandle.IsValid()) UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
    ScreenshotHandle.Reset();
    if (APlayerController* Controller = UGameplayStatics::GetPlayerController(&World, 0))
        Controller->SetViewTarget(Controller->GetPawn());
    if (Camera.IsValid()) Camera->Destroy();
    Exposure.Reset();

    // GPU frame per case and round, and the means; the deltas are against the base (no lights, MegaLights off).
    auto Mean = [](const TArray<FTimes>& Rounds, double FTimes::* Field)
    {
        double Sum = 0;
        int32 Count = 0;
        for (const FTimes& Round : Rounds)
            if (Round.Frames)
            {
                Sum += Round.*Field / Round.Frames;
                ++Count;
            }
        return Count ? Sum / Count : 0.;
    };
    if (const TArray<FTimes>* Before = CaseTimes.Find(TEXT("cube-before")))
    {
        Line(FString::Printf(
            TEXT("Glowing cubes, GPU frame ms (mean of 120 frames): before %.2f"), Mean(*Before, &FTimes::GpuMs)));
        const double Was = Mean(*Before, &FTimes::GpuMs);
        for (const TPair<FString, TArray<FTimes>>& Case : CaseTimes)
            if (Case.Key.StartsWith(TEXT("cube-")) && Case.Key != TEXT("cube-before"))
            {
                const double On = Mean(Case.Value, &FTimes::GpuMs);
                const double Off = Mean(CaseTimes.FindRef(TEXT("cubeoff-") + Case.Key.Mid(5)), &FTimes::GpuMs);
                Line(FString::Printf(TEXT("  %s: %.2f (%+.2f); removed again %.2f (%+.2f)"), *Case.Key.Mid(5), On,
                    On - Was, Off, Off - Was));
            }
    }
    if (!Cases.IsEmpty())
    {
        Line(FString::Printf(TEXT("Local lights at %d x %d, GPU frame ms, mean of 120 frames per round (round 2 ran "
                                  "in reverse order):"),
            Width, Height));
        Line(TEXT("| Case | Shown | Round 1 | Round 2 | Round 3 | Mean | vs base | Game ms | Frame ms | "
                  "Image vs N = 0 (mean luma diff, % pixels > 8) | Noise std (mean, p99, lit pixels) |"));
        Line(TEXT("|---|---|---|---|---|---|---|---|---|---|---|"));
        const double Base = Mean(CaseTimes.FindRef(TEXT("base")), &FTimes::GpuMs);
        for (const FCase& Case : Cases)
        {
            const TArray<FTimes>& Rounds = CaseTimes.FindRef(Case.Name);
            FString Cells3;
            for (int32 Round = 0; Round < 3; ++Round)
                Cells3 += Rounds.IsValidIndex(Round) && Rounds[Round].Frames
                    ? FString::Printf(TEXT(" %.2f |"), Rounds[Round].GpuMs / Rounds[Round].Frames)
                    : FString(TEXT(" - |"));
            const FVector2D* Difference = Differences.Find(Case.Name);
            const FVector* Noise = Noises.Find(Case.Name);
            Line(FString::Printf(TEXT("| %s | %d |%s %.2f | %+.2f | %.2f | %.2f | %s | %s |"), *Case.Name,
                Shown.FindRef(Case.Name), *Cells3, Mean(Rounds, &FTimes::GpuMs), Mean(Rounds, &FTimes::GpuMs) - Base,
                Mean(Rounds, &FTimes::GameMs), Mean(Rounds, &FTimes::FrameMs),
                Difference ? *FString::Printf(TEXT("%.2f, %.1f%%"), Difference->X, Difference->Y) : TEXT("-"),
                Noise ? *FString::Printf(TEXT("%.2f, %.2f, %.2f"), Noise->X, Noise->Y, Noise->Z) : TEXT("-")));
        }
    }
    if (bTorchGates) AddTorchTables(World);
    if (bTorchLookPairs)
    {
        Line(
            TEXT("Look pairs (luma 0.2126 R + 0.7152 G + 0.0722 B of the saved sRGB pixels, 0-255; saturated: luma 250 "
                 "or more; black: luma 5 or less; glow: the level the torch models are meshed with, voxel.TorchGlow at "
                 "the world's build; intensity in cd with falloff 0 (1/d^2), brightness unitless above 0):"));
        Line(
            TEXT("| Shot | MegaLights | Mean luma | Saturated % | Black % | Glow | Intensity | Temperature | Falloff | "
                 "Brightness | Exposure |"));
        Line(TEXT("|---|---|---|---|---|---|---|---|---|---|---|"));
        for (const FString& Row : LookRows)
            Line(Row);
        FFoundationBlockWorld& LookBlocks = World.GetAuthority()->World;
        const FFoundationBlockType* LookTorch = LookBlocks.FindBlock(TEXT("torch"));
        int32 Standing = 0, Unknown = 0;
        for (const FIntVector& Cell : TorchSites)
            if (!LookBlocks.IsKnown(Cell))
                ++Unknown;
            else if (LookTorch && LookBlocks.Get(Cell).State == LookTorch->GetDefaultState())
                ++Standing;
        Line(FString::Printf(
            TEXT("Torch cells: %d used, %d still a torch, %d unloaded."), TorchSites.Num(), Standing, Unknown));
    }
    if (bTorchRoom)
    {
        RestoreRoomVariables();
        AddTorchRoomTables();
    }
    float Low = TNumericLimits<float>::Max(), High = 0;
    for (const TPair<FString, float>& Value : Exposures)
    {
        Low = FMath::Min(Low, Value.Value);
        High = FMath::Max(High, Value.Value);
    }
    if (!Exposures.IsEmpty())
        Line(FString::Printf(TEXT("Exposure over the %d measured steps: %.6g to %.6g (%s)."), Exposures.Num(), Low,
            High, Low == High ? TEXT("identical") : TEXT("NOT identical")));
    // SetCell skips a cell whose column is unloaded, so the cleanup cannot put those back: count them.
    int32 Unloaded = 0;
    for (const TPair<FIntVector, FFoundationBlockState>& Original : Originals)
        if (!World.GetAuthority()->World.IsKnown(Original.Key))
            ++Unloaded;
        else if (World.GetAuthority()->World.Get(Original.Key).State != Original.Value)
            Line(FString::Printf(TEXT("Cell %s was not restored."), *Original.Key.ToString()));
    Line(FString::Printf(TEXT("Restored %d edited cells (%d not restored: column unloaded); MegaLights for the "
                              "project back to %d."),
        Originals.Num() - Unloaded, Unloaded, MegaLightsVariable ? MegaLightsVariable->GetInt() : -1));
    const FString ReportName = bTorchLookPairs ? TEXT("look")
        : bTorchRoom                           ? TEXT("room")
        : bTorchGates                          ? TEXT("torches")
                                               : TEXT("lights");
    FFileHelper::SaveStringToFile(
        FString::Join(Report, TEXT("\n")) + TEXT("\n"), *FPaths::Combine(Folder, ReportName + TEXT(".txt")));
    FFileHelper::SaveStringToFile(TEXT("step,frame,patch,r,g,b,mean\n") + FString::Join(Csv, TEXT("\n")) + TEXT("\n"),
        *FPaths::Combine(Folder, ReportName + TEXT(".csv")));
}

namespace
{
    // Mean luma difference of two images and the % of pixels changed by more than 8/255.
    FVector2D LightShotsTorchDifference(const TArray<FColor>& Pixels, const TArray<FColor>& Zero)
    {
        if (Zero.Num() != Pixels.Num() || Pixels.IsEmpty()) return FVector2D(-1, -1);
        double Sum = 0;
        int32 Changed = 0;
        for (int32 Index = 0; Index < Pixels.Num(); ++Index)
        {
            const FColor& A = Pixels[Index];
            const FColor& B = Zero[Index];
            Sum += FMath::Abs(LightShotsLuma(A) - LightShotsLuma(B));
            Changed += FMath::Max3(FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G), FMath::Abs(A.B - B.B)) > 8;
        }
        return FVector2D(Sum / Pixels.Num(), 100. * Changed / Pixels.Num());
    }

    // Mean, p95 (nearest rank) and max of edit costs, as table cells.
    FString LightShotsTorchCostCells(TArray<double> Values)
    {
        if (Values.IsEmpty()) return TEXT("- | - | -");
        Values.Sort();
        double Sum = 0;
        for (const double Value : Values)
            Sum += Value;
        const int32 Rank = FMath::Clamp(FMath::CeilToInt32(.95 * Values.Num()) - 1, 0, Values.Num() - 1);
        return FString::Printf(TEXT("%.3f | %.3f | %.3f"), Sum / Values.Num(), Values[Rank], Values.Last());
    }
}

void FVoxelLightShots::AddTorchGateSteps(AVoxelWorld& World)
{
    using VoxelLightProbe::EMode;
    // The probe's floor cells: torches-N stands on the first N, megalights-N hangs its lights 1 m over the same ones.
    const FVector EyeCm = Cells->ToCm(ViewBlocks);
    TorchCells = VoxelLightProbe::FindFloorLightCells(*Cells, EyeCm, ViewRotation.Vector(), MaxLights);
    LightPositions = VoxelLightProbe::FindFloorLights(*Cells, EyeCm, ViewRotation.Vector(), MaxLights);
    SpreadCells = FindSpreadTorchCells(64, 48);
    TArray<double> Spread, Clustered;
    for (const FIntVector& Cell : SpreadCells)
        Spread.Add(FVector::Dist(FVector(Cell) + FVector(.5, .5, .5), ViewBlocks));
    for (int32 Index = 0; Index < FMath::Min(64, TorchCells.Num()); ++Index)
        Clustered.Add(FVector::Dist(FVector(TorchCells[Index]) + FVector(.5, .5, .5), ViewBlocks));
    Spread.Sort();
    Clustered.Sort();
    auto Quartiles = [](const TArray<double>& Values)
    {
        if (Values.IsEmpty()) return FString(TEXT("none"));
        const int32 Last = Values.Num() - 1;
        return FString::Printf(TEXT("%.1f / %.1f / %.1f / %.1f / %.1f"), Values[0], Values[Last / 4], Values[Last / 2],
            Values[Last * 3 / 4], Values[Last]);
    };
    Line(FString::Printf(TEXT("Torch gates: %d probe floor cells for torches and probe lights (of %d); %d spread floor "
                              "cells (of 64, toward 48 blocks)."),
        TorchCells.Num(), MaxLights, SpreadCells.Num()));
    Line(FString::Printf(TEXT("Distance from the eye (blocks; min / quartiles / max): clustered 64 %s; spread 64 %s."),
        *Quartiles(Clustered), *Quartiles(Spread)));

    AddTorchLookSteps(World);
    // The probe cave: the eye adapts once with MegaLights off, then stays as it is for every case (as T1).
    AddStep(TEXT("settle"), 150,
        [this]
        {
            SetView(ViewBlocks, ViewRotation);
            SetMegaLights(false);
            VoxelLightProbe::Clear();
            VoxelLightProbe::RemoveTorches();
            SetExposureSpeed(50);
        })
        .bEdit = true;
    AddStep(TEXT("freeze"), 30, [this] { SetExposureSpeed(0); }).End = [this]
    { Line(FString::Printf(TEXT("Exposure frozen at %.6g."), GetExposure())); };
    // (e) The probe cave with 10 torches, MegaLights off, then on.
    for (const bool bOn : {false, true})
    {
        const FString Shot = FString::Printf(TEXT("torches-look-probe-10-megalights%d"), int32(bOn));
        const FCase Case{TEXT("torches-10"), EMode::MegaLights, 10, bOn, true};
        FStep& Look = AddStep(Shot, 91, [this, &World, Case] { ShowTorchCase(World, Case, LightPositions); });
        Look.bEdit = true;
        Look.CaptureFrom = 90;
        Look.OnFrame = [this, Shot](int32 Frame, const TArray<FColor>& Pixels) { SavePng(Shot, Pixels); };
    }
    AddTorchEditSteps(World);

    // (b) and (c): T1's cases with the pool's torches beside the probe's lights, in three rounds, the middle one in
    // reverse order. Every case waits for its edits' columns before settling.
    Cases = {{TEXT("base"), EMode::None, 0, false}, {TEXT("floor"), EMode::MegaLights, 0, true}};
    for (const int32 Count : {1, 10, 100})
        Cases.Add({FString::Printf(TEXT("torches-%d"), Count), EMode::MegaLights, Count, true, true});
    for (const int32 Count : {1, 10, 100})
        Cases.Add({FString::Printf(TEXT("megalights-%d"), Count), EMode::MegaLights, Count, true});
    Cases.Add({TEXT("torches-64-clustered"), EMode::MegaLights, 64, true, true});
    Cases.Add({TEXT("torches-64-spread"), EMode::MegaLights, 64, true, true, true});
    for (int32 Round = 0; Round < 3; ++Round)
        for (int32 Order = 0; Order < Cases.Num(); ++Order)
        {
            const FCase Case = Cases[Round == 1 ? Cases.Num() - 1 - Order : Order];
            AddStep(FString::Printf(TEXT("%s-r%d-settle"), *Case.Name, Round + 1), 90,
                [this, &World, Case]
                {
                    SetView(ViewBlocks, ViewRotation);
                    ShowTorchCase(World, Case, LightPositions);
                })
                .bEdit = true;
            FStep& Timed = AddStep(FString::Printf(TEXT("%s-r%d"), *Case.Name, Round + 1), 120 + TimedSkip);
            Timed.bTimed = true;
            Timed.End = [this, &World, Case, Round]
            {
                CaseTimes.FindOrAdd(Case.Name).Add(Times);
                if (Round > 0) return;
                // One GPU profile per case, in the log after this marker (Scripts/SummarizeLightProbe.ps1).
                UE_LOG(LogUnnamedVoxel, Display,
                    TEXT("LightShots ProfileGPU %s: %d lights shown, %d in view, MegaLights %d"), *Case.Name,
                    CountAllShown(World), CountInView(GetCasePositions(Case)), MegaLightsVariable->GetInt());
                GEngine->Exec(Camera->GetWorld(), TEXT("ProfileGPU"));
            };
            if (Round > 0) continue;
            FStep& Look = AddStep(Case.Name + TEXT("-look"), 11);
            Look.CaptureFrom = 10;
            Look.OnFrame = [this, Case](int32 Frame, const TArray<FColor>& Pixels)
            {
                SavePng(TEXT("torches-probe-") + Case.Name, Pixels);
                if (Case.Count == 0)
                {
                    Images.Add(Case.Name, Pixels);
                    if (Case.Name == TEXT("base")) return;
                }
                // Against the N = 0 image with MegaLights on (the floor's against the base).
                const FVector2D Difference =
                    LightShotsTorchDifference(Pixels, Images.FindRef(Case.Count > 0 ? TEXT("floor") : TEXT("base")));
                if (Difference.X >= 0) Differences.Add(Case.Name, Difference);
            };
            Look.End = [this, &World, Case]
            {
                NoteExposure(Case.Name);
                Shown.Add(Case.Name, CountAllShown(World));
            };
        }
    // (d) The leak site with a torch on the column's top.
    AddLeakSteps(World, true);
    // Last, so a stopped run still gets here: every torch and the MegaLights setting back.
    AddStep(TEXT("cleanup"), 10,
        [this]
        {
            VoxelLightProbe::Clear();
            VoxelLightProbe::RemoveTorches();
            SetMegaLights(MegaLightsAtStart != 0);
        });
}

void FVoxelLightShots::AddTorchLookSteps(AVoxelWorld& World)
{
    // (e) The -Caves views without torches, MegaLights off, then on: the eye adapts to each view with MegaLights off,
    // then stays for the pair.
    int32 Views = 0;
    for (const TTuple<FString, FVector, FVector>& View : World.GetCaveShotViews())
    {
        const FString Name = View.Get<0>();
        if (Name != TEXT("cave") && Name != TEXT("cave-lava")) continue;
        ++Views;
        const FVector From = Cells->ToBlocks(View.Get<1>());
        const FRotator Rotation = (View.Get<2>() - View.Get<1>()).Rotation();
        AddStep(Name + TEXT("-adapt"), 150,
            [this, From, Rotation]
            {
                SetView(From, Rotation);
                SetMegaLights(false);
                VoxelLightProbe::RemoveTorches();
                SetExposureSpeed(50);
            });
        for (const bool bOn : {false, true})
        {
            const FString Shot = FString::Printf(TEXT("torches-look-%s-megalights%d"), *Name, int32(bOn));
            FStep& Look = AddStep(Shot, 91,
                [this, bOn]
                {
                    SetExposureSpeed(0);
                    SetMegaLights(bOn);
                });
            Look.CaptureFrom = 90;
            Look.OnFrame = [this, Shot](int32 Frame, const TArray<FColor>& Pixels) { SavePng(Shot, Pixels); };
        }
    }
    Line(FString::Printf(TEXT("Look pairs: %d of the -Caves views (cave, cave-lava) in the streamed area."), Views));
}

void FVoxelLightShots::AddTorchEditSteps(AVoxelWorld& World)
{
    // (a) One torch on the probe floor cell nearest the eye in front of it, placed and taken back 20 times with
    // MegaLights on: the game-thread ms of the pool's updates from the edit until its column is back on screen.
    FIntVector Near = FIntVector::ZeroValue;
    double Nearest = TNumericLimits<double>::Max();
    for (const FIntVector& Cell : TorchCells)
    {
        const FVector Center = FVector(Cell) + FVector(.5, .5, .5);
        const double Distance = FVector::Dist(Center, ViewBlocks);
        if (ViewRotation.UnrotateVector(Center - ViewBlocks).X > 2 && Distance < Nearest)
        {
            Nearest = Distance;
            Near = Cell;
        }
    }
    if (Nearest == TNumericLimits<double>::Max())
    {
        Line(TEXT("Edit cost: no probe floor cell in front of the camera."));
        return;
    }
    Line(FString::Printf(TEXT("Edit cost: one torch on cell %s, %.1f blocks from the eye, placed and taken back 20 "
                              "times with MegaLights on."),
        *Near.ToString(), Nearest));
    // The pool off first, so it holds no component and the first placement makes one.
    AddStep(TEXT("edit-off"), 10,
        [this]
        {
            SetMegaLights(false);
            VoxelLightProbe::Clear();
            VoxelLightProbe::RemoveTorches();
        })
        .bEdit = true;
    AddStep(TEXT("edit-live"), 60, [this] { SetMegaLights(true); }).End = [this, &World]
    {
        Line(FString::Printf(TEXT("Edit cost: the pool live with %d components before the first placement."),
            World.GetLights() ? World.GetLights()->GetComponentCount() : -1));
    };
    for (int32 Index = 0; Index < 20; ++Index)
        for (const bool bPlace : {true, false})
        {
            FStep& Step =
                AddStep(FString::Printf(TEXT("edit-%s-%d"), bPlace ? TEXT("place") : TEXT("remove"), Index + 1), 10,
                    [this, &World, Near, bPlace]
                    {
                        const FVoxelLights* Pool = World.GetLights();
                        EditCost = FEditCost();
                        EditUpdates = Pool ? Pool->GetUpdateCount() : 0;
                        EditComponents = Pool ? Pool->GetComponentCount() : 0;
                        if (!bPlace)
                        {
                            VoxelLightProbe::RemoveTorches();
                            return;
                        }
                        TorchSites.Add(Near);
                        VoxelLightProbe::PlaceTorches(World, {Near});
                    });
            Step.bEdit = true;
            Step.OnTick = [this, &World]
            {
                const FVoxelLights* Pool = World.GetLights();
                if (!Pool || Pool->GetUpdateCount() == EditUpdates) return;
                EditUpdates = Pool->GetUpdateCount();
                EditCost.Ms += Pool->GetLastMs();
                ++EditCost.Updates;
            };
            Step.End = [this, &World, bPlace]
            {
                const FVoxelLights* Pool = World.GetLights();
                EditCost.Created = (Pool ? Pool->GetComponentCount() : 0) - EditComponents;
                (bPlace ? PlaceCosts : RemoveCosts).Add(EditCost);
            };
        }
}

TArray<FIntVector> FVoxelLightShots::FindSpreadTorchCells(int32 Count, double Radius) const
{
    // Every covered floor cell (air on a solid cell, air above, under three blocks or more) within Radius of the eye,
    // then the farthest from the eye and from those picked before, in turn.
    const FIntVector Eye = VoxelLightProbe::FCells::ToCell(ViewBlocks);
    const int32 Reach = FMath::FloorToInt32(Radius);
    TArray<FVector> Floors;
    for (int32 Y = Eye.Y - Reach; Y <= Eye.Y + Reach; ++Y)
        for (int32 X = Eye.X - Reach; X <= Eye.X + Reach; ++X)
            for (int32 Z = FMath::Max(Eye.Z - Reach, VoxelConstants::MinZ + 1),
                       Top = FMath::Min(Eye.Z + Reach, Cells->GetSurface(X, Y) - 4);
                Z <= Top; ++Z)
            {
                const FIntVector Cell(X, Y, Z);
                const FVector Center = FVector(Cell) + FVector(.5, .5, .5);
                if (FVector::DistSquared(Center, ViewBlocks) <= Radius * Radius && Cells->IsAir(Cell) &&
                    Cells->IsSolid(Cell - LightShotsUp) && Cells->IsAir(Cell + LightShotsUp) && Cells->IsCovered(Cell))
                    Floors.Add(Center);
            }
    TArray<FIntVector> Picked;
    TArray<double> Gaps;
    Gaps.Init(TNumericLimits<double>::Max(), Floors.Num());
    FVector Last = ViewBlocks;
    while (Picked.Num() < FMath::Min(Count, Floors.Num()))
    {
        int32 Best = 0;
        for (int32 Index = 0; Index < Floors.Num(); ++Index)
        {
            Gaps[Index] = FMath::Min(Gaps[Index], FVector::DistSquared(Floors[Index], Last));
            if (Gaps[Index] > Gaps[Best]) Best = Index;
        }
        Last = Floors[Best];
        Gaps[Best] = 0;
        Picked.Add(VoxelLightProbe::FCells::ToCell(Last));
    }
    return Picked;
}

void FVoxelLightShots::ShowTorchCase(AVoxelWorld& World, const FCase& Case, const TArray<FVector>& Positions)
{
    if (!Case.bTorches)
    {
        VoxelLightProbe::RemoveTorches();
        ShowCase(World, Case, Positions);
        return;
    }
    SetMegaLights(Case.bMegaLights);
    VoxelLightProbe::Clear();
    const TArray<FIntVector>& Floor = Case.bSpread ? SpreadCells : TorchCells;
    const TArray<FIntVector> Placing(Floor.GetData(), FMath::Min(Case.Count, Floor.Num()));
    TorchSites.Append(Placing);
    const int32 Placed = VoxelLightProbe::PlaceTorches(World, Placing);
    if (Placed < Case.Count)
        Line(FString::Printf(TEXT("%s: %d of %d torches placed."), *Case.Name, Placed, Case.Count));
}

int32 FVoxelLightShots::CountAllShown(const AVoxelWorld& World) const
{
    const FVoxelLights* Pool = World.GetLights();
    return VoxelLightProbe::CountShown() + (Pool && Pool->IsLive() ? Pool->GetPicks().Num() : 0);
}

TArray<FVector> FVoxelLightShots::GetCasePositions(const FCase& Case) const
{
    if (!Case.bTorches) return TArray<FVector>(LightPositions.GetData(), FMath::Min(Case.Count, LightPositions.Num()));
    const TArray<FIntVector>& Floor = Case.bSpread ? SpreadCells : TorchCells;
    TArray<FVector> Positions;
    for (int32 Index = 0; Index < FMath::Min(Case.Count, Floor.Num()); ++Index)
        Positions.Add(Cells->ToCm(FVector(Floor[Index]) + FVector(.5, .5, .5)));
    return Positions;
}

void FVoxelLightShots::SetExposureSpeed(float Speed)
{
    if (!Camera.IsValid()) return;
    FPostProcessSettings& Post = Camera->GetCameraComponent()->PostProcessSettings;
    Post.AutoExposureSpeedUp = Post.AutoExposureSpeedDown = Speed;
}

void FVoxelLightShots::AddTorchTables(AVoxelWorld& World)
{
    auto MeanGpu = [this](const FString& Name)
    {
        double Sum = 0;
        int32 Rounds = 0;
        for (const FTimes& Round : CaseTimes.FindRef(Name))
            if (Round.Frames)
            {
                Sum += Round.GpuMs / Round.Frames;
                ++Rounds;
            }
        return Rounds ? Sum / Rounds : -1.;
    };
    // (b): torches-N against the probe's megalights-N, the same cells and run.
    for (const int32 Count : {1, 10, 100})
    {
        const FString Torches = FString::Printf(TEXT("torches-%d"), Count);
        const FString Probe = FString::Printf(TEXT("megalights-%d"), Count);
        const double Delta = MeanGpu(Torches) - MeanGpu(Probe);
        // Only a measured pair showing as many lights compares (a stopped run, or the pool's cap below 100, fails).
        const bool bMeasured = MeanGpu(Torches) >= 0 && MeanGpu(Probe) >= 0;
        const bool bComparable = Shown.FindRef(Torches) == Shown.FindRef(Probe);
        const TCHAR* Verdict = !bMeasured ? TEXT("FAIL (not measured)")
            : !bComparable                ? TEXT("FAIL (not comparable: the lights shown differ)")
            : Delta <= .3                 ? TEXT("PASS")
                                          : TEXT("FAIL");
        Line(FString::Printf(TEXT("Gate %s against %s: %.2f against %.2f ms, %+.2f ms, within 0.3 ms: %s (%d against "
                                  "%d lights shown)."),
            *Torches, *Probe, MeanGpu(Torches), MeanGpu(Probe), Delta, Verdict, Shown.FindRef(Torches),
            Shown.FindRef(Probe)));
    }
    // (c): spread against clustered.
    Line(FString::Printf(TEXT("Spread against clustered (64 torches): %.2f against %.2f ms, %+.2f ms (%d against %d "
                              "lights shown)."),
        MeanGpu(TEXT("torches-64-spread")), MeanGpu(TEXT("torches-64-clustered")),
        MeanGpu(TEXT("torches-64-spread")) - MeanGpu(TEXT("torches-64-clustered")),
        Shown.FindRef(TEXT("torches-64-spread")), Shown.FindRef(TEXT("torches-64-clustered"))));
    // (d)
    Line(LeakGate.IsEmpty() ? FString(TEXT("Leak gate: not measured (no leak site).")) : LeakGate);
    // (a): the first placement makes the pool's component; the others reuse it.
    auto Row = [this](const FString& Name, TConstArrayView<FEditCost> Edits)
    {
        TArray<double> Values;
        int32 Updates = 0, Created = 0, Missed = 0;
        for (const FEditCost& Edit : Edits)
        {
            Updates += Edit.Updates;
            Created += Edit.Created;
            Missed += Edit.Updates == 0;
            if (Edit.Updates) Values.Add(Edit.Ms);
        }
        Line(FString::Printf(TEXT("| %s | %d | %s | %d | %d | %d |"), *Name, Values.Num(),
            *LightShotsTorchCostCells(Values), Updates, Missed, Created));
    };
    Line(TEXT("Edit cost: the pool's game-thread ms per torch edit (its updates from the edit until the column is back "
              "on screen, summed):"));
    Line(TEXT("| Edits | n | Mean ms | p95 ms | Max ms | Updates | Edits without one | Components made |"));
    Line(TEXT("|---|---|---|---|---|---|---|---|"));
    if (!PlaceCosts.IsEmpty())
    {
        Row(TEXT("place, first"), TConstArrayView<FEditCost>(PlaceCosts).Left(1));
        Row(TEXT("place, rest"), TConstArrayView<FEditCost>(PlaceCosts).RightChop(1));
    }
    Row(TEXT("remove"), RemoveCosts);
    // Every cell a torch stood on is back.
    FFoundationBlockWorld& Blocks = World.GetAuthority()->World;
    const FFoundationBlockType* Torch = Blocks.FindBlock(TEXT("torch"));
    int32 Standing = 0, Unknown = 0;
    for (const FIntVector& Cell : TorchSites)
        if (!Blocks.IsKnown(Cell))
            ++Unknown;
        else if (Torch && Blocks.Get(Cell).State == Torch->GetDefaultState())
            ++Standing;
    Line(FString::Printf(TEXT("Torch cells: %d used, %d still a torch, %d unloaded; the pool has %d components."),
        TorchSites.Num(), Standing, Unknown, World.GetLights() ? World.GetLights()->GetComponentCount() : -1));
}

namespace
{
    // -VoxelShotTorchRoom: the room's interior is TorchRoomSize x TorchRoomHeight x TorchRoomSize air cells in a stone
    // shell one block thick, and its ceiling's shell cells are TorchRoomDepth blocks under the lowest surface over it.
    constexpr int32 TorchRoomSize = 9;
    constexpr int32 TorchRoomHeight = 4;
    constexpr int32 TorchRoomDepth = 31;
    constexpr double TorchRoomEyeHeight = 1.62;
    constexpr double TorchRoomPitch = -15;

    // A cell from interior coordinates (1 to TorchRoomSize across, 1 to TorchRoomHeight up; 0 and one past the last
    // are the shell). Corner is interior (1, 1, 1).
    FIntVector TorchRoomCell(const FIntVector& Corner, int32 X, int32 Y, int32 Z = 1)
    {
        return Corner + FIntVector(X - 1, Y - 1, Z - 1);
    }

    struct FTorchRoomShot
    {
        FString Name;
        bool bMegaLights = false;
        // 1 or 4 torches, or 0 for the ceiling's glowstone.
        int32 Torches = 0;
        float Candelas = 1000;
        float Kelvin = 3000;
        // voxel.LightFalloff: 0 for inverse squared in candelas; above 0 that exponent, with the brightness the
        // room's search found for it (RoomBrightness).
        float Falloff = 0;
    };

    // The pool's falloff exponents the room shows beside inverse squared, and the brightness searches' steps each.
    constexpr float TorchRoomFalloffs[] = {2, 8};
    constexpr int32 TorchRoomSearchSteps = 4;
    // The one-torch inverse-squared shot whose floor-1 patch the searches aim at.
    const TCHAR* const TorchRoomReferenceShot = TEXT("room-ml1-torch1-1000cd");

    // A mean sRGB value (0-255) as linear light (0-1), near enough for a patch's mean.
    double TorchRoomLinear(double Srgb)
    {
        const double Value = FMath::Clamp(Srgb / 255, 0., 1.);
        return Value <= .04045 ? Value / 12.92 : FMath::Pow((Value + .055) / 1.055, 2.4);
    }

    // Vanilla's block light at an air cell of the room: the brightest source's level less its taxicab distance (the
    // room is an open box, so light reaches every cell along such a path).
    int32 TorchRoomVanillaLevel(const FIntVector& Cell, const TArray<FIntVector>& LightCells, int32 Level)
    {
        int32 Best = 0;
        for (const FIntVector& Source : LightCells)
        {
            const FIntVector Offset = Cell - Source;
            Best = FMath::Max(Best, Level - (FMath::Abs(Offset.X) + FMath::Abs(Offset.Y) + FMath::Abs(Offset.Z)));
        }
        return Best;
    }
}

void FVoxelLightShots::AddTorchRoomSteps(AVoxelWorld& World)
{
    FFoundationBlockWorld& Blocks = World.GetAuthority()->World;
    const FFoundationBlockType* Stone = Blocks.FindBlock(TEXT("stone"));
    const FFoundationBlockType* Glowstone = Blocks.FindBlock(TEXT("glowstone"));
    IConsoleVariable* Intensity = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.LightIntensity"));
    IConsoleVariable* Temperature = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.LightTemperature"));
    IConsoleVariable* Falloff = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.LightFalloff"));
    IConsoleVariable* Brightness = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.LightBrightness"));
    if (!Stone || !Glowstone || !Blocks.FindBlock(TEXT("torch")) || !Intensity || !Temperature || !Falloff ||
        !Brightness)
    {
        Line(TEXT("No torch room: stone, glowstone, torch or the pool's console variables are missing."));
        return;
    }
    RoomIntensityVariable = Intensity;
    RoomTemperatureVariable = Temperature;
    RoomFalloffVariable = Falloff;
    RoomBrightnessVariable = Brightness;
    RoomIntensityAtStart = Intensity->GetFloat();
    RoomTemperatureAtStart = Temperature->GetFloat();
    RoomFalloffAtStart = Falloff->GetFloat();
    RoomBrightnessAtStart = Brightness->GetFloat();

    // The place: under the spawn's column first, then 16 blocks further out ring by ring; the first whose whole shell
    // is in known columns and clear of the bedrock layers.
    FVector Spawn = FVector::ZeroVector;
    if (const APlayerController* Controller = UGameplayStatics::GetPlayerController(&World, 0);
        Controller && Controller->GetPawn())
        Spawn = Cells->ToBlocks(Controller->GetPawn()->GetActorLocation());
    TOptional<FIntVector> Found;
    int32 Surface = 0;
    for (int32 Ring = 0; Ring <= 3 && !Found; ++Ring)
        for (int32 DY = -Ring; DY <= Ring && !Found; ++DY)
            for (int32 DX = -Ring; DX <= Ring && !Found; ++DX)
            {
                if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != Ring) continue;
                const int32 X0 = FMath::FloorToInt32(Spawn.X) + DX * 16 - TorchRoomSize / 2;
                const int32 Y0 = FMath::FloorToInt32(Spawn.Y) + DY * 16 - TorchRoomSize / 2;
                int32 Lowest = TNumericLimits<int32>::Max();
                for (int32 Y = Y0 - 1; Y <= Y0 + TorchRoomSize; ++Y)
                    for (int32 X = X0 - 1; X <= X0 + TorchRoomSize; ++X)
                        Lowest = FMath::Min(Lowest, Cells->GetSurface(X, Y));
                const FIntVector Corner(X0, Y0, Lowest - TorchRoomDepth - TorchRoomHeight);
                if (Corner.Z - 1 < VoxelConstants::MinZ + 8) continue;
                bool bKnown = true;
                for (int32 Z = -1; Z <= TorchRoomHeight && bKnown; ++Z)
                    for (int32 Y = -1; Y <= TorchRoomSize && bKnown; ++Y)
                        for (int32 X = -1; X <= TorchRoomSize; ++X)
                            bKnown &= Blocks.IsKnown(Corner + FIntVector(X, Y, Z));
                if (!bKnown) continue;
                Found = Corner;
                Surface = Lowest;
            }
    if (!Found)
    {
        Line(TEXT("No torch room: no place near the spawn with its whole shell known and above the bedrock."));
        return;
    }
    const FIntVector Corner = *Found;
    const FFoundationBlockState StoneState = Stone->GetDefaultState(), GlowState = Glowstone->GetDefaultState();
    const FIntVector GlowCell = TorchRoomCell(Corner, 5, 5, TorchRoomHeight + 1);
    const TArray<FIntVector> OneTorch = {TorchRoomCell(Corner, 5, 5)};
    const TArray<FIntVector> FourTorches = {TorchRoomCell(Corner, 3, 3), TorchRoomCell(Corner, 3, 7),
        TorchRoomCell(Corner, 7, 3), TorchRoomCell(Corner, 7, 7)};
    const TArray<FIntVector> GlowCells = {GlowCell};
    // The eye: the corner cell's centre, 1.62 blocks over the floor, toward the room's middle and slightly down.
    const FVector Eye = FVector(Corner) + FVector(.5, .5, TorchRoomEyeHeight);
    FRotator Rotation =
        (FVector(Corner) + FVector(TorchRoomSize * .5, TorchRoomSize * .5, TorchRoomEyeHeight) - Eye).Rotation();
    Rotation.Pitch = TorchRoomPitch;
    Rotation.Roll = 0;
    const FIntVector Last = Corner + FIntVector(TorchRoomSize - 1, TorchRoomSize - 1, TorchRoomHeight - 1);
    Line(FString::Printf(TEXT("Torch room (probe P4): interior air X %d..%d, Y %d..%d, Z %d..%d (9 x 4 x 9; interior "
                              "coordinates 1-9 across and 1-4 up from cell %s), stone shell X %d..%d, Y %d..%d, Z "
                              "%d..%d (11 x 6 x 11). Lowest surface over it Z %d: the ceiling's shell is %d blocks "
                              "under it. Spawn (blocks) %s. Cells: X east, Y south, Z up (vanilla x, z, y)."),
        Corner.X, Last.X, Corner.Y, Last.Y, Corner.Z, Last.Z, *Corner.ToString(), Corner.X - 1, Last.X + 1,
        Corner.Y - 1, Last.Y + 1, Corner.Z - 1, Last.Z + 1, Surface, Surface - (Last.Z + 1), *Spawn.ToCompactString()));
    Line(FString::Printf(TEXT("Lights: one torch on the floor at interior (5,5) (cell %s); four at (3,3), (3,7), (7,3) "
                              "and (7,7), the floor's quarter points (cells %s, %s, %s, %s); the glowstone replaces "
                              "the ceiling's shell over (5,5) (cell %s)."),
        *OneTorch[0].ToString(), *FourTorches[0].ToString(), *FourTorches[1].ToString(), *FourTorches[2].ToString(),
        *FourTorches[3].ToString(), *GlowCell.ToString()));
    const double Vertical = 2 *
        FMath::RadiansToDegrees(FMath::Atan(
            FMath::Tan(FMath::DegreesToRadians(LightShotsFieldOfView / 2)) * Height / FMath::Max(Width, 1)));
    Line(FString::Printf(TEXT("Camera: blocks %s, the centre of interior corner cell (1,1) with the eye 1.62 over the "
                              "floor; yaw %.1f (toward the room's middle: south-east, vanilla yaw -45), pitch %.0f "
                              "(vanilla pitch %.0f, looking down). FOV %.0f deg horizontal (the shot camera's; %.1f "
                              "deg vertical at %d x %d). Vanilla's default FOV 70 is vertical."),
        *Eye.ToCompactString(), Rotation.Yaw, Rotation.Pitch, -Rotation.Pitch, LightShotsFieldOfView, Vertical, Width,
        Height));

    // The patches: floor faces 1, 2, 4 and 6 blocks (taxicab) from the single torch, on the side away from the camera
    // so the torch hides none, and the east wall's face beside them.
    auto AddPatch = [this](const TCHAR* Name, const FVector& Point, const FVector& Normal)
    {
        FPatch& Patch = RoomPatches.AddDefaulted_GetRef();
        Patch.Name = Name;
        Patch.Point = Point;
        Patch.Normal = Normal;
    };
    const FVector Up(0, 0, 1);
    AddPatch(TEXT("floor-1"), FVector(TorchRoomCell(Corner, 6, 5)) + FVector(.5, .5, .01), Up);
    AddPatch(TEXT("floor-2"), FVector(TorchRoomCell(Corner, 7, 5)) + FVector(.5, .5, .01), Up);
    AddPatch(TEXT("floor-4"), FVector(TorchRoomCell(Corner, 9, 5)) + FVector(.5, .5, .01), Up);
    AddPatch(TEXT("floor-6"), FVector(TorchRoomCell(Corner, 9, 7)) + FVector(.5, .5, .01), Up);
    AddPatch(TEXT("wall"), FVector(TorchRoomCell(Corner, 10, 5, 2)) + FVector(-.01, .5, .5), FVector(-1, 0, 0));
    Line(TEXT("Patches (mean sRGB 0-255 of a square about 0.4 block across at the face's centre, from the frozen "
              "exposure): floor-1 = the floor under interior (6,5), 1 block from the single torch; floor-2 (7,5), 2 "
              "blocks; floor-4 (9,5), 4 blocks; floor-6 (9,7), 6 blocks taxicab (4.47 straight); wall = the east "
              "wall's face in front of interior (9,5), second block up (5 blocks taxicab from the torch's cell). "
              "Vanilla's block light falls one level per block, taxicab: 14 at the torch, 13 one block away, down to "
              "1 at 13 blocks (glowstone: 15, 14 beside it). L<n> in the table is vanilla's level in the air cell in "
              "front of the patch for that shot's lights."));

    // Every shot: MegaLights off (no pool lights: today's default look), then on at four intensities, two colour
    // temperatures and the glowstone.
    const float Kelvin = RoomTemperatureAtStart;
    TArray<FTorchRoomShot> Shots;
    for (const int32 Torches : {1, 4})
        Shots.Add({FString::Printf(TEXT("room-ml0-torch%d"), Torches), false, Torches, 1000, Kelvin});
    for (const float Candelas : {250.f, 500.f, 1000.f, 2000.f})
        for (const int32 Torches : {1, 4})
            Shots.Add({FString::Printf(TEXT("room-ml1-torch%d-%04dcd"), Torches, int32(Candelas)), true, Torches,
                Candelas, Kelvin});
    for (const float Other : {2700.f, 4000.f})
        Shots.Add({FString::Printf(TEXT("room-ml1-torch1-1000cd-%dK"), int32(Other)), true, 1, 1000, Other});
    Shots.Add({TEXT("room-ml0-glowstone"), false, 0, 1000, Kelvin});
    Shots.Add({TEXT("room-ml1-glowstone-1000cd"), true, 0, 1000, Kelvin});

    // The room is built, the camera put in its corner, and the eye adapts to the one-torch case at 1000 cd with
    // MegaLights on; then the exposure stays frozen for every shot.
    AddStep(TEXT("room-build"), 150,
        [this, &World, Corner, StoneState, Eye, Rotation, OneTorch]
        {
            for (int32 Z = -1; Z <= TorchRoomHeight; ++Z)
                for (int32 Y = -1; Y <= TorchRoomSize; ++Y)
                    for (int32 X = -1; X <= TorchRoomSize; ++X)
                    {
                        const bool bInside =
                            X >= 0 && Y >= 0 && Z >= 0 && X < TorchRoomSize && Y < TorchRoomSize && Z < TorchRoomHeight;
                        SetCell(World, Corner + FIntVector(X, Y, Z), bInside ? FFoundationBlockState() : StoneState);
                    }
            SetView(Eye, Rotation);
            SetMegaLights(true);
            RoomIntensityVariable->Set(1000.f, ECVF_SetByConsole);
            RoomTemperatureVariable->Set(RoomTemperatureAtStart, ECVF_SetByConsole);
            RoomFalloffVariable->Set(0.f, ECVF_SetByConsole);
            VoxelLightProbe::PlaceTorches(World, OneTorch);
            RoomTorches = 1;
            SetExposureSpeed(50);
        })
        .bEdit = true;
    AddStep(TEXT("room-freeze"), 30, [this] { SetExposureSpeed(0); }).End = [this]
    {
        const float Value = GetExposure();
        Line(FString::Printf(TEXT("Exposure settled on the one-torch case (MegaLights 1, 1000 cd, %.0f K) and frozen "
                                  "at %.6g for every shot: EV %.2f (-log2 of the exposure scale, UE's EV100 with the "
                                  "extended luminance range and no exposure compensation)."),
            RoomTemperatureAtStart, Value, Value > 0 ? -FMath::Log2(Value) : 0.f));
    };
    if (bTorchLookPairs)
    {
        AddTorchLookPairSteps(World, Eye, Rotation, OneTorch, FourTorches);
        AddTorchRoomCleanup(World);
        return;
    }
    auto AddShot = [&](const FTorchRoomShot& Shot)
    {
        FStep& Look = AddStep(Shot.Name, 91,
            [this, &World, Shot, Eye, Rotation, OneTorch, FourTorches, GlowCell, StoneState, GlowState]
            {
                SetView(Eye, Rotation);
                SetMegaLights(Shot.bMegaLights);
                RoomIntensityVariable->Set(Shot.Candelas, ECVF_SetByConsole);
                RoomTemperatureVariable->Set(Shot.Kelvin, ECVF_SetByConsole);
                RoomFalloffVariable->Set(Shot.Falloff, ECVF_SetByConsole);
                RoomBrightnessVariable->Set(
                    Shot.Falloff > 0 ? RoomBrightness.FindRef(Shot.Falloff) : RoomBrightnessAtStart, ECVF_SetByConsole);
                SetCell(World, GlowCell, Shot.Torches ? StoneState : GlowState);
                if (Shot.Torches == RoomTorches) return;
                if (Shot.Torches)
                    VoxelLightProbe::PlaceTorches(World, Shot.Torches == 1 ? OneTorch : FourTorches);
                else
                    VoxelLightProbe::RemoveTorches();
                RoomTorches = Shot.Torches;
            });
        Look.bEdit = true;
        Look.CaptureFrom = 90;
        Look.OnFrame = [this, Shot, OneTorch, FourTorches, GlowCells](int32 Frame, const TArray<FColor>& Pixels)
        {
            SavePng(Shot.Name, Pixels);
            const FString Light = Shot.Torches == 0 ? FString(TEXT("glowstone"))
                : Shot.Torches == 1                 ? FString(TEXT("1 torch"))
                                                    : FString::Printf(TEXT("%d torches"), Shot.Torches);
            const FString Intensity = Shot.Falloff > 0
                ? FString::Printf(TEXT("%.0f (unitless)"), RoomBrightness.FindRef(Shot.Falloff))
                : FString::Printf(TEXT("%.0f cd"), Shot.Candelas);
            const FString Falloff =
                Shot.Falloff > 0 ? FString::Printf(TEXT("exponent %g"), Shot.Falloff) : FString(TEXT("1/d^2"));
            FString Row = FString::Printf(TEXT("| %s | %d | %s | %s | %s | %.0f |"), *Shot.Name,
                int32(Shot.bMegaLights), *Light, *Intensity, *Falloff, Shot.Kelvin);
            for (const FPatch& Patch : RoomPatches)
            {
                const FVector4 Value = Measure(Patch, Pixels);
                Record(Shot.Name, Frame, Patch, Value);
                if (Shot.Name == TorchRoomReferenceShot && Patch.Name == TEXT("floor-1")) RoomTargetFloor1 = Value.W;
                const FIntVector Air = VoxelLightProbe::FCells::ToCell(Patch.Point + Patch.Normal * .5);
                const int32 Level = Shot.Torches
                    ? TorchRoomVanillaLevel(Air, Shot.Torches == 1 ? OneTorch : FourTorches, 14)
                    : TorchRoomVanillaLevel(Air, GlowCells, 15);
                Row += Value.W < 0 ? FString::Printf(TEXT(" off screen, L%d |"), Level)
                                   : FString::Printf(TEXT(" %.1f (%.0f, %.0f, %.0f) L%d |"), Value.W, Value.X, Value.Y,
                                         Value.Z, Level);
            }
            RoomRows.Add(Row);
        };
        Look.End = [this, &World, Shot]
        {
            NoteExposure(Shot.Name);
            if (!Shot.bMegaLights) return;
            if (const FVoxelLights* Pool = World.GetLights()) Line(Shot.Name + TEXT(": ") + Pool->Describe());
        };
    };
    for (const FTorchRoomShot& Shot : Shots)
        AddShot(Shot);

    // The flatter falloffs (voxel.LightFalloff above 0, MegaLights on): per exponent a short search for the
    // voxel.LightBrightness that puts the single torch's floor-1 patch where the inverse-squared 1000 cd shot put it,
    // then the one- and four-torch shots at the brightness found.
    for (const float Exponent : TorchRoomFalloffs)
    {
        RoomBrightness.Add(Exponent, RoomBrightnessAtStart > 0 ? RoomBrightnessAtStart : 700.f);
        for (int32 Search = 1; Search <= TorchRoomSearchSteps; ++Search)
        {
            const FString Name = FString::Printf(TEXT("room-search-exp%g-%d"), Exponent, Search);
            FStep& Try = AddStep(Name, 91,
                [this, &World, Exponent, Eye, Rotation, OneTorch, GlowCell, StoneState]
                {
                    SetView(Eye, Rotation);
                    SetMegaLights(true);
                    RoomIntensityVariable->Set(1000.f, ECVF_SetByConsole);
                    RoomTemperatureVariable->Set(RoomTemperatureAtStart, ECVF_SetByConsole);
                    RoomFalloffVariable->Set(Exponent, ECVF_SetByConsole);
                    RoomBrightnessVariable->Set(RoomBrightness.FindRef(Exponent), ECVF_SetByConsole);
                    SetCell(World, GlowCell, StoneState);
                    if (RoomTorches == 1) return;
                    VoxelLightProbe::PlaceTorches(World, OneTorch);
                    RoomTorches = 1;
                });
            Try.bEdit = true;
            Try.CaptureFrom = 90;
            Try.OnFrame = [this, Name, Exponent](int32 Frame, const TArray<FColor>& Pixels)
            {
                const FPatch& Floor1 = RoomPatches[0];
                const FVector4 Value = Measure(Floor1, Pixels);
                Record(Name, Frame, Floor1, Value);
                const float Tried = RoomBrightness.FindRef(Exponent);
                // Aim at the reference shot's floor-1 (room-1's 138.3 if this run has none).
                const double Target = RoomTargetFloor1 > 0 ? RoomTargetFloor1 : 138.3;
                if (Value.W <= 0)
                {
                    Line(
                        FString::Printf(TEXT("%s: brightness %.1f, floor-1 off screen or black; kept."), *Name, Tried));
                    return;
                }
                // Linear light against brightness: a secant through the last two tries in log-log, or a straight
                // proportion for the first; at most a factor 4 a step.
                const double LogB = FMath::Loge(double(Tried)), LogM = FMath::Loge(TorchRoomLinear(Value.W));
                double Slope = 1;
                if (const FVector2D* Last = RoomSearchLast.Find(Exponent); Last && FMath::Abs(LogB - Last->X) > 1e-3)
                    Slope = FMath::Clamp((LogM - Last->Y) / (LogB - Last->X), .25, 2.);
                RoomSearchLast.Add(Exponent, FVector2D(LogB, LogM));
                const double Step = FMath::Clamp(
                    (FMath::Loge(TorchRoomLinear(Target)) - LogM) / Slope, -FMath::Loge(4.), FMath::Loge(4.));
                const float Next = float(FMath::Exp(LogB + Step));
                RoomBrightness.Add(Exponent, Next);
                Line(FString::Printf(TEXT("%s: voxel.LightFalloff %g, voxel.LightBrightness %.1f gives floor-1 %.1f "
                                          "against %.1f (%s); next %.1f."),
                    *Name, Exponent, Tried, Value.W, Target,
                    RoomTargetFloor1 > 0 ? TorchRoomReferenceShot : TEXT("room-1's value"), Next));
            };
        }
        for (const int32 Torches : {1, 4})
            AddShot({FString::Printf(TEXT("room-ml1-torch%d-exp%g"), Torches, Exponent), true, Torches, 1000, Kelvin,
                Exponent});
    }
    AddTorchRoomCleanup(World);
}

void FVoxelLightShots::AddTorchRoomCleanup(AVoxelWorld& World)
{
    // Last, so a stopped run still gets here: the torches, the variables, MegaLights and every written cell back.
    AddStep(TEXT("room-cleanup"), 10,
        [this, &World]
        {
            VoxelLightProbe::RemoveTorches();
            RoomTorches = 0;
            LookTorches.Reset();
            RestoreRoomVariables();
            SetMegaLights(MegaLightsAtStart != 0);
            for (const TPair<FIntVector, FFoundationBlockState>& Original : Originals)
                SetCell(World, Original.Key, Original.Value);
        });
}

void FVoxelLightShots::AddTorchRoomTables()
{
    if (!RoomRows.IsEmpty())
    {
        Line(TEXT("Patch means per shot, mean sRGB (R, G, B) and vanilla's level L<n> (intensity: the pool's "
                  "voxel.LightIntensity in cd with inverse-squared falloff 1/d^2, or voxel.LightBrightness, unitless, "
                  "with voxel.LightFalloff's exponent; K is voxel.LightTemperature; with MegaLights 0 the pool has no "
                  "lights):"));
        Line(TEXT(
            "| Shot | MegaLights | Light | Intensity | Falloff | K | floor-1 | floor-2 | floor-4 | floor-6 | wall |"));
        Line(TEXT("|---|---|---|---|---|---|---|---|---|---|---|"));
        for (const FString& Row : RoomRows)
            Line(Row);
    }
    for (const TPair<float, float>& Found : RoomBrightness)
        Line(FString::Printf(TEXT("voxel.LightFalloff %g: voxel.LightBrightness %.1f after %d search steps (the "
                                  "room-search-exp%g-* lines above), aiming floor-1 at %.1f."),
            Found.Key, Found.Value, TorchRoomSearchSteps, Found.Key, RoomTargetFloor1));
    if (RoomIntensityVariable && RoomTemperatureVariable && RoomFalloffVariable && RoomBrightnessVariable)
        Line(FString::Printf(TEXT("voxel.LightIntensity back to %.0f, voxel.LightTemperature to %.0f, "
                                  "voxel.LightFalloff to %g and voxel.LightBrightness to %g (at start %.0f, %.0f, %g "
                                  "and %g)."),
            RoomIntensityVariable->GetFloat(), RoomTemperatureVariable->GetFloat(), RoomFalloffVariable->GetFloat(),
            RoomBrightnessVariable->GetFloat(), RoomIntensityAtStart, RoomTemperatureAtStart, RoomFalloffAtStart,
            RoomBrightnessAtStart));
}

void FVoxelLightShots::RestoreRoomVariables()
{
    if (RoomIntensityVariable) RoomIntensityVariable->Set(RoomIntensityAtStart, ECVF_SetByConsole);
    if (RoomTemperatureVariable) RoomTemperatureVariable->Set(RoomTemperatureAtStart, ECVF_SetByConsole);
    if (RoomFalloffVariable) RoomFalloffVariable->Set(RoomFalloffAtStart, ECVF_SetByConsole);
    if (RoomBrightnessVariable) RoomBrightnessVariable->Set(RoomBrightnessAtStart, ECVF_SetByConsole);
}

namespace
{
    // -VoxelShotTorchLook's crop: the camera this far from the single torch (blocks, level), its eye at the flame's
    // height over the floor, aiming at the torch's middle, with a narrower field of view (degrees, horizontal) so the
    // torch fills much of the frame; the exposure stays the room's frozen one.
    constexpr double TorchLookCropDistance = 1.2;
    constexpr double TorchLookCropEyeHeight = .6;
    constexpr double TorchLookCropAimHeight = .36;
    constexpr double TorchLookCropFieldOfView = 60;
    // Torches in the probe cave's view.
    constexpr int32 TorchLookProbeTorches = 10;

    // An image's mean luma (0-255) and its shares (%) of saturated (luma 250 or more) and black (5 or less) pixels.
    FVector TorchLookStats(const TArray<FColor>& Pixels)
    {
        if (Pixels.IsEmpty()) return FVector(-1, -1, -1);
        double Sum = 0;
        int32 Saturated = 0, Black = 0;
        for (const FColor& Pixel : Pixels)
        {
            const double Luma = LightShotsLuma(Pixel);
            Sum += Luma;
            Saturated += Luma >= 250;
            Black += Luma <= 5;
        }
        return FVector(Sum / Pixels.Num(), 100. * Saturated / Pixels.Num(), 100. * Black / Pixels.Num());
    }

    struct FTorchLookView
    {
        FString Name;
        FVector Eye = FVector::ZeroVector;
        FRotator Rotation = FRotator::ZeroRotator;
        double FieldOfView = LightShotsFieldOfView;
        TArray<FIntVector> Torches;
    };
}

void FVoxelLightShots::AddTorchLookPairSteps(AVoxelWorld& World, const FVector& Eye, const FRotator& Rotation,
    const TArray<FIntVector>& OneTorch, const TArray<FIntVector>& FourTorches)
{
    // The room-build step leaves the single torch standing.
    LookTorches = OneTorch;
    TorchSites.Append(OneTorch);
    TorchSites.Append(FourTorches);
    TArray<FTorchLookView> Views;
    Views.Add({TEXT("room-1"), Eye, Rotation, LightShotsFieldOfView, OneTorch});
    // The crop: from the single torch toward the room's corner camera, so the lit floor and far wall are behind it.
    const FVector TorchBase = FVector(OneTorch[0]) + FVector(.5, .5, 0);
    FVector Toward = Eye - TorchBase;
    Toward.Z = 0;
    const FVector CropEye =
        TorchBase + Toward.GetSafeNormal() * TorchLookCropDistance + FVector(0, 0, TorchLookCropEyeHeight);
    FRotator CropRotation = (TorchBase + FVector(0, 0, TorchLookCropAimHeight) - CropEye).Rotation();
    CropRotation.Roll = 0;
    Views.Add({TEXT("crop"), CropEye, CropRotation, TorchLookCropFieldOfView, OneTorch});
    Views.Add({TEXT("room-4"), Eye, Rotation, LightShotsFieldOfView, FourTorches});
    Line(FString::Printf(TEXT("Look pairs at the run's own settings, each with MegaLights 0 then 1, under the frozen "
                              "exposure: room-1, crop, room-4, probe-%d. Crop: camera %s (blocks), %.1f blocks level "
                              "from the torch at cell %s with the eye %.2f over the floor (the flame's height), aiming "
                              "%.2f over the floor (pitch %.1f), FOV %.0f deg horizontal."),
        TorchLookProbeTorches, *CropEye.ToCompactString(), TorchLookCropDistance, *OneTorch[0].ToString(),
        TorchLookCropEyeHeight, TorchLookCropAimHeight, CropRotation.Pitch, TorchLookCropFieldOfView));
    if (!ViewBlocks.IsZero())
    {
        const TArray<FIntVector> ProbeCells = VoxelLightProbe::FindFloorLightCells(
            *Cells, Cells->ToCm(ViewBlocks), ViewRotation.Vector(), TorchLookProbeTorches);
        TorchSites.Append(ProbeCells);
        Views.Add({FString::Printf(TEXT("probe-%d"), TorchLookProbeTorches), ViewBlocks, ViewRotation,
            LightShotsFieldOfView, ProbeCells});
        Line(FString::Printf(TEXT("probe-%d: %d floor cells of the probe cave (T1's), camera %s facing %s."),
            TorchLookProbeTorches, ProbeCells.Num(), *ViewBlocks.ToCompactString(),
            *ViewRotation.Vector().ToCompactString()));
    }
    else
        Line(FString::Printf(TEXT("probe-%d: no cave view in the streamed area; skipped."), TorchLookProbeTorches));

    for (const FTorchLookView& View : Views)
        for (const bool bOn : {false, true})
        {
            const FString Shot = FString::Printf(TEXT("look-%s-ml%d"), *View.Name, int32(bOn));
            FStep& Look = AddStep(Shot, 91,
                [this, &World, View, bOn]
                {
                    SetView(View.Eye, View.Rotation);
                    if (Camera.IsValid()) Camera->GetCameraComponent()->SetFieldOfView(float(View.FieldOfView));
                    SetMegaLights(bOn);
                    // The run's own settings: the room-build step set the settle's.
                    RestoreRoomVariables();
                    if (LookTorches == View.Torches) return;
                    VoxelLightProbe::PlaceTorches(World, View.Torches);
                    LookTorches = View.Torches;
                });
            Look.bEdit = true;
            Look.CaptureFrom = 90;
            Look.OnFrame = [this, &World, Shot, bOn](int32 Frame, const TArray<FColor>& Pixels)
            {
                SavePng(Shot, Pixels);
                const FVector Stats = TorchLookStats(Pixels);
                const FVoxelLights::FSettings Settings = FVoxelLights::ReadSettings();
                LookRows.Add(FString::Printf(TEXT("| %s | %d | %.2f | %.3f | %.3f | %d | %.0f cd | %.0f K | %g | %g | "
                                                  "%.6g |"),
                    *Shot, int32(bOn), Stats.X, Stats.Y, Stats.Z, World.GetMeshTorchGlow(), Settings.IntensityCd,
                    Settings.Temperature, Settings.Falloff, Settings.Brightness, GetExposure()));
            };
            Look.End = [this, &World, Shot]
            {
                NoteExposure(Shot);
                if (const FVoxelLights* Pool = World.GetLights()) Line(Shot + TEXT(": ") + Pool->Describe());
            };
        }
}
