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
#include "Random/VoxelRandom.h"
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
#include "Voxel/VoxelLightSources.h"
#include "Voxel/VoxelLights.h"
#include "Voxel/VoxelWorldSettings.h"
#include "Voxel/VoxelFluidShots.h"
#include "Voxel/VoxelFluidSystem.h"
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
    // One per EVoxelLayer: model quads draw with the cutout material in their own section.
    const TCHAR* const ChunkMaterialNames[] = {TEXT("M_ChunkOpaque"), TEXT("M_ChunkCutout"), TEXT("M_ChunkTranslucent"),
        TEXT("M_ChunkWater"), TEXT("M_ChunkCutout")};
    static_assert(UE_ARRAY_COUNT(ChunkMaterialNames) == int32(EVoxelLayer::Count), "A material per EVoxelLayer");

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
    // The torch models' mesh glow (voxel.TorchGlow) is read at Regenerate, which meshes with Table lowered to it;
    // captures set it from the start with -VoxelTorchGlow=N.
    FVoxelLights::SetTorchGlowFromCommandLine();
    // The torch lights: which states give light, and the pool of point lights around the camera.
    LightMinLevel = FVoxelLights::ReadMinLevel();
    LightTable = VoxelLightSources::MakeTable(*FFoundationBlockRegistry::GetDefault(), *Table, LightMinLevel);
    if (Lights) Lights->Reset();
    Lights = MakeShared<FVoxelLights>(this, LightComponents);
    Lights->SetOrigin(OriginCm);
    FVoxelSeed StartSeed = FVoxelSeed::Parse(UVoxelWorldSettings::Get().DevSeed);
    if (const FUnnamedSavedWorld* World = GetSavedWorld())
    {
        // The seed saved at creation; a blank text's random draw is never repeated.
        StartSeed.Value = World->Seed;
        StartSeed.Source = World->SeedText.TrimStartAndEnd().IsEmpty() ? EVoxelSeedSource::Random
                                                                       : FVoxelSeed::Parse(World->SeedText).Source;
    }
    SetupClock();
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
    FString ViewText;
    if (FParse::Value(FCommandLine::Get(), TEXT("VoxelShotView="), ViewText, false))
    {
        TArray<FString> Parts;
        ViewText.ParseIntoArray(Parts, TEXT(","));
        bool bNumeric = Parts.Num() == 6;
        for (const FString& Part : Parts)
            bNumeric = bNumeric && Part.IsNumeric();
        if (bNumeric)
            ShotView = TPair<FVector, FVector>(
                FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2])),
                FVector(FCString::Atod(*Parts[3]), FCString::Atod(*Parts[4]), FCString::Atod(*Parts[5])));
        else
            UE_LOG(
                LogUnnamedVoxel, Warning, TEXT("-VoxelShotView=%s is not CX,CY,CZ,TX,TY,TZ; no view shot"), *ViewText);
    }
    bSpawnExact = FParse::Param(FCommandLine::Get(), TEXT("VoxelSpawnExact"));
    bShotEdits = FParse::Param(FCommandLine::Get(), TEXT("VoxelShotEdits"));
    bShotGarden = FParse::Param(FCommandLine::Get(), TEXT("VoxelShotGarden"));
    bShotCaves = FParse::Param(FCommandLine::Get(), TEXT("VoxelShotCaves"));
    bShotVeins = FParse::Param(FCommandLine::Get(), TEXT("VoxelShotVeins"));
    FString VeinsAt;
    if (FParse::Value(FCommandLine::Get(), TEXT("VoxelShotVeinsAt="), VeinsAt, false))
    {
        TArray<FString> Parts;
        VeinsAt.ParseIntoArray(Parts, TEXT(","));
        for (int32 Index = 0; Index + 2 < Parts.Num(); Index += 3)
            ShotVeinTargets.Add(
                {FCString::Atoi(*Parts[Index]), FCString::Atoi(*Parts[Index + 1]), FCString::Atoi(*Parts[Index + 2])});
        bShotVeins = true;
    }
    if (!ShotFolder.IsEmpty() && FParse::Param(FCommandLine::Get(), TEXT("VoxelShotLights")))
        LightShots = MakeShared<FVoxelLightShots>(ShotFolder);
    FString FluidScenario;
    if (!ShotFolder.IsEmpty() && FParse::Value(FCommandLine::Get(), TEXT("VoxelShotFluids="), FluidScenario))
        FluidShots = FVoxelFluidShots::Make(ShotFolder, FluidScenario);
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

