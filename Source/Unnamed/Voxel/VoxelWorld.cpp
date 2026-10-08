#include "Voxel/VoxelWorld.h"
#include "Async/ParallelFor.h"
#include "Blocks/VoxelBlockTable.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Core/RealtimeMeshDataStream.h"
#include "DynamicRHI.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationBlockTextureTable.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Generation/VoxelArea.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshCardBuild.h"
#include "Misc/FileHelper.h"
#include "RenderUtils.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "UnnamedProjectSettings.h"
#include "UnrealClient.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelChunkComponent.h"
#include "Voxel/VoxelConsole.h"
#include "Voxel/VoxelWorldSettings.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"
#include "VoxelStats.h"

DECLARE_CYCLE_STAT(TEXT("Apply columns"), STAT_VoxelApplyColumns, STATGROUP_Voxel);

// A finished build on its way to the screen: the area, one stream set and set of Lumen cards per column with
// geometry, and the apply bookkeeping.
struct AVoxelWorld::FBuild
{
    struct FColumnMesh
    {
        // Index into Area.Columns.
        int32 Column = 0;
        RealtimeMesh::FRealtimeMeshStreamSet Streams;
        FMeshCardsBuildData Cards;
        double CardsMs = 0;
        uint32 LayerMask = 0, SectionMask = 0;
    };

    int32 Serial = 0;
    FVoxelArea Area;
    TArray<FColumnMesh> ColumnMeshes;
    double StreamsMs = 0;
    // The Lumen cards across all columns, the most in one column, and their build time per column.
    int32 CardCount = 0, MostCards = 0;
    double CardsMs = 0, WorstCardsMs = 0;
    double StartSeconds = 0;
    int32 NextColumn = 0;
    double ApplyMs = 0, WorstFrameMs = 0;
    int32 ApplyFrames = 0;
};

namespace
{
    constexpr double CellCm = UFoundationProfile::BlockCm;
    const TCHAR* const ChunkMaterialNames[] = {
        TEXT("M_ChunkOpaque"), TEXT("M_ChunkCutout"), TEXT("M_ChunkTranslucent"), TEXT("M_ChunkWater")};

    // Frames each screenshot waits for Lumen and the shadow cache to settle.
    constexpr int32 ShotSettleFrames = 150;
    // The last settle frames, whose GPU time the shot report averages.
    constexpr int32 ShotTimedFrames = 60;
    // Frames to wait for a requested screenshot file before moving on anyway.
    constexpr int32 ShotWriteFrames = 120;

    struct FShot
    {
        const TCHAR* Name;
        FVector Location;
        FVector Target;
        bool bLumenScene;
    };

    // The settings that decide how the chunks are lit, so every shot report says what it shows.
    FString DescribeShotLighting()
    {
        auto Value = [](const TCHAR* Name)
        {
            const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
            return Variable ? Variable->GetString() : FString(TEXT("?"));
        };
        return FString::Printf(TEXT("  Lighting: r.DynamicGlobalIlluminationMethod %s, r.ReflectionMethod %s, "
                                    "r.Lumen.HardwareRayTracing %s (ray tracing %s), "
                                    "r.Lumen.HardwareRayTracing.LightingMode %s"),
            *Value(TEXT("r.DynamicGlobalIlluminationMethod")), *Value(TEXT("r.ReflectionMethod")),
            *Value(TEXT("r.Lumen.HardwareRayTracing")), IsRayTracingEnabled() ? TEXT("on") : TEXT("off"),
            *Value(TEXT("r.Lumen.HardwareRayTracing.LightingMode")));
    }
}

AVoxelWorld::AVoxelWorld()
{
    PrimaryActorTick.bCanEverTick = true;
}

AVoxelWorld* AVoxelWorld::Find(const UWorld* World)
{
    if (!World) return nullptr;
    for (TActorIterator<AVoxelWorld> It(const_cast<UWorld*>(World)); It; ++It)
        return *It;
    return nullptr;
}

