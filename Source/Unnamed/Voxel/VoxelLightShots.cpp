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
}

FVoxelLightShots::~FVoxelLightShots()
{
    if (bBuilt && !bFinished) SetMegaLights(MegaLightsAtStart != 0);
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
    if (!Cells->IsValid() || !CaveView || !MegaLightsVariable || !Controller || Width <= 0)
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
    ViewBlocks = Cells->ToBlocks(CaveView->Get<1>());
    ViewRotation = (CaveView->Get<2>() - CaveView->Get<1>()).Rotation();
    Line(FString::Printf(TEXT("Cave view: camera %s, facing %s (blocks). Light intensity %.0f cd, radius %.0f cm, "
                              "source radius %.0f cm. MegaLights for the project at start: %d."),
        *ViewBlocks.ToCompactString(), *ViewRotation.Vector().ToCompactString(), IntensityCd,
        VoxelLightProbe::FSettings().RadiusCm, VoxelLightProbe::FSettings().SourceRadiusCm, MegaLightsAtStart));

    // Every case but the leak test looks into the probe cave: dark, and big enough for 100 lights.
    FindProbeView();
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
                    VoxelLightProbe::CountShown(), CountInView(FMath::Min(Case.Count, LightPositions.Num())),
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

void FVoxelLightShots::AddLeakSteps(AVoxelWorld& World)
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
    for (const FCase& Case : {FCase{TEXT("base"), EMode::None, 0, false}, FCase{TEXT("none-1"), EMode::None, 1, false},
             FCase{TEXT("vsm-1"), EMode::Vsm, 1, false}, FCase{TEXT("floor"), EMode::MegaLights, 0, true},
             FCase{TEXT("megalights-1"), EMode::MegaLights, 1, true}})
    {
        const FString Name = TEXT("leak-") + Case.Name;
        FStep& Step = AddStep(Name, 150,
            [this, &World, Case, Light, LeakCamera, Rotation]
            {
                SetView(LeakCamera, Rotation);
                ShowCase(World, Case, Light);
            });
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
        };
    }
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

int32 FVoxelLightShots::CountInView(int32 Count) const
{
    // Spheres of the lights' radius against the view frustum's side planes and the camera plane.
    const double Reach = VoxelLightProbe::FSettings().RadiusCm / 100;
    const double Across = FMath::DegreesToRadians(LightShotsFieldOfView / 2);
    const double Up = FMath::Atan(FMath::Tan(Across) * Height / FMath::Max(Width, 1));
    int32 InView = 0;
    for (int32 Index = 0; Index < FMath::Min(Count, LightPositions.Num()); ++Index)
    {
        const FVector Local = ViewRotation.UnrotateVector(Cells->ToBlocks(LightPositions[Index]) - ViewBlocks);
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
    float Low = TNumericLimits<float>::Max(), High = 0;
    for (const TPair<FString, float>& Value : Exposures)
    {
        Low = FMath::Min(Low, Value.Value);
        High = FMath::Max(High, Value.Value);
    }
    if (!Exposures.IsEmpty())
        Line(FString::Printf(TEXT("Exposure over the %d measured steps: %.6g to %.6g (%s)."), Exposures.Num(), Low,
            High, Low == High ? TEXT("identical") : TEXT("NOT identical")));
    for (const TPair<FIntVector, FFoundationBlockState>& Original : Originals)
        if (World.GetAuthority()->World.Get(Original.Key).State != Original.Value)
            Line(FString::Printf(TEXT("Cell %s was not restored."), *Original.Key.ToString()));
    Line(FString::Printf(TEXT("Restored %d edited cells; MegaLights for the project back to %d."), Originals.Num(),
        MegaLightsVariable ? MegaLightsVariable->GetInt() : -1));
    FFileHelper::SaveStringToFile(
        FString::Join(Report, TEXT("\n")) + TEXT("\n"), *FPaths::Combine(Folder, TEXT("lights.txt")));
    FFileHelper::SaveStringToFile(TEXT("step,frame,patch,r,g,b,mean\n") + FString::Join(Csv, TEXT("\n")) + TEXT("\n"),
        *FPaths::Combine(Folder, TEXT("lights.csv")));
}
