#include "Voxel/VoxelWorld.h"
#include "Blocks/VoxelBlockTable.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Decoration/VoxelDecorator.h"
#include "DynamicRHI.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationBlockTextureTable.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationVitals.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "RenderUtils.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Terrain/VoxelFlatTerrain.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"
#include "UnrealClient.h"
#include "Voxel/VoxelBlockStorage.h"
#include "Voxel/VoxelBiomeDebug.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelConsole.h"
#include "Voxel/VoxelFlight.h"
#include "Voxel/VoxelLightShots.h"
#include "Voxel/VoxelWorldSettings.h"
#include "Voxel/VoxelWorldSubsystem.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

namespace
{
    constexpr double CellCm = UFoundationProfile::BlockCm;
    // Edits reach as far as the reference's world border, in blocks.
    constexpr int32 VoxelWorldLimit = 30000000;

    // The edge fog's own light in cd/m2, matched to the sky just above the horizon at the map's sun, in the flight's
    // screenshots: (184, 189, 184) in sRGB.
    const FLinearColor EdgeHazeLuminance(5900.f, 6500.f, 6000.f);
    // How far away the view radius looks to the sky's aerial perspective, as a multiple of its real distance at
    // radius 12; smaller radii scale it up, so the land is as hazy at the edge whatever the radius.
    constexpr double EdgeAerialScale = 50.;
    const TCHAR* const ChunkMaterialNames[] = {
        TEXT("M_ChunkOpaque"), TEXT("M_ChunkCutout"), TEXT("M_ChunkTranslucent"), TEXT("M_ChunkWater")};

    // Frames each screenshot waits for Lumen and the shadow cache to settle.
    constexpr int32 ShotSettleFrames = 150;
    // The last settle frames, whose GPU time the shot report averages.
    constexpr int32 ShotTimedFrames = 60;
    // Frames to wait for a requested screenshot file before moving on anyway.
    constexpr int32 ShotWriteFrames = 120;
    // Eye adaptation speed of the shot camera, in f-stops per second. The map adapts slowly (CreateGeneratedMap.py);
    // shots must land on the adapted exposure within the settle frames, even going from daylight into a cave.
    constexpr float ShotExposureSpeed = 50.f;

    struct FShot
    {
        const TCHAR* Name;
        FVector Location;
        FVector Target;
        bool bLumenScene;
        // The edge fog, for shots from inside the world; it would hide the world from outside.
        bool bFog;
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
    FVoxelSeed StartSeed = FVoxelSeed::Parse(UVoxelWorldSettings::Get().DevSeed);
    if (const FUnnamedSavedWorld* World = GetSavedWorld())
    {
        // The seed saved at creation; a blank text's random draw is never repeated.
        StartSeed.Value = World->Seed;
        StartSeed.Source = World->SeedText.TrimStartAndEnd().IsEmpty() ? EVoxelSeedSource::Random
                                                                       : FVoxelSeed::Parse(World->SeedText).Source;
    }
    FString SeedText;
    if (FParse::Value(FCommandLine::Get(), TEXT("VoxelSeed="), SeedText)) StartSeed = FVoxelSeed::Parse(SeedText);
    FParse::Value(FCommandLine::Get(), TEXT("VoxelShot="), ShotFolder);
    FString SpawnAt;
    // Not stopping at the comma between X and Y.
    if (FParse::Value(FCommandLine::Get(), TEXT("VoxelSpawnAt="), SpawnAt, false))
    {
        FString X, Y;
        if (SpawnAt.Split(TEXT(","), &X, &Y) && X.IsNumeric() && Y.IsNumeric())
            SpawnSearchCenter = FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y));
        else
            UE_LOG(LogUnnamedVoxel, Warning, TEXT("-VoxelSpawnAt=%s is not X,Y; the spawn search starts at the origin"),
                *SpawnAt);
    }
    bSpawnExact = FParse::Param(FCommandLine::Get(), TEXT("VoxelSpawnExact"));
    bShotEdits = FParse::Param(FCommandLine::Get(), TEXT("VoxelShotEdits"));
    bShotCaves = FParse::Param(FCommandLine::Get(), TEXT("VoxelShotCaves"));
    if (!ShotFolder.IsEmpty() && FParse::Param(FCommandLine::Get(), TEXT("VoxelShotLights")))
        LightShots = MakeShared<FVoxelLightShots>(ShotFolder);
    FString FlightFolder;
    if (FParse::Value(FCommandLine::Get(), TEXT("VoxelFlight="), FlightFolder))
    {
        double FlightSeconds = 0;
        FParse::Value(FCommandLine::Get(), TEXT("VoxelFlightSeconds="), FlightSeconds);
        Flight = MakeShared<FVoxelFlight>(FlightFolder, FlightSeconds);
    }
    for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
    {
        EdgeFog = It->GetComponent();
        break;
    }
    for (TActorIterator<ASkyAtmosphere> It(GetWorld()); It; ++It)
    {
        Sky = It->GetComponent();
        break;
    }
    // A plane a thousand kilometres wide under the world's bottom, for the fog to color below the horizon. Fogged
    // through, its own look never shows, so it stays out of shadows, Lumen and ray tracing.
    if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
    {
        FogFloor = NewObject<UStaticMeshComponent>(this, TEXT("FogFloor"));
        FogFloor->SetStaticMesh(Plane);
        FogFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        FogFloor->SetCanEverAffectNavigation(false);
        FogFloor->SetCastShadow(false);
        FogFloor->SetReceivesDecals(false);
        FogFloor->bVisibleInRayTracing = false;
        FogFloor->bAffectDynamicIndirectLighting = false;
        FogFloor->bAffectDistanceFieldLighting = false;
        FogFloor->SetupAttachment(GetRootComponent());
        FogFloor->RegisterComponent();
        FogFloor->SetWorldScale3D(FVector(1e6, 1e6, 1));
    }
    LoadMaterials();
    SetupAuthority();
    Regenerate(StartSeed);
    // The outline, cracks, held item and effects; the chunks draw the blocks.
    View = NewObject<UFoundationInteractionView>(this);
    View->bDrawBlocks = false;
    View->bLabFixtures = false;
    View->RegisterComponent();
}