void AVoxelWorld::BuildLayout()
{
    // Not the lab's ground and course: this map's ground is the voxel world.
    Table = VoxelBlocks::GetDefault();
    FString SeedText = UVoxelWorldSettings::Get().DevSeed;
    FParse::Value(FCommandLine::Get(), TEXT("VoxelSeed="), SeedText);
    FParse::Value(FCommandLine::Get(), TEXT("VoxelShot="), ShotFolder);
    LoadMaterials();
    Regenerate(FVoxelSeed::Parse(SeedText));
}

void AVoxelWorld::LoadMaterials()
{
    const UFoundationBlockAssets* Assets = FFoundationBlockRegistry::GetDefault()->GetAssets();
    TextureTable = NewObject<UFoundationBlockTextureTable>(this);
    TextureTable->Initialize(Assets);
    for (const TCHAR* Name : ChunkMaterialNames)
    {
        UMaterialInterface* Base = Cast<UMaterialInterface>(UUnnamedProjectSettings::GetBlockAssetPath(Name).TryLoad());
        if (!Base)
        {
            UE_LOG(LogUnnamedVoxel, Warning,
                TEXT("Chunk material %s is missing: run Scripts/Editor/CreateBlockMaterials.py. Using the default."),
                Name);
            LayerMaterials.Add(nullptr);
            continue;
        }
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, this);
        Instance->SetTextureParameterValue(TEXT("TextureTable"), TextureTable->GetTexture());
        LayerMaterials.Add(Instance);
    }
}

void AVoxelWorld::Regenerate(const FVoxelSeed& NewSeed)
{
    Seed = NewSeed;
    Terrain = MakeShared<FVoxelHeightTerrain>(Seed.Value, *Table);
    SpawnCell.Reset();
    for (const TPair<FIntPoint, TObjectPtr<UVoxelChunkComponent>>& Chunk : Chunks)
        if (Chunk.Value) Chunk.Value->DestroyComponent();
    Chunks.Reset();
    Columns.Reset();
    Applying.Reset();
    bReady = false;
    StartBuild();
}

void AVoxelWorld::StartBuild()
{
    const int32 Serial = ++BuildSerial;
    const int32 Radius = FMath::Clamp(VoxelConsole::CVarTestAreaRadius.GetValueOnGameThread(), 0, 16);
    FVoxelMeshOptions Options;
    Options.bGreedy = VoxelConsole::CVarGreedyMeshing.GetValueOnGameThread();
    Options.bAmbientOcclusion = VoxelConsole::CVarAmbientOcclusion.GetValueOnGameThread();
    const TSharedRef<const FVoxelBlockTable> SharedTable = Table.ToSharedRef();
    const int64 SeedValue = Seed.Value;
    const double StartSeconds = FPlatformTime::Seconds();
    UE_LOG(LogUnnamedVoxel, Display, TEXT("Building %d x %d columns, seed %s"), 2 * Radius + 1, 2 * Radius + 1,
        *Seed.Describe());
    BuildTask = UE::Tasks::Launch(UE_SOURCE_LOCATION,
        [SharedTable, SeedValue, Radius, Options, Serial, StartSeconds]
        {
            TSharedPtr<FBuild> Build = MakeShared<FBuild>();
            Build->Serial = Serial;
            Build->StartSeconds = StartSeconds;
            FVoxelArea::Build(*SharedTable, SeedValue, FIntPoint::ZeroValue, Radius, Options, true, Build->Area);
            // One stream set and set of Lumen cards per column, merging its sections (UVoxelChunkComponent). Both are
            // plain data, so they are built here too; the game thread only hands them over.
            const double StreamStart = FPlatformTime::Seconds();
            TArray<TArray<const FVoxelMeshData*>> Sections;
            Sections.SetNum(Build->Area.Columns.Num());
            TArray<uint32> SectionMasks;
            SectionMasks.SetNumZeroed(Build->Area.Columns.Num());
            for (const FVoxelArea::FSectionMesh& Section : Build->Area.Meshes)
            {
                Sections[Section.Column].Add(&Section.Mesh);
                SectionMasks[Section.Column] |= 1u << Section.Section;
            }
            for (int32 Column = 0; Column < Sections.Num(); ++Column)
                if (!Sections[Column].IsEmpty())
                {
                    FBuild::FColumnMesh& Mesh = Build->ColumnMeshes.AddDefaulted_GetRef();
                    Mesh.Column = Column;
                    Mesh.SectionMask = SectionMasks[Column];
                }
            ParallelFor(TEXT("VoxelWorld.Streams"), Build->ColumnMeshes.Num(), 1,
                [&Build, &Sections](int32 Index)
                {
                    FBuild::FColumnMesh& Mesh = Build->ColumnMeshes[Index];
                    UVoxelChunkComponent::BuildStreams(Sections[Mesh.Column], Mesh.Streams);
                    const double CardsStart = FPlatformTime::Seconds();
                    UVoxelChunkComponent::BuildCards(Sections[Mesh.Column], Mesh.Cards);
                    Mesh.CardsMs = (FPlatformTime::Seconds() - CardsStart) * 1000.;
                    Mesh.LayerMask = UVoxelChunkComponent::GetLayerMask(Sections[Mesh.Column]);
                });
            Build->StreamsMs = (FPlatformTime::Seconds() - StreamStart) * 1000.;
            for (const FBuild::FColumnMesh& Mesh : Build->ColumnMeshes)
            {
                Build->CardCount += Mesh.Cards.CardBuildData.Num();
                Build->MostCards = FMath::Max(Build->MostCards, Mesh.Cards.CardBuildData.Num());
                Build->CardsMs += Mesh.CardsMs;
                Build->WorstCardsMs = FMath::Max(Build->WorstCardsMs, Mesh.CardsMs);
            }
            return Build;
        });
}

