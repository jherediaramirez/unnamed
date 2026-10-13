#include "Voxel/VoxelFluidShots.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationCoordinates.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Storage/VoxelColumn.h"
#include "UnrealClient.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelFluidSystem.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelWorldSubsystem.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

namespace
{
    constexpr double FluidShotsCellCm = UFoundationProfile::BlockCm;
    // The top water block of a sea: its surface is sea level (63).
    constexpr int32 FluidSeaTop = VoxelConstants::SeaLevel - 1;
    // Breach: the room's interior is 2 BreachRoomHalf + 1 cells square and BreachRoomHeight high, its ceiling
    // BreachShaftAbove + 1 cells under the seabed block, which lies under at least BreachMinSeaDepth water sources.
    constexpr int32 BreachRoomHalf = 3;
    constexpr int32 BreachRoomHeight = 4;
    constexpr int32 BreachShaftAbove = 2;
    constexpr int32 BreachMinSeaDepth = 2;
    // Cells around a site whose columns must be in the simulation ring: the site, and the cells its acts read.
    constexpr int32 FluidRingMargin = 8;
    constexpr double BreachShotSeconds[] = {3., 8., 20.};
    constexpr int32 BreachShotCount = UE_ARRAY_COUNT(BreachShotSeconds);
    // Flood: a (2 FloodHalfGrid + 1) square grid of sources (5 x 5: terraced ground loses water to drops, and about
    // 1,000 cells get wet), the sources' spacing and how far one may move off its grid point, the half width of the box
    // its water is counted in, the trace's length, and the spacing and reach of the candidate centres (the ring limits
    // them), in cells.
    constexpr int32 FloodHalfGrid = 2;
    constexpr int32 FloodSpacing = 10;
    constexpr int32 FloodSourceShift = 2;
    constexpr int32 FloodReach = FloodHalfGrid * FloodSpacing + 8;
    constexpr double FloodSeconds = 30.;
    constexpr int32 FloodStep = 4;
    constexpr int32 FloodSearch = 192;
    // A flood site this flat and open (score: the mean height difference from the centre source's ground x 100, plus
    // the % of top cells covered) ends the search.
    constexpr int32 FloodGoodScore = 10;
    // Frames the view waits once the edits are on screen (Lumen and the exposure), at most for the edits, and for a
    // screenshot's file.
    constexpr int32 FluidSettleFrames = 150;
    constexpr int32 FluidIdleWaitFrames = 900;
    constexpr int32 FluidShotWriteFrames = 120;
    constexpr float FluidShotsFieldOfView = 90.f;
    constexpr float FluidShotsExposureSpeed = 50.f;
    constexpr uint8 FluidAllWalls = 0x3F;
    // Scenes: when the water and the lava poses may start after the build (the probe took its water frames 8-25 s
    // after the build and its lava frames 35-45 s after it), the frames a pose is on screen before its shot (Lumen and
    // the exposure), and vanilla's eye height and vertical field of view. The probe set fov 70, but its spectator
    // camera flies, which widens the view: its frames show the ledge 1/1.138 as wide as a 70 degree camera does
    // (s2_fall_front45, 290 against 330 px), which is 77 degrees (2 atan(1.138 tan 35)).
    constexpr double FluidSceneWaterSeconds = 8.;
    constexpr double FluidSceneLavaSeconds = 35.;
    constexpr int32 FluidScenePoseFrames = 60;
    constexpr double FluidSceneEyeHeight = 1.62;
    constexpr double FluidSceneVerticalFov = 77.;
    struct FFluidScenePose
    {
        const TCHAR* Label;
        // The probe's /tp: vanilla feet x, y, z, yaw and pitch.
        double X, Y, Z, Yaw, Pitch;
        bool bLava;
    };
    // facts.md's poses, in its order, the settle pairs by their first frame (_a).
    const FFluidScenePose FluidScenePoses[] = {{TEXT("s1_spread_above45_a"), -99.5, -51.62, .5, 0, 45, false},
        {TEXT("s1_spread_low"), -99.5, -60.82, -2.5, 0, 5, false},
        {TEXT("s6_spread_close_down"), -96.5, -58.62, 10.5, 0, 90, false},
        {TEXT("s6_spread_close45"), -96.5, -58.62, 7.5, 0, 45, false},
        {TEXT("s2_fall_front_a"), -77.5, -57.62, 22.5, 180, 5, false},
        {TEXT("s2_fall_front45"), -77.5, -51.62, 21.5, 180, 45, false},
        {TEXT("s2_fall_side"), -65.5, -58.62, 11.5, 90, 5, false},
        {TEXT("s2_fall_lip"), -77.5, -56.12, 13.5, 180, 31, false},
        {TEXT("s3_terraces_front45_a"), -52.5, -49.62, 15.5, 180, 44, false},
        {TEXT("s3_terraces_side"), -43.5, -53.62, 5.5, 90, 30, false},
        {TEXT("s6_terrace_close_down"), -52.5, -55.62, 5.5, 0, 90, false},
        {TEXT("s6_terrace_close45"), -52.5, -55.62, 2.5, 0, 45, false},
        {TEXT("s4_pool_above45_a"), -27.5, -52.62, -3.5, 0, 45, false},
        {TEXT("s4_pool_shore_low"), -19.5, -60.82, 6.5, 90, 8, false},
        {TEXT("s4_pool_gap45"), -23.5, -59.62, 6.5, 90, 45, false},
        {TEXT("s5_lava_spread45_a"), -99.5, -54.62, 43.5, 0, 45, true},
        {TEXT("s5_lava_spread_low"), -99.5, -60.82, 42.5, 0, 5, true},
        {TEXT("s5_lava_fall_front_a"), -77.5, -57.62, 62.5, 180, 5, true},
        {TEXT("s5_lava_fall_front45"), -77.5, -51.62, 61.5, 180, 45, true},
        {TEXT("s5_lava_fall_side"), -65.5, -58.62, 51.5, 90, 5, true}};
    // Ice (W3c): the frozen pool's box (vanilla x and z, inclusive; 3 x 3 columns) and its poses: a line along x at
    // 45 degrees down, 0.75 blocks a step, then the pool from above.
    constexpr int32 FluidIceMinX = -48, FluidIceMaxX = -1, FluidIceMinZ = 0, FluidIceMaxZ = 47;
    const FFluidScenePose FluidIcePoses[] = {{TEXT("ice_path_00"), -30.5, -55, 2.5, 0, 45, false},
        {TEXT("ice_path_01"), -29.75, -55, 2.5, 0, 45, false}, {TEXT("ice_path_02"), -29, -55, 2.5, 0, 45, false},
        {TEXT("ice_path_03"), -28.25, -55, 2.5, 0, 45, false}, {TEXT("ice_path_04"), -27.5, -55, 2.5, 0, 45, false},
        {TEXT("ice_path_05"), -26.75, -55, 2.5, 0, 45, false}, {TEXT("ice_path_06"), -26, -55, 2.5, 0, 45, false},
        {TEXT("ice_path_07"), -25.25, -55, 2.5, 0, 45, false}, {TEXT("ice_path_08"), -24.5, -55, 2.5, 0, 45, false},
        {TEXT("ice_path_09"), -23.75, -55, 2.5, 0, 45, false}, {TEXT("ice_path_10"), -23, -55, 2.5, 0, 45, false},
        {TEXT("ice_path_11"), -22.25, -55, 2.5, 0, 45, false}, {TEXT("ice_path_12"), -21.5, -55, 2.5, 0, 45, false},
        {TEXT("ice_path_13"), -20.75, -55, 2.5, 0, 45, false}, {TEXT("ice_path_14"), -20, -55, 2.5, 0, 45, false},
        {TEXT("ice_path_15"), -19.25, -55, 2.5, 0, 45, false}, {TEXT("ice_above"), -24.5, -30, 24.5, 0, 90, false}};