const FUnnamedSavedWorld* AVoxelWorld::GetSavedWorld() const
{
    const UWorld* World = GetWorld();
    if (!bSavedWorldRead && World && World->IsGameWorld())
    {
        bSavedWorldRead = true;
        if (const TCHAR* Id = World->URL.GetOption(TEXT("VoxelWorld="), nullptr))
        {
            FUnnamedSavedWorld Saved;
            FString Error;
            if (UnnamedSavedWorlds::Load(Id, Saved, Error))
            {
                UE_LOG(LogUnnamedWorlds, Display, TEXT("Opened world %s (\"%s\", %s, %s, %s)"), *Saved.Id, *Saved.Name,
                    *Saved.Type, *Saved.GameMode, *Saved.Difficulty);
                SavedWorld = MoveTemp(Saved);
            }
            else
                UE_LOG(LogUnnamedWorlds, Error, TEXT("World %s could not be opened (%s); showing the test area."), Id,
                    *Error);
        }
    }
    return SavedWorld.GetPtrOrNull();
}

void AVoxelWorld::ConfigurePlayer(FFoundationVitals& Vitals) const
{
    const FUnnamedSavedWorld* World = GetSavedWorld();
    if (!World) return;
    // Hardcore plays as Survival on Hard (the store forces Hard); its permadeath needs a game-over screen first.
    Vitals.Mode =
        World->GameMode == TEXT("Creative") ? EFoundationPlayerMode::Creative : EFoundationPlayerMode::Survival;
    static_assert(int32(EFoundationDifficulty::Hard) == 3, "Difficulty names follow the enum");
    const TCHAR* const Difficulties[] = {TEXT("Peaceful"), TEXT("Easy"), TEXT("Normal"), TEXT("Hard")};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Difficulties); ++Index)
        if (World->Difficulty == Difficulties[Index]) Vitals.Difficulty = EFoundationDifficulty(Index);
    Vitals.bNaturalRegeneration = World->GetRule(TEXT("natural_health_regeneration"), true);
}

TSharedRef<const IVoxelTerrain> AVoxelWorld::MakeTerrain() const
{
    FString Error;
    if (TSharedPtr<const IVoxelTerrain> Made =
            VoxelTerrainFactory::Make(GetSavedWorld(), Seed.Value, *Table, VoxelWorldgen::GetReferenceRoot(), Error))
        return Made.ToSharedRef();
    // A saved world whose terrain cannot be built gets no other terrain: back to the title screen, with the reason in
    // the log (Create and Play refuse such a world before travelling, with the reason on screen). Until the travel, an
    // empty world generates nothing.
    UE_LOG(LogUnnamedVoxel, Error, TEXT("World %s cannot be played: %s. Returning to the title screen."),
        *GetSavedWorld()->Id, *Error);
    UGameplayStatics::OpenLevel(this, FName(UUnnamedProjectSettings::GetMenuMapPackage()));
    return MakeShared<FVoxelFlatTerrain>(TArray<FVoxelFlatLayer>(), *Table);
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
    Terrain = MakeTerrain();
    SpawnCell.Reset();
    bSpawned = false;
    UVoxelWorldSubsystem* Streaming = GetStreaming();
    if (!Streaming) return;
    UE_LOG(LogUnnamedVoxel, Display, TEXT("Streaming the world around the camera, seed %s"), *Seed.Describe());
    TArray<UMaterialInterface*> Materials;
    for (UMaterialInterface* Instance : LayerMaterials)
        Materials.Add(Instance);
    const FVector Spawn = GetSafeSpawn(*GetDefault<UFoundationProfile>());
    Streaming->Start(*this, Table.ToSharedRef(), Terrain.ToSharedRef(), Materials, Spawn);
    // A new session on a new world: nothing is loaded yet, so emptying the block world is free.
    Authority.World.OriginCm = OriginCm;
    Authority.Reset();
    // Shots fly the camera far out; the rings stay around the player.
    if (!ShotFolder.IsEmpty()) Streaming->SetFocusOverride(Spawn);
}

void AVoxelWorld::SetupAuthority()
{
    const UFoundationInteractionSettings& Settings = UFoundationInteractionSettings::Get();
    Settings.BuildRules(Authority.Rules);
    Authority.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
    Authority.World.OriginCm = OriginCm;
    Authority.World.BoundsMin = FIntVector(-VoxelWorldLimit, -VoxelWorldLimit, VoxelConstants::MinZ);
    Authority.World.BoundsMax = FIntVector(VoxelWorldLimit - 1, VoxelWorldLimit - 1, VoxelConstants::MaxZ);
    Authority.World.SetStorage(MakeUnique<FVoxelBlockStorage>(
        [Streaming = TWeakObjectPtr<UVoxelWorldSubsystem>(GetStreaming())]() -> FVoxelColumnPipeline*
        { return Streaming.IsValid() ? Streaming->GetPipeline() : nullptr; }));
    Authority.Options.PlacementPreset = Settings.DefaultPreset;
    // A saved world plays by its game mode; the map opened bare plays as the Playground's lab does.
    const FUnnamedSavedWorld* Saved = GetSavedWorld();
    const bool bCreative = Saved ? Saved->GameMode == TEXT("Creative") : Settings.bInfiniteSupplyDefault;
    Authority.Options.bInfiniteSupply = bCreative;
    Authority.Options.bToolWear = !bCreative;
    Authority.Options.bCreativeInventory = bCreative;
    // Survival starts with the loadout's tools and no blocks: blocks come from mining.
    Authority.Options.FiniteBlockCount = Saved ? 0 : Settings.FiniteBlockCount;
}

FFoundationTarget AVoxelWorld::TraceTarget(FVector Eye, FVector Direction, const UFoundationProfile& Profile) const
{
    return TraceAuthority(Authority, Eye, Direction);
}

bool AVoxelWorld::GetWaterBox(const FIntVector& Cell, FBox& OutBox) const
{
    if (!Table) return false;
    const FVoxelStateInfo& Info = Table->Get(GetState(Cell));
    if (!Info.IsFluid()) return false;
    const double Top = Table->Get(GetState(Cell + FIntVector(0, 0, 1))).IsFluid() ? 1. : Info.GetFluidHeight();
    const FVector Min = FoundationGrid::CellMin(Cell, OriginCm);
    OutBox = FBox(Min, Min + FVector(CellCm, CellCm, Top * CellCm));
    return true;
}