void AVoxelWorld::FinishBuild(const TSharedPtr<FBuild>& Build)
{
    Columns.Reset();
    for (const TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>& Column : Build->Area.Columns)
        Columns.Add(Column->GetPosition(), Column);
    Applying = Build;
    // Collision exists from now on, so the player can stand on the ground even before every mesh is up.
    ReturnPlayerToSpawn();
}

void AVoxelWorld::ApplyColumns(double BudgetSeconds)
{
    SCOPE_CYCLE_COUNTER(STAT_VoxelApplyColumns);
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelWorld_ApplyColumns);
    FBuild& Build = *Applying;
    const double Start = FPlatformTime::Seconds();
    // At least one column per frame, then as many as fit in the budget.
    do
    {
        FBuild::FColumnMesh& Mesh = Build.ColumnMeshes[Build.NextColumn];
        GetChunk(Build.Area.Columns[Mesh.Column]->GetPosition())
            ->SetMesh(MoveTemp(Mesh.Streams), MoveTemp(Mesh.Cards), Mesh.LayerMask, Mesh.SectionMask);
        ++Build.NextColumn;
    } while (Build.NextColumn < Build.ColumnMeshes.Num() && FPlatformTime::Seconds() - Start < BudgetSeconds);
    const double FrameMs = (FPlatformTime::Seconds() - Start) * 1000.;
    Build.ApplyMs += FrameMs;
    Build.WorstFrameMs = FMath::Max(Build.WorstFrameMs, FrameMs);
    ++Build.ApplyFrames;
    if (Build.NextColumn < Build.ColumnMeshes.Num()) return;

    BuildReport = FString::Printf(TEXT("Seed %s\n"), *Seed.Describe()) + Build.Area.Describe() +
        FString::Printf(TEXT("\n  Stream sets and Lumen cards: %d columns in %.1f ms (wall, all workers)\n"),
            Build.ColumnMeshes.Num(), Build.StreamsMs) +
        FString::Printf(
            TEXT("  Lumen cards: %d (%.1f per column, most %d of %d), mean %.3f ms per column, worst %.3f ms\n"),
            Build.CardCount, Build.CardCount / FMath::Max(1., double(Build.ColumnMeshes.Num())), Build.MostCards,
            UVoxelChunkComponent::MaxCards, Build.CardsMs / FMath::Max(1, Build.ColumnMeshes.Num()),
            Build.WorstCardsMs) +
        FString::Printf(TEXT("  Apply: %d columns in %.2f ms over %d frames, worst frame %.2f ms (budget %.1f ms)\n"),
            Build.ColumnMeshes.Num(), Build.ApplyMs, Build.ApplyFrames, Build.WorstFrameMs,
            VoxelConsole::CVarApplyBudgetMs.GetValueOnGameThread()) +
        FString::Printf(TEXT("  Start to last section on screen: %.0f ms; %d chunk components hold %d sections"),
            (FPlatformTime::Seconds() - Build.StartSeconds) * 1000., Chunks.Num(), GetSectionMeshCount());
    TArray<FString> Lines;
    BuildReport.ParseIntoArrayLines(Lines);
    for (const FString& Line : Lines)
        UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Line);
    Applying.Reset();
    bReady = true;
}