void AVoxelWorld::SetupClock()
{
    const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
    Clock = FVoxelWorldClock(Profile.TickSeconds, Profile.MaxCatchupSteps);
    const FUnnamedSavedWorld* World = GetSavedWorld();
    bAdvanceTime = !World || World->GetRule(TEXT("advance_time"), true);
    if (World && World->Clock)
    {
        FVoxelDayClock Saved;
        Saved.TotalTicks = World->Clock->TotalTicks;
        Saved.Rate = World->Clock->Rate;
        Saved.bPaused = World->Clock->bPaused;
        Clock.SetDayClock(Saved);
    }
    FString Pinned;
    if (FParse::Value(FCommandLine::Get(), TEXT("VoxelTime="), Pinned))
    {
        int64 Ticks = 0;
        int32 Marker = 0;
        if (FVoxelWorldClock::FindMarker(Pinned, Marker) || FVoxelWorldClock::ParseTime(Pinned, Ticks))
        {
            Clock.SetTotal(FVoxelWorldClock::FindMarker(Pinned, Marker) ? Marker : Ticks);
            Clock.SetPaused(true);
            bTimePinned = true;
        }
        else
            UE_LOG(LogUnnamedVoxel, Warning, TEXT("-VoxelTime=%s is not a time or a marker; the clock runs."), *Pinned);
    }
    UE_LOG(LogUnnamedVoxel, Display, TEXT("World clock: %s%s"), *Clock.Describe(bAdvanceTime),
        bTimePinned ? TEXT(" (pinned by -VoxelTime)") : TEXT(""));
}

void AVoxelWorld::SaveClock() const
{
    const FUnnamedSavedWorld* Opened = GetSavedWorld();
    if (!Opened || bTimePinned) return;
    FUnnamedSavedWorld World;
    FString Error;
    const FVoxelDayClock& Day = Clock.GetDayClock();
    if (UnnamedSavedWorlds::Load(Opened->Id, World, Error))
    {
        World.Clock = FUnnamedSavedWorld::FClock{Day.TotalTicks, Day.Rate, Day.bPaused};
        if (UnnamedSavedWorlds::Save(World, Error))
        {
            UE_LOG(LogUnnamedWorlds, Display, TEXT("Saved world %s's time: %lld ticks%s%s"), *World.Id, Day.TotalTicks,
                Day.bPaused ? TEXT(", paused") : TEXT(""),
                Day.Rate != 1.f ? *FString::Printf(TEXT(", rate %s"), *FString::SanitizeFloat(Day.Rate)) : TEXT(""));
            return;
        }
    }
    UE_LOG(LogUnnamedWorlds, Error, TEXT("World %s's time could not be saved (%s)."), *Opened->Id, *Error);
}

FString AVoxelWorld::DescribeDebug() const
{
    return Clock.Describe(bAdvanceTime);
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
    // The torch models glow at voxel.TorchGlow in the mesh; the light table keeps Table's level (14).
    MeshTorchGlow = FVoxelLights::ReadTorchGlow();
    MeshTable = VoxelLightSources::MakeTorchGlowTable(
        *FFoundationBlockRegistry::GetDefault(), Table.ToSharedRef(), MeshTorchGlow);
    if (MeshTable != Table)
        UE_LOG(LogUnnamedVoxel, Display, TEXT("Torch models meshed at glow %d (voxel.TorchGlow)."), MeshTorchGlow);
    Streaming->Start(*this, MeshTable.ToSharedRef(), Terrain.ToSharedRef(), Materials, Spawn);
    // A new session on a new world: nothing is loaded yet, so emptying the block world is free.
    Authority.World.OriginCm = OriginCm;
    Authority.Reset();
    // The new world's fluids start still.
    if (Fluids) Fluids->Reset();
    // Shots fly the camera far out; the rings stay around the player.
    if (!ShotFolder.IsEmpty()) Streaming->SetFocusOverride(Spawn);
}