bool AVoxelWorld::IsInWater(FVector Point) const
{
    FBox Water;
    return GetWaterBox(FoundationGrid::CellAt(Point, OriginCm), Water) && Water.IsInside(Point);
}

bool AVoxelWorld::IsInWater(const FBox& Body) const
{
    // As the lab's water boxes: any water overlapping the body, shrunk a hair so touching a surface is not wading.
    const FBox Inner = Body.ExpandBy(-.1);
    const FIntVector Low = FoundationGrid::CellAt(Inner.Min, OriginCm),
                     High = FoundationGrid::CellAt(Inner.Max, OriginCm);
    for (int32 Z = Low.Z; Z <= High.Z; ++Z)
        for (int32 Y = Low.Y; Y <= High.Y; ++Y)
            for (int32 X = Low.X; X <= High.X; ++X)
            {
                FBox Water;
                if (GetWaterBox({X, Y, Z}, Water) && Water.Intersect(Inner)) return true;
            }
    return false;
}

UVoxelWorldSubsystem* AVoxelWorld::GetStreaming() const
{
    return UVoxelWorldSubsystem::Get(GetWorld());
}

void AVoxelWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UVoxelWorldSubsystem* Streaming = GetStreaming()) Streaming->Stop();
    Super::EndPlay(EndPlayReason);
}

bool AVoxelWorld::IsReady() const
{
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    return bSpawned && Streaming && Streaming->IsViewOnScreen();
}

FString AVoxelWorld::GetBuildReport() const
{
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    return FString::Printf(TEXT("Seed %s\n"), *Seed.Describe()) +
        (Streaming ? Streaming->GetFirstViewReport() : FString());
}

void AVoxelWorld::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (TextureTable)
        TextureTable->SetTick(FMath::FloorToInt64(GetWorld()->GetTimeSeconds() * FoundationBlockFaces::TicksPerSecond));
    // Collision exists once the columns around the spawn do: the player is put there then, once.
    if (!bSpawned && AreSpawnColumnsGenerated())
    {
        bSpawned = true;
        ReturnPlayerToSpawn();
    }
    UpdateEdgeFog();
    if (ShotEditStage == 1) ++ShotEditFrames;
    if (!ShotFolder.IsEmpty() && IsReady())
    {
        // With -VoxelShotEdits the shots wait until the edited columns are back on screen.
        if (bShotEdits && ShotEditStage == 0)
        {
            ApplyShotEdits();
            ShotEditStage = 1;
            ShotEditSeconds = FPlatformTime::Seconds();
        }
        else if (ShotEditStage == 1)
        {
            ShotEditStage = 2;
            ShotEditReport += FString::Printf(TEXT(", on screen after %.0f ms over %d frames"),
                (FPlatformTime::Seconds() - ShotEditSeconds) * 1000., ShotEditFrames);
            UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *ShotEditReport);
        }
        // With -VoxelShotLights the light probe runs first (FVoxelLightShots).
        else if (!LightShots || !LightShots->Tick(*this))
            UpdateShots();
    }
    if (Flight && (Flight->IsStarted() || IsReady()) && !Flight->Tick(*this, DeltaSeconds))
    {
        Flight.Reset();
        FPlatformMisc::RequestExit(false, TEXT("VoxelFlight"));
    }
}

void AVoxelWorld::UpdateEdgeFog()
{
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    if (FogFloor)
    {
        const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
        const FVector Camera = Controller && Controller->PlayerCameraManager
            ? Controller->PlayerCameraManager->GetCameraLocation()
            : OriginCm;
        FogFloor->SetWorldLocation(FVector(Camera.X, Camera.Y, OriginCm.Z + (VoxelConstants::MinZ - 1) * CellCm));
    }
    if (!EdgeFog || !Streaming || !Streaming->GetPipeline()) return;
    const FVoxelStreamingRings& Rings = Streaming->GetPipeline()->GetRings();
    if (Rings.ViewRadius == EdgeFogRadius) return;
    EdgeFogRadius = Rings.ViewRadius;
    constexpr double ColumnCm = VoxelConstants::ColumnSize * CellCm;
    // Clear until 1.5 columns before the view radius, then 99% opaque by it: the density (per 1000 cm, as the
    // engine counts it) times the fade's length is ln 100. Every block within the radius is drawn, so the fog
    // never shows a missing column. Almost no height falloff: the edge fades the same from a mountain top.
    constexpr double FadeCm = 1.5 * ColumnCm;
    EdgeFog->SetStartDistance(float(FMath::Max(0., Rings.ViewRadius * ColumnCm - FadeCm)));
    EdgeFog->SetFogDensity(float(1000. * FMath::Loge(100.) / FadeCm));
    EdgeFog->SetFogHeightFalloff(0.001f);
    EdgeFog->SetFogMaxOpacity(1.f);
    // Distant haze glows like the sky at the horizon, a pale neutral, rather than like the sky's average light,
    // which is the blue overhead: the land then fades into the horizon instead of into a darker band below it.
    // The sun side still brightens it (directional in-scattering).
    EdgeFog->SetSkyAtmosphereAmbientContributionColorScale(FLinearColor::Black);
    EdgeFog->SetFogInscatteringColor(EdgeHazeLuminance);
    if (Sky && bEdgeFogVisible)
        Sky->SetAerialPespectiveViewDistanceScale(float(EdgeAerialScale * 12. / FMath::Max(1, Rings.ViewRadius)));
    // Everything but the sky, which lies beyond any distance: the terrain and the fog floor take the fog's color,
    // the sky stays clear.
    EdgeFog->SetFogCutoffDistance(1e9f);
}

void AVoxelWorld::SetEdgeFogVisible(bool bVisible)
{
    bEdgeFogVisible = bVisible;
    if (EdgeFog) EdgeFog->SetVisibility(bVisible);
    if (FogFloor) FogFloor->SetVisibility(bVisible);
    // Shots from outside the world see it at the sky's own scale, as before the fog.
    if (Sky)
        Sky->SetAerialPespectiveViewDistanceScale(
            bVisible ? float(EdgeAerialScale * 12. / FMath::Max(1, EdgeFogRadius)) : 1.f);
}