UVoxelChunkComponent* AVoxelWorld::GetChunk(FIntPoint Column)
{
    if (TObjectPtr<UVoxelChunkComponent>* Found = Chunks.Find(Column)) return *Found;
    UVoxelChunkComponent* Chunk = NewObject<UVoxelChunkComponent>(this);
    Chunk->SetupAttachment(GetRootComponent());
    Chunk->RegisterComponent();
    Chunk->SetWorldLocation(OriginCm + FVector(Column.X, Column.Y, 0) * VoxelConstants::ColumnSize * CellCm);
    TArray<UMaterialInterface*> Materials;
    for (UMaterialInterface* Instance : LayerMaterials)
        Materials.Add(Instance);
    Chunk->Setup(Materials);
    Chunks.Add(Column, Chunk);
    return Chunk;
}

int32 AVoxelWorld::GetSectionMeshCount() const
{
    int32 Count = 0;
    for (const TPair<FIntPoint, TObjectPtr<UVoxelChunkComponent>>& Chunk : Chunks)
        if (Chunk.Value) Count += Chunk.Value->GetSectionMeshCount();
    return Count;
}

void AVoxelWorld::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (BuildTask.IsValid() && BuildTask.IsCompleted())
    {
        const TSharedPtr<FBuild> Build = BuildTask.GetResult();
        BuildTask = {};
        // A regenerate during the build already started a newer one.
        if (Build && Build->Serial == BuildSerial) FinishBuild(Build);
    }
    if (Applying && Applying->ColumnMeshes.IsEmpty())
    {
        Applying.Reset();
        bReady = true;
    }
    else if (Applying)
        ApplyColumns(VoxelConsole::CVarApplyBudgetMs.GetValueOnGameThread() / 1000.);
    if (TextureTable)
        TextureTable->SetTick(FMath::FloorToInt64(GetWorld()->GetTimeSeconds() * FoundationBlockFaces::TicksPerSecond));
    if (!ShotFolder.IsEmpty() && bReady) UpdateShots();
}

uint16 AVoxelWorld::GetState(const FIntVector& Cell) const
{
    const FIntPoint Column(FoundationGrid::FloorDivide(Cell.X, VoxelConstants::ColumnSize),
        FoundationGrid::FloorDivide(Cell.Y, VoxelConstants::ColumnSize));
    const TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>* Found = Columns.Find(Column);
    if (!Found) return 0;
    return (*Found)->GetState(
        Cell.X - Column.X * VoxelConstants::ColumnSize, Cell.Y - Column.Y * VoxelConstants::ColumnSize, Cell.Z);
}

void AVoxelWorld::GatherCollision(const FBox& Region, TArray<FBox>& Out) const
{
    Super::GatherCollision(Region, Out);
    if (!Table) return;
    const FIntVector Low = FoundationGrid::CellAt(Region.Min, OriginCm);
    const FIntVector High = FoundationGrid::CellAt(Region.Max, OriginCm);
    for (int32 Z = FMath::Max(Low.Z, VoxelConstants::MinZ); Z <= FMath::Min(High.Z, VoxelConstants::MaxZ); ++Z)
        for (int32 Y = Low.Y; Y <= High.Y; ++Y)
            for (int32 X = Low.X; X <= High.X; ++X)
            {
                const FVoxelStateInfo& Info = Table->Get(GetState({X, Y, Z}));
                if (!Info.bSolid) continue;
                const FVector Min = FoundationGrid::CellMin({X, Y, Z}, OriginCm);
                const double Bottom = Info.Shape == EVoxelShape::SlabTop ? .5 : 0.;
                const double Top = Info.Shape == EVoxelShape::SlabBottom ? .5 : 1.;
                Out.Add(FBox(Min + FVector(0, 0, Bottom * CellCm), Min + FVector(CellCm, CellCm, Top * CellCm)));
            }
}