    FIntPoint FluidColumnOf(int32 X, int32 Y)
    {
        return FIntPoint(FoundationGrid::FloorDivide(X, VoxelConstants::ColumnSize),
            FoundationGrid::FloorDivide(Y, VoxelConstants::ColumnSize));
    }

    const FVoxelColumnPipeline* GetFluidShotsPipeline(const AVoxelWorld& World)
    {
        const UVoxelWorldSubsystem* Streaming = UVoxelWorldSubsystem::Get(World.GetWorld());
        return Streaming ? Streaming->GetPipeline() : nullptr;
    }

    // Reads the pipeline's final columns, keeping the last column found: only while nothing writes.
    struct FFluidColumnReader
    {
        explicit FFluidColumnReader(const FVoxelColumnPipeline& InPipeline) : Pipeline(InPipeline) {}

        const FVoxelColumn* Find(int32 X, int32 Y)
        {
            const FIntPoint At = FluidColumnOf(X, Y);
            if (!bCached || At != Column)
            {
                Column = At;
                Data = Pipeline.FindColumn(At);
                bCached = true;
            }
            return Data;
        }
        // False when the cell's column is not final or the cell lies outside the world's height.
        bool Get(const FIntVector& Cell, uint16& OutState)
        {
            const FVoxelColumn* Found = Find(Cell.X, Cell.Y);
            if (!Found || Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return false;
            OutState = Found->GetState(
                Cell.X - Column.X * VoxelConstants::ColumnSize, Cell.Y - Column.Y * VoxelConstants::ColumnSize, Cell.Z);
            return true;
        }
        // The top block of a heightmap; below the world when the column is not final.
        int32 Height(EVoxelHeightmap Map, int32 X, int32 Y)
        {
            const FVoxelColumn* Found = Find(X, Y);
            return Found ? Found->GetHeight(Map, X - Column.X * VoxelConstants::ColumnSize,
                               Y - Column.Y * VoxelConstants::ColumnSize)
                         : VoxelConstants::MinZ - 1;
        }

        const FVoxelColumnPipeline& Pipeline;
        FIntPoint Column = FIntPoint::ZeroValue;
        const FVoxelColumn* Data = nullptr;
        bool bCached = false;
    };

    // A full solid block holding no fluid: no fluid crosses it.
    bool IsFluidWall(const FVoxelFluidCell& Cell)
    {
        return Cell.Fluid == EVoxelFluid::None && Cell.bSolid && Cell.Walls == FluidAllWalls;
    }

    // Visits the points of the square ring of Radius around the origin (the origin for 0) until Visit returns false.
    bool ForEachFluidRingPoint(int32 Radius, TFunctionRef<bool(FIntPoint)> Visit)
    {
        if (Radius == 0) return Visit(FIntPoint::ZeroValue);
        for (int32 D = -Radius; D <= Radius; ++D)
            if (!Visit({D, -Radius}) || !Visit({D, Radius})) return false;
        for (int32 D = 1 - Radius; D < Radius; ++D)
            if (!Visit({-Radius, D}) || !Visit({Radius, D})) return false;
        return true;
    }

    // Every column within Margin cells of Site is in the fluids' simulation ring.
    bool IsFluidSiteInRing(const FVoxelFluidSystem& Fluids, FIntPoint Site, int32 Margin)
    {
        const FIntPoint Low = FluidColumnOf(Site.X - Margin, Site.Y - Margin),
                        High = FluidColumnOf(Site.X + Margin, Site.Y + Margin);
        for (int32 Y = Low.Y; Y <= High.Y; ++Y)
            for (int32 X = Low.X; X <= High.X; ++X)
                if (!Fluids.IsInRing({X, Y})) return false;
        return true;
    }

    const TCHAR* GetFluidScenarioName(FVoxelFluidShots::EScenario Scenario)
    {
        return Scenario == FVoxelFluidShots::EScenario::Breach ? TEXT("breach")
            : Scenario == FVoxelFluidShots::EScenario::Flood   ? TEXT("flood")
            : Scenario == FVoxelFluidShots::EScenario::Scenes  ? TEXT("scenes")
                                                               : TEXT("ice");
    }

    TConstArrayView<FFluidScenePose> GetFluidScenePoses(FVoxelFluidShots::EScenario Scenario)
    {
        if (Scenario == FVoxelFluidShots::EScenario::Ice) return FluidIcePoses;
        return FluidScenePoses;
    }

    // The fluid cell at a vanilla position (x, y, z: cell (x, z, y)) from the pipeline's final columns; null when its
    // column is not final.
    const FVoxelFluidCell* ReadSceneCell(
        const FVoxelColumnPipeline& Pipeline, const FVoxelFluidSystem& Fluids, int32 X, int32 Y, int32 Z)
    {
        FFluidColumnReader Reader(Pipeline);
        uint16 State = 0;
        return Reader.Get({X, Z, Y}, State) ? &Fluids.GetCell(State) : nullptr;
    }

    // A cell as a character: '0' a source, '1'-'7' flowing, 'F' falling, '#' a solid block fluid never crosses, '.'
    // anything else, '?' not final.
    TCHAR DescribeSceneCell(const FVoxelFluidCell* Cell)
    {
        if (!Cell) return TEXT('?');
        if (Cell->Fluid != EVoxelFluid::None) return Cell->Level >= 8 ? TEXT('F') : TCHAR(TEXT('0') + Cell->Level);
        return IsFluidWall(*Cell) ? TEXT('#') : TEXT('.');
    }

    // The value at a share of the sorted values (0.95: the 95th percentile).
    double FluidPercentile(TArray<double> Values, double Share)
    {
        if (Values.IsEmpty()) return 0;
        Values.Sort();
        return Values[FMath::Clamp(FMath::CeilToInt32(Share * Values.Num()) - 1, 0, Values.Num() - 1)];
    }