uint16 AVoxelWorld::GetState(const FIntVector& Cell) const
{
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
    if (!Pipeline) return 0;
    const FIntPoint Column(FoundationGrid::FloorDivide(Cell.X, VoxelConstants::ColumnSize),
        FoundationGrid::FloorDivide(Cell.Y, VoxelConstants::ColumnSize));
    const FVoxelColumn* Data = Pipeline->FindColumn(Column);
    return Data ? Data->GetState(Cell.X - Column.X * VoxelConstants::ColumnSize,
                      Cell.Y - Column.Y * VoxelConstants::ColumnSize, Cell.Z)
                : 0;
}

int32 AVoxelWorld::GetBiome(const FIntVector& Cell) const
{
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
    if (!Pipeline) return INDEX_NONE;
    const FIntPoint Column(FoundationGrid::FloorDivide(Cell.X, VoxelConstants::ColumnSize),
        FoundationGrid::FloorDivide(Cell.Y, VoxelConstants::ColumnSize));
    const FVoxelColumn* Data = Pipeline->FindColumn(Column);
    return Data ? Data->GetBiome(Cell.X - Column.X * VoxelConstants::ColumnSize,
                      Cell.Y - Column.Y * VoxelConstants::ColumnSize, Cell.Z)
                : INDEX_NONE;
}

void AVoxelWorld::GatherCollision(const FBox& Region, TArray<FBox>& Out) const
{
    Super::GatherCollision(Region, Out);
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
    if (!Table || !Pipeline) return;
    const FIntVector Low = FoundationGrid::CellAt(Region.Min, OriginCm);
    const FIntVector High = FoundationGrid::CellAt(Region.Max, OriginCm);
    const int32 LowZ = FMath::Max(Low.Z, VoxelConstants::MinZ), HighZ = FMath::Min(High.Z, VoxelConstants::MaxZ);
    if (LowZ > HighZ) return;
    constexpr int32 Size = VoxelConstants::ColumnSize;
    for (int32 ColumnY = FoundationGrid::FloorDivide(Low.Y, Size); ColumnY <= FoundationGrid::FloorDivide(High.Y, Size);
        ++ColumnY)
        for (int32 ColumnX = FoundationGrid::FloorDivide(Low.X, Size);
            ColumnX <= FoundationGrid::FloorDivide(High.X, Size); ++ColumnX)
        {
            const int32 LowX = FMath::Max(Low.X, ColumnX * Size), HighX = FMath::Min(High.X, ColumnX * Size + Size - 1);
            const int32 LowY = FMath::Max(Low.Y, ColumnY * Size), HighY = FMath::Min(High.Y, ColumnY * Size + Size - 1);
            const FVoxelColumn* Column = Pipeline->FindColumn({ColumnX, ColumnY});
            if (!Column)
            {
                // Not generated yet: solid, so nothing walks or falls into it.
                Out.Add(FBox(FoundationGrid::CellMin({LowX, LowY, LowZ}, OriginCm),
                    FoundationGrid::CellMin({HighX + 1, HighY + 1, HighZ + 1}, OriginCm)));
                continue;
            }
            for (int32 Z = LowZ; Z <= HighZ; ++Z)
                for (int32 Y = LowY; Y <= HighY; ++Y)
                    for (int32 X = LowX; X <= HighX; ++X)
                    {
                        const FVoxelStateInfo& Info =
                            Table->Get(Column->GetState(X - ColumnX * Size, Y - ColumnY * Size, Z));
                        if (!Info.bSolid) continue;
                        const FVector Min = FoundationGrid::CellMin({X, Y, Z}, OriginCm);
                        const double Bottom = Info.Shape == EVoxelShape::SlabTop ? .5 : 0.;
                        // Snow layers collide one layer lower than they show (bSolid is off for a single layer).
                        const double Top = Info.Shape == EVoxelShape::SlabBottom ? .5
                            : Info.Shape == EVoxelShape::Layers                  ? (Info.Layers - 1) / 8.
                                                                                 : 1.;
                        Out.Add(
                            FBox(Min + FVector(0, 0, Bottom * CellCm), Min + FVector(CellCm, CellCm, Top * CellCm)));
                    }
        }
}

bool AVoxelWorld::AreSpawnColumnsGenerated() const
{
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
    if (!Pipeline || !Terrain) return false;
    const FIntVector Cell = GetSpawnCell();
    const FIntPoint Column(FoundationGrid::FloorDivide(Cell.X, VoxelConstants::ColumnSize),
        FoundationGrid::FloorDivide(Cell.Y, VoxelConstants::ColumnSize));
    for (int32 DY = -1; DY <= 1; ++DY)
        for (int32 DX = -1; DX <= 1; ++DX)
            if (!Pipeline->IsGenerated(Column + FIntPoint(DX, DY))) return false;
    return true;
}

FIntVector AVoxelWorld::GetSpawnCell() const
{
    if (SpawnCell) return *SpawnCell;
    // Rings outward from the search centre (the origin unless -VoxelSpawnAt says otherwise); the first column whose top
    // solid block is dry land wins. Where IsDry is an estimate, a limited number of candidates are confirmed against
    // the real column.
    const FIntPoint Center = SpawnSearchCenter;
    const int32 Reach = FMath::Max(8, VoxelConsole::CVarViewRadius.GetValueOnGameThread() * VoxelConstants::ColumnSize);
    // -VoxelSpawnExact confirms every column within 32 blocks instead (a few seconds at most).
    int32 Confirmations = bSpawnExact ? 65 * 65 : 64;
    for (int32 Ring = 0; Ring <= Reach && Terrain && Confirmations > 0; ++Ring)
        for (int32 Y = -Ring; Y <= Ring; ++Y)
            for (int32 X = -Ring; X <= Ring; ++X)
            {
                if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Ring ||
                    (!bSpawnExact && !Terrain->IsDry(Center.X + X, Center.Y + Y)))
                    continue;
                if (Confirmations <= 0 || (--Confirmations, !Terrain->ConfirmDry(Center.X + X, Center.Y + Y))) continue;
                SpawnCell =
                    FIntVector(Center.X + X, Center.Y + Y, Terrain->GetSurfaceHeight(Center.X + X, Center.Y + Y) + 1);
                return *SpawnCell;
            }
    // All sea: stand on the water surface's floor at the centre.
    SpawnCell = FIntVector(
        Center.X, Center.Y, Terrain ? Terrain->GetSurfaceHeight(Center.X, Center.Y) + 1 : VoxelConstants::SeaLevel);
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
    Authority.World.OriginCm = OriginCm;
}