FIntVector AVoxelWorld::GetSpawnCell() const
{
    if (SpawnCell) return *SpawnCell;
    // Rings outward from the origin; the first column whose top solid block is above the sea wins.
    const int32 Reach = FMath::Max(8, VoxelConsole::CVarTestAreaRadius.GetValueOnGameThread() * 16);
    for (int32 Ring = 0; Ring <= Reach && Terrain; ++Ring)
        for (int32 Y = -Ring; Y <= Ring; ++Y)
            for (int32 X = -Ring; X <= Ring; ++X)
            {
                if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Ring) continue;
                const int32 Surface = Terrain->GetSurfaceHeight(X, Y);
                if (Surface < VoxelConstants::SeaLevel) continue;
                SpawnCell = FIntVector(X, Y, Surface + 1);
                return *SpawnCell;
            }
    // All sea: stand on the water surface's floor at the origin.
    SpawnCell = FIntVector(0, 0, Terrain ? Terrain->GetSurfaceHeight(0, 0) + 1 : VoxelConstants::SeaLevel);
    return *SpawnCell;
}

FVector AVoxelWorld::GetSafeSpawn(const UFoundationProfile& Profile) const
{
    const FIntVector Cell = GetSpawnCell();
    return FoundationGrid::CellMin(Cell, OriginCm) + FVector(CellCm / 2, CellCm / 2, 0);
}

double AVoxelWorld::GetFallOutZ() const
{
    // The reference's void damage starts 64 blocks below the world.
    return OriginCm.Z + (VoxelConstants::MinZ - 64) * CellCm;
}

void AVoxelWorld::ApplyLogicalOffset(const FVector& Offset)
{
    Super::ApplyLogicalOffset(Offset);
    OriginCm += Offset;
}

void AVoxelWorld::ReturnPlayerToSpawn() const
{
    AFoundationPawn* Pawn = Cast<AFoundationPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (Pawn && Pawn->GetLab() == this) Pawn->Respawn();
}