    double FluidMean(const TArray<double>& Values)
    {
        double Sum = 0;
        for (const double Value : Values)
            Sum += Value;
        return Values.IsEmpty() ? 0 : Sum / Values.Num();
    }
}

TSharedPtr<FVoxelFluidShots> FVoxelFluidShots::Make(const FString& InFolder, const FString& Name)
{
    if (Name.Equals(TEXT("breach"), ESearchCase::IgnoreCase))
        return MakeShared<FVoxelFluidShots>(InFolder, EScenario::Breach);
    if (Name.Equals(TEXT("flood"), ESearchCase::IgnoreCase))
        return MakeShared<FVoxelFluidShots>(InFolder, EScenario::Flood);
    if (Name.Equals(TEXT("scenes"), ESearchCase::IgnoreCase))
        return MakeShared<FVoxelFluidShots>(InFolder, EScenario::Scenes);
    if (Name.Equals(TEXT("ice"), ESearchCase::IgnoreCase))
        return MakeShared<FVoxelFluidShots>(InFolder, EScenario::Ice);
    UE_LOG(LogUnnamedVoxel, Warning, TEXT("-VoxelShotFluids=%s is not breach, flood, scenes or ice; no fluid shots"),
        *Name);
    return nullptr;
}

FVoxelFluidShots::FVoxelFluidShots(const FString& InFolder, EScenario InScenario)
    : Folder(InFolder), Scenario(InScenario)
{
}

FVoxelFluidShots::~FVoxelFluidShots()
{
    if (Stage != EStage::Done && FluidsVariable) FluidsVariable->Set(FluidsAtStart, ECVF_SetByConsole);
}

bool FVoxelFluidShots::Tick(AVoxelWorld& World)
{
    if (Stage == EStage::Done) return false;
    ++StageFrames;
    FVoxelFluidSystem* Fluids = World.GetFluids();
    const FVoxelColumnPipeline* Pipeline = GetFluidShotsPipeline(World);
    APlayerController* Controller = UGameplayStatics::GetPlayerController(&World, 0);
    auto SetStage = [this, &World](EStage Next)
    {
        Stage = Next;
        StageFrames = 0;
        IdleFrames = 0;
        StageSeconds = GetSeconds(World);
        StageTick = World.GetClock().GetGameTick();
    };
    if (Stage == EStage::Enable)
    {
        FluidsVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.Fluids"));
        // Read before anything can end the capture, which puts it back.
        if (FluidsVariable) FluidsAtStart = FluidsVariable->GetInt();
        if (!FluidsVariable || !Fluids || !Fluids->IsRunning() || !Pipeline || !Controller)
        {
            Line(TEXT("No fluid shots: no voxel.Fluids, running fluids, streaming or player. RESULT: FAILED"));
            Finish(World);
            return false;
        }
        // The scenes run as voxel.Fluids is set, so a control run can stop the flow.
        const bool bScenes = Scenario == EScenario::Scenes || Scenario == EScenario::Ice;
        if (!bScenes) FluidsVariable->Set(1, ECVF_SetByConsole);
        FieldOfView = FluidShotsFieldOfView;
        if (bScenes)
        {
            // Vanilla's field of view is vertical.
            FVector2D Size = FVector2D::ZeroVector;
            if (UGameViewportClient* Viewport = World.GetWorld()->GetGameViewport()) Viewport->GetViewportSize(Size);
            if (Size.X <= 0 || Size.Y <= 0) Size = FVector2D(16, 9);
            FieldOfView = float(FoundationCoordinates::HorizontalFov(FluidSceneVerticalFov, Size.X / Size.Y));
        }
        ACameraActor* ShotCamera = World.GetWorld()->SpawnActor<ACameraActor>();
        UCameraComponent* Lens = ShotCamera->GetCameraComponent();
        Lens->SetFieldOfView(FieldOfView);
        Lens->bConstrainAspectRatio = false;
        FPostProcessSettings& Post = Lens->PostProcessSettings;
        Post.bOverride_AutoExposureSpeedUp = Post.bOverride_AutoExposureSpeedDown = true;
        Post.AutoExposureSpeedUp = Post.AutoExposureSpeedDown = FluidShotsExposureSpeed;
        Camera = ShotCamera;
        IFileManager::Get().MakeDirectory(*Folder, true);
        SetStage(EStage::Setup);
        return true;
    }
    if (!Fluids || !Pipeline || !Controller)
    {
        Line(TEXT("Stopped: the fluids, streaming or player went away."));
        Finish(World);
        return false;
    }
    switch (Stage)
    {
    case EStage::Setup:
    {
        // The fluids see the ring from their first Advance with voxel.Fluids on.
        if (!Fluids->IsInRing(Pipeline->GetRings().Center) && StageFrames < 30) return true;
        const bool bReady = Scenario == EScenario::Breach ? SetupBreach(World)
            : Scenario == EScenario::Flood                ? SetupFlood(World)
                                                          : SetupScenes(World);
        if (!bReady)
        {
            Finish(World);
            return false;
        }
        Controller->SetViewTarget(Camera.Get());
        // The scenes' clock starts at the build, as the probe's did.
        SetStage(Scenario == EScenario::Scenes || Scenario == EScenario::Ice ? EStage::Run : EStage::Settle);
        return true;
    }
    case EStage::Settle:
        // The edits remesh, then Lumen and the exposure settle on the view.
        if (!Pipeline->IsIdle() && StageFrames < FluidIdleWaitFrames) return true;
        if (++IdleFrames < FluidSettleFrames) return true;
        if (Scenario == EScenario::Flood)
        {
            StartFlood(World);
            SetStage(EStage::Run);
            return true;
        }
        if (LastShot.IsEmpty())
        {
            const FCount Room = CountWater(World, RoomMin, RoomMax);
            Line(FString::Printf(TEXT("Shot dry after %d frames (%d waiting for the remesh): room water %d sources, %d "
                                      "flowing, %d falling."),
                StageFrames, StageFrames - IdleFrames, Room.Sources, Room.Flowing, Room.Falling));
            Shoot(TEXT("dry"));
            return true;
        }
        if (IsShotPending()) return true;
        BreakSeabed(World);
        SetStage(EStage::Run);
        RecordBreachSecond(World, 0);
        return true;
    case EStage::Run:
        if (Scenario == EScenario::Breach)
            TickBreach(World);
        else if (Scenario == EScenario::Flood)
            TickFlood(World);
        else
            TickScenes(World);
        return true;
    case EStage::Drain:
        if (IsShotPending()) return true;
        Finish(World);
        return false;
    default:
        return false;
    }
}

bool FVoxelFluidShots::SetupBreach(AVoxelWorld& World)
{
    const FVoxelFluidSystem& Fluids = *World.GetFluids();
    FFoundationBlockWorld& Blocks = World.GetAuthority()->World;
    const FVoxelColumnPipeline& Pipeline = *GetFluidShotsPipeline(World);
    const FVoxelStreamingRings& Rings = Pipeline.GetRings();
    const FIntPoint Middle = Rings.Center * VoxelConstants::ColumnSize + FIntPoint(VoxelConstants::ColumnSize / 2);
    const int32 Reach = (Rings.ViewRadius + 1) * VoxelConstants::ColumnSize;
    const FFoundationBlockType* Glowstone = Blocks.FindBlock(TEXT("glowstone"));
    if (!Glowstone)
    {
        Line(TEXT("No breach: no glowstone block. RESULT: FAILED"));
        return false;
    }
    FFluidColumnReader Reader(Pipeline);
    // A cell's state as the rules see it; null when its column is not final.
    auto Read = [&Reader, &Fluids](const FIntVector& Cell) -> const FVoxelFluidCell*
    {
        uint16 State = 0;
        return Reader.Get(Cell, State) ? &Fluids.GetCell(State) : nullptr;
    };
    auto IsWallAt = [&Read](const FIntVector& Cell)
    {
        const FVoxelFluidCell* Found = Read(Cell);
        return Found && IsFluidWall(*Found);
    };
    auto IsDryAt = [&Read](const FIntVector& Cell)
    {
        const FVoxelFluidCell* Found = Read(Cell);
        return Found && Found->Fluid == EVoxelFluid::None;
    };
    // A sea over a solid seabed: sources from the seabed up to the top water block, air over it.
    auto IsSea = [&](FIntPoint Site, int32 Bed)
    {
        if (Bed > FluidSeaTop - BreachMinSeaDepth ||
            Bed - BreachShaftAbove - BreachRoomHeight - 2 < VoxelConstants::MinZ)
            return false;
        for (int32 Z = Bed + 1; Z <= FluidSeaTop; ++Z)
        {
            const FVoxelFluidCell* Water = Read({Site.X, Site.Y, Z});
            if (!Water || !Water->IsSource(EVoxelFluid::Water)) return false;
        }
        return IsDryAt({Site.X, Site.Y, FluidSeaTop + 1}) && IsWallAt({Site.X, Site.Y, Bed});
    };
    // The room's shell (9 x 9 x 6 less the interior and the shaft's ceiling cell) and the shaft's walls are solid
    // blocks without fluid; the interior and the shaft hold no fluid.
    auto IsSealed = [&](FIntPoint Site, int32 Bed)
    {
        const int32 Ceiling = Bed - BreachShaftAbove - 1, Floor = Ceiling - BreachRoomHeight - 1;
        for (int32 Z = Floor; Z <= Ceiling; ++Z)
            for (int32 Y = Site.Y - BreachRoomHalf - 1; Y <= Site.Y + BreachRoomHalf + 1; ++Y)
                for (int32 X = Site.X - BreachRoomHalf - 1; X <= Site.X + BreachRoomHalf + 1; ++X)
                {
                    const bool bInterior = FMath::Abs(X - Site.X) <= BreachRoomHalf &&
                        FMath::Abs(Y - Site.Y) <= BreachRoomHalf && Z > Floor && Z < Ceiling;
                    const bool bShaft = X == Site.X && Y == Site.Y && Z == Ceiling;
                    if (bInterior || bShaft ? !IsDryAt({X, Y, Z}) : !IsWallAt({X, Y, Z})) return false;
                }
        for (int32 Z = Ceiling + 1; Z < Bed; ++Z)
            if (!IsDryAt({Site.X, Site.Y, Z}) || !IsWallAt({Site.X - 1, Site.Y, Z}) ||
                !IsWallAt({Site.X + 1, Site.Y, Z}) || !IsWallAt({Site.X, Site.Y - 1, Z}) ||
                !IsWallAt({Site.X, Site.Y + 1, Z}))
                return false;
        return true;
    };
    int32 Checked = 0, NotSea = 0, NotSealed = 0;
    bool bFound = false;
    for (int32 Radius = 0; Radius <= Reach && !bFound; ++Radius)
        ForEachFluidRingPoint(Radius,
            [&](FIntPoint Offset)
            {
                const FIntPoint Site = Middle + Offset;
                if (!IsFluidSiteInRing(Fluids, Site, BreachRoomHalf + FluidRingMargin)) return true;
                ++Checked;
                const int32 Bed = Reader.Height(EVoxelHeightmap::OceanFloor, Site.X, Site.Y);
                if (!IsSea(Site, Bed))
                {
                    ++NotSea;
                    return true;
                }
                if (!IsSealed(Site, Bed))
                {
                    ++NotSealed;
                    return true;
                }
                Shaft = Site;
                Seabed = Bed;
                bFound = true;
                return false;
            });
    const FString Search = FString::Printf(TEXT("%d columns checked from the streaming center %s (cell %s): %d not a "
                                                "sea over a solid seabed, %d not sealed"),
        Checked, *Rings.Center.ToString(), *Middle.ToString(), NotSea, NotSealed);
    if (!bFound)
    {
        Line(TEXT("No breach site in the simulation ring: ") + Search + TEXT(". RESULT: FAILED"));
        return false;
    }
    const int32 Ceiling = Seabed - BreachShaftAbove - 1;
    RoomMin = FIntVector(Shaft.X - BreachRoomHalf, Shaft.Y - BreachRoomHalf, Ceiling - BreachRoomHeight);
    RoomMax = FIntVector(Shaft.X + BreachRoomHalf, Shaft.Y + BreachRoomHalf, Ceiling - 1);
    const FFoundationBlockState SeabedState = Blocks.Get({Shaft.X, Shaft.Y, Seabed}).State;
    Line(FString::Printf(TEXT("Breach site: shaft column (%d, %d), %.0f cells from the streaming center's middle; "
                              "seabed %s at Z %d under %d water sources (Z %d-%d); %s."),
        Shaft.X, Shaft.Y, FVector2D(Shaft - Middle).Size(), *Blocks.GetBlock(SeabedState).Id.ToString(), Seabed,
        FluidSeaTop - Seabed, Seabed + 1, FluidSeaTop, *Search));

    // The room, the shaft from the ceiling to the cell under the seabed, and four glowstones in the ceiling.
    int32 Carved = 0, Changed = 0;
    auto Write = [&Blocks, &Changed](const FIntVector& Cell, FFoundationBlockState State)
    {
        if (!Blocks.IsKnown(Cell) || Blocks.Get(Cell).State == State) return;
        Blocks.Set(Cell, State);
        ++Changed;
    };
    for (int32 Z = RoomMin.Z; Z <= RoomMax.Z; ++Z)
        for (int32 Y = RoomMin.Y; Y <= RoomMax.Y; ++Y)
            for (int32 X = RoomMin.X; X <= RoomMax.X; ++X)
            {
                Write({X, Y, Z}, FFoundationBlockState{});
                ++Carved;
            }
    for (int32 Z = Ceiling; Z < Seabed; ++Z)
    {
        ShaftCells.Add({Shaft.X, Shaft.Y, Z});
        Write(ShaftCells.Last(), FFoundationBlockState{});
        ++Carved;
    }
    for (const FIntPoint Corner : {FIntPoint(-2, -2), FIntPoint(2, -2), FIntPoint(-2, 2), FIntPoint(2, 2)})
        Write({Shaft.X + Corner.X, Shaft.Y + Corner.Y, Ceiling}, Glowstone->GetDefaultState());
    Line(FString::Printf(TEXT("Room: interior %s to %s (%d x %d x %d), floor Z %d, ceiling Z %d; shaft Z %d-%d (%d "
                              "cells); %d cells carved to air, %d cells changed with the 4 ceiling glowstones at "
                              "(%d+-2, %d+-2, %d); shell 9 x 9 x %d and shaft walls checked solid and dry."),
        *RoomMin.ToString(), *RoomMax.ToString(), 2 * BreachRoomHalf + 1, 2 * BreachRoomHalf + 1, BreachRoomHeight,
        RoomMin.Z - 1, Ceiling, Ceiling, Seabed - 1, ShaftCells.Num(), Carved, Changed, Shaft.X, Shaft.Y, Ceiling,
        BreachRoomHeight + 2));

    // From a ceiling corner of the room, at the shaft's foot.
    SetView(World, FVector(RoomMin.X + .35, RoomMin.Y + .35, RoomMin.Z + 2.8),
        FVector(Shaft.X + .5, Shaft.Y + .5, RoomMin.Z + 1.8));
    Csv.Add(FString(TEXT("seconds,game_ticks,room_sources,room_flowing,room_falling,shaft_water,")) + StatsHeader());
    return true;
}

void FVoxelFluidShots::BreakSeabed(AVoxelWorld& World)
{
    FFoundationBlockWorld& Blocks = World.GetAuthority()->World;
    const FIntVector Cell(Shaft.X, Shaft.Y, Seabed);
    const FFoundationBlockState Before = Blocks.Get(Cell).State;
    // An edit, as the player's mining is: the block world's hook wakes the sea.
    Blocks.Set(Cell, FFoundationBlockState{});
    Line(FString::Printf(TEXT("Seabed %s at %s mined to air at fluid tick %llu, game tick %llu."),
        *Blocks.GetBlock(Before).Id.ToString(), *Cell.ToString(), World.GetFluids()->GetStats().Tick,
        World.GetClock().GetGameTick()));
}

void FVoxelFluidShots::RecordBreachSecond(AVoxelWorld& World, double Seconds)
{
    BreachSecond = FMath::FloorToInt32(Seconds);
    const FCount Room = CountWater(World, RoomMin, RoomMax);
    int32 ShaftWater = 0;
    for (const FIntVector& Cell : ShaftCells)
        ShaftWater += CountWater(World, Cell, Cell).Total();
    ShaftWater += CountWater(World, {Shaft.X, Shaft.Y, Seabed}, {Shaft.X, Shaft.Y, Seabed}).Total();
    Csv.Add(FString::Printf(TEXT("%.2f,%llu,%d,%d,%d,%d,%s"), Seconds, World.GetClock().GetGameTick() - StageTick,
        Room.Sources, Room.Flowing, Room.Falling, ShaftWater, *DescribeStats(World)));
}

void FVoxelFluidShots::TickBreach(AVoxelWorld& World)
{
    const double Seconds = GetSeconds(World) - StageSeconds;
    if (FMath::FloorToInt32(Seconds) > BreachSecond) RecordBreachSecond(World, Seconds);
    if (BreachShot < BreachShotCount && Seconds >= BreachShotSeconds[BreachShot])
    {
        const FCount Room = CountWater(World, RoomMin, RoomMax);
        const FString Name = FString::Printf(TEXT("%02ds"), FMath::RoundToInt32(BreachShotSeconds[BreachShot]));
        Line(FString::Printf(TEXT("Shot %s at %.2f s (%llu game ticks): room water %d sources, %d flowing, %d falling "
                                  "(%d of %d cells); fluids %s."),
            *Name, Seconds, World.GetClock().GetGameTick() - StageTick, Room.Sources, Room.Flowing, Room.Falling,
            Room.Total(), (RoomMax.X - RoomMin.X + 1) * (RoomMax.Y - RoomMin.Y + 1) * (RoomMax.Z - RoomMin.Z + 1),
            *DescribeStats(World)));
        Shoot(Name);
        if (++BreachShot == BreachShotCount)
        {
            Stage = EStage::Drain;
            StageFrames = 0;
        }
    }
}

bool FVoxelFluidShots::SetupFlood(AVoxelWorld& World)
{
    const FVoxelFluidSystem& Fluids = *World.GetFluids();
    const FVoxelColumnPipeline& Pipeline = *GetFluidShotsPipeline(World);
    const FVoxelStreamingRings& Rings = Pipeline.GetRings();
    const FIntPoint Middle = Rings.Center * VoxelConstants::ColumnSize + FIntPoint(VoxelConstants::ColumnSize / 2);
    const int32 BoxCells = FMath::Square(2 * FloodReach + 1);
    FFluidColumnReader Reader(Pipeline);
    int32 Checked = 0, NotOpen = 0, Wet = 0;
    int32 BestScore = MAX_int32, BestLow = 0, BestHigh = 0, BestDeviation = 0, BestCovered = 0;
    for (int32 Radius = 0; Radius * FloodStep <= FloodSearch && BestScore > FloodGoodScore; ++Radius)
        ForEachFluidRingPoint(Radius,
            [&](FIntPoint Offset)
            {
                const FIntPoint Center = Middle + Offset * FloodStep;
                if (!IsFluidSiteInRing(Fluids, Center, FloodReach + 2)) return true;
                ++Checked;
                // Each source: the cell nearest its grid point, at most FloodSourceShift away, that is air over a
                // solid top block.
                TArray<FIntVector> GridSources;
                for (int32 J = -FloodHalfGrid; J <= FloodHalfGrid; ++J)
                    for (int32 I = -FloodHalfGrid; I <= FloodHalfGrid; ++I)
                    {
                        const int32 Wanted = GridSources.Num() + 1;
                        const FIntPoint Grid(Center.X + I * FloodSpacing, Center.Y + J * FloodSpacing);
                        for (int32 Shift = 0; Shift <= FloodSourceShift && GridSources.Num() < Wanted; ++Shift)
                            ForEachFluidRingPoint(Shift,
                                [&](FIntPoint Step)
                                {
                                    const int32 X = Grid.X + Step.X, Y = Grid.Y + Step.Y;
                                    const int32 Top = Reader.Height(EVoxelHeightmap::MotionBlockingNoLeaves, X, Y);
                                    uint16 Ground = 0, Above = 1;
                                    if (!Reader.Get({X, Y, Top}, Ground) || !IsFluidWall(Fluids.GetCell(Ground)) ||
                                        !Reader.Get({X, Y, Top + 1}, Above) || Above != 0)
                                        return true;
                                    GridSources.Add({X, Y, Top + 1});
                                    return false;
                                });
                        if (GridSources.Num() < Wanted)
                        {
                            ++NotOpen;
                            return true;
                        }
                    }
                // The box's surface: dry, its height range, its summed height difference from the centre source's
                // ground, and the top cells covered (plants, snow).
                const int32 Base = GridSources[GridSources.Num() / 2].Z - 1;
                int32 Low = MAX_int32, High = MIN_int32, Deviation = 0, Covered = 0;
                for (int32 Y = Center.Y - FloodReach; Y <= Center.Y + FloodReach; ++Y)
                    for (int32 X = Center.X - FloodReach; X <= Center.X + FloodReach; ++X)
                    {
                        const int32 Top = Reader.Height(EVoxelHeightmap::MotionBlockingNoLeaves, X, Y);
                        uint16 Ground = 0, Above = 0;
                        if (!Reader.Get({X, Y, Top}, Ground) || Fluids.GetCell(Ground).Fluid != EVoxelFluid::None)
                        {
                            ++Wet;
                            return true;
                        }
                        Low = FMath::Min(Low, Top);
                        High = FMath::Max(High, Top);
                        Deviation += FMath::Abs(Top - Base);
                        if (Deviation * 100 / BoxCells >= BestScore) return true;
                        Covered += Reader.Get({X, Y, Top + 1}, Above) && Above != 0;
                    }
                const int32 Score = (Deviation + Covered) * 100 / BoxCells;
                if (Score < BestScore)
                {
                    BestScore = Score;
                    FloodCenter = Center;
                    BestLow = Low;
                    BestHigh = High;
                    BestDeviation = Deviation;
                    BestCovered = Covered;
                    FloodSources = MoveTemp(GridSources);
                }
                return BestScore > FloodGoodScore;
            });
    const FString Search = FString::Printf(TEXT("%d centres checked from the streaming center %s (cell %s): %d with no "
                                                "cell of air over a solid block near a source's grid point, %d wet"),
        Checked, *Rings.Center.ToString(), *Middle.ToString(), NotOpen, Wet);
    if (BestScore == MAX_int32)
    {
        Line(TEXT("No flood site in the simulation ring: ") + Search + TEXT(". RESULT: FAILED"));
        return false;
    }
    FloodMin = FIntVector(FloodCenter.X - FloodReach, FloodCenter.Y - FloodReach, BestLow - 6);
    FloodMax = FIntVector(FloodCenter.X + FloodReach, FloodCenter.Y + FloodReach, BestHigh + 2);
    Line(FString::Printf(TEXT("Flood site: centre (%d, %d), %.0f cells from the streaming center's middle; surface Z "
                              "%d-%d over the %d x %d box, %.2f blocks from the centre source's ground on average, "
                              "%d%% of its top cells covered (plants, snow); %s."),
        FloodCenter.X, FloodCenter.Y, FVector2D(FloodCenter - Middle).Size(), BestLow, BestHigh, 2 * FloodReach + 1,
        2 * FloodReach + 1, double(BestDeviation) / BoxCells, BestCovered * 100 / BoxCells, *Search));
    // From above the south-west corner, at the grid's middle.
    SetView(World, FVector(FloodCenter.X + .5 - 18, FloodCenter.Y + .5 - 18, BestHigh + 24),
        FVector(FloodCenter.X + .5, FloodCenter.Y + .5, BestLow + 1));
    Csv.Add(TEXT("frame,seconds,frame_ms,fluid_ms,acts,behind,queued,parked,water_cells"));
    return true;
}

void FVoxelFluidShots::StartFlood(AVoxelWorld& World)
{
    FVoxelFluidSystem& Fluids = *World.GetFluids();
    FFoundationBlockWorld& Blocks = World.GetAuthority()->World;
    FloodBaseline = CountWater(World, FloodMin, FloodMax).Total();
    const FFoundationBlockState Source{Fluids.GetState(FVoxelFluidCell::MakeFluid(EVoxelFluid::Water, 0))};
    // The air cells over solid top blocks the search found.
    TArray<FString> Cells;
    for (const FIntVector& Site : FloodSources)
    {
        Blocks.Set(Site, Source);
        Cells.Add(Site.ToString());
    }
    CutAtStart = Fluids.GetStats().CutFrames;
    Line(
        FString::Printf(TEXT("Placed %d water sources after %d frames at %s; water in the counted box %s to %s before: "
                             "%d cells; fluid tick %llu."),
            FloodSources.Num(), StageFrames, *FString::Join(Cells, TEXT(" ")), *FloodMin.ToString(),
            *FloodMax.ToString(), FloodBaseline, Fluids.GetStats().Tick));
}

void FVoxelFluidShots::TickFlood(AVoxelWorld& World)
{
    const double Seconds = GetSeconds(World) - StageSeconds;
    const FVoxelFluidSystem::FStats Stats = World.GetFluids()->GetStats();
    FString Water;
    if (FMath::FloorToInt32(Seconds) > FloodSecond)
    {
        FloodSecond = FMath::FloorToInt32(Seconds);
        LastCells = CountWater(World, FloodMin, FloodMax).Total() - FloodBaseline;
        PeakCells = FMath::Max(PeakCells, LastCells);
        Water = FString::FromInt(LastCells);
    }
    FrameMs.Add(FApp::GetDeltaTime() * 1000.);
    FluidMs.Add(Stats.LastMs);
    Csv.Add(FString::Printf(TEXT("%d,%.3f,%.3f,%.4f,%d,%llu,%d,%d,%s"), FrameMs.Num(), Seconds, FrameMs.Last(),
        Stats.LastMs, Stats.LastActs, Stats.Behind, Stats.Queued, Stats.Parked, *Water));
    if (!bSettled && Stats.Queued == 0 && Stats.Behind == 0 && Stats.Parked == 0)
    {
        bSettled = true;
        SettledTick = Stats.Tick;
        SettledTicks = World.GetClock().GetGameTick() - StageTick;
        SettledSeconds = Seconds;
    }
    if (Seconds < FloodSeconds) return;

    LastCells = CountWater(World, FloodMin, FloodMax).Total() - FloodBaseline;
    PeakCells = FMath::Max(PeakCells, LastCells);
    const double BudgetMs = FVoxelFluidSystem::FSettings::Read().BudgetMs;
    int32 Over = 0;
    double FluidMax = 0, FrameMax = 0;
    for (const double Ms : FluidMs)
    {
        Over += BudgetMs > 0 && Ms > BudgetMs;
        FluidMax = FMath::Max(FluidMax, Ms);
    }
    for (const double Ms : FrameMs)
        FrameMax = FMath::Max(FrameMax, Ms);
    Line(FString::Printf(TEXT("Frames: %d over %.2f s."), FrameMs.Num(), Seconds));
    Line(FString::Printf(TEXT("Fluid ms per frame: mean %.4f, p95 %.4f, max %.4f; frames over the %.2f ms budget: %d "
                              "(%lld stopped at it with acts left)."),
        FluidMean(FluidMs), FluidPercentile(FluidMs, .95), FluidMax, BudgetMs, Over, Stats.CutFrames - CutAtStart));
    Line(FString::Printf(
        TEXT("Frame ms: mean %.2f, p95 %.2f, max %.2f."), FluidMean(FrameMs), FluidPercentile(FrameMs, .95), FrameMax));
    Line(FString::Printf(
        TEXT("Water cells (less the %d before): peak %d, final %d."), FloodBaseline, PeakCells, LastCells));
    Line(bSettled
            ? FString::Printf(TEXT("Settled (nothing queued, nothing behind) at fluid tick %llu: %llu game ticks, "
                                   "%.2f s after the sources."),
                  SettledTick, SettledTicks, SettledSeconds)
            : FString::Printf(TEXT("Not settled after %.2f s: %d acts queued, %llu ticks behind."), Seconds,
                  Stats.Queued, Stats.Behind));
    Shoot(TEXT("end"));
    Stage = EStage::Drain;
    StageFrames = 0;
}

FVoxelFluidShots::FCount FVoxelFluidShots::CountWater(
    AVoxelWorld& World, const FIntVector& Min, const FIntVector& Max) const
{
    FCount Count;
    const FVoxelColumnPipeline* Pipeline = GetFluidShotsPipeline(World);
    if (!Pipeline || !World.GetFluids()) return Count;
    const FVoxelFluidSystem& Fluids = *World.GetFluids();
    FFluidColumnReader Reader(*Pipeline);
    for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
        for (int32 X = Min.X; X <= Max.X; ++X)
            for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
            {
                uint16 State = 0;
                if (!Reader.Get({X, Y, Z}, State)) continue;
                const FVoxelFluidCell& Cell = Fluids.GetCell(State);
                if (Cell.Fluid != EVoxelFluid::Water) continue;
                if (Cell.Level == 0)
                    ++Count.Sources;
                else if (Cell.Level < 8)
                    ++Count.Flowing;
                else
                    ++Count.Falling;
            }
    return Count;
}

bool FVoxelFluidShots::SetupScenes(AVoxelWorld& World)
{
    FFoundationBlockWorld& Blocks = World.GetAuthority()->World;
    const FFoundationBlockType* Grass = Blocks.FindBlock(TEXT("grass_block"));
    const FFoundationBlockType* Dirt = Blocks.FindBlock(TEXT("dirt"));
    const FFoundationBlockType* Stone = Blocks.FindBlock(TEXT("stone"));
    const FFoundationBlockType* Water = Blocks.FindBlock(TEXT("water"));
    const FFoundationBlockType* Lava = Blocks.FindBlock(TEXT("lava"));
    const FFoundationBlockType* Ice = Blocks.FindBlock(TEXT("ice"));
    if (!Grass || !Dirt || !Stone || !Water || !Lava || !Ice)
    {
        Line(TEXT("No scenes: grass_block, dirt, stone, water, lava or ice is missing. RESULT: FAILED"));
        return false;
    }
    // Vanilla (x, y, z) is cell (x, z, y).
    auto At = [](int32 X, int32 Y, int32 Z) { return FIntVector(X, Z, Y); };
    const FFoundationBlockState Ground = Blocks.Get(At(-100, -61, 10)).State;
    const FFoundationBlockState Under = Blocks.Get(At(-100, -62, 10)).State;
    const FFoundationBlockState Over = Blocks.Get(At(-100, -60, 10)).State;
    if (Blocks.GetBlock(Ground).Id != Grass->Id || Blocks.GetBlock(Under).Id != Dirt->Id || !Over.IsAir())
    {
        Line(FString::Printf(TEXT("No scenes: vanilla (-100, -61..-60, 10) holds %s over %s under %s, not VP1's "
                                  "Superflat (grass_block over dirt, air above): open a Superflat world. RESULT: "
                                  "FAILED"),
            *Blocks.GetBlock(Ground).Id.ToString(), *Blocks.GetBlock(Under).Id.ToString(),
            *Blocks.GetBlock(Over).Id.ToString()));
        return false;
    }
    int32 Changed = 0, Unknown = 0;
    auto Write = [&Blocks, &Changed, &Unknown, &At](int32 X, int32 Y, int32 Z, FFoundationBlockState State)
    {
        const FIntVector Cell = At(X, Y, Z);
        if (!Blocks.IsKnown(Cell))
        {
            ++Unknown;
            return;
        }
        if (Blocks.Get(Cell).State == State) return;
        // An edit, as the probe's commands are: the block world's hook wakes the fluids.
        Blocks.Set(Cell, State);
        ++Changed;
    };
    // The probe's /fill: inclusive, in vanilla coordinates.
    auto Fill = [&Write](int32 X0, int32 Y0, int32 Z0, int32 X1, int32 Y1, int32 Z1, FFoundationBlockState State)
    {
        for (int32 Y = Y0; Y <= Y1; ++Y)
            for (int32 Z = Z0; Z <= Z1; ++Z)
                for (int32 X = X0; X <= X1; ++X)
                    Write(X, Y, Z, State);
    };
    const FFoundationBlockState Air, GrassState = Grass->GetDefaultState(), StoneState = Stone->GetDefaultState(),
                                     WaterState = Water->GetDefaultState(), LavaState = Lava->GetDefaultState();
    const bool bIce = Scenario == EScenario::Ice;
    if (bIce)
    {
        // The frozen pool: water two deep under ice at the grass level, as a frozen lake's top.
        Fill(FluidIceMinX, -63, FluidIceMinZ, FluidIceMaxX, -62, FluidIceMaxZ, WaterState);
        Fill(FluidIceMinX, -61, FluidIceMinZ, FluidIceMaxX, -61, FluidIceMaxZ, Ice->GetDefaultState());
    }
    else
    {
        // facts.md's setup commands, in order. 1: clear the area.
        Fill(-110, -61, -6, -15, -61, 66, GrassState);
        for (const int32 X : {-110, -86, -62, -38})
            Fill(X, -60, -6, X + 23, -50, 66, Air);
        // 2: one water source.
        Write(-100, -60, 10, WaterState);
        // 3: the waterfall ledge (5 wide, 4 high, 7 deep, top face at y -56).
        Fill(-80, -60, 4, -76, -57, 10, StoneState);
        Write(-78, -56, 10, WaterState);
        // 4: three 1-block terraces in a walled 5-wide channel: the back wall, the terraces, the side walls, the
        // source.
        Fill(-56, -60, -1, -50, -57, -1, StoneState);
        Fill(-55, -60, 0, -51, -58, 4, StoneState);
        Fill(-55, -60, 5, -51, -59, 7, StoneState);
        Fill(-55, -60, 8, -51, -60, 10, StoneState);
        Fill(-56, -60, 0, -56, -57, 4, StoneState);
        Fill(-50, -60, 0, -50, -57, 4, StoneState);
        Fill(-56, -60, 5, -56, -58, 7, StoneState);
        Fill(-50, -60, 5, -50, -58, 7, StoneState);
        Fill(-56, -60, 8, -56, -59, 10, StoneState);
        Fill(-50, -60, 8, -50, -59, 10, StoneState);
        Write(-53, -57, 0, WaterState);
        // 5: the pool, one block into the ground, with one rim gap.
        Fill(-30, -61, 4, -26, -61, 8, WaterState);
        Write(-25, -61, 6, Air);
        // 6: lava, a source and a copy of the ledge 40 blocks south.
        Write(-100, -60, 50, LavaState);
        Fill(-80, -60, 44, -76, -57, 50, StoneState);
        Write(-78, -56, 50, LavaState);
    }
    if (Unknown)
    {
        Line(FString::Printf(TEXT("No scenes: %d cells of the scenes' box are not loaded. RESULT: FAILED"), Unknown));
        return false;
    }
    const FVoxelFluidSystem& Fluids = *World.GetFluids();
    bool bInRing = true;
    const IConsoleVariable* SeeThroughPart = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.SeeThroughPart"));
    const FIntPoint Low = bIce ? FluidColumnOf(FluidIceMinX, FluidIceMinZ) : FluidColumnOf(-110, -6),
                    High = bIce ? FluidColumnOf(FluidIceMaxX, FluidIceMaxZ) : FluidColumnOf(-15, 66);
    for (int32 Y = Low.Y; Y <= High.Y; ++Y)
        for (int32 X = Low.X; X <= High.X; ++X)
            bInRing &= Fluids.IsInRing({X, Y});
    const FVoxelPipelineStats Stats = GetFluidShotsPipeline(World)->GetStats();
    MeshJobsAtBuild = Stats.Mesh.Count;
    MeshMsAtBuild = Stats.Mesh.TotalMs;
    Line(FString::Printf(
        TEXT("%s at game tick %llu: %d cells changed; voxel.Fluids %d, voxel.SeeThroughPart %s; the "
             "scenes' columns (%d..%d, %d..%d) %s the fluid ring. Cells: vanilla (x, y, z) at (x, z, y). Camera: "
             "field of view %.2f deg horizontal (vanilla's measured 77 vertical), eye at the /tp feet + %.2f."),
        bIce ? TEXT("The frozen pool built (water y -63..-62 under ice at -61, x -48..-1, z 0..47)")
             : TEXT("Scenes built by facts.md's commands"),
        World.GetClock().GetGameTick(), Changed, FluidsAtStart,
        SeeThroughPart ? *SeeThroughPart->GetString() : TEXT("?"), Low.X, High.X, Low.Y, High.Y,
        bInRing ? TEXT("all in") : TEXT("not all in"), FieldOfView, FluidSceneEyeHeight));
    return true;
}

void FVoxelFluidShots::TickScenes(AVoxelWorld& World)
{
    if (IsShotPending()) return;
    const TConstArrayView<FFluidScenePose> Poses = GetFluidScenePoses(Scenario);
    if (ScenePose >= Poses.Num())
    {
        if (Scenario == EScenario::Scenes) RecordScenes(World);
        Stage = EStage::Drain;
        StageFrames = 0;
        return;
    }
    const FFluidScenePose& Pose = Poses[ScenePose];
    const double Seconds = GetSeconds(World) - StageSeconds;
    if (Seconds < (Pose.bLava ? FluidSceneLavaSeconds : FluidSceneWaterSeconds)) return;
    if (PoseFrames++ == 0)
        SetPose(World, FVector(Pose.X, Pose.Z, Pose.Y + FluidSceneEyeHeight),
            FRotator(
                FoundationCoordinates::ToUnrealPitch(Pose.Pitch), FoundationCoordinates::ToUnrealYaw(Pose.Yaw), 0));
    if (PoseFrames < FluidScenePoseFrames) return;
    Line(FString::Printf(TEXT("Shot %s at %.2f s (%llu game ticks) from /tp %.2f %.2f %.2f %.0f %.0f: camera %s "
                              "blocks, yaw %.0f, pitch %.0f; fluids %s."),
        Pose.Label, Seconds, World.GetClock().GetGameTick() - StageTick, Pose.X, Pose.Y, Pose.Z, Pose.Yaw, Pose.Pitch,
        *CameraBlocks.ToCompactString(), FoundationCoordinates::ToUnrealYaw(Pose.Yaw),
        FoundationCoordinates::ToUnrealPitch(Pose.Pitch), *DescribeStats(World)));
    Shoot(Pose.Label);
    PoseFrames = 0;
    ++ScenePose;
}

void FVoxelFluidShots::RecordScenes(AVoxelWorld& World)
{
    const FVoxelColumnPipeline& Pipeline = *GetFluidShotsPipeline(World);
    const FVoxelFluidSystem& Fluids = *World.GetFluids();
    struct FReach
    {
        const TCHAR* What;
        int32 X, Y, Z;
        EVoxelFluid Fluid;
    };
    // The probe's RCON checks; None: the cell holds no fluid.
    const FReach Reaches[] = {
        {TEXT("water reaches 7 cells on flat ground: (-93, -60, 10) holds water"), -93, -60, 10, EVoxelFluid::Water},
        {TEXT("...and no further: (-92, -60, 10) holds none"), -92, -60, 10, EVoxelFluid::None},
        {TEXT("overworld lava reaches 3 cells: (-97, -60, 50) holds lava"), -97, -60, 50, EVoxelFluid::Lava},
        {TEXT("...and no further: (-96, -60, 50) holds none"), -96, -60, 50, EVoxelFluid::None},
        {TEXT("the waterfall lands: (-78, -60, 11) holds water"), -78, -60, 11, EVoxelFluid::Water},
        {TEXT("the lava fall lands: (-78, -60, 51) holds lava"), -78, -60, 51, EVoxelFluid::Lava},
        {TEXT("the terrace runoff reaches the ground: (-53, -60, 13) holds water"), -53, -60, 13, EVoxelFluid::Water},
        {TEXT("the pool gap fills: (-25, -61, 6) holds water"), -25, -61, 6, EVoxelFluid::Water}};
    int32 Matches = 0;
    for (const FReach& Reach : Reaches)
    {
        const FVoxelFluidCell* Cell = ReadSceneCell(Pipeline, Fluids, Reach.X, Reach.Y, Reach.Z);
        const bool bMatch = Cell && Cell->Fluid == Reach.Fluid;
        Matches += bMatch;
        Line(FString::Printf(TEXT("Reach: %s: %s (here '%c')."), Reach.What,
            bMatch ? TEXT("as vanilla") : TEXT("DIFFERS"), DescribeSceneCell(Cell)));
    }
    Line(FString::Printf(TEXT("Reach: %d of %d as the vanilla probe."), Matches, int32(UE_ARRAY_COUNT(Reaches))));

    struct FMap
    {
        const TCHAR* Name;
        int32 Y, X0, X1, Z0, Z1;
    };
    const FMap Maps[] = {{TEXT("scene 1, one source: the ground (y -60)"), -60, -108, -92, 2, 18},
        {TEXT("scene 2: the ledge's top (y -56)"), -56, -81, -75, 3, 12},
        {TEXT("scene 2: the waterfall's ground (y -60)"), -60, -88, -68, 3, 22},
        {TEXT("scene 3: the top terrace (y -57)"), -57, -57, -49, -2, 12},
        {TEXT("scene 3: the middle terrace (y -58)"), -58, -57, -49, -2, 12},
        {TEXT("scene 3: the low terrace (y -59)"), -59, -57, -49, -2, 12},
        {TEXT("scene 3: the ground (y -60)"), -60, -62, -44, -2, 22},
        {TEXT("scene 4, the pool (y -61)"), -61, -32, -22, 2, 10},
        {TEXT("scene 5, the lava source: the ground (y -60)"), -60, -105, -95, 45, 55},
        {TEXT("scene 5, the lava fall: the ledge's top (y -56)"), -56, -81, -75, 43, 52},
        {TEXT("scene 5, the lava fall: the ground (y -60)"), -60, -86, -70, 43, 60}};
    for (const FMap& Map : Maps)
    {
        Line(FString::Printf(TEXT("Map, %s: x %d..%d left to right, z %d..%d top to bottom ('0' source, '1'-'7' "
                                  "flowing, 'F' falling, '#' solid, '.' other)."),
            Map.Name, Map.X0, Map.X1, Map.Z0, Map.Z1));
        for (int32 Z = Map.Z0; Z <= Map.Z1; ++Z)
        {
            FString Row;
            for (int32 X = Map.X0; X <= Map.X1; ++X)
                Row.AppendChar(DescribeSceneCell(ReadSceneCell(Pipeline, Fluids, X, Map.Y, Z)));
            Line(FString::Printf(TEXT("  z %4d  %s"), Z, *Row));
        }
    }
    const FVoxelPipelineStats Stats = Pipeline.GetStats();
    const int64 Jobs = Stats.Mesh.Count - MeshJobsAtBuild;
    Line(FString::Printf(TEXT("Mesh jobs since the build: %lld, %.3f ms mean each (worker time gathering and meshing a "
                              "column's touched sections; the worst job since the world opened %.3f ms)."),
        Jobs, Jobs ? (Stats.Mesh.TotalMs - MeshMsAtBuild) / Jobs : 0., Stats.Mesh.WorstMs));
}

const TCHAR* FVoxelFluidShots::StatsHeader()
{
    return TEXT("fluid_tick,behind,queued,parked,last_ms,last_acts,acts,writes,worst_ms,over_budget_frames");
}

FString FVoxelFluidShots::DescribeStats(AVoxelWorld& World) const
{
    const FVoxelFluidSystem::FStats Stats = World.GetFluids()->GetStats();
    return FString::Printf(TEXT("%llu,%llu,%d,%d,%.4f,%d,%lld,%lld,%.4f,%lld"), Stats.Tick, Stats.Behind, Stats.Queued,
        Stats.Parked, Stats.LastMs, Stats.LastActs, Stats.Acts, Stats.Writes, Stats.WorstMs, Stats.OverBudgetFrames);
}

void FVoxelFluidShots::SetView(AVoxelWorld& World, const FVector& Blocks, const FVector& Target)
{
    CameraBlocks = Blocks;
    TargetBlocks = Target;
    if (Camera.IsValid())
        Camera->SetActorLocationAndRotation(World.OriginCm + Blocks * FluidShotsCellCm, (Target - Blocks).Rotation());
    Line(FString::Printf(TEXT("Camera at %s blocks looking at %s (field of view %.0f)."), *Blocks.ToCompactString(),
        *Target.ToCompactString(), FieldOfView));
}

void FVoxelFluidShots::SetPose(AVoxelWorld& World, const FVector& Blocks, const FRotator& Rotation)
{
    CameraBlocks = Blocks;
    TargetBlocks = Blocks + Rotation.Vector();
    if (Camera.IsValid()) Camera->SetActorLocationAndRotation(World.OriginCm + Blocks * FluidShotsCellCm, Rotation);
}

void FVoxelFluidShots::Shoot(const FString& Name)
{
    const TCHAR* Prefix = GetFluidScenarioName(Scenario);
    LastShot = FPaths::Combine(Folder, FString::Printf(TEXT("%s-%s.png"), Prefix, *Name));
    LastShotFrame = GFrameCounter;
    FScreenshotRequest::RequestScreenshot(LastShot, false, false);
}

bool FVoxelFluidShots::IsShotPending() const
{
    return !LastShot.IsEmpty() && !IFileManager::Get().FileExists(*LastShot) &&
        GFrameCounter - LastShotFrame < FluidShotWriteFrames;
}

double FVoxelFluidShots::GetSeconds(AVoxelWorld& World) const
{
    return World.GetWorld()->GetTimeSeconds();
}

void FVoxelFluidShots::Line(const FString& Text)
{
    Report.Add(Text);
    UE_LOG(LogUnnamedVoxel, Display, TEXT("FluidShots: %s"), *Text);
}

void FVoxelFluidShots::Finish(AVoxelWorld& World)
{
    Stage = EStage::Done;
    if (const FVoxelFluidSystem* Fluids = World.GetFluids())
    {
        Line(TEXT("Fluids at the end: ") + Fluids->Describe());
        Line(FString(TEXT("Fluid stats at the end (")) + StatsHeader() + TEXT("): ") + DescribeStats(World));
    }
    if (FluidsVariable)
    {
        FluidsVariable->Set(FluidsAtStart, ECVF_SetByConsole);
        Line(FString::Printf(TEXT("voxel.Fluids back to %d."), FluidsVariable->GetInt()));
    }
    if (APlayerController* Controller = UGameplayStatics::GetPlayerController(&World, 0))
        Controller->SetViewTarget(Controller->GetPawn());
    if (Camera.IsValid()) Camera->Destroy();
    const FString Name = GetFluidScenarioName(Scenario);
    IFileManager::Get().MakeDirectory(*Folder, true);
    FFileHelper::SaveStringToFile(
        FString::Join(Report, TEXT("\n")) + TEXT("\n"), *FPaths::Combine(Folder, Name + TEXT(".txt")));
    if (!Csv.IsEmpty())
        FFileHelper::SaveStringToFile(
            FString::Join(Csv, TEXT("\n")) + TEXT("\n"), *FPaths::Combine(Folder, Name + TEXT(".csv")));
}