void AVoxelWorld::SetupAuthority()
{
    const UFoundationInteractionSettings& Settings = UFoundationInteractionSettings::Get();
    Settings.BuildRules(Authority.Rules);
    Authority.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
    Authority.PlantSurvival = VoxelBlocks::GetPlantSurvival(Authority.World.GetRegistry());
    const FFoundationBlockRegistry& Registry = Authority.World.GetRegistry();
    ModelCollides.Init(false, Registry.GetStateLimit());
    TArray<FBox> Boxes;
    for (int32 Id = 0; Id < Registry.GetStateLimit(); ++Id)
        if (Registry.GetShape({uint16(Id)}) == EFoundationBlockShape::Model)
        {
            Registry.GetCollisionBoxes({uint16(Id)}, Boxes);
            ModelCollides[Id] = !Boxes.IsEmpty();
        }
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
    // Flowing water and lava (water and sky W2a) wake up from the block world's changes, under the saved world's game
    // rules (26.3's defaults for the dev seed's world).
    FVoxelFluidOptions FluidOptions;
    FluidOptions.bWaterSourceConversion = !Saved || Saved->GetRule(TEXT("water_source_conversion"), true);
    FluidOptions.bLavaSourceConversion = Saved && Saved->GetRule(TEXT("lava_source_conversion"), false);
    Fluids = MakeShared<FVoxelFluidSystem>();
    Fluids->Start(
        Authority.World, Table.ToSharedRef(),
        [Streaming = TWeakObjectPtr<UVoxelWorldSubsystem>(GetStreaming())]() -> FVoxelColumnPipeline*
        { return Streaming.IsValid() ? Streaming->GetPipeline() : nullptr; }, FluidOptions, Clock.GetGameTick());
    // What the fluid replaces pops when washed away, and the plants around it settle (W2b).
    Fluids->OnFlushed = [this](const FIntVector& Cell, FFoundationBlockState Before, bool bWashedAway)
    { Authority.SettleAfterFluid(Cell, Before, bWashedAway); };
}

FFoundationTarget AVoxelWorld::TraceTarget(FVector Eye, FVector Direction, const UFoundationProfile& Profile) const
{
    return TraceAuthority(Authority, Eye, Direction);
}

