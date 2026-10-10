#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Foundation/FoundationProfile.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DelayedAutoRegister.h"
#include "Storage/VoxelColumn.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelWorldSubsystem.h"
#include "VoxelConstants.h"

namespace
{
    // Columns each side of the player's whose borders are drawn: 7 x 7 columns, 8 border lines per axis.
    constexpr int32 ColumnGridReach = 3;
    constexpr int32 ColumnGridSpan = (2 * ColumnGridReach + 1) * VoxelConstants::ColumnSize;
    // Height above the surface block's top, in blocks, and of the corner posts.
    constexpr double ColumnGridLift = 1.05;
    constexpr double ColumnGridPost = 6.;
    constexpr int32 ColumnGridUnloaded = MIN_int32;

    TAutoConsoleVariable<int32> CVarDebugColumnGrid(TEXT("voxel.Debug.ColumnGrid"), 0,
        TEXT("1 draws the borders of the 7 x 7 columns around the player (16 x 16 blocks each) as magenta lines that "
             "follow the ground (under plants and water), with a post at each corner. Debug only, not a setting."));

    FDelegateHandle ColumnGridHandle;

    // The OCEAN_FLOOR height (the ground under plants and water) at a world block from the streamed columns, or
    // ColumnGridUnloaded.
    int32 ColumnGridGetHeight(const FVoxelColumnPipeline& Pipeline, int32 X, int32 Y)
    {
        constexpr int32 Size = VoxelConstants::ColumnSize;
        const int32 ColumnX = FMath::DivideAndRoundDown(X, Size), ColumnY = FMath::DivideAndRoundDown(Y, Size);
        const FVoxelColumn* Column = Pipeline.FindColumn(FIntPoint(ColumnX, ColumnY));
        return Column ? Column->GetHeight(EVoxelHeightmap::OceanFloor, X - ColumnX * Size, Y - ColumnY * Size)
                      : ColumnGridUnloaded;
    }

    // One border line at world block coordinate Fixed (a Y when bAlongX, else an X), from Start along the other axis.
    void ColumnGridDrawBorder(UWorld* World, const AVoxelWorld& Voxels, const FVoxelColumnPipeline& Pipeline,
        bool bAlongX, int32 Fixed, int32 Start)
    {
        const auto ToWorld = [&Voxels](double X, double Y, double Z)
        { return Voxels.OriginCm + FVector(X, Y, Z) * UFoundationProfile::BlockCm; };
        const auto Point = [bAlongX, Fixed, &ToWorld](double Along, double Z)
        { return bAlongX ? ToWorld(Along, Fixed, Z) : ToWorld(Fixed, Along, Z); };
        const auto Line = [World](const FVector& A, const FVector& B)
        { DrawDebugLine(World, A, B, FColor(255, 0, 255), false, -1.f, 0, 4.f); };

        int32 Previous = ColumnGridUnloaded;
        for (int32 Step = 0; Step < ColumnGridSpan; ++Step)
        {
            const int32 Along = Start + Step;
            // The two blocks either side of the border.
            const int32 Low = bAlongX ? ColumnGridGetHeight(Pipeline, Along, Fixed - 1)
                                      : ColumnGridGetHeight(Pipeline, Fixed - 1, Along);
            const int32 High =
                bAlongX ? ColumnGridGetHeight(Pipeline, Along, Fixed) : ColumnGridGetHeight(Pipeline, Fixed, Along);
            if (Low == ColumnGridUnloaded || High == ColumnGridUnloaded)
            {
                Previous = ColumnGridUnloaded;
                continue;
            }
            const double Z = FMath::Max(Low, High) + ColumnGridLift;
            if (Previous != ColumnGridUnloaded && Previous != FMath::Max(Low, High))
                Line(Point(Along, Previous + ColumnGridLift), Point(Along, Z));
            Line(Point(Along, Z), Point(Along + 1, Z));
            // A post where this border meets another (the borders along X carry them all).
            if (bAlongX && Step % VoxelConstants::ColumnSize == 0)
                Line(Point(Along, Z), Point(Along, Z + ColumnGridPost));
            Previous = FMath::Max(Low, High);
        }
        if (bAlongX && Previous != ColumnGridUnloaded)
        {
            const double Z = Previous + ColumnGridLift;
            Line(Point(Start + ColumnGridSpan, Z), Point(Start + ColumnGridSpan, Z + ColumnGridPost));
        }
    }

    void ColumnGridDraw(UWorld* World, ELevelTick, float)
    {
        if (!World || !World->IsGameWorld() || CVarDebugColumnGrid.GetValueOnGameThread() != 1) return;
        const AVoxelWorld* Voxels = AVoxelWorld::Find(World);
        const UVoxelWorldSubsystem* Streaming = Voxels ? UVoxelWorldSubsystem::Get(World) : nullptr;
        const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
        const APlayerController* Controller = UGameplayStatics::GetPlayerController(World, 0);
        if (!Pipeline || !Controller) return;
        FVector Location;
        FRotator Rotation;
        Controller->GetPlayerViewPoint(Location, Rotation);
        if (const APawn* Pawn = Controller->GetPawn()) Location = Pawn->GetActorLocation();
        const FVector Blocks = (Location - Voxels->OriginCm) / UFoundationProfile::BlockCm;
        constexpr int32 Size = VoxelConstants::ColumnSize;
        const int32 FirstX = (FMath::FloorToInt32(Blocks.X / Size) - ColumnGridReach) * Size;
        const int32 FirstY = (FMath::FloorToInt32(Blocks.Y / Size) - ColumnGridReach) * Size;
        for (int32 Index = 0; Index <= 2 * ColumnGridReach + 1; ++Index)
        {
            ColumnGridDrawBorder(World, *Voxels, *Pipeline, true, FirstY + Index * Size, FirstX);
            ColumnGridDrawBorder(World, *Voxels, *Pipeline, false, FirstX + Index * Size, FirstY);
        }
    }

    // Hooks the draw while the variable is on; it finds a voxel world itself each frame, so the variable may be set
    // before the world exists.
    void ColumnGridRefresh(IConsoleVariable*)
    {
        const bool bOn = CVarDebugColumnGrid.GetValueOnGameThread() == 1;
        if (bOn && !ColumnGridHandle.IsValid())
            ColumnGridHandle = FWorldDelegates::OnWorldPostActorTick.AddStatic(&ColumnGridDraw);
        else if (!bOn && ColumnGridHandle.IsValid())
        {
            FWorldDelegates::OnWorldPostActorTick.Remove(ColumnGridHandle);
            ColumnGridHandle.Reset();
        }
    }

    FDelayedAutoRegisterHelper ColumnGridRegister(EDelayedRegisterRunPhase::EndOfEngineInit,
        []
        {
            CVarDebugColumnGrid.AsVariable()->SetOnChangedCallback(
                FConsoleVariableDelegate::CreateStatic(&ColumnGridRefresh));
            ColumnGridRefresh(nullptr);
            FCoreDelegates::OnEnginePreExit.AddLambda(
                []
                {
                    if (!ColumnGridHandle.IsValid()) return;
                    FWorldDelegates::OnWorldPostActorTick.Remove(ColumnGridHandle);
                    ColumnGridHandle.Reset();
                });
        });
}
