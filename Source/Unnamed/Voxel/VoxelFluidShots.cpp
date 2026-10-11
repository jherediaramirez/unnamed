#include "Voxel/VoxelFluidShots.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlocks.h"
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
    UE_LOG(LogUnnamedVoxel, Warning, TEXT("-VoxelShotFluids=%s is neither breach nor flood; no fluid shots"), *Name);
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
        FluidsVariable->Set(1, ECVF_SetByConsole);
        ACameraActor* ShotCamera = World.GetWorld()->SpawnActor<ACameraActor>();
        UCameraComponent* Lens = ShotCamera->GetCameraComponent();
        Lens->SetFieldOfView(FluidShotsFieldOfView);
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
        const bool bReady = Scenario == EScenario::Breach ? SetupBreach(World) : SetupFlood(World);
        if (!bReady)
        {
            Finish(World);
            return false;
        }
        Controller->SetViewTarget(Camera.Get());
        SetStage(EStage::Settle);
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
        else
            TickFlood(World);
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
        *Target.ToCompactString(), FluidShotsFieldOfView));
}

void FVoxelFluidShots::Shoot(const FString& Name)
{
    const TCHAR* Prefix = Scenario == EScenario::Breach ? TEXT("breach") : TEXT("flood");
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
    const FString Name = Scenario == EScenario::Breach ? TEXT("breach") : TEXT("flood");
    IFileManager::Get().MakeDirectory(*Folder, true);
    FFileHelper::SaveStringToFile(
        FString::Join(Report, TEXT("\n")) + TEXT("\n"), *FPaths::Combine(Folder, Name + TEXT(".txt")));
    if (!Csv.IsEmpty())
        FFileHelper::SaveStringToFile(
            FString::Join(Csv, TEXT("\n")) + TEXT("\n"), *FPaths::Combine(Folder, Name + TEXT(".csv")));
}