void AVoxelWorld::ReturnPlayerToSpawn() const
{
    AFoundationPawn* Pawn = Cast<AFoundationPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (Pawn && Pawn->GetLab() == this) Pawn->Respawn();
}

void AVoxelWorld::ApplyShotEdits()
{
    UVoxelWorldSubsystem* Streaming = GetStreaming();
    const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
    FFoundationBlockWorld& World = Authority.World;
    const FFoundationBlockType* Stone = World.FindBlock(TEXT("stone"));
    const FFoundationBlockType* Glass = World.FindBlock(TEXT("glass"));
    const FFoundationBlockType* Glowstone = World.FindBlock(TEXT("glowstone"));
    if (!Pipeline || !Stone || !Glass || !Glowstone) return;
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const FIntVector Spawn = GetSpawnCell();
    // The column corner nearest the spawn's column whose pit and pillar stand on dry land, clear of the player, so
    // they read clearly; the corner east and south of the spawn's column when none within four columns does.
    const FIntPoint Home(FoundationGrid::FloorDivide(Spawn.X, Size), FoundationGrid::FloorDivide(Spawn.Y, Size));
    ShotEditCorner = (Home + FIntPoint(1, 1)) * Size;
    auto IsGoodCorner = [this, &Spawn](FIntPoint Corner)
    {
        if (FMath::Max(FMath::Abs(Corner.X - Spawn.X), FMath::Abs(Corner.Y - Spawn.Y)) < 8) return false;
        for (int32 Y = Corner.Y - 2; Y < Corner.Y + 6; ++Y)
            for (int32 X = Corner.X - 2; X < Corner.X + 2; ++X)
                if (!Terrain->IsDry(X, Y)) return false;
        return true;
    };
    bool bFound = false;
    // Rings of corners around the spawn's column: ring 1 is its own four.
    for (int32 Ring = 1; Ring <= 4 && !bFound; ++Ring)
        for (int32 DY = 1 - Ring; DY <= Ring && !bFound; ++DY)
            for (int32 DX = 1 - Ring; DX <= Ring && !bFound; ++DX)
            {
                const bool bOnRing = FMath::Max(FMath::Abs(DX - .5), FMath::Abs(DY - .5)) > Ring - 1;
                if (bOnRing && IsGoodCorner((Home + FIntPoint(DX, DY)) * Size))
                {
                    ShotEditCorner = (Home + FIntPoint(DX, DY)) * Size;
                    bFound = true;
                }
            }
    // The top non-air block at a world X, Y, from its column's heightmap.
    auto Surface = [Pipeline](int32 X, int32 Y)
    {
        const FIntPoint Column(FoundationGrid::FloorDivide(X, Size), FoundationGrid::FloorDivide(Y, Size));
        const FVoxelColumn* Data = Pipeline->FindColumn(Column);
        return Data ? Data->GetHeight(EVoxelHeightmap::WorldSurface, X - Column.X * Size, Y - Column.Y * Size)
                    : VoxelConstants::MinZ - 1;
    };
    TSet<FIntPoint> Columns;
    int32 Changed = 0;
    auto Write = [&World, &Columns, &Changed](const FIntVector& Cell, FFoundationBlockState State)
    {
        if (!World.IsKnown(Cell) || World.Get(Cell).State == State) return;
        World.Set(Cell, State);
        Columns.Add(FIntPoint(FoundationGrid::FloorDivide(Cell.X, Size), FoundationGrid::FloorDivide(Cell.Y, Size)));
        ++Changed;
    };
    // A pit four cells wide and ten deep, centred on the corner, so it opens into four columns.
    ShotEditTop = VoxelConstants::MinZ;
    for (int32 Y = ShotEditCorner.Y - 2; Y < ShotEditCorner.Y + 2; ++Y)
        for (int32 X = ShotEditCorner.X - 2; X < ShotEditCorner.X + 2; ++X)
        {
            const int32 Top = Surface(X, Y);
            ShotEditTop = FMath::Max(ShotEditTop, Top);
            for (int32 Z = Top - 9; Z <= Top; ++Z)
                Write({X, Y, Z}, FFoundationBlockState{});
        }
    // A pillar on the border between two columns, five cells south of the corner: stone and glass, glowstone on top.
    const int32 PillarY = ShotEditCorner.Y + 5, Base = Surface(ShotEditCorner.X, PillarY);
    for (int32 Level = 1; Level <= 6; ++Level)
        Write({ShotEditCorner.X, PillarY, Base + Level},
            (Level == 6         ? Glowstone
                    : Level % 2 ? Stone
                                : Glass)
                ->GetDefaultState());
    ShotEditReport = FString::Printf(TEXT("  Edits: %d cells in %d columns around the corner (%d, %d)"), Changed,
        Columns.Num(), ShotEditCorner.X, ShotEditCorner.Y);
}

const TArray<TTuple<FString, FVector, FVector>>& AVoxelWorld::GetCaveShotViews()
{
    if (!bShotCavesSearched) FindCaveShots();
    return ShotCaveViews;
}