bool AVoxelWorld::GetWaterBox(const FIntVector& Cell, FBox& OutBox) const
{
    if (!Table) return false;
    // Model states holding water (kelp, seagrass) are water to swim in.
    const FVoxelStateInfo& CellInfo = Table->Get(GetState(Cell));
    if (!CellInfo.HoldsFluid()) return false;
    const FVoxelStateInfo& Info = CellInfo.HeldFluid ? Table->Get(CellInfo.HeldFluid) : CellInfo;
    const double Top = Table->Get(GetState(Cell + FIntVector(0, 0, 1))).HoldsFluid() ? 1. : Info.GetFluidHeight();
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
    if (Fluids) Fluids->Stop();
    if (UVoxelWorldSubsystem* Streaming = GetStreaming()) Streaming->Stop();
    if (Lights) Lights->Reset();
    SaveClock();
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
    // The world's game ticks for this frame (the water and sky plan, section 2): the day clock steps in each, then the
    // fluids run the same ticks within their own frame budget (W2a).
    const int32 Ticks = Clock.TakeTicks(DeltaSeconds);
    for (int32 Index = 0; Index < Ticks; ++Index)
        Clock.Step(bAdvanceTime);
    if (Fluids) Fluids->Advance(Ticks, FVoxelFluidSystem::FSettings::Read());
    if (TextureTable)
        TextureTable->SetTick(FMath::FloorToInt64(GetWorld()->GetTimeSeconds() * FoundationBlockFaces::TicksPerSecond));
    // Collision exists once the columns around the spawn do: the player is put there then, once.
    if (!bSpawned && AreSpawnColumnsGenerated())
    {
        bSpawned = true;
        ReturnPlayerToSpawn();
    }
    UpdateEdgeFog();
    if (Lights)
    {
        // A new voxel.LightMinLevel gives the meshes a new light table: columns use it from their next mesh.
        if (const int32 MinLevel = FVoxelLights::ReadMinLevel(); MinLevel != LightMinLevel && Table)
        {
            LightMinLevel = MinLevel;
            LightTable = VoxelLightSources::MakeTable(*FFoundationBlockRegistry::GetDefault(), *Table, LightMinLevel);
            if (UVoxelWorldSubsystem* Streaming = GetStreaming(); Streaming && Streaming->GetPipeline())
                Streaming->GetPipeline()->SetLightTable(LightTable);
        }
        // The torch lights gather around the camera the streaming follows; switched off, they go with or without one.
        const FVoxelLights::FSettings LightSettings = FVoxelLights::ReadSettings();
        const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
        if (Controller && Controller->PlayerCameraManager)
            Lights->Update(Controller->PlayerCameraManager->GetCameraLocation(), LightSettings);
        else if (!LightSettings.bLive)
            Lights->Update(OriginCm, LightSettings);
    }
    if (ShotEditStage == 1) ++ShotEditFrames;
    if (!ShotFolder.IsEmpty() && IsReady())
    {
        // With -VoxelShotEdits or -VoxelShotGarden the shots wait until the edited columns are back on screen.
        if ((bShotEdits || bShotGarden) && ShotEditStage == 0)
        {
            if (bShotEdits) ApplyShotEdits();
            if (bShotGarden) ApplyShotGarden();
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
        // With -VoxelShotLights the light probe runs first (FVoxelLightShots), then with -VoxelShotFluids the flowing
        // water capture (FVoxelFluidShots).
        else if ((!LightShots || !LightShots->Tick(*this)) && (!FluidShots || !FluidShots->Tick(*this)))
            UpdateShots();
    }
    // A running fluid capture's writes remesh columns, so it also runs while they are off screen.
    else if (FluidShots && FluidShots->IsRunning())
        FluidShots->Tick(*this);
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
                        const uint16 State = Column->GetState(X - ColumnX * Size, Y - ColumnY * Size, Z);
                        const FVoxelStateInfo& Info = Table->Get(State);
                        if (Info.Shape == EVoxelShape::Model)
                        {
                            // Model states collide with their facts boxes, moved where the facts move them (cactus,
                            // bamboo, lily pads, carpets); bSolid (blocks_motion) is for the heightmaps.
                            if (State < ModelCollides.Num() && ModelCollides.GetData()[State])
                                Authority.World.GetCollisionBoxes({X, Y, Z}, FFoundationBlockState{State}, Out);
                            continue;
                        }
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
    if (Lights) Lights->SetOrigin(OriginCm);
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

void AVoxelWorld::ApplyShotGarden()
{
    UVoxelWorldSubsystem* Streaming = GetStreaming();
    const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
    FFoundationBlockWorld& World = Authority.World;
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    auto Find = [&World](const TCHAR* Id)
    {
        const FFoundationBlockType* Type = World.FindBlock(Id);
        return Type ? Type->GetDefaultState() : FFoundationBlockState{};
    };
    const FFoundationBlockState Grass = Find(TEXT("grass_block")), Dirt = Find(TEXT("dirt")),
                                Stone = Find(TEXT("stone")), Water = Find(TEXT("water")),
                                ShortGrass = Find(TEXT("short_grass")), Dandelion = Find(TEXT("dandelion")),
                                Poppy = Find(TEXT("poppy")), TallGrass = Find(TEXT("tall_grass")),
                                LeafLitter = Find(TEXT("leaf_litter")), Lichen = Find(TEXT("glow_lichen")),
                                Dripstone = Find(TEXT("pointed_dripstone")), LilyPad = Find(TEXT("lily_pad")),
                                Pickle = Find(TEXT("sea_pickle"));
    if (!Pipeline || !Grass.Id || !Dirt.Id || !Stone.Id || !Water.Id || !ShortGrass.Id || !Lichen.Id) return;
    constexpr int32 Size = VoxelConstants::ColumnSize;
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
    auto With = [&Registry](FFoundationBlockState State, const TCHAR* Property, const TCHAR* Value)
    { return Registry.WithValue(State, Property, Value); };
    // A plant and, for a double plant, its upper half above it.
    auto Plant = [&](const FIntVector& Cell, FFoundationBlockState State)
    {
        Write(Cell, State);
        if (Registry.GetValue(State, TEXT("half")) == TEXT("lower"))
            Write(Cell + FIntVector(0, 0, 1), With(State, TEXT("half"), TEXT("upper")));
    };

    // The plot: 72 x 20 cells east and south of the spawn, its rows crossing a column border. Its floor is one above
    // the highest ground under it, built up from the ground, so no slope walls it in and the distance shots see it.
    const FIntVector Spawn = GetSpawnCell();
    GardenOrigin = FIntVector(FoundationGrid::FloorDivide(Spawn.X, Size) * Size + Size - 7, Spawn.Y + 4, 0);
    int32 Floor = VoxelConstants::MinZ;
    for (int32 Y = GardenOrigin.Y; Y < GardenOrigin.Y + 20; ++Y)
        for (int32 X = GardenOrigin.X; X < GardenOrigin.X + 72; ++X)
            Floor = FMath::Max(Floor, Surface(X, Y) + 1);
    Floor = FMath::Min(Floor, VoxelConstants::MaxZ - 24);
    GardenOrigin.Z = Floor;
    const FIntVector& O = GardenOrigin;
    for (int32 Y = O.Y; Y < O.Y + 20; ++Y)
        for (int32 X = O.X; X < O.X + 72; ++X)
        {
            for (int32 Z = FMath::Min(Surface(X, Y), Floor - 3); Z < Floor; ++Z)
                Write({X, Y, Z}, Dirt);
            Write({X, Y, Floor}, Grass);
            for (int32 Z = Floor + 1; Z <= Floor + 20; ++Z)
                Write({X, Y, Z}, FFoundationBlockState{});
        }
    // Rows of every model block's default state in registry order, two cells apart; the water-holding ones stand
    // in the pool along the south edge, with lily pads on it and sea pickles of every count.
    for (int32 Y = O.Y + 15; Y < O.Y + 19; ++Y)
        for (int32 X = O.X + 1; X < O.X + 47; ++X)
        {
            Write({X, Y, Floor - 2}, Stone);
            Write({X, Y, Floor - 1}, Water);
            Write({X, Y, Floor}, Water);
        }
    int32 Land = 0, Wet = 0;
    for (int32 Index = 1; Index < Registry.GetBlockCount(); ++Index)
    {
        const FFoundationBlockState State = Registry.GetBlockAt(Index).GetDefaultState();
        if (Registry.GetShape(State) != EFoundationBlockShape::Model) continue;
        if (Registry.GetFluid(State).Amount > 0)
        {
            Plant(FIntVector(O.X + 2 + Wet % 22 * 2, O.Y + 16 + Wet / 22, Floor - 1), State);
            ++Wet;
            continue;
        }
        Plant(FIntVector(O.X + 1 + Land % 23 * 2, O.Y + 2 + Land / 23 * 3, Floor + 1), State);
        ++Land;
    }
    for (int32 Index = 0; Index < 12 && LilyPad.Id; ++Index)
        Write(FIntVector(O.X + 2 + Index * 2, O.Y + 18, Floor + 1), LilyPad);
    for (int32 Index = 0; Index < 12 && Pickle.Id; ++Index)
        Write(FIntVector(O.X + 25 + Index * 2, O.Y + 18, Floor - 1),
            With(Pickle, TEXT("pickles"), *LexToString(1 + Index % 4)));
    // The multiface overhang: a stone roof and back wall; glow lichen (x 49-50), sculk vein (51-52) and vine (53) on
    // the ceiling (up), on the wall (south) and on the floor (down, not for vine), so a look call sees the override
    // turned and unturned side by side, with and without emission.
    for (int32 X = O.X + 48; X <= O.X + 54; ++X)
    {
        for (int32 Y = O.Y + 2; Y <= O.Y + 7; ++Y)
            Write({X, Y, Floor + 4}, Stone);
        for (int32 Z = Floor + 1; Z <= Floor + 3; ++Z)
            Write({X, O.Y + 7, Z}, Stone);
    }
    auto Multiface = [&](FFoundationBlockState State, int32 X)
    {
        if (!State.Id) return;
        for (const TCHAR* Face : {TEXT("up"), TEXT("down"), TEXT("north"), TEXT("south"), TEXT("east"), TEXT("west")})
            if (!Registry.GetValue(State, Face).IsNone()) State = With(State, Face, TEXT("false"));
        Write({X, O.Y + 4, Floor + 3}, With(State, TEXT("up"), TEXT("true")));
        Write({X, O.Y + 6, Floor + 2}, With(State, TEXT("south"), TEXT("true")));
        if (!Registry.GetValue(State, TEXT("down")).IsNone())
        {
            Write({X, O.Y + 3, Floor + 1}, With(State, TEXT("down"), TEXT("true")));
            // And in the sun, in front of the overhang, where the floor variant's shading shows.
            Write({X, O.Y, Floor + 1}, With(State, TEXT("down"), TEXT("true")));
        }
    };
    for (const int32 X : {49, 50})
        Multiface(Lichen, O.X + X);
    for (const int32 X : {51, 52})
        Multiface(Find(TEXT("sculk_vein")), O.X + X);
    Multiface(Find(TEXT("vine")), O.X + 53);
    // Amethyst clusters on the back wall's sunny side, facing south: their cross is shaded as its turned override
    // (south), or as up with voxel.Debug.ModelShadeUnturned 1.
    if (const FFoundationBlockState Cluster = Find(TEXT("amethyst_cluster")); Cluster.Id)
        for (int32 X = O.X + 49; X <= O.X + 53; ++X)
            Write({X, O.Y + 8, Floor + 2}, With(Cluster, TEXT("facing"), TEXT("south")));
    // A dripstone column: three stacked on the floor share one offset.
    for (int32 Z = Floor + 1; Z <= Floor + 3 && Dripstone.Id; ++Z)
        Write({O.X + 57, O.Y + 4, Z}, Dripstone);
    // Tall grass across a section border: its lower half in a section's last row, its upper half in the next one's
    // first, on a stone post.
    GardenBorderZ = VoxelConstants::MinZ +
        ((Floor + 3 - VoxelConstants::MinZ) / VoxelConstants::SectionSize + 1) * VoxelConstants::SectionSize;
    for (int32 Z = Floor + 1; Z <= GardenBorderZ - 2; ++Z)
        Write({O.X + 60, O.Y + 4, Z}, Stone);
    if (TallGrass.Id) Plant({O.X + 60, O.Y + 4, GardenBorderZ - 1}, TallGrass);
    // The distance plot: 3 x 3 patches of short grass, dandelions and leaf litter side by side, for shots from 8 to
    // 128 blocks away along -X.
    const FFoundationBlockState Patches[3] = {ShortGrass, Dandelion, LeafLitter};
    for (int32 Patch = 0; Patch < 3; ++Patch)
        for (int32 DY = 0; DY < 3; ++DY)
            for (int32 DX = 0; DX < 3; ++DX)
                if (Patches[Patch].Id) Write({O.X + 64 + DX, O.Y + 8 + Patch * 4 + DY, Floor + 1}, Patches[Patch]);
    // The field: plants on every grass block within 36 cells of the clearing, south of the plot; the clearing (7 x 7)
    // stays bare, so its ground is the same pixels with plants on and off (capture G3).
    GardenClearing = FIntVector(O.X + 30, O.Y + 60, 0);
    GardenClearing.Z = Surface(GardenClearing.X, GardenClearing.Y);
    int32 FieldPlants = 0;
    for (int32 Y = GardenClearing.Y - 36; Y <= GardenClearing.Y + 36; ++Y)
        for (int32 X = GardenClearing.X - 36; X <= GardenClearing.X + 36; ++X)
        {
            if (FMath::Abs(X - GardenClearing.X) <= 3 && FMath::Abs(Y - GardenClearing.Y) <= 3) continue;
            if (Y < O.Y + 21) continue;
            const int32 Top = Surface(X, Y);
            if (!World.IsKnown({X, Y, Top}) || !World.IsKnown({X, Y, Top + 1})) continue;
            if (World.Get({X, Y, Top}).State != Grass || !World.Get({X, Y, Top + 1}).State.IsAir()) continue;
            const uint32 Pick = uint32(VoxelRandom::Hash64(0, VoxelRandom::Salt("shot garden"), X, Y) >> 40) % 100;
            Plant({X, Y, Top + 1}, Pick < 85 ? ShortGrass : Pick < 90 ? Dandelion : Pick < 95 ? Poppy : TallGrass);
            ++FieldPlants;
        }
    // After the edits' line when both run.
    ShotEditReport += FString::Printf(
        TEXT("%s  Garden: %d cells in %d columns from (%d, %d, %d): %d model blocks in rows, ")
            TEXT("%d in the pool, %d field plants around the clearing (%d, %d, %d), section border at Z %d"),
        ShotEditReport.IsEmpty() ? TEXT("") : TEXT("\n"), Changed, Columns.Num(), O.X, O.Y, Floor, Land, Wet,
        FieldPlants, GardenClearing.X, GardenClearing.Y, GardenClearing.Z, GardenBorderZ);
}

void AVoxelWorld::FindVeinShots()
{
    bShotVeinsSearched = true;
    const UVoxelWorldSubsystem* Streaming = GetStreaming();
    const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
    if (!Pipeline || !Table) return;
    constexpr int32 Size = VoxelConstants::ColumnSize, Wide = 32, High = 18, Zs = VoxelConstants::Height;
    const FVoxelStreamingRings& Rings = Pipeline->GetRings();
    // The drawn area reaches one column past the view radius; its four cut faces are its outermost blocks.
    const int32 Radius = Rings.ViewRadius + 1, Along = (2 * Radius + 1) * Size;
    const FIntPoint Low = (Rings.Center - FIntPoint(Radius)) * Size;
    const FIntPoint High2 = (Rings.Center + FIntPoint(Radius)) * Size + FIntPoint(Size - 1);
    // Face 0 west, 1 east, 2 north, 3 south: the plane's coordinate, and where the camera looks from.
    const auto Cell = [&](int32 Face, int32 Index) -> FIntPoint
    {
        return Face == 0 ? FIntPoint(Low.X, Low.Y + Index)
            : Face == 1  ? FIntPoint(High2.X, Low.Y + Index)
            : Face == 2  ? FIntPoint(Low.X + Index, Low.Y)
                         : FIntPoint(Low.X + Index, High2.Y);
    };
    TArray<uint16> Faces;
    Faces.Init(0, 4 * Along * Zs);
    for (int32 Face = 0; Face < 4; ++Face)
        for (int32 Index = 0; Index < Along; ++Index)
        {
            const FIntPoint At = Cell(Face, Index);
            const FIntPoint ColumnAt(
                FMath::FloorToInt32(double(At.X) / Size), FMath::FloorToInt32(double(At.Y) / Size));
            if (const FVoxelColumn* Column = Pipeline->FindColumn(ColumnAt))
                for (int32 Z = 0; Z < Zs; ++Z)
                    Faces[(Face * Along + Index) * Zs + Z] =
                        Column->GetState(At.X - ColumnAt.X * Size, At.Y - ColumnAt.Y * Size, Z + VoxelConstants::MinZ);
        }
    const auto StateAt = [&](int32 Face, int32 Index, int32 Z)
    {
        return Index < 0 || Index >= Along || Z < VoxelConstants::MinZ || Z > VoxelConstants::MaxZ
            ? uint16(0)
            : Faces[(Face * Along + Index) * Zs + Z - VoxelConstants::MinZ];
    };
    struct FVein
    {
        const TCHAR* Name;
        const TCHAR* Names[3];
        int32 Bottom, Top;
    };
    const FVein Veins[2] = {
        {TEXT("vein-copper"), {TEXT("copper_ore"), TEXT("raw_copper_block"), TEXT("granite")}, 0, 49},
        {TEXT("vein-iron"), {TEXT("deepslate_iron_ore"), TEXT("raw_iron_block"), TEXT("tuff")}, -60, -9}};
    const TCHAR* const Counted[] = {TEXT("copper_ore"), TEXT("raw_copper_block"), TEXT("granite"),
        TEXT("deepslate_iron_ore"), TEXT("raw_iron_block"), TEXT("tuff"), TEXT("stone"), TEXT("deepslate")};
    const TCHAR* const FaceNames[] = {TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south")};
    auto Blocks = [this](double X, double Y, double Z) { return OriginCm + FVector(X, Y, Z) * CellCm; };
    FString Aim;
    for (int32 Kind = 0; Kind < 2; ++Kind)
    {
        const FVein& Vein = Veins[Kind];
        const uint16 Ore = Table->FindState(Vein.Names[0]), Raw = Table->FindState(Vein.Names[1]),
                     Filler = Table->FindState(Vein.Names[2]);
        // A vein mixes its ore into its filler, which an ore blob in stone or a filler blob does not: ore beside
        // filler scores, raw ore (only veins make it) scores most.
        const auto Score = [&](int32 Face, int32 Index, int32 Z)
        {
            const uint16 State = StateAt(Face, Index, Z);
            if (State == Raw) return 10;
            if (State != Ore && State != Filler) return 0;
            const uint16 Other = State == Ore ? Filler : Ore;
            const bool bMixed = StateAt(Face, Index - 1, Z) == Other || StateAt(Face, Index + 1, Z) == Other ||
                StateAt(Face, Index, Z - 1) == Other || StateAt(Face, Index, Z + 1) == Other;
            return bMixed ? (State == Ore ? 3 : 1) : 0;
        };
        int32 Face = 0, Best = -1;
        FIntPoint Centre(Along / 2, (Vein.Bottom + Vein.Top) / 2);
        if (Kind < ShotVeinTargets.Num())
        {
            Face = FMath::Clamp(ShotVeinTargets[Kind].Face, 0, 3);
            Centre = {FMath::Clamp(ShotVeinTargets[Kind].Along, Wide / 2, Along - Wide / 2), ShotVeinTargets[Kind].Z};
        }
        else
            for (int32 Candidate = 0; Candidate < 4; ++Candidate)
                for (int32 A0 = 0; A0 + Wide <= Along; A0 += 2)
                    for (int32 Z0 = Vein.Bottom - 4; Z0 + High <= Vein.Top + 5; Z0 += 2)
                    {
                        int32 Sum = 0;
                        for (int32 A = A0; A < A0 + Wide; ++A)
                            for (int32 Z = Z0; Z < Z0 + High; ++Z)
                                Sum += Score(Candidate, A, Z);
                        if (Sum <= Best) continue;
                        Best = Sum;
                        Face = Candidate;
                        Centre = {A0 + Wide / 2, Z0 + High / 2};
                    }
        FString Counts;
        for (const TCHAR* Name : Counted)
        {
            const uint16 State = Table->FindState(Name);
            int32 Count = 0;
            for (int32 A = Centre.X - Wide / 2; A < Centre.X + Wide / 2; ++A)
                for (int32 Z = Centre.Y - High / 2; Z < Centre.Y + High / 2; ++Z)
                    Count += StateAt(Face, A, Z) == State;
            Counts += FString::Printf(TEXT("%s%s %d"), Counts.IsEmpty() ? TEXT("") : TEXT(", "), Name, Count);
        }
        const FIntPoint Target = Cell(Face, Centre.X);
        ShotVeinReport += FString::Printf(TEXT("  %s: %s cut, window %d x %d centred on block (%d, %d, %d) (%s): %s\n"),
            Vein.Name, FaceNames[Face], Wide, High, Target.X, Target.Y, Centre.Y,
            *(Kind < ShotVeinTargets.Num() ? FString(TEXT("fixed")) : FString::Printf(TEXT("found, score %d"), Best)),
            *Counts);
        Aim += FString::Printf(TEXT("%s%d,%d,%d"), Aim.IsEmpty() ? TEXT("") : TEXT(","), Face, Centre.X, Centre.Y);
        // From outside the face, 24 blocks away, looking straight at it.
        const FIntPoint Out = Face == 0 ? FIntPoint(-24, 0)
            : Face == 1                 ? FIntPoint(24, 0)
            : Face == 2                 ? FIntPoint(0, -24)
                                        : FIntPoint(0, 24);
        ShotCaveViews.Emplace(Vein.Name, Blocks(Target.X + .5 + Out.X, Target.Y + .5 + Out.Y, Centre.Y + .5),
            Blocks(Target.X + .5, Target.Y + .5, Centre.Y + .5));
    }
    ShotVeinReport += FString::Printf(TEXT("  Vein shots: -VoxelShotVeinsAt=%s\n"), *Aim);
    UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *ShotVeinReport);
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
    if (ShotView)
        Shots.Add({TEXT("view"), Blocks(ShotView->Key.X, ShotView->Key.Y, ShotView->Key.Z),
            Blocks(ShotView->Value.X, ShotView->Value.Y, ShotView->Value.Z), false, true});

    if (bShotGarden)
    {
        const FIntVector& O = GardenOrigin;
        const double F = O.Z;
        Shots.Append({
            // The rows from above their west end.
            {TEXT("garden"), Blocks(O.X + 1, O.Y + 1, F + 9), Blocks(O.X + 16, O.Y + 7, F), false, true},
            // A cross at a grazing angle, then the first row low down.
            {TEXT("garden-close"), Blocks(O.X + 0.5, O.Y + 0.4, F + 1.5), Blocks(O.X + 9, O.Y + 3, F + 1.3), false,
                true},
            {TEXT("garden-pool"), Blocks(O.X + 14, O.Y + 11, F + 6), Blocks(O.X + 22, O.Y + 17, F), false, true},
            {TEXT("garden-lichen"), Blocks(O.X + 51.5, O.Y - 7, F + 3), Blocks(O.X + 51.5, O.Y + 6, F + 1.5), false,
                true},
            {TEXT("garden-amethyst"), Blocks(O.X + 51.5, O.Y + 13, F + 2.6), Blocks(O.X + 51.5, O.Y + 8, F + 2.3),
                false, true},
            {TEXT("garden-border"), Blocks(O.X + 55, O.Y - 2, GardenBorderZ + 1),
                Blocks(O.X + 59, O.Y + 4.5, GardenBorderZ - 1), false, true},
            // The field at eye height (GPU), and looking down onto its bare clearing (ground brightness).
            {TEXT("field"), Blocks(GardenClearing.X - 30.5, GardenClearing.Y - 30.5, GardenClearing.Z + 2.62),
                Blocks(GardenClearing.X, GardenClearing.Y, GardenClearing.Z + 1), false, true},
            {TEXT("field-clearing"), Blocks(GardenClearing.X + .5, GardenClearing.Y - 6.5, GardenClearing.Z + 6),
                Blocks(GardenClearing.X + .5, GardenClearing.Y + .5, GardenClearing.Z + 1), false, true},
        });
        // The distance plot from 8 to 128 blocks along -X, 14 degrees down, so its footprint scales with 1 / d^2.
        static const TCHAR* const DistanceNames[] = {TEXT("distance-008"), TEXT("distance-016"), TEXT("distance-032"),
            TEXT("distance-064"), TEXT("distance-128")};
        const FVector Plot = Blocks(O.X + 65.5, O.Y + 13.5, F + 1.3);
        for (int32 Step = 0; Step < 5; ++Step)
        {
            const double Distance = 8 << Step;
            const FVector Offset =
                FVector(-FMath::Cos(FMath::DegreesToRadians(14.)), 0, FMath::Sin(FMath::DegreesToRadians(14.))) *
                Distance * CellCm;
            Shots.Add({DistanceNames[Step], Plot + Offset, Plot, false, true});
        }
    }
    if (bShotCaves && !bShotCavesSearched) FindCaveShots();
    if (bShotVeins && !bShotVeinsSearched) FindVeinShots();
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
    const FString Edits = (ShotEditReport.IsEmpty() ? FString() : ShotEditReport + TEXT("\n")) + ShotVeinReport;
    FFileHelper::SaveStringToFile(GetBuildReport() + TEXT("\n") + Edits + Lighting + TEXT("\n") + Gpu + TEXT("\n") +
            World + TEXT("\n") + Player + TEXT("\n"),
        *FPaths::Combine(ShotFolder, TEXT("build.txt")));
    FPlatformMisc::RequestExit(false, TEXT("VoxelShot"));
}