void AVoxelWorld::UpdateShots()
{
    APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
    if (!Controller) return;
    // Framing from the built area: its centre, its highest surface and the spawn.
    const int32 Radius = FMath::Clamp(VoxelConsole::CVarTestAreaRadius.GetValueOnGameThread(), 0, 16);
    int32 Highest = VoxelConstants::SeaLevel;
    FIntPoint Peak = FIntPoint::ZeroValue;
    for (const TPair<FIntPoint, TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>>& Pair : Columns)
        for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
            for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
            {
                const int32 Height = Pair.Value->GetHeight(EVoxelHeightmap::WorldSurface, X, Y);
                if (Height <= Highest) continue;
                Highest = Height;
                Peak = Pair.Key * VoxelConstants::ColumnSize + FIntPoint(X, Y);
            }
    auto Blocks = [this](double X, double Y, double Z) { return OriginCm + FVector(X, Y, Z) * CellCm; };
    const double Edge = Radius * VoxelConstants::ColumnSize + 8, Middle = VoxelConstants::ColumnSize / 2.;
    const FIntVector Spawn = GetSpawnCell();
    const FShot Shots[] = {
        {TEXT("overview"), Blocks(-Edge - 30, -Edge - 30, Highest + 60), Blocks(Middle, Middle, Highest * .5), false},
        {TEXT("closeup"), Blocks(Peak.X - 28, Peak.Y - 34, Highest + 6), Blocks(Peak.X, Peak.Y, Highest - 12), false},
        {TEXT("ground"), Blocks(Spawn.X + .5, Spawn.Y + .5, Spawn.Z + 1.62),
            Blocks(Peak.X, Peak.Y, FMath::Max(Spawn.Z + 1.62, Highest * .7)), false},
        {TEXT("lumen-scene"), Blocks(-Edge - 30, -Edge - 30, Highest + 60), Blocks(Middle, Middle, Highest * .5), true},
        // Outside the west edge, level with the middle of the world: the cut face from bedrock to the surface.
        {TEXT("section"), Blocks(-Edge - 150, Middle, 20), Blocks(Middle, Middle, 20), false},
    };

    if (ShotIndex == INDEX_NONE)
    {
        ShotIndex = 0;
        ShotFrames = 0;
        ShotCamera = GetWorld()->SpawnActor<ACameraActor>();
        ShotCamera->GetCameraComponent()->SetFieldOfView(75);
        ShotCamera->GetCameraComponent()->bConstrainAspectRatio = false;
        Controller->SetViewTarget(ShotCamera);
        IFileManager::Get().MakeDirectory(*ShotFolder, true);
    }
    if (ShotIndex >= UE_ARRAY_COUNT(Shots)) return;
    const FShot& Shot = Shots[ShotIndex];
    const FString File = FPaths::Combine(ShotFolder, FString(Shot.Name) + TEXT(".png"));
    if (ShotFrames == 0)
    {
        ShotCamera->SetActorLocation(Shot.Location);
        ShotCamera->SetActorRotation((Shot.Target - Shot.Location).Rotation());
        Controller->SetViewTarget(ShotCamera);
        Controller->ConsoleCommand(Shot.bLumenScene ? TEXT("r.Lumen.Visualize 3") : TEXT("r.Lumen.Visualize 0"));
        Controller->ConsoleCommand(
            Shot.bLumenScene ? TEXT("ShowFlag.VisualizeLumen 1") : TEXT("ShowFlag.VisualizeLumen 0"));
    }
    ++ShotFrames;
    // Lighting cost: the GPU time of the settled frames just before the screenshot.
    if (ShotFrames > ShotSettleFrames - ShotTimedFrames && ShotFrames <= ShotSettleFrames)
        ShotGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
    if (ShotFrames == ShotSettleFrames) FScreenshotRequest::RequestScreenshot(File, false, false);
    const bool bWritten = ShotFrames > ShotSettleFrames + 2 && IFileManager::Get().FileExists(*File);
    if (!bWritten && ShotFrames < ShotSettleFrames + ShotWriteFrames) return;
    UE_LOG(LogUnnamedVoxel, Display, TEXT("Screenshot %s: %s"), bWritten ? TEXT("saved") : TEXT("missing"), *File);
    ShotTimings += FString::Printf(
        TEXT("%s%s %.2f"), ShotTimings.IsEmpty() ? TEXT("") : TEXT(", "), Shot.Name, ShotGpuMs / ShotTimedFrames);
    ShotGpuMs = 0;
    ++ShotIndex;
    ShotFrames = 0;
    if (ShotIndex < UE_ARRAY_COUNT(Shots)) return;
    // The player has been standing at spawn for every shot: on the block collision, its feet rest on the surface.
    FString Player = TEXT("  Player: none");
    if (const AFoundationPawn* Pawn = Cast<AFoundationPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        const FVector Expected = GetSafeSpawn(*GetDefault<UFoundationProfile>());
        Player = FString::Printf(TEXT("  Player: feet at %s, %s; spawn cell %s, %.1f cm from spawn"),
            *Pawn->Simulation.Feet.ToCompactString(), Pawn->Simulation.bGrounded ? TEXT("grounded") : TEXT("airborne"),
            *GetSpawnCell().ToString(), FVector::Dist(Pawn->Simulation.Feet, Expected));
    }
    const FIntPoint Size = GetWorld()->GetGameViewport() && GetWorld()->GetGameViewport()->Viewport
        ? GetWorld()->GetGameViewport()->Viewport->GetSizeXY()
        : FIntPoint::ZeroValue;
    const FString Gpu = FString::Printf(TEXT("  GPU frame per shot, ms (mean of %d settled frames at %d x %d): %s"),
        ShotTimedFrames, Size.X, Size.Y, *ShotTimings);
    const FString Lighting = DescribeShotLighting();
    UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Lighting);
    UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Gpu);
    UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Player);
    FFileHelper::SaveStringToFile(
        BuildReport + TEXT("\n") + Lighting + TEXT("\n") + Gpu + TEXT("\n") + Player + TEXT("\n"),
        *FPaths::Combine(ShotFolder, TEXT("build.txt")));
    FPlatformMisc::RequestExit(false, TEXT("VoxelShot"));
}