void AVoxelWorld::FindCaveShots()
{
    bShotCavesSearched = true;
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
    if (!Pipeline || !Table) return;
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const FVoxelBlockTable& States = *Table;
    const uint16 Lava = States.FindState(TEXT("lava"));
    // Unloaded cells count as solid, so no view looks into a missing column.
    auto Get = [Pipeline](int32 X, int32 Y, int32 Z) -> uint16
    {
        if (Z < VoxelConstants::MinZ || Z > VoxelConstants::MaxZ) return 0;
        const FIntPoint Column(FoundationGrid::FloorDivide(X, Size), FoundationGrid::FloorDivide(Y, Size));
        const FVoxelColumn* Data = Pipeline->FindColumn(Column);
        return Data ? Data->GetState(X - Column.X * Size, Y - Column.Y * Size, Z) : uint16(1);
    };
    auto IsOpen = [&States, &Get](int32 X, int32 Y, int32 Z) { return !States.Get(Get(X, Y, Z)).bSolid; };
    auto Covered = [Pipeline](int32 X, int32 Y, int32 Z)
    {
        const FIntPoint Column(FoundationGrid::FloorDivide(X, Size), FoundationGrid::FloorDivide(Y, Size));
        const FVoxelColumn* Data = Pipeline->FindColumn(Column);
        return Data && Z < Data->GetHeight(EVoxelHeightmap::WorldSurface, X - Column.X * Size, Y - Column.Y * Size) - 3;
    };
    auto IsSkyOpen = [Pipeline](int32 X, int32 Y, int32 Z)
    {
        const FIntPoint Column(FoundationGrid::FloorDivide(X, Size), FoundationGrid::FloorDivide(Y, Size));
        const FVoxelColumn* Data = Pipeline->FindColumn(Column);
        return Data && Z > Data->GetHeight(EVoxelHeightmap::WorldSurface, X - Column.X * Size, Y - Column.Y * Size);
    };
    const FIntPoint Directions[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    auto Blocks = [this](double X, double Y, double Z) { return OriginCm + FVector(X, Y, Z) * CellCm; };
    const FIntPoint Center = Pipeline->GetRings().Center;

    // The cave: the covered, floored spot with headroom and the longest straight open run ahead (up to 40 blocks).
    int32 BestRun = 0;
    FIntVector BestCell = FIntVector::ZeroValue;
    FIntPoint BestDirection = FIntPoint::ZeroValue;
    // Lava: the first covered lava surface with open air above it seen from 3 to 6 blocks away.
    bool bLava = false;
    FIntVector LavaCell = FIntVector::ZeroValue, LavaCamera = FIntVector::ZeroValue;
    for (int32 CY = Center.Y - 7; CY <= Center.Y + 7; ++CY)
        for (int32 CX = Center.X - 7; CX <= Center.X + 7; ++CX)
            for (int32 LocalY = 0; LocalY < Size; LocalY += 2)
                for (int32 LocalX = 0; LocalX < Size; LocalX += 2)
                {
                    const int32 X = CX * Size + LocalX, Y = CY * Size + LocalY;
                    for (int32 Z = VoxelConstants::MinZ + 6; Z < VoxelConstants::MaxZ - 2; ++Z)
                    {
                        if (!Covered(X, Y, Z)) break;
                        const uint16 State = Get(X, Y, Z);
                        if (!bLava && Lava && State == Lava && Get(X, Y, Z + 1) == 0)
                            for (const FIntPoint& Direction : Directions)
                                for (int32 Away = 6; Away >= 3 && !bLava; --Away)
                                {
                                    bool bClear = true;
                                    for (int32 Step = 1; Step <= Away && bClear; ++Step)
                                        bClear = Get(X + Direction.X * Step, Y + Direction.Y * Step, Z + 1) == 0 &&
                                            Get(X + Direction.X * Step, Y + Direction.Y * Step, Z + 2) == 0;
                                    if (!bClear) continue;
                                    bLava = true;
                                    LavaCell = FIntVector(X, Y, Z);
                                    LavaCamera = FIntVector(X + Direction.X * Away, Y + Direction.Y * Away, Z + 2);
                                }
                        if (State != 0 || States.Get(Get(X, Y, Z - 1)).bSolid == false || !IsOpen(X, Y, Z + 1) ||
                            !IsOpen(X, Y, Z + 2))
                            continue;
                        for (const FIntPoint& Direction : Directions)
                        {
                            // A run that reaches a cell under open sky lets daylight in: those views come first.
                            int32 Run = 0;
                            bool bSky = false;
                            while (Run < 40 && IsOpen(X + Direction.X * (Run + 1), Y + Direction.Y * (Run + 1), Z) &&
                                IsOpen(X + Direction.X * (Run + 1), Y + Direction.Y * (Run + 1), Z + 1))
                            {
                                ++Run;
                                bSky |= IsSkyOpen(X + Direction.X * Run, Y + Direction.Y * Run, Z + 1);
                            }
                            const int32 Score = Run + (bSky ? 100 : 0);
                            if (Score > BestRun)
                            {
                                BestRun = Score;
                                BestCell = FIntVector(X, Y, Z);
                                BestDirection = Direction;
                            }
                        }
                    }
                }
    // The cave wall (S3; scout E's ore look calls need it): a covered camera with headroom facing a wall 3 to 10 blocks
    // away, scored by the ores (ids ending in _ore) on the wall's 9 x 5 face (eye level down to the floor's top, not
    // the floor), doubled when an opening to the sky lies within 16 blocks of the camera (daylight on the stone).
    TSet<uint16> Ores;
    if (const FVoxelDecorator* Decorator = Pipeline->GetDecorator())
        for (const TPair<uint16, FString>& Ore : Decorator->GetOreStates())
            if (Ore.Value.EndsWith(TEXT("_ore"))) Ores.Add(Ore.Key);
    int32 BestWall = 0, WallOres = 0, WallDistance = 0;
    bool bWallLit = false;
    FIntVector WallCell = FIntVector::ZeroValue;
    FIntPoint WallDirection = FIntPoint::ZeroValue;
    if (!Ores.IsEmpty())
        for (int32 CY = Center.Y - 6; CY <= Center.Y + 6; ++CY)
            for (int32 CX = Center.X - 6; CX <= Center.X + 6; ++CX)
                for (int32 LocalY = 0; LocalY < Size; LocalY += 2)
                    for (int32 LocalX = 0; LocalX < Size; LocalX += 2)
                    {
                        const int32 X = CX * Size + LocalX, Y = CY * Size + LocalY;
                        for (int32 Z = VoxelConstants::MinZ + 6; Z < VoxelConstants::MaxZ - 2; ++Z)
                        {
                            if (!Covered(X, Y, Z)) break;
                            if (Get(X, Y, Z) != 0 || !States.Get(Get(X, Y, Z - 1)).bSolid || !IsOpen(X, Y, Z + 1))
                                continue;
                            // Daylight: an open run at eye height reaching a cell under the sky.
                            bool bLit = false;
                            for (const FIntPoint& Direction : Directions)
                                for (int32 Step = 1; Step <= 16 && !bLit; ++Step)
                                {
                                    const int32 SX = X + Direction.X * Step, SY = Y + Direction.Y * Step;
                                    if (!IsOpen(SX, SY, Z + 1)) break;
                                    bLit = IsSkyOpen(SX, SY, Z + 1);
                                }
                            for (const FIntPoint& Direction : Directions)
                            {
                                int32 Distance = 1;
                                while (Distance <= 10 &&
                                    IsOpen(X + Direction.X * Distance, Y + Direction.Y * Distance, Z + 1))
                                    ++Distance;
                                if (Distance < 3 || Distance > 10) continue;
                                // The first solid cell of each line of sight across the face.
                                const FIntPoint Across(-Direction.Y, Direction.X);
                                int32 Found = 0;
                                for (int32 U = -4; U <= 4; ++U)
                                    for (int32 V = -1; V <= 3; ++V)
                                        for (int32 Depth = Distance - 2; Depth <= Distance + 2; ++Depth)
                                        {
                                            const int32 WX = X + Direction.X * Depth + Across.X * U,
                                                        WY = Y + Direction.Y * Depth + Across.Y * U, WZ = Z + 1 + V;
                                            const uint16 State = Get(WX, WY, WZ);
                                            if (!States.Get(State).bSolid) continue;
                                            Found += Ores.Contains(State);
                                            break;
                                        }
                                const int32 Score = Found * (bLit ? 2 : 1);
                                if (Found >= 2 && Score > BestWall)
                                {
                                    BestWall = Score;
                                    WallOres = Found;
                                    WallDistance = Distance;
                                    bWallLit = bLit;
                                    WallCell = FIntVector(X, Y, Z);
                                    WallDirection = Direction;
                                }
                            }
                        }
                    }
    if (BestWall)
        ShotCaveViews.Emplace(TEXT("cave-wall"), Blocks(WallCell.X + .5, WallCell.Y + .5, WallCell.Z + 1.6),
            Blocks(WallCell.X + .5 + WallDirection.X * WallDistance, WallCell.Y + .5 + WallDirection.Y * WallDistance,
                WallCell.Z + 1.5));
    UE_LOG(LogUnnamedVoxel, Display, TEXT("Cave wall shot: %s (%d ores on the face, %d blocks away%s, at %s)"),
        BestWall ? TEXT("found") : TEXT("not found"), WallOres, WallDistance, bWallLit ? TEXT(", daylight") : TEXT(""),
        *WallCell.ToString());

    const int32 Length = BestRun % 100;
    if (Length >= 8)
        ShotCaveViews.Emplace(TEXT("cave"), Blocks(BestCell.X + .5, BestCell.Y + .5, BestCell.Z + 1.5),
            Blocks(BestCell.X + .5 + BestDirection.X * Length, BestCell.Y + .5 + BestDirection.Y * Length,
                BestCell.Z + .8));
    if (bLava)
        ShotCaveViews.Emplace(TEXT("cave-lava"), Blocks(LavaCamera.X + .5, LavaCamera.Y + .5, LavaCamera.Z + .6),
            Blocks(LavaCell.X + .5, LavaCell.Y + .5, LavaCell.Z + .9));
    UE_LOG(LogUnnamedVoxel, Display, TEXT("Cave shots: cave %s (run %d%s at %s), lava %s (at %s)"),
        Length >= 8 ? TEXT("found") : TEXT("not found"), Length, BestRun >= 100 ? TEXT(" to open sky") : TEXT(""),
        *BestCell.ToString(), bLava ? TEXT("found") : TEXT("not found"), *LavaCell.ToString());
}

void AVoxelWorld::UpdateShots()
{
    APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
    if (!Controller) return;
    // Framing from the streamed area around the player: its center column, the highest surface near it (the
    // closeup stays as close as phase 2's area was) and the spawn. The Min()s only matter for land far below sea
    // level, such as Superflat's; the height terrain frames as it always has.
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
    if (!Pipeline) return;
    const FVoxelStreamingRings& Rings = Pipeline->GetRings();
    // The drawn area reaches one column past the view radius on each axis.
    const int32 Radius = Rings.ViewRadius + 1;
    int32 Highest = VoxelConstants::MinZ - 1;
    FIntPoint Peak = FIntPoint::ZeroValue;
    Pipeline->ForEachColumn(
        [&Highest, &Peak, &Rings](FIntPoint Position, const FVoxelColumn& Column)
        {
            if (FMath::Abs(Position.X - Rings.Center.X) > 4 || FMath::Abs(Position.Y - Rings.Center.Y) > 4) return;
            for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
                for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                {
                    const int32 Height = Column.GetHeight(EVoxelHeightmap::WorldSurface, X, Y);
                    if (Height <= Highest) continue;
                    Highest = Height;
                    Peak = Position * VoxelConstants::ColumnSize + FIntPoint(X, Y);
                }
        });
    auto Blocks = [this](double X, double Y, double Z) { return OriginCm + FVector(X, Y, Z) * CellCm; };
    const double Edge = Radius * VoxelConstants::ColumnSize + 8;
    const double MiddleX = Rings.Center.X * VoxelConstants::ColumnSize + VoxelConstants::ColumnSize / 2.;
    const double MiddleY = Rings.Center.Y * VoxelConstants::ColumnSize + VoxelConstants::ColumnSize / 2.;
    const FIntVector Spawn = GetSpawnCell();
    const double OverviewZ = FMath::Min(Highest * .5, Highest - 30.),
                 SectionZ = FMath::Min(20., (VoxelConstants::MinZ + Highest) / 2.);
    TArray<FShot, TInlineAllocator<6>> Shots = {
        {TEXT("overview"), Blocks(MiddleX - Edge - 30, MiddleY - Edge - 30, Highest + 60),
            Blocks(MiddleX, MiddleY, OverviewZ), false, false},
        {TEXT("closeup"), Blocks(Peak.X - 28, Peak.Y - 34, Highest + 6), Blocks(Peak.X, Peak.Y, Highest - 12), false,
            true},
        {TEXT("ground"), Blocks(Spawn.X + .5, Spawn.Y + .5, Spawn.Z + 1.62),
            Blocks(Peak.X, Peak.Y, FMath::Max(Spawn.Z + 1.62, FMath::Min(Highest * .7, Highest - 20.))), false, true},
        {TEXT("lumen-scene"), Blocks(MiddleX - Edge - 30, MiddleY - Edge - 30, Highest + 60),
            Blocks(MiddleX, MiddleY, OverviewZ), true, false},
        // Outside the west edge, level with the middle of the world: the cut face from bedrock to the surface.
        {TEXT("section"), Blocks(MiddleX - Edge - 150, MiddleY, SectionZ), Blocks(MiddleX, MiddleY, SectionZ), false,
            false},
    };
    // Looking down into the pit across the column corner, the pillar beyond it.
    if (bShotEdits)
        Shots.Add({TEXT("edits"), Blocks(ShotEditCorner.X - 7, ShotEditCorner.Y - 7, ShotEditTop + 8),
            Blocks(ShotEditCorner.X + 1, ShotEditCorner.Y + 2, ShotEditTop - 5), false, true});

    if (bShotCaves && !bShotCavesSearched) FindCaveShots();
    for (const TTuple<FString, FVector, FVector>& CaveView : ShotCaveViews)
        Shots.Add({*CaveView.Get<0>(), CaveView.Get<1>(), CaveView.Get<2>(), false, true});

    if (ShotIndex == INDEX_NONE)
    {
        ShotIndex = 0;
        ShotFrames = 0;
        ShotCamera = GetWorld()->SpawnActor<ACameraActor>();
        ShotCamera->GetCameraComponent()->SetFieldOfView(75);
        ShotCamera->GetCameraComponent()->bConstrainAspectRatio = false;
        FPostProcessSettings& Post = ShotCamera->GetCameraComponent()->PostProcessSettings;
        Post.bOverride_AutoExposureSpeedUp = Post.bOverride_AutoExposureSpeedDown = true;
        Post.AutoExposureSpeedUp = Post.AutoExposureSpeedDown = ShotExposureSpeed;
        Controller->SetViewTarget(ShotCamera);
        IFileManager::Get().MakeDirectory(*ShotFolder, true);
    }
    if (ShotIndex >= Shots.Num()) return;
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
        SetEdgeFogVisible(Shot.bFog);
    }
    ++ShotFrames;
    // Lighting cost: the GPU time of the settled frames just before the screenshot.
    if (ShotFrames > ShotSettleFrames - ShotTimedFrames && ShotFrames <= ShotSettleFrames)
        ShotGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
    // The UI stays out of shots, except the biome debug views when one is on.
    if (ShotFrames == ShotSettleFrames)
        FScreenshotRequest::RequestScreenshot(File, VoxelBiomeDebug::IsOverlayOn(), false);
    const bool bWritten = ShotFrames > ShotSettleFrames + 2 && IFileManager::Get().FileExists(*File);
    if (!bWritten && ShotFrames < ShotSettleFrames + ShotWriteFrames) return;
    UE_LOG(LogUnnamedVoxel, Display, TEXT("Screenshot %s: %s"), bWritten ? TEXT("saved") : TEXT("missing"), *File);
    ShotTimings += FString::Printf(
        TEXT("%s%s %.2f"), ShotTimings.IsEmpty() ? TEXT("") : TEXT(", "), Shot.Name, ShotGpuMs / ShotTimedFrames);
    ShotGpuMs = 0;
    ++ShotIndex;
    ShotFrames = 0;
    if (ShotIndex < Shots.Num()) return;
    // The player has been standing at spawn for every shot: on the block collision, its feet rest on the surface.
    FString Player = TEXT("  Player: none");
    if (const AFoundationPawn* Pawn = Cast<AFoundationPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        const FVector Expected = GetSafeSpawn(*GetDefault<UFoundationProfile>());
        // Vitals.Describe() starts with the mode and difficulty.
        FString Rules = Pawn->Vitals.Describe();
        Rules.Split(TEXT(" regen="), &Rules, nullptr);
        Player = FString::Printf(TEXT("  Player: feet at %s, %s; %s; spawn cell %s, %.1f cm from spawn"),
            *Pawn->Simulation.Feet.ToCompactString(), Pawn->Simulation.bGrounded ? TEXT("grounded") : TEXT("airborne"),
            *Rules, *GetSpawnCell().ToString(), FVector::Dist(Pawn->Simulation.Feet, Expected));
    }
    const FUnnamedSavedWorld* Saved = GetSavedWorld();
    const FString World = Saved ? FString::Printf(TEXT("  World: %s \"%s\", %s, %s, %s"), *Saved->Id, *Saved->Name,
                                      *Saved->Type, *Saved->GameMode, *Saved->Difficulty)
                                : FString(TEXT("  World: none (the dev seed's test area)"));
    const FIntPoint Size = GetWorld()->GetGameViewport() && GetWorld()->GetGameViewport()->Viewport
        ? GetWorld()->GetGameViewport()->Viewport->GetSizeXY()
        : FIntPoint::ZeroValue;
    const FString Gpu = FString::Printf(TEXT("  GPU frame per shot, ms (mean of %d settled frames at %d x %d): %s"),
        ShotTimedFrames, Size.X, Size.Y, *ShotTimings);
    const FString Lighting = DescribeShotLighting();
    UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Lighting);
    UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Gpu);
    UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *World);
    UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Player);
    const FString Edits = ShotEditReport.IsEmpty() ? FString() : ShotEditReport + TEXT("\n");
    FFileHelper::SaveStringToFile(GetBuildReport() + TEXT("\n") + Edits + Lighting + TEXT("\n") + Gpu + TEXT("\n") +
            World + TEXT("\n") + Player + TEXT("\n"),
        *FPaths::Combine(ShotFolder, TEXT("build.txt")));
    FPlatformMisc::RequestExit(false, TEXT("VoxelShot"));
}
